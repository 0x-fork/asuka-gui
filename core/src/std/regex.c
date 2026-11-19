// regex.c: std/regex's builtins: regular expressions in RE2's syntax
// (a subset: no \p classes), matched in time linear in the text by a
// Pike VM (Thompson's construction, threads carrying their captures),
// never by backtracking. Leftmost-first, as RE2 and Perl choose matches.
//
// A pattern compiles to a program kept in a bytes value (a Regex's
// field), so it's a value: compared, copied into spawn, frozen in a
// constant. Its layout, little-endian:
//
//	"AKRE" ninst nranges ncap nnames   (u32s after the magic)
//	ninst × {op u8, 0 0 0, x u32, y u32}
//	nranges × {lo u32, hi u32}          classes' sorted ranges
//	nnames bytes: each group's name (from 1) and a NUL ("" unnamed)
//
// The builtins (std/regex/regex.asuka declares them):
//
//	regex.program(pattern) -> bytes            throws a bad pattern
//	regex.test(program, s) -> bool
//	regex.search(program, s, limit) -> [[int]] the matches (all when
//	    limit < 0), each its groups' byte offsets: start, end, … (-1, -1
//	    for a group that took no part)
//	regex.substitute(program, s, template, limit) -> string   $1, ${1},
//	    $name, ${name} in template are the groups, $$ a $
//	regex.names(program) -> [string]
//	regex.quote(s) -> string                   s, matched literally
#include "std/std.h"
#include "rt/unicode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define STR(i) ((ak_str *)args[i].p)
#define BYTES(i) ((ak_bytes *)args[i].p)

enum {
	I_CHAR = 1,  // x: the code point
	I_CLASS,     // x: first range, y: how many
	I_ANY,       // any code point
	I_ANYNL,     // any but \n
	I_SPLIT,     // x first, then y
	I_JMP,       // x
	I_SAVE,      // x: the slot
	I_ASSERT,    // x: A_*
	I_MATCH,
};

enum { A_BOL = 1, A_EOL, A_BOT, A_EOT, A_WORD, A_NOWORD };

enum {
	MAX_INST = 50000,   // a program's instructions
	MAX_REPEAT = 1000,  // x{n,m}'s n and m
	MAX_DEPTH = 250,    // nested groups
	MAX_SLOTS = 1 << 20, // instructions × captures: the matcher's memory
};

typedef struct {
	uint8_t op;
	uint32_t x, y;
} inst;

typedef struct {
	uint32_t lo, hi;
} range;

// ---- Parsing ----

enum { N_EMPTY, N_LIT, N_CLASS, N_ANY, N_ASSERT, N_CAT, N_ALT, N_GROUP, N_REPEAT };

typedef struct {
	uint8_t kind;
	bool flag;           // N_LIT: fold case; N_ANY: \n too; N_REPEAT: greedy
	uint32_t a, b;       // N_LIT: rune; N_CLASS: ranges [a, a+b); N_ASSERT: a; N_GROUP: a cap (0: none); N_REPEAT: min a, max b (UINT32_MAX: none)
	int32_t first, next; // children: first, then each one's next (-1 none)
} node;

typedef struct {
	const char *s;
	size_t n, i;
	AK_VEC(node) nodes;
	AK_VEC(range) ranges;
	AK_VEC(char) names;  // each group's name and a NUL
	uint32_t ncap;
	int depth;
	char err[160];
} parser;

typedef struct {
	bool i, m, s, U;
} flags;

static int32_t mk(parser *p, uint8_t kind) {
	node n = {kind, false, 0, 0, -1, -1};
	ak_vec_push(&p->nodes, n);
	return (int32_t)p->nodes.len - 1;
}

static bool fail(parser *p, const char *fmt, ...) AK_PRINTF(2, 3);
static bool fail(parser *p, const char *fmt, ...) {
	if (p->err[0]) return false;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p->err, sizeof p->err, fmt, ap);
	va_end(ap);
	return false;
}

static bool more(parser *p) { return p->i < p->n; }
static char peek(parser *p) { return p->i < p->n ? p->s[p->i] : 0; }

static uint32_t rune(parser *p) { return ak_utf8_decode(p->s, p->n, &p->i); }

// Case folding: the other case of c, for ASCII, Latin-1, Latin
// Extended-A, Greek and Cyrillic; c itself when it has none.
static uint32_t other_case(uint32_t c) {
	if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) return c + 0x20;
	if ((c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7)) return c - 0x20;
	if (c == 0xff) return 0x178;
	if (c == 0x178) return 0xff;
	if ((c >= 0x100 && c <= 0x12f) || (c >= 0x132 && c <= 0x137) || (c >= 0x14a && c <= 0x177)) return c ^ 1;
	if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e)) return c & 1 ? c + 1 : c - 1;
	if (c >= 0x391 && c <= 0x3a9 && c != 0x3a2) return c + 0x20;
	if (c >= 0x3b1 && c <= 0x3c9 && c != 0x3c2) return c - 0x20;
	if (c == 0x3c2) return 0x3a3;  // final sigma
	if (c >= 0x410 && c <= 0x42f) return c + 0x20;
	if (c >= 0x430 && c <= 0x44f) return c - 0x20;
	if (c >= 0x400 && c <= 0x40f) return c + 0x50;
	if (c >= 0x450 && c <= 0x45f) return c - 0x50;
	return c;
}

// A class being built: ranges, unsorted, in the parser's temporary.
typedef AK_VEC(range) ranges;

static void add_range(ranges *r, uint32_t lo, uint32_t hi) {
	range x = {lo, hi};
	ak_vec_push(r, x);
}

static int cmp_range(const void *a, const void *b) {
	const range *x = a, *y = b;
	return x->lo < y->lo ? -1 : x->lo > y->lo;
}

// finish_class folds, sorts, merges and maybe negates r, and stores it in
// the parser's ranges as a class node.
static int32_t finish_class(parser *p, ranges *r, bool fold, bool neg) {
	if (fold) {
		uint32_t n = r->len;
		for (uint32_t k = 0; k < n; k++) {
			uint32_t lo = r->data[k].lo, hi = r->data[k].hi;
			if (lo < 'A') lo = 'A';
			if (hi > 0x45f) hi = 0x45f;
			for (uint32_t c = lo; c <= hi; c++) {
				uint32_t o = other_case(c);
				if (o != c) add_range(r, o, o);
			}
		}
	}
	if (r->len) qsort(r->data, r->len, sizeof(range), cmp_range);
	uint32_t m = 0;
	for (uint32_t k = 0; k < r->len; k++) {
		if (m && r->data[k].lo <= r->data[m - 1].hi + 1) {
			if (r->data[k].hi > r->data[m - 1].hi) r->data[m - 1].hi = r->data[k].hi;
		} else {
			r->data[m++] = r->data[k];
		}
	}
	r->len = m;
	int32_t id = mk(p, N_CLASS);
	p->nodes.data[id].a = p->ranges.len;
	if (neg) {
		uint32_t next = 0;
		for (uint32_t k = 0; k < r->len; k++) {
			if (r->data[k].lo > next) ak_vec_push(&p->ranges, ((range){next, r->data[k].lo - 1}));
			next = r->data[k].hi + 1;
		}
		if (next <= 0x10ffff) ak_vec_push(&p->ranges, ((range){next, 0x10ffff}));
	} else {
		for (uint32_t k = 0; k < r->len; k++) ak_vec_push(&p->ranges, r->data[k]);
	}
	p->nodes.data[id].b = p->ranges.len - p->nodes.data[id].a;
	return id;
}

// Perl's classes (\d \s \w) and POSIX's ([:alpha:]), ASCII as RE2's.
static bool perl_class(char c, ranges *r, bool *neg) {
	*neg = c >= 'A' && c <= 'Z';
	switch (c | 0x20) {
	case 'd':
		add_range(r, '0', '9');
		return true;
	case 's':
		add_range(r, '\t', '\n');
		add_range(r, '\f', '\r');
		add_range(r, ' ', ' ');
		return true;
	case 'w':
		add_range(r, '0', '9');
		add_range(r, 'A', 'Z');
		add_range(r, '_', '_');
		add_range(r, 'a', 'z');
		return true;
	}
	return false;
}

static bool posix_class(const char *name, size_t len, ranges *r) {
	static const struct {
		const char *name;
		const char *ranges;  // pairs
	} t[] = {
		{"alnum", "09AZaz"}, {"alpha", "AZaz"}, {"ascii", "\x01\x7f"}, {"blank", "\t\t  "},
		{"cntrl", "\x01\x1f\x7f\x7f"}, {"digit", "09"}, {"graph", "!~"}, {"lower", "az"},
		{"print", " ~"}, {"punct", "!/:@[`{~"}, {"space", "\t\r  "}, {"upper", "AZ"},
		{"word", "09AZ__az"}, {"xdigit", "09AFaf"},
	};
	for (size_t k = 0; k < AK_LEN(t); k++) {
		if (strlen(t[k].name) != len || memcmp(t[k].name, name, len) != 0) continue;
		for (const char *q = t[k].ranges; *q; q += 2) add_range(r, (uint8_t)q[0], (uint8_t)q[1]);
		if (strcmp(name, "ascii") == 0 || strcmp(name, "cntrl") == 0) add_range(r, 0, 0);
		return true;
	}
	return false;
}

static int hexdig(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1; }

// escape reads the escape after a backslash that stands for one code
// point (\n, \x41, \x{263a}, \.); false when it isn't one.
static bool escape(parser *p, uint32_t *out) {
	if (!more(p)) return fail(p, "a \\ at the end of the pattern");
	char c = p->s[p->i++];
	switch (c) {
	case 'a': *out = 7; return true;
	case 'f': *out = '\f'; return true;
	case 't': *out = '\t'; return true;
	case 'n': *out = '\n'; return true;
	case 'r': *out = '\r'; return true;
	case 'v': *out = '\v'; return true;
	case '0': *out = 0; return true;
	case 'x': {
		uint32_t v = 0;
		if (peek(p) == '{') {
			p->i++;
			int k = 0;
			while (more(p) && peek(p) != '}') {
				int d = hexdig(p->s[p->i++]);
				if (d < 0 || ++k > 6) return fail(p, "a bad \\x{…} escape");
				v = v * 16 + (uint32_t)d;
			}
			if (!more(p) || k == 0) return fail(p, "a bad \\x{…} escape");
			p->i++;
		} else {
			for (int k = 0; k < 2; k++) {
				int d = more(p) ? hexdig(p->s[p->i++]) : -1;
				if (d < 0) return fail(p, "a bad \\x escape (two hex digits)");
				v = v * 16 + (uint32_t)d;
			}
		}
		if (v > 0x10ffff) return fail(p, "\\x{%x} isn't a code point", v);
		*out = v;
		return true;
	}
	}
	if ((unsigned char)c < 0x80 && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
		*out = (uint8_t)c;  // punctuation stands for itself
		return true;
	}
	if (c == 'p' || c == 'P') return fail(p, "\\%c (Unicode classes) isn't supported", c);
	return fail(p, "\\%c isn't an escape regex knows", c);
}

static int32_t parse_class(parser *p, const flags *f) {
	// After '['.
	bool neg = false;
	if (peek(p) == '^') {
		neg = true;
		p->i++;
	}
	ranges r = {0};
	bool first = true;
	for (;;) {
		if (!more(p)) {
			ak_vec_free(&r);
			fail(p, "a [ without its ]");
			return -1;
		}
		char c = peek(p);
		if (c == ']' && !first) {
			p->i++;
			break;
		}
		first = false;
		if (c == '[' && p->i + 1 < p->n && p->s[p->i + 1] == ':') {
			const char *end = NULL;
			for (size_t k = p->i + 2; k + 1 < p->n; k++)
				if (p->s[k] == ':' && p->s[k + 1] == ']') {
					end = p->s + k;
					break;
				}
			if (end) {
				const char *name = p->s + p->i + 2;
				bool not = name < end && name[0] == '^';
				if (not) name++;
				ranges pr = {0};
				if (!posix_class(name, (size_t)(end - name), &pr)) {
					ak_vec_free(&pr);
					ak_vec_free(&r);
					fail(p, "[:%.*s:] isn't a class regex knows", (int)(end - name), name);
					return -1;
				}
				if (not) {
					if (pr.len > 1) qsort(pr.data, pr.len, sizeof(range), cmp_range);
					uint32_t next = 0;
					for (uint32_t k = 0; k < pr.len; k++) {
						if (pr.data[k].lo > next) add_range(&r, next, pr.data[k].lo - 1);
						if (pr.data[k].hi + 1 > next) next = pr.data[k].hi + 1;
					}
					add_range(&r, next, 0x10ffff);
				} else {
					for (uint32_t k = 0; k < pr.len; k++) ak_vec_push(&r, pr.data[k]);
				}
				ak_vec_free(&pr);
				p->i = (size_t)(end - p->s) + 2;
				continue;
			}
		}
		uint32_t lo;
		if (c == '\\') {
			p->i++;
			bool pneg;
			ranges pr = {0};
			if (more(p) && perl_class(peek(p), &pr, &pneg)) {
				p->i++;
				if (pneg) {
					uint32_t next = 0;
					for (uint32_t k = 0; k < pr.len; k++) {
						if (pr.data[k].lo > next) add_range(&r, next, pr.data[k].lo - 1);
						next = pr.data[k].hi + 1;
					}
					add_range(&r, next, 0x10ffff);
				} else {
					for (uint32_t k = 0; k < pr.len; k++) ak_vec_push(&r, pr.data[k]);
				}
				ak_vec_free(&pr);
				continue;
			}
			ak_vec_free(&pr);
			if (!escape(p, &lo)) {
				ak_vec_free(&r);
				return -1;
			}
		} else {
			lo = rune(p);
		}
		uint32_t hi = lo;
		if (peek(p) == '-' && p->i + 1 < p->n && p->s[p->i + 1] != ']') {
			p->i++;
			if (peek(p) == '\\') {
				p->i++;
				if (!escape(p, &hi)) {
					ak_vec_free(&r);
					return -1;
				}
			} else {
				hi = rune(p);
			}
			if (hi < lo) {
				ak_vec_free(&r);
				fail(p, "a range out of order in a class");
				return -1;
			}
		}
		add_range(&r, lo, hi);
	}
	int32_t id = finish_class(p, &r, f->i, neg);
	ak_vec_free(&r);
	return id;
}

static int32_t parse_alt(parser *p, flags *f);

// append adds child to parent's children.
static void append(parser *p, int32_t parent, int32_t child) {
	node *n = &p->nodes.data[parent];
	if (n->first < 0) {
		n->first = child;
		return;
	}
	int32_t k = n->first;
	while (p->nodes.data[k].next >= 0) k = p->nodes.data[k].next;
	p->nodes.data[k].next = child;
}

// read_flags reads flags up to ':' or ')' (after "(?"), into f.
static bool read_flags(parser *p, flags *f) {
	bool on = true;
	while (more(p) && peek(p) != ':' && peek(p) != ')') {
		char c = p->s[p->i++];
		switch (c) {
		case '-':
			if (!on) return fail(p, "two - in a group's flags");
			on = false;
			break;
		case 'i': f->i = on; break;
		case 'm': f->m = on; break;
		case 's': f->s = on; break;
		case 'U': f->U = on; break;
		default: return fail(p, "(?%c isn't a flag regex knows (i, m, s, U)", c);
		}
	}
	if (!more(p)) return fail(p, "a ( without its )");
	return true;
}

static int32_t parse_atom(parser *p, flags *f) {
	char c = peek(p);
	switch (c) {
	case '(': {
		p->i++;
		if (++p->depth > MAX_DEPTH) {
			fail(p, "groups nested too deep");
			return -1;
		}
		uint32_t cap = 0;
		flags inner = *f;
		if (peek(p) == '?') {
			p->i++;
			const char *name = NULL;
			size_t nlen = 0;
			if (peek(p) == 'P' || peek(p) == '<') {
				if (peek(p) == 'P') p->i++;
				if (peek(p) != '<') {
					fail(p, "a bad named group: (?P<name>…)");
					return -1;
				}
				p->i++;
				name = p->s + p->i;
				while (more(p) && peek(p) != '>') {
					char d = p->s[p->i];
					if (!(d == '_' || (d >= '0' && d <= '9') || (d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z'))) {
						fail(p, "a group's name is letters, digits and _");
						return -1;
					}
					p->i++;
				}
				nlen = (size_t)(p->s + p->i - name);
				if (!more(p) || nlen == 0) {
					fail(p, "a bad named group: (?P<name>…)");
					return -1;
				}
				p->i++;
				cap = ++p->ncap;
			} else {
				if (!read_flags(p, &inner)) return -1;
				if (p->s[p->i++] == ')') {
					// (?i): the flags for the rest of the group around.
					*f = inner;
					p->depth--;
					return mk(p, N_EMPTY);
				}
			}
			if (cap) {
				// Names (from group 1) in order: fill the unnamed before it.
				uint32_t have = 0;
				for (uint32_t k = 0; k < p->names.len; k++) have += p->names.data[k] == 0;
				while (have + 1 < cap) {
					ak_vec_push(&p->names, 0);
					have++;
				}
				for (size_t k = 0; k < nlen; k++) ak_vec_push(&p->names, name[k]);
				ak_vec_push(&p->names, 0);
			}
		} else {
			cap = ++p->ncap;
		}
		int32_t body = parse_alt(p, &inner);
		if (body < 0) return -1;
		if (peek(p) != ')') {
			fail(p, "a ( without its )");
			return -1;
		}
		p->i++;
		p->depth--;
		int32_t g = mk(p, N_GROUP);
		p->nodes.data[g].a = cap;
		p->nodes.data[g].first = body;
		return g;
	}
	case '[': {
		p->i++;
		return parse_class(p, f);
	}
	case '.': {
		p->i++;
		int32_t n = mk(p, N_ANY);
		p->nodes.data[n].flag = f->s;
		return n;
	}
	case '^':
	case '$': {
		p->i++;
		int32_t n = mk(p, N_ASSERT);
		p->nodes.data[n].a = c == '^' ? (f->m ? A_BOL : A_BOT) : (f->m ? A_EOL : A_EOT);
		return n;
	}
	case '\\': {
		p->i++;
		if (!more(p)) {
			fail(p, "a \\ at the end of the pattern");
			return -1;
		}
		char e = peek(p);
		int kind = e == 'b' ? A_WORD : e == 'B' ? A_NOWORD : e == 'A' ? A_BOT : e == 'z' ? A_EOT : 0;
		if (kind) {
			p->i++;
			int32_t n = mk(p, N_ASSERT);
			p->nodes.data[n].a = (uint32_t)kind;
			return n;
		}
		ranges r = {0};
		bool neg;
		if (perl_class(e, &r, &neg)) {
			p->i++;
			int32_t n = finish_class(p, &r, false, neg);
			ak_vec_free(&r);
			return n;
		}
		ak_vec_free(&r);
		uint32_t cp;
		if (!escape(p, &cp)) return -1;
		int32_t n = mk(p, N_LIT);
		p->nodes.data[n].a = cp;
		p->nodes.data[n].flag = f->i;
		return n;
	}
	case '*':
	case '+':
	case '?':
		fail(p, "%c with nothing before it to repeat", c);
		return -1;
	}
	int32_t n = mk(p, N_LIT);
	p->nodes.data[n].a = rune(p);
	p->nodes.data[n].flag = f->i;
	return n;
}

// repeat_count reads {n}, {n,} or {n,m} after '{'; false (and p->i
// unmoved) when it isn't one, so the '{' is a literal.
static bool repeat_count(parser *p, uint32_t *min, uint32_t *max) {
	size_t i = p->i + 1;
	uint64_t a = 0, b;
	size_t start = i;
	while (i < p->n && p->s[i] >= '0' && p->s[i] <= '9') a = a * 10 + (uint64_t)(p->s[i++] - '0'), a = a > 100000 ? 100000 : a;
	if (i == start || i >= p->n) return false;
	if (p->s[i] == '}') {
		b = a;
	} else if (p->s[i] == ',') {
		i++;
		size_t s2 = i;
		b = 0;
		while (i < p->n && p->s[i] >= '0' && p->s[i] <= '9') b = b * 10 + (uint64_t)(p->s[i++] - '0'), b = b > 100000 ? 100000 : b;
		if (i >= p->n || p->s[i] != '}') return false;
		if (i == s2) b = UINT32_MAX;
	} else {
		return false;
	}
	p->i = i + 1;
	*min = (uint32_t)a;
	*max = (uint32_t)b;
	return true;
}

static int32_t parse_repeat(parser *p, flags *f) {
	int32_t x = parse_atom(p, f);
	if (x < 0) return -1;
	for (;;) {
		char c = peek(p);
		uint32_t min, max;
		if (c == '*') min = 0, max = UINT32_MAX, p->i++;
		else if (c == '+') min = 1, max = UINT32_MAX, p->i++;
		else if (c == '?') min = 0, max = 1, p->i++;
		else if (c == '{' && repeat_count(p, &min, &max)) {
			if (min > MAX_REPEAT || (max != UINT32_MAX && max > MAX_REPEAT)) {
				fail(p, "a repeat count over %d", MAX_REPEAT);
				return -1;
			}
			if (max < min) {
				fail(p, "a repeat {n,m} with m less than n");
				return -1;
			}
		} else {
			return x;
		}
		if (p->nodes.data[x].kind == N_REPEAT) {
			fail(p, "a repeat of a repeat (write (?:x*)* to mean it)");
			return -1;
		}
		bool greedy = true;
		if (peek(p) == '?') {
			p->i++;
			greedy = false;
		}
		if (f->U) greedy = !greedy;
		int32_t r = mk(p, N_REPEAT);
		p->nodes.data[r].a = min;
		p->nodes.data[r].b = max;
		p->nodes.data[r].flag = greedy;
		p->nodes.data[r].first = x;
		x = r;
	}
}

static int32_t parse_cat(parser *p, flags *f) {
	int32_t cat = mk(p, N_CAT);
	while (more(p) && peek(p) != '|' && peek(p) != ')') {
		int32_t x = parse_repeat(p, f);
		if (x < 0) return -1;
		append(p, cat, x);
	}
	return cat;
}

static int32_t parse_alt(parser *p, flags *f) {
	int32_t first = parse_cat(p, f);
	if (first < 0 || peek(p) != '|') return first;
	int32_t alt = mk(p, N_ALT);
	append(p, alt, first);
	while (peek(p) == '|') {
		p->i++;
		int32_t x = parse_cat(p, f);
		if (x < 0) return -1;
		append(p, alt, x);
	}
	return alt;
}

// ---- Compiling ----

typedef struct {
	parser *p;
	AK_VEC(inst) code;
} compiler;

static uint32_t emit(compiler *c, uint8_t op, uint32_t x, uint32_t y) {
	inst i = {op, x, y};
	ak_vec_push(&c->code, i);
	return c->code.len - 1;
}

static bool compile(compiler *c, int32_t id) {
	if (c->code.len > MAX_INST) return fail(c->p, "the pattern is too large");
	node n = c->p->nodes.data[id];
	switch (n.kind) {
	case N_EMPTY:
		return true;
	case N_LIT: {
		uint32_t o = n.flag ? other_case(n.a) : n.a;
		if (o == n.a) {
			emit(c, I_CHAR, n.a, 0);
		} else {
			// Both cases: a class of the two, in the parser's ranges.
			uint32_t lo = n.a < o ? n.a : o, hi = n.a < o ? o : n.a;
			uint32_t at = c->p->ranges.len;
			ak_vec_push(&c->p->ranges, ((range){lo, lo}));
			ak_vec_push(&c->p->ranges, ((range){hi, hi}));
			emit(c, I_CLASS, at, 2);
		}
		return true;
	}
	case N_CLASS:
		emit(c, I_CLASS, n.a, n.b);
		return true;
	case N_ANY:
		emit(c, n.flag ? I_ANY : I_ANYNL, 0, 0);
		return true;
	case N_ASSERT:
		emit(c, I_ASSERT, n.a, 0);
		return true;
	case N_CAT:
		for (int32_t k = n.first; k >= 0; k = c->p->nodes.data[k].next)
			if (!compile(c, k)) return false;
		return true;
	case N_ALT: {
		// SPLIT L1, next; L1: e1; JMP end; next: SPLIT L2, … ; last: ek
		AK_VEC(uint32_t) jumps = {0};
		for (int32_t k = n.first; k >= 0; k = c->p->nodes.data[k].next) {
			if (c->p->nodes.data[k].next >= 0) {
				uint32_t split = emit(c, I_SPLIT, 0, 0);
				c->code.data[split].x = c->code.len;
				if (!compile(c, k)) {
					ak_vec_free(&jumps);
					return false;
				}
				ak_vec_push(&jumps, emit(c, I_JMP, 0, 0));
				c->code.data[split].y = c->code.len;
			} else if (!compile(c, k)) {
				ak_vec_free(&jumps);
				return false;
			}
		}
		for (uint32_t k = 0; k < jumps.len; k++) c->code.data[jumps.data[k]].x = c->code.len;
		ak_vec_free(&jumps);
		return true;
	}
	case N_GROUP:
		if (n.a) emit(c, I_SAVE, 2 * n.a, 0);
		if (!compile(c, n.first)) return false;
		if (n.a) emit(c, I_SAVE, 2 * n.a + 1, 0);
		return true;
	case N_REPEAT: {
		bool greedy = n.flag;
		for (uint32_t k = 0; k < n.a; k++) {
			uint32_t before = c->code.len;
			if (!compile(c, n.first)) return false;
			// A body that compiles to nothing does each time: its
			// copies, nested repeats of them, would cost compiles but
			// no instructions, which MAX_INST bounds.
			if (c->code.len == before) break;
		}
		if (n.b == UINT32_MAX) {
			// L: SPLIT body, out; body: e; JMP L; out:
			uint32_t split = emit(c, I_SPLIT, 0, 0);
			if (!compile(c, n.first)) return false;
			emit(c, I_JMP, split, 0);
			uint32_t body = split + 1, out = c->code.len;
			c->code.data[split].x = greedy ? body : out;
			c->code.data[split].y = greedy ? out : body;
			return true;
		}
		// x{0,k} after the min: (x(x(x)?)?)?, each SPLIT to the end.
		AK_VEC(uint32_t) splits = {0};
		for (uint32_t k = n.a; k < n.b; k++) {
			ak_vec_push(&splits, emit(c, I_SPLIT, 0, 0));
			if (!compile(c, n.first)) {
				ak_vec_free(&splits);
				return false;
			}
		}
		uint32_t out = c->code.len;
		for (uint32_t k = 0; k < splits.len; k++) {
			uint32_t s = splits.data[k];
			c->code.data[s].x = greedy ? s + 1 : out;
			c->code.data[s].y = greedy ? out : s + 1;
		}
		ak_vec_free(&splits);
		return true;
	}
	}
	return fail(c->p, "internal: a node of kind %d", n.kind);
}

static void put32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// program compiles pattern; NULL with err set when it's bad.
static ak_bytes *program(const char *pattern, size_t n, char *err, size_t errn) {
	parser p = {.s = pattern, .n = n};
	flags f = {0};
	int32_t root = parse_alt(&p, &f);
	if (root >= 0 && more(&p)) {
		fail(&p, "a ) without its (");
		root = -1;
	}
	compiler c = {&p, {0}};
	if (root >= 0) {
		emit(&c, I_SAVE, 0, 0);
		if (compile(&c, root)) {
			emit(&c, I_SAVE, 1, 0);
			emit(&c, I_MATCH, 0, 0);
			if (c.code.len > MAX_INST || (uint64_t)c.code.len * 2 * (p.ncap + 1) > MAX_SLOTS) fail(&p, "the pattern is too large");
		}
	}
	ak_bytes *out = NULL;
	if (!p.err[0]) {
		// Unnamed groups after the last named one.
		uint32_t have = 0;
		for (uint32_t k = 0; k < p.names.len; k++) have += p.names.data[k] == 0;
		while (have < p.ncap) {
			ak_vec_push(&p.names, 0);
			have++;
		}
		size_t size = 20 + (size_t)c.code.len * 12 + (size_t)p.ranges.len * 8 + p.names.len;
		out = ak_bytes_new(NULL, size);
		uint8_t *o = out->data;
		memcpy(o, "AKRE", 4);
		put32(o + 4, c.code.len);
		put32(o + 8, p.ranges.len);
		put32(o + 12, p.ncap + 1);
		put32(o + 16, p.names.len);
		o += 20;
		for (uint32_t k = 0; k < c.code.len; k++, o += 12) {
			o[0] = c.code.data[k].op, o[1] = o[2] = o[3] = 0;
			put32(o + 4, c.code.data[k].x);
			put32(o + 8, c.code.data[k].y);
		}
		for (uint32_t k = 0; k < p.ranges.len; k++, o += 8) {
			put32(o, p.ranges.data[k].lo);
			put32(o + 4, p.ranges.data[k].hi);
		}
		if (p.names.len) memcpy(o, p.names.data, p.names.len);
	} else {
		snprintf(err, errn, "%s", p.err);
	}
	ak_vec_free(&c.code);
	ak_vec_free(&p.nodes);
	ak_vec_free(&p.ranges);
	ak_vec_free(&p.names);
	return out;
}

// ---- Matching ----

typedef struct {
	inst *code;
	range *ranges;
	const char *names;
	uint32_t ninst, nranges, ncap, nnames;
	// What a match can start with (prefilter): a literal every match
	// starts with (lit, litn > 1), or the bytes one can (first); none
	// when a match may be empty or start with what isn't UTF-8.
	bool filter;
	uint8_t first[32];  // a bit a byte
	int single;         // the one byte first has, or -1
	char lit[32];
	size_t litn;
} prog;

// decode reads a program, checking it (a Regex can be made by hand).
static inst *decode(const ak_bytes *b, prog *p) {
	if (!b || b->len < 20 || memcmp(b->data, "AKRE", 4) != 0) return NULL;
	const uint8_t *d = b->data;
	p->ninst = get32(d + 4), p->nranges = get32(d + 8), p->ncap = get32(d + 12), p->nnames = get32(d + 16);
	if (p->ninst == 0 || p->ninst > MAX_INST + 8 || p->ncap == 0 || (uint64_t)p->ninst * 2 * p->ncap > MAX_SLOTS || p->nranges > (b->len - 20) / 8) return NULL;
	if (20 + (uint64_t)p->ninst * 12 + (uint64_t)p->nranges * 8 + p->nnames != b->len) return NULL;
	inst *code = ak_alloc(sizeof(inst) * p->ninst + sizeof(range) * (p->nranges ? p->nranges : 1));
	range *ranges = (range *)(code + p->ninst);
	const uint8_t *o = d + 20;
	for (uint32_t k = 0; k < p->ninst; k++, o += 12) {
		inst *i = &code[k];
		i->op = o[0], i->x = get32(o + 4), i->y = get32(o + 8);
		bool ok;
		switch (i->op) {
		case I_CHAR: ok = i->x <= 0x10ffff; break;
		case I_CLASS: ok = i->x <= p->nranges && i->y <= p->nranges - i->x; break;
		case I_ANY:
		case I_ANYNL:
		case I_MATCH: ok = true; break;
		case I_SPLIT: ok = i->x < p->ninst && i->y < p->ninst; break;
		case I_JMP: ok = i->x < p->ninst; break;
		case I_SAVE: ok = i->x < 2 * p->ncap; break;
		case I_ASSERT: ok = i->x >= A_BOL && i->x <= A_NOWORD; break;
		default: ok = false;
		}
		// Every instruction but MATCH and the jumps goes on to the next.
		if (ok && i->op != I_MATCH && i->op != I_JMP && i->op != I_SPLIT && k + 1 >= p->ninst) ok = false;
		if (!ok) {
			ak_free(code);
			return NULL;
		}
	}
	for (uint32_t k = 0; k < p->nranges; k++, o += 8) ranges[k] = (range){get32(o), get32(o + 4)};
	if (p->nnames && o[p->nnames - 1] != 0) {
		ak_free(code);
		return NULL;
	}
	p->code = code;
	p->ranges = ranges;
	p->names = (const char *)o;
	return code;
}

// A list of threads: a sparse set of pcs, each with its captures.
typedef struct {
	uint32_t *sparse, *dense;
	int32_t *caps;  // ninst × nslot
	uint32_t len;
} tlist;

typedef struct {
	uint32_t pc;
	int32_t slot;  // >= 0: restore cur[slot] to val
	int32_t val;
} frame;

typedef struct {
	prog *p;
	uint32_t nslot;
	tlist a, b;
	frame *stack;
	int32_t *cur, *match, *none;
	void *mem;
} vm;

static void vm_init(vm *m, prog *p) {
	m->p = p;
	m->nslot = 2 * p->ncap;
	size_t n = p->ninst, s = m->nslot;
	size_t words = 4 * n + 2 * n * s + 3 * s;
	m->mem = ak_alloc(words * sizeof(uint32_t) + sizeof(frame) * (2 * n + 2));
	uint32_t *w = m->mem;
	m->a.sparse = w, w += n;
	m->a.dense = w, w += n;
	m->b.sparse = w, w += n;
	m->b.dense = w, w += n;
	m->a.caps = (int32_t *)w, w += n * s;
	m->b.caps = (int32_t *)w, w += n * s;
	m->cur = (int32_t *)w, w += s;
	m->match = (int32_t *)w, w += s;
	m->none = (int32_t *)w, w += s;
	for (size_t k = 0; k < s; k++) m->none[k] = -1;
	m->stack = (frame *)w;
}

static bool has(const tlist *l, uint32_t pc) { return l->sparse[pc] < l->len && l->dense[l->sparse[pc]] == pc; }

static bool word(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

// The code points around a position: before (-1 at the start) and at
// (-1 at the end).
typedef struct {
	int64_t before, at;
} around;

static bool holds(uint32_t kind, around c) {
	switch (kind) {
	case A_BOT: return c.before < 0;
	case A_EOT: return c.at < 0;
	case A_BOL: return c.before < 0 || c.before == '\n';
	case A_EOL: return c.at < 0 || c.at == '\n';
	case A_WORD:
	case A_NOWORD: {
		bool w = (c.before >= 0 && word((uint32_t)c.before)) != (c.at >= 0 && word((uint32_t)c.at));
		return kind == A_WORD ? w : !w;
	}
	}
	return false;
}

// add follows pc's empty transitions from captures caps at pos, adding
// the threads that consume (or match) to l, in priority order.
static void add(vm *m, tlist *l, uint32_t pc0, const int32_t *caps, int32_t pos, around ctx) {
	uint32_t s = m->nslot;
	memcpy(m->cur, caps, sizeof(int32_t) * s);
	uint32_t top = 0;
	m->stack[top++] = (frame){pc0, -1, 0};
	while (top) {
		frame f = m->stack[--top];
		if (f.slot >= 0) {
			m->cur[f.slot] = f.val;
			continue;
		}
		uint32_t pc = f.pc;
		if (has(l, pc)) continue;
		l->sparse[pc] = l->len;
		l->dense[l->len++] = pc;
		const inst *i = &m->p->code[pc];
		switch (i->op) {
		case I_JMP:
			m->stack[top++] = (frame){i->x, -1, 0};
			break;
		case I_SPLIT:
			m->stack[top++] = (frame){i->y, -1, 0};
			m->stack[top++] = (frame){i->x, -1, 0};
			break;
		case I_SAVE:
			m->stack[top++] = (frame){0, (int32_t)i->x, m->cur[i->x]};
			m->cur[i->x] = pos;
			m->stack[top++] = (frame){pc + 1, -1, 0};
			break;
		case I_ASSERT:
			if (holds(i->x, ctx)) m->stack[top++] = (frame){pc + 1, -1, 0};
			break;
		default:
			memcpy(l->caps + (size_t)pc * s, m->cur, sizeof(int32_t) * s);
		}
	}
}

static bool in_class(const prog *p, const inst *i, uint32_t c) {
	const range *r = p->ranges + i->x;
	uint32_t lo = 0, hi = i->y;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		if (c < r[mid].lo) hi = mid;
		else if (c > r[mid].hi) lo = mid + 1;
		else return true;
	}
	return false;
}

// prev_rune is the code point that ends at s[pos] (-1 at the start).
static int64_t prev_rune(const char *s, size_t pos) {
	if (pos == 0) return -1;
	size_t k = pos - 1;
	while (k > 0 && pos - k < 4 && ((uint8_t)s[k] & 0xc0) == 0x80) k--;
	size_t j = k;
	uint32_t c = ak_utf8_decode(s, pos, &j);
	if (j != pos) return 0xfffd;  // the bytes before weren't one sequence
	return c;
}

// ---- What a match starts with ----
//
// A match starts where a thread from the program's first instruction
// consumes its first character: so the bytes that can begin one are the
// first bytes of what the consuming instructions its empty transitions
// reach take. Where no thread is alive, the matcher skips to the next
// such byte (memchr, or memmem for a literal prefix), as RE2's and the
// regex crate's prefilters do: the same matches, sooner.

static void first_byte(prog *p, uint8_t b) { p->first[b >> 3] |= (uint8_t)(1u << (b & 7)); }

// first_bytes adds the lead bytes of the code points lo to hi (none of
// them U+FFFD, which any byte that isn't UTF-8 reads as).
static void first_bytes(prog *p, uint32_t lo, uint32_t hi) {
	static const uint32_t band_lo[4] = {0, 0x80, 0x800, 0x10000}, band_hi[4] = {0x7f, 0x7ff, 0xffff, 0x10ffff};
	for (int k = 0; k < 4; k++) {
		uint32_t a = lo > band_lo[k] ? lo : band_lo[k], z = hi < band_hi[k] ? hi : band_hi[k];
		if (a > z) continue;
		char ea[4], ez[4];
		ak_utf8_encode(a, ea);
		ak_utf8_encode(z, ez);
		for (unsigned c = (uint8_t)ea[0]; c <= (uint8_t)ez[0]; c++) first_byte(p, (uint8_t)c);
	}
}

bool ak_regex_prefilter_off;

static void prefilter(prog *p) {
	p->filter = false;
	if (ak_regex_prefilter_off) return;
	p->single = -1;
	p->litn = 0;
	memset(p->first, 0, sizeof p->first);
	// The literal prefix: characters, past saves and assertions (they
	// consume nothing), until anything else.
	for (uint32_t pc = 0; pc < p->ninst;) {
		const inst *i = &p->code[pc];
		if (i->op == I_SAVE || i->op == I_ASSERT) {
			pc++;
			continue;
		}
		if (i->op != I_CHAR || i->x == 0xfffd || p->litn + 4 > sizeof p->lit) break;
		p->litn += ak_utf8_encode(i->x, p->lit + p->litn);
		pc++;
	}
	// The first bytes: the empty transitions from 0, depth first.
	uint8_t *seen = ak_alloc(p->ninst);
	uint32_t *stack = ak_alloc(sizeof(uint32_t) * (p->ninst + 1));
	uint32_t top = 0;
	bool ok = true;
	stack[top++] = 0;
	while (top && ok) {
		uint32_t pc = stack[--top];
		if (seen[pc]) continue;
		seen[pc] = 1;
		const inst *i = &p->code[pc];
		switch (i->op) {
		case I_JMP: stack[top++] = i->x; break;
		case I_SPLIT: stack[top++] = i->y, stack[top++] = i->x; break;
		case I_SAVE:
		case I_ASSERT: stack[top++] = pc + 1; break;  // an assertion may fail: what's after it is a superset
		case I_CHAR:
			if (i->x == 0xfffd) ok = false;
			else first_bytes(p, i->x, i->x);
			break;
		case I_CLASS:
			for (uint32_t r = 0; r < i->y && ok; r++) {
				const range *g = &p->ranges[i->x + r];
				if (g->lo <= 0xfffd && 0xfffd <= g->hi) ok = false;
				else first_bytes(p, g->lo, g->hi);
			}
			break;
		default:  // ANY, ANYNL take any byte; MATCH: a match may be empty
			ok = false;
		}
	}
	ak_free(seen);
	ak_free(stack);
	if (!ok) return;
	int count = 0;
	for (int b = 0; b < 256; b++)
		if (p->first[b >> 3] >> (b & 7) & 1) count++, p->single = b;
	if (count == 0 || count == 256) return;
	if (count > 1) p->single = -1;
	p->filter = true;
}

// skip is the first position from pos where a match can start, n when
// none can.
static size_t skip(const prog *p, const char *s, size_t n, size_t pos) {
	if (p->litn > 1) {
		int64_t at = ak_mem_find(s + pos, n - pos, p->lit, p->litn);
		return at < 0 ? n : pos + (size_t)at;
	}
	if (p->single >= 0) {
		const char *q = memchr(s + pos, p->single, n - pos);
		return q ? (size_t)(q - s) : n;
	}
	while (pos < n && !(p->first[(uint8_t)s[pos] >> 3] >> ((uint8_t)s[pos] & 7) & 1)) pos++;
	return pos;
}

// exec finds the leftmost-first match in s[0:n] at or after start; its
// captures in m->match.
static bool exec(vm *m, const char *s, size_t n, size_t start) {
	prog *p = m->p;
	uint32_t ns = m->nslot;
	tlist *cl = &m->a, *nl = &m->b;
	cl->len = nl->len = 0;
	bool matched = false;
	size_t pos = start;
	int64_t before = prev_rune(s, start);
	for (;;) {
		if (!matched && cl->len == 0 && p->filter) {
			// No thread alive: on to where a match can start (no match
			// is empty, so none starts at the end).
			size_t q = skip(p, s, n, pos);
			if (q >= n) break;
			if (q != pos) {
				pos = q;
				before = prev_rune(s, pos);
			}
		}
		size_t next = pos;
		int64_t at = pos < n ? (int64_t)ak_utf8_decode(s, n, &next) : -1;
		around ctx = {before, at};
		if (!matched) add(m, cl, 0, m->none, (int32_t)pos, ctx);
		if (cl->len == 0) break;
		nl->len = 0;
		size_t after = next;
		int64_t after_at = -1;
		if (after < n) {
			size_t t = after;
			after_at = ak_utf8_decode(s, n, &t);
		}
		around nctx = {at, after_at};
		for (uint32_t k = 0; k < cl->len; k++) {
			uint32_t pc = cl->dense[k];
			const inst *i = &p->code[pc];
			const int32_t *caps = cl->caps + (size_t)pc * ns;
			bool step = false;
			switch (i->op) {
			case I_MATCH:
				memcpy(m->match, caps, sizeof(int32_t) * ns);
				matched = true;
				k = cl->len;  // threads after it have lower priority
				continue;
			case I_CHAR: step = at == (int64_t)i->x; break;
			case I_CLASS: step = at >= 0 && in_class(p, i, (uint32_t)at); break;
			case I_ANY: step = at >= 0; break;
			case I_ANYNL: step = at >= 0 && at != '\n'; break;
			}
			if (step) add(m, nl, pc + 1, caps, (int32_t)next, nctx);
		}
		tlist *t = cl;
		cl = nl;
		nl = t;
		if (pos >= n) break;
		before = at;
		pos = next;
	}
	return matched;
}

static bool load(const ak_bytes *b, prog *p) {
	if (decode(b, p)) {
		prefilter(p);
		return true;
	}
	ak_throw_msg("regex: not a compiled pattern (make a Regex with regex.compile)");
	return false;
}

// ---- The builtins ----

static ak_val program_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	char err[200];
	ak_bytes *b = program(STR(0)->data, STR(0)->len, err, sizeof err);
	if (!b) {
		ak_throw_msg("regex: %s: %s", STR(0)->data, err);
		return (ak_val){0};
	}
	return (ak_val){.p = b};
}

static ak_val test_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	prog p;
	if (!load(BYTES(0), &p)) return (ak_val){0};
	vm m;
	vm_init(&m, &p);
	bool ok = exec(&m, STR(1)->data, STR(1)->len, 0);
	ak_free(m.mem);
	ak_free(p.code);
	return (ak_val){.i = ok};
}

// placed is whether a match's captures are where a compiled pattern
// puts them: the match from pos on, each group's start and end in order
// in the text, or both unset. A program made by hand can SAVE anywhere.
static bool placed(const int32_t *caps, uint32_t ns, size_t pos, size_t len) {
	if (caps[0] < 0 || (size_t)caps[0] < pos || caps[1] < caps[0] || (size_t)caps[1] > len) return false;
	for (uint32_t k = 2; k + 1 < ns; k += 2) {
		if (caps[k] < 0 && caps[k + 1] < 0) continue;
		if (caps[k] < 0 || caps[k + 1] < caps[k] || (size_t)caps[k + 1] > len) return false;
	}
	return true;
}

// each runs f on each match of p in s (up to limit, all when < 0):
// successive matches that don't overlap, an empty one right after the
// last skipped (as Go and RE2 do). A match no pattern makes throws.
static void each(prog *p, const ak_str *s, int64_t limit, void (*f)(void *ctx, const int32_t *caps, uint32_t ns), void *ctx) {
	vm m;
	vm_init(&m, p);
	size_t pos = 0;
	int64_t last = -1, count = 0;
	while ((limit < 0 || count < limit) && pos <= s->len) {
		if (!exec(&m, s->data, s->len, pos)) break;
		if (!placed(m.match, m.nslot, pos, s->len)) {
			ak_throw_msg("regex: not a compiled pattern (make a Regex with regex.compile)");
			break;
		}
		int32_t a = m.match[0], b = m.match[1];
		if (a == b && a == last) {
			// Empty, right after the last: try one character on.
			if ((size_t)a >= s->len) break;
			size_t k = (size_t)a;
			ak_utf8_decode(s->data, s->len, &k);
			pos = k;
			continue;
		}
		f(ctx, m.match, m.nslot);
		count++;
		last = b;
		if (a == b) {
			if ((size_t)b >= s->len) break;
			size_t k = (size_t)b;
			ak_utf8_decode(s->data, s->len, &k);
			pos = k;
		} else {
			pos = (size_t)b;
		}
	}
	ak_free(m.mem);
}

static void collect(void *ctx, const int32_t *caps, uint32_t ns) {
	ak_arr **out = ctx;
	ak_arr *one = ak_arr_new(AK_K_INT, NULL, ns);
	for (uint32_t k = 0; k < ns; k++) one->data[k].i = caps[k];
	one->len = ns;
	ak_arr_push(out, (ak_val){.p = one});
}

static ak_val search_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	prog p;
	if (!load(BYTES(0), &p)) return (ak_val){0};
	ak_arr *out = ak_arr_new(AK_K_REF, NULL, 0);
	each(&p, STR(1), args[2].i, collect, &out);
	ak_free(p.code);
	if (ak_error_pending()) {
		ak_release(out);
		return (ak_val){0};
	}
	return (ak_val){.p = out};
}

// group finds the group a template names (a number or a name); -1 none.
static int64_t group(const prog *p, const char *name, size_t len) {
	bool number = len > 0;
	int64_t v = 0;
	for (size_t k = 0; k < len; k++) {
		if (name[k] < '0' || name[k] > '9') number = false;
		else if (v < 1000000) v = v * 10 + (name[k] - '0');
	}
	if (number) return v < p->ncap ? v : -1;
	const char *q = p->names;
	for (uint32_t g = 1; g < p->ncap && q < p->names + p->nnames; g++) {
		size_t l = strlen(q);
		if (l == len && memcmp(q, name, len) == 0) return g;
		q += l + 1;
	}
	return -1;
}

typedef struct {
	const prog *p;
	const ak_str *s, *tmpl;
	ak_buf out;
	size_t pos;
} repl;

static void replace_one(void *ctx, const int32_t *caps, uint32_t ns) {
	AK_UNUSED(ns);
	repl *r = ctx;
	ak_buf_put(&r->out, r->s->data + r->pos, (size_t)caps[0] - r->pos);
	const char *t = r->tmpl->data;
	size_t tn = r->tmpl->len;
	for (size_t k = 0; k < tn;) {
		if (t[k] != '$' || k + 1 >= tn) {
			ak_buf_u8(&r->out, (uint8_t)t[k++]);
			continue;
		}
		if (t[k + 1] == '$') {
			ak_buf_u8(&r->out, '$');
			k += 2;
			continue;
		}
		const char *name;
		size_t len, end;
		if (t[k + 1] == '{') {
			const char *close = memchr(t + k + 2, '}', tn - k - 2);
			if (!close) {
				ak_buf_u8(&r->out, (uint8_t)t[k++]);
				continue;
			}
			name = t + k + 2, len = (size_t)(close - name), end = (size_t)(close - t) + 1;
		} else {
			size_t j = k + 1;
			while (j < tn && word((uint8_t)t[j])) j++;
			if (j == k + 1) {
				ak_buf_u8(&r->out, (uint8_t)t[k++]);
				continue;
			}
			name = t + k + 1, len = j - k - 1, end = j;
		}
		int64_t g = group(r->p, name, len);
		if (g >= 0 && caps[2 * g] >= 0) ak_buf_put(&r->out, r->s->data + caps[2 * g], (size_t)(caps[2 * g + 1] - caps[2 * g]));
		k = end;
	}
	r->pos = (size_t)caps[1];
}

static ak_val replace_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	prog p;
	if (!load(BYTES(0), &p)) return (ak_val){0};
	repl r = {&p, STR(1), STR(2), {0}, 0};
	each(&p, STR(1), args[3].i, replace_one, &r);
	if (ak_error_pending()) {
		ak_buf_free(&r.out);
		ak_free(p.code);
		return (ak_val){0};
	}
	ak_buf_put(&r.out, r.s->data + r.pos, r.s->len - r.pos);
	ak_str *s = ak_str_new((const char *)r.out.data, r.out.len);
	ak_buf_free(&r.out);
	ak_free(p.code);
	return (ak_val){.p = s};
}

static ak_val names_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	prog p;
	if (!load(BYTES(0), &p)) return (ak_val){0};
	ak_arr *out = ak_str_arr(p.ncap);
	const char *q = p.names;
	for (uint32_t g = 1; g < p.ncap; g++) {
		size_t l = q < p.names + p.nnames ? strlen(q) : 0;
		ak_str_arr_push(&out, q, l);
		q += l + 1;
	}
	ak_free(p.code);
	return (ak_val){.p = out};
}

static ak_val quote_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *s = STR(0);
	ak_buf b = {0};
	for (uint32_t k = 0; k < s->len; k++) {
		char c = s->data[k];
		if (c && strchr("\\.+*?()|[]{}^$", c)) ak_buf_u8(&b, '\\');
		ak_buf_u8(&b, (uint8_t)c);
	}
	ak_str *r = ak_str_new((const char *)b.data, b.len);
	ak_buf_free(&b);
	return (ak_val){.p = r};
}

const ak_lib_entry ak_std_regex[] = {
	{"regex.program", program_fn},
	{"regex.test", test_fn},
	{"regex.search", search_fn},
	{"regex.substitute", replace_fn},
	{"regex.names", names_fn},
	{"regex.quote", quote_fn},
	{NULL, NULL},
};
