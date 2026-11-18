#include "rt/rt.h"
#include "rt/unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

ak_str *ak_str_new(const char *s, size_t n) {
	ak_str *o = ak_obj_new(AK_TAG_STR, sizeof(ak_str) + n + 1);
	o->len = (uint32_t)n;
	if (n && s) memcpy(o->data, s, n);  // s NULL: the caller fills them
	o->data[n] = 0;
	return o;
}

ak_str *ak_str_empty(void) {
	static void *e;
	ak_str *s = ak_constant_get(&e);
	return s ? s : ak_constant_put(&e, ak_str_new("", 0));
}

ak_str *ak_str_concat(ak_str *a, ak_str *b) {
	if (a->len == 0) return ak_retain(b);
	if (b->len == 0) return ak_retain(a);
	ak_str *o = ak_obj_new(AK_TAG_STR, sizeof(ak_str) + a->len + b->len + 1);
	o->len = a->len + b->len;
	memcpy(o->data, a->data, a->len);
	memcpy(o->data + a->len, b->data, b->len);
	o->data[o->len] = 0;
	return o;
}

ak_str *ak_str_from_int(int64_t v) {
	char buf[24];
	int n = snprintf(buf, sizeof buf, "%lld", (long long)v);
	return ak_str_new(buf, (size_t)n);
}

// fmt_float writes the shortest form of v that reads back as v, as
// JavaScript prints numbers (ECMAScript's Number::toString): the fewest
// significant digits that round-trip, then plain digits for magnitudes
// from 1e-6 to below 1e21 (an integer without a fraction), an exponent
// otherwise.
static int fmt_float(char *buf, size_t cap, double v) {
	if (v != v) return snprintf(buf, cap, "NaN");
	if (v == INFINITY) return snprintf(buf, cap, "Infinity");
	if (v == -INFINITY) return snprintf(buf, cap, "-Infinity");
	if (v == 0) return snprintf(buf, cap, "0");
	// The shortest digits: d.ddde±x with as few as read back as v.
	char e[40];
	for (int p = 0; p < 17; p++) {
		snprintf(e, sizeof e, "%.*e", p, fabs(v));
		if (strtod(e, NULL) == fabs(v)) break;
	}
	char digits[24];
	int k = 0;
	const char *c = e;
	for (; *c && *c != 'e'; c++)
		if (*c >= '0' && *c <= '9') digits[k++] = *c;
	while (k > 1 && digits[k - 1] == '0') k--;
	// v is 0.d1..dk × 10^n.
	int n = atoi(c + 1) + 1;
	char out[48];
	size_t o = 0;
	if (v < 0) out[o++] = '-';
	if (k <= n && n <= 21) {
		memcpy(out + o, digits, (size_t)k);
		o += (size_t)k;
		for (int i = k; i < n; i++) out[o++] = '0';
	} else if (0 < n && n <= 21) {
		memcpy(out + o, digits, (size_t)n);
		o += (size_t)n;
		out[o++] = '.';
		memcpy(out + o, digits + n, (size_t)(k - n));
		o += (size_t)(k - n);
	} else if (-6 < n && n <= 0) {
		out[o++] = '0';
		out[o++] = '.';
		for (int i = n; i < 0; i++) out[o++] = '0';
		memcpy(out + o, digits, (size_t)k);
		o += (size_t)k;
	} else {
		out[o++] = digits[0];
		if (k > 1) {
			out[o++] = '.';
			memcpy(out + o, digits + 1, (size_t)(k - 1));
			o += (size_t)(k - 1);
		}
		o += (size_t)snprintf(out + o, sizeof out - o, "e%c%d", n - 1 < 0 ? '-' : '+', abs(n - 1));
	}
	out[o] = 0;
	return snprintf(buf, cap, "%s", out);
}

ak_str *ak_str_from_float(double v) {
	char buf[48];
	int n = fmt_float(buf, sizeof buf, v);
	return ak_str_new(buf, (size_t)n);
}

ak_str *ak_str_from_bool(bool v) {
	static void *t, *f;
	void **slot = v ? &t : &f;
	ak_str *s = ak_constant_get(slot);
	return s ? s : ak_constant_put(slot, ak_str_c(v ? "true" : "false"));
}

bool ak_str_eq(const ak_str *a, const ak_str *b) {
	if (a == b) return true;
	if (!a || !b || a->len != b->len) return false;
	if (a->hash && b->hash && a->hash != b->hash) return false;
	return memcmp(a->data, b->data, a->len) == 0;
}

int ak_str_cmp(const ak_str *a, const ak_str *b) {
	uint32_t n = a->len < b->len ? a->len : b->len;
	int c = memcmp(a->data, b->data, n);
	if (c) return c < 0 ? -1 : 1;
	return a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
}

uint32_t ak_str_hash(ak_str *s) {
	if (s->hash) return s->hash;
	uint32_t h = 2166136261u;  // FNV-1a
	for (uint32_t i = 0; i < s->len; i++) {
		h ^= (uint8_t)s->data[i];
		h *= 16777619u;
	}
	if (h == 0) h = 1;
	// Immortal strings (constants) may be read from worker threads: their
	// hash is computed once, as they become immortal.
	if (s->h.rc != AK_RC_IMMORTAL) s->hash = h;
	return h;
}

ak_str *ak_str_slice(ak_str *s, int64_t from, int64_t to) {
	if (from < 0) from = 0;
	if (to > s->len) to = s->len;
	if (from >= to) return ak_str_empty();
	if (from == 0 && to == s->len) return ak_retain(s);
	return ak_str_new(s->data + from, (size_t)(to - from));
}

int64_t ak_str_count(const ak_str *s) {
	int64_t n = 0;
	for (uint32_t i = 0; i < s->len; i++)
		if (((uint8_t)s->data[i] & 0xc0) != 0x80) n++;
	return n;
}

int64_t ak_str_find(const ak_str *s, const ak_str *sub) {
	if (sub->len == 0) return 0;
	if (sub->len > s->len) return -1;
	for (uint32_t i = 0; i + sub->len <= s->len; i++)
		if (s->data[i] == sub->data[0] && memcmp(s->data + i, sub->data, sub->len) == 0) return i;
	return -1;
}

bool ak_str_has_prefix(const ak_str *s, const ak_str *p) {
	return p->len <= s->len && memcmp(s->data, p->data, p->len) == 0;
}

bool ak_str_has_suffix(const ak_str *s, const ak_str *p) {
	return p->len <= s->len && memcmp(s->data + s->len - p->len, p->data, p->len) == 0;
}

bool ak_str_contains(const ak_str *s, const ak_str *sub) { return ak_str_find(s, sub) >= 0; }

static ak_str *map_ascii(ak_str *s, bool upper) {
	bool change = false;
	for (uint32_t i = 0; i < s->len && !change; i++) {
		char c = s->data[i];
		change = upper ? (c >= 'a' && c <= 'z') : (c >= 'A' && c <= 'Z');
	}
	if (!change) return ak_retain(s);
	ak_str *o = ak_str_new(s->data, s->len);
	for (uint32_t i = 0; i < o->len; i++) {
		char c = o->data[i];
		if (upper && c >= 'a' && c <= 'z') o->data[i] = (char)(c - 32);
		if (!upper && c >= 'A' && c <= 'Z') o->data[i] = (char)(c + 32);
	}
	return o;
}

static bool ascii_only(const ak_str *s) {
	for (uint32_t i = 0; i < s->len; i++)
		if ((uint8_t)s->data[i] & 0x80) return false;
	return true;
}

// final_sigma: whether the Σ at s[i:j] ends a word (SpecialCasing's
// Final_Sigma): a cased letter before it, case-ignorables aside, and
// none after it.
static bool final_sigma(const char *s, size_t n, size_t i, size_t j) {
	bool before = false;
	for (size_t k = i; k > 0;) {
		size_t b = k - 1;
		while (b > 0 && ((uint8_t)s[b] & 0xc0) == 0x80) b--;
		size_t t = b;
		uint16_t p = ak_uprops(ak_utf8_decode(s, n, &t));
		k = b;
		if (p & AK_UP_CASE_IGNORABLE) continue;
		before = (p & AK_UP_CASED) != 0;
		break;
	}
	if (!before) return false;
	for (size_t k = j; k < n;) {
		uint16_t p = ak_uprops(ak_utf8_decode(s, n, &k));
		if (p & AK_UP_CASE_IGNORABLE) continue;
		return !(p & AK_UP_CASED);
	}
	return true;
}

// map_case maps each code point as the Unicode's full mappings without
// a language's rules say (Rust's to_lowercase, JavaScript's
// toLowerCase): ß is SS in upper case, a word's last Σ is ς in lower.
static ak_str *map_case(ak_str *s, ak_case_kind k) {
	if (ascii_only(s)) {
		if (k == AK_CASE_UPPER) return map_ascii(s, true);
		return map_ascii(s, false);  // folding ASCII is lowering it
	}
	ak_strb b = {0};
	bool changed = false;
	for (size_t i = 0; i < s->len;) {
		size_t from = i;
		uint32_t c = ak_utf8_decode(s->data, s->len, &i);
		uint32_t out[3];
		int m;
		if (k == AK_CASE_LOWER && c == 0x3a3 && final_sigma(s->data, s->len, from, i)) out[0] = 0x3c2, m = 1;
		else m = ak_case_map(c, k, out);
		if (m == 1 && out[0] == c) {
			ak_strb_cstr(&b, s->data + from, i - from);  // as it was (what isn't UTF-8 too)
			continue;
		}
		changed = true;
		for (int q = 0; q < m; q++) {
			char u[4];
			ak_strb_cstr(&b, u, ak_utf8_encode(out[q], u));
		}
	}
	if (!changed) {
		ak_buf_free(&b.b);
		return ak_retain(s);
	}
	return ak_strb_finish(&b);
}

ak_str *ak_str_lower(ak_str *s) { return map_case(s, AK_CASE_LOWER); }
ak_str *ak_str_upper(ak_str *s) { return map_case(s, AK_CASE_UPPER); }
ak_str *ak_str_fold(ak_str *s) { return map_case(s, AK_CASE_FOLD); }

// ---- Comparing without case: full case folding, code point by code
// point (Go's strings.EqualFold, but with ß equal to ss) ----

// A fold_it gives a string's folded code points one at a time.
typedef struct {
	const char *s;
	size_t n, i;
	uint32_t buf[3];
	int k, m;  // buf[k:m]: what's left of the last code point's folding
} fold_it;

// fold_next: the next folded code point, or -1 at the end.
static int64_t fold_next(fold_it *it) {
	if (it->k < it->m) return it->buf[it->k++];
	if (it->i >= it->n) return -1;
	uint32_t c = ak_utf8_decode(it->s, it->n, &it->i);
	it->m = ak_case_map(c, AK_CASE_FOLD, it->buf);
	it->k = 1;
	return it->buf[0];
}

static fold_it folding(const ak_str *s) { return (fold_it){.s = s->data, .n = s->len}; }

int ak_str_cmp_fold(const ak_str *a, const ak_str *b) {
	fold_it x = folding(a), y = folding(b);
	for (;;) {
		int64_t p = fold_next(&x), q = fold_next(&y);
		if (p != q) return p < q ? -1 : 1;
		if (p < 0) return 0;
	}
}

bool ak_str_eq_fold(const ak_str *a, const ak_str *b) { return ak_str_eq(a, b) || ak_str_cmp_fold(a, b) == 0; }

bool ak_str_has_prefix_fold(const ak_str *s, const ak_str *p) {
	fold_it x = folding(s), y = folding(p);
	for (int64_t q; (q = fold_next(&y)) >= 0;)
		if (fold_next(&x) != q) return false;
	return x.k == x.m;  // not in the middle of a code point's folding: ß isn't s...
}

// fold_all: s's folded code points, each one that starts a code point's
// folding marked with bit 31 (a match starts and ends at them).
#define FOLD_START 0x80000000u
static uint32_t *fold_all(const ak_str *s, uint32_t *stack, size_t cap, size_t *len) {
	uint32_t *out = stack;
	size_t n = 0;
	for (size_t i = 0; i < s->len;) {
		uint32_t c = ak_utf8_decode(s->data, s->len, &i), m3[3];
		int m = ak_case_map(c, AK_CASE_FOLD, m3);
		if (n + 3 > cap) {
			uint32_t *o = ak_alloc((cap * 2 + 3) * sizeof *o);
			memcpy(o, out, n * sizeof *o);
			if (out != stack) ak_free(out);
			out = o;
			cap = cap * 2 + 3;
		}
		for (int q = 0; q < m; q++) out[n++] = m3[q] | (q == 0 ? FOLD_START : 0);
	}
	*len = n;
	return out;
}

// find_fold: whether sub's folding is in s's at a code point's start
// (where is anywhere, or 1: at the start, 2: at the end), ending at one.
static bool find_fold(const ak_str *s, const ak_str *sub, int where) {
	if (sub->len == 0) return true;
	uint32_t sa[256], sb[64];
	size_t n, m;
	uint32_t *a = fold_all(s, sa, AK_LEN(sa), &n), *b = fold_all(sub, sb, AK_LEN(sb), &m);
	bool found = false;
	for (size_t i = where == 2 ? (n >= m ? n - m : n + 1) : 0; !found && i + m <= n; i++) {
		if (!(a[i] & FOLD_START)) continue;
		size_t k = 0;
		while (k < m && (a[i + k] & ~FOLD_START) == (b[k] & ~FOLD_START)) k++;
		found = k == m && (i + m == n || (a[i + m] & FOLD_START));
		if (where == 1) break;
	}
	if (a != sa) ak_free(a);
	if (b != sb) ak_free(b);
	return found;
}

bool ak_str_has_suffix_fold(const ak_str *s, const ak_str *p) { return find_fold(s, p, 2); }
bool ak_str_contains_fold(const ak_str *s, const ak_str *sub) { return find_fold(s, sub, 0); }

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

ak_str *ak_str_trim(ak_str *s) {
	uint32_t a = 0, b = s->len;
	while (a < b && is_space(s->data[a])) a++;
	while (b > a && is_space(s->data[b - 1])) b--;
	return ak_str_slice(s, a, b);
}

ak_str *ak_str_replace(ak_str *s, const ak_str *old, const ak_str *new_) {
	if (old->len == 0 || ak_str_find(s, old) < 0) return ak_retain(s);
	ak_strb b = {0};
	uint32_t i = 0;
	while (i < s->len) {
		if (i + old->len <= s->len && memcmp(s->data + i, old->data, old->len) == 0) {
			ak_strb_str(&b, new_);
			i += old->len;
		} else {
			ak_strb_cstr(&b, s->data + i, 1);
			i++;
		}
	}
	return ak_strb_finish(&b);
}

ak_arr *ak_str_split(ak_str *s, const ak_str *sep) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 4);
	a->h.flags = 1;
	if (sep->len == 0) {
		// Split into characters.
		uint32_t i = 0;
		while (i < s->len) {
			uint32_t j = i + 1;
			while (j < s->len && ((uint8_t)s->data[j] & 0xc0) == 0x80) j++;
			ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + i, j - i)});
			i = j;
		}
		return a;
	}
	uint32_t start = 0;
	for (uint32_t i = 0; i + sep->len <= s->len;) {
		if (memcmp(s->data + i, sep->data, sep->len) == 0) {
			ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + start, i - start)});
			i += sep->len;
			start = i;
		} else {
			i++;
		}
	}
	ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + start, s->len - start)});
	return a;
}

// The digits from p, past which it returns.
static const char *digits(const char *p, const char *end) {
	while (p < end && *p >= '0' && *p <= '9') p++;
	return p;
}

// Whether p..end, past a sign, is word, whatever its case.
static bool word_is(const char *p, const char *end, const char *word) {
	size_t n = strlen(word);
	if ((size_t)(end - p) != n) return false;
	for (size_t i = 0; i < n; i++)
		if ((p[i] | 0x20) != word[i]) return false;
	return true;
}

// toInt and toFloat read the whole string as Swift, Kotlin and Go do:
// an optional sign, then the number, with nothing around it (no spaces:
// trim() first). strtoll and strtod alone would also take leading
// spaces, and strtod hex floats; what they're given here is only ever
// the decimal form, so they read it the same way everywhere.
bool ak_str_to_int(const ak_str *s, int64_t *out) {
	const char *p = s->data, *end = s->data + s->len;
	if (p < end && (*p == '+' || *p == '-')) p++;
	if (p == end || digits(p, end) != end) return false;
	char *e;
	errno = 0;
	long long v = strtoll(s->data, &e, 10);
	if (errno || e != end) return false;
	*out = v;
	return true;
}

bool ak_str_to_float(const ak_str *s, double *out) {
	const char *p = s->data, *end = s->data + s->len;
	bool neg = p < end && *p == '-';
	if (p < end && (*p == '+' || *p == '-')) p++;
	// What a float prints as (Infinity, NaN), and the short forms.
	if (word_is(p, end, "infinity") || word_is(p, end, "inf")) {
		*out = neg ? -INFINITY : INFINITY;
		return true;
	}
	if (word_is(p, end, "nan")) {
		*out = NAN;
		return true;
	}
	// digits [. digits] [e [sign] digits], with a digit before or after
	// the point: 1, 1.5, .5, 5., 1e3, 2.5E-3.
	const char *q = digits(p, end);
	bool some = q > p;
	if (q < end && *q == '.') {
		const char *f = digits(q + 1, end);
		some = some || f > q + 1;
		q = f;
	}
	if (!some) return false;
	if (q < end && (*q == 'e' || *q == 'E')) {
		q++;
		if (q < end && (*q == '+' || *q == '-')) q++;
		const char *x = digits(q, end);
		if (x == q) return false;
		q = x;
	}
	if (q != end) return false;
	char *e;
	double v = strtod(s->data, &e);
	if (e != end) return false;
	*out = v;
	return true;
}

void ak_strb_str(ak_strb *b, const ak_str *s) { ak_buf_put(&b->b, s->data, s->len); }
void ak_strb_cstr(ak_strb *b, const char *s, size_t n) { ak_buf_put(&b->b, s, n); }

void ak_strb_int(ak_strb *b, int64_t v) {
	char buf[24];
	int n = snprintf(buf, sizeof buf, "%lld", (long long)v);
	ak_buf_put(&b->b, buf, (size_t)n);
}

void ak_strb_float(ak_strb *b, double v) {
	char buf[48];
	int n = fmt_float(buf, sizeof buf, v);
	ak_buf_put(&b->b, buf, (size_t)n);
}

void ak_strb_bool(ak_strb *b, bool v) {
	if (v) ak_buf_put(&b->b, "true", 4);
	else ak_buf_put(&b->b, "false", 5);
}

ak_str *ak_strb_finish(ak_strb *b) {
	ak_str *s = b->b.len ? ak_str_new((const char *)b->b.data, b->b.len) : ak_str_empty();
	ak_buf_free(&b->b);
	return s;
}

ak_bytes *ak_bytes_new(const void *p, size_t n) {
	ak_bytes *o = ak_obj_new(AK_TAG_BYTES, sizeof(ak_bytes) + n);
	o->len = (uint32_t)n;
	if (n && p) memcpy(o->data, p, n);  // p NULL: the caller fills them
	return o;
}
