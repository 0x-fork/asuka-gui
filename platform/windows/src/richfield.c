// richfield.c: a Rich TextField. It's a rich edit control (msftedit's
// RICHEDIT50W) instead of an EDIT. Its styled text goes in and comes out
// as RTF (rtf.c): links are the control's friendly links (HYPERLINK
// fields), which only RTF carries. Ctrl+B, Ctrl+I and Ctrl+U are the
// control's own; the Format command sets the selection's character
// format, undoably, as they do. A span's colors come back as they were
// sent (a semantic color stays one, drawn again as the theme changes); a
// color the user gave, as drawn. Its text style and size are the field's.
#include "nav.h"

#include <richedit.h>

#include "rtf.h"

#define MONO_FACE "Cascadia Mono"

typedef struct {
	int32_t start, end;  // UTF-16
	char *link;          // NULL: none
} field_run;

struct wh_field {
	uint8_t *content;  // the Rich prop, as the core sent it
	uint32_t content_len;
	// Each color as drawn, and the color it was sent as.
	struct {
		rtf_color drawn;
		ak_color sent;
	} * colors;
	uint32_t ncolors, capcolors;
	char face[LF_FACESIZE * 3];
	rtf_doc doc;
	field_run *runs;  // the text's runs and where they are, for its formats
	uint32_t nruns, capruns;
	bool formats_known;
	uint8_t formats;
	char *format_link;
};

static wh_field *field_of(node *n) {
	if (!n->field) n->field = wh_alloc(sizeof *n->field);
	return n->field;
}

static void forget_runs(wh_field *f) {
	for (uint32_t i = 0; i < f->nruns; i++) free(f->runs[i].link);
	f->nruns = 0;
}

void wh_field_free(node *n) {
	wh_field *f = n->field;
	if (!f) return;
	forget_runs(f);
	free(f->runs);
	free(f->colors);
	free(f->content);
	free(f->format_link);
	free(f);
	n->field = NULL;
}

// msftedit is the rich edit's library, loaded once.
void wh_load_msftedit(void) {
	static bool loaded;
	if (!loaded) {
		loaded = true;
		LoadLibraryExW(L"msftedit.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	}
}

// wh_rich_field_class is the class a field's control is made of: a Rich
// field's rich edit, else an EDIT.
const wchar_t *wh_rich_field_class(node *n) {
	if (!n->rich_text) return L"EDIT";
	wh_load_msftedit();
	return MSFTEDIT_CLASS;
}

// stream_out is the control's RTF: all of it, or its selection's (free
// out->p).
typedef struct {
	rtf_buf *b;
} stream_ctx;

static DWORD CALLBACK stream_out_cb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb) {
	rtf_buf *b = (rtf_buf *)cookie;
	if (cb > 0) {
		if (b->n + (size_t)cb + 1 > b->cap) {
			b->cap = (b->n + (size_t)cb + 1) * 2;
			b->p = realloc(b->p, b->cap);
			if (!b->p) abort();
		}
		memcpy(b->p + b->n, buf, (size_t)cb);
		b->n += (size_t)cb;
		b->p[b->n] = 0;
	}
	*pcb = cb;
	return 0;
}

static void stream_out(HWND ctl, bool selection, rtf_buf *out) {
	out->n = 0;
	EDITSTREAM es = {.dwCookie = (DWORD_PTR)out, .pfnCallback = stream_out_cb};
	SendMessageW(ctl, EM_STREAMOUT, SF_RTF | (selection ? SFF_SELECTION : 0), (LPARAM)&es);
	// Some versions of msftedit end the stream with a NUL.
	while (out->n && out->p[out->n - 1] == 0) out->n--;
}

// rich_doc is the field's defaults as RTF has them: its font, size and
// color.
static void rich_doc(node *n, wh_field *f) {
	font_spec s = wh_node_spec(n);
	rgba c;
	if (!wh_resolve(n->color, &c)) c = H.theme.label;
	wchar_t buf[LF_FACESIZE];
	const wchar_t *face = wh_face(&s, buf, LF_FACESIZE);
	WideCharToMultiByte(CP_UTF8, 0, face, -1, f->face, sizeof f->face, NULL, NULL);
	// The control's twips, as RichText's: half points at the system's DPI.
	f->doc = (rtf_doc){f->face, MONO_FACE, (float)wh_twips(s.size, s.k) / 20, {c.r, c.g, c.b}};
}

// keep notes the color a color sent is drawn in.
static void keep(wh_field *f, rtf_color c, ak_color sent) {
	for (uint32_t i = 0; i < f->ncolors; i++)
		if (f->colors[i].drawn.r == c.r && f->colors[i].drawn.g == c.g && f->colors[i].drawn.b == c.b) return;
	if (f->ncolors == f->capcolors) {
		f->capcolors = f->capcolors ? f->capcolors * 2 : 8;
		f->colors = realloc(f->colors, f->capcolors * sizeof *f->colors);
		if (!f->colors) abort();
	}
	f->colors[f->ncolors].drawn = c;
	f->colors[f->ncolors++].sent = sent;
}

// rtf_run_of is a run of the core's styled text as RTF writes it (its
// text and link the run's: not copied).
static rtf_run rtf_run_of(wh_field *f, const ak_rich_run *r) {
	const ak_rich_style *s = &r->style;
	rtf_run out = {
		.text = (char *)r->text.p,
		.len = r->text.len,
		.bold = s->weight >= AK_FONT_WEIGHT_SEMIBOLD,
		.italic = s->flags & AK_RICH_ITALIC,
		.mono = s->flags & AK_RICH_MONO,
		.underline = s->flags & AK_RICH_UNDERLINE,
		.strike = s->flags & AK_RICH_STRIKE,
	};
	if (s->link.len) out.link = wh_strndup(s->link.p, s->link.len);
	rgba c;
	if (wh_resolve(s->color, &c)) {
		out.has_color = true;
		out.color = (rtf_color){c.r, c.g, c.b};
		keep(f, out.color, s->color);
	}
	if (wh_resolve(s->background, &c) && c.a > 0) {
		out.has_bg = true;
		out.bg = (rtf_color){c.r, c.g, c.b};
		keep(f, out.bg, s->background);
	}
	return out;
}

// sent_color is a color the control gave as it was sent, where it's one
// of those (the user's own as it's drawn).
static ak_color sent_color(wh_field *f, rtf_color c) {
	for (uint32_t i = 0; i < f->ncolors; i++)
		if (f->colors[i].drawn.r == c.r && f->colors[i].drawn.g == c.g && f->colors[i].drawn.b == c.b) return f->colors[i].sent;
	return ak_rgba(c.r, c.g, c.b, 255);
}

// core_run is a run the control gave as the core's styled text has it.
static ak_rich_run core_run(wh_field *f, const rtf_run *r) {
	ak_rich_run out = {0};
	ak_rich_style *s = &out.style;
	if (r->bold) s->weight = AK_FONT_WEIGHT_BOLD;
	s->flags = (uint8_t)((r->italic ? AK_RICH_ITALIC : 0) | (r->mono ? AK_RICH_MONO : 0) | (r->underline ? AK_RICH_UNDERLINE : 0) |
		(r->strike ? AK_RICH_STRIKE : 0));
	if (r->has_color) s->color = sent_color(f, r->color);
	if (r->has_bg) s->background = sent_color(f, r->bg);
	if (r->link) s->link = (ak_span){r->link, strlen(r->link)};
	out.text = (ak_span){r->text, r->len};
	return out;
}

// wh_visible_text is a rich edit's text without its hidden text (a link's
// field instruction, HYPERLINK "url", which WM_GETTEXT includes): its
// UTF-16 units in out (cap of them, with the NUL).
int wh_visible_text(node *n, wchar_t *out, int cap) {
	if (cap <= 0) return 0;
	GETTEXTEX gt = {.cb = (DWORD)cap * sizeof(wchar_t), .flags = GT_NOHIDDENTEXT, .codepage = 1200};
	LRESULT got = SendMessageW(n->ctl, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)out);
	if (got < 0) got = 0;
	if (got >= cap) got = cap - 1;
	out[got] = 0;
	return (int)got;
}

// plain is the field's text, its lines ended by \n (a rich edit's end in
// \r): into n->text; its UTF-16 length.
static int plain(node *n) {
	int len = GetWindowTextLengthW(n->ctl) + 1;  // at least as long, hidden text and all
	wchar_t *s = wh_alloc((size_t)len * sizeof *s);
	len = wh_visible_text(n, s, len);
	int k = 0;
	for (int i = 0; i < len; i++) {
		if (s[i] == '\r' && i + 1 < len && s[i + 1] == '\n') continue;
		s[k++] = s[i] == '\r' ? '\n' : s[i];
	}
	wstr_setw(&n->text, s, k);
	free(s);
	return k;
}

// read_runs reads the field's runs (its RTF) and keeps where they are, for
// its formats (free out with rtf_runs_free).
static void read_runs(node *n, wh_field *f, rtf_runs *out) {
	rtf_buf b = {0};
	stream_out(n->ctl, false, &b);
	rtf_parse(b.p ? b.p : "", b.n, &f->doc, out);
	free(b.p);
	// A rich edit's document ends with a paragraph mark of its own, which
	// isn't the field's text: the runs end where its visible text does.
	rtf_trim(out, (size_t)plain(n));
	forget_runs(f);
	int32_t at = 0;
	for (uint32_t i = 0; i < out->n; i++) {
		int32_t l = (int32_t)rtf_utf16_len(out->v[i].text, out->v[i].len);
		if (f->nruns == f->capruns) {
			f->capruns = f->capruns ? f->capruns * 2 : 8;
			f->runs = realloc(f->runs, f->capruns * sizeof *f->runs);
			if (!f->runs) abort();
		}
		f->runs[f->nruns++] = (field_run){at, at + l, out->v[i].link ? wh_strndup(out->v[i].link, strlen(out->v[i].link)) : NULL};
		at += l;
	}
}

// formats_now is the styles at the cursor (what's typed next: the
// control's insertion format) or that all the selection has, and its link
// (the field's: not a copy).
static uint8_t formats_now(node *n, wh_field *f, const char **link) {
	CHARFORMAT2W cf = {.cbSize = sizeof cf};
	SendMessageW(n->ctl, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
	uint8_t fl = 0;
#define ALL(bit) ((cf.dwMask & (bit)) && (cf.dwEffects & (bit)))
	if (ALL(CFM_BOLD)) fl |= 1;
	if (ALL(CFM_ITALIC)) fl |= 2;
	if (ALL(CFM_UNDERLINE)) fl |= 4;
	if (ALL(CFM_STRIKEOUT)) fl |= 8;
#undef ALL
	if ((cf.dwMask & CFM_FACE) && _wcsicmp(cf.szFaceName, L"" MONO_FACE) == 0) fl |= 16;
	CHARRANGE sel = {0};
	SendMessageW(n->ctl, EM_EXGETSEL, 0, (LPARAM)&sel);
	const char *l = NULL;
	bool first = true;
	for (uint32_t i = 0; i < f->nruns; i++) {
		field_run *r = &f->runs[i];
		bool in = (sel.cpMin == sel.cpMax && sel.cpMin > r->start && sel.cpMin <= r->end) ||
			(sel.cpMin != sel.cpMax && r->end > sel.cpMin && r->start < sel.cpMax);
		if (!in) continue;
		const char *rl = r->link ? r->link : "";
		if (first) l = rl, first = false;
		else if (strcmp(l, rl) != 0) l = "";
	}
	*link = l ? l : "";
	return fl;
}

// report_formats sends Formats if they changed.
static void report_formats(node *n) {
	if (!n->rich_text || !n->ctl || !node_listening(n, AK_EVENT_FORMATS)) return;
	wh_field *f = field_of(n);
	const char *link;
	uint8_t fl = formats_now(n, f, &link);
	if (f->formats_known && fl == f->formats && strcmp(link, f->format_link ? f->format_link : "") == 0) return;
	f->formats_known = true;
	f->formats = fl;
	free(f->format_link);
	f->format_link = wh_strndup(link, strlen(link));
	size_t ln = strlen(link);
	uint8_t *b = wh_alloc(5 + ln);
	b[0] = fl;
	uint32_t l32 = (uint32_t)ln;
	memcpy(b + 1, &l32, 4);  // little-endian, as every Windows is
	memcpy(b + 5, link, ln);
	wh_emit_bytes(n, AK_EVENT_FORMATS, b, 5 + ln);
	free(b);
}

// set_text_ex puts RTF in the control: all its text, or the selection's
// (keeping the undo stack).
static void set_text_ex(node *n, const rtf_buf *rtf, bool selection) {
	SETTEXTEX st = {selection ? ST_SELECTION | ST_KEEPUNDO : ST_DEFAULT, CP_ACP};
	SendMessageW(n->ctl, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)rtf->p);
}

// wh_set_rich_text is the Rich prop: the field's styled text, as the
// core's styled text.
void wh_set_rich_text(node *n, const uint8_t *b, size_t len) {
	wh_field *f = field_of(n);
	if (b != f->content) {
		uint8_t *c = wh_alloc(len ? len : 1);
		memcpy(c, b, len);
		free(f->content);
		f->content = c;
		f->content_len = (uint32_t)len;
	}
	if (!n->rich_text) {
		n->rich_text = true;
		wh_make_edit(n);  // a rich edit now; wh_setup_rich_edit sets the text
		wh_theme_control(n);
		return;
	}
	if (!n->ctl) return;
	// Each color as drawn now, and the color it was sent as: what the
	// control gives back in it is that color again (a semantic one stays
	// semantic, and follows the theme).
	f->ncolors = 0;
	ak_rich_run_vec core = {0};
	ak_decode_rich(f->content, f->content_len, &core);
	rtf_run *runs = wh_alloc((core.len + 1) * sizeof *runs);
	uint32_t nr = 0;
	for (uint32_t i = 0; i < core.len; i++) {
		if (core.data[i].style.flags & AK_RICH_INLINE) continue;  // inline components aren't editable
		runs[nr++] = rtf_run_of(f, &core.data[i]);
	}
	rich_doc(n, f);
	rtf_buf data = {0};
	rtf_write(runs, nr, &f->doc, &data);
	for (uint32_t i = 0; i < nr; i++) free(runs[i].link);
	free(runs);
	ak_vec_free(&core);
	CHARRANGE sel = {0};
	SendMessageW(n->ctl, EM_EXGETSEL, 0, (LPARAM)&sel);
	n->setting_text = true;
	set_text_ex(n, &data, false);
	n->setting_text = false;
	free(data.p);
	rtf_runs got = {0};
	read_runs(n, f, &got);
	rtf_runs_free(&got);
	LONG length = (LONG)n->text.n;
	sel.cpMin = sel.cpMin < length ? sel.cpMin : length;
	sel.cpMax = sel.cpMax < length ? sel.cpMax : length;
	SendMessageW(n->ctl, EM_EXSETSEL, 0, (LPARAM)&sel);
	report_formats(n);
}

// wh_setup_rich_edit readies a Rich field's new control: its changes and
// selection's are reported, its background is the field's.
void wh_setup_rich_edit(node *n) {
	if (!n->rich_text || !n->ctl) return;
	SendMessageW(n->ctl, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
	rgba bg = n->plain ? wh_backdrop(n) : H.theme.field_bg;
	SendMessageW(n->ctl, EM_SETBKGNDCOLOR, 0, rgba_ref(bg));
	wh_field *f = n->field;
	if (f && f->content_len) wh_set_rich_text(n, f->content, f->content_len);
}

// emit_runs reports the field's runs as the core's styled text.
static void emit_runs(node *n, wh_field *f, const rtf_runs *runs) {
	ak_rich_run *core = wh_alloc((runs->n + 1) * sizeof *core);
	for (uint32_t i = 0; i < runs->n; i++) core[i] = core_run(f, &runs->v[i]);
	ak_buf b = {0};
	ak_append_rich(&b, core, runs->n);
	wh_emit_bytes(n, AK_EVENT_RICH_CHANGED, b.data, b.len);
	ak_buf_free(&b);
	free(core);
}

// wh_rich_edited reports the user's change of the text or its styles.
void wh_rich_edited(node *n) {
	if (!n->rich_text || n->setting_text || !n->ctl) return;
	wh_field *f = field_of(n);
	if (!f->doc.face) rich_doc(n, f);
	rtf_runs runs = {0};
	read_runs(n, f, &runs);
	emit_runs(n, f, &runs);
	rtf_runs_free(&runs);
	report_formats(n);
}

// wh_retheme_rich draws a Rich field's text again in the theme's colors
// (a semantic color resolves differently in dark mode), as the user left
// it.
void wh_retheme_rich(node *n) {
	if (!n->rich_text || !n->ctl) return;
	wh_field *f = field_of(n);
	if (!f->doc.face) rich_doc(n, f);
	rtf_runs runs = {0};
	read_runs(n, f, &runs);
	ak_rich_run *core = wh_alloc((runs.n + 1) * sizeof *core);
	for (uint32_t i = 0; i < runs.n; i++) core[i] = core_run(f, &runs.v[i]);
	ak_buf b = {0};
	ak_append_rich(&b, core, runs.n);
	free(core);
	rtf_runs_free(&runs);
	wh_set_rich_text(n, b.data, b.len);
	ak_buf_free(&b);
}

// wh_field_notify is a field's rich edit's WM_NOTIFY: its selection moved.
LRESULT wh_field_notify(node *n, NMHDR *hdr, LPARAM lp, bool *handled) {
	(void)lp;
	*handled = false;
	if (hdr->code == EN_SELCHANGE && n->rich_text) {
		report_formats(n);
		wh_check_selection(n);
		*handled = true;
	}
	return 0;
}

// wh_format_rich is the Format command: the selection's character format
// (at the cursor, the insertion format), undoably. args: u8 bits, u8 how
// (0 off, 1 on, 2 toggle).
void wh_format_rich(node *n, const uint8_t *args, size_t len) {
	if (!n->rich_text || !n->ctl || len < 2) return;
	wh_field *f = field_of(n);
	uint8_t bits = args[0], how = args[1];
	const char *link;
	uint8_t has = formats_now(n, f, &link);
	bool on = how == 1;
	if (how == 2) on = (has & bits) != bits;
	CHARFORMAT2W cf = {.cbSize = sizeof cf};
	static const DWORD masks[][2] = {{1, CFM_BOLD}, {2, CFM_ITALIC}, {4, CFM_UNDERLINE}, {8, CFM_STRIKEOUT}};
	for (size_t i = 0; i < 4; i++) {
		if (!(bits & masks[i][0])) continue;
		cf.dwMask |= masks[i][1];
		if (on) cf.dwEffects |= masks[i][1];
	}
	if (bits & 16) {
		cf.dwMask |= CFM_FACE;
		if (on) {
			cf.bPitchAndFamily = FIXED_PITCH | FF_MODERN;  // \fmodern\fprq1 in its RTF
			wcscpy(cf.szFaceName, L"" MONO_FACE);
		} else {
			rich_doc(n, f);
			MultiByteToWideChar(CP_UTF8, 0, f->doc.face, -1, cf.szFaceName, LF_FACESIZE);
			cf.szFaceName[LF_FACESIZE - 1] = 0;
		}
	}
	SendMessageW(n->ctl, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
	wh_rich_edited(n);
}

// wh_set_rich_link is the SetLink command: the selection (at the cursor,
// the link it's in) becomes a friendly link to url, or plain text again.
void wh_set_rich_link(node *n, const char *url, size_t len) {
	if (!n->rich_text || !n->ctl) return;
	wh_field *f = field_of(n);
	if (!f->doc.face) rich_doc(n, f);
	CHARRANGE sel = {0};
	SendMessageW(n->ctl, EM_EXGETSEL, 0, (LPARAM)&sel);
	if (sel.cpMin == sel.cpMax) {
		bool found = false;
		for (uint32_t i = 0; i < f->nruns; i++) {
			field_run *r = &f->runs[i];
			if (r->link && sel.cpMin > r->start && sel.cpMin <= r->end) {
				sel = (CHARRANGE){r->start, r->end};
				found = true;
				break;
			}
		}
		if (!found) return;
		SendMessageW(n->ctl, EM_EXSETSEL, 0, (LPARAM)&sel);
	}
	rtf_buf b = {0};
	stream_out(n->ctl, true, &b);
	rtf_runs runs = {0};
	rtf_parse(b.p ? b.p : "", b.n, &f->doc, &runs);
	free(b.p);
	for (uint32_t i = 0; i < runs.n; i++) {
		free(runs.v[i].link);
		runs.v[i].link = len ? wh_strndup(url, len) : NULL;
	}
	rtf_buf data = {0};
	rtf_write(runs.v, runs.n, &f->doc, &data);
	rtf_runs_free(&runs);
	set_text_ex(n, &data, true);
	free(data.p);
	SendMessageW(n->ctl, EM_EXSETSEL, 0, (LPARAM)&sel);
	wh_rich_edited(n);
}
