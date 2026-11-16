// asuka/markdown on Windows: the paragraphs the core read as RTF in a read-only rich
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

typedef struct {
	markdown_Document base;
	HWND hwnd, ctl;
	char *rtf;  // what the control shows
	link *links;
	size_t nlinks, lcap;
	int32_t requested;  // EN_REQUESTRESIZE's height
	int pressed;
} document;

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

typedef struct {
	document *v;
	asuka_writer body;
	palette pal;
	int32_t at;  // the characters so far: a paragraph's end is one
	int half;
} builder;

static void add(builder *b, rtf_run r, const char *link_to) {
	rgb accent;
	if (link_to) {
		document *v = b->v;
		if (v->nlinks == v->lcap) {
			v->lcap = v->lcap ? v->lcap * 2 : 8;
			v->links = realloc(v->links, v->lcap * sizeof *v->links);
		}
		int32_t n = utf16_len(r.text);
		v->links[v->nlinks++] = (link){b->at, b->at + n, asuka_strdup(link_to)};
		accent = semantic(ACCENT, (rgb){0, 0, 200});
		r.underline = true;
		r.color = &accent;
	}
	b->at += utf16_len(r.text);
	rtf_run_write(&b->body, &b->pal, &r, b->half);
}

static void clear_links(document *v) {
	for (size_t i = 0; i < v->nlinks; i++) free(v->links[i].url);
	v->nlinks = 0;
}

// build is the paragraphs as RTF (malloc's), their links the document's.
static char *build(document *v, markdown_list_Paragraph d) {
	rgb label = semantic(LABEL, (rgb){0, 0, 0});
	rgb secondary = semantic(SECONDARY_LABEL, (rgb){96, 96, 96});
	rgb code = semantic(SECONDARY_BACKGROUND, (rgb){240, 240, 240});
	rgb bg = semantic(BACKGROUND, (rgb){255, 255, 255});
	const float body = 10.5f;  // points: Segoe UI at 14 pixels
	static const float sizes[] = {20, 16, 13.5f, 12, 10.5f, 10.5f};
	float scale = (float)dpi_of(v->hwnd) / (float)dpi_of(NULL);  // the control converts points at the system's DPI

	clear_links(v);
	builder b = {.v = v, .half = (int)(body * scale * 2 + 0.5f)};
	color_index(&b.pal, label);
	for (size_t i = 0; i < d.len; i++) {
		const markdown_Paragraph *p = &d.data[i];
		if (i > 0) {
			add(&b, (rtf_run){.text = "\n"}, NULL);
			if (!d.data[i - 1].tight || !p->tight) add(&b, (rtf_run){.text = "\n", .size = body / 2 * scale}, NULL);
		}
		asuka_writer lead = {0};
		for (int k = 1; k < p->indent; k++) asuka_writer_put(&lead, "    ", 4);
		if (*p->marker) {
			asuka_writer_put(&lead, p->marker, strlen(p->marker));
			asuka_writer_put(&lead, " ", 1);
		} else if (p->indent > 0) {
			asuka_writer_put(&lead, "    ", 4);
		}
		asuka_writer_u8(&lead, 0);
		if (lead.data[0]) add(&b, (rtf_run){.text = (char *)lead.data, .color = &secondary}, NULL);
		asuka_writer_free(&lead);
		if (p->rule) {
			char line[24 * 3 + 1];
			for (int k = 0; k < 24; k++) memcpy(line + k * 3, "―", 3);
			line[24 * 3] = 0;
			add(&b, (rtf_run){.text = line, .color = &secondary}, NULL);
			continue;
		}
		for (size_t j = 0; j < p->runs.len; j++) {
			const markdown_Run *r = &p->runs.data[j];
			rtf_run x = {.text = r->text, .bold = r->bold, .italic = r->italic, .strike = r->struck, .mono = r->code};
			char *indented = NULL;
			if (p->code && p->indent > 0) {
				asuka_writer w = {0};
				for (const char *s = r->text; *s; s++) {
					asuka_writer_put(&w, s, 1);
					if (*s == '\n')
						for (int k = 0; k < p->indent; k++) asuka_writer_put(&w, "    ", 4);
				}
				asuka_writer_u8(&w, 0);
				indented = (char *)w.data;
				x.text = indented;
			}
			if (r->code) x.background = &code;
			if (r->quoted) x.color = &secondary;
			if (p->heading > 0) {
				x.bold = true;
				x.size = sizes[(p->heading > 6 ? 6 : p->heading) - 1] * scale;
			}
			add(&b, x, *r->link ? r->link : NULL);
			free(indented);
		}
	}
	asuka_writer doc = {0};
	puts_w(&doc, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl{\\f0\\fnil Segoe UI;}{\\f1\\fmodern\\fprq1 Cascadia Mono;}}{\\colortbl ;");
	for (int i = 0; i < b.pal.n; i++) {
		char c[64];
		int k = snprintf(c, sizeof c, "\\red%d\\green%d\\blue%d;", b.pal.colors[i].r, b.pal.colors[i].g, b.pal.colors[i].b);
		asuka_writer_put(&doc, c, (size_t)k);
	}
	printf_w(&doc, "}\\pard\\f0\\fs%d\\cf1 ", b.half);
	asuka_writer_put(&doc, b.body.data, b.body.len);
	puts_w(&doc, "}");
	asuka_writer_u8(&doc, 0);
	asuka_writer_free(&b.body);
	SendMessageW(v->ctl, EM_SETBKGNDCOLOR, 0, RGB(bg.r, bg.g, bg.b));
	return (char *)doc.data;
}

static void set_paragraphs(markdown_Document *self, markdown_list_Paragraph d) {
	document *v = (document *)self;
	char *rtf = build(v, d);
	if (v->rtf && strcmp(v->rtf, rtf) == 0) {  // as it is: its selection kept
		free(rtf);
		return;
	}
	free(v->rtf);
	v->rtf = rtf;
	SETTEXTEX st = {ST_DEFAULT, CP_ACP};
	SendMessageW(v->ctl, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)rtf);
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

// link_at is the link under p (the control's pixels), or -1.
static int link_at(document *v, POINT p) {
	POINTL pt = {p.x, p.y};
	int32_t c = (int32_t)SendMessageW(v->ctl, EM_CHARFROMPOS, 0, (LPARAM)&pt);
	for (size_t i = 0; i < v->nlinks; i++)
		if (c >= v->links[i].start && c < v->links[i].end) return (int)i;
	return -1;
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
		if (link_at(v, p) >= 0) {
			SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
			return 1;
		}
		break;
	}
	case WM_LBUTTONDOWN:
		v->pressed = link_at(v, (POINT){(short)LOWORD(l), (short)HIWORD(l)});
		break;
	case WM_LBUTTONUP: {
		int k = link_at(v, (POINT){(short)LOWORD(l), (short)HIWORD(l)});
		if (k >= 0 && k == v->pressed) {
			LRESULT r = DefSubclassProc(hwnd, m, w, l);
			wchar_t *u = asuka_wide(v->links[k].url);
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
		clear_links(v);
		free(v->links);
		free(v->rtf);
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
