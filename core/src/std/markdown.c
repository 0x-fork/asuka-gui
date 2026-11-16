// markdown.c: asuka/markdown's builtins, the same on every platform:
//
//	markdown.read(text) -> string   the text read into styled paragraphs (JSON)
//	markdown.heal(text) -> string   what text being written will most likely become
//
// read reads the text with md4c 0.5.2 (core/third_party/md4c: CommonMark, with
// GitHub's tables, task lists, strikethrough and autolinks) into the
// paragraphs the platforms' views show as their own styled text: each
// one's runs of text and their style, how deep in lists and quotes it is,
// its list's marker, what kind of block it is. A paragraph knows its
// list's marker: a container passes its marker to its first child ("1."
// to a list item's first paragraph), a list makes one for each item ("•",
// "2.", "• ☑"), a quote indents its children and makes them quoted text.
// md4c doesn't report a tight list item's paragraphs as blocks (their
// text comes inside the item): such text opens a paragraph of its own,
// closed as the item ends or another block starts. A table's row is a
// paragraph, its cells' runs apart by tabs. Raw HTML is text; a link
// opens only to a web, mail or phone address.
//
// The paragraphs are JSON, as std/markdown's Paragraph and Run (a field
// that's false, 0 or empty left out):
//
//	[{"runs": [{"text": "Hi ", "bold": true, "italic": true, "code": true,
//	  "struck": true, "quoted": true, "link": "https://..."}, ...],
//	  "heading": 1, "indent": 1, "marker": "1.", "code": true,
//	  "rule": true, "row": true, "tight": true}, ...]
#include "std/std.h"

#include <string.h>

// md4c, compiled in; its two exported names the core's own. GCC at -O2
// reads a link label's line (md_is_link_reference_definition's
// label_contents_line_index) as maybe unset where md_is_link_label, which
// sets it whenever it succeeds, inlines: md4c stays as released.
#define md_parse ak_md_parse
#define entity_lookup ak_md_entity_lookup
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../../third_party/md4c/entity.c"
#include "../../third_party/md4c/md4c.c"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define ARG_STR(i) ((ak_str *)args[i].p)

// ---- The paragraphs ----

enum { R_BOLD = 1, R_ITALIC = 2, R_MONO = 4, R_STRIKE = 8, R_QUOTE = 16 };
enum { P_CODE = 1, P_RULE = 2, P_CELLS = 4, P_TIGHT = 8 };

typedef struct {
	ak_buf text;
	uint8_t style;
	char *link;  // NULL: not a link, or one that doesn't open
} run;

typedef struct {
	run *runs;
	size_t nruns, cap;
	uint8_t flags, heading, indent;
	char *marker;  // NULL: none
} para;

enum { F_DOC, F_QUOTE, F_LIST, F_ITEM, F_LEAF, F_TABLE, F_ROW, F_CELL, F_OTHER };

typedef struct {
	int kind;
	int indent;
	bool quote, tight;
	char *marker;  // for the first child
	bool ordered, list_tight;
	unsigned number;
	size_t para;    // a leaf's or a row's paragraph
	bool implicit;  // a leaf text opened (a tight item's)
	bool header;    // a table's head: its cells bold
	int cells;      // a row's cells so far
	bool code, html;
} frame;

typedef struct {
	uint8_t style;
	char *link;
	bool owns;
} style;

typedef struct {
	para *paras;
	size_t n, cap;
	frame *frames;
	size_t nframes, fcap;
	style *styles;
	size_t nstyles, scap;
} reader;

static char *dup(const char *s, size_t n) {
	char *p = ak_alloc(n + 1);
	memcpy(p, s, n);
	return p;
}

static char *dupc(const char *s) { return dup(s, strlen(s)); }

static frame *top(reader *r) { return &r->frames[r->nframes - 1]; }

static frame *push(reader *r, frame f) {
	if (r->nframes == r->fcap) {
		r->fcap = r->fcap ? r->fcap * 2 : 16;
		r->frames = ak_realloc(r->frames, r->fcap * sizeof *r->frames);
	}
	r->frames[r->nframes++] = f;
	return top(r);
}

static void pop(reader *r) {
	ak_free(top(r)->marker);
	r->nframes--;
}

static char *take_marker(frame *f) {
	char *m = f->marker;
	f->marker = NULL;
	return m;
}

static size_t add_para(reader *r, para p) {
	if (r->n == r->cap) {
		r->cap = r->cap ? r->cap * 2 : 16;
		r->paras = ak_realloc(r->paras, r->cap * sizeof *r->paras);
	}
	r->paras[r->n] = p;
	return r->n++;
}

static style cur_style(reader *r) {
	if (r->nstyles == 0) return (style){0};
	style s = r->styles[r->nstyles - 1];
	s.owns = false;
	return s;
}

static bool same_link(const char *a, const char *b) { return (!a && !b) || (a && b && strcmp(a, b) == 0); }

// add_text appends text to the paragraph in the style: to its last run
// when that's of the same style (but a row's tabs, which stay runs of
// their own).
static void add_text(reader *r, size_t p, const char *text, size_t n, style s) {
	if (n == 0) return;
	para *pa = &r->paras[p];
	bool tab = (pa->flags & P_CELLS) && n == 1 && text[0] == '\t';
	if (pa->nruns > 0) {
		run *last = &pa->runs[pa->nruns - 1];
		bool last_tab = (pa->flags & P_CELLS) && last->text.len == 1 && last->text.data[0] == '\t';
		if (last->style == s.style && same_link(last->link, s.link) && !tab && !last_tab) {
			ak_buf_put(&last->text, text, n);
			return;
		}
	}
	if (pa->nruns == pa->cap) {
		pa->cap = pa->cap ? pa->cap * 2 : 4;
		pa->runs = ak_realloc(pa->runs, pa->cap * sizeof *pa->runs);
	}
	run *x = &pa->runs[pa->nruns++];
	*x = (run){.style = s.style, .link = s.link ? dupc(s.link) : NULL};
	ak_buf_put(&x->text, text, n);
}

static void close_implicit(reader *r) {
	if (r->nframes > 0 && top(r)->kind == F_LEAF && top(r)->implicit) pop(r);
}

// leaf opens a paragraph in the container on top: its indent, its
// quoting, its tightness, and the container's marker if it's the first.
static frame *leaf(reader *r, int heading, bool code, bool html, bool rule) {
	frame *p = top(r);
	char *m = take_marker(p);
	para pa = {.heading = (uint8_t)heading, .indent = (uint8_t)(p->indent > 255 ? 255 : p->indent)};
	pa.flags = (uint8_t)((code ? P_CODE : 0) | (rule ? P_RULE : 0) | (p->tight ? P_TIGHT : 0));
	if (!rule) pa.marker = m;
	else ak_free(m);
	frame f = {.kind = F_LEAF, .indent = p->indent, .quote = p->quote, .para = add_para(r, pa), .code = code || html, .html = html};
	return push(r, f);
}

static char *item_marker(frame *list, const MD_BLOCK_LI_DETAIL *li) {
	char buf[32];
	if (list->ordered) snprintf(buf, sizeof buf, "%u.", list->number++);
	else snprintf(buf, sizeof buf, "•");
	if (li && li->is_task) {
		size_t k = strlen(buf);
		snprintf(buf + k, sizeof buf - k, " %s", li->task_mark == ' ' ? "☐" : "☑");
	}
	return dupc(buf);
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *ud) {
	reader *r = ud;
	close_implicit(r);
	frame *p = top(r);
	switch (type) {
	case MD_BLOCK_DOC:
		break;
	case MD_BLOCK_QUOTE:
		push(r, (frame){.kind = F_QUOTE, .indent = p->indent + 1, .quote = true, .marker = take_marker(p)});
		break;
	case MD_BLOCK_UL:
	case MD_BLOCK_OL: {
		ak_free(take_marker(p));  // a list makes its items' own
		frame f = {.kind = F_LIST, .indent = p->indent, .quote = p->quote, .tight = p->tight};
		if (type == MD_BLOCK_OL) {
			const MD_BLOCK_OL_DETAIL *d = detail;
			f.ordered = true, f.number = d->start, f.list_tight = d->is_tight;
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
	case MD_BLOCK_TABLE:
		ak_free(take_marker(p));
		push(r, (frame){.kind = F_TABLE, .indent = p->indent, .quote = p->quote});
		break;
	case MD_BLOCK_THEAD:
	case MD_BLOCK_TBODY:
		push(r, (frame){.kind = F_OTHER, .indent = p->indent, .quote = p->quote, .header = type == MD_BLOCK_THEAD});
		break;
	case MD_BLOCK_TR: {
		para pa = {.flags = P_CELLS | P_TIGHT, .indent = (uint8_t)(p->indent > 255 ? 255 : p->indent)};
		push(r, (frame){.kind = F_ROW, .indent = p->indent, .quote = p->quote, .header = p->header, .para = add_para(r, pa)});
		break;
	}
	case MD_BLOCK_TH:
	case MD_BLOCK_TD:
		if (p->kind == F_ROW && p->cells++ > 0) add_text(r, p->para, "\t", 1, (style){0});
		push(r, (frame){.kind = F_CELL, .indent = p->indent, .quote = p->quote, .para = p->kind == F_ROW ? p->para : 0,
		                .header = p->header || type == MD_BLOCK_TH});
		break;
	}
	return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *ud) {
	reader *r = ud;
	close_implicit(r);
	if (type == MD_BLOCK_DOC || r->nframes <= 1) return 0;
	frame *f = top(r);
	if (f->kind == F_LEAF && f->code) {  // a code block's last line's end
		para *p = &r->paras[f->para];
		if (p->nruns > 0) {
			ak_buf *t = &p->runs[p->nruns - 1].text;
			if (t->len > 0 && t->data[t->len - 1] == '\n') t->len--;
		}
	}
	pop(r);
	return 0;
}

// safe reports whether a link opens: a web, mail or phone address.
static bool safe(const char *dest) {
	while (*dest == ' ' || *dest == '\t' || *dest == '\n') dest++;
	static const char *ok[] = {"http://", "https://", "mailto:", "tel:"};
	for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) {
		size_t j = 0;
		while (ok[i][j] && (dest[j] | 0x20) == ok[i][j]) j++;  // the prefixes are lower case letters, ':' and '/'
		if (!ok[i][j]) return true;
	}
	return false;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *ud) {
	reader *r = ud;
	if (r->nstyles == r->scap) {
		r->scap = r->scap ? r->scap * 2 : 8;
		r->styles = ak_realloc(r->styles, r->scap * sizeof *r->styles);
	}
	style s = cur_style(r);
	switch (type) {
	case MD_SPAN_EM: s.style |= R_ITALIC; break;
	case MD_SPAN_STRONG: s.style |= R_BOLD; break;
	case MD_SPAN_DEL: s.style |= R_STRIKE; break;
	case MD_SPAN_CODE: s.style |= R_MONO; break;
	case MD_SPAN_A: {
		const MD_ATTRIBUTE *h = &((const MD_SPAN_A_DETAIL *)detail)->href;
		ak_buf b = {0};
		for (int i = 0; h->substr_offsets && h->substr_offsets[i] < h->size; i++)
			ak_buf_put(&b, h->text + h->substr_offsets[i], h->substr_offsets[i + 1] - h->substr_offsets[i]);
		ak_buf_u8(&b, 0);
		s.link = NULL;
		if (safe((const char *)b.data)) {
			s.link = dupc((const char *)b.data);
			s.owns = true;
		}
		ak_buf_free(&b);
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
	if (s->owns) ak_free(s->link);
	return 0;
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
	if (f->kind != F_LEAF && f->kind != F_CELL) {
		f = leaf(r, 0, false, false, false);
		f->implicit = true;
	}
	style s = cur_style(r);
	if (f->kind == F_CELL && f->header) s.style |= R_BOLD;
	if (f->code) s = (style){.style = f->html ? 0 : R_MONO};
	if (f->quote) s.style |= R_QUOTE;
	char buf[8];
	switch (type) {
	case MD_TEXT_NULLCHAR:
		add_text(r, f->para, buf, ak_utf8_encode(0xFFFD, buf), s);
		break;
	case MD_TEXT_BR:
		add_text(r, f->para, "\n", 1, s);
		break;
	case MD_TEXT_SOFTBR:
		add_text(r, f->para, " ", 1, s);
		break;
	case MD_TEXT_ENTITY:
		if (n > 3 && t[1] == '#') {
			unsigned c = 0;
			bool hex = t[2] == 'x' || t[2] == 'X';
			for (MD_SIZE i = hex ? 3 : 2; i + 1 < n; i++) c = hex ? 16 * c + (unsigned)hexval(t[i]) : 10 * c + (unsigned)(t[i] - '0');
			add_text(r, f->para, buf, ak_utf8_encode(c <= 0x10FFFF && c != 0 && (c < 0xD800 || c > 0xDFFF) ? c : 0xFFFD, buf), s);
		} else {
			const ENTITY *e = entity_lookup(t, n);
			if (!e) {
				add_text(r, f->para, t, n, s);
				break;
			}
			size_t k = ak_utf8_encode(e->codepoints[0], buf);
			if (e->codepoints[1]) k += ak_utf8_encode(e->codepoints[1], buf + k);
			add_text(r, f->para, buf, k, s);
		}
		break;
	default:  // normal, code, raw HTML (shown as text), math
		add_text(r, f->para, t, n, s);
		break;
	}
	return 0;
}

// json_str writes s as a JSON string.
static void json_str(ak_buf *b, const char *s, size_t n) {
	ak_buf_u8(b, '"');
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '"' || c == '\\') {
			ak_buf_u8(b, '\\');
			ak_buf_u8(b, c);
		} else if (c == '\n') {
			ak_buf_put(b, "\\n", 2);
		} else if (c == '\t') {
			ak_buf_put(b, "\\t", 2);
		} else if (c < 0x20) {
			ak_buf_printf(b, "\\u%04x", c);
		} else {
			ak_buf_u8(b, c);
		}
	}
	ak_buf_u8(b, '"');
}

static void json_flag(ak_buf *b, const char *name, int on) {
	if (on) ak_buf_printf(b, ",\"%s\":true", name);
}

static ak_val read_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *src = ARG_STR(0);
	reader r = {0};
	push(&r, (frame){.kind = F_DOC});
	MD_PARSER p = {.flags = MD_DIALECT_GITHUB, .enter_block = enter_block, .leave_block = leave_block, .enter_span = enter_span,
	               .leave_span = leave_span, .text = text};
	md_parse(src->data, (MD_SIZE)src->len, &p, &r);
	while (r.nframes > 0) pop(&r);
	while (r.nstyles > 0) leave_span(MD_SPAN_EM, NULL, &r);
	ak_free(r.frames);
	ak_free(r.styles);
	// The paragraphs, but those that came to nothing (a list item's
	// empty text).
	ak_buf out = {0};
	ak_buf_u8(&out, '[');
	bool first = true;
	for (size_t i = 0; i < r.n; i++) {
		para *pa = &r.paras[i];
		if (pa->nruns > 0 || (pa->flags & (P_RULE | P_CELLS | P_CODE)) || pa->marker) {
			if (!first) ak_buf_u8(&out, ',');
			first = false;
			ak_buf_put(&out, "{\"runs\":[", 9);
			for (size_t j = 0; j < pa->nruns; j++) {
				run *x = &pa->runs[j];
				if (j > 0) ak_buf_u8(&out, ',');
				ak_buf_put(&out, "{\"text\":", 8);
				json_str(&out, (const char *)x->text.data, x->text.len);
				json_flag(&out, "bold", x->style & R_BOLD);
				json_flag(&out, "italic", x->style & R_ITALIC);
				json_flag(&out, "code", x->style & R_MONO);
				json_flag(&out, "struck", x->style & R_STRIKE);
				json_flag(&out, "quoted", x->style & R_QUOTE);
				if (x->link) {
					ak_buf_put(&out, ",\"link\":", 8);
					json_str(&out, x->link, strlen(x->link));
				}
				ak_buf_u8(&out, '}');
			}
			ak_buf_u8(&out, ']');
			if (pa->heading) ak_buf_printf(&out, ",\"heading\":%d", pa->heading);
			if (pa->indent) ak_buf_printf(&out, ",\"indent\":%d", pa->indent);
			if (pa->marker) {
				ak_buf_put(&out, ",\"marker\":", 10);
				json_str(&out, pa->marker, strlen(pa->marker));
			}
			json_flag(&out, "code", pa->flags & P_CODE);
			json_flag(&out, "rule", pa->flags & P_RULE);
			json_flag(&out, "row", pa->flags & P_CELLS);
			json_flag(&out, "tight", pa->flags & P_TIGHT);
			ak_buf_u8(&out, '}');
		}
		for (size_t j = 0; j < pa->nruns; j++) {
			ak_buf_free(&pa->runs[j].text);
			ak_free(pa->runs[j].link);
		}
		ak_free(pa->runs);
		ak_free(pa->marker);
	}
	ak_buf_u8(&out, ']');
	ak_free(r.paras);
	ak_str *json = ak_str_new((const char *)out.data, out.len);
	ak_buf_free(&out);
	return (ak_val){.p = json};
}

// ---- heal ----
//
// What text being written will most likely become: a code fence it
// opened closed, and in its last paragraph, the code, emphasis and
// strikethrough it opened closed, and a link whose destination is being
// written ended. Characters are UTF-8's code points; only ASCII ones
// mark anything.

// trimmed is a line's text without its leading and trailing
// spaces: [*from, *to).
static void trimmed(const char *s, size_t a, size_t b, size_t *from, size_t *to) {
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
	*from = a, *to = b;
}

static bool wordy(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

typedef struct {
	char m[2];  // the marker: "*", "**", "_", "__", "~~"
	uint8_t n;
} marker;

typedef AK_VEC(marker) markers;

// toggled closes m when it's the innermost open, else opens it.
static void toggled(markers *open, const char *m, uint8_t n) {
	if (open->len > 0 && open->data[open->len - 1].n == n && !memcmp(open->data[open->len - 1].m, m, n)) {
		open->len--;
		return;
	}
	marker x = {{m[0], n > 1 ? m[1] : 0}, n};
	ak_vec_push(open, x);
}

static ak_val heal_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *src = ARG_STR(0);
	const char *s = src->data;
	size_t len = src->len;
	// Fences, line by line; where the last paragraph starts.
	char fence[3] = {0};
	size_t last = 0;  // the byte its first line starts at
	for (size_t a = 0; a <= len;) {
		size_t b = a;
		while (b < len && s[b] != '\n') b++;
		size_t from, to;
		trimmed(s, a, b, &from, &to);
		bool next_starts = b < len;
		if (!fence[0]) {
			if (to - from >= 3 && (!memcmp(s + from, "```", 3) || !memcmp(s + from, "~~~", 3))) {
				memcpy(fence, s + from, 3);
			} else if (from == to) {
				last = next_starts ? b + 1 : len;
			}
		} else if (to - from >= 3 && !memcmp(s + from, fence, 3)) {
			fence[0] = 0;
			last = next_starts ? b + 1 : len;
		}
		if (!next_starts) break;
		a = b + 1;
	}
	if (fence[0]) {
		ak_buf b = {0};
		ak_buf_put(&b, s, len);
		if (len == 0 || s[len - 1] != '\n') ak_buf_u8(&b, '\n');
		ak_buf_put(&b, fence, 3);
		ak_str *r = ak_str_new((const char *)b.data, b.len);
		ak_buf_free(&b);
		return (ak_val){.p = r};
	}
	// The last paragraph's characters.
	size_t cap = len - last + 1, nc = 0;
	uint32_t *cs = ak_alloc(cap * sizeof *cs);
	for (size_t i = last; i < len;) cs[nc++] = ak_utf8_decode(s, len, &i);
	markers open = {0};
	bool code = false, dest = false, line_start = true;
	for (size_t i = 0; i < nc;) {
		uint32_t c = cs[i];
		if (c == '\n') {
			line_start = true;
			i++;
			continue;
		}
		bool at_start = line_start;
		if (c != ' ') line_start = false;
		if (c == '\\') {
			i += 2;
			continue;
		}
		if (code) {
			if (c == '`') code = false;
			i++;
			continue;
		}
		if (dest) {
			if (c == ')') dest = false;
			i++;
			continue;
		}
		if (c == '`') {
			code = true;
		} else if (c == ']' && i + 1 < nc && cs[i + 1] == '(') {
			dest = true;
			i++;
		} else if (c == '*' || c == '_' || c == '~') {
			size_t k = 1;
			while (i + k < nc && cs[i + k] == c) k++;
			uint32_t next = i + k < nc ? cs[i + k] : ' ', prev = i > 0 ? cs[i - 1] : ' ';
			// A list's bullet, or an underscore inside a word: not a marker.
			bool bullet = at_start && k == 1 && c != '~' && next == ' ';
			bool in_word = c == '_' && wordy(prev) && wordy(next);
			if (!bullet && !in_word) {
				char m[2] = {(char)c, (char)c};
				if (c == '~') {
					if (k >= 2) toggled(&open, m, 2);
				} else {
					if (k >= 2) toggled(&open, m, 2);
					if (k % 2 == 1) toggled(&open, m, 1);
				}
			}
			i += k;
			continue;
		}
		i++;
	}
	ak_free(cs);
	if (!code && !dest && open.len == 0) {
		ak_vec_free(&open);
		return (ak_val){.p = ak_retain(src)};
	}
	ak_buf b = {0};
	ak_buf_put(&b, s, len);
	if (code) ak_buf_u8(&b, '`');
	if (dest) ak_buf_u8(&b, ')');
	while (open.len > 0) {
		marker *m = &open.data[--open.len];
		ak_buf_put(&b, m->m, m->n);
	}
	ak_vec_free(&open);
	ak_str *r = ak_str_new((const char *)b.data, b.len);
	ak_buf_free(&b);
	return (ak_val){.p = r};
}

const ak_lib_entry ak_std_markdown[] = {
    {"markdown.read", read_fn},
    {"markdown.heal", heal_fn},
    {NULL, NULL},
};
