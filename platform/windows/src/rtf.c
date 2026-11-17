// rtf.c: RTF in and out of a rich edit control (rtf.h).
#include "rtf.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *grow(void *p, size_t size) {
	void *q = realloc(p, size);
	if (!q && size) abort();
	return q;
}

static char *dupn(const char *s, size_t n) {
	char *d = grow(NULL, n + 1);
	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

static void put(rtf_buf *b, const char *s, size_t n) {
	if (b->n + n + 1 > b->cap) {
		b->cap = (b->n + n + 1) * 2;
		b->p = grow(b->p, b->cap);
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

static void puts_(rtf_buf *b, const char *s) { put(b, s, strlen(s)); }

static void putc_(rtf_buf *b, char c) { put(b, &c, 1); }

static void putf(rtf_buf *b, const char *fmt, ...) {
	char tmp[96];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n > 0) put(b, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

// next decodes a UTF-8 character at s[*i] (U+FFFD for a bad byte).
static uint32_t next(const char *s, size_t len, size_t *i) {
	const unsigned char *u = (const unsigned char *)s;
	unsigned char c = u[*i];
	uint32_t r;
	size_t k;
	if (c < 0x80) {
		(*i)++;
		return c;
	} else if ((c & 0xE0) == 0xC0) r = c & 0x1F, k = 1;
	else if ((c & 0xF0) == 0xE0) r = c & 0x0F, k = 2;
	else if ((c & 0xF8) == 0xF0) r = c & 0x07, k = 3;
	else {
		(*i)++;
		return 0xFFFD;
	}
	for (size_t j = 1; j <= k; j++) {
		if (*i + j >= len || (u[*i + j] & 0xC0) != 0x80) {
			(*i)++;
			return 0xFFFD;
		}
		r = r << 6 | (u[*i + j] & 0x3F);
	}
	*i += k + 1;
	return r;
}

static void put_utf8(rtf_buf *b, uint32_t r) {
	char o[4];
	if (r < 0x80) o[0] = (char)r, put(b, o, 1);
	else if (r < 0x800) o[0] = (char)(0xC0 | r >> 6), o[1] = (char)(0x80 | (r & 0x3F)), put(b, o, 2);
	else if (r < 0x10000) o[0] = (char)(0xE0 | r >> 12), o[1] = (char)(0x80 | (r >> 6 & 0x3F)), o[2] = (char)(0x80 | (r & 0x3F)), put(b, o, 3);
	else o[0] = (char)(0xF0 | r >> 18), o[1] = (char)(0x80 | (r >> 12 & 0x3F)), o[2] = (char)(0x80 | (r >> 6 & 0x3F)), o[3] = (char)(0x80 | (r & 0x3F)), put(b, o, 4);
}

size_t rtf_utf16_len(const char *s, size_t len) {
	size_t n = 0;
	for (size_t i = 0; i < len;) n += next(s, len, &i) >= 0x10000 ? 2 : 1;
	return n;
}

// escape writes UTF-8 text as RTF text: its specials escaped, what isn't
// ASCII as \u escapes (UTF-16 units, a ? for readers without them).
static void escape(rtf_buf *b, const char *s, size_t len) {
	for (size_t i = 0; i < len;) {
		uint32_t r = next(s, len, &i);
		if (r == '\\' || r == '{' || r == '}') {
			putc_(b, '\\');
			putc_(b, (char)r);
		} else if (r == '\t') {
			puts_(b, "\\tab ");
		} else if (r >= 0x20 && r < 0x80) {
			putc_(b, (char)r);
		} else if (r < 0x20) {
		} else if (r >= 0x10000) {
			uint32_t v = r - 0x10000;
			putf(b, "\\u%d?", (int)(int16_t)(uint16_t)(0xD800 + (v >> 10)));
			putf(b, "\\u%d?", (int)(int16_t)(uint16_t)(0xDC00 + (v & 0x3FF)));
		} else {
			putf(b, "\\u%d?", (int)(int16_t)(uint16_t)r);
		}
	}
}

static bool eq_color(bool ha, rtf_color a, bool hb, rtf_color b) {
	return ha == hb && (!ha || (a.r == b.r && a.g == b.g && a.b == b.b));
}

static bool eq_str(const char *a, const char *b) {
	if (!a) a = "";
	if (!b) b = "";
	return strcmp(a, b) == 0;
}

// same reports whether a and b look the same.
static bool same(const rtf_run *a, const rtf_run *b) {
	return a->bold == b->bold && a->italic == b->italic && a->underline == b->underline && a->strike == b->strike &&
		a->mono == b->mono && eq_str(a->link, b->link) && a->size == b->size && eq_color(a->has_color, a->color, b->has_color, b->color) &&
		eq_color(a->has_bg, a->bg, b->has_bg, b->bg);
}

typedef struct {
	rtf_color *v;
	uint32_t n, cap;
} color_table;

static int color_index(color_table *t, rtf_color c) {
	for (uint32_t i = 0; i < t->n; i++)
		if (t->v[i].r == c.r && t->v[i].g == c.g && t->v[i].b == c.b) return (int)i + 1;
	if (t->n == t->cap) {
		t->cap = t->cap ? t->cap * 2 : 8;
		t->v = grow(t->v, t->cap * sizeof *t->v);
	}
	t->v[t->n++] = c;
	return (int)t->n;
}

void rtf_write(const rtf_run *runs, uint32_t n, const rtf_doc *d, rtf_buf *out) {
	color_table colors = {0};
	color_index(&colors, d->color);
	rtf_buf body = {0};
	int half = (int)(d->size * 2 + 0.5f);
	for (uint32_t k = 0; k < n; k++) {
		const rtf_run *r = &runs[k];
		size_t start = 0;
		for (size_t i = 0; i <= r->len; i++) {
			if (i < r->len && r->text[i] != '\n') continue;
			if (start > 0) puts_(&body, "\\par ");
			size_t tn = i - start;
			const char *t = r->text + start;
			start = i + 1;
			if (!tn) continue;
			putc_(&body, '{');
			int size = r->size > 0 ? (int)(r->size * 2 + 0.5f) : half;
			putf(&body, "\\f%d\\fs%d", r->mono ? 1 : 0, size);
			if (r->bold) puts_(&body, "\\b");
			if (r->italic) puts_(&body, "\\i");
			if (r->underline) puts_(&body, "\\ul");
			if (r->strike) puts_(&body, "\\strike");
			if (r->has_color) putf(&body, "\\cf%d", color_index(&colors, r->color));
			else puts_(&body, "\\cf1");
			if (r->has_bg) {
				int c = color_index(&colors, r->bg);
				putf(&body, "\\chcbpat%d\\highlight%d", c, c);
			}
			putc_(&body, ' ');
			if (r->link && *r->link) {
				puts_(&body, "{\\field{\\*\\fldinst{HYPERLINK \"");
				// Its characters whole (a byte at a time, one beyond ASCII
				// would be U+FFFD); its quotes, which end it, escaped.
				for (const char *l = r->link; *l;) {
					if (*l == '"') {
						puts_(&body, "%22");
						l++;
						continue;
					}
					size_t q = strcspn(l, "\"");
					escape(&body, l, q);
					l += q;
				}
				puts_(&body, "\"}}{\\fldrslt{");
				escape(&body, t, tn);
				puts_(&body, "}}}");
			} else {
				escape(&body, t, tn);
			}
			putc_(&body, '}');
		}
	}
	out->n = 0;
	puts_(out, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl{\\f0\\fnil ");
	escape(out, d->face, strlen(d->face));
	puts_(out, ";}{\\f1\\fmodern\\fprq1 ");
	escape(out, d->mono_face, strlen(d->mono_face));
	puts_(out, ";}}{\\colortbl ;");
	for (uint32_t i = 0; i < colors.n; i++) putf(out, "\\red%d\\green%d\\blue%d;", colors.v[i].r, colors.v[i].g, colors.v[i].b);
	putf(out, "}\\pard\\f0\\fs%d\\cf1 ", half);
	if (body.n) put(out, body.p, body.n);
	puts_(out, "}");
	free(body.p);
	free(colors.v);
}

// ---- Reading ----

// fold_eq reports whether s (n bytes) is t, ASCII case aside.
static bool fold_eq(const char *s, size_t n, const char *t) {
	if (strlen(t) != n) return false;
	for (size_t i = 0; i < n; i++)
		if (tolower((unsigned char)s[i]) != tolower((unsigned char)t[i])) return false;
	return true;
}


// A group's character formatting (its run's text and link unused).
typedef struct {
	rtf_run run;
	int font;
	int uc;        // the characters after a \u to skip
	bool skip;     // a destination whose text isn't the document's
	bool fldinst;  // a field's instruction
	bool fldrslt;  // a field's result: its text, linked
	int field;     // the instruction's text (an index in fields), or -1
	bool fonttbl, colortbl;
} state;

typedef struct {
	const char *data;
	size_t len, i;
	state *stack;
	uint32_t nstack, capstack;
	state st;
	rtf_runs *runs;
	int *mono_fonts;  // font numbers that are monospaced
	uint32_t nmono, capmono;
	struct {
		bool set;
		rtf_color c;
	} * colors;  // \colortbl's, 0 the auto color
	uint32_t ncolors, capcolors;
	rtf_color color;  // the table entry being read
	uint16_t *text;   // UTF-16 units waiting (a surrogate pair's halves)
	size_t ntext, captext;
	rtf_run cur;  // the style text is appended in (its link owned)
	int pend;     // characters to skip after \u
	char *link;   // the URL of the field whose result is read
	int font_num;
	rtf_buf font_name;
	const char *mono_face;
	rtf_buf *fields;
	uint32_t nfields;
	uint16_t high;     // a surrogate pair's first half, put_unit's
	rtf_buf *high_to;  // and where it goes
} parser;

// put_unit appends a UTF-16 unit of a destination's text (a field's
// instruction, a font's name) to b as UTF-8: a surrogate pair's halves
// as their one character, a lone half as U+FFFD.
static void drop_high(parser *p) {
	if (!p->high) return;
	put_utf8(p->high_to, 0xFFFD);
	p->high = 0;
	p->high_to = NULL;
}

static void put_unit(parser *p, rtf_buf *b, uint16_t u) {
	if (p->high && (p->high_to != b || u < 0xDC00 || u >= 0xE000)) drop_high(p);
	if (u >= 0xD800 && u < 0xDC00) {
		p->high = u, p->high_to = b;
		return;
	}
	if (u >= 0xDC00 && u < 0xE000) {
		put_utf8(b, p->high ? 0x10000 + ((uint32_t)(p->high - 0xD800) << 10) + (u - 0xDC00) : 0xFFFD);
		p->high = 0;
		return;
	}
	put_utf8(b, u);
}

static bool is_mono(parser *p, int f) {
	for (uint32_t i = 0; i < p->nmono; i++)
		if (p->mono_fonts[i] == f) return true;
	return false;
}

static void set_mono(parser *p, int f) {
	if (is_mono(p, f)) return;
	if (p->nmono == p->capmono) {
		p->capmono = p->capmono ? p->capmono * 2 : 4;
		p->mono_fonts = grow(p->mono_fonts, p->capmono * sizeof *p->mono_fonts);
	}
	p->mono_fonts[p->nmono++] = f;
}

static void append_run(rtf_runs *r, rtf_run run) {
	if (r->n == r->cap) {
		r->cap = r->cap ? r->cap * 2 : 8;
		r->v = grow(r->v, r->cap * sizeof *r->v);
	}
	r->v[r->n++] = run;
}

// utf16_to_utf8 is UTF-16 units as UTF-8 (U+FFFD for a lone surrogate).
static void utf16_to_utf8(const uint16_t *u, size_t n, rtf_buf *b) {
	for (size_t i = 0; i < n; i++) {
		uint32_t r = u[i];
		if (r >= 0xD800 && r < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000) {
			r = 0x10000 + ((r - 0xD800) << 10) + (u[i + 1] - 0xDC00);
			i++;
		} else if (r >= 0xD800 && r < 0xE000) {
			r = 0xFFFD;
		}
		put_utf8(b, r);
	}
}

// flush ends the run text was appended to.
static void flush(parser *p) {
	if (!p->ntext) return;
	rtf_buf b = {0};
	utf16_to_utf8(p->text, p->ntext, &b);
	rtf_run r = p->cur;
	r.text = b.p ? b.p : dupn("", 0);
	r.len = b.n;
	r.link = p->cur.link && *p->cur.link ? dupn(p->cur.link, strlen(p->cur.link)) : NULL;
	append_run(p->runs, r);
	p->ntext = 0;
}

// ch takes a character of the current group.
static void ch(parser *p, uint16_t u) {
	if (p->st.colortbl && u == ';') {
		if (p->ncolors == p->capcolors) {
			p->capcolors = p->capcolors ? p->capcolors * 2 : 8;
			p->colors = grow(p->colors, p->capcolors * sizeof *p->colors);
		}
		bool zero = !p->color.r && !p->color.g && !p->color.b;
		p->colors[p->ncolors].set = !(p->ncolors == 0 && zero);  // the auto color
		p->colors[p->ncolors].c = p->color;
		p->ncolors++;
		p->color = (rtf_color){0};
		return;
	}
	if (p->st.fonttbl) {
		// A font's name, up to its ;: the monospaced face's is monospaced
		// (a control may name it without \fmodern or \fprq1).
		if (u == ';') {
			drop_high(p);
			const char *s = p->font_name.p ? p->font_name.p : "";
			size_t n = p->font_name.n;
			while (n && isspace((unsigned char)*s)) s++, n--;
			while (n && isspace((unsigned char)s[n - 1])) n--;
			if (p->mono_face && *p->mono_face && fold_eq(s, n, p->mono_face)) set_mono(p, p->font_num);
			p->font_name.n = 0;
		} else {
			put_unit(p, &p->font_name, u);
		}
		return;
	}
	if (p->st.colortbl) return;
	if (p->st.fldinst) {
		if (p->st.field >= 0) put_unit(p, &p->fields[p->st.field], u);
		return;
	}
	if (p->st.skip) return;
	rtf_run r = p->st.run;
	r.link = p->link;
	if (!same(&r, &p->cur)) {
		flush(p);
		free(p->cur.link);
		p->cur = r;
		p->cur.link = p->link ? dupn(p->link, strlen(p->link)) : NULL;
	}
	if (p->ntext == p->captext) {
		p->captext = p->captext ? p->captext * 2 : 64;
		p->text = grow(p->text, p->captext * sizeof *p->text);
	}
	p->text[p->ntext++] = u;
}

static bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool digit(char c) { return c >= '0' && c <= '9'; }

static const uint16_t cp1252_high[32] = {
	0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
	0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178,
};

// cp1252 is a Windows-1252 byte's character (\'hh escapes in \ansicpg1252).
static uint16_t cp1252(uint8_t b) { return b >= 0x80 && b <= 0x9F ? cp1252_high[b - 0x80] : b; }

static int hexval(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// color_at is \colortbl's color n (false: the auto color, or none).
static bool color_at(parser *p, int n, rtf_color *out) {
	if (n <= 0 || (uint32_t)n >= p->ncolors || !p->colors[n].set) return false;
	*out = p->colors[n].c;
	return true;
}

static bool word_is(const char *w, size_t n, const char *s) { return strlen(s) == n && memcmp(w, s, n) == 0; }

static void control(parser *p) {
	p->i++;  // the backslash
	if (p->i >= p->len) return;
	char c = p->data[p->i];
	if (!letter(c)) {
		p->i++;
		switch (c) {
		case '\\': case '{': case '}':
			ch(p, (uint16_t)c);
			break;
		case '\'':
			if (p->i + 2 <= p->len) {
				int a = hexval(p->data[p->i]), b = hexval(p->data[p->i + 1]);
				if (a >= 0 && b >= 0) {
					if (p->pend > 0) p->pend--;
					else ch(p, cp1252((uint8_t)(a << 4 | b)));
				}
				p->i += 2;
			}
			break;
		case '*':
			p->st.skip = true;
			break;
		case '~':
			ch(p, 0xA0);
			break;
		case '_':
			ch(p, '-');
			break;
		}
		return;
	}
	size_t start = p->i;
	while (p->i < p->len && letter(p->data[p->i])) p->i++;
	const char *w = p->data + start;
	size_t wn = p->i - start;
	// A parameter is 32 bits (a long is, on Windows): a longer one
	// stops growing at its largest.
	int32_t num = 0;
	bool has = false;
	if (p->i < p->len && (p->data[p->i] == '-' || digit(p->data[p->i]))) {
		bool neg = p->data[p->i] == '-';
		if (neg) p->i++;
		while (p->i < p->len && digit(p->data[p->i])) {
			int d = p->data[p->i] - '0';
			num = num <= (INT32_MAX - d) / 10 ? num * 10 + d : INT32_MAX;
			p->i++;
		}
		if (neg) num = -num;
		has = true;
	}
	if (p->i < p->len && p->data[p->i] == ' ') p->i++;
	bool on = !has || num != 0;
	rtf_run *r = &p->st.run;
	if (word_is(w, wn, "fonttbl")) {
		p->st.fonttbl = true;
		p->st.skip = false;
	} else if (word_is(w, wn, "colortbl")) {
		p->st.colortbl = true;
		p->ncolors = 0;
		p->color = (rtf_color){0};
	} else if (word_is(w, wn, "red")) {
		p->color.r = (uint8_t)num;
	} else if (word_is(w, wn, "green")) {
		p->color.g = (uint8_t)num;
	} else if (word_is(w, wn, "blue")) {
		p->color.b = (uint8_t)num;
	} else if (word_is(w, wn, "f")) {
		p->st.font = (int)num;
		if (p->st.fonttbl) {
			p->font_num = (int)num;
			drop_high(p);
			p->font_name.n = 0;
			return;
		}
		r->mono = is_mono(p, (int)num);
	} else if (word_is(w, wn, "fmodern")) {
		if (p->st.fonttbl) set_mono(p, p->font_num);
	} else if (word_is(w, wn, "fprq")) {
		if (p->st.fonttbl && num == 1) set_mono(p, p->font_num);
	} else if (word_is(w, wn, "b")) {
		r->bold = on;
	} else if (word_is(w, wn, "i")) {
		r->italic = on;
	} else if (word_is(w, wn, "ul")) {
		r->underline = on;
	} else if (word_is(w, wn, "ulnone")) {
		r->underline = false;
	} else if (word_is(w, wn, "strike")) {
		r->strike = on;
	} else if (word_is(w, wn, "cf")) {
		r->has_color = color_at(p, (int)num, &r->color);
	} else if (word_is(w, wn, "highlight") || word_is(w, wn, "chcbpat") || word_is(w, wn, "cb")) {
		r->has_bg = color_at(p, (int)num, &r->bg);
	} else if (word_is(w, wn, "plain")) {
		*r = (rtf_run){.mono = is_mono(p, 0)};
	} else if (word_is(w, wn, "par") || word_is(w, wn, "line")) {
		ch(p, '\n');
	} else if (word_is(w, wn, "tab")) {
		ch(p, '\t');
	} else if (word_is(w, wn, "uc")) {
		p->st.uc = (int)num;
	} else if (word_is(w, wn, "u")) {
		ch(p, (uint16_t)num);  // a negative one's UTF-16 unit too
		p->pend = p->st.uc;
	} else if (word_is(w, wn, "fldinst")) {
		p->st.fldinst = true;
		drop_high(p);  // before the fields move
		p->fields = grow(p->fields, (p->nfields + 1) * sizeof *p->fields);
		p->fields[p->nfields] = (rtf_buf){0};
		p->st.field = (int)p->nfields++;
	} else if (word_is(w, wn, "fldrslt")) {
		p->st.fldrslt = true;
	} else {
		static const char *skipped[] = {"pict", "object", "info", "stylesheet", "generator", "listtable", "listoverridetable", "themedata",
			"colorschememapping", "latentstyles", "datastore", "xmlnstbl", "rsidtbl", "mmathPr"};
		for (size_t k = 0; k < sizeof skipped / sizeof *skipped; k++)
			if (word_is(w, wn, skipped[k])) p->st.skip = true;
	}
}

// field_link reads a field instruction's HYPERLINK "url" into p->link.
static void field_link(parser *p, const rtf_buf *f) {
	const char *s = f->p ? f->p : "";
	size_t n = f->n;
	while (n && isspace((unsigned char)*s)) s++, n--;
	while (n && isspace((unsigned char)s[n - 1])) n--;
	if (n < 9 || memcmp(s, "HYPERLINK", 9) != 0) return;
	s += 9, n -= 9;
	while (n && isspace((unsigned char)*s)) s++, n--;
	while (n && isspace((unsigned char)s[n - 1])) n--;
	const char *u = s;
	size_t un = n;
	if (n && *s == '"') {
		const char *e = memchr(s + 1, '"', n - 1);
		if (e) u = s + 1, un = (size_t)(e - s - 1);
	} else {
		un = 0;
		while (un < n && !isspace((unsigned char)s[un])) un++;
	}
	free(p->link);
	p->link = dupn(u, un);
}

static void parse(parser *p) {
	p->st.uc = 1;
	p->st.field = -1;
	while (p->i < p->len) {
		char c = p->data[p->i];
		switch (c) {
		case '{':
			flush(p);
			if (p->nstack == p->capstack) {
				p->capstack = p->capstack ? p->capstack * 2 : 16;
				p->stack = grow(p->stack, p->capstack * sizeof *p->stack);
			}
			p->stack[p->nstack++] = p->st;
			p->i++;
			break;
		case '}':
			flush(p);
			drop_high(p);
			if (p->st.fldinst && p->st.field >= 0) field_link(p, &p->fields[p->st.field]);
			if (p->nstack) {
				bool was_rslt = p->st.fldrslt;
				p->st = p->stack[--p->nstack];
				if (was_rslt && !p->st.fldrslt) {
					free(p->link);
					p->link = NULL;
				}
			}
			p->i++;
			break;
		case '\\':
			control(p);
			break;
		case '\r': case '\n':
			p->i++;
			break;
		default:
			if (p->pend > 0) {
				p->pend--;
				p->i++;
				continue;
			}
			ch(p, (uint8_t)c);
			p->i++;
		}
	}
	flush(p);
}

static void run_free(rtf_run *r) {
	free(r->text);
	free(r->link);
}

void rtf_parse(const char *data, size_t len, const rtf_doc *d, rtf_runs *out) {
	rtf_runs raw = {0};
	parser p = {.data = data, .len = len, .runs = &raw, .mono_face = d->mono_face};
	parse(&p);
	free(p.stack);
	free(p.mono_fonts);
	free(p.colors);
	free(p.text);
	free(p.cur.link);
	free(p.link);
	free(p.font_name.p);
	for (uint32_t i = 0; i < p.nfields; i++) free(p.fields[i].p);
	free(p.fields);
	// Text in the document's color has none of its own; merge the runs of
	// one style; drop the final paragraph end.
	out->n = 0;
	for (uint32_t i = 0; i < raw.n; i++) {
		rtf_run *r = &raw.v[i];
		if (r->has_color && eq_color(true, r->color, true, d->color)) r->has_color = false;
		if (!r->len) {
			run_free(r);
			continue;
		}
		rtf_run *last = out->n ? &out->v[out->n - 1] : NULL;
		if (last && same(last, r)) {
			last->text = grow(last->text, last->len + r->len + 1);
			memcpy(last->text + last->len, r->text, r->len);
			last->len += r->len;
			last->text[last->len] = 0;
			run_free(r);
		} else {
			append_run(out, *r);
		}
	}
	free(raw.v);
	if (out->n) {
		rtf_run *last = &out->v[out->n - 1];
		if (last->len && last->text[last->len - 1] == '\n') {
			last->text[--last->len] = 0;
			if (!last->len) {
				run_free(last);
				out->n--;
			}
		}
	}
}

void rtf_trim(rtf_runs *r, size_t n) {
	size_t at = 0;
	for (uint32_t k = 0; k < r->n; k++) {
		rtf_run *run = &r->v[k];
		size_t u = rtf_utf16_len(run->text, run->len);
		if (at + u > n) {
			size_t keep = n > at ? n - at : 0, units = 0, i = 0;
			while (i < run->len) {
				size_t j = i;
				uint32_t c = next(run->text, run->len, &j);
				size_t w = c >= 0x10000 ? 2 : 1;
				if (units + w > keep) break;
				units += w;
				i = j;
			}
			run->len = i;
			run->text[i] = 0;
		}
		at += u;
	}
	while (r->n && !r->v[r->n - 1].len) run_free(&r->v[--r->n]);
}

void rtf_runs_free(rtf_runs *r) {
	for (uint32_t i = 0; i < r->n; i++) run_free(&r->v[i]);
	free(r->v);
	*r = (rtf_runs){0};
}
