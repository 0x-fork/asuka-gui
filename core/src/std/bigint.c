// bigint.c: std/bigint's builtins: integers of any size, the same on
// every platform (the web through the core's WebAssembly).
//
// A BigInt is an Asuka struct with one field, bytes: empty for 0, else
// a sign byte (0 or 1: negative) and the magnitude's bytes, least
// significant first, the last one not 0. One number has one encoding, so
// the struct's == and hashing are the number's. Each builtin reads its
// operands into limbs, computes, and writes a new value: values are
// immutable, as every Asuka value is.
//
// The magnitude is in limbs of 64 bits where the compiler has a 128-bit
// product (64-bit targets), else 32 (wasm32, armv7, i686, MSVC):
// AK_BIGINT_LIMB32 forces 32 (core/test's bigint32_test.c runs both).
// The algorithms are the usual ones (Knuth's TAOCP vol. 2, Go's
// math/big, GMP):
//
//   multiplication  schoolbook below KARA limbs, Karatsuba above
//                   (squaring by its own, with half the products)
//   division        Knuth's algorithm D below BZ limbs, Burnikel and
//                   Ziegler's recursive division above (MPI-I-98-1-022)
//   radix           powers of two by their bits; other radixes divide
//                   and conquer over a table of the big base's powers
//                   squared, both ways, so a million digits print in
//                   about a second, not minutes
//   modPow          Montgomery multiplication for odd moduli, sliding
//                   windows; plain reduction for even ones
//   primes          trial division, then Baillie-PSW (Miller-Rabin to
//                   base 2 and a strong Lucas test, Selfridge's
//                   parameters: no composite below 2^64 passes, and none
//                   is known that does), then more Miller-Rabin rounds
//   gcd             Lehmer's, as Go's (Collins' condition)
//
// Results past MAX_BITS (2^30 bits, as V8's BigInt) throw: a pow or a
// shift that would take gigabytes is a bug shown where it happens.
#include "std/std.h"

#include <math.h>

#if !defined(AK_BIGINT_LIMB32) && defined(__SIZEOF_INT128__) && UINTPTR_MAX == UINT64_MAX
typedef uint64_t limb;
__extension__ typedef unsigned __int128 dlimb;
#define LB 64
#else
typedef uint32_t limb;
typedef uint64_t dlimb;
#define LB 32
#endif
#define LBYTES (LB / 8)
#define LMAX ((limb)-1)

#define MAX_BITS ((uint64_t)1 << 30)
#define MAX_LIMBS ((size_t)(MAX_BITS / LB + 2))

// Thresholds, in limbs (measured: bigint_cases.h's bench).
#ifndef KARA
#define KARA 40
#endif
#ifndef KARA_SQR
#define KARA_SQR 64
#endif
#ifndef BZ
#define BZ 64
#endif
#define CONV_LEAF 16

#define BYTES(i) ((ak_bytes *)args[i].p)
#define STR(i) ((ak_str *)args[i].p)

// ---- Limbs: magnitudes as arrays, least significant first ----

static limb *lalloc(size_t n) { return ak_alloc((n ? n : 1) * sizeof(limb)); }

static size_t norm(const limb *d, size_t n) {
	while (n && !d[n - 1]) n--;
	return n;
}

static int clz(limb x) {
	if (!x) return LB;
#if defined(__GNUC__) || defined(__clang__)
#if LB == 64
	return __builtin_clzll(x);
#else
	return __builtin_clz(x);
#endif
#else
	int n = 0;
	while (!(x >> (LB - 1))) x <<= 1, n++;
	return n;
#endif
}

static int ctz(limb x) {
	if (!x) return LB;
#if defined(__GNUC__) || defined(__clang__)
#if LB == 64
	return __builtin_ctzll(x);
#else
	return __builtin_ctz(x);
#endif
#else
	int n = 0;
	while (!(x & 1)) x >>= 1, n++;
	return n;
#endif
}

static uint64_t bitlen(const limb *d, size_t n) {
	n = norm(d, n);
	return n ? (uint64_t)n * LB - (uint64_t)clz(d[n - 1]) : 0;
}

static int cmp_n(const limb *a, const limb *b, size_t n) {
	while (n--)
		if (a[n] != b[n]) return a[n] < b[n] ? -1 : 1;
	return 0;
}

// cmp_nat compares two normalized magnitudes.
static int cmp_nat(const limb *a, size_t an, const limb *b, size_t bn) {
	if (an != bn) return an < bn ? -1 : 1;
	return cmp_n(a, b, an);
}

static limb add_n(limb *z, const limb *x, const limb *y, size_t n) {
	limb c = 0;
	for (size_t i = 0; i < n; i++) {
		limb s = x[i] + c;
		c = s < c;
		s += y[i];
		c += s < y[i];
		z[i] = s;
	}
	return c;
}

static limb add_1(limb *z, const limb *x, size_t n, limb c) {
	size_t i = 0;
	for (; i < n && c; i++) {
		limb s = x[i] + c;
		c = s < c;
		z[i] = s;
	}
	if (z != x)
		for (; i < n; i++) z[i] = x[i];
	return c;
}

// add_nat is z = x + y, xn >= yn, z of xn limbs (it may be x); the carry.
// (A zero's limbs may be NULL: nothing is offset from them.)
static limb add_nat(limb *z, const limb *x, size_t xn, const limb *y, size_t yn) {
	limb c = add_n(z, x, y, yn);
	return xn > yn ? add_1(z + yn, x + yn, xn - yn, c) : c;
}

static limb sub_n(limb *z, const limb *x, const limb *y, size_t n) {
	limb b = 0;
	for (size_t i = 0; i < n; i++) {
		limb xi = x[i], yi = y[i];
		z[i] = xi - yi - b;
		b = (xi < yi) | ((xi == yi) & b);
	}
	return b;
}

static limb sub_1(limb *z, const limb *x, size_t n, limb b) {
	size_t i = 0;
	for (; i < n && b; i++) {
		limb xi = x[i];
		z[i] = xi - b;
		b = xi < b;
	}
	if (z != x)
		for (; i < n; i++) z[i] = x[i];
	return b;
}

// sub_nat is z = x - y, xn >= yn; the borrow.
static limb sub_nat(limb *z, const limb *x, size_t xn, const limb *y, size_t yn) {
	limb b = sub_n(z, x, y, yn);
	return xn > yn ? sub_1(z + yn, x + yn, xn - yn, b) : b;
}

static limb mul_1(limb *z, const limb *x, size_t n, limb m) {
	dlimb c = 0;
	for (size_t i = 0; i < n; i++) {
		c += (dlimb)x[i] * m;
		z[i] = (limb)c;
		c >>= LB;
	}
	return (limb)c;
}

static limb addmul_1(limb *z, const limb *x, size_t n, limb m) {
	dlimb c = 0;
	for (size_t i = 0; i < n; i++) {
		c += (dlimb)x[i] * m + z[i];
		z[i] = (limb)c;
		c >>= LB;
	}
	return (limb)c;
}

static limb submul_1(limb *z, const limb *x, size_t n, limb m) {
	limb borrow = 0;
	for (size_t i = 0; i < n; i++) {
		dlimb p = (dlimb)x[i] * m + borrow;
		limb lo = (limb)p, zi = z[i];
		borrow = (limb)(p >> LB) + (zi < lo);
		z[i] = zi - lo;
	}
	return borrow;
}

// shl_bits is z = x << s (s < LB), n limbs; the bits shifted out. z may
// be x.
static limb shl_bits(limb *z, const limb *x, size_t n, unsigned s) {
	if (!n) return 0;
	if (!s) {
		if (z != x) memmove(z, x, n * sizeof(limb));
		return 0;
	}
	limb out = x[n - 1] >> (LB - s);
	for (size_t i = n - 1; i > 0; i--) z[i] = x[i] << s | x[i - 1] >> (LB - s);
	z[0] = x[0] << s;
	return out;
}

// shr_bits is z = x >> s (s < LB), n limbs. z may be x.
static void shr_bits(limb *z, const limb *x, size_t n, unsigned s) {
	if (!n) return;
	if (!s) {
		if (z != x) memmove(z, x, n * sizeof(limb));
		return;
	}
	for (size_t i = 0; i + 1 < n; i++) z[i] = x[i] >> s | x[i + 1] << (LB - s);
	z[n - 1] = x[n - 1] >> s;
}

// ---- Multiplication ----

static void mul_basecase(limb *z, const limb *x, size_t xn, const limb *y, size_t yn) {
	z[xn] = mul_1(z, x, xn, y[0]);
	for (size_t j = 1; j < yn; j++) z[xn + j] = addmul_1(z + j, x, xn, y[j]);
}

// sqr_basecase is z = x², 2n limbs: the products below the diagonal
// once, doubled, and the diagonal's.
static void sqr_basecase(limb *z, const limb *x, size_t n) {
	memset(z, 0, 2 * n * sizeof(limb));
	for (size_t i = 0; i + 1 < n; i++) z[i + n] = addmul_1(z + 2 * i + 1, x + i + 1, n - i - 1, x[i]);
	shl_bits(z, z, 2 * n, 1);
	limb c = 0;
	for (size_t i = 0; i < n; i++) {
		dlimb p = (dlimb)x[i] * x[i];
		dlimb s = (dlimb)z[2 * i] + (limb)p + c;
		z[2 * i] = (limb)s;
		s = (s >> LB) + z[2 * i + 1] + (limb)(p >> LB);
		z[2 * i + 1] = (limb)s;
		c = (limb)(s >> LB);
	}
}

// diff is z = |a - b|, an >= bn, z of an limbs; whether a < b.
static bool diff(limb *z, const limb *a, size_t an, const limb *b, size_t bn) {
	bool less = false;
	if (norm(a + bn, an - bn) == 0) less = cmp_n(a, b, bn) < 0;
	if (!less) {
		sub_nat(z, a, an, b, bn);
		return false;
	}
	sub_n(z, b, a, bn);
	for (size_t i = bn; i < an; i++) z[i] = 0;
	return true;
}

static size_t kara_scratch(size_t n) {
	size_t s = 2;
	while (n >= KARA) {
		size_t h = n - n / 2;
		s += 4 * h + 1;
		n = h;
	}
	return s;
}

// kara is z = x·y, n limbs each, z of 2n; t is kara_scratch(n) limbs.
// With x = x1·B^l + x0 (and y alike), xy = z2·B^2l + (z2 + z0 - (x1 -
// x0)(y1 - y0))·B^l + z0: three products of half the size.
static void kara(limb *z, const limb *x, const limb *y, size_t n, limb *t) {
	if (n < KARA) {
		mul_basecase(z, x, n, y, n);
		return;
	}
	size_t l = n / 2, h = n - l;
	limb *d = t, *dx = t + 2 * h, *dy = dx + h, *rest = t + 4 * h + 1;
	bool nx = diff(dx, x + l, h, x, l);
	bool ny = diff(dy, y + l, h, y, l);
	kara(d, dx, dy, h, rest);
	kara(z, x, y, l, rest);
	kara(z + 2 * l, x + l, y + l, h, rest);
	// s = z0 + z2 ∓ d, 2h + 1 limbs, into dx and dy (done with).
	limb *s = dx;
	s[2 * h] = add_nat(s, z + 2 * l, 2 * h, z, 2 * l);
	if (nx == ny) s[2 * h] -= sub_n(s, s, d, 2 * h);
	else s[2 * h] += add_n(s, s, d, 2 * h);
	limb c = add_n(z + l, z + l, s, 2 * h + 1);
	add_1(z + l + 2 * h + 1, z + l + 2 * h + 1, 2 * n - l - 2 * h - 1, c);
}

static void kara_sqr(limb *z, const limb *x, size_t n, limb *t) {
	if (n < KARA_SQR) {
		sqr_basecase(z, x, n);
		return;
	}
	size_t l = n / 2, h = n - l;
	limb *d = t, *dx = t + 2 * h, *rest = t + 4 * h + 1;
	diff(dx, x + l, h, x, l);
	kara_sqr(d, dx, h, rest);
	kara_sqr(z, x, l, rest);
	kara_sqr(z + 2 * l, x + l, h, rest);
	limb *s = dx;
	s[2 * h] = add_nat(s, z + 2 * l, 2 * h, z, 2 * l);
	s[2 * h] -= sub_n(s, s, d, 2 * h);
	limb c = add_n(z + l, z + l, s, 2 * h + 1);
	add_1(z + l + 2 * h + 1, z + l + 2 * h + 1, 2 * n - l - 2 * h - 1, c);
}

// mul_nat is z = x·y, z of xn + yn limbs, apart from x and y; xn, yn >=
// 1. A longer x goes in pieces of y's size.
static void mul_nat(limb *z, const limb *x, size_t xn, const limb *y, size_t yn) {
	if (xn < yn) {
		const limb *t = x;
		x = y, y = t;
		size_t tn = xn;
		xn = yn, yn = tn;
	}
	if (x == y && xn == yn) {
		if (xn < KARA_SQR) {
			sqr_basecase(z, x, xn);
			return;
		}
		limb *t = lalloc(kara_scratch(xn));
		kara_sqr(z, x, xn, t);
		ak_free(t);
		return;
	}
	if (yn < KARA) {
		mul_basecase(z, x, xn, y, yn);
		return;
	}
	limb *t = lalloc(kara_scratch(yn) + 2 * yn);
	if (xn == yn) {
		kara(z, x, y, xn, t);
		ak_free(t);
		return;
	}
	limb *p = t + kara_scratch(yn);
	memset(z, 0, (xn + yn) * sizeof(limb));
	size_t i = 0;
	for (; i + yn <= xn; i += yn) {
		kara(p, x + i, y, yn, t);
		add_nat(z + i, z + i, xn + yn - i, p, 2 * yn);
	}
	ak_free(t);
	if (i < xn) {
		size_t r = xn - i;
		limb *q = lalloc(r + yn);
		mul_nat(q, y, yn, x + i, r);
		add_nat(z + i, z + i, xn + yn - i, q, r + yn);
		ak_free(q);
	}
}

// ---- Division ----

// divmod_1 is q = x / d (q may be x, or NULL); the remainder.
static limb divmod_1(limb *q, const limb *x, size_t n, limb d) {
	dlimb r = 0;
	for (size_t i = n; i-- > 0;) {
		r = r << LB | x[i];
		limb qi = (limb)(r / d);
		r -= (dlimb)qi * d;
		if (q) q[i] = qi;
	}
	return (limb)r;
}

// knuth is q = u / v and r = u % v by Knuth's algorithm D (TAOCP 4.3.1),
// un >= vn >= 2, v normalized (its top limb not 0); q has un - vn + 1
// limbs, r vn (either may be NULL).
static void knuth(limb *q, limb *r, const limb *u, size_t un, const limb *v, size_t vn) {
	int s = clz(v[vn - 1]);
	limb *vs = lalloc(vn), *us = lalloc(un + 1);
	shl_bits(vs, v, vn, (unsigned)s);
	us[un] = shl_bits(us, u, un, (unsigned)s);
	limb v1 = vs[vn - 1], v2 = vs[vn - 2];
	for (size_t j = un - vn + 1; j-- > 0;) {
		dlimb num = (dlimb)us[j + vn] << LB | us[j + vn - 1];
		dlimb qhat = num / v1, rhat = num - qhat * v1;
		while (qhat >> LB || qhat * v2 > (rhat << LB | us[j + vn - 2])) {
			qhat--;
			rhat += v1;
			if (rhat >> LB) break;
		}
		limb b = submul_1(us + j, vs, vn, (limb)qhat);
		limb top = us[j + vn];
		us[j + vn] = top - b;
		if (top < b) {
			qhat--;
			us[j + vn] += add_n(us + j, us + j, vs, vn);
		}
		if (q) q[j] = (limb)qhat;
	}
	if (r) shr_bits(r, us, vn, (unsigned)s);
	ak_free(vs);
	ak_free(us);
}

// school is q = a / b, r = a % b for a of an limbs and b of n, b's top
// bit set and a < b·B^(an - n): q has an - n limbs, r n.
static void school(limb *q, limb *r, const limb *a, size_t an, const limb *b, size_t n) {
	limb *qq = lalloc(an - n + 1);
	knuth(qq, r, a, an, b, n);
	memcpy(q, qq, (an - n) * sizeof(limb));
	ak_free(qq);
}

static void d3n2n(limb *q, limb *r, const limb *a, const limb *b, size_t h);

// d2n1n is Burnikel and Ziegler's D_2n/1n: a of 2n limbs over b of n,
// b's top bit set, a < b·B^n: q and r of n limbs each.
static void d2n1n(limb *q, limb *r, const limb *a, const limb *b, size_t n) {
	if (n % 2 || n < BZ) {
		school(q, r, a, 2 * n, b, n);
		return;
	}
	size_t h = n / 2;
	limb *t = lalloc(3 * h);
	d3n2n(q + h, t + h, a + h, b, h);
	memcpy(t, a, h * sizeof(limb));
	d3n2n(q, r, t, b, h);
	ak_free(t);
}

// d3n2n is D_3n/2n: a of 3h limbs over b of 2h (b1 its top h, b2 its
// low h), a < b·B^h: q of h limbs, r of 2h.
static void d3n2n(limb *q, limb *r, const limb *a, const limb *b, size_t h) {
	const limb *b1 = b + h, *b2 = b;
	limb *r1 = lalloc(h + 1), *d = lalloc(2 * h), *x = lalloc(2 * h + 1);
	if (cmp_n(a + 2 * h, b1, h) < 0) {
		d2n1n(q, r1, a + h, b1, h);
	} else {
		// a's top h limbs are b1's: q = B^h - 1, r1 = a's top 2h - q·b1.
		for (size_t i = 0; i < h; i++) q[i] = LMAX;
		r1[h] = add_n(r1, a + h, b1, h);
	}
	mul_nat(d, q, h, b2, h);
	memcpy(x, a, h * sizeof(limb));
	memcpy(x + h, r1, (h + 1) * sizeof(limb));
	if (x[2 * h] || cmp_n(x, d, 2 * h) >= 0) {
		sub_nat(x, x, 2 * h + 1, d, 2 * h);
		memcpy(r, x, 2 * h * sizeof(limb));
	} else {
		// Negative: q was too big (by 2 at most); b back until it isn't.
		sub_n(x, d, x, 2 * h);
		for (;;) {
			sub_1(q, q, h, 1);
			if (cmp_n(x, b, 2 * h) <= 0) {
				sub_n(r, b, x, 2 * h);
				break;
			}
			sub_n(x, x, b, 2 * h);
		}
	}
	ak_free(r1);
	ak_free(d);
	ak_free(x);
}

// divmod_bz is q = u / v and r = u % v for a v of BZ limbs or more: v
// shifted to n = j·2^k limbs, its top bit set, and u's blocks of n
// divided top down, two at a time (Burnikel and Ziegler's algorithm 3).
static void divmod_bz(limb *q, limb *r, const limb *u, size_t un, const limb *v, size_t vn) {
	size_t m = 1;
	while (m * BZ <= vn) m <<= 1;
	size_t j = (vn + m - 1) / m, n = j * m;
	uint64_t sigma = (uint64_t)(n - vn) * LB + (uint64_t)clz(v[vn - 1]);
	size_t ls = (size_t)(sigma / LB);
	unsigned bs = (unsigned)(sigma % LB);
	limb *b = lalloc(n);
	shl_bits(b + ls, v, vn, bs);
	size_t an = un + ls + 1;
	limb *a0 = lalloc(an);
	a0[un + ls] = shl_bits(a0 + ls, u, un, bs);
	an = norm(a0, an);
	size_t t = (an + n - 1) / n;
	if (t < 2) t = 2;
	if (an == t * n && a0[an - 1] >> (LB - 1)) t++;
	limb *a = lalloc(t * n);
	memcpy(a, a0, an * sizeof(limb));
	ak_free(a0);
	limb *qq = lalloc((t - 1) * n), *z = lalloc(2 * n), *rr = lalloc(n);
	memcpy(z, a + (t - 2) * n, 2 * n * sizeof(limb));
	for (size_t i = t - 1; i-- > 0;) {
		d2n1n(qq + i * n, rr, z, b, n);
		if (i > 0) {
			memcpy(z, a + (i - 1) * n, n * sizeof(limb));
			memcpy(z + n, rr, n * sizeof(limb));
		}
	}
	if (q) {
		size_t qn = un - vn + 1, have = (t - 1) * n;
		memset(q, 0, qn * sizeof(limb));
		memcpy(q, qq, (have < qn ? have : qn) * sizeof(limb));
	}
	if (r) {
		// rr >> sigma: vn limbs.
		shr_bits(rr + ls, rr + ls, n - ls, bs);
		memcpy(r, rr + ls, vn * sizeof(limb));
	}
	ak_free(a);
	ak_free(b);
	ak_free(qq);
	ak_free(z);
	ak_free(rr);
}

// divmod_nat is q = u / v (un - vn + 1 limbs) and r = u % v (vn limbs),
// u and v normalized, v not 0, un >= vn; either may be NULL.
static void divmod_nat(limb *q, limb *r, const limb *u, size_t un, const limb *v, size_t vn) {
	if (vn == 1) {
		limb rem = divmod_1(q, u, un, v[0]);
		if (r) r[0] = rem;
		return;
	}
	if (vn < BZ || un - vn < BZ) knuth(q, r, u, un, v, vn);
	else divmod_bz(q, r, u, un, v, vn);
}

// ---- Numbers: a magnitude and a sign ----

typedef struct {
	limb *d;  // owned (NULL when n is 0 and nothing was allocated)
	size_t n; // limbs, normalized
	bool neg; // never for 0
} num;

static void nfree(num *x) {
	ak_free(x->d);
	*x = (num){0};
}

static num nnew(size_t n) { return (num){.d = lalloc(n), .n = n}; }

static void nfix(num *x) {
	x->n = norm(x->d, x->n);
	if (!x->n) x->neg = false;
}

static num ncopy(const num *x) {
	num z = nnew(x->n);
	if (x->n) memcpy(z.d, x->d, x->n * sizeof(limb));
	z.neg = x->neg;
	return z;
}

static num nu64(uint64_t v) {
	num z = nnew(64 / LB);
	z.d[0] = (limb)v;
#if LB == 32
	z.d[1] = (limb)(v >> 32);
#endif
	nfix(&z);
	return z;
}

static num ni64(int64_t v) {
	num z = nu64(v < 0 ? 0 - (uint64_t)v : (uint64_t)v);
	z.neg = v < 0;
	return z;
}

static bool too_big(size_t limbs) {
	if (limbs > MAX_LIMBS) {
		ak_throw_msg("bigint: too large: more than 2^30 bits");
		return true;
	}
	return false;
}

static num load(const ak_bytes *b) {
	num x = {0};
	if (!b || b->len < 2) return x;
	size_t nb = b->len - 1, n = (nb + LBYTES - 1) / LBYTES;
	x.d = lalloc(n);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	memcpy(x.d, b->data + 1, nb);
#else
	for (size_t i = 0; i < nb; i++) x.d[i / LBYTES] |= (limb)b->data[1 + i] << (8 * (i % LBYTES));
#endif
	x.n = n;
	x.neg = b->data[0] != 0;
	nfix(&x);
	return x;
}

// encode writes x as a BigInt's bytes.
static ak_bytes *encode(const num *x) {
	size_t n = norm(x->d, x->n);
	if (!n) return ak_bytes_new(NULL, 0);
	size_t nb = n * LBYTES;
	while (!(uint8_t)(x->d[(nb - 1) / LBYTES] >> (8 * ((nb - 1) % LBYTES)))) nb--;
	ak_bytes *b = ak_bytes_new(NULL, nb + 1);
	b->data[0] = x->neg;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	memcpy(b->data + 1, x->d, nb);
#else
	for (size_t i = 0; i < nb; i++) b->data[1 + i] = (uint8_t)(x->d[i / LBYTES] >> (8 * (i % LBYTES)));
#endif
	return b;
}

// store is x as a BigInt's bytes, x freed; past MAX_BITS it throws.
static ak_val store(num *x) {
	nfix(x);
	if (bitlen(x->d, x->n) > MAX_BITS) {
		nfree(x);
		ak_throw_msg("bigint: too large: more than 2^30 bits");
		return (ak_val){0};
	}
	ak_bytes *b = encode(x);
	nfree(x);
	return (ak_val){.p = b};
}

static int ncmp_mag(const num *x, const num *y) { return cmp_nat(x->d, x->n, y->d, y->n); }

static int ncmp(const num *x, const num *y) {
	if (x->neg != y->neg) return x->neg ? -1 : 1;
	int c = ncmp_mag(x, y);
	return x->neg ? -c : c;
}

static num nadd_mag(const num *x, const num *y) {
	if (x->n < y->n) {
		const num *t = x;
		x = y, y = t;
	}
	num z = nnew(x->n + 1);
	z.d[x->n] = add_nat(z.d, x->d, x->n, y->d, y->n);
	z.n = x->n + 1;
	nfix(&z);
	return z;
}

// nsub_mag is |x| - |y|, signed.
static num nsub_mag(const num *x, const num *y) {
	int c = ncmp_mag(x, y);
	if (c == 0) return (num){0};
	if (c < 0) {
		num z = nsub_mag(y, x);
		z.neg = true;
		return z;
	}
	num z = nnew(x->n);
	sub_nat(z.d, x->d, x->n, y->d, y->n);
	z.n = x->n;
	nfix(&z);
	return z;
}

static num nadd(const num *x, const num *y) {
	num z;
	if (x->neg == y->neg) {
		z = nadd_mag(x, y);
		z.neg = x->neg;
	} else {
		z = nsub_mag(x, y);
		if (x->neg) z.neg = !z.neg;
	}
	nfix(&z);
	return z;
}

static num nneg(const num *x) {
	num t = *x;
	t.neg = t.n && !t.neg;
	return t;  // a view: not to be freed
}

static num nsub(const num *x, const num *y) {
	num ny = nneg(y);
	return nadd(x, &ny);
}

static num nmul(const num *x, const num *y) {
	if (!x->n || !y->n) return (num){0};
	if (too_big(x->n + y->n)) return (num){0};
	num z = nnew(x->n + y->n);
	mul_nat(z.d, x->d, x->n, y->d, y->n);
	z.n = x->n + y->n;
	z.neg = x->neg != y->neg;
	nfix(&z);
	return z;
}

// ndivmod is the quotient toward zero and its remainder (the dividend's
// sign); y not 0. q or r may be NULL.
static void ndivmod(const num *x, const num *y, num *q, num *r) {
	if (ncmp_mag(x, y) < 0) {
		if (q) *q = (num){0};
		if (r) *r = ncopy(x);
		return;
	}
	num qq = q ? nnew(x->n - y->n + 1) : (num){0};
	num rr = r ? nnew(y->n) : (num){0};
	divmod_nat(q ? qq.d : NULL, r ? rr.d : NULL, x->d, x->n, y->d, y->n);
	if (q) {
		qq.n = x->n - y->n + 1;
		qq.neg = x->neg != y->neg;
		nfix(&qq);
		*q = qq;
	}
	if (r) {
		rr.n = y->n;
		rr.neg = x->neg;
		nfix(&rr);
		*r = rr;
	}
}

// nmod is x modulo m (m > 0), in [0, m).
static num nmod(const num *x, const num *m) {
	num r;
	ndivmod(x, m, NULL, &r);
	if (r.neg) {
		num t = nadd(&r, m);
		nfree(&r);
		return t;
	}
	return r;
}

static num nshl(const num *x, uint64_t s) {
	if (!x->n) return (num){0};
	size_t ls = (size_t)(s / LB);
	if (s / LB > MAX_LIMBS) {
		too_big(MAX_LIMBS + 1);
		return (num){0};
	}
	if (too_big(x->n + ls + 1)) return (num){0};
	num z = nnew(x->n + ls + 1);
	z.d[x->n + ls] = shl_bits(z.d + ls, x->d, x->n, (unsigned)(s % LB));
	z.n = x->n + ls + 1;
	z.neg = x->neg;
	nfix(&z);
	return z;
}

// nshr_mag is |x| >> s.
static num nshr_mag(const num *x, uint64_t s) {
	if (s / LB >= x->n) return (num){0};
	size_t ls = (size_t)(s / LB);
	num z = nnew(x->n - ls);
	shr_bits(z.d, x->d + ls, x->n - ls, (unsigned)(s % LB));
	z.n = x->n - ls;
	nfix(&z);
	return z;
}

static bool nis_one(const num *x) { return x->n == 1 && x->d[0] == 1 && !x->neg; }

static bool nfits_u64(const num *x) { return x->n * LB <= 64; }

static uint64_t nto_u64(const num *x) {
	uint64_t v = 0;
	for (size_t i = x->n; i-- > 0;) v = (LB == 64 ? 0 : v << (LB % 64)) | x->d[i];
	return v;
}

static uint64_t gcd_u64(uint64_t a, uint64_t b) {
	while (b) {
		uint64_t t = a % b;
		a = b, b = t;
	}
	return a;
}

// nmul_limb is |x|·w, negative when neg.
static num nmul_limb(const num *x, limb w, bool neg) {
	num z = nnew(x->n + 1);
	z.d[x->n] = mul_1(z.d, x->d, x->n, w);
	z.n = x->n + 1;
	z.neg = neg;
	nfix(&z);
	return z;
}

// ngcd is gcd(|x|, |y|) by Lehmer's algorithm, as Go's lehmerGCD: the
// quotients the top limbs of a and b give, by Collins' condition, are
// the true ones, so a cosequence of them is applied to the whole
// numbers at once, a limb's worth of steps for four products by a limb;
// a plain Euclid's step where they give none, machine words once both
// fit one.
static num ngcd(const num *x, const num *y) {
	num a = ncopy(x), b = ncopy(y);
	a.neg = b.neg = false;
	if (ncmp_mag(&a, &b) < 0) {
		num t = a;
		a = b, b = t;
	}
	while (b.n > 1) {
		size_t n = a.n, m = b.n;
		int h = clz(a.d[n - 1]);
		limb a1 = h ? a.d[n - 1] << h | a.d[n - 2] >> (LB - h) : a.d[n - 1], a2 = 0;
		if (n == m) a2 = h ? b.d[n - 1] << h | b.d[n - 2] >> (LB - h) : b.d[n - 1];
		else if (n == m + 1) a2 = h ? b.d[n - 2] >> (LB - h) : 0;
		// The cosequences' signs alternate: even, u0 and v1 are >= 0 and
		// u1 and v0 <= 0; odd, the other way.
		bool even = false;
		limb u0 = 0, u1 = 1, u2 = 0, v0 = 0, v1 = 0, v2 = 1;
		while (a2 >= v2 && a1 - a2 >= v1 + v2) {
			limb q = a1 / a2, r = a1 % a2;
			a1 = a2, a2 = r;
			limb t = u1 + q * u2;
			u0 = u1, u1 = u2, u2 = t;
			t = v1 + q * v2;
			v0 = v1, v1 = v2, v2 = t;
			even = !even;
		}
		if (v0) {
			// a, b = u0·a + v0·b, u1·a + v1·b
			num t = nmul_limb(&a, u0, !even), s = nmul_limb(&b, v0, even);
			num r = nmul_limb(&a, u1, even), q = nmul_limb(&b, v1, !even);
			nfree(&a);
			nfree(&b);
			a = nadd(&t, &s);
			b = nadd(&r, &q);
			nfree(&t);
			nfree(&s);
			nfree(&r);
			nfree(&q);
		} else {
			num r;
			ndivmod(&a, &b, NULL, &r);
			nfree(&a);
			a = b;
			b = r;
		}
	}
	while (b.n) {
		if (nfits_u64(&a)) {
			uint64_t g = gcd_u64(nto_u64(&a), nto_u64(&b));
			nfree(&a);
			nfree(&b);
			return nu64(g);
		}
		num r;
		ndivmod(&a, &b, NULL, &r);
		nfree(&a);
		a = b;
		b = r;
	}
	nfree(&b);
	return a;
}

// ninverse is x^-1 modulo m (m > 1), in [0, m); false when gcd(x, m)
// isn't 1. The extended Euclid's algorithm, keeping x's cofactor only.
static bool ninverse(const num *x, const num *m, num *out) {
	num a = nmod(x, m), b = ncopy(m);
	num ua = nu64(1), ub = (num){0};
	// a·? ≡ ua·x, b ≡ ub·x (mod m)
	while (a.n) {
		num q, r;
		ndivmod(&b, &a, &q, &r);
		num qu = nmul(&q, &ua);
		num nu = nsub(&ub, &qu);
		nfree(&qu);
		nfree(&q);
		nfree(&b);
		nfree(&ub);
		b = a, ub = ua;
		a = r, ua = nu;
	}
	nfree(&a);
	nfree(&ua);
	bool ok = nis_one(&b);
	nfree(&b);
	if (!ok) {
		nfree(&ub);
		return false;
	}
	*out = nmod(&ub, m);
	nfree(&ub);
	return true;
}

// ---- Modular arithmetic: Montgomery's for odd moduli ----

typedef struct {
	const limb *m;
	size_t n;
	bool mont;   // m odd: Montgomery form; else plain residues
	limb minv;   // -m^-1 mod B
	limb *r2;    // B^2n mod m (Montgomery)
	limb *one;   // 1 in the form
	limb *t;     // scratch: 2n + 1 limbs
} modctx;

static void redc(const modctx *M, limb *z, limb *t) {
	size_t n = M->n;
	limb carry = 0;
	for (size_t i = 0; i < n; i++) {
		limb u = t[i] * M->minv;
		limb c = addmul_1(t + i, M->m, n, u);
		limb s = t[i + n] + carry;
		limb c1 = s < carry;
		s += c;
		c1 += s < c;
		t[i + n] = s;
		carry = c1;
	}
	if (carry || cmp_n(t + n, M->m, n) >= 0) sub_n(z, t + n, M->m, n);
	else memcpy(z, t + n, n * sizeof(limb));
}

// mm_mul is z = a·b in M's form (z may be a or b); n limbs each.
static void mm_mul(const modctx *M, limb *z, const limb *a, const limb *b) {
	size_t n = M->n;
	limb *t = M->t;
	mul_nat(t, a, n, b, n);
	if (M->mont) {
		t[2 * n] = 0;
		redc(M, z, t);
		return;
	}
	size_t tn = norm(t, 2 * n);
	if (tn < n) {
		memset(z, 0, n * sizeof(limb));
		memcpy(z, t, tn * sizeof(limb));
		return;
	}
	limb *r = lalloc(n);
	divmod_nat(NULL, r, t, tn, M->m, n);
	memcpy(z, r, n * sizeof(limb));
	ak_free(r);
}

// mm_open sets M up for the modulus m (normalized, more than 1).
static void mm_open(modctx *M, const limb *m, size_t n) {
	*M = (modctx){.m = m, .n = n, .mont = m[0] & 1};
	M->t = lalloc(2 * n + 1);
	M->one = lalloc(n);
	if (M->mont) {
		limb x = m[0];
		for (int i = 0; i < 6; i++) x *= 2 - m[0] * x;
		M->minv = (limb)0 - x;
		limb *p = lalloc(2 * n + 1);
		p[2 * n] = 1;
		M->r2 = lalloc(n);
		divmod_nat(NULL, M->r2, p, 2 * n + 1, m, n);
		ak_free(p);
		// 1·R = REDC(R²).
		memset(M->t, 0, (2 * n + 1) * sizeof(limb));
		memcpy(M->t, M->r2, n * sizeof(limb));
		redc(M, M->one, M->t);
	} else {
		M->one[0] = 1;
	}
}

static void mm_close(modctx *M) {
	ak_free(M->t);
	ak_free(M->one);
	ak_free(M->r2);
}

// mm_in puts x (in [0, m), n limbs) in M's form.
static void mm_in(const modctx *M, limb *z, const limb *x) {
	if (M->mont) mm_mul(M, z, x, M->r2);
	else if (z != x) memcpy(z, x, M->n * sizeof(limb));
}

static void mm_out(const modctx *M, limb *z, const limb *x) {
	if (!M->mont) {
		if (z != x) memcpy(z, x, M->n * sizeof(limb));
		return;
	}
	memset(M->t, 0, (2 * M->n + 1) * sizeof(limb));
	memcpy(M->t, x, M->n * sizeof(limb));
	redc(M, z, M->t);
}

static bool bit_at(const limb *e, size_t en, uint64_t i) {
	return i / LB < en && (e[i / LB] >> (i % LB) & 1);
}

// mm_pow is z = g^e in M's form (g in it too), by sliding windows over
// e's bits.
static void mm_pow(const modctx *M, limb *z, const limb *g, const limb *e, size_t en) {
	size_t n = M->n;
	uint64_t bits = bitlen(e, en);
	if (!bits) {
		memcpy(z, M->one, n * sizeof(limb));
		return;
	}
	int w = bits > 671 ? 6 : bits > 239 ? 5 : bits > 79 ? 4 : bits > 23 ? 3 : 1;
	size_t tn = (size_t)1 << (w - 1);
	limb *tab = lalloc(tn * n);
	memcpy(tab, g, n * sizeof(limb));
	if (tn > 1) {
		limb *g2 = lalloc(n);
		mm_mul(M, g2, g, g);
		for (size_t i = 1; i < tn; i++) mm_mul(M, tab + i * n, tab + (i - 1) * n, g2);
		ak_free(g2);
	}
	bool started = false;
	int64_t i = (int64_t)bits - 1;
	while (i >= 0) {
		if (!bit_at(e, en, (uint64_t)i)) {
			mm_mul(M, z, z, z);
			i--;
			continue;
		}
		int64_t j = i - w + 1;
		if (j < 0) j = 0;
		while (!bit_at(e, en, (uint64_t)j)) j++;
		size_t v = 0;
		for (int64_t k = i; k >= j; k--) v = v << 1 | bit_at(e, en, (uint64_t)k);
		if (started) {
			for (int64_t k = i; k >= j; k--) mm_mul(M, z, z, z);
			mm_mul(M, z, z, tab + (v - 1) / 2 * n);
		} else {
			memcpy(z, tab + (v - 1) / 2 * n, n * sizeof(limb));
			started = true;
		}
		i = j - 1;
	}
	ak_free(tab);
}

// nmodpow is x^e mod m, e >= 0, m > 1: in [0, m).
static num nmodpow(const num *x, const num *e, const num *m) {
	modctx M;
	mm_open(&M, m->d, m->n);
	num b = nmod(x, m);
	limb *g = lalloc(m->n);
	if (b.n) memcpy(g, b.d, b.n * sizeof(limb));
	nfree(&b);
	mm_in(&M, g, g);
	num z = nnew(m->n);
	mm_pow(&M, z.d, g, e->d, e->n);
	mm_out(&M, z.d, z.d);
	z.n = m->n;
	nfix(&z);
	ak_free(g);
	mm_close(&M);
	return z;
}

// ---- Primes ----

static const uint16_t small_primes[] = {
	2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97,
	101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193,
	197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307,
	311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421,
	431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547,
	557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647, 653, 659,
	661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797,
	809, 811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929,
	937, 941, 947, 953, 967, 971, 977, 983, 991, 997,
};
#define NSMALL (sizeof small_primes / sizeof small_primes[0])

// jacobi is the Jacobi symbol (a/n), n odd and positive.
static int jacobi_u64(uint64_t a, uint64_t n) {
	int t = 1;
	a %= n;
	while (a) {
		while (!(a & 1)) {
			a >>= 1;
			uint64_t r = n & 7;
			if (r == 3 || r == 5) t = -t;
		}
		uint64_t s = a;
		a = n, n = s;
		if ((a & 3) == 3 && (n & 3) == 3) t = -t;
		a %= n;
	}
	return n == 1 ? t : 0;
}

// jacobi_small is (d/n) for a small d of either sign and an odd n > |d|.
static int jacobi_small(int64_t d, const limb *n, size_t nn) {
	int t = 1;
	uint64_t a = d < 0 ? (uint64_t)-d : (uint64_t)d;
	limb n8 = n[0] & 7;
	if (d < 0 && (n[0] & 3) == 3) t = -t;  // (-1/n)
	while (a && !(a & 1)) {
		a >>= 1;
		if (n8 == 3 || n8 == 5) t = -t;  // (2/n)
	}
	if (a == 1) return t;
	if (!a) return 0;
	// (a/n) = (n/a)·(-1)^((a-1)/2·(n-1)/2), a odd.
	if ((a & 3) == 3 && (n[0] & 3) == 3) t = -t;
	uint64_t nm = divmod_1(NULL, n, nn, (limb)a);
	return t * jacobi_u64(nm, a);
}

// mm_small is s modulo m (s signed and small) in M's form.
static void mm_small(const modctx *M, limb *z, int64_t s) {
	memset(z, 0, M->n * sizeof(limb));
	uint64_t a = s < 0 ? (uint64_t)-s : (uint64_t)s;
	z[0] = (limb)a;
#if LB == 32
	if (M->n > 1) z[1] = (limb)(a >> 32);
#endif
	if (s < 0) sub_n(z, M->m, z, M->n);
	mm_in(M, z, z);
}

static void mm_add(const modctx *M, limb *z, const limb *a, const limb *b) {
	limb c = add_n(z, a, b, M->n);
	if (c || cmp_n(z, M->m, M->n) >= 0) sub_n(z, z, M->m, M->n);
}

static void mm_sub(const modctx *M, limb *z, const limb *a, const limb *b) {
	if (sub_n(z, a, b, M->n)) add_n(z, z, M->m, M->n);
}

// mm_half is z / 2 modulo m (odd): z, or z + m, shifted.
static void mm_half(const modctx *M, limb *z) {
	limb c = 0;
	if (z[0] & 1) c = add_n(z, z, M->m, M->n);
	shr_bits(z, z, M->n, 1);
	z[M->n - 1] |= c << (LB - 1);
}

static bool is_zero_n(const limb *z, size_t n) { return norm(z, n) == 0; }

// miller_rabin is the strong probable prime test of n (odd, > 3) to base
// g (in [2, n-2], M's form), with n - 1 = d·2^s.
static bool miller_rabin(const modctx *M, const limb *g, const limb *d, size_t dn, uint64_t s, const limb *minus1) {
	size_t n = M->n;
	limb *x = lalloc(n);
	mm_pow(M, x, g, d, dn);
	bool ok = cmp_n(x, M->one, n) == 0 || cmp_n(x, minus1, n) == 0;
	for (uint64_t i = 1; i < s && !ok; i++) {
		mm_mul(M, x, x, x);
		if (cmp_n(x, M->one, n) == 0) break;
		ok = cmp_n(x, minus1, n) == 0;
	}
	ak_free(x);
	return ok;
}

static bool is_square(const num *x);

// lucas is the strong Lucas probable prime test of n (odd, not a
// square), with Selfridge's parameters: D the first of 5, -7, 9, -11,
// ... with (D/n) = -1, P = 1, Q = (1 - D)/4 (FIPS 186-5, B.3.3).
static bool lucas(const modctx *M, const num *nv) {
	size_t n = M->n;
	int64_t D = 5;
	for (;;) {
		int j = jacobi_small(D, nv->d, nv->n);
		if (j == -1) break;
		if (j == 0) {
			// D shares a factor with n: composite, unless n is |D|.
			uint64_t a = D < 0 ? (uint64_t)-D : (uint64_t)D;
			return nfits_u64(nv) && nto_u64(nv) == a;
		}
		D = D > 0 ? -(D + 2) : -D + 2;
		if (D == 13 && is_square(nv)) return false;
	}
	int64_t Q = (1 - D) / 4;
	// n + 1 = d·2^s
	num one = nu64(1);
	num k = nadd(nv, &one);
	nfree(&one);
	uint64_t s = 0;
	while (!bit_at(k.d, k.n, s)) s++;
	num d = nshr_mag(&k, s);
	nfree(&k);
	limb *U = lalloc(n), *V = lalloc(n), *Qk = lalloc(n), *Qm = lalloc(n), *Dm = lalloc(n), *t = lalloc(n);
	mm_small(M, Qm, Q);
	mm_small(M, Dm, D);
	memcpy(U, M->one, n * sizeof(limb));  // U_1 = 1
	memcpy(V, M->one, n * sizeof(limb));  // V_1 = P = 1
	memcpy(Qk, Qm, n * sizeof(limb));     // Q^1
	for (int64_t i = (int64_t)bitlen(d.d, d.n) - 2; i >= 0; i--) {
		// k to 2k: U = U·V, V = V² - 2Q^k, Q^2k.
		mm_mul(M, U, U, V);
		mm_mul(M, V, V, V);
		mm_add(M, t, Qk, Qk);
		mm_sub(M, V, V, t);
		mm_mul(M, Qk, Qk, Qk);
		if (bit_at(d.d, d.n, (uint64_t)i)) {
			// k to k + 1: U = (P·U + V)/2, V = (D·U + P·V)/2.
			mm_mul(M, t, Dm, U);
			mm_add(M, U, U, V);
			mm_half(M, U);
			mm_add(M, V, t, V);
			mm_half(M, V);
			mm_mul(M, Qk, Qk, Qm);
		}
	}
	bool ok = is_zero_n(U, n) || is_zero_n(V, n);
	for (uint64_t r = 1; r < s && !ok; r++) {
		mm_mul(M, V, V, V);
		mm_add(M, t, Qk, Qk);
		mm_sub(M, V, V, t);
		mm_mul(M, Qk, Qk, Qk);
		ok = is_zero_n(V, n);
	}
	nfree(&d);
	ak_free(U);
	ak_free(V);
	ak_free(Qk);
	ak_free(Qm);
	ak_free(Dm);
	ak_free(t);
	return ok;
}

static uint64_t splitmix(uint64_t *x) {
	uint64_t z = (*x += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}

// nprime reports whether x is probably prime: trial division, BPSW,
// then rounds of Miller-Rabin to bases drawn from a generator seeded by
// x (so an answer is the same each time it's asked).
static bool nprime(const num *x, int64_t rounds) {
	if (x->neg || !x->n) return false;
	if (nfits_u64(x) && nto_u64(x) < 1000) {
		uint64_t v = nto_u64(x);
		for (size_t i = 0; i < NSMALL; i++)
			if (small_primes[i] == v) return true;
		return false;
	}
	for (size_t i = 0; i < NSMALL; i++)
		if (divmod_1(NULL, x->d, x->n, small_primes[i]) == 0) return false;
	if (nfits_u64(x) && nto_u64(x) < 1000000) return true;  // no factor below its root
	size_t n = x->n;
	modctx M;
	mm_open(&M, x->d, n);
	// n - 1 = d·2^s
	num one = nu64(1);
	num nm1 = nsub(x, &one);
	nfree(&one);
	uint64_t s = 0;
	while (!bit_at(nm1.d, nm1.n, s)) s++;
	num d = nshr_mag(&nm1, s);
	limb *minus1 = lalloc(n), *g = lalloc(n);
	memcpy(minus1, nm1.d, nm1.n * sizeof(limb));
	mm_in(&M, minus1, minus1);
	mm_small(&M, g, 2);
	bool ok = miller_rabin(&M, g, d.d, d.n, s, minus1) && lucas(&M, x);
	uint64_t seed = x->n;
	for (size_t i = 0; i < x->n; i++) seed = seed * 0x100000001b3ull ^ (uint64_t)x->d[i];
	// Bases in [2, n - 2]: random limbs modulo n - 3, plus 2.
	num three = nu64(3);
	num span = nsub(x, &three);
	nfree(&three);
	for (int64_t r = 0; r < rounds && ok; r++) {
		num b = nnew(n);
		for (size_t i = 0; i < n; i++) b.d[i] = (limb)splitmix(&seed);
		nfix(&b);
		num m = nmod(&b, &span);
		nfree(&b);
		memset(g, 0, n * sizeof(limb));
		if (m.n) memcpy(g, m.d, m.n * sizeof(limb));
		nfree(&m);
		add_1(g, g, n, 2);
		mm_in(&M, g, g);
		ok = miller_rabin(&M, g, d.d, d.n, s, minus1);
	}
	nfree(&span);
	nfree(&nm1);
	nfree(&d);
	ak_free(minus1);
	ak_free(g);
	mm_close(&M);
	return ok;
}

// ---- Square roots ----

// nsqrt is ⌊√x⌋, x >= 0: Newton's iteration from above, its first guess
// from the top 64 bits' root.
static num nsqrt(const num *x) {
	if (!x->n) return (num){0};
	uint64_t b = bitlen(x->d, x->n);
	uint64_t s = b > 64 ? (b - 64 + 1) & ~(uint64_t)1 : 0;
	num top = nshr_mag(x, s);
	double r = floor(sqrt((double)nto_u64(&top))) + 2;
	nfree(&top);
	num g = nu64((uint64_t)r);
	num y = nshl(&g, s / 2);
	nfree(&g);
	for (;;) {
		num q;
		ndivmod(x, &y, &q, NULL);
		num t = nadd(&y, &q);
		nfree(&q);
		num z = nshr_mag(&t, 1);
		nfree(&t);
		if (ncmp(&z, &y) >= 0) {
			nfree(&z);
			return y;
		}
		nfree(&y);
		y = z;
	}
}

static bool is_square(const num *x) {
	num r = nsqrt(x);
	num sq = nmul(&r, &r);
	bool ok = ncmp(&sq, x) == 0;
	nfree(&r);
	nfree(&sq);
	return ok;
}

// ---- Text ----

static int digit_of(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'z') return c - 'a' + 10;
	if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
	return 99;
}

static int log2_exact(int r) {
	for (int k = 1; k <= 5; k++)
		if (r == 1 << k) return k;
	return 0;
}

// A radix's big base: the most digits a limb holds (k) and radix^k.
typedef struct {
	int radix, k;
	limb bb;
	// The table: pw[i] = bb^(CONV_LEAF·2^i), its digits CONV_LEAF·k·2^i.
	num pw[48];
	int npw;
} conv;

static void conv_open(conv *c, int radix) {
	*c = (conv){.radix = radix};
	limb bb = (limb)radix;
	int k = 1;
	while (bb <= LMAX / (limb)radix) bb *= (limb)radix, k++;
	c->bb = bb;
	c->k = k;
}

static void conv_close(conv *c) {
	for (int i = 0; i < c->npw; i++) nfree(&c->pw[i]);
}

static const num *conv_pow(conv *c, int i) {
	while (c->npw <= i) {
		if (c->npw == 0) {
			num p = nu64(1);
			for (int j = 0; j < CONV_LEAF; j++) {
				num t = nnew(p.n + 1);
				t.d[p.n] = mul_1(t.d, p.d, p.n, c->bb);
				t.n = p.n + 1;
				nfix(&t);
				nfree(&p);
				p = t;
			}
			c->pw[0] = p;
		} else {
			c->pw[c->npw] = nmul(&c->pw[c->npw - 1], &c->pw[c->npw - 1]);
		}
		c->npw++;
	}
	return &c->pw[i];
}

static size_t conv_digits(const conv *c, int i) { return (size_t)CONV_LEAF * (size_t)c->k << i; }

static const char digit_chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

// conv_leaf writes x's digits right to left, ending before end: width of
// them (zeros before) when pad, else as many as it has; their number.
static size_t conv_leaf(conv *c, const limb *x, size_t xn, char *end, size_t width, bool pad) {
	limb *t = lalloc(xn);
	memcpy(t, x, xn * sizeof(limb));
	size_t n = norm(t, xn), w = 0;
	while (n) {
		limb rem = divmod_1(t, t, n, c->bb);
		n = norm(t, n);
		for (int i = 0; i < c->k && (n || rem || pad); i++) {
			*--end = digit_chars[rem % (limb)c->radix];
			rem /= (limb)c->radix;
			w++;
		}
	}
	ak_free(t);
	if (pad)
		for (; w < width; w++) *--end = '0';
	return w;
}

// conv_write is conv_leaf for any size: x split by the table's largest
// power under it, the low part padded to that power's digits.
static size_t conv_write(conv *c, const limb *x, size_t xn, char *end, size_t width, bool pad, int level) {
	xn = norm(x, xn);
	int i = level;
	while (i >= 0 && cmp_nat(x, xn, c->pw[i].d, c->pw[i].n) < 0) i--;
	if (i < 0 || xn <= CONV_LEAF) return conv_leaf(c, x, xn, end, width, pad);
	const num *p = &c->pw[i];
	limb *q = lalloc(xn - p->n + 1), *r = lalloc(p->n);
	divmod_nat(q, r, x, xn, p->d, p->n);
	size_t lo = conv_digits(c, i);
	conv_write(c, r, p->n, end, lo, true, i - 1);
	ak_free(r);
	size_t hi = conv_write(c, q, xn - p->n + 1, end - lo, pad ? width - lo : 0, pad, i - 1);
	ak_free(q);
	return lo + hi;
}

// text writes x in radix (2 to 36) into b: a sign, digits, lowercase.
static void text(ak_strb *sb, const num *x, int radix) {
	if (!x->n) {
		ak_strb_cstr(sb, "0", 1);
		return;
	}
	uint64_t bits = bitlen(x->d, x->n);
	int lg = log2_exact(radix);
	size_t max = lg ? (size_t)((bits + (uint64_t)lg - 1) / (uint64_t)lg) : (size_t)ceil((double)bits / log2((double)radix)) + 2;
	char *buf = ak_alloc(max + 1);
	char *end = buf + max + 1;
	size_t w = 0;
	if (lg) {
		for (uint64_t at = 0; at < bits; at += (uint64_t)lg) {
			unsigned v = 0;
			for (int j = lg - 1; j >= 0; j--) v = v << 1 | bit_at(x->d, x->n, at + (uint64_t)j);
			*--end = digit_chars[v];
			w++;
		}
	} else {
		conv c;
		conv_open(&c, radix);
		if (x->n <= CONV_LEAF) {
			w = conv_leaf(&c, x->d, x->n, end, 0, false);
		} else {
			// The table up to a power whose square is past x: each part
			// a level splits is under the power the level below squares.
			int level = 0;
			while (2 * conv_pow(&c, level)->n - 2 < x->n) level++;
			w = conv_write(&c, x->d, x->n, end, 0, false, level);
		}
		conv_close(&c);
	}
	if (x->neg) ak_strb_cstr(sb, "-", 1);
	ak_strb_cstr(sb, buf + max + 1 - w, w);
	ak_free(buf);
}

// conv_read is the value of n digits (their values, most significant
// first) in c's radix: a leaf's digits multiplied in a limb at a time;
// more, the high part times the table's power plus the low part.
static num conv_read(conv *c, const uint8_t *dg, size_t n) {
	if (n <= conv_digits(c, 0) * 2) {
		num x = nnew(n / (size_t)c->k + 2);
		size_t xn = 0, i = 0;
		while (i < n) {
			size_t take = (n - i) % (size_t)c->k;
			if (!take) take = (size_t)c->k;
			limb v = 0, scale = 1;
			for (size_t j = 0; j < take; j++) v = v * (limb)c->radix + dg[i + j], scale *= (limb)c->radix;
			i += take;
			limb carry = mul_1(x.d, x.d, xn, scale);
			if (carry) x.d[xn++] = carry;
			if (!xn) {
				if (v) x.d[xn++] = v;
			} else if (add_1(x.d, x.d, xn, v)) {
				x.d[xn++] = 1;
			}
		}
		x.n = xn;
		nfix(&x);
		return x;
	}
	int i = 0;
	while (conv_digits(c, i + 1) < n) i++;
	size_t lo = conv_digits(c, i);
	num h = conv_read(c, dg, n - lo), l = conv_read(c, dg + n - lo, lo);
	num p = nmul(&h, conv_pow(c, i));
	num x = nadd(&p, &l);
	nfree(&h);
	nfree(&l);
	nfree(&p);
	return x;
}

// parse reads [sign][prefix]digits, an underscore only before a digit
// and after a digit or the prefix; radix 0: as a literal says
// (0x, 0o, 0b, else 10). false when the text isn't such a number.
static bool parse(const char *s, size_t n, int radix, num *out) {
	size_t i = 0;
	bool neg = false;
	if (i < n && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
	bool prefixed = false;
	if (n - i >= 2 && s[i] == '0') {
		char p = (char)(s[i + 1] | 0x20);
		int pr = p == 'x' ? 16 : p == 'o' ? 8 : p == 'b' ? 2 : 0;
		if (pr && (radix == 0 || radix == pr)) {
			radix = pr;
			i += 2;
			prefixed = true;
		}
	}
	if (radix == 0) radix = 10;
	uint8_t *dg = ak_alloc(n - i + 1);
	size_t nd = 0;
	bool ok = true, after = prefixed;  // whether an underscore may come
	for (; i < n; i++) {
		if (s[i] == '_') {
			if (!after || i + 1 >= n || digit_of((unsigned char)s[i + 1]) >= radix) {
				ok = false;
				break;
			}
			after = false;
			continue;
		}
		int v = digit_of((unsigned char)s[i]);
		if (v >= radix) {
			ok = false;
			break;
		}
		dg[nd++] = (uint8_t)v;
		after = true;
	}
	if (!ok || !nd) {
		ak_free(dg);
		return false;
	}
	size_t z = 0;
	while (z + 1 < nd && !dg[z]) z++;
	int lg = log2_exact(radix);
	num x;
	if (lg) {
		uint64_t bits = (uint64_t)(nd - z) * (uint64_t)lg;
		if (bits / LB + 1 > MAX_LIMBS) {
			ak_free(dg);
			*out = (num){0};
			too_big(MAX_LIMBS + 1);
			return true;
		}
		x = nnew((size_t)(bits / LB) + 1);
		uint64_t at = 0;
		for (size_t j = nd; j-- > z;) {
			for (int b = 0; b < lg; b++, at++)
				if (dg[j] >> b & 1) x.d[at / LB] |= (limb)1 << (at % LB);
		}
		x.n = (size_t)(bits / LB) + 1;
		nfix(&x);
	} else {
		if ((double)(nd - z) * log2((double)radix) > (double)MAX_BITS + 64) {
			ak_free(dg);
			*out = (num){0};
			too_big(MAX_LIMBS + 1);
			return true;
		}
		conv c;
		conv_open(&c, radix);
		x = conv_read(&c, dg + z, nd - z);
		conv_close(&c);
	}
	ak_free(dg);
	x.neg = neg && x.n;
	*out = x;
	return true;
}

// ---- What json.c calls (std.h) ----

static bool json_is(const char *name, size_t n) {
	static const char bigint[] = "std/bigint.BigInt";
	return n == sizeof bigint - 1 && memcmp(name, bigint, n) == 0;
}

static void json_write(ak_strb *sb, const ak_bytes *data) {
	num x = load(data);
	text(sb, &x, 10);
	nfree(&x);
}

static ak_bytes *json_read(const char *s, size_t n) {
	num x;
	if (!parse(s, n, 10, &x)) return NULL;
	if (ak_error_pending()) return NULL;
	ak_bytes *b = encode(&x);
	nfree(&x);
	return b;
}

const ak_bigint_json ak_bigint_json_impl = {json_is, json_write, json_read};

// ---- The builtins ----

static bool radix_ok(int64_t r, bool zero) {
	if ((r >= 2 && r <= 36) || (zero && r == 0)) return true;
	ak_throw_msg("bigint: radix %lld isn't 2 to 36", (long long)r);
	return false;
}

static ak_val ret_num(num x) { return store(&x); }

static ak_val from_int(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return ret_num(ni64(args[0].i));
}

static ak_val parse_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!radix_ok(args[1].i, true)) return (ak_val){0};
	num x;
	if (!parse(STR(0)->data, STR(0)->len, (int)args[1].i, &x)) return (ak_val){0};
	if (ak_error_pending()) return (ak_val){0};
	return store(&x);
}

static ak_val text_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!radix_ok(args[1].i, false)) return (ak_val){0};
	num x = load(BYTES(0));
	ak_strb sb = {0};
	text(&sb, &x, (int)args[1].i);
	nfree(&x);
	return (ak_val){.p = ak_strb_finish(&sb)};
}

static ak_val to_int(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num x = load(BYTES(0));
	ak_val r = {0};
	if (nfits_u64(&x)) {
		uint64_t v = nto_u64(&x);
		if (!x.neg && v <= (uint64_t)INT64_MAX) r.p = ak_box_new(AK_K_INT, (ak_val){.i = (int64_t)v});
		else if (x.neg && v <= (uint64_t)INT64_MAX + 1) r.p = ak_box_new(AK_K_INT, (ak_val){.i = (int64_t)(0 - v)});
	}
	nfree(&x);
	return r;
}

// to_float is the nearest float: the top 64 bits, the rest one sticky
// bit, rounded once by the conversion.
static ak_val to_float(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num x = load(BYTES(0));
	uint64_t bits = bitlen(x.d, x.n);
	double f;
	if (bits <= 64) {
		f = (double)nto_u64(&x);
	} else {
		uint64_t s = bits - 64;
		num top = nshr_mag(&x, s);
		uint64_t v = nto_u64(&top);
		nfree(&top);
		bool sticky = false;
		for (uint64_t i = 0; i < s / LB && !sticky; i++) sticky = x.d[i] != 0;
		if (!sticky && s % LB) sticky = (x.d[s / LB] & (((limb)1 << (s % LB)) - 1)) != 0;
		f = bits > 2000 ? INFINITY : ldexp((double)(v | sticky), (int)s);
	}
	if (x.neg) f = -f;
	nfree(&x);
	return (ak_val){.f = f};
}

static ak_val from_float(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	double f = args[0].f;
	if (isnan(f) || isinf(f)) return (ak_val){0};
	f = trunc(f);
	int e;
	double m = frexp(fabs(f), &e);  // |f| = m·2^e, m in [0.5, 1)
	num x = (num){0};
	if (m != 0) {
		num mm = nu64((uint64_t)ldexp(m, 53));
		x = e >= 53 ? nshl(&mm, (uint64_t)(e - 53)) : nshr_mag(&mm, (uint64_t)(53 - e));
		nfree(&mm);
	}
	x.neg = f < 0 && x.n;
	return store(&x);
}

static ak_val from_bytes(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *b = BYTES(0);
	size_t len = b ? b->len : 0;
	num x = nnew(len / LBYTES + 1);
	x.n = len / LBYTES + 1;
	bool neg = args[1].i && len && b->data[0] >> 7;
	for (size_t i = 0; i < len; i++) {
		uint8_t v = b->data[len - 1 - i];
		if (neg) v = (uint8_t)~v;
		x.d[i / LBYTES] |= (limb)v << (8 * (i % LBYTES));
	}
	nfix(&x);
	if (neg) {
		// -(~b + 1)
		num one = nu64(1);
		num t = nadd(&x, &one);
		nfree(&one);
		nfree(&x);
		x = t;
		x.neg = true;
	}
	return store(&x);
}

static ak_val to_bytes(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num x = load(BYTES(0));
	int64_t size = args[1].i;
	bool sig = args[2].i;
	if (size < 0) {
		nfree(&x);
		ak_throw_msg("bigint: a negative size (%lld)", (long long)size);
		return (ak_val){0};
	}
	if (x.neg && !sig) {
		nfree(&x);
		ak_throw_msg("bigint: a negative number has no unsigned bytes: signed: true writes its two's complement");
		return (ak_val){0};
	}
	// The magnitude written: |x|, or |x| - 1 inverted for a negative x.
	num m = ncopy(&x);
	m.neg = false;
	if (x.neg) {
		num one = nu64(1);
		num t = nsub(&m, &one);
		nfree(&one);
		nfree(&m);
		m = t;
	}
	uint64_t bits = bitlen(m.d, m.n) + (sig ? 1 : 0);
	size_t need = (size_t)((bits + 7) / 8);
	if (sig && !need) need = 1;
	if (size && need > (size_t)size) {
		nfree(&x);
		nfree(&m);
		ak_throw_msg("bigint: the number doesn't fit in %lld bytes", (long long)size);
		return (ak_val){0};
	}
	size_t len = size ? (size_t)size : need;
	ak_bytes *b = ak_bytes_new(NULL, len);
	for (size_t i = 0; i < len; i++) {
		size_t li = i / LBYTES;
		uint8_t v = li < m.n ? (uint8_t)(m.d[li] >> (8 * (i % LBYTES))) : 0;
		b->data[len - 1 - i] = x.neg ? (uint8_t)~v : v;
	}
	nfree(&x);
	nfree(&m);
	return (ak_val){.p = b};
}

static ak_val add_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	num z = nadd(&a, &b);
	nfree(&a);
	nfree(&b);
	return store(&z);
}

static ak_val sub_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	num z = nsub(&a, &b);
	nfree(&a);
	nfree(&b);
	return store(&z);
}

static ak_val mul_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	num z = nmul(&a, &b);
	nfree(&a);
	nfree(&b);
	if (ak_error_pending()) {
		nfree(&z);
		return (ak_val){0};
	}
	return store(&z);
}

// ndiv is x / y in a mode: the quotient toward zero or its remainder
// (truncated: the dividend's sign), or the quotient toward negative
// infinity or its remainder (floored: the divisor's sign).
static bool ndiv(const num *x, const num *y, bool floor_, num *q, num *r) {
	if (!y->n) {
		ak_throw_msg("division by zero");
		return false;
	}
	num qq, rr;
	ndivmod(x, y, &qq, &rr);
	if (floor_ && rr.n && rr.neg != y->neg) {
		num one = nu64(1);
		num t = nsub(&qq, &one);
		nfree(&one);
		nfree(&qq);
		qq = t;
		t = nadd(&rr, y);
		nfree(&rr);
		rr = t;
	}
	if (q) *q = qq;
	else nfree(&qq);
	if (r) *r = rr;
	else nfree(&rr);
	return true;
}

// div_fn: which 0, the truncated quotient; 1, its remainder; 2, the
// floored quotient; 3, its remainder.
static ak_val div_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	int64_t which = args[2].i;
	num z;
	bool ok = ndiv(&a, &b, which >= 2, which % 2 ? NULL : &z, which % 2 ? &z : NULL);
	nfree(&a);
	nfree(&b);
	if (!ok) return (ak_val){0};
	return store(&z);
}

static ak_val div_both(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	num q, r;
	bool ok = ndiv(&a, &b, args[2].i != 0, &q, &r);
	nfree(&a);
	nfree(&b);
	if (!ok) return (ak_val){0};
	ak_arr *out = ak_arr_new(AK_K_REF, NULL, 2);
	ak_arr_push(&out, store(&q));
	ak_arr_push(&out, store(&r));
	return (ak_val){.p = out};
}

static ak_val neg_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0));
	a.neg = a.n && !a.neg;
	return store(&a);
}

static ak_val abs_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0));
	a.neg = false;
	return store(&a);
}

static ak_val cmp_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	int c = ncmp(&a, &b);
	nfree(&a);
	nfree(&b);
	return (ak_val){.i = c};
}

static ak_val pow_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0));
	int64_t e = args[1].i;
	if (e < 0) {
		nfree(&a);
		ak_throw_msg("bigint: pow of a negative exponent (%lld): modPow takes one, with a modulus", (long long)e);
		return (ak_val){0};
	}
	if (!e || (a.n == 1 && a.d[0] == 1)) {
		bool neg = a.neg && (e & 1);
		nfree(&a);
		num one = nu64(1);
		one.neg = neg;
		return store(&one);
	}
	if (!a.n) return store(&a);
	uint64_t bits = bitlen(a.d, a.n);
	if ((double)(bits - 1) * (double)e > (double)MAX_BITS) {
		nfree(&a);
		too_big(MAX_LIMBS + 1);
		return (ak_val){0};
	}
	num z = nu64(1);
	num b = ncopy(&a);
	for (uint64_t k = (uint64_t)e;;) {
		if (k & 1) {
			num t = nmul(&z, &b);
			nfree(&z);
			z = t;
		}
		k >>= 1;
		if (!k) break;
		num t = nmul(&b, &b);
		nfree(&b);
		b = t;
	}
	nfree(&b);
	z.neg = a.neg && (e & 1) && z.n;
	nfree(&a);
	if (ak_error_pending()) {
		nfree(&z);
		return (ak_val){0};
	}
	return store(&z);
}

static bool positive_modulus(const num *m, const char *what) {
	if (m->n && !m->neg) return true;
	if (!m->n) ak_throw_msg("division by zero");
	else ak_throw_msg("bigint: %s of a negative modulus", what);
	return false;
}

static ak_val modpow_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), e = load(BYTES(1)), m = load(BYTES(2));
	ak_val r = {0};
	if (!positive_modulus(&m, "modPow")) goto done;
	if (nis_one(&m)) {
		num z = {0};
		r = store(&z);
		goto done;
	}
	if (e.neg) {
		num inv;
		if (!ninverse(&a, &m, &inv)) {
			ak_throw_msg("bigint: modPow of a negative exponent, of a number with no inverse modulo m");
			goto done;
		}
		nfree(&a);
		a = inv;
		e.neg = false;
	}
	num z = nmodpow(&a, &e, &m);
	r = store(&z);
done:
	nfree(&a);
	nfree(&e);
	nfree(&m);
	return r;
}

static ak_val modinv_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), m = load(BYTES(1));
	ak_val r = {0};
	if (positive_modulus(&m, "modInverse")) {
		num z = {0};
		if (nis_one(&m) || ninverse(&a, &m, &z)) r = store(&z);
	}
	nfree(&a);
	nfree(&m);
	return r;
}

static ak_val gcd_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	num g = ngcd(&a, &b);
	nfree(&a);
	nfree(&b);
	return store(&g);
}

static ak_val lcm_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0)), b = load(BYTES(1));
	a.neg = b.neg = false;
	num z = {0};
	if (a.n && b.n) {
		num g = ngcd(&a, &b), q;
		ndivmod(&a, &g, &q, NULL);
		z = nmul(&q, &b);
		nfree(&g);
		nfree(&q);
	}
	nfree(&a);
	nfree(&b);
	if (ak_error_pending()) {
		nfree(&z);
		return (ak_val){0};
	}
	return store(&z);
}

static ak_val sqrt_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0));
	if (a.neg) {
		nfree(&a);
		ak_throw_msg("bigint: the square root of a negative number");
		return (ak_val){0};
	}
	num z = nsqrt(&a);
	nfree(&a);
	return store(&z);
}

// Bits: a negative number as two's complement, infinitely many ones on
// the left, as Go's and Python's are: -x is ~(x - 1).

// twos is x's limbs as two's complement in n limbs (n past x's).
static void twos(limb *z, const num *x, size_t n) {
	memset(z, 0, n * sizeof(limb));
	if (x->n) memcpy(z, x->d, x->n * sizeof(limb));
	if (x->neg) {
		for (size_t i = 0; i < n; i++) z[i] = ~z[i];
		add_1(z, z, n, 1);
	}
}

// from_twos is the number of n limbs of two's complement.
static num from_twos(limb *z, size_t n) {
	num x = nnew(n);
	bool neg = z[n - 1] >> (LB - 1);
	memcpy(x.d, z, n * sizeof(limb));
	if (neg) {
		for (size_t i = 0; i < n; i++) x.d[i] = ~x.d[i];
		add_1(x.d, x.d, n, 1);
	}
	x.n = n;
	x.neg = neg;
	nfix(&x);
	return x;
}

static ak_val bitop(ak_val *args, int op) {
	num a = load(BYTES(0)), b = load(BYTES(1));
	size_t n = (a.n > b.n ? a.n : b.n) + 1;
	limb *x = lalloc(n), *y = lalloc(n);
	twos(x, &a, n);
	twos(y, &b, n);
	for (size_t i = 0; i < n; i++) x[i] = op == 0 ? x[i] & y[i] : op == 1 ? x[i] | y[i] : x[i] ^ y[i];
	num z = from_twos(x, n);
	ak_free(x);
	ak_free(y);
	nfree(&a);
	nfree(&b);
	return store(&z);
}

static ak_val and_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return bitop(args, 0);
}

static ak_val or_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return bitop(args, 1);
}

static ak_val xor_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return bitop(args, 2);
}

// not_fn: ~x is -x - 1.
static ak_val not_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0));
	num one = nu64(1);
	num na = nneg(&a);
	num z = nsub(&na, &one);
	nfree(&one);
	nfree(&a);
	return store(&z);
}

static bool count_ok(int64_t c) {
	if (c >= 0) return true;
	ak_throw_msg("bigint: a shift by a negative count (%lld)", (long long)c);
	return false;
}

static ak_val shl_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!count_ok(args[1].i)) return (ak_val){0};
	num a = load(BYTES(0));
	if (a.n && (uint64_t)args[1].i > MAX_BITS) {
		nfree(&a);
		too_big(MAX_LIMBS + 1);
		return (ak_val){0};
	}
	num z = nshl(&a, (uint64_t)args[1].i);
	nfree(&a);
	if (ak_error_pending()) {
		nfree(&z);
		return (ak_val){0};
	}
	return store(&z);
}

// shr_fn shifts toward negative infinity, as >> does an int: -x >> s is
// -(((x - 1) >> s) + 1).
static ak_val shr_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!count_ok(args[1].i)) return (ak_val){0};
	num a = load(BYTES(0));
	uint64_t s = (uint64_t)args[1].i;
	num z;
	if (!a.neg) {
		z = nshr_mag(&a, s);
	} else {
		num one = nu64(1);
		num m = ncopy(&a);
		m.neg = false;
		num t = nsub(&m, &one);
		num u = nshr_mag(&t, s);
		z = nadd(&u, &one);
		z.neg = true;
		nfree(&one);
		nfree(&m);
		nfree(&t);
		nfree(&u);
	}
	nfree(&a);
	return store(&z);
}

static ak_val bitlen_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0));
	int64_t b = (int64_t)bitlen(a.d, a.n);
	nfree(&a);
	return (ak_val){.i = b};
}

static ak_val bit_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t i = args[1].i;
	if (i < 0) {
		ak_throw_msg("bigint: a negative bit index (%lld)", (long long)i);
		return (ak_val){0};
	}
	num a = load(BYTES(0));
	bool v;
	if (!a.neg) {
		v = bit_at(a.d, a.n, (uint64_t)i);
	} else {
		num one = nu64(1);
		num m = ncopy(&a);
		m.neg = false;
		num t = nsub(&m, &one);
		v = !bit_at(t.d, t.n, (uint64_t)i);
		nfree(&one);
		nfree(&m);
		nfree(&t);
	}
	nfree(&a);
	return (ak_val){.i = v};
}

static ak_val tz_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num a = load(BYTES(0));
	int64_t z = 0;
	for (size_t i = 0; i < a.n; i++) {
		if (a.d[i]) {
			z = (int64_t)(i * LB) + ctz(a.d[i]);
			break;
		}
	}
	nfree(&a);
	return (ak_val){.i = z};
}

static ak_val prime_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (args[1].i < 0) {
		ak_throw_msg("bigint: a negative number of rounds (%lld)", (long long)args[1].i);
		return (ak_val){0};
	}
	num a = load(BYTES(0));
	bool p = nprime(&a, args[1].i);
	nfree(&a);
	return (ak_val){.i = p};
}

// random_fn is uniform in [0, below), from the system's randomness:
// random bits as many as below has, until one is under it.
static ak_val random_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	num m = load(BYTES(0));
	if (!m.n || m.neg) {
		nfree(&m);
		ak_throw_msg("bigint: random's bound must be positive");
		return (ak_val){0};
	}
	uint64_t bits = bitlen(m.d, m.n);
	num x = nnew(m.n);
	for (;;) {
		if (!ak_os_random(x.d, m.n * sizeof(limb))) {
			nfree(&m);
			nfree(&x);
			ak_throw_msg("bigint: the system gave no randomness");
			return (ak_val){0};
		}
		if (bits % LB) x.d[m.n - 1] &= ((limb)1 << (bits % LB)) - 1;
		if (cmp_n(x.d, m.d, m.n) < 0) break;
	}
	x.n = m.n;
	nfix(&x);
	nfree(&m);
	return store(&x);
}

// prod is lo·(lo+1)·…·hi, by halves (a product tree): the work is in
// multiplying numbers of about one size, which Karatsuba's is for.
static num prod(uint64_t lo, uint64_t hi) {
	if (hi - lo < 16) {
		num z = nnew((size_t)(hi - lo + 2) * (64 / LB) + 1);
		z.d[0] = 1;
		size_t zn = 1;
		for (uint64_t k = lo; k <= hi; k++) {
			if (k <= LMAX) {
				limb c = mul_1(z.d, z.d, zn, (limb)k);
				if (c) z.d[zn++] = c;
				continue;
			}
			num kk = nu64(k);
			limb *t = lalloc(zn + kk.n);
			mul_nat(t, z.d, zn, kk.d, kk.n);
			memcpy(z.d, t, (zn + kk.n) * sizeof(limb));
			zn = norm(z.d, zn + kk.n);
			ak_free(t);
			nfree(&kk);
		}
		z.n = zn;
		nfix(&z);
		return z;
	}
	uint64_t mid = lo + (hi - lo) / 2;
	num a = prod(lo, mid), b = prod(mid + 1, hi);
	num z = nmul(&a, &b);
	nfree(&a);
	nfree(&b);
	return z;
}

static ak_val factorial_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t k = args[0].i;
	if (k < 0) {
		ak_throw_msg("bigint: the factorial of a negative number (%lld)", (long long)k);
		return (ak_val){0};
	}
	// log2 k! by Stirling's formula, a little over.
	double kd = (double)k;
	if (k > 2 && (kd * log(kd) - kd + 0.5 * log(2 * 3.141592653589793 * kd) + 1) / log(2.0) > (double)MAX_BITS) {
		too_big(MAX_LIMBS + 1);
		return (ak_val){0};
	}
	num z = k < 2 ? nu64(1) : prod(2, (uint64_t)k);
	return store(&z);
}

const ak_lib_entry ak_std_bigint[] = {
	{"bigint.bigFromInt", from_int},
	{"bigint.bigParse", parse_fn},
	{"bigint.bigText", text_fn},
	{"bigint.bigToInt", to_int},
	{"bigint.bigToFloat", to_float},
	{"bigint.bigFromFloat", from_float},
	{"bigint.bigFromBytes", from_bytes},
	{"bigint.bigToBytes", to_bytes},
	{"bigint.bigAdd", add_fn},
	{"bigint.bigSub", sub_fn},
	{"bigint.bigMul", mul_fn},
	{"bigint.bigDiv", div_fn},
	{"bigint.bigDivBoth", div_both},
	{"bigint.bigNeg", neg_fn},
	{"bigint.bigAbs", abs_fn},
	{"bigint.bigCmp", cmp_fn},
	{"bigint.bigPow", pow_fn},
	{"bigint.bigModPow", modpow_fn},
	{"bigint.bigModInverse", modinv_fn},
	{"bigint.bigGcd", gcd_fn},
	{"bigint.bigLcm", lcm_fn},
	{"bigint.bigSqrt", sqrt_fn},
	{"bigint.bigAnd", and_fn},
	{"bigint.bigOr", or_fn},
	{"bigint.bigXor", xor_fn},
	{"bigint.bigNot", not_fn},
	{"bigint.bigShl", shl_fn},
	{"bigint.bigShr", shr_fn},
	{"bigint.bigBitLength", bitlen_fn},
	{"bigint.bigBit", bit_fn},
	{"bigint.bigTrailingZeros", tz_fn},
	{"bigint.bigIsPrime", prime_fn},
	{"bigint.bigRandom", random_fn},
	{"bigint.bigFactorial", factorial_fn},
	{NULL, NULL},
};
