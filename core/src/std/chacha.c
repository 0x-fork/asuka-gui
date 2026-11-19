// chacha.c: ChaCha20-Poly1305 (RFC 8439) and XChaCha20-Poly1305
// (draft-irtf-cfrg-xchacha) for std/crypto's AEAD (aead.c), as fast as
// the CPU lets plain C: ChaCha20 four blocks at a time in NEON on ARMv8
// (each state word a vector of the four blocks', Bernstein's and
// OpenSSL's layout), a block at a time elsewhere; Poly1305 in three
// 44-bit limbs and 64-bit products (Andrew Moon's poly1305-donna-64).
// The same bytes as Monocypher's (core/test/aead_test.c checks every
// length against it), constant time: no branch or index depends on a
// key or the data.
#include "std/chacha.h"

#include <string.h>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define CHACHA_NEON 1
#endif

static inline uint32_t ld32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void st32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static inline uint32_t rotl32(uint32_t x, int n) { return x << n | x >> (32 - n); }

#define QR(a, b, c, d) \
	a += b, d = rotl32(d ^ a, 16), c += d, b = rotl32(b ^ c, 12), a += b, d = rotl32(d ^ a, 8), c += d, b = rotl32(b ^ c, 7)

// rounds runs ChaCha's 20 rounds over x (its column and diagonal rounds,
// ten each).
static void rounds(uint32_t x[16]) {
	for (int i = 0; i < 10; i++) {
		QR(x[0], x[4], x[8], x[12]), QR(x[1], x[5], x[9], x[13]), QR(x[2], x[6], x[10], x[14]), QR(x[3], x[7], x[11], x[15]);
		QR(x[0], x[5], x[10], x[15]), QR(x[1], x[6], x[11], x[12]), QR(x[2], x[7], x[8], x[13]), QR(x[3], x[4], x[9], x[14]);
	}
}

static void init(uint32_t s[16], const uint8_t key[32], uint32_t counter, const uint8_t nonce[12]) {
	s[0] = 0x61707865, s[1] = 0x3320646e, s[2] = 0x79622d32, s[3] = 0x6b206574;
	for (int i = 0; i < 8; i++) s[4 + i] = ld32(key + 4 * i);
	s[12] = counter;
	for (int i = 0; i < 3; i++) s[13 + i] = ld32(nonce + 4 * i);
}

// block_xor: one block's keystream XOR in (n <= 64 bytes) into out.
static void block_xor(const uint32_t s[16], const uint8_t *in, uint8_t *out, size_t n) {
	uint32_t x[16];
	uint8_t ks[64];
	memcpy(x, s, sizeof x);
	rounds(x);
	for (int i = 0; i < 16; i++) st32(ks + 4 * i, x[i] + s[i]);
	for (size_t i = 0; i < n; i++) out[i] = in[i] ^ ks[i];
}

#if CHACHA_NEON
#define ROTV(v, n) vsriq_n_u32(vshlq_n_u32(v, n), v, 32 - (n))
#define ROT16(v) vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32(v)))
#define QRV(a, b, c, d) \
	a = vaddq_u32(a, b), d = ROT16(veorq_u32(d, a)), c = vaddq_u32(c, d), b = ROTV(veorq_u32(b, c), 12), \
	a = vaddq_u32(a, b), d = ROTV(veorq_u32(d, a), 8), c = vaddq_u32(c, d), b = ROTV(veorq_u32(b, c), 7)

// four_xor: four blocks' keystream (counters s[12] to s[12] + 3) XOR the
// 256 bytes of in into out.
static void four_xor(const uint32_t s[16], const uint8_t *in, uint8_t *out) {
	static const uint32_t inc[4] = {0, 1, 2, 3};
	uint32x4_t x[16], o[16];
	for (int i = 0; i < 16; i++) o[i] = vdupq_n_u32(s[i]);
	o[12] = vaddq_u32(o[12], vld1q_u32(inc));
	for (int i = 0; i < 16; i++) x[i] = o[i];
	for (int r = 0; r < 10; r++) {
		QRV(x[0], x[4], x[8], x[12]), QRV(x[1], x[5], x[9], x[13]), QRV(x[2], x[6], x[10], x[14]), QRV(x[3], x[7], x[11], x[15]);
		QRV(x[0], x[5], x[10], x[15]), QRV(x[1], x[6], x[11], x[12]), QRV(x[2], x[7], x[8], x[13]), QRV(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++) x[i] = vaddq_u32(x[i], o[i]);
	// Each four words, transposed: a vector of each block's.
	for (int g = 0; g < 4; g++) {
		uint32x4x2_t a = vtrnq_u32(x[4 * g], x[4 * g + 1]), b = vtrnq_u32(x[4 * g + 2], x[4 * g + 3]);
		uint32x4_t w[4] = {
			vcombine_u32(vget_low_u32(a.val[0]), vget_low_u32(b.val[0])),
			vcombine_u32(vget_low_u32(a.val[1]), vget_low_u32(b.val[1])),
			vcombine_u32(vget_high_u32(a.val[0]), vget_high_u32(b.val[0])),
			vcombine_u32(vget_high_u32(a.val[1]), vget_high_u32(b.val[1])),
		};
		for (int j = 0; j < 4; j++) {
			const uint8_t *p = in + 64 * j + 16 * g;
			vst1q_u8(out + 64 * j + 16 * g, veorq_u8(vld1q_u8(p), vreinterpretq_u8_u32(w[j])));
		}
	}
}
#endif

void ak_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t *in, uint8_t *out, size_t n) {
	uint32_t s[16];
	init(s, key, counter, nonce);
#if CHACHA_NEON
	for (; n >= 256; n -= 256, in += 256, out += 256, s[12] += 4) four_xor(s, in, out);
#endif
	for (; n; s[12]++) {
		size_t k = n < 64 ? n : 64;
		block_xor(s, in, out, k);
		in += k, out += k, n -= k;
	}
	memset(s, 0, sizeof s);
}

void ak_hchacha20(uint8_t out[32], const uint8_t key[32], const uint8_t in[16]) {
	uint32_t x[16];
	x[0] = 0x61707865, x[1] = 0x3320646e, x[2] = 0x79622d32, x[3] = 0x6b206574;
	for (int i = 0; i < 8; i++) x[4 + i] = ld32(key + 4 * i);
	for (int i = 0; i < 4; i++) x[12 + i] = ld32(in + 4 * i);
	rounds(x);
	for (int i = 0; i < 4; i++) st32(out + 4 * i, x[i]), st32(out + 16 + 4 * i, x[12 + i]);
	memset(x, 0, sizeof x);
}

// ---- Poly1305 ----
#if AK_CHACHA_FAST

typedef unsigned __int128 u128;

static inline uint64_t ld64(const uint8_t *p) { return (uint64_t)ld32(p) | (uint64_t)ld32(p + 4) << 32; }
static inline void st64(uint8_t *p, uint64_t v) { st32(p, (uint32_t)v), st32(p + 4, (uint32_t)(v >> 32)); }
#define M44 ((uint64_t)0xfffffffffff)
#define M42 ((uint64_t)0x3ffffffffff)

void ak_poly1305_init(ak_poly1305 *p, const uint8_t key[32]) {
	uint64_t t0 = ld64(key), t1 = ld64(key + 8);
	p->r[0] = t0 & 0xffc0fffffff;
	p->r[1] = ((t0 >> 44) | (t1 << 20)) & 0xfffffc0ffff;
	p->r[2] = (t1 >> 24) & 0x00ffffffc0f;
	p->h[0] = p->h[1] = p->h[2] = 0;
	p->pad[0] = ld64(key + 16), p->pad[1] = ld64(key + 24);
	p->n = 0;
}

// blocks: the 16-byte blocks of m, each with its 2^128 bit (hibit) set.
static void blocks(ak_poly1305 *p, const uint8_t *m, size_t n, uint64_t hibit) {
	uint64_t r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], s1 = r1 * (5 << 2), s2 = r2 * (5 << 2);
	uint64_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2];
	for (; n >= 16; m += 16, n -= 16) {
		uint64_t t0 = ld64(m), t1 = ld64(m + 8);
		h0 += t0 & M44;
		h1 += ((t0 >> 44) | (t1 << 20)) & M44;
		h2 += ((t1 >> 24) & M42) | hibit;
		u128 d0 = (u128)h0 * r0 + (u128)h1 * s2 + (u128)h2 * s1;
		u128 d1 = (u128)h0 * r1 + (u128)h1 * r0 + (u128)h2 * s2;
		u128 d2 = (u128)h0 * r2 + (u128)h1 * r1 + (u128)h2 * r0;
		uint64_t c = (uint64_t)(d0 >> 44);
		h0 = (uint64_t)d0 & M44;
		d1 += c;
		c = (uint64_t)(d1 >> 44);
		h1 = (uint64_t)d1 & M44;
		d2 += c;
		c = (uint64_t)(d2 >> 42);
		h2 = (uint64_t)d2 & M42;
		h0 += c * 5;
		c = h0 >> 44;
		h0 &= M44;
		h1 += c;
	}
	p->h[0] = h0, p->h[1] = h1, p->h[2] = h2;
}

void ak_poly1305_update(ak_poly1305 *p, const uint8_t *m, size_t n) {
	if (p->n) {
		size_t k = 16 - p->n < n ? 16 - p->n : n;
		memcpy(p->buf + p->n, m, k);
		p->n += k, m += k, n -= k;
		if (p->n < 16) return;
		blocks(p, p->buf, 16, (uint64_t)1 << 40);
		p->n = 0;
	}
	if (n >= 16) {
		size_t w = n & ~(size_t)15;
		blocks(p, m, w, (uint64_t)1 << 40);
		m += w, n -= w;
	}
	if (n) memcpy(p->buf, m, n), p->n = n;
}

void ak_poly1305_final(ak_poly1305 *p, uint8_t tag[16]) {
	if (p->n) {
		// The last block: a 1 after its bytes, zeros to 16, no 2^128.
		p->buf[p->n] = 1;
		memset(p->buf + p->n + 1, 0, 15 - p->n);
		blocks(p, p->buf, 16, 0);
	}
	uint64_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], c;
	c = h1 >> 44, h1 &= M44;
	h2 += c, c = h2 >> 42, h2 &= M42;
	h0 += c * 5, c = h0 >> 44, h0 &= M44;
	h1 += c, c = h1 >> 44, h1 &= M44;
	h2 += c, c = h2 >> 42, h2 &= M42;
	h0 += c * 5, c = h0 >> 44, h0 &= M44;
	h1 += c;
	// h - p, taken when it doesn't go below 0 (constant time).
	uint64_t g0 = h0 + 5, g1, g2;
	c = g0 >> 44, g0 &= M44;
	g1 = h1 + c, c = g1 >> 44, g1 &= M44;
	g2 = h2 + c - ((uint64_t)1 << 42);
	uint64_t mask = (g2 >> 63) - 1;
	g0 &= mask, g1 &= mask, g2 &= mask;
	mask = ~mask;
	h0 = (h0 & mask) | g0, h1 = (h1 & mask) | g1, h2 = (h2 & mask) | g2;
	// h + pad, mod 2^128.
	uint64_t t0 = p->pad[0], t1 = p->pad[1];
	h0 += t0 & M44, c = h0 >> 44, h0 &= M44;
	h1 += (((t0 >> 44) | (t1 << 20)) & M44) + c, c = h1 >> 44, h1 &= M44;
	h2 += ((t1 >> 24) & M42) + c, h2 &= M42;
	st64(tag, h0 | h1 << 44);
	st64(tag + 8, h1 >> 20 | h2 << 24);
	memset(p, 0, sizeof *p);
}

// ---- The AEAD ----

static const uint8_t zeros[16];

static void mac(uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t an, const uint8_t *ct, size_t cn) {
	uint8_t pk[64] = {0};
	ak_chacha20_xor(key, nonce, 0, pk, pk, 64);
	ak_poly1305 p;
	ak_poly1305_init(&p, pk);
	ak_poly1305_update(&p, ad, an);
	ak_poly1305_update(&p, zeros, (16 - an % 16) % 16);
	ak_poly1305_update(&p, ct, cn);
	ak_poly1305_update(&p, zeros, (16 - cn % 16) % 16);
	uint8_t lens[16];
	st64(lens, an), st64(lens + 8, cn);
	ak_poly1305_update(&p, lens, 16);
	ak_poly1305_final(&p, tag);
	memset(pk, 0, sizeof pk);
}

// xkey: XChaCha's: the key HChaCha20 makes of the nonce's first 16
// bytes, and the 12-byte nonce of the rest.
static void xkey(uint8_t sub[32], uint8_t n12[12], const uint8_t key[32], const uint8_t nonce[24]) {
	ak_hchacha20(sub, key, nonce);
	memset(n12, 0, 4);
	memcpy(n12 + 4, nonce + 16, 8);
}

void ak_chacha_seal(bool x, const uint8_t key[32], const uint8_t *nonce, const uint8_t *ad, size_t an, const uint8_t *pt, uint8_t *ct, size_t n, uint8_t tag[16]) {
	uint8_t sub[32], n12[12];
	if (x) xkey(sub, n12, key, nonce), key = sub, nonce = n12;
	ak_chacha20_xor(key, nonce, 1, pt, ct, n);
	mac(tag, key, nonce, ad, an, ct, n);
	memset(sub, 0, sizeof sub);
}

bool ak_chacha_open(bool x, const uint8_t key[32], const uint8_t *nonce, const uint8_t *ad, size_t an, const uint8_t *ct, uint8_t *pt, size_t n, const uint8_t tag[16]) {
	uint8_t sub[32], n12[12], mine[16];
	if (x) xkey(sub, n12, key, nonce), key = sub, nonce = n12;
	mac(mine, key, nonce, ad, an, ct, n);
	uint8_t diff = 0;
	for (int i = 0; i < 16; i++) diff |= (uint8_t)(mine[i] ^ tag[i]);
	bool ok = diff == 0;
	if (ok) ak_chacha20_xor(key, nonce, 1, ct, pt, n);
	memset(sub, 0, sizeof sub);
	return ok;
}
#endif
