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

// copy_small copies n bytes (n <= 16) without a call for the few a word
// or two holds: two overlapping loads and stores, as memcpy's own small
// cases do, reading and writing nothing outside.
static inline void copy_small(char *d, const char *s, size_t n) {
	if (n >= 8) {
		uint64_t a, b;
		memcpy(&a, s, 8);
		memcpy(&b, s + n - 8, 8);
		memcpy(d, &a, 8);
		memcpy(d + n - 8, &b, 8);
	} else if (n >= 4) {
		uint32_t a, b;
		memcpy(&a, s, 4);
		memcpy(&b, s + n - 4, 4);
		memcpy(d, &a, 4);
		memcpy(d + n - 4, &b, 4);
	} else {
		for (size_t i = 0; i < n; i++) d[i] = s[i];
	}
}

// ---- Pieces (rt.h) ----
//
// A slab: its count of live pieces, then pieces, 8-byte aligned, each
// one's h.aux its offset from the slab's start in 8 bytes (a slab is
// 16 KB: 11 bits), then 16 bytes of room past its end for ak_piece's
// copies. Under AddressSanitizer each piece is a string of its own, so a
// use after its free is still caught.

// A slab and its 16 bytes of room past its end are 16 KB, one of the
// allocator's size classes: 16 KB and 16 bytes more would take a 20 KB
// block, a fifth of it unused.
#define SLAB ((16 << 10) - 16)

const bool ak_space_table[256] = {[' '] = true, ['\t'] = true, ['\n'] = true, ['\r'] = true, ['\v'] = true, ['\f'] = true};

void ak_pieces_start(ak_pieces *p, const ak_str *from, size_t left) {
	bool ascii = ak_str_ascii(from);
	*p = (ak_pieces){
		.from_end = from->data + from->len + 1,
		.flags = (uint8_t)(AK_STR_SLAB | (ascii ? AK_STR_KNOWN | AK_STR_ASCII : 0)),
		.left = left,
	};
}

void ak_pieces_end(ak_pieces *p) {
	if (p->slab) *(uint32_t *)p->slab = p->count;
}

// new_slab gives p a slab with room for need bytes: no bigger than what
// the pieces to come need at most.
static void new_slab(ak_pieces *p, size_t need) {
	if (p->slab) {
		*(uint32_t *)p->slab = p->count;
		size_t used = (size_t)(p->at - p->slab) - 8;
		p->left = p->left > used ? p->left - used : 0;
	}
	size_t size = p->left && p->left + 8 < SLAB ? p->left + 8 : SLAB;
	if (size < need + 8) size = need + 8;
	p->slab = ak_realloc(NULL, size + 16);  // not zeroed: each piece is written whole
	p->count = 0;
	p->at = p->slab + 8;
	p->end = p->slab + size;
}

ak_str *ak_piece_slow(ak_pieces *p, const char *s, size_t n) {
	if (n == 0) return ak_str_empty();
	uint8_t known = p->flags & (AK_STR_KNOWN | AK_STR_ASCII);
#if defined(AK_NO_SLABS)
	bool small = false;
#else
	bool small = n <= AK_PIECE_MAX;
#endif
	if (!small) {
		ak_str *o = ak_str_new(s, n);
		o->h.flags |= known;
		return o;
	}
	size_t need = (sizeof(ak_str) + n + 1 + 7) & ~(size_t)7;
	if (!p->slab || need > (size_t)(p->end - p->at)) new_slab(p, need);
	ak_str *o = (ak_str *)p->at;
	o->h = (ak_obj){.rc = 1, .tag = AK_TAG_STR, .flags = p->flags, .aux = (uint16_t)((size_t)(p->at - p->slab) >> 3)};
	o->len = (uint32_t)n;
	o->hash = 0;
	memcpy(o->data, s, n);
	o->data[n] = 0;
	p->at += need;
	p->count++;
	ak_obj_count(1);
	return o;
}

ak_str *ak_str_empty(void) {
	static void *e;
	ak_str *s = ak_constant_get(&e);
	return s ? s : ak_constant_put(&e, ak_str_new("", 0));
}

// grown makes a string with room for n bytes (a power of two, less its
// NUL: ak_str_room) holding s's len.
static ak_str *grown(const ak_str *s, size_t n) {
	if (n >= UINT32_MAX) ak_panic("out of memory (a string of %zu bytes)", n);
	unsigned k = 4;
	while (((size_t)1 << k) < n + 1) k++;
	ak_str *o = ak_obj_new(AK_TAG_STR, sizeof(ak_str) + ((size_t)1 << k));
	o->h.aux = (uint16_t)k;
	o->h.flags = s->h.flags & (AK_STR_KNOWN | AK_STR_ASCII);
	o->len = s->len;
	memcpy(o->data, s->data, s->len);
	return o;
}

void ak_str_append(ak_str **sp, const ak_str *t) {
	ak_str *s = *sp;
	uint32_t tn = t->len;
	if (tn == 0) return;
	size_t n = (size_t)s->len + tn;
	if (!ak_unique(s) || (s->h.flags & AK_STR_SLAB)) {
		// Shared (or a slab's piece): a copy, with room to grow.
		ak_str *o = grown(s, n);
		if (t == s) t = o;  // s + s: s's bytes, which o holds
		ak_release(s);
		*sp = s = o;
	} else if (n > ak_str_room(s)) {
		// Full: its buffer twice as big, past what's needed.
		if (n >= UINT32_MAX) ak_panic("out of memory (a string of %zu bytes)", n);
		unsigned k = s->h.aux > 4 ? s->h.aux : 4;
		while (((size_t)1 << k) < n + 1) k++;
		bool self = t == s;
		s = ak_realloc(s, sizeof(ak_str) + ((size_t)1 << k));
		s->h.aux = (uint16_t)k;
		if (self) t = s;
		*sp = s;
	}
	memcpy(s->data + s->len, t->data, tn);
	s->len = (uint32_t)n;
	s->data[n] = 0;
	s->hash = 0;
	if ((s->h.flags & AK_STR_KNOWN) && (t->h.flags & AK_STR_KNOWN) != 0) s->h.flags = (uint8_t)(AK_STR_KNOWN | (s->h.flags & t->h.flags & AK_STR_ASCII));
	else s->h.flags = 0;
}

// strs_join is ak_strs_join, in buf when it's given and the result fits.
static AK_ALWAYS_INLINE ak_str *strs_join(ak_strbuf *buf, const ak_val *v, uint32_t n) {
	uint64_t total = 0;
	uint8_t known = AK_STR_KNOWN | AK_STR_ASCII;
	for (uint32_t i = 0; i < n; i++) {
		const ak_str *s = v[i].p;
		if (!s) continue;
		total += s->len;
		known &= s->h.flags;
	}
	if (total == 0) return ak_str_empty();
	if (total >= UINT32_MAX) ak_panic("out of memory (a string of %llu bytes)", (unsigned long long)total);
	ak_str *o = buf && total <= AK_STRBUF_ROOM ? ak_strbuf_str(buf, (size_t)total) : ak_str_new(NULL, (size_t)total);
	char *w = o->data;
	for (uint32_t i = 0; i < n; i++) {
		const ak_str *s = v[i].p;
		if (!s || !s->len) continue;
		if (s->len <= 16) copy_small(w, s->data, s->len);
		else memcpy(w, s->data, s->len);
		w += s->len;
	}
	if (known & AK_STR_KNOWN) o->h.flags |= known & (AK_STR_KNOWN | AK_STR_ASCII);
	return o;
}

ak_str *ak_strs_join(const ak_val *v, uint32_t n) { return strs_join(NULL, v, n); }
ak_str *ak_strs_join_in(ak_strbuf *buf, const ak_val *v, uint32_t n) { return strs_join(buf, v, n); }

ak_str *ak_str_concat_in(ak_strbuf *buf, ak_str *a, ak_str *b) {
	size_t n = (size_t)a->len + b->len;
	if (a->len == 0 || b->len == 0 || n > AK_STRBUF_ROOM) return ak_str_concat(a, b);
	ak_str *o = ak_strbuf_str(buf, n);
	memcpy(o->data, a->data, a->len);
	memcpy(o->data + a->len, b->data, b->len);
	return o;
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

// int_digits writes v in decimal, ending at end (24 bytes room before
// it), two digits a division; where it starts.
static char *int_digits(int64_t v, char *end) {
	static const char pairs[] =
		"00010203040506070809101112131415161718192021222324252627282930313233343536373839"
		"40414243444546474849505152535455565758596061626364656667686970717273747576777879"
		"8081828384858687888990919293949596979899";
	uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
	char *p = end;
	while (u >= 100) {
		uint64_t q = u / 100;
		uint32_t r = (uint32_t)(u - q * 100);
		p -= 2;
		memcpy(p, pairs + 2 * r, 2);
		u = q;
	}
	if (u >= 10) {
		p -= 2;
		memcpy(p, pairs + 2 * u, 2);
	} else {
		*--p = (char)('0' + u);
	}
	if (v < 0) *--p = '-';
	return p;
}

ak_str *ak_str_from_int(int64_t v) {
	char buf[24];
	char *p = int_digits(v, buf + sizeof buf);
	return ak_str_new(p, (size_t)(buf + sizeof buf - p));
}

// ---- The shortest digits of a float: Ryu ----
//
// Ulf Adams's Ryū ("Ryū: fast float-to-string conversion", PLDI 2018,
// its d2s.c, Apache 2.0 or Boost): the fewest decimal digits that read
// back as the float, the closest of them to it, in a few 128-bit
// multiplies by a power of 5 (ryu_gen.c's small tables). As the digits
// printf's %.*e rounds to with as few as read back, which is how they
// were found before (and still are for powers of 2, whose interval of
// what reads back as them isn't symmetric: printf's closest digits
// needn't be in it while others as few are, so the two may differ).

extern const uint64_t ak_ryu_pow5_table[26];
extern const uint64_t ak_ryu_pow5_split[13][2];
extern const uint8_t ak_ryu_pow5_offsets[326];
extern const uint64_t ak_ryu_inv_pow5_split[13][2];
extern const uint8_t ak_ryu_inv_pow5_offsets[292];

static inline uint64_t umul128(uint64_t a, uint64_t b, uint64_t *hi) {
#ifdef __SIZEOF_INT128__
	__uint128_t r = (__uint128_t)a * b;
	*hi = (uint64_t)(r >> 64);
	return (uint64_t)r;
#else
	uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
	uint64_t b00 = al * bl, b01 = al * bh, b10 = ah * bl, b11 = ah * bh;
	uint64_t mid1 = b10 + (b00 >> 32);
	uint64_t mid2 = b01 + (uint32_t)mid1;
	*hi = b11 + (mid1 >> 32) + (mid2 >> 32);
	return (mid2 << 32) | (uint32_t)b00;
#endif
}

static inline uint64_t shr128(uint64_t lo, uint64_t hi, uint32_t d) { return (hi << (64 - d)) | (lo >> d); }  // 0 < d < 64

static inline int32_t pow5bits(int32_t e) { return ((e * 1217359) >> 19) + 1; }
static inline uint32_t log10_pow2(int32_t e) { return (uint32_t)((e * 78913) >> 18); }
static inline uint32_t log10_pow5(int32_t e) { return (uint32_t)((e * 732923) >> 20); }

// 5^i's top 125 bits.
static void pow5(uint32_t i, uint64_t r[2]) {
	uint32_t base = i / 26, off = i - base * 26;
	const uint64_t *mul = ak_ryu_pow5_split[base];
	if (off == 0) {
		r[0] = mul[0], r[1] = mul[1];
		return;
	}
	uint64_t m = ak_ryu_pow5_table[off], high1, high0;
	uint64_t low1 = umul128(m, mul[1], &high1);
	uint64_t low0 = umul128(m, mul[0], &high0);
	uint64_t sum = high0 + low1;
	if (sum < high0) high1++;
	uint32_t delta = (uint32_t)(pow5bits((int32_t)i) - pow5bits((int32_t)(base * 26)));
	r[0] = shr128(low0, sum, delta) + ak_ryu_pow5_offsets[i];
	r[1] = shr128(sum, high1, delta);
}

// ⌊2^(pow5bits(i) - 1 + 125) / 5^i⌋ + 1.
static void inv_pow5(uint32_t i, uint64_t r[2]) {
	uint32_t base = (i + 25) / 26, off = base * 26 - i;
	const uint64_t *mul = ak_ryu_inv_pow5_split[base];
	if (off == 0) {
		r[0] = mul[0], r[1] = mul[1];
		return;
	}
	uint64_t m = ak_ryu_pow5_table[off], high1, high0;
	uint64_t low1 = umul128(m, mul[1], &high1);
	uint64_t low0 = umul128(m, mul[0] - 1, &high0);
	uint64_t sum = high0 + low1;
	if (sum < high0) high1++;
	uint32_t delta = (uint32_t)(pow5bits((int32_t)(base * 26)) - pow5bits((int32_t)i));
	r[0] = shr128(low0, sum, delta) + 1 + ak_ryu_inv_pow5_offsets[i];
	r[1] = shr128(sum, high1, delta);
}

// (m × mul) >> j, for j >= 64: the 192-bit product's bits from j.
static inline uint64_t mul_shift(uint64_t m, const uint64_t mul[2], int32_t j) {
	uint64_t high1, high0;
	uint64_t low1 = umul128(m, mul[1], &high1);
	umul128(m, mul[0], &high0);
	uint64_t sum = high0 + low1;
	if (sum < high0) high1++;
	return shr128(sum, high1, (uint32_t)(j - 64));
}

static inline uint32_t pow5_factor(uint64_t v) {
	uint32_t n = 0;
	while (v % 5 == 0) v /= 5, n++;
	return n;
}

// ryu: v's (finite, > 0, its bits not a power of 2's) shortest digits,
// *digits × 10^*exp.
static void ryu(uint64_t bits, uint64_t *digits, int32_t *exp) {
	uint64_t mant = bits & ((1ull << 52) - 1);
	uint32_t ieee_exp = (uint32_t)(bits >> 52) & 0x7ff;
	int32_t e2;
	uint64_t m2;
	if (ieee_exp == 0) {
		e2 = 1 - 1023 - 52 - 2;
		m2 = mant;
	} else {
		e2 = (int32_t)ieee_exp - 1023 - 52 - 2;
		m2 = (1ull << 52) | mant;
	}
	bool accept_bounds = (m2 & 1) == 0;  // an even mantissa: its interval's ends read back as it
	uint64_t mv = 4 * m2;
	uint32_t mm_shift = mant != 0 || ieee_exp <= 1;
	uint64_t vr, vp, vm, mul[2];
	int32_t e10;
	bool vm_zeros = false, vr_zeros = false;
	if (e2 >= 0) {
		uint32_t q = log10_pow2(e2) - (e2 > 3);
		e10 = (int32_t)q;
		int32_t k = 125 + pow5bits((int32_t)q) - 1;
		int32_t i = -e2 + (int32_t)q + k;
		inv_pow5(q, mul);
		vr = mul_shift(4 * m2, mul, i);
		vp = mul_shift(4 * m2 + 2, mul, i);
		vm = mul_shift(4 * m2 - 1 - mm_shift, mul, i);
		if (q <= 21) {
			// Only one of mp, mv and mm can be a multiple of 5, if any.
			if (mv % 5 == 0) vr_zeros = pow5_factor(mv) >= q;
			else if (accept_bounds) vm_zeros = pow5_factor(mv - 1 - mm_shift) >= q;
			else vp -= pow5_factor(mv + 2) >= q;
		}
	} else {
		uint32_t q = log10_pow5(-e2) - (-e2 > 1);
		e10 = (int32_t)q + e2;
		int32_t i = -e2 - (int32_t)q;
		int32_t k = pow5bits(i) - 125;
		int32_t j = (int32_t)q - k;
		pow5((uint32_t)i, mul);
		vr = mul_shift(4 * m2, mul, j);
		vp = mul_shift(4 * m2 + 2, mul, j);
		vm = mul_shift(4 * m2 - 1 - mm_shift, mul, j);
		if (q <= 1) {
			// mv = 4 m2 has two trailing zero bits at least.
			vr_zeros = true;
			if (accept_bounds) vm_zeros = mm_shift == 1;
			else vp--;
		} else if (q < 63) {
			vr_zeros = (mv & ((1ull << q) - 1)) == 0;
		}
	}
	// The shortest in the interval: digits off while vp and vm differ.
	int32_t removed = 0;
	uint8_t last = 0;
	uint64_t out;
	if (vm_zeros || vr_zeros) {
		for (;;) {
			uint64_t vp10 = vp / 10, vm10 = vm / 10;
			if (vp10 <= vm10) break;
			uint32_t vm_mod = (uint32_t)(vm - 10 * vm10);
			uint64_t vr10 = vr / 10;
			uint32_t vr_mod = (uint32_t)(vr - 10 * vr10);
			vm_zeros &= vm_mod == 0;
			vr_zeros &= last == 0;
			last = (uint8_t)vr_mod;
			vr = vr10, vp = vp10, vm = vm10;
			removed++;
		}
		if (vm_zeros) {
			for (;;) {
				uint64_t vm10 = vm / 10;
				uint32_t vm_mod = (uint32_t)(vm - 10 * vm10);
				if (vm_mod != 0) break;
				uint64_t vp10 = vp / 10, vr10 = vr / 10;
				uint32_t vr_mod = (uint32_t)(vr - 10 * vr10);
				vr_zeros &= last == 0;
				last = (uint8_t)vr_mod;
				vr = vr10, vp = vp10, vm = vm10;
				removed++;
			}
		}
		if (vr_zeros && last == 5 && vr % 2 == 0) last = 4;  // exactly .5: to even
		out = vr + ((vr == vm && (!accept_bounds || !vm_zeros)) || last >= 5);
	} else {
		bool up = false;
		uint64_t vp100 = vp / 100, vm100 = vm / 100;
		if (vp100 > vm100) {
			uint64_t vr100 = vr / 100;
			up = (uint32_t)(vr - 100 * vr100) >= 50;
			vr = vr100, vp = vp100, vm = vm100;
			removed += 2;
		}
		for (;;) {
			uint64_t vp10 = vp / 10, vm10 = vm / 10;
			if (vp10 <= vm10) break;
			uint64_t vr10 = vr / 10;
			up = (uint32_t)(vr - 10 * vr10) >= 5;
			vr = vr10, vp = vp10, vm = vm10;
			removed++;
		}
		out = vr + (vr == vm || up);
	}
	*digits = out;
	*exp = e10 + removed;
}

// shortest: v's (finite, not 0) shortest digits that read back as it,
// without trailing zeros, into d (17 room); their count. v is 0.d × 10^*n.
static int shortest(double v, char *d, int *n) {
	uint64_t bits;
	memcpy(&bits, &v, 8);
	bits &= ~(1ull << 63);
	uint64_t mant = bits & ((1ull << 52) - 1);
	uint32_t ieee_exp = (uint32_t)(bits >> 52);
	if (AK_UNLIKELY(mant == 0 && ieee_exp > 1)) {
		// A power of 2: as printf rounds, with as few digits as read back.
		char e[40];
		double a = fabs(v);
		for (int p = 0; p < 17; p++) {
			snprintf(e, sizeof e, "%.*e", p, a);
			if (strtod(e, NULL) == a) break;
		}
		int k = 0;
		const char *c = e;
		for (; *c && *c != 'e'; c++)
			if (*c >= '0' && *c <= '9') d[k++] = *c;
		while (k > 1 && d[k - 1] == '0') k--;
		*n = atoi(c + 1) + 1;
		return k;
	}
	uint64_t m;
	int32_t e;
	ryu(bits, &m, &e);
	while (m >= 10 && m % 10 == 0) m /= 10, e++;
	char buf[24];
	char *p = int_digits((int64_t)m, buf + sizeof buf);
	int k = (int)(buf + sizeof buf - p);
	memcpy(d, p, (size_t)k);
	*n = e + k;
	return k;
}

ak_str *ak_str_from_uint(uint64_t v) {
	char buf[24];
	int n = snprintf(buf, sizeof buf, "%llu", (unsigned long long)v);
	return ak_str_new(buf, (size_t)n);
}

// ---- Integers' text ----

const char *const ak_int_names[8] = {"i8", "i16", "i32", "int", "u8", "u16", "u32", "u64"};

static int digit_of(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'z') return c - 'a' + 10;
	if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
	return 99;
}

bool ak_int_parse(const char *text, size_t n, int radix, int t, int64_t *out) {
	size_t i = 0;
	bool neg = false;
	if (i < n && (text[i] == '+' || text[i] == '-')) neg = text[i++] == '-';
	if (i == n || radix < 2 || radix > 36) return false;
	uint64_t v = 0;
	for (; i < n; i++) {
		int d = digit_of(text[i]);
		if (d >= radix) return false;
		if (__builtin_mul_overflow(v, (uint64_t)radix, &v) || __builtin_add_overflow(v, (uint64_t)d, &v)) return false;
	}
	if (t == AK_U64) {
		if (neg && v) return false;
		*out = (int64_t)v;
		return true;
	}
	// Up to 2^63 (the smallest int's magnitude), then the type's range.
	if (v > (uint64_t)INT64_MAX + (neg ? 1 : 0)) return false;
	int64_t s = neg ? (int64_t)(0 - v) : (int64_t)v;
	if (!ak_int_fits(s, AK_I64, t)) return false;
	*out = s;
	return true;
}

int ak_int_text(char *buf, int64_t v, int t, int radix) {
	static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	bool neg = t != AK_U64 && v < 0;
	uint64_t u = neg ? 0 - (uint64_t)v : (uint64_t)v;
	char tmp[66];
	int i = (int)sizeof tmp;
	do {
		tmp[--i] = digits[u % (uint64_t)radix];
		u /= (uint64_t)radix;
	} while (u);
	if (neg) tmp[--i] = '-';
	int n = (int)sizeof tmp - i;
	memcpy(buf, tmp + i, (size_t)n);
	return n;
}

void ak_int_throw_fit(int64_t v, int from, int to) {
	char buf[66];
	int n = ak_int_text(buf, v, from, 10);
	const char *name = ak_int_names[to & 7];
	ak_throw_msg("integer overflow: %.*s doesn't fit in %s %s", n, buf, name[0] == 'i' ? "an" : "a", name);
}

// fmt_float writes the shortest form of v that reads back as v, as
// JavaScript prints numbers (ECMAScript's Number::toString): the fewest
// significant digits that round-trip, then plain digits for magnitudes
// from 1e-6 to below 1e21 (an integer without a fraction), an exponent
// otherwise. buf has 32 bytes room at least; the length.
static int fmt_float(char *buf, double v) {
	if (v != v) return (memcpy(buf, "NaN", 3), 3);
	if (v == INFINITY) return (memcpy(buf, "Infinity", 8), 8);
	if (v == -INFINITY) return (memcpy(buf, "-Infinity", 9), 9);
	if (v == 0) return (buf[0] = '0', 1);
	char digits[24];
	int n, k = shortest(v, digits, &n);
	// v is 0.d1..dk × 10^n.
	char *out = buf;
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
		out[o++] = 'e';
		out[o++] = n - 1 < 0 ? '-' : '+';
		char eb[24];
		char *ep = int_digits(abs(n - 1), eb + sizeof eb);
		memcpy(out + o, ep, (size_t)(eb + sizeof eb - ep));
		o += (size_t)(eb + sizeof eb - ep);
	}
	return (int)o;
}

int ak_float_text(char *buf, double v) { return fmt_float(buf, v); }

// ---- Reading floats: Eisel and Lemire's algorithm ----
//
// A decimal of at most 19 digits, m × 10^q, is m times 10^q's 128 top
// bits (ryu_gen.c's, rounded down): the product's top bits are the
// float's, unless they fall too near a halfway point to tell, which it
// leaves to strtod (Daniel Lemire, "Number Parsing at a Gigabyte per
// Second", 2021; as Go's strconv.eiselLemire64 has it).

extern const uint64_t ak_pow10_base[26][2];
extern const uint64_t ak_pow5_norm[27];
extern const uint8_t ak_pow10_offsets[696];

#define P10_MIN (-348)
#define P10_MAX 347
#define P10_BASE (-351)

// pow10_128 is 10^q's top 128 bits, rounded down (P10_MIN <= q <= P10_MAX):
// its step's times 5^off, corrected.
static void pow10_128(int q, uint64_t *hi, uint64_t *lo) {
	int k = (q - P10_BASE) / 27, off = q - P10_BASE - 27 * k;
	const uint64_t *b = ak_pow10_base[k];
	uint64_t f = ak_pow5_norm[off];
	uint64_t p1h, p1l = umul128(b[0], f, &p1h);
	uint64_t p2h, p2l = umul128(b[1], f, &p2h);
	uint64_t mid = p2l + p1h, top = p2h + (mid < p2l);
	if (!(top >> 63)) {
		top = top << 1 | mid >> 63;
		mid = mid << 1 | p1l >> 63;
	}
	*hi = top;
	*lo = mid + ak_pow10_offsets[q - P10_MIN];
}

// eisel_lemire is m × 10^q (m not 0) as a double, into *out: false when
// it can't tell (strtod's to decide), or it's past a double's range.
static bool eisel_lemire(uint64_t m, int q, bool neg, double *out) {
	if (q < P10_MIN || q > P10_MAX) return false;
	int clz = __builtin_clzll(m);
	m <<= clz;
	uint64_t exp2 = (uint64_t)(((217706 * q) >> 16) + 64 + 1023) - (uint64_t)clz;
	uint64_t phi, plo;
	pow10_128(q, &phi, &plo);
	uint64_t xhi, xlo = umul128(m, phi, &xhi);
	if ((xhi & 0x1ff) == 0x1ff && xlo + m < m) {
		// The wider product: with 10^q's low word too.
		uint64_t yhi, ylo = umul128(m, plo, &yhi);
		uint64_t mhi = xhi, mlo = xlo + yhi;
		if (mlo < xlo) mhi++;
		if ((mhi & 0x1ff) == 0x1ff && mlo + 1 == 0 && ylo + m < m) return false;
		xhi = mhi, xlo = mlo;
	}
	uint64_t msb = xhi >> 63, mant = xhi >> (msb + 9);
	exp2 -= 1 ^ msb;
	if (xlo == 0 && (xhi & 0x1ff) == 0 && (mant & 3) == 1) return false;  // halfway: can't tell
	mant += mant & 1;
	mant >>= 1;
	if (mant >> 53) {
		mant >>= 1;
		exp2++;
	}
	if (exp2 - 1 >= 0x7ff - 1) return false;  // subnormal, or past the largest
	uint64_t bits = exp2 << 52 | (mant & ((1ull << 52) - 1));
	if (neg) bits |= 1ull << 63;
	memcpy(out, &bits, 8);
	return true;
}

// ---- A precision's digits: printf's %.*f and %.*e ----
//
// v is m × 2^e exactly (m below 2^53), so its digits to a precision are
// an integer's, N = v × 10^k rounded half to even (as printf rounds the
// exact binary value: 0.125 to 0.12, 2.675 to 2.67), computed in 128
// bits: m × 10^k shifted or divided, its remainder deciding the
// rounding. That covers what's printed (precisions to 19, numbers whose
// digits fit); the rest goes to snprintf, as before.

#ifdef __SIZEOF_INT128__
__extension__ typedef unsigned __int128 u128;

static const uint64_t pow10_u64[20] = {
	1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull, 100000000ull, 1000000000ull,
	10000000000ull, 100000000000ull, 1000000000000ull, 10000000000000ull, 100000000000000ull,
	1000000000000000ull, 10000000000000000ull, 100000000000000000ull, 1000000000000000000ull,
	10000000000000000000ull,
};

// split_double is v (finite, >= 0) as m × 2^e.
static void split_double(double v, uint64_t *m, int *e) {
	uint64_t bits;
	memcpy(&bits, &v, 8);
	int be = (int)((bits >> 52) & 0x7ff);
	*m = bits & ((1ull << 52) - 1);
	if (be == 0) {
		*e = -1074;
	} else {
		*m |= 1ull << 52;
		*e = be - 1075;
	}
}

// scaled is v × 10^k rounded half to even (v = m × 2^e, |k| <= 19),
// into *out: false when it doesn't fit 64 bits or the arithmetic 128.
static bool scaled(uint64_t m, int e, int k, uint64_t *out) {
	u128 num = m, den = 1;
	if (k >= 0) num *= pow10_u64[k];  // below 2^117
	else den = pow10_u64[-k];
	if (e >= 0) {
		if (e >= 128 || (num >> (127 - e)) != 0) return false;
		num <<= e;
	} else {
		int s = -e;
		if (den == 1) {
			// num / 2^s: a shift, its remainder the bits shifted out.
			if (s >= 128) {
				*out = 0;  // num is below 2^117: under half of 2^128
				return true;
			}
			u128 q = num >> s, r = num - (q << s), half = (u128)1 << (s - 1);
			if (r > half || (r == half && (q & 1))) q++;
			if (q >> 64) return false;
			*out = (uint64_t)q;
			return true;
		}
		if (s >= 64 || (den >> (127 - s)) != 0) return false;
		den <<= s;
	}
	u128 q = num / den, r = num - q * den;
	if (r > den - r || (r == den - r && (q & 1))) q++;
	if (q >> 64) return false;
	*out = (uint64_t)q;
	return true;
}

// put_digits writes u's decimal digits, at least w of them (0s before),
// at p; how many.
static int put_digits(char *p, uint64_t u, int w) {
	char d[24], *end = d + sizeof d, *s;
	if (u <= INT64_MAX) {
		s = int_digits((int64_t)u, end);
	} else {
		// Past int64's: the last digit apart, the rest as one.
		s = int_digits((int64_t)(u / 10), end - 1);
		end[-1] = (char)('0' + u % 10);
	}
	int n = (int)(end - s), k = 0;
	for (int i = n; i < w; i++) p[k++] = '0';
	memcpy(p + k, s, (size_t)n);
	return k + n;
}

int ak_float_fixed(char *buf, double v, int prec) {
	if (prec >= 0 && prec <= 19 && v >= 0 && v < 1e19) {
		uint64_t m, n;
		int e;
		split_double(v, &m, &e);
		if (scaled(m, e, prec, &n)) {
			uint64_t ip = n / pow10_u64[prec], fp = n % pow10_u64[prec];
			int k = put_digits(buf, ip, 1);
			if (prec) {
				buf[k++] = '.';
				k += put_digits(buf + k, fp, prec);
			}
			buf[k] = 0;
			return k;
		}
	}
	return snprintf(buf, AK_FLOAT_FMT_MAX, "%.*f", prec, v);
}

int ak_float_exp(char *buf, double v, int prec) {
	if (prec >= 0 && prec <= 17 && v >= 0 && v == v && v != INFINITY) {
		uint64_t m, n = 0;
		int e;
		split_double(v, &m, &e);
		int E = 0;
		bool ok = m == 0;
		if (!ok) {
			// Its leading digit's place, near enough: log10 of its top
			// bit's; one off at most, which the loop corrects.
			int top = e + 63 - __builtin_clzll(m);
			E = (int)floor(top * 0.30102999566398120);
			for (int tries = 0; tries < 3; tries++) {
				int k = prec - E;
				if (k > 19 || k < -19 || !scaled(m, e, k, &n)) break;
				if (n >= pow10_u64[prec + 1]) E++;
				else if (n < pow10_u64[prec]) E--;
				else {
					ok = true;
					break;
				}
			}
		}
		if (ok) {
			char d[24];
			put_digits(d, n, prec + 1);
			int k = 0;
			buf[k++] = d[0];
			if (prec) {
				buf[k++] = '.';
				memcpy(buf + k, d + 1, (size_t)prec);
				k += prec;
			}
			buf[k++] = 'e';
			buf[k++] = E < 0 ? '-' : '+';
			k += put_digits(buf + k, (uint64_t)(E < 0 ? -E : E), 2);
			buf[k] = 0;
			return k;
		}
	}
	return snprintf(buf, AK_FLOAT_FMT_MAX, "%.*e", prec, v);
}
#else
int ak_float_fixed(char *buf, double v, int prec) { return snprintf(buf, AK_FLOAT_FMT_MAX, "%.*f", prec, v); }
int ak_float_exp(char *buf, double v, int prec) { return snprintf(buf, AK_FLOAT_FMT_MAX, "%.*e", prec, v); }
#endif

ak_str *ak_str_from_float(double v) {
	char buf[48];
	int n = fmt_float(buf, v);
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
	uint32_t h = (uint32_t)ak_hash_bytes(s->data, s->len);
	if (h == 0) h = 1;
	// Immortal strings (constants) may be read from worker threads: their
	// hash, and what's known of their bytes, is computed once, as they
	// become immortal (ak_make_immortal hashes them).
	if (s->h.rc != AK_RC_IMMORTAL) {
		s->hash = h;
		ak_str_ascii(s);
	}
	return h;
}

// ---- Hashing: wyhash (final version 4, public domain) ----

static inline void wymum(uint64_t *a, uint64_t *b) {
#ifdef __SIZEOF_INT128__
	__uint128_t r = (__uint128_t)*a * *b;
	*a = (uint64_t)r;
	*b = (uint64_t)(r >> 64);
#else
	uint64_t ha = *a >> 32, hb = *b >> 32, la = (uint32_t)*a, lb = (uint32_t)*b;
	uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb;
	uint64_t t = rl + (rm0 << 32), c = t < rl;
	uint64_t lo = t + (rm1 << 32);
	c += lo < t;
	*a = lo;
	*b = rh + (rm0 >> 32) + (rm1 >> 32) + c;
#endif
}

static inline uint64_t wymix(uint64_t a, uint64_t b) {
	wymum(&a, &b);
	return a ^ b;
}

static inline uint64_t wyr8(const uint8_t *p) {
	uint64_t v;
	memcpy(&v, p, 8);
	return v;
}

static inline uint64_t wyr4(const uint8_t *p) {
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}

static inline uint64_t wyr3(const uint8_t *p, size_t k) { return ((uint64_t)p[0] << 16) | ((uint64_t)p[k >> 1] << 8) | p[k - 1]; }

AK_NOINLINE uint64_t ak_hash_bytes(const void *key, size_t len) {
	static const uint64_t p0 = 0x2d358dccaa6c78a5ull, p1 = 0x8bb84b93962eacc9ull, p2 = 0x4b33a62ed433d4a3ull, p3 = 0x4d5a2da51de1aa47ull;
	const uint8_t *p = key;
	uint64_t seed = 0x9e3779b97f4a7c15ull;
	seed ^= wymix(seed ^ p0, p1);
	uint64_t a, b;
	if (AK_LIKELY(len <= 16)) {
		if (AK_LIKELY(len >= 4)) {
			a = (wyr4(p) << 32) | wyr4(p + ((len >> 3) << 2));
			b = (wyr4(p + len - 4) << 32) | wyr4(p + len - 4 - ((len >> 3) << 2));
		} else if (AK_LIKELY(len > 0)) {
			a = wyr3(p, len);
			b = 0;
		} else {
			a = b = 0;
		}
	} else {
		size_t i = len;
		if (AK_UNLIKELY(i > 48)) {
			uint64_t see1 = seed, see2 = seed;
			do {
				seed = wymix(wyr8(p) ^ p1, wyr8(p + 8) ^ seed);
				see1 = wymix(wyr8(p + 16) ^ p2, wyr8(p + 24) ^ see1);
				see2 = wymix(wyr8(p + 32) ^ p3, wyr8(p + 40) ^ see2);
				p += 48;
				i -= 48;
			} while (AK_LIKELY(i > 48));
			seed ^= see1 ^ see2;
		}
		while (AK_UNLIKELY(i > 16)) {
			seed = wymix(wyr8(p) ^ p1, wyr8(p + 8) ^ seed);
			i -= 16;
			p += 16;
		}
		a = wyr8(p + i - 16);
		b = wyr8(p + i - 8);
	}
	a ^= p1;
	b ^= seed;
	wymum(&a, &b);
	return wymix(a ^ p0 ^ len, b ^ p1);
}

// ---- Bytes eight at a time ----

static inline uint64_t load64(const char *p) {
	uint64_t x;
	memcpy(&x, p, 8);
	return x;
}

#define HIGHS 0x8080808080808080ull

// The characters (code points, as bytes that don't continue one) in
// n bytes at p; *ascii is cleared when one isn't ASCII.
static int64_t chars_in(const char *p, size_t n, bool *ascii) {
	int64_t cont = 0;
	uint64_t any = 0;
	size_t i = 0;
	for (; i + 8 <= n; i += 8) {
		uint64_t x = load64(p + i);
		any |= x;
		// A continuation byte is 10xxxxxx: its high bit set, the next one
		// clear.
		cont += __builtin_popcountll(x & ~(x << 1) & HIGHS);
	}
	for (; i < n; i++) {
		uint8_t c = (uint8_t)p[i];
		any |= c;
		cont += (c & 0xc0) == 0x80;
	}
	if (any & HIGHS) *ascii = false;
	return (int64_t)n - cont;
}

AK_NOINLINE bool ak_str_ascii(const ak_str *cs) {
	if (cs->h.flags & AK_STR_KNOWN) return (cs->h.flags & AK_STR_ASCII) != 0;
	uint64_t any = 0;
	size_t i = 0;
	for (; i + 8 <= cs->len; i += 8) any |= load64(cs->data + i);
	for (; i < cs->len; i++) any |= (uint8_t)cs->data[i];
	bool ascii = (any & HIGHS) == 0;
	if (cs->h.rc != AK_RC_IMMORTAL) ((ak_str *)cs)->h.flags |= (uint8_t)(AK_STR_KNOWN | (ascii ? AK_STR_ASCII : 0));
	return ascii;
}

ak_str *ak_str_slice(ak_str *s, int64_t from, int64_t to) {
	if (from < 0) from = 0;
	if (to > s->len) to = s->len;
	if (from >= to) return ak_str_empty();
	if (from == 0 && to == s->len) return ak_retain(s);
	return ak_str_new(s->data + from, (size_t)(to - from));
}

int64_t ak_str_count(const ak_str *s) {
	if (s->h.flags & AK_STR_KNOWN && s->h.flags & AK_STR_ASCII) return s->len;
	bool ascii = true;
	int64_t n = chars_in(s->data, s->len, &ascii);
	if (s->h.rc != AK_RC_IMMORTAL) ((ak_str *)s)->h.flags |= (uint8_t)(AK_STR_KNOWN | (ascii ? AK_STR_ASCII : 0));
	return n;
}

int64_t ak_str_char_offset(const ak_str *s, int64_t c) {
	if (c <= 0) return 0;
	if (ak_str_ascii(s)) return c < s->len ? c : s->len;
	// Eight bytes at a time while they hold fewer characters than are
	// left to skip, then a byte at a time to the one.
	const char *p = s->data;
	size_t n = s->len, i = 0;
	int64_t k = 0;  // characters before i
	for (; i + 8 <= n; i += 8) {
		uint64_t x = load64(p + i);
		int64_t in = 8 - __builtin_popcountll(x & ~(x << 1) & HIGHS);
		if (k + in > c) break;
		k += in;
	}
	// i is at a character's start unless the bytes before it ended in the
	// middle of one: back up to its start (a continuation byte isn't one).
	for (; i < n; i++) {
		if (((uint8_t)p[i] & 0xc0) != 0x80) {
			if (k == c) return (int64_t)i;
			k++;
		}
	}
	return (int64_t)n;
}

// ---- Searching ----

// byte_rank: how common a byte is in text, the higher the more (as the
// memchr crate ranks bytes for its prefilter): a needle's rarest byte is
// the one to look for.
static int byte_rank(uint8_t c) {
	static const char common[] = " etaoinsrhldcumfpgwybvkxjqz";
	bool upper = c >= 'A' && c <= 'Z';
	const char *at = memchr(common, upper ? c + 32 : c, sizeof common - 1);
	if (at) return 255 - (int)(at - common) * 4 - (upper ? 80 : 0);
	if (c >= '0' && c <= '9') return 150;
	if (c == '\n' || c == ',' || c == '.' || c == '_' || c == '-' || c == '/' || c == '"' || c == ':') return 140;
	if (c >= 0x80) return 100;  // UTF-8's: common in some text, rare in other
	if (c < 0x20) return 10;
	return 60;  // the rest of punctuation
}

#define MAXZ(a, b) ((a) > (b) ? (a) : (b))

// two_way: the first n (l bytes, l >= 2) in h[0:hn], by the two-way
// algorithm (Crochemore and Perrin), with a bad character shift on its
// last byte, as musl's memmem has it: time linear in hn, whatever the
// needle.
static AK_NOINLINE int64_t two_way(const uint8_t *h, size_t hn, const uint8_t *n, size_t l) {
	size_t shift[256];
	uint64_t byteset[4] = {0};
	for (size_t i = 0; i < l; i++) {
		byteset[n[i] >> 6] |= 1ull << (n[i] & 63);
		shift[n[i]] = i + 1;
	}
	// The maximal suffix, and its period, by both orders; the longer.
	size_t ip = (size_t)-1, jp = 0, k = 1, p = 1;
	while (jp + k < l) {
		if (n[ip + k] == n[jp + k]) {
			if (k == p) jp += p, k = 1;
			else k++;
		} else if (n[ip + k] > n[jp + k]) {
			jp += k, k = 1, p = jp - ip;
		} else {
			ip = jp++, k = p = 1;
		}
	}
	size_t ms = ip, p0 = p;
	ip = (size_t)-1, jp = 0, k = p = 1;
	while (jp + k < l) {
		if (n[ip + k] == n[jp + k]) {
			if (k == p) jp += p, k = 1;
			else k++;
		} else if (n[ip + k] < n[jp + k]) {
			jp += k, k = 1, p = jp - ip;
		} else {
			ip = jp++, k = p = 1;
		}
	}
	if (ip + 1 > ms + 1) ms = ip;
	else p = p0;
	// A periodic needle remembers how much of it matched across a shift.
	size_t mem0, mem = 0;
	if (memcmp(n, n + p, ms + 1)) {
		mem0 = 0;
		p = MAXZ(ms, l - ms - 1) + 1;
	} else {
		mem0 = l - p;
	}
	const uint8_t *start = h, *z = h + hn;
	for (;;) {
		if ((size_t)(z - h) < l) return -1;
		uint8_t last = h[l - 1];
		if (byteset[last >> 6] & (1ull << (last & 63))) {
			k = l - shift[last];
			if (k) {
				if (k < mem) k = mem;
				h += k, mem = 0;
				continue;
			}
		} else {
			h += l, mem = 0;
			continue;
		}
		for (k = MAXZ(ms + 1, mem); k < l && n[k] == h[k]; k++) {}
		if (k < l) {
			h += k - ms, mem = 0;
			continue;
		}
		for (k = ms + 1; k > mem && n[k - 1] == h[k - 1]; k--) {}
		if (k <= mem) return h - start;
		h += p, mem = mem0;
	}
}

int64_t ak_mem_find(const void *hv, size_t hn, const void *nv, size_t nn) {
	const uint8_t *h = hv, *n = nv;
	if (nn == 0) return 0;
	if (nn > hn) return -1;
	if (nn == 1) {
		const uint8_t *p = memchr(h, n[0], hn);
		return p ? p - h : -1;
	}
	// memchr for the needle's rarest byte, then a check of the rest at
	// each one; while false starts are few, nothing's faster.
	size_t r = 0;
	for (size_t i = 1; i < nn && i < 256; i++)
		if (byte_rank(n[i]) < byte_rank(n[r])) r = i;
	uint8_t rb = n[r], first = n[0], lastb = n[nn - 1];
	size_t i = r, end = hn - nn + r;  // the rare byte's offsets: from r to end
	size_t falses = 0, from = 0;
	while (i <= end) {
		const uint8_t *p = memchr(h + i, rb, end - i + 1);
		if (!p) return -1;
		size_t at = (size_t)(p - h) - r;
		if (h[at] == first && h[at + nn - 1] == lastb && memcmp(h + at, n, nn) == 0) return (int64_t)at;
		i = (size_t)(p - h) + 1;
		// Too many: each costs a compare of up to nn bytes, and the
		// two-way algorithm none at all.
		if (++falses > 64 && falses * MAXZ(16, nn) > at - from) {
			int64_t w = two_way(h + at + 1, hn - at - 1, n, nn);
			return w < 0 ? -1 : (int64_t)(at + 1) + w;
		}
	}
	return -1;
}

int64_t ak_mem_find_last(const void *hv, size_t hn, const void *nv, size_t nn) {
	const uint8_t *h = hv, *n = nv;
	if (nn == 0) return (int64_t)hn;
	if (nn > hn) return -1;
	uint8_t first = n[0], last = n[nn - 1];
	for (size_t i = hn - nn + 1; i-- > 0;)
		if (h[i + nn - 1] == last && h[i] == first && memcmp(h + i, n, nn) == 0) return (int64_t)i;
	return -1;
}

int64_t ak_str_find(const ak_str *s, const ak_str *sub) { return ak_mem_find(s->data, s->len, sub->data, sub->len); }
int64_t ak_str_find_last(const ak_str *s, const ak_str *sub) { return ak_mem_find_last(s->data, s->len, sub->data, sub->len); }

bool ak_str_has_prefix(const ak_str *s, const ak_str *p) {
	return p->len <= s->len && memcmp(s->data, p->data, p->len) == 0;
}

bool ak_str_has_suffix(const ak_str *s, const ak_str *p) {
	return p->len <= s->len && memcmp(s->data + s->len - p->len, p->data, p->len) == 0;
}

bool ak_str_contains(const ak_str *s, const ak_str *sub) { return ak_str_find(s, sub) >= 0; }

static ak_str *map_ascii(ak_str *s, bool upper) {
	uint8_t lo = upper ? 'a' : 'A';
	uint32_t i = 0;
	while (i < s->len && (uint8_t)((uint8_t)s->data[i] - lo) >= 26) i++;
	if (i == s->len) return ak_retain(s);
	ak_str *o = ak_str_new(NULL, s->len);
	memcpy(o->data, s->data, i);
	for (; i < s->len; i++) {
		uint8_t c = (uint8_t)s->data[i];
		o->data[i] = (char)((uint8_t)(c - lo) < 26 ? c ^ 0x20 : c);
	}
	o->h.flags = AK_STR_KNOWN | AK_STR_ASCII;
	return o;
}

static bool ascii_only(const ak_str *s) { return ak_str_ascii(s); }

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
	// What a code point maps to is written as it's found; what doesn't
	// change is copied a run at a time (as it was: what isn't UTF-8 too),
	// and an ASCII byte is mapped where it is, without a decode.
	ak_strb b = {0};
	bool changed = false;
	uint8_t lo = k == AK_CASE_UPPER ? 'a' : 'A';
	size_t run = 0;  // the unchanged bytes not yet copied start here
	for (size_t i = 0; i < s->len;) {
		uint8_t a = (uint8_t)s->data[i];
		if (a < 0x80) {
			if ((uint8_t)(a - lo) >= 26) {
				i++;
				continue;
			}
			// A letter that changes: the run before it copied, then the
			// ASCII from it on mapped in one go.
			if (!changed) {
				changed = true;
				ak_strb_room(&b, s->len + s->len / 8);
			}
			ak_strb_cstr(&b, s->data + run, i - run);
			size_t j = i;
			while (j < s->len && (uint8_t)s->data[j] < 0x80) j++;
			char *w = ak_strb_room(&b, j - i);
			for (size_t q = i; q < j; q++) {
				uint8_t c = (uint8_t)s->data[q];
				w[q - i] = (char)((uint8_t)(c - lo) < 26 ? c ^ 0x20 : c);
			}
			ak_strb_wrote(&b, j - i);
			i = run = j;
			continue;
		}
		size_t from = i;
		uint32_t c = ak_utf8_decode(s->data, s->len, &i);
		uint32_t out[3];
		int m;
		if (k == AK_CASE_LOWER && c == 0x3a3 && final_sigma(s->data, s->len, from, i)) out[0] = 0x3c2, m = 1;
		else m = ak_case_map(c, k, out);
		if (m == 1 && out[0] == c) continue;
		if (!changed) {
			changed = true;
			ak_strb_room(&b, s->len + s->len / 8);
		}
		ak_strb_cstr(&b, s->data + run, from - run);
		char *w = ak_strb_room(&b, 12);
		size_t n = 0;
		for (int q = 0; q < m; q++) n += ak_utf8_encode(out[q], w + n);
		ak_strb_wrote(&b, n);
		run = i;
	}
	if (!changed) return ak_retain(s);
	ak_strb_cstr(&b, s->data + run, s->len - run);
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

// ascii_lower is c lowered if it's an ASCII letter: folding an ASCII
// character (a string all ASCII has only those) is lowering it.
static inline uint8_t ascii_lower(uint8_t c) { return (uint8_t)(c - 'A') < 26 ? c | 0x20 : c; }

// find_ascii_fold is find_fold of two strings all ASCII: a byte each, no
// folding made (a contains's text of megabytes would be four times that
// as code points).
static bool find_ascii_fold(const ak_str *s, const ak_str *sub, int where) {
	size_t n = s->len, m = sub->len;
	if (m > n) return false;
	const uint8_t *a = (const uint8_t *)s->data, *b = (const uint8_t *)sub->data;
	size_t from = where == 2 ? n - m : 0, to = where == 0 ? n - m : from;
	uint8_t first = ascii_lower(b[0]);
	for (size_t i = from; i <= to; i++) {
		if (ascii_lower(a[i]) != first) continue;
		size_t k = 1;
		while (k < m && ascii_lower(a[i + k]) == ascii_lower(b[k])) k++;
		if (k == m) return true;
	}
	return false;
}

// SUFFIX_MAX: the longest sub (in bytes) suffix_fold folds s's end for;
// its folding is at most three code points a byte, and s's a code point
// more's.
#define SUFFIX_MAX 64

// suffix_fold is find_fold at the end, for a sub shorter than SUFFIX_MAX:
// only s's last code points folded, as many as sub's folding has (or a
// code point's more), read backwards.
static bool suffix_fold(const ak_str *s, const ak_str *sub) {
	uint32_t sb[3 * SUFFIX_MAX + 3], sa[3 * SUFFIX_MAX + 6];
	size_t m;
	uint32_t *b = fold_all(sub, sb, AK_LEN(sb), &m);
	bool found = false;
	// s's foldings into sa's end, a code point at a time from s's.
	size_t n = 0, end = s->len;
	while (n < m && end > 0) {
		size_t st = end - 1;
		while (st > 0 && ((uint8_t)s->data[st] & 0xc0) == 0x80 && end - st < 4) st--;
		size_t i = st;
		uint32_t c = ak_utf8_decode(s->data, end, &i), m3[3];
		if (i != end) {
			// Not one code point (what isn't UTF-8): a byte.
			st = end - 1;
			i = st;
			c = ak_utf8_decode(s->data, end, &i);
		}
		int k = ak_case_map(c, AK_CASE_FOLD, m3);
		for (int q = k; q-- > 0;) sa[AK_LEN(sa) - ++n] = m3[q] | (q == 0 ? FOLD_START : 0);
		end = st;
	}
	if (n >= m) {
		const uint32_t *a = sa + AK_LEN(sa) - m;
		size_t k = 0;
		while (k < m && (a[k] & ~FOLD_START) == (b[k] & ~FOLD_START)) k++;
		found = k == m && (a[0] & FOLD_START);
	}
	if (b != sb) ak_free(b);
	return found;
}

// find_fold: whether sub's folding is in s's at a code point's start
// (where is anywhere, or 1: at the start, 2: at the end), ending at one.
static bool find_fold(const ak_str *s, const ak_str *sub, int where) {
	if (sub->len == 0) return true;
	if (ak_str_ascii(s) && ak_str_ascii(sub)) return find_ascii_fold(s, sub, where);
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

bool ak_str_has_suffix_fold(const ak_str *s, const ak_str *p) {
	if (p->len == 0) return true;
	if (ak_str_ascii(s) && ak_str_ascii(p)) return find_ascii_fold(s, p, 2);
	if (p->len < SUFFIX_MAX) return suffix_fold(s, p);
	return find_fold(s, p, 2);
}
bool ak_str_contains_fold(const ak_str *s, const ak_str *sub) { return find_fold(s, sub, 0); }

static bool is_space(char c) { return ak_space(c); }

ak_str *ak_str_trim(ak_str *s) {
	uint32_t a = 0, b = s->len;
	while (a < b && is_space(s->data[a])) a++;
	while (b > a && is_space(s->data[b - 1])) b--;
	return ak_str_slice(s, a, b);
}

ak_str *ak_str_replace_n(ak_str *s, const ak_str *old, const ak_str *new_, int64_t count) {
	if (old->len == 0 || count == 0) return ak_retain(s);
	// Where the olds are, counted first (as Go's Replace does), so the
	// result is made once, at its size.
	uint32_t found = 0;
	for (int64_t i = 0; count < 0 || found < count;) {
		int64_t at = ak_mem_find(s->data + i, s->len - (size_t)i, old->data, old->len);
		if (at < 0) break;
		found++;
		i += at + old->len;
	}
	if (!found) return ak_retain(s);
	size_t len = s->len + (size_t)found * new_->len - (size_t)found * old->len;
	if (len >= UINT32_MAX) ak_panic("out of memory (a string of %zu bytes)", len);
	ak_str *o = ak_str_new(NULL, len);
	char *w = o->data;
	size_t i = 0;
	for (uint32_t k = 0; k < found; k++) {
		size_t at = (size_t)ak_mem_find(s->data + i, s->len - i, old->data, old->len);
		memcpy(w, s->data + i, at);
		w += at;
		memcpy(w, new_->data, new_->len);
		w += new_->len;
		i += at + old->len;
	}
	memcpy(w, s->data + i, s->len - i);
	return o;
}

ak_str *ak_str_replace(ak_str *s, const ak_str *old, const ak_str *new_) { return ak_str_replace_n(s, old, new_, -1); }

// ak_mem_count is how many times needle (nn bytes, not none) is in h,
// not overlapping, as ak_mem_find finds them one after another.
static size_t mem_count(const char *h, size_t hn, const char *needle, size_t nn) {
	size_t c = 0;
	if (nn == 1) {
		for (size_t i = 0; i < hn; i++) c += h[i] == *needle;  // vectorized
		return c;
	}
	for (size_t at = 0;;) {
		int64_t i = ak_mem_find(h + at, hn - at, needle, nn);
		if (i < 0) return c;
		c++;
		at += (size_t)i + nn;
	}
}

// The parts of a split, its array made at its size (the separators
// counted first, as Go's Split does) and its pieces in slabs.
ak_arr *ak_str_split_n(ak_str *s, const ak_str *sep, int64_t count) {
	ak_pieces p;
	if (sep->len == 0) {
		ak_pieces_start(&p, s, AK_PIECES_LEFT(s->len, s->len));
		bool ascii = p.flags & AK_STR_ASCII;
		// Split into characters.
		ak_arr *a = ak_arr_new(AK_K_REF, NULL, ascii ? s->len : 4);
		a->h.flags = 1;
		ak_val *v = ak_arr_vals(a);
		if (ascii) {
			for (uint32_t i = 0; i < s->len; i++) v[i].p = ak_piece(&p, s->data + i, 1);
			a->len = s->len;
			ak_pieces_end(&p);
			return a;
		}
		uint32_t i = 0;
		while (i < s->len) {
			uint32_t j = i + 1;
			while (j < s->len && ((uint8_t)s->data[j] & 0xc0) == 0x80) j++;
			ak_arr_push(&a, (ak_val){.p = ak_piece(&p, s->data + i, j - i)});
			i = j;
		}
		ak_pieces_end(&p);
		return a;
	}
	if (count == 0) {
		ak_arr *a = ak_arr_new(AK_K_REF, NULL, 0);
		a->h.flags = 1;
		return a;
	}
	// A byte's separators are counted first (a vectorized pass) and the
	// array made at its size; a longer separator's, found once each, fill
	// an array that doubles as it fills.
	size_t parts = sep->len == 1 ? mem_count(s->data, s->len, sep->data, 1) + 1 : s->len / sep->len + 1;
	if (count > 0 && (uint64_t)count < parts) parts = (size_t)count;
	if (parts > UINT32_MAX) parts = UINT32_MAX;
	ak_pieces_start(&p, s, AK_PIECES_LEFT(parts, s->len));
	uint32_t cap = sep->len == 1 || parts < 16 ? (uint32_t)parts : 16;
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, cap);
	a->h.flags = 1;
	ak_val *v = ak_arr_vals(a);
	size_t start = 0;
	uint32_t k = 0;
	if (sep->len == 1) {
		const char *end = s->data + s->len;
		for (const char *q = s->data; k + 1 < parts;) {
			const char *at = ak_find_byte(q, end, sep->data[0]);
			if (!at) break;
			v[k++].p = ak_piece(&p, q, (size_t)(at - q));
			q = at + 1;
			start = (size_t)(q - s->data);
		}
	}
	while (k + 1 < parts) {
		int64_t at = ak_mem_find(s->data + start, s->len - start, sep->data, sep->len);
		if (at < 0) break;
		if (AK_UNLIKELY(k + 1 >= cap)) {
			a->len = k;
			ak_arr_reserve(&a, cap);
			cap = a->cap;
			v = ak_arr_vals(a);
		}
		v[k++].p = ak_piece(&p, s->data + start, (size_t)at);
		start += (size_t)at + sep->len;
	}
	v[k++].p = ak_piece(&p, s->data + start, s->len - start);
	ak_pieces_end(&p);
	a->len = k;
	return a;
}

ak_arr *ak_str_split(ak_str *s, const ak_str *sep) { return ak_str_split_n(s, sep, -1); }

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
	bool neg = p < end && *p == '-';
	if (p < end && (*p == '+' || *p == '-')) p++;
	if (p == end) return false;
	// Its magnitude, as an unsigned: one past int's largest is -int's
	// least.
	uint64_t u = 0;
	for (; p < end; p++) {
		unsigned d = (unsigned)(uint8_t)*p - '0';
		if (d > 9) return false;
		if (u > (UINT64_MAX - d) / 10) return false;
		u = u * 10 + d;
	}
	if (u > (uint64_t)INT64_MAX + neg) return false;
	*out = neg ? (int64_t)(0 - u) : (int64_t)u;
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
	// Its digits, at most 19 (m: exact in 64 bits), and its exponent.
	uint64_t m = 0;
	int nd = 0, e10 = 0;
	const char *c = p;
	for (; c < end && *c >= '0' && *c <= '9'; c++) {
		if (m || *c != '0') nd++;
		m = m * 10 + (uint64_t)(*c - '0');
		if (nd > 19) break;
	}
	if (nd <= 19 && c < end && *c == '.') {
		for (c++; c < end && *c >= '0' && *c <= '9'; c++) {
			if (m || *c != '0') nd++;
			m = m * 10 + (uint64_t)(*c - '0');
			e10--;
			if (nd > 19) break;
		}
	}
	if (nd <= 19 && c < end && (*c == 'e' || *c == 'E')) {
		c++;
		bool eneg = c < end && *c == '-';
		if (c < end && (*c == '+' || *c == '-')) c++;
		int x = 0;
		for (; c < end && x < 10000; c++) x = x * 10 + (*c - '0');
		if (c == end) e10 += eneg ? -x : x;
	}
	if (nd <= 19 && c == end) {
		if (m == 0) {
			*out = neg ? -0.0 : 0.0;
			return true;
		}
		// Clinger's fast path: a mantissa exact in a double times or over
		// a power of ten that is too (10^22 at most) is one correctly
		// rounded operation.
		if (m < (1ull << 53) && e10 >= -22 && e10 <= 22) {
			static const double pow10[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
			                               1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
			double v = (double)m;
			v = e10 < 0 ? v / pow10[-e10] : v * pow10[e10];
			*out = neg ? -v : v;
			return true;
		}
		if (eisel_lemire(m, e10, neg, out)) return true;
	}
	char *e;
	double v = strtod(s->data, &e);
	if (e != end) return false;
	*out = v;
	return true;
}

char *ak_strb_room(ak_strb *b, size_t n) {
	if (!b->b.data) {
		if (n <= sizeof b->small - b->n) return b->small + b->n;
		// Outgrown: what it holds moves to the heap.
		ak_buf_reserve(&b->b, b->n + n > 256 ? b->n + n : 256);
		memcpy(b->b.data, b->small, b->n);
		b->b.len = b->n;
		b->n = 0;
	}
	ak_buf_reserve(&b->b, n);
	return (char *)b->b.data + b->b.len;
}

ak_buf *ak_strb_buf(ak_strb *b) {
	if (!b->b.data) {
		// On the heap from now on, so what's added later is there too.
		ak_buf_reserve(&b->b, b->n ? b->n : 1);
		memcpy(b->b.data, b->small, b->n);
		b->b.len = b->n;
		b->n = 0;
	}
	return &b->b;
}

void ak_strb_cstr(ak_strb *b, const char *s, size_t n) {
	if (!n) return;
	memcpy(ak_strb_room(b, n), s, n);
	ak_strb_wrote(b, n);
}

void ak_strb_str(ak_strb *b, const ak_str *s) { ak_strb_cstr(b, s->data, s->len); }

void ak_strb_char(ak_strb *b, char c) {
	*ak_strb_room(b, 1) = c;
	ak_strb_wrote(b, 1);
}

void ak_strb_int(ak_strb *b, int64_t v) {
	char buf[24];
	char *p = int_digits(v, buf + sizeof buf);
	ak_strb_cstr(b, p, (size_t)(buf + sizeof buf - p));
}

void ak_strb_uint(ak_strb *b, uint64_t v) {
	char buf[24];
	int n = snprintf(buf, sizeof buf, "%llu", (unsigned long long)v);
	ak_strb_cstr(b, buf, (size_t)n);
}

void ak_strb_float(ak_strb *b, double v) {
	char *w = ak_strb_room(b, 32);
	ak_strb_wrote(b, (size_t)fmt_float(w, v));
}

void ak_strb_bool(ak_strb *b, bool v) {
	if (v) ak_strb_cstr(b, "true", 4);
	else ak_strb_cstr(b, "false", 5);
}

// A buffer's bytes as a string, the buffer taken: a big one's buffer
// becomes the string, its bytes moved up past a header in place (its
// room past them, never written, isn't resident), rather than copied
// into a string as big, which at the copy's end holds them twice.
#define AK_BUF_IN_PLACE ((size_t)512 << 10)

ak_str *ak_str_of_buf(ak_buf *b) {
	size_t n = b->len;
	if (n < AK_BUF_IN_PLACE || n >= UINT32_MAX) {
		ak_str *s = n ? ak_str_new((const char *)b->data, n) : ak_str_empty();
		ak_buf_free(b);
		return s;
	}
	char *d = ak_realloc(b->data, sizeof(ak_str) + n + 1);  // in place: it's no bigger than the buffer, or 17 bytes more
	memmove(d + sizeof(ak_str), d, n);
	ak_str *s = (ak_str *)(void *)d;
	ak_obj_count(1);
	s->h = (ak_obj){.rc = 1, .tag = AK_TAG_STR};
	s->len = (uint32_t)n;
	s->hash = 0;
	s->data[n] = 0;
	b->data = NULL;
	b->len = b->cap = 0;
	return s;
}

ak_str *ak_strb_finish(ak_strb *b) {
	if (b->b.data) {
		b->n = 0;
		return ak_str_of_buf(&b->b);
	}
	ak_str *s = b->n ? ak_str_new(b->small, b->n) : ak_str_empty();
	ak_strb_free(b);
	return s;
}

ak_str *ak_strb_finish_in(ak_strb *b, ak_strbuf *buf) {
	size_t n = ak_strb_len(b);
	if (n == 0 || n > AK_STRBUF_ROOM) return ak_strb_finish(b);
	ak_str *s = ak_strbuf_str(buf, n);
	memcpy(s->data, b->b.data ? (const char *)b->b.data : b->small, n);
	ak_strb_free(b);
	return s;
}

void ak_strb_free(ak_strb *b) {
	ak_buf_free(&b->b);
	b->n = 0;
}

ak_bytes *ak_bytes_new(const void *p, size_t n) {
	ak_bytes *o = ak_obj_new(AK_TAG_BYTES, sizeof(ak_bytes) + n);
	o->len = (uint32_t)n;
	if (n && p) memcpy(o->data, p, n);  // p NULL: zeros
	return o;
}

ak_bytes *ak_bytes_raw(size_t n) {
	ak_bytes *o = ak_obj_new_raw(AK_TAG_BYTES, sizeof(ak_bytes) + n);
	o->len = (uint32_t)n;
	return o;
}
