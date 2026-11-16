// markdown.c: the text read by md4c into styled paragraphs (markdown.h),
// for markdown_linux.c and markdown_windows.c.
//
// A paragraph knows how deep in lists and quotes it is and its list's
// marker: a container passes its marker to its first child ("1." to a
// list item's first paragraph), a list makes one for each item ("•",
// "2.", "• ☑"), a quote indents its children and makes them secondary
// text. md4c doesn't report a tight list item's paragraphs as blocks
// (their text comes inside the item): such text opens a paragraph of
// its own, closed as the item ends or another block starts.
#include "markdown.h"

#include <stdlib.h>
#include <string.h>

#include "third_party/md4c/entity.h"
#include "third_party/md4c/md4c.h"

enum { F_DOC, F_QUOTE, F_LIST, F_ITEM, F_LEAF, F_TABLE, F_ROW, F_CELL, F_OTHER };

typedef struct {
	int kind;
	int indent;
	bool quote, tight;
	char *marker;  // for the first child (owned)
	// A list's:
	bool ordered, list_tight;
	unsigned number;
	// A leaf's or a row's paragraph; a leaf opened by text (a tight item's)
	size_t para;
	bool implicit;
	bool header;  // a table's head (its rows' cells bold)
	int cells;    // a row's cells so far
	bool code;    // a code or HTML block: its text is one run
	bool html;
} frame;

// style is the spans open.
typedef struct {
	bool bold, italic, strike, mono;
	char *link;  // owned by the style that opened it
	bool owns;
} style;

typedef struct {
	md_doc doc;
	frame *frames;
	size_t nframes, fcap;
	style *styles;
	size_t nstyles, scap;
} reader;

static frame *top(reader *r) { return &r->frames[r->nframes - 1]; }

static frame *push(reader *r, frame f) {
	if (r->nframes == r->fcap) {
		r->fcap = r->fcap ? r->fcap * 2 : 16;
		r->frames = realloc(r->frames, r->fcap * sizeof *r->frames);
	}
	r->frames[r->nframes++] = f;
	return top(r);
}

static void pop(reader *r) {
	frame *f = top(r);
	free(f->marker);
	r->nframes--;
}

static char *take_marker(frame *f) {
	char *m = f->marker;
	f->marker = NULL;
	return m;
}

static size_t add_para(reader *r, md_para p) {
	md_doc *d = &r->doc;
	if (d->n == d->cap) {
		d->cap = d->cap ? d->cap * 2 : 16;
		d->paras = realloc(d->paras, d->cap * sizeof *d->paras);
	}
	d->paras[d->n] = p;
	return d->n++;
}

static style cur_style(reader *r) {
	if (r->nstyles == 0) return (style){0};
	style s = r->styles[r->nstyles - 1];
	s.owns = false;
	return s;
}

static bool same_style(const md_run *a, const style *s, bool quote) {
	if (a->bold != s->bold || a->italic != s->italic || a->strike != s->strike || a->mono != s->mono || a->quote != quote) return false;
	if (!a->link || !s->link) return !a->link && !s->link;
	return strcmp(a->link, s->link) == 0;
}

// add_text appends text to the paragraph p in the style s: to its last
// run when that's of the same style.
static void add_text(reader *r, size_t p, const char *text, size_t n, style s, bool quote) {
	if (n == 0) return;
	md_para *para = &r->doc.paras[p];
	if (para->nruns > 0) {
		md_run *last = &para->runs[para->nruns - 1];
		// A row's cells stay apart: its tabs are runs of their own.
		bool tab = para->cells && (strcmp(last->text, "\t") == 0 || (n == 1 && text[0] == '\t'));
		if (same_style(last, &s, quote) && !tab) {
			size_t had = strlen(last->text);
			last->text = realloc(last->text, had + n + 1);
			memcpy(last->text + had, text, n);
			last->text[had + n] = 0;
			return;
		}
	}
	if (para->nruns == para->cap) {
		para->cap = para->cap ? para->cap * 2 : 4;
		para->runs = realloc(para->runs, para->cap * sizeof *para->runs);
	}
	para->runs[para->nruns++] = (md_run){.text = asuka_strndup(text, n), .bold = s.bold, .italic = s.italic, .mono = s.mono,
	                                     .strike = s.strike, .quote = quote, .link = s.link ? asuka_strdup(s.link) : NULL};
}

// close_implicit closes a paragraph text opened (a tight item's).
static void close_implicit(reader *r) {
	if (r->nframes > 0 && top(r)->kind == F_LEAF && top(r)->implicit) pop(r);
}

// leaf opens a paragraph in the container on top: its indent, its
// quoting, its tightness, and the container's marker if it's the first.
static frame *leaf(reader *r, int heading, bool code, bool html, bool rule) {
	frame *p = top(r);
	char *m = take_marker(p);
	md_para para = {.indent = p->indent, .heading = heading, .code = code, .rule = rule, .tight = p->tight};
	if (!rule) para.marker = m;
	else free(m);
	frame f = {.kind = F_LEAF, .indent = p->indent, .quote = p->quote, .para = add_para(r, para), .code = code || html, .html = html};
	return push(r, f);
}

static char *item_marker(frame *list, const MD_BLOCK_LI_DETAIL *li) {
	char *m = list->ordered ? asuka_sprintf("%u.", list->number++) : asuka_strdup("•");
	if (li && li->is_task) {
		char *t = asuka_sprintf("%s %s", m, li->task_mark == ' ' ? "☐" : "☑");
		free(m);
		m = t;
	}
	return m;
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *ud) {
	reader *r = ud;
	close_implicit(r);
	frame *p = top(r);
	switch (type) {
	case MD_BLOCK_DOC:
		break;
	case MD_BLOCK_QUOTE: {
		frame f = {.kind = F_QUOTE, .indent = p->indent + 1, .quote = true, .marker = take_marker(p)};
		push(r, f);
		break;
	}
	case MD_BLOCK_UL:
	case MD_BLOCK_OL: {
		free(take_marker(p));  // a list makes its items' own
		frame f = {.kind = F_LIST, .indent = p->indent, .quote = p->quote, .tight = p->tight};
		if (type == MD_BLOCK_OL) {
			const MD_BLOCK_OL_DETAIL *d = detail;
			f.ordered = true;
			f.number = d->start;
			f.list_tight = d->is_tight;
		} else {
			f.list_tight = ((const MD_BLOCK_UL_DETAIL *)detail)->is_tight;
		}
		push(r, f);
		break;
	}
	case MD_BLOCK_LI: {
		frame f = {.kind = F_ITEM, .indent = p->indent + 1, .quote = p->quote, .tight = p->list_tight};
		f.marker = p->kind == F_LIST ? item_marker(p, detail) : take_marker(p);
		push(r, f);
		break;
	}
	case MD_BLOCK_HR:
		leaf(r, 0, false, false, true);
		break;
	case MD_BLOCK_H:
		leaf(r, (int)((const MD_BLOCK_H_DETAIL *)detail)->level, false, false, false);
		break;
	case MD_BLOCK_CODE:
		leaf(r, 0, true, false, false);
		break;
	case MD_BLOCK_HTML:
		leaf(r, 0, false, true, false);
		break;
	case MD_BLOCK_P:
		leaf(r, 0, false, false, false);
		break;
	case MD_BLOCK_TABLE: {
		free(take_marker(p));
		frame f = {.kind = F_TABLE, .indent = p->indent, .quote = p->quote};
		push(r, f);
		break;
	}
	case MD_BLOCK_THEAD:
	case MD_BLOCK_TBODY:
		if (p->kind == F_TABLE) p->header = type == MD_BLOCK_THEAD;
		push(r, (frame){.kind = F_OTHER, .indent = p->indent, .quote = p->quote, .header = type == MD_BLOCK_THEAD});
		break;
	case MD_BLOCK_TR: {
		md_para para = {.indent = p->indent, .cells = true, .tight = true};
		frame f = {.kind = F_ROW, .indent = p->indent, .quote = p->quote, .header = p->header, .para = add_para(r, para)};
		push(r, f);
		break;
	}
	case MD_BLOCK_TH:
	case MD_BLOCK_TD: {
		if (p->kind == F_ROW && p->cells++ > 0) add_text(r, p->para, "\t", 1, (style){0}, false);
		frame f = {.kind = F_CELL, .indent = p->indent, .quote = p->quote, .para = p->kind == F_ROW ? p->para : 0, .header = p->header || type == MD_BLOCK_TH};
		push(r, f);
		break;
	}
	}
	return 0;
}

// trim_newline drops a code block's last line's end.
static void trim_newline(md_para *p) {
	if (p->nruns == 0) return;
	md_run *last = &p->runs[p->nruns - 1];
	size_t n = strlen(last->text);
	if (n > 0 && last->text[n - 1] == '\n') last->text[n - 1] = 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *ud) {
	reader *r = ud;
	close_implicit(r);
	if (type == MD_BLOCK_DOC || r->nframes <= 1) return 0;
	frame *f = top(r);
	if (f->kind == F_LEAF && f->code) trim_newline(&r->doc.paras[f->para]);
	pop(r);
	return 0;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *ud) {
	reader *r = ud;
	if (r->nstyles == r->scap) {
		r->scap = r->scap ? r->scap * 2 : 8;
		r->styles = realloc(r->styles, r->scap * sizeof *r->styles);
	}
	style s = cur_style(r);
	switch (type) {
	case MD_SPAN_EM:
		s.italic = true;
		break;
	case MD_SPAN_STRONG:
		s.bold = true;
		break;
	case MD_SPAN_DEL:
		s.strike = true;
		break;
	case MD_SPAN_CODE:
		s.mono = true;
		break;
	case MD_SPAN_A: {
		const MD_SPAN_A_DETAIL *a = detail;
		asuka_writer w = {0};
		const MD_ATTRIBUTE *h = &a->href;
		for (int i = 0; h->substr_offsets && h->substr_offsets[i] < h->size; i++) {
			MD_OFFSET from = h->substr_offsets[i], to = h->substr_offsets[i + 1];
			asuka_writer_put(&w, h->text + from, to - from);
		}
		asuka_writer_u8(&w, 0);
		if (markdown_safe_link((const char *)w.data)) {
			s.link = (char *)w.data;
			s.owns = true;
		} else {
			asuka_writer_free(&w);
			s.link = NULL;
		}
		break;
	}
	default:  // an image's description, as text; the rest as it is
		break;
	}
	r->styles[r->nstyles++] = s;
	return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *ud) {
	reader *r = ud;
	if (r->nstyles == 0) return 0;
	style *s = &r->styles[--r->nstyles];
	if (s->owns) free(s->link);
	return 0;
}

// utf8 writes a code point as UTF-8 into b (4 bytes): its length.
static size_t utf8(unsigned c, char *b) {
	if (c == 0 || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
	if (c < 0x80) {
		b[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		b[0] = (char)(0xC0 | c >> 6), b[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		b[0] = (char)(0xE0 | c >> 12), b[1] = (char)(0x80 | (c >> 6 & 0x3F)), b[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	}
	b[0] = (char)(0xF0 | c >> 18), b[1] = (char)(0x80 | (c >> 12 & 0x3F)), b[2] = (char)(0x80 | (c >> 6 & 0x3F)), b[3] = (char)(0x80 | (c & 0x3F));
	return 4;
}

static int hexval(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

static int text(MD_TEXTTYPE type, const MD_CHAR *t, MD_SIZE n, void *ud) {
	reader *r = ud;
	frame *f = top(r);
	if (f->kind != F_LEAF && f->kind != F_CELL) f = leaf(r, 0, false, false, false), f->implicit = true;
	style s = cur_style(r);
	if (f->kind == F_CELL && f->header) s.bold = true;
	if (f->code) s = (style){.mono = !f->html};
	char buf[8];
	switch (type) {
	case MD_TEXT_NULLCHAR:
		add_text(r, f->para, buf, utf8(0xFFFD, buf), s, f->quote);
		break;
	case MD_TEXT_BR:
		add_text(r, f->para, "\n", 1, s, f->quote);
		break;
	case MD_TEXT_SOFTBR:
		add_text(r, f->para, " ", 1, s, f->quote);
		break;
	case MD_TEXT_ENTITY:
		if (n > 3 && t[1] == '#') {
			unsigned c = 0;
			bool hex = t[2] == 'x' || t[2] == 'X';
			for (MD_SIZE i = hex ? 3 : 2; i + 1 < n; i++) c = hex ? 16 * c + (unsigned)hexval(t[i]) : 10 * c + (unsigned)(t[i] - '0');
			add_text(r, f->para, buf, utf8(c, buf), s, f->quote);
		} else {
			const ENTITY *e = entity_lookup(t, n);
			if (!e) {
				add_text(r, f->para, t, n, s, f->quote);
				break;
			}
			size_t k = utf8(e->codepoints[0], buf);
			if (e->codepoints[1]) k += utf8(e->codepoints[1], buf + k);
			add_text(r, f->para, buf, k, s, f->quote);
		}
		break;
	default:  // normal, code, raw HTML (shown as text), math
		add_text(r, f->para, t, n, s, f->quote);
		break;
	}
	return 0;
}

md_doc markdown_read(const char *src) {
	reader r = {0};
	push(&r, (frame){.kind = F_DOC});
	MD_PARSER p = {
		.flags = MD_DIALECT_GITHUB,
		.enter_block = enter_block,
		.leave_block = leave_block,
		.enter_span = enter_span,
		.leave_span = leave_span,
		.text = text,
	};
	md_parse(src ? src : "", (MD_SIZE)(src ? strlen(src) : 0), &p, &r);
	while (r.nframes > 0) pop(&r);
	while (r.nstyles > 0) leave_span(MD_SPAN_EM, NULL, &r);
	free(r.frames);
	free(r.styles);
	// Paragraphs that came to nothing (a list item's empty text).
	size_t k = 0;
	for (size_t i = 0; i < r.doc.n; i++) {
		md_para *p = &r.doc.paras[i];
		if (p->nruns == 0 && !p->rule && !p->cells && !p->code && !p->marker) {
			free(p->runs);
			continue;
		}
		r.doc.paras[k++] = *p;
	}
	r.doc.n = k;
	return r.doc;
}

void markdown_doc_free(md_doc *d) {
	for (size_t i = 0; i < d->n; i++) {
		md_para *p = &d->paras[i];
		for (size_t j = 0; j < p->nruns; j++) {
			free(p->runs[j].text);
			free(p->runs[j].link);
		}
		free(p->runs);
		free(p->marker);
	}
	free(d->paras);
	*d = (md_doc){0};
}

bool markdown_safe_link(const char *dest) {
	while (*dest == ' ' || *dest == '\t' || *dest == '\n') dest++;
	static const char *ok[] = {"http://", "https://", "mailto:", "tel:"};
	for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) {
		size_t j = 0;
		while (ok[i][j] && (dest[j] | 0x20) == ok[i][j]) j++;  // the prefixes are lower case letters, ':' and '/'
		if (!ok[i][j]) return true;
	}
	return false;
}
