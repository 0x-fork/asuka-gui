// i18n.c: catalogs in the core: an app's messages, compiled by package
// i18n (Catalog.Compile: its format is documented there), said in the
// language the app shows (locale.c), by CLDR's plural rules
// (i18n_gen.c) and the catalog's languages' number formats. A port of
// i18n's Catalog.Format, format.go and pseudo.go, which a test checks
// against, case by case (core/test/testdata/catalog_cases.txt).
#include "ui/i18n.h"
#include "ui/ui_ext.h"

#include <stdio.h>
#include <stdlib.h>

// ---- Plural operands and rules ----

ak_operands ak_operands_int(int64_t n) {
	uint64_t u = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
	const uint64_t big = UINT64_C(1000000000000000000);
	if (u >= big) u = u % big + big;
	return (ak_operands){.i = u};
}

ak_operands ak_operands_dec(const char *s, size_t n) {
	ak_operands o = {0};
	const uint64_t big = UINT64_C(1000000000000000000);
	size_t i = 0;
	if (i < n && (s[i] == '-' || s[i] == '+')) i++;
	int digits = 0;
	for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
		o.i = o.i * 10 + (uint64_t)(s[i] - '0');
		if (++digits > 18) o.i = o.i % big + big;
	}
	if (i < n && s[i] == '.') {
		i++;
		size_t start = i, last = i;
		for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
			o.f = o.f * 10 + (uint64_t)(s[i] - '0');
			if (s[i] != '0') last = i + 1;
			if (i - start >= 18) o.f %= big;
		}
		o.v = (int)(i - start);
		o.w = (int)(last - start);
		o.t = o.f;
		for (int k = o.w; k < o.v; k++) o.t /= 10;
	}
	return o;
}

static int search(const char *const *list, uint32_t n, ak_span s) {
	uint32_t lo = 0, hi = n;
	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;
		size_t ln = strlen(list[m]);
		int c = memcmp(list[m], s.p, ln < s.len ? ln : s.len);
		if (!c) c = ln < s.len ? -1 : ln > s.len ? 1 : 0;
		if (!c) return (int)m;
		if (c < 0) lo = m + 1;
		else hi = m;
	}
	return -1;
}

static ak_span strip_ext(ak_span s) {
	for (uint32_t i = 1; i < s.len; i++)
		if (s.p[i - 1] == '-' && s.p[i] != '-' && (i + 1 == s.len || s.p[i + 1] == '-')) return (ak_span){s.p, i - 1};
	return s;
}

// rule_of is the rule set of tag: that of the tag, or of the tag without
// its last subtag, down to its language.
static uint8_t rule_of(ak_span tag, const char *const *langs, const uint8_t *rules, uint32_t n) {
	ak_str *t = ak_i18n_normalize(tag);
	ak_span s = strip_ext(ak_span_str(t));
	uint8_t r = 0;
	while (s.len) {
		int i = search(langs, n, s);
		if (i >= 0) {
			r = rules[i];
			break;
		}
		while (s.len && s.p[s.len - 1] != '-') s.len--;
		if (s.len) s.len--;
	}
	ak_release(t);
	return r;
}

int ak_plural(ak_span tag, const ak_operands *o, bool ordinal) {
	if (ordinal) return ak_i18n_ordinal_rule(rule_of(tag, ak_i18n_ordinal_langs, ak_i18n_ordinal_rules, ak_i18n_nordinal), o);
	return ak_i18n_cardinal_rule(rule_of(tag, ak_i18n_cardinal_langs, ak_i18n_cardinal_rules, ak_i18n_ncardinal), o);
}

// ---- Catalogs ----

typedef struct {
	ak_span prefix, suffix, neg_prefix, neg_suffix;
	uint8_t primary, secondary, min_int, max_frac;
} num_pattern;

typedef struct {
	ak_span tag;
	bool rtl;
	ak_span digits[10];
	bool latin;
	ak_span decimal, group;
	uint8_t min_grouping;
	num_pattern dec, pct;
	uint32_t *msgs;  // per key, the offset of its nodes in the blob + 1 (0: none)
} cat_lang;

struct ak_catalog {
	ak_opaque h;
	ak_bytes *blob;
	ak_span source;
	ak_span *keys;
	uint32_t nkeys;
	cat_lang *langs;
	uint32_t nlangs;
};

static void catalog_drop(ak_opaque *o) {
	ak_catalog *c = (ak_catalog *)o;
	for (uint32_t i = 0; i < c->nlangs; i++) ak_free(c->langs[i].msgs);
	ak_free(c->langs);
	ak_free(c->keys);
	ak_release(c->blob);
}

static AK_VEC(ak_catalog *) registry;

void ak_locale_changed(void);

// skip_nodes reads past nodes, checking them.
static bool skip_nodes(ak_reader *r, int depth) {
	if (depth > 32) return false;
	uint32_t n = ak_read_u32(r);
	for (uint32_t i = 0; i < n && !r->err; i++) {
		uint8_t kind = ak_read_u8(r);
		switch (kind) {
		case 0: ak_read_str(r); break;
		case 1:
		case 4: ak_read_u8(r); break;
		case 2: break;
		case 3: ak_read_u8(r), ak_read_u8(r), ak_read_u8(r), ak_read_u8(r); break;
		case 5:
		case 6: {
			ak_read_u8(r);
			ak_read_u64(r);
			uint32_t cases = ak_read_u32(r);
			for (uint32_t k = 0; k < cases && !r->err; k++) {
				if (ak_read_u8(r) == 0) ak_read_str(r);
				else ak_read_u8(r);
				if (!skip_nodes(r, depth + 1)) return false;
			}
			break;
		}
		case 7: {
			ak_read_u8(r);
			uint32_t cases = ak_read_u32(r);
			for (uint32_t k = 0; k < cases && !r->err; k++) {
				ak_read_str(r);
				if (!skip_nodes(r, depth + 1)) return false;
			}
			break;
		}
		default: return false;
		}
	}
	return !r->err;
}

static void read_pattern(ak_reader *r, num_pattern *p) {
	p->prefix = ak_read_str(r), p->suffix = ak_read_str(r);
	p->neg_prefix = ak_read_str(r), p->neg_suffix = ak_read_str(r);
	p->primary = ak_read_u8(r), p->secondary = ak_read_u8(r), p->min_int = ak_read_u8(r), p->max_frac = ak_read_u8(r);
}

ak_catalog *ak_i18n_catalog(ak_bytes *blob) {
	if (!blob || blob->len < 4 || memcmp(blob->data, "AKC1", 4) != 0) {
		ak_throw_msg("i18n.catalog: not a compiled catalog");
		return NULL;
	}
	ak_catalog *c = ak_opaque_new(AK_TAG_OPAQUE, sizeof(ak_catalog), catalog_drop);
	c->blob = ak_retain(blob);
	ak_reader r = {blob->data, blob->len, 4, false};
	c->source = ak_read_str(&r);
	c->nkeys = ak_read_u32(&r);
	if (c->nkeys > blob->len) r.err = true;
	c->keys = ak_alloc((c->nkeys ? c->nkeys : 1) * sizeof(ak_span));
	for (uint32_t i = 0; i < c->nkeys && !r.err; i++) c->keys[i] = ak_read_str(&r);
	c->nlangs = ak_read_u32(&r);
	if (c->nlangs > blob->len) r.err = true;
	c->langs = ak_alloc((c->nlangs && !r.err ? c->nlangs : 1) * sizeof(cat_lang));
	for (uint32_t i = 0; i < c->nlangs && !r.err; i++) {
		cat_lang *l = &c->langs[i];
		l->tag = ak_read_str(&r);
		l->rtl = ak_read_u8(&r) & 1;
		l->latin = true;
		for (int d = 0; d < 10; d++) {
			l->digits[d] = ak_read_str(&r);
			l->latin &= l->digits[d].len == 1 && l->digits[d].p[0] == '0' + d;
		}
		l->decimal = ak_read_str(&r), l->group = ak_read_str(&r);
		l->min_grouping = ak_read_u8(&r);
		read_pattern(&r, &l->dec);
		read_pattern(&r, &l->pct);
		l->msgs = ak_alloc((c->nkeys ? c->nkeys : 1) * sizeof(uint32_t));
		uint32_t n = ak_read_u32(&r);
		for (uint32_t k = 0; k < n && !r.err; k++) {
			uint32_t key = ak_read_u32(&r);
			uint32_t at = (uint32_t)r.off;
			if (key >= c->nkeys || !skip_nodes(&r, 0)) r.err = true;
			else l->msgs[key] = at + 1;
		}
	}
	if (r.err) {
		ak_release(c);
		ak_throw_msg("i18n.catalog: a compiled catalog cut short or malformed");
		return NULL;
	}
	ak_vec_push(&registry, (ak_catalog *)ak_retain(c));
	ak_locale_changed();
	return c;
}

ak_arr *ak_i18n_catalog_languages(void) {
	if (!registry.len) return NULL;
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 4);
	a->h.flags = 1;
	for (uint32_t i = 0; i < registry.len; i++) {
		ak_catalog *c = registry.data[i];
		// The first catalog's source first, then the languages each has.
		for (int pass = 0; pass < 2; pass++) {
			for (uint32_t j = 0; j < c->nlangs; j++) {
				ak_span t = c->langs[j].tag;
				if ((pass == 0) != ak_span_eq(t, c->source)) continue;
				bool seen = false;
				for (uint32_t k = 0; k < a->len && !seen; k++) seen = ak_span_eq(ak_span_str(a->data[k].p), t);
				if (!seen) ak_arr_push(&a, (ak_val){.p = ak_str_span(t)});
			}
		}
	}
	return a;
}

void ak_i18n_reset(void) {
	for (uint32_t i = 0; i < registry.len; i++) ak_release(registry.data[i]);
	ak_vec_free(&registry);
}

static cat_lang *lang_of(ak_catalog *c, ak_span tag) {
	for (uint32_t i = 0; i < c->nlangs; i++)
		if (ak_span_eq(c->langs[i].tag, tag)) return &c->langs[i];
	return NULL;
}

// ---- Numbers (format.go's formatNumber, number.go's formatDecimal) ----

typedef struct {
	bool percent, integer, no_grouping, half_even;
	int min_frac, max_frac;  // max -1: the pattern's
} num_opts;

typedef struct {
	char *p;
	size_t len, cap;
} sbuf;

static void sb_put(sbuf *b, const char *s, size_t n) {
	if (b->len + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap * 2 : 64;
		while (c < b->len + n + 1) c *= 2;
		char *q = ak_alloc(c);
		if (b->len) memcpy(q, b->p, b->len);
		ak_free(b->p);
		b->p = q, b->cap = c;
	}
	memcpy(b->p + b->len, s, n);
	b->len += n;
	b->p[b->len] = 0;
}

static void sb_span(sbuf *b, ak_span s) { sb_put(b, s.p, s.len); }

// A decimal number as it's written ("-1234.5"), in a buffer of its own.
typedef struct {
	char s[64];
} decimal;

static void dec_int(decimal *d, int64_t v) { snprintf(d->s, sizeof d->s, "%lld", (long long)v); }

// dec_float writes v in its fewest digits that read back as it, without
// an exponent (Go's strconv.FormatFloat(v, 'f', -1, 64)).
static void dec_float(decimal *d, double v) {
	if (v != v || v == (double)INFINITY || v == -(double)INFINITY) {
		snprintf(d->s, sizeof d->s, "0");
		return;
	}
	for (int prec = 0; prec <= 20; prec++) {
		snprintf(d->s, sizeof d->s, "%.*f", prec, v);
		if (strtod(d->s, NULL) == v) return;
	}
}

static void write_digits(sbuf *b, const cat_lang *l, const char *digits, size_t n) {
	if (!l || l->latin) {
		sb_put(b, digits, n);
		return;
	}
	for (size_t i = 0; i < n; i++) sb_span(b, l->digits[digits[i] - '0']);
}

static const num_pattern latin_dec = {{"", 0}, {"", 0}, {"-", 1}, {"", 0}, 3, 3, 1, 3};
static const num_pattern latin_pct = {{"", 0}, {"%", 1}, {"-", 1}, {"%", 1}, 3, 3, 1, 0};

// format_number writes the decimal number in for language l's patterns
// and symbols (latin ones, English's, for none).
static void format_number(sbuf *out, const cat_lang *l, const char *in, num_opts o) {
	const num_pattern *p = o.percent ? (l ? &l->pct : &latin_pct) : (l ? &l->dec : &latin_dec);
	int min = o.min_frac < 0 ? 0 : o.min_frac, max = o.max_frac;
	if (o.integer) min = max = 0;
	else if (max <= 0 && min == 0) max = p->max_frac;
	if (max < min) max = min;
	// split: sign, integer digits (no leading zeros, "0" for none), fraction.
	bool neg = in[0] == '-';
	const char *s = in + (neg || in[0] == '+');
	const char *dot = strchr(s, '.');
	char ip[96], fp[96];
	size_t il = dot ? (size_t)(dot - s) : strlen(s), fl = dot ? strlen(dot + 1) : 0;
	if (il > 80) il = 80;
	if (fl > 80) fl = 80;
	memcpy(ip, s, il), ip[il] = 0;
	memcpy(fp, dot ? dot + 1 : "", fl), fp[fl] = 0;
	size_t z = 0;
	while (z + 1 < il && ip[z] == '0') z++;
	memmove(ip, ip + z, il - z + 1), il -= z;
	if (!il) strcpy(ip, "0"), il = 1;
	if (o.percent) {
		// times 100: two digits from the fraction, or zeros.
		for (int k = 0; k < 2; k++) {
			ip[il++] = fl ? fp[0] : '0';
			ip[il] = 0;
			if (fl) memmove(fp, fp + 1, fl--);
		}
		z = 0;
		while (z + 1 < il && ip[z] == '0') z++;
		memmove(ip, ip + z, il - z + 1), il -= z;
	}
	// Round to max fraction digits, half away from zero or half to even.
	if ((int)fl > max) {
		const char *drop = fp + max;
		bool up = false;
		if (drop[0] > '5') up = true;
		else if (drop[0] == '5') {
			bool rest = false;
			for (const char *q = drop + 1; *q; q++) rest |= *q != '0';
			if (rest || !o.half_even) up = true;
			else {
				char last = max > 0 ? fp[max - 1] : ip[il - 1];
				up = (last - '0') % 2 == 1;
			}
		}
		fl = (size_t)max;
		fp[fl] = 0;
		if (up) {
			char all[192];
			size_t n = il + fl;
			memcpy(all, ip, il), memcpy(all + il, fp, fl), all[n] = 0;
			long i = (long)n - 1;
			for (; i >= 0; i--) {
				if (all[i] == '9') {
					all[i] = '0';
					continue;
				}
				all[i]++;
				break;
			}
			if (i < 0) {
				memmove(all + 1, all, n + 1);
				all[0] = '1';
				n++;
			}
			il = n - fl;
			memcpy(ip, all, il), ip[il] = 0;
			memcpy(fp, all + il, fl), fp[fl] = 0;
		}
	}
	while ((int)fl > min && fp[fl - 1] == '0') fp[--fl] = 0;
	while ((int)fl < min) fp[fl++] = '0', fp[fl] = 0;
	sb_span(out, neg ? p->neg_prefix : p->prefix);
	// The integer, grouped as the pattern groups it.
	while ((int)il < p->min_int) memmove(ip + 1, ip, il + 1), ip[0] = '0', il++;
	uint8_t mg = l ? l->min_grouping : 1;
	if (o.no_grouping || !p->primary || (int)il < p->primary + (mg ? mg : 1)) {
		write_digits(out, l, ip, il);
	} else {
		int sec = p->secondary ? p->secondary : p->primary;
		int first = ((int)il - p->primary) % sec;
		if (!first) first = sec;
		size_t pos = 0;
		for (int size = first; pos < il;) {
			write_digits(out, l, ip + pos, (size_t)size);
			pos += (size_t)size;
			if (pos < il) sb_span(out, l ? l->group : AK_STR(","));
			size = il - pos == (size_t)p->primary ? p->primary : sec;
		}
	}
	if (fl) {
		sb_span(out, l ? l->decimal : AK_STR("."));
		write_digits(out, l, fp, fl);
	}
	sb_span(out, neg ? p->neg_suffix : p->suffix);
}

// ---- Pseudo locales (pseudo.go) ----

static const char *const accented[52] = {
	"å", "ƀ", "ç", "ð", "é", "ƒ", "ĝ", "ĥ", "î", "ĵ", "ķ", "ļ", "ɱ", "ñ", "ö", "þ", "ǫ", "ŕ", "š", "ţ", "û", "ṽ", "ŵ", "ẋ", "ý", "ž",
	"Å", "Ɓ", "Ç", "Ð", "É", "Ƒ", "Ĝ", "Ĥ", "Î", "Ĵ", "Ķ", "Ļ", "Ṁ", "Ñ", "Ö", "Þ", "Ǫ", "Ŕ", "Š", "Ţ", "Û", "Ṽ", "Ŵ", "Ẋ", "Ý", "Ž",
};

enum { PSEUDO_NONE, PSEUDO_ACCENTS, PSEUDO_BIDI };

typedef struct {
	int pseudo;
	size_t length;  // en-XA: the text's characters so far
	const cat_lang *lang;     // the message's: its plural rules
	const cat_lang *formats;  // whose numbers
	const ak_struct *args;
	sbuf out;
} fmt_ctx;

static int rune_len(uint8_t c) { return c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4; }

static bool is_space(const char *p, int n) {
	if (n == 1) return *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f';
	// U+00A0 and U+2000..U+200A, U+3000 count as spaces too.
	const uint8_t *u = (const uint8_t *)p;
	if (n == 2) return u[0] == 0xC2 && (u[1] == 0xA0 || u[1] == 0x85);
	if (n == 3) return (u[0] == 0xE2 && u[1] == 0x80 && (u[2] <= 0x8A || u[2] == 0xA8 || u[2] == 0xA9 || u[2] == 0xAF)) ||
	                   (u[0] == 0xE3 && u[1] == 0x80 && u[2] == 0x80) || (u[0] == 0xE2 && u[1] == 0x81 && u[2] == 0x9F);
	return false;
}

#define RLM "\xE2\x80\x8F"
#define RLO "\xE2\x80\xAE"
#define PDF "\xE2\x80\xAC"

static void put_text(fmt_ctx *f, ak_span s) {
	if (f->pseudo == PSEUDO_ACCENTS) {
		for (uint32_t i = 0; i < s.len;) {
			int n = rune_len((uint8_t)s.p[i]);
			char c = s.p[i];
			if (n == 1 && c >= 'a' && c <= 'z') sb_put(&f->out, accented[c - 'a'], strlen(accented[c - 'a']));
			else if (n == 1 && c >= 'A' && c <= 'Z') sb_put(&f->out, accented[26 + c - 'A'], strlen(accented[26 + c - 'A']));
			else sb_put(&f->out, s.p + i, (size_t)n);
			f->length++;
			i += (uint32_t)n;
		}
		return;
	}
	if (f->pseudo == PSEUDO_BIDI) {
		// Each word right to left: an override reverses its letters, marks
		// set it in a right-to-left run.
		uint32_t i = 0;
		while (i < s.len) {
			uint32_t start = i;
			while (i < s.len && is_space(s.p + i, rune_len((uint8_t)s.p[i]))) i += (uint32_t)rune_len((uint8_t)s.p[i]);
			sb_put(&f->out, s.p + start, i - start);
			if (i >= s.len) break;
			start = i;
			while (i < s.len && !is_space(s.p + i, rune_len((uint8_t)s.p[i]))) i += (uint32_t)rune_len((uint8_t)s.p[i]);
			sb_put(&f->out, RLM RLO, 6);
			sb_put(&f->out, s.p + start, i - start);
			sb_put(&f->out, PDF RLM, 6);
		}
		return;
	}
	sb_span(&f->out, s);
}

static void put_placeholder(fmt_ctx *f, const char *s, size_t n) {
	if (f->pseudo == PSEUDO_BIDI) sb_put(&f->out, RLM, 3);
	sb_put(&f->out, s, n);
	if (f->pseudo == PSEUDO_BIDI) sb_put(&f->out, RLM, 3);
}

// ---- Messages ----

// An argument's value: its kind and, for a number, its decimal.
typedef struct {
	ak_kind kind;
	ak_val v;
	bool present;
} arg_val;

static arg_val arg_of(fmt_ctx *f, uint8_t i) {
	const ak_struct *a = f->args;
	if (!a || i >= a->shape->nfields) return (arg_val){0};
	return (arg_val){(ak_kind)a->shape->kinds[i], a->f[i], true};
}

static bool arg_number(arg_val a, decimal *d) {
	if (a.kind == AK_K_INT) dec_int(d, a.v.i);
	else if (a.kind == AK_K_FLOAT) {
		if (a.v.f != a.v.f || a.v.f == (double)INFINITY || a.v.f == -(double)INFINITY) return false;
		dec_float(d, a.v.f);
	} else return false;
	return true;
}

// value_string writes a simple argument's value: a string as it is, a
// number as a number is by default, a bool as true or false.
static void put_value(fmt_ctx *f, arg_val a) {
	decimal d;
	if (arg_number(a, &d)) {
		sbuf s = {0};
		format_number(&s, f->formats, d.s, (num_opts){.max_frac = -1});
		put_placeholder(f, s.p, s.len);
		ak_free(s.p);
	} else if (a.kind == AK_K_BOOL) {
		put_placeholder(f, a.v.i ? "true" : "false", a.v.i ? 4 : 5);
	} else {
		ak_span s = ak_span_str(a.v.p);
		put_placeholder(f, s.p, s.len);
	}
}

// minus is n less offset, with as many fraction digits as it has.
static void minus(decimal *d, int64_t offset) {
	if (!offset) return;
	if (!strchr(d->s, '.')) {
		long long v = strtoll(d->s, NULL, 10);
		snprintf(d->s, sizeof d->s, "%lld", v - (long long)offset);
		return;
	}
	const char *dot = strchr(d->s, '.');
	int frac = (int)strlen(dot + 1);
	double v = strtod(d->s, NULL) - (double)offset;
	snprintf(d->s, sizeof d->s, "%.*f", frac, v);
}

// shown_operands are the operands of a number as a message writes it by
// default: rounded to three fraction digits, without trailing zeros.
static ak_operands shown_operands(const decimal *d) {
	sbuf s = {0};
	format_number(&s, NULL, d->s, (num_opts){.max_frac = 3});
	// The latin form: digits, no grouping (3 digits ahead of a group are
	// fine: the operands skip separators).
	char plain[96];
	size_t n = 0;
	for (size_t i = 0; i < s.len && n + 1 < sizeof plain; i++)
		if ((s.p[i] >= '0' && s.p[i] <= '9') || s.p[i] == '.') plain[n++] = s.p[i];
	plain[n] = 0;
	ak_free(s.p);
	return ak_operands_dec(plain, n);
}

static void run_nodes(fmt_ctx *f, ak_reader *r, const decimal *pound, int depth);

// skip one case's nodes.
static void skip_case(ak_reader *r) { skip_nodes(r, 0); }

static void run_plural(fmt_ctx *f, ak_reader *r, bool ordinal, int depth) {
	arg_val a = arg_of(f, ak_read_u8(r));
	int64_t offset = (int64_t)ak_read_u64(r);
	uint32_t n = ak_read_u32(r);
	decimal d;
	bool num = arg_number(a, &d);
	ak_operands exact = {0};
	if (num && d.s[0] != '-') exact = ak_operands_dec(d.s, strlen(d.s));
	decimal shown = d;
	if (num) minus(&shown, offset);
	int cat = AK_PLURAL_OTHER;
	if (num) {
		ak_operands o = shown_operands(&shown);
		cat = ak_plural(f->lang ? f->lang->tag : AK_STR("en"), &o, ordinal);
	}
	// Find the case: an exact value the number itself, else its category,
	// else other. The cases are read in order; the chosen one is run.
	size_t start = r->off;
	int pick = -1, other = -1, by_cat = -1;
	for (uint32_t i = 0; i < n && !r->err; i++) {
		uint8_t type = ak_read_u8(r);
		if (type == 0) {
			ak_span v = ak_read_str(r);
			if (num && d.s[0] != '-' && pick < 0) {
				ak_operands e = ak_operands_dec(v.p, v.len);
				if (e.i == exact.i && e.t == exact.t && e.w == exact.w) pick = (int)i;
			}
		} else {
			uint8_t c = ak_read_u8(r);
			if (c == AK_PLURAL_OTHER) other = (int)i;
			if (num && c == cat && by_cat < 0) by_cat = (int)i;
		}
		skip_case(r);
	}
	if (pick < 0) pick = num && by_cat >= 0 ? by_cat : other;
	size_t end = r->off;
	r->off = start;
	for (uint32_t i = 0; i < n && !r->err; i++) {
		if (ak_read_u8(r) == 0) ak_read_str(r);
		else ak_read_u8(r);
		if ((int)i == pick) {
			if (num) run_nodes(f, r, &shown, depth + 1);
			else {
				// Not a number: the other case, whose # writes the value.
				decimal raw;
				ak_span s = a.present && a.kind == AK_K_REF ? ak_span_str(a.v.p) : AK_STR("");
				snprintf(raw.s, sizeof raw.s, "\x01%.*s", (int)(s.len < 60 ? s.len : 60), s.p);
				run_nodes(f, r, &raw, depth + 1);
			}
		} else {
			skip_case(r);
		}
	}
	r->off = end;
}

static void run_select(fmt_ctx *f, ak_reader *r, const decimal *pound, int depth) {
	arg_val a = arg_of(f, ak_read_u8(r));
	char buf[64];
	ak_span key = AK_STR("");
	decimal d;
	if (a.kind == AK_K_REF) key = ak_span_str(a.v.p);
	else if (a.kind == AK_K_BOOL) key = a.v.i ? AK_STR("true") : AK_STR("false");
	else if (arg_number(a, &d)) {
		snprintf(buf, sizeof buf, "%s", d.s);
		key = ak_cstr(buf);
	}
	uint32_t n = ak_read_u32(r);
	size_t start = r->off;
	int pick = -1, other = -1;
	for (uint32_t i = 0; i < n && !r->err; i++) {
		ak_span k = ak_read_str(r);
		if (pick < 0 && a.present && ak_span_eq(k, key)) pick = (int)i;
		if (ak_span_eqc(k, "other")) other = (int)i;
		skip_case(r);
	}
	if (pick < 0) pick = other;
	size_t end = r->off;
	r->off = start;
	for (uint32_t i = 0; i < n && !r->err; i++) {
		ak_read_str(r);
		if ((int)i == pick) run_nodes(f, r, pound, depth + 1);
		else skip_case(r);
	}
	r->off = end;
}

static void run_nodes(fmt_ctx *f, ak_reader *r, const decimal *pound, int depth) {
	if (depth > 32) return;
	uint32_t n = ak_read_u32(r);
	for (uint32_t i = 0; i < n && !r->err; i++) {
		switch (ak_read_u8(r)) {
		case 0: put_text(f, ak_read_str(r)); break;
		case 1: {
			arg_val a = arg_of(f, ak_read_u8(r));
			if (a.present) put_value(f, a);
			break;
		}
		case 2:
			if (!pound) {
				sb_put(&f->out, "#", 1);
			} else if (pound->s[0] == 1) {
				put_placeholder(f, pound->s + 1, strlen(pound->s + 1));
			} else {
				sbuf s = {0};
				format_number(&s, f->formats, pound->s, (num_opts){.max_frac = -1});
				put_placeholder(f, s.p, s.len);
				ak_free(s.p);
			}
			break;
		case 3: {
			arg_val a = arg_of(f, ak_read_u8(r));
			uint8_t flags = ak_read_u8(r), mn = ak_read_u8(r), mx = ak_read_u8(r);
			decimal d;
			if (!arg_number(a, &d)) {
				if (a.present) put_value(f, a);
				break;
			}
			num_opts o = {.percent = flags & 1, .integer = flags & 2, .no_grouping = flags & 4, .half_even = flags & 8, .max_frac = -1};
			if (flags & 16) o.min_frac = mn, o.max_frac = mx;
			sbuf s = {0};
			format_number(&s, f->formats, d.s, o);
			put_placeholder(f, s.p, s.len);
			ak_free(s.p);
			break;
		}
		case 4: {
			arg_val a = arg_of(f, ak_read_u8(r));
			if (a.present) put_value(f, a);  // a date the caller wrote
			break;
		}
		case 5: run_plural(f, r, false, depth); break;
		case 6: run_plural(f, r, true, depth); break;
		case 7: run_select(f, r, pound, depth); break;
		default: r->err = true;
		}
	}
}

// ---- Saying a message ----

static void add_cand(ak_span *cands, uint32_t *n, char (*bufs)[64], ak_span t) {
	if (*n >= 24 || !t.len) return;
	for (uint32_t i = 0; i < *n; i++)
		if (ak_span_eq(cands[i], t)) return;
	size_t m = t.len < 63 ? t.len : 63;
	memcpy(bufs[*n], t.p, m);
	cands[*n] = (ak_span){bufs[*n], (uint32_t)m};
	(*n)++;
}

// add_chain adds tag and its parents, nearest first.
static void add_chain(ak_span *cands, uint32_t *n, char (*bufs)[64], ak_span tag) {
	char cur[64];
	size_t m = tag.len < 63 ? tag.len : 63;
	memcpy(cur, tag.p, m);
	ak_span t = {cur, (uint32_t)m};
	for (int i = 0; i < 8 && t.len; i++) {
		add_cand(cands, n, bufs, t);
		char next[64];
		ak_span p = ak_i18n_parent(t, next, sizeof next);
		if (!p.len) break;
		m = p.len < 63 ? p.len : 63;
		memmove(cur, p.p, m);
		t = (ak_span){cur, (uint32_t)m};
	}
}

// starts_ltr reports whether s's first strong character reads left to
// right (or it starts with a left-to-right mark).
static bool starts_ltr(const char *s, size_t n) {
	for (size_t i = 0; i < n;) {
		const uint8_t *u = (const uint8_t *)s + i;
		uint32_t c;
		int l = rune_len(u[0]);
		if (l == 1) c = u[0];
		else if (l == 2) c = (uint32_t)(u[0] & 0x1f) << 6 | (u[1] & 0x3f);
		else if (l == 3) c = (uint32_t)(u[0] & 0x0f) << 12 | (uint32_t)(u[1] & 0x3f) << 6 | (u[2] & 0x3f);
		else c = (uint32_t)(u[0] & 0x07) << 18 | (uint32_t)(u[1] & 0x3f) << 12 | (uint32_t)(u[2] & 0x3f) << 6 | (u[3] & 0x3f);
		i += (size_t)l;
		if (c == 0x200E) return true;
		if (c == 0x200F) return false;
		if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) || (c >= 0x10800 && c <= 0x10FFF) ||
		    (c >= 0x1E800 && c <= 0x1EFFF)) {
			// Arabic and Hebrew letters read right to left; their digits
			// and marks are weak.
			bool letter = !(c >= 0x0660 && c <= 0x0669) && !(c >= 0x06F0 && c <= 0x06F9) && !(c >= 0x064B && c <= 0x065F) && c != 0x060C &&
			              c != 0x061B && c != 0x061F && c != 0x066A && c != 0x066B && c != 0x066C;
			if (letter) return false;
			continue;
		}
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c < 0x2000) || (c >= 0x3040 && c < 0xD800) ||
		    (c >= 0x10000 && c < 0x10800))
			return true;
	}
	return false;
}

ak_str *ak_i18n_say(ak_catalog *c, int64_t key, ak_val args, const ak_str *lang) {
	if (!c || key < 0 || (uint64_t)key >= c->nkeys) return ak_str_c("");
	ak_span locale;
	if (lang && lang->len) locale = ak_span_str(lang);
	else locale = ak_span_str(ak_locale_now());  // tracked: what follows the language
	ak_str *norm = ak_i18n_normalize(locale);
	ak_span tag = strip_ext(ak_span_str(norm));
	int pseudo = PSEUDO_NONE;
	if (ak_span_eqc(tag, "en-XA")) pseudo = PSEUDO_ACCENTS;
	else if (ak_span_eqc(tag, "ar-XB")) pseudo = PSEUDO_BIDI;
	// The tags to look the key up in, in order.
	ak_span cands[24];
	char bufs[24][64];
	uint32_t n = 0;
	if (tag.len && !pseudo) {
		add_chain(cands, &n, bufs, tag);
		// The language the catalog has that suits the locale best, as
		// zh-Hant for zh-TW, whose chain (zh-TW, zh) has none of it.
		if (!lang_of(c, tag)) {
			ak_arr *pref = ak_arr_new(AK_K_REF, NULL, 1), *sup = ak_arr_new(AK_K_REF, NULL, c->nlangs);
			ak_arr_push(&pref, (ak_val){.p = ak_str_span(tag)});
			for (uint32_t i = 0; i < c->nlangs; i++) ak_arr_push(&sup, (ak_val){.p = ak_str_span(c->langs[i].tag)});
			int m = ak_i18n_match(pref, sup);
			if (m >= 0) add_chain(cands, &n, bufs, c->langs[m].tag);
			ak_release(pref), ak_release(sup);
		}
	}
	add_chain(cands, &n, bufs, c->source);
	fmt_ctx f = {.pseudo = pseudo, .args = args.p};
	const uint8_t *prog = NULL;
	for (uint32_t i = 0; i < n && !prog; i++) {
		cat_lang *l = lang_of(c, cands[i]);
		if (l && l->msgs[key]) {
			prog = c->blob->data + l->msgs[key] - 1;
			f.lang = l;
		}
	}
	if (!prog) {
		ak_release(norm);
		return ak_str_span(c->keys[key]);
	}
	// Numbers in the conventions of the language shown, when the catalog
	// has it, else of the message's.
	f.formats = pseudo ? f.lang : lang_of(c, tag);
	if (!f.formats) f.formats = f.lang;
	ak_release(norm);
	ak_reader r = {prog, (size_t)(c->blob->data + c->blob->len - prog), 0, false};
	run_nodes(&f, &r, NULL, 0);
	if (pseudo == PSEUDO_ACCENTS) {
		// Bracketed, and padded by 40% of its text with words, as
		// translations run longer than English.
		static const char *const pad[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
		                                  "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty"};
		sbuf w = {0};
		sb_put(&w, "[", 1);
		if (f.out.len) sb_put(&w, f.out.p, f.out.len);
		size_t want = (f.length * 2 + 4) / 5;
		for (size_t i = 0, got = 0; got < want; i++) {
			const char *word = pad[i % AK_LEN(pad)];
			sb_put(&w, " ", 1);
			sb_put(&w, word, strlen(word));
			got += 1 + strlen(word);
		}
		sb_put(&w, "]", 1);
		ak_free(f.out.p);
		f.out = w;
	}
	ak_str *s;
	// A right-to-left language's message that starts reading the other way
	// starts with a right-to-left mark (a view takes a paragraph's
	// direction from its first strong character).
	if (f.lang->rtl && !pseudo && starts_ltr(f.out.p ? f.out.p : "", f.out.len)) {
		ak_strb b = {0};
		ak_strb_cstr(&b, RLM, 3);
		if (f.out.len) ak_strb_cstr(&b, f.out.p, f.out.len);
		s = ak_strb_finish(&b);
	} else {
		s = ak_str_new(f.out.p ? f.out.p : "", f.out.len);
	}
	ak_free(f.out.p);
	return s;
}
