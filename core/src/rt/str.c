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

uint64_t ak_hash_bytes(const void *key, size_t len) {
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

bool ak_str_ascii(const ak_str *cs) {
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
static int64_t two_way(const uint8_t *h, size_t hn, const uint8_t *n, size_t l) {
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

ak_arr *ak_str_split_n(ak_str *s, const ak_str *sep, int64_t count) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 4);
	a->h.flags = 1;
	if (sep->len == 0) {
		// Split into characters.
		if (ak_str_ascii(s)) {
			ak_arr_mut(&a);
			for (uint32_t i = 0; i < s->len; i++) {
				ak_str *c = ak_str_new(s->data + i, 1);
				c->h.flags = AK_STR_KNOWN | AK_STR_ASCII;
				ak_arr_push(&a, (ak_val){.p = c});
			}
			return a;
		}
		uint32_t i = 0;
		while (i < s->len) {
			uint32_t j = i + 1;
			while (j < s->len && ((uint8_t)s->data[j] & 0xc0) == 0x80) j++;
			ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + i, j - i)});
			i = j;
		}
		return a;
	}
	if (count == 0) return a;
	size_t start = 0;
	while (count < 0 || a->len + 1 < count) {
		int64_t at = ak_mem_find(s->data + start, s->len - start, sep->data, sep->len);
		if (at < 0) break;
		ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + start, (size_t)at)});
		start += (size_t)at + sep->len;
	}
	ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + start, s->len - start)});
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

void ak_strb_float(ak_strb *b, double v) {
	char *w = ak_strb_room(b, 32);
	ak_strb_wrote(b, (size_t)fmt_float(w, v));
}

void ak_strb_bool(ak_strb *b, bool v) {
	if (v) ak_strb_cstr(b, "true", 4);
	else ak_strb_cstr(b, "false", 5);
}

ak_str *ak_strb_finish(ak_strb *b) {
	ak_str *s;
	if (b->b.data) s = b->b.len ? ak_str_new((const char *)b->b.data, b->b.len) : ak_str_empty();
	else s = b->n ? ak_str_new(b->small, b->n) : ak_str_empty();
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
	if (n && p) memcpy(o->data, p, n);  // p NULL: the caller fills them
	return o;
}
