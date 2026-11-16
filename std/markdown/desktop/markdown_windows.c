// std/markdown on Windows: the paragraphs the core read as RTF in a read-only rich
// edit (msftedit's RICHEDIT50W: its typesetter, selection, copying and
// accessibility), in a window of the package's that it fills, which gets
// the rich edit's EN_REQUESTRESIZE to measure it. Links are the theme's
// accent, underlined; a click on one opens it.
#include "asuka_windows.h"
#include "markdown.asuka.h"

#include <commctrl.h>
#include <math.h>
#include <richedit.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int32_t start, end;  // its characters (UTF-16), [start, end)
	char *url;
} link;

typedef struct document document;

// ---- RTF ----

typedef struct {
	uint8_t r, g, b;
} rgb;

typedef struct {
	rgb colors[16];
	int n;
} palette;

static int color_index(palette *p, rgb c) {
	for (int i = 0; i < p->n; i++)
		if (p->colors[i].r == c.r && p->colors[i].g == c.g && p->colors[i].b == c.b) return i + 1;
	if (p->n == (int)(sizeof p->colors / sizeof *p->colors)) return 1;
	p->colors[p->n++] = c;
	return p->n;
}

// rtf_escape writes s as RTF text: its specials escaped, what isn't
// ASCII as \u escapes (UTF-16 units, a ? for readers without them).
static void rtf_escape(asuka_writer *w, const char *s, size_t n) {
	wchar_t *u = NULL;
	char *t = asuka_strndup(s, n);
	u = asuka_wide(t);
	free(t);
	for (wchar_t *c = u; *c; c++) {
		char b[16];
		if (*c == '\\' || *c == '{' || *c == '}') {
			b[0] = '\\', b[1] = (char)*c;
			asuka_writer_put(w, b, 2);
		} else if (*c == '\t') {
			asuka_writer_put(w, "\\tab ", 5);
		} else if (*c >= 0x20 && *c < 0x80) {
			b[0] = (char)*c;
			asuka_writer_put(w, b, 1);
		} else if (*c >= 0x80) {
			int k = snprintf(b, sizeof b, "\\u%d?", (int)(int16_t)*c);
			asuka_writer_put(w, b, (size_t)k);
		}
	}
	free(u);
}

static void puts_w(asuka_writer *w, const char *s) { asuka_writer_put(w, s, strlen(s)); }

static void printf_w(asuka_writer *w, const char *format, int v) {
	char b[64];
	int k = snprintf(b, sizeof b, format, v);
	asuka_writer_put(w, b, (size_t)k);
}

// A run as RTF takes: its text, style, colors and size in points (0:
// the document's).
typedef struct {
	const char *text;
	bool bold, italic, underline, strike, mono;
	const rgb *color, *background;
	float size;
} rtf_run;

// rtf_run_write writes a run, its paragraphs ended with \par.
static void rtf_run_write(asuka_writer *body, palette *pal, const rtf_run *r, int half) {
	const char *s = r->text;
	for (bool first = true;; first = false) {
		const char *nl = strchr(s, '\n');
		size_t n = nl ? (size_t)(nl - s) : strlen(s);
		if (!first) puts_w(body, "\\par ");
		if (n > 0) {
			asuka_writer_u8(body, '{');
			int size = r->size > 0 ? (int)(r->size * 2 + 0.5f) : half;
			printf_w(body, "\\f%d", r->mono ? 1 : 0);
			printf_w(body, "\\fs%d", size);
			if (r->bold) puts_w(body, "\\b");
			if (r->italic) puts_w(body, "\\i");
			if (r->underline) puts_w(body, "\\ul");
			if (r->strike) puts_w(body, "\\strike");
			printf_w(body, "\\cf%d", r->color ? color_index(pal, *r->color) : 1);
			if (r->background) {
				int k = color_index(pal, *r->background);
				printf_w(body, "\\chcbpat%d", k);
				printf_w(body, "\\highlight%d", k);
			}
			asuka_writer_u8(body, ' ');
			rtf_escape(body, s, n);
			asuka_writer_u8(body, '}');
		}
		if (!nl) break;
		s = nl + 1;
	}
}

// ---- The document ----

static rgb semantic(int c, rgb def) {
	uint32_t v = asuka_desktop_color(((uint64_t)1 << 32) | (uint64_t)c);
	if (v == 0) return def;
	return (rgb){(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8)};
}

enum { LABEL = 1, SECONDARY_LABEL = 2, BACKGROUND = 4, SECONDARY_BACKGROUND = 5, ACCENT = 8 };

static UINT dpi_of(HWND h) {
	static UINT(WINAPI * for_window)(HWND);
	static UINT(WINAPI * for_system)(void);
	static bool tried;
	if (!tried) {
		tried = true;
		HMODULE u = GetModuleHandleW(L"user32.dll");
		*(FARPROC *)&for_window = GetProcAddress(u, "GetDpiForWindow");
		*(FARPROC *)&for_system = GetProcAddress(u, "GetDpiForSystem");
	}
	UINT d = h ? (for_window ? for_window(h) : 0) : (for_system ? for_system() : 0);
	return d ? d : 96;
}

// utf16_len is s's length in UTF-16 units.
static int32_t utf16_len(const char *s) {
	int32_t n = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if ((*p & 0xC0) == 0x80) continue;
		n += *p >= 0xF0 ? 2 : 1;
	}
	return n;
}

// A block as the control has it: its RTF (its runs, after the paragraph
// ends that part it from the block before), its length in characters (a
// paragraph's end is one) and its links, from its start.
typedef struct {
	char *rtf;
	int32_t len;
	link *links;
	size_t nlinks;
} block;

typedef struct {
	asuka_writer body;
	palette *pal;
	block *b;
	size_t lcap;
	int half;
} builder;

static void add(builder *b, rtf_run r, const char *link_to) {
	if (link_to) {
		block *k = b->b;
		if (k->nlinks == b->lcap) {
			b->lcap = b->lcap ? b->lcap * 2 : 4;
			k->links = realloc(k->links, b->lcap * sizeof *k->links);
		}
		int32_t n = utf16_len(r.text);
		k->links[k->nlinks++] = (link){k->len, k->len + n, asuka_strdup(link_to)};
		r.underline = true;
		r.color = &b->pal->colors[2];  // the accent
	}
	b->b->len += utf16_len(r.text);
	rtf_run_write(&b->body, b->pal, &r, b->half);
}

static void block_free(block *b) {
	for (size_t i = 0; i < b->nlinks; i++) free(b->links[i].url);
	free(b->links);
	free(b->rtf);
}

// The text's style: its colors (label, secondary label, accent, code's
// background: the color table's first four, in every block's RTF) and
// sizes, for the appearance and DPI shown.
typedef struct {
	palette pal;
	int half;   // the body's size, in half points
	float pts;  // a point, at the control's DPI
} look;

struct document {
	markdown_Document base;
	HWND hwnd, ctl;
	block *blocks;  // what the control shows, in order
	size_t n, cap;
	look look;          // theirs
	int32_t requested;  // EN_REQUESTRESIZE's height
	int pressed;        // the link pressed (its character), or -1
};

static look look_of(document *v) {
	look l = {0};
	l.pal.n = 4;  // in this order, though two may be alike
	l.pal.colors[0] = semantic(LABEL, (rgb){0, 0, 0});
	l.pal.colors[1] = semantic(SECONDARY_LABEL, (rgb){96, 96, 96});
	l.pal.colors[2] = semantic(ACCENT, (rgb){0, 0, 200});
	l.pal.colors[3] = semantic(SECONDARY_BACKGROUND, (rgb){240, 240, 240});
	l.pts = (float)dpi_of(v->hwnd) / (float)dpi_of(NULL);  // the control converts points at the system's DPI
	l.half = (int)(10.5f * l.pts * 2 + 0.5f);              // Segoe UI at 14 pixels
	return l;
}

static bool same_look(const look *a, const look *b) { return a->half == b->half && memcmp(a->pal.colors, b->pal.colors, 4 * sizeof(rgb)) == 0; }

// make is paragraph p as a block (prev: the paragraph before it, or NULL).
static block make(look *lk, const markdown_Paragraph *p, const markdown_Paragraph *prev) {
	static const float sizes[] = {20, 16, 13.5f, 12, 10.5f, 10.5f};
	rgb *secondary = &lk->pal.colors[1], *code = &lk->pal.colors[3];
	block k = {0};
	builder b = {.pal = &lk->pal, .b = &k, .half = lk->half};
	if (prev) {
		add(&b, (rtf_run){.text = "\n"}, NULL);
		if (!prev->tight || !p->tight) add(&b, (rtf_run){.text = "\n", .size = 10.5f / 2 * lk->pts}, NULL);
	}
	asuka_writer lead = {0};
	for (int i = 1; i < p->indent; i++) asuka_writer_put(&lead, "    ", 4);
	if (*p->marker) {
		asuka_writer_put(&lead, p->marker, strlen(p->marker));
		asuka_writer_put(&lead, " ", 1);
	} else if (p->indent > 0) {
		asuka_writer_put(&lead, "    ", 4);
	}
	asuka_writer_u8(&lead, 0);
	if (lead.data[0]) add(&b, (rtf_run){.text = (char *)lead.data, .color = secondary}, NULL);
	asuka_writer_free(&lead);
	if (p->rule) {
		char line[24 * 3 + 1];
		for (int i = 0; i < 24; i++) memcpy(line + i * 3, "―", 3);
		line[24 * 3] = 0;
		add(&b, (rtf_run){.text = line, .color = secondary}, NULL);
	}
	for (size_t j = 0; !p->rule && j < p->runs.len; j++) {
		const markdown_Run *r = &p->runs.data[j];
		rtf_run x = {.text = r->text, .bold = r->bold, .italic = r->italic, .strike = r->struck, .mono = r->code};
		char *indented = NULL;
		if (p->code && p->indent > 0) {
			asuka_writer w = {0};
			for (const char *s = r->text; *s; s++) {
				asuka_writer_put(&w, s, 1);
				if (*s == '\n')
					for (int i = 0; i < p->indent; i++) asuka_writer_put(&w, "    ", 4);
			}
			asuka_writer_u8(&w, 0);
			indented = (char *)w.data;
			x.text = indented;
		}
		if (r->code) x.background = code;
		if (r->quoted) x.color = secondary;
		if (p->heading > 0) {
			x.bold = true;
			x.size = sizes[(p->heading > 6 ? 6 : p->heading) - 1] * lk->pts;
		}
		add(&b, x, *r->link ? r->link : NULL);
		free(indented);
	}
	asuka_writer_u8(&b.body, 0);
	k.rtf = (char *)b.body.data;
	return k;
}

// rtf is blocks as an RTF document, in the look.
static char *rtf(const look *lk, const block *blocks, size_t n) {
	asuka_writer doc = {0};
	puts_w(&doc, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl{\\f0\\fnil Segoe UI;}{\\f1\\fmodern\\fprq1 Cascadia Mono;}}{\\colortbl ;");
	for (int i = 0; i < lk->pal.n; i++) {
		char c[64];
		int k = snprintf(c, sizeof c, "\\red%d\\green%d\\blue%d;", lk->pal.colors[i].r, lk->pal.colors[i].g, lk->pal.colors[i].b);
		asuka_writer_put(&doc, c, (size_t)k);
	}
	printf_w(&doc, "}\\pard\\f0\\fs%d\\cf1 ", lk->half);
	for (size_t i = 0; i < n; i++) puts_w(&doc, blocks[i].rtf);
	puts_w(&doc, "}");
	asuka_writer_u8(&doc, 0);
	return (char *)doc.data;
}

// set_paragraphs keeps the blocks before the first that differs as the
// control has them (their text, layout and selection) and replaces the
// rest: the selection made into the control's text from where they
// start to its end, written over with their RTF.
static void set_paragraphs(markdown_Document *self, markdown_list_Paragraph d) {
	document *v = (document *)self;
	look lk = look_of(v);
	block *next = calloc(d.len ? d.len : 1, sizeof *next);
	for (size_t i = 0; i < d.len; i++) next[i] = make(&lk, &d.data[i], i > 0 ? &d.data[i - 1] : NULL);
	size_t keep = 0;
	if (same_look(&lk, &v->look))
		while (keep < v->n && keep < d.len && strcmp(v->blocks[keep].rtf, next[keep].rtf) == 0) keep++;
	int32_t start = 0;
	for (size_t i = 0; i < keep; i++) start += v->blocks[i].len;
	if (keep == v->n && keep == d.len) goto kept;  // the text as it is
	CHARRANGE sel;
	SendMessageW(v->ctl, EM_EXGETSEL, 0, (LPARAM)&sel);
	SendMessageW(v->ctl, EM_SETSEL, start, -1);
	if (keep < d.len) {
		char *tail = rtf(&lk, next + keep, d.len - keep);
		SETTEXTEX st = {keep > 0 ? ST_SELECTION : ST_DEFAULT, CP_ACP};
		SendMessageW(v->ctl, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)tail);
		free(tail);
	} else {
		SendMessageW(v->ctl, EM_REPLACESEL, FALSE, (LPARAM)L"");
	}
	// The selection as it was, in what's kept.
	if (sel.cpMin > start) sel.cpMin = start;
	if (sel.cpMax > start) sel.cpMax = start;
	SendMessageW(v->ctl, EM_EXSETSEL, 0, (LPARAM)&sel);
	rgb bg = semantic(BACKGROUND, (rgb){255, 255, 255});
	SendMessageW(v->ctl, EM_SETBKGNDCOLOR, 0, RGB(bg.r, bg.g, bg.b));
kept:
	for (size_t i = keep; i < v->n; i++) block_free(&v->blocks[i]);
	for (size_t i = 0; i < keep; i++) {  // the text kept, the links new (their addresses aren't in the text)
		block old = v->blocks[i];
		v->blocks[i].links = next[i].links, v->blocks[i].nlinks = next[i].nlinks;
		next[i].links = old.links, next[i].nlinks = old.nlinks;
		block_free(&next[i]);
	}
	if (d.len > v->cap) {
		v->cap = d.len;
		v->blocks = realloc(v->blocks, v->cap * sizeof *v->blocks);
	}
	for (size_t i = keep; i < d.len; i++) v->blocks[i] = next[i];
	v->n = d.len;
	v->look = lk;
	free(next);
}

// measure is its height at the width it's given, in points.
static bool measure(markdown_Document *self, float width, float height, float *ow, float *oh) {
	document *v = (document *)self;
	float k = (float)dpi_of(v->hwnd) / 96;
	if (isnan(width) || width <= 0) width = 10000;
	int px = (int)ceilf(width * k);
	RECT cur;
	GetClientRect(v->ctl, &cur);
	UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOREDRAW;
	SetWindowPos(v->ctl, NULL, 0, 0, px, cur.bottom > 1 ? cur.bottom : 1, flags);
	v->requested = 0;
	SendMessageW(v->ctl, EM_REQUESTRESIZE, 0, 0);
	int32_t ht = v->requested;
	RECT r;
	GetClientRect(v->hwnd, &r);
	SetWindowPos(v->ctl, NULL, 0, 0, r.right, r.bottom, flags);
	*ow = width;
	*oh = (float)ht / k;
	return true;
}

// link_at is the link under p (the control's pixels), or NULL; *at its
// first character.
static const link *link_at(document *v, POINT p, int32_t *at) {
	POINTL pt = {p.x, p.y};
	int32_t c = (int32_t)SendMessageW(v->ctl, EM_CHARFROMPOS, 0, (LPARAM)&pt);
	int32_t start = 0;
	for (size_t i = 0; i < v->n; start += v->blocks[i++].len) {
		const block *b = &v->blocks[i];
		if (c >= start + b->len) continue;
		for (size_t j = 0; j < b->nlinks; j++)
			if (c >= start + b->links[j].start && c < start + b->links[j].end) {
				*at = start + b->links[j].start;
				return &b->links[j];
			}
		break;
	}
	return NULL;
}

// control is the rich edit's subclass: links take clicks and show the
// hand; the wheel goes on to the scroll view it's in.
static LRESULT CALLBACK control(HWND hwnd, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
	document *v = (document *)ref;
	switch (m) {
	case WM_MOUSEWHEEL:
		return SendMessageW(GetParent(v->hwnd), m, w, l);
	case WM_SETCURSOR: {
		POINT p;
		GetCursorPos(&p);
		ScreenToClient(hwnd, &p);
		int32_t at;
		if (link_at(v, p, &at)) {
			SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
			return 1;
		}
		break;
	}
	case WM_LBUTTONDOWN:
	{
		int32_t at;
		v->pressed = link_at(v, (POINT){(short)LOWORD(l), (short)HIWORD(l)}, &at) ? at : -1;
		break;
	}
	case WM_LBUTTONUP: {
		int32_t at;
		const link *k = link_at(v, (POINT){(short)LOWORD(l), (short)HIWORD(l)}, &at);
		if (k && at == v->pressed) {
			LRESULT r = DefSubclassProc(hwnd, m, w, l);
			wchar_t *u = asuka_wide(k->url);
			ShellExecuteW(NULL, L"open", u, NULL, NULL, SW_SHOWNORMAL);
			free(u);
			return r;
		}
		break;
	}
	case WM_NCDESTROY:
		RemoveWindowSubclass(hwnd, control, id);
		break;
	}
	return DefSubclassProc(hwnd, m, w, l);
}

static bool window(void *ctx, HWND hwnd, UINT m, WPARAM wp, LPARAM lp, LRESULT *result) {
	document *v = ctx;
	switch (m) {
	case WM_NOTIFY: {
		const NMHDR *hdr = (const NMHDR *)lp;
		if (hdr->hwndFrom == v->ctl && hdr->code == EN_REQUESTRESIZE) {
			const REQRESIZE *rr = (const REQRESIZE *)lp;
			v->requested = rr->rc.bottom - rr->rc.top;
			*result = 0;
			return true;
		}
		break;
	}
	case WM_SIZE: {
		RECT r;
		GetClientRect(hwnd, &r);
		SetWindowPos(v->ctl, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
		break;
	}
	case WM_NCDESTROY:
		for (size_t i = 0; i < v->n; i++) block_free(&v->blocks[i]);
		free(v->blocks);
		free(v);
		break;
	}
	return false;
}

// destroy: the window (the host's) frees the document as it goes.
static void destroy(markdown_Document *self) {}

static const markdown_Document_vtable vt = {.destroy = destroy, .set_paragraphs = set_paragraphs, .measure = measure};

markdown_Document *markdown_make_Document(markdown_Document_events events) {
	static bool loaded;
	if (!loaded) {
		loaded = true;
		LoadLibraryExW(L"msftedit.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	}
	document *v = calloc(1, sizeof *v);
	v->base.vt = &vt;
	v->pressed = -1;
	v->hwnd = asuka_win_window(0, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, (HWND)events.parent, window, v);
	if (!v->hwnd) {
		free(v);
		return NULL;
	}
	v->ctl = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL, 0, 0, 0, 0, v->hwnd, NULL,
	                         GetModuleHandleW(NULL), NULL);
	SendMessageW(v->ctl, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE);
	SendMessageW(v->ctl, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
	SetWindowSubclass(v->ctl, control, 1, (DWORD_PTR)v);
	v->base.widget = v->hwnd;
	return &v->base;
}
