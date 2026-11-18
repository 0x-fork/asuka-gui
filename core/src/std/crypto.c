// crypto.c: std/crypto's builtins: SHA-256, SHA-384, SHA-512 and SHA-1
// (FIPS 180-4), MD5 (RFC 1321), HMAC (RFC 2104), HKDF (RFC 5869), PBKDF2
// (RFC 8018), Ed25519 (RFC 8032, ed25519.c), the system's randomness and
// version 4 UUIDs. What costs more has tables of its own, which only an
// app that calls them links: aead.c (AES-GCM, ChaCha20-Poly1305),
// password.c (Argon2, bcrypt), pubkey.c (RSA, ECDSA, keys).
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE  // syscall
#endif
#if defined(_WIN32)
#define _CRT_RAND_S  // rand_s
#endif
#include "std/std.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(__APPLE__)
// arc4random_buf, in stdlib.h
#elif defined(_WIN32)
// rand_s, in stdlib.h
#elif defined(__wasm__)
#include <sys/random.h>  // getentropy
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define BYTES(i) ((ak_bytes *)args[i].p)

static uint32_t rotr(uint32_t x, int n) { return x >> n | x << (32 - n); }
static uint32_t rotl(uint32_t x, int n) { return x << n | x >> (32 - n); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

// md_pad runs block over p and the padding after it (a 1 bit, zeros,
// the length in bits, big-endian): what SHA-1 and SHA-2 share.
static void md_pad(const uint8_t *p, size_t n, void *state, void (*block)(void *state, const uint8_t *b)) {
	size_t i = 0;
	for (; i + 64 <= n; i += 64) block(state, p + i);
	uint8_t last[128] = {0};
	size_t rest = n - i;
	if (rest) memcpy(last, p + i, rest);
	last[rest] = 0x80;
	size_t len = rest + 1 + 8 <= 64 ? 64 : 128;
	uint64_t bits = (uint64_t)n * 8;
	for (int k = 0; k < 8; k++) last[len - 1 - k] = (uint8_t)(bits >> (8 * k));
	block(state, last);
	if (len == 128) block(state, last + 64);
}

static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_block(uint32_t h[8], const uint8_t *b) {
	uint32_t w[64];
	for (int i = 0; i < 16; i++) w[i] = be32(b + 4 * i);
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
		hh = g, g = f, f = e, e = d + t1, d = c, c = bb, bb = a, a = t1 + t2;
	}
	h[0] += a, h[1] += bb, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}

void ak_sha256_init(ak_sha256_ctx *c) {
	static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	memcpy(c->h, iv, sizeof iv);
	c->n = 0;
	c->len = 0;
}

void ak_sha256_update(ak_sha256_ctx *c, const void *data, size_t n) {
	const uint8_t *p = data;
	c->len += n;
	if (c->n) {
		size_t k = 64 - c->n < n ? 64 - c->n : n;
		memcpy(c->buf + c->n, p, k);
		c->n += k, p += k, n -= k;
		if (c->n < 64) return;
		sha256_block(c->h, c->buf);
		c->n = 0;
	}
	for (; n >= 64; p += 64, n -= 64) sha256_block(c->h, p);
	if (n) memcpy(c->buf, p, n);
	c->n = n;
}

void ak_sha256_final(ak_sha256_ctx *c, uint8_t out[32]) {
	uint64_t bits = c->len * 8;
	uint8_t pad[72] = {0x80};
	size_t k = c->n < 56 ? 56 - c->n : 120 - c->n;
	for (int i = 0; i < 8; i++) pad[k + (size_t)i] = (uint8_t)(bits >> (56 - 8 * i));
	ak_sha256_update(c, pad, k + 8);
	for (int i = 0; i < 8; i++) put_be32(out + 4 * i, c->h[i]);
}

void ak_sha256(const void *p, size_t n, uint8_t out[32]) {
	ak_sha256_ctx c;
	ak_sha256_init(&c);
	ak_sha256_update(&c, p, n);
	ak_sha256_final(&c, out);
}

// ---- SHA-512 (FIPS 180-4) ----

static const uint64_t K512[80] = {
	0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
	0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
	0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
	0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
	0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
	0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
	0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
	0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
	0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
	0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
	0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
	0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
	0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
	0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
	0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
	0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

static uint64_t rotr64(uint64_t x, int n) { return x >> n | x << (64 - n); }

static uint64_t be64(const uint8_t *p) {
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) v = v << 8 | p[i];
	return v;
}

static void sha512_block(uint64_t h[8], const uint8_t *b) {
	uint64_t w[80];
	for (int i = 0; i < 16; i++) w[i] = be64(b + 8 * i);
	for (int i = 16; i < 80; i++) {
		uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
		uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint64_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
	for (int i = 0; i < 80; i++) {
		uint64_t t1 = hh + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) + ((e & f) ^ (~e & g)) + K512[i] + w[i];
		uint64_t t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) + ((a & bb) ^ (a & c) ^ (bb & c));
		hh = g, g = f, f = e, e = d + t1, d = c, c = bb, bb = a, a = t1 + t2;
	}
	h[0] += a, h[1] += bb, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}

void ak_sha512_init(ak_sha512_ctx *c) {
	static const uint64_t iv[8] = {0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
	                               0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179};
	memcpy(c->h, iv, sizeof iv);
	c->n = 0;
	c->len = 0;
}

void ak_sha512_update(ak_sha512_ctx *c, const void *data, size_t n) {
	const uint8_t *p = data;
	c->len += n;
	if (c->n) {
		size_t k = 128 - c->n < n ? 128 - c->n : n;
		memcpy(c->buf + c->n, p, k);
		c->n += k, p += k, n -= k;
		if (c->n < 128) return;
		sha512_block(c->h, c->buf);
		c->n = 0;
	}
	for (; n >= 128; p += 128, n -= 128) sha512_block(c->h, p);
	if (n) memcpy(c->buf, p, n);
	c->n = n;
}

void ak_sha512_final(ak_sha512_ctx *c, uint8_t out[64]) {
	// The length is 128 bits; messages here are shorter than 2^61 bytes.
	uint64_t bits = c->len * 8, hi = c->len >> 61;
	uint8_t pad[144] = {0x80};
	size_t k = c->n < 112 ? 112 - c->n : 240 - c->n;
	for (int i = 0; i < 8; i++) {
		pad[k + (size_t)i] = (uint8_t)(hi >> (56 - 8 * i));
		pad[k + 8 + (size_t)i] = (uint8_t)(bits >> (56 - 8 * i));
	}
	ak_sha512_update(c, pad, k + 16);
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 8; j++) out[8 * i + j] = (uint8_t)(c->h[i] >> (56 - 8 * j));
}

void ak_sha512(const void *p, size_t n, uint8_t out[64]) {
	ak_sha512_ctx c;
	ak_sha512_init(&c);
	ak_sha512_update(&c, p, n);
	ak_sha512_final(&c, out);
}

static void sha1_block(void *state, const uint8_t *b) {
	uint32_t *h = state, w[80];
	for (int i = 0; i < 16; i++) w[i] = be32(b + 4 * i);
	for (int i = 16; i < 80; i++) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
	for (int i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) f = (bb & c) | (~bb & d), k = 0x5a827999;
		else if (i < 40) f = bb ^ c ^ d, k = 0x6ed9eba1;
		else if (i < 60) f = (bb & c) | (bb & d) | (c & d), k = 0x8f1bbcdc;
		else f = bb ^ c ^ d, k = 0xca62c1d6;
		uint32_t t = rotl(a, 5) + f + e + k + w[i];
		e = d, d = c, c = rotl(bb, 30), bb = a, a = t;
	}
	h[0] += a, h[1] += bb, h[2] += c, h[3] += d, h[4] += e;
}

void ak_sha1(const void *p, size_t n, uint8_t out[20]) {
	uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	md_pad(p, n, h, sha1_block);
	for (int i = 0; i < 5; i++) put_be32(out + 4 * i, h[i]);
}

// The same in pieces, for HMAC over SHA-1 (PBKDF2's RFC 6070 vectors,
// what old protocols sign with).
void ak_sha1_init(ak_sha1_ctx *c) {
	static const uint32_t iv[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	memcpy(c->h, iv, sizeof iv);
	c->n = 0;
	c->len = 0;
}

// md_update feeds p to a hash of 64-byte blocks (SHA-1's, MD5's).
static void md_update(uint8_t buf[64], size_t *bn, uint64_t *total, void *state, void (*block)(void *, const uint8_t *), const void *data, size_t n) {
	const uint8_t *p = data;
	*total += n;
	if (*bn) {
		size_t k = 64 - *bn < n ? 64 - *bn : n;
		memcpy(buf + *bn, p, k);
		*bn += k, p += k, n -= k;
		if (*bn < 64) return;
		block(state, buf);
		*bn = 0;
	}
	for (; n >= 64; p += 64, n -= 64) block(state, p);
	if (n) memcpy(buf, p, n);
	*bn = n;
}

void ak_sha1_update(ak_sha1_ctx *c, const void *p, size_t n) { md_update(c->buf, &c->n, &c->len, c->h, sha1_block, p, n); }

void ak_sha1_final(ak_sha1_ctx *c, uint8_t out[20]) {
	uint64_t bits = c->len * 8;
	uint8_t pad[72] = {0x80};
	size_t k = c->n < 56 ? 56 - c->n : 120 - c->n;
	for (int i = 0; i < 8; i++) pad[k + (size_t)i] = (uint8_t)(bits >> (56 - 8 * i));
	ak_sha1_update(c, pad, k + 8);
	for (int i = 0; i < 5; i++) put_be32(out + 4 * i, c->h[i]);
}

// ---- SHA-384 (FIPS 180-4): SHA-512 from its own start, cut to 48 bytes ----

void ak_sha384_init(ak_sha512_ctx *c) {
	static const uint64_t iv[8] = {0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939,
	                               0x67332667ffc00b31, 0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4};
	memcpy(c->h, iv, sizeof iv);
	c->n = 0;
	c->len = 0;
}

void ak_sha384(const void *p, size_t n, uint8_t out[48]) {
	ak_sha512_ctx c;
	uint8_t full[64];
	ak_sha384_init(&c);
	ak_sha512_update(&c, p, n);
	ak_sha512_final(&c, full);
	memcpy(out, full, 48);
}

// ---- MD5 (RFC 1321): for checksums and old protocols, not security ----

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void md5_block(void *state, const uint8_t *b) {
	static const uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
	};
	static const uint8_t S[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
	uint32_t *h = state, m[16];
	for (int i = 0; i < 16; i++) m[i] = le32(b + 4 * i);
	uint32_t a = h[0], bb = h[1], c = h[2], d = h[3];
	for (int i = 0; i < 64; i++) {
		uint32_t f;
		int g;
		if (i < 16) f = (bb & c) | (~bb & d), g = i;
		else if (i < 32) f = (d & bb) | (~d & c), g = (5 * i + 1) & 15;
		else if (i < 48) f = bb ^ c ^ d, g = (3 * i + 5) & 15;
		else f = c ^ (bb | ~d), g = (7 * i) & 15;
		uint32_t t = d;
		d = c, c = bb;
		bb = bb + rotl(a + f + K[i] + m[g], S[(i >> 4) * 4 + (i & 3)]);
		a = t;
	}
	h[0] += a, h[1] += bb, h[2] += c, h[3] += d;
}

void ak_md5(const void *p, size_t n, uint8_t out[16]) {
	uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
	uint8_t buf[64];
	size_t bn = 0;
	uint64_t total = 0;
	md_update(buf, &bn, &total, h, md5_block, p, n);
	uint64_t bits = total * 8;
	uint8_t pad[72] = {0x80};
	size_t k = bn < 56 ? 56 - bn : 120 - bn;
	for (int i = 0; i < 8; i++) pad[k + (size_t)i] = (uint8_t)(bits >> (8 * i));
	md_update(buf, &bn, &total, h, md5_block, pad, k + 8);
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(h[i] >> (8 * j));
}

// ---- Hashes by kind, and HMAC (RFC 2104) over them ----

size_t ak_hash_size(int kind) {
	switch (kind) {
	case AK_HASH_SHA1: return 20;
	case AK_HASH_SHA256: return 32;
	case AK_HASH_SHA384: return 48;
	case AK_HASH_SHA512: return 64;
	}
	return 0;
}

static size_t block_size(int kind) { return kind == AK_HASH_SHA384 || kind == AK_HASH_SHA512 ? 128 : 64; }

typedef struct {
	int kind;
	union {
		ak_sha1_ctx s1;
		ak_sha256_ctx s256;
		ak_sha512_ctx s512;
	} u;
} hash_ctx;

static void hash_init(hash_ctx *c, int kind) {
	c->kind = kind;
	switch (kind) {
	case AK_HASH_SHA1: ak_sha1_init(&c->u.s1); break;
	case AK_HASH_SHA256: ak_sha256_init(&c->u.s256); break;
	case AK_HASH_SHA384: ak_sha384_init(&c->u.s512); break;
	default: ak_sha512_init(&c->u.s512); break;
	}
}

static void hash_update(hash_ctx *c, const void *p, size_t n) {
	switch (c->kind) {
	case AK_HASH_SHA1: ak_sha1_update(&c->u.s1, p, n); break;
	case AK_HASH_SHA256: ak_sha256_update(&c->u.s256, p, n); break;
	default: ak_sha512_update(&c->u.s512, p, n); break;
	}
}

// hash_final writes the digest (ak_hash_size bytes) into out.
static void hash_final(hash_ctx *c, uint8_t *out) {
	uint8_t full[64];
	switch (c->kind) {
	case AK_HASH_SHA1: ak_sha1_final(&c->u.s1, out); return;
	case AK_HASH_SHA256: ak_sha256_final(&c->u.s256, out); return;
	case AK_HASH_SHA384:
		ak_sha512_final(&c->u.s512, full);
		memcpy(out, full, 48);
		return;
	default: ak_sha512_final(&c->u.s512, out); return;
	}
}

void ak_hash(int kind, const void *p, size_t n, uint8_t *out) {
	hash_ctx c;
	hash_init(&c, kind);
	hash_update(&c, p, n);
	hash_final(&c, out);
}

// An HMAC key's two states: the hash having taken key ^ ipad, and key ^
// opad. Each message then costs what it is plus two blocks, which
// PBKDF2's iterations need.
typedef struct {
	hash_ctx inner, outer;
} hmac_key;

static void hmac_key_init(hmac_key *k, int kind, const uint8_t *key, size_t n) {
	uint8_t b[128] = {0}, pad[128];
	size_t bs = block_size(kind);
	if (n > bs) ak_hash(kind, key, n, b);
	else if (n) memcpy(b, key, n);
	for (size_t i = 0; i < bs; i++) pad[i] = b[i] ^ 0x36;
	hash_init(&k->inner, kind);
	hash_update(&k->inner, pad, bs);
	for (size_t i = 0; i < bs; i++) pad[i] = b[i] ^ 0x5c;
	hash_init(&k->outer, kind);
	hash_update(&k->outer, pad, bs);
	ak_wipe(b, sizeof b);
	ak_wipe(pad, sizeof pad);
}

// hmac_run: HMAC of the n pieces (p[i], lens[i]) under k, into out.
static void hmac_run(const hmac_key *k, const uint8_t *const *p, const size_t *lens, int n, uint8_t *out) {
	hash_ctx c = k->inner;
	for (int i = 0; i < n; i++) hash_update(&c, p[i], lens[i]);
	uint8_t inner[64];
	hash_final(&c, inner);
	c = k->outer;
	hash_update(&c, inner, ak_hash_size(k->inner.kind));
	hash_final(&c, out);
	ak_wipe(&c, sizeof c);
}

void ak_hmac(int kind, const uint8_t *key, size_t kn, const uint8_t *m, size_t mn, uint8_t *out) {
	hmac_key k;
	hmac_key_init(&k, kind, key, kn);
	hmac_run(&k, &m, &mn, 1, out);
	ak_wipe(&k, sizeof k);
}

void ak_wipe(void *p, size_t n) {
	volatile uint8_t *b = p;
	while (n--) *b++ = 0;
}

// ---- HKDF (RFC 5869) and PBKDF2 (RFC 8018) ----

// ak_hkdf_expand writes n bytes (at most 255 digests) of HKDF-Expand of
// prk and info.
void ak_hkdf_expand(int kind, const uint8_t *prk, size_t pn, const uint8_t *info, size_t in, uint8_t *out, size_t n) {
	hmac_key k;
	hmac_key_init(&k, kind, prk, pn);
	size_t hs = ak_hash_size(kind), tn = 0;
	uint8_t t[64];
	for (uint8_t i = 1; n; i++) {
		const uint8_t *p[3] = {t, info, &i};
		size_t lens[3] = {tn, in, 1};
		hmac_run(&k, p, lens, 3, t);
		tn = hs;
		size_t m = n < hs ? n : hs;
		memcpy(out, t, m);
		out += m, n -= m;
	}
	ak_wipe(t, sizeof t);
	ak_wipe(&k, sizeof k);
}

void ak_pbkdf2(int kind, const uint8_t *pw, size_t pn, const uint8_t *salt, size_t sn, uint64_t iterations, uint8_t *out, size_t n) {
	hmac_key k;
	hmac_key_init(&k, kind, pw, pn);
	size_t hs = ak_hash_size(kind);
	uint8_t u[64], t[64];
	for (uint32_t block = 1; n; block++) {
		uint8_t be[4];
		put_be32(be, block);
		const uint8_t *p[2] = {salt, be};
		size_t lens[2] = {sn, 4};
		hmac_run(&k, p, lens, 2, u);
		memcpy(t, u, hs);
		for (uint64_t i = 1; i < iterations; i++) {
			const uint8_t *q = u;
			hmac_run(&k, &q, &hs, 1, u);
			for (size_t j = 0; j < hs; j++) t[j] ^= u[j];
		}
		size_t m = n < hs ? n : hs;
		memcpy(out, t, m);
		out += m, n -= m;
	}
	ak_wipe(u, sizeof u);
	ak_wipe(t, sizeof t);
	ak_wipe(&k, sizeof k);
}

static const uint8_t *data(const ak_bytes *b) { return b ? b->data : (const uint8_t *)""; }
static size_t len(const ak_bytes *b) { return b ? b->len : 0; }

static ak_val sha256_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *out = ak_bytes_new(NULL, 32);
	ak_sha256(data(BYTES(0)), len(BYTES(0)), out->data);
	return (ak_val){.p = out};
}

static ak_val sha1_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *out = ak_bytes_new(NULL, 20);
	ak_sha1(data(BYTES(0)), len(BYTES(0)), out->data);
	return (ak_val){.p = out};
}

static ak_val sha512_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *out = ak_bytes_new(NULL, 64);
	ak_sha512(data(BYTES(0)), len(BYTES(0)), out->data);
	return (ak_val){.p = out};
}

// ---- Ed25519 ----

// seed_of reads a private key: its 32-byte seed, or Go's 64 bytes (the
// seed, then the public key).
static bool seed_of(const ak_bytes *key, uint8_t seed[32]) {
	if (len(key) != 32 && len(key) != 64) {
		ak_throw_msg("crypto: an Ed25519 private key is 32 or 64 bytes, not %zu", len(key));
		return false;
	}
	memcpy(seed, data(key), 32);
	return true;
}

// ed25519PublicKey(privateKey): its 32-byte public key.
static ak_val ed25519_public_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	uint8_t seed[32];
	if (!seed_of(BYTES(0), seed)) return (ak_val){.p = NULL};
	ak_bytes *out = ak_bytes_new(NULL, 32);
	ak_ed25519_public(seed, out->data);
	return (ak_val){.p = out};
}

// ed25519Sign(privateKey, message): the 64-byte signature.
static ak_val ed25519_sign_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	uint8_t seed[32], pub[32];
	if (!seed_of(BYTES(0), seed)) return (ak_val){.p = NULL};
	ak_ed25519_public(seed, pub);
	ak_bytes *out = ak_bytes_new(NULL, 64);
	ak_ed25519_sign(seed, pub, data(BYTES(1)), len(BYTES(1)), out->data);
	return (ak_val){.p = out};
}

// ed25519Verify(publicKey, message, signature): whether signature is
// the key's of message; false for a key or a signature of the wrong size.
static ak_val ed25519_verify_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (len(BYTES(0)) != 32 || len(BYTES(2)) != 64) return (ak_val){.i = 0};
	return (ak_val){.i = ak_ed25519_verify(data(BYTES(0)), data(BYTES(1)), len(BYTES(1)), data(BYTES(2)))};
}

// ed25519NewKey(): a new private key, 64 bytes as Go's (the seed from the
// system's randomness, then the public key).
static ak_val ed25519_new_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	ak_bytes *out = ak_bytes_new(NULL, 64);
	if (!ak_os_random(out->data, 32)) {
		ak_release(out);
		ak_throw_msg("crypto: the system gave no randomness");
		return (ak_val){.p = NULL};
	}
	ak_ed25519_public(out->data, out->data + 32);
	return (ak_val){.p = out};
}

// hmacSHA256(key, data): HMAC-SHA256 (32 bytes).
static ak_val hmac_sha256(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *out = ak_bytes_new(NULL, 32);
	ak_hmac(AK_HASH_SHA256, data(BYTES(0)), len(BYTES(0)), data(BYTES(1)), len(BYTES(1)), out->data);
	return (ak_val){.p = out};
}

// hash_of is a Hash's code (std/crypto's hashCode), or a throw.
static int hash_of(int64_t k) {
	if (ak_hash_size((int)k) && k >= AK_HASH_SHA1 && k <= AK_HASH_SHA512) return (int)k;
	ak_throw_msg("crypto: no hash %lld", (long long)k);
	return 0;
}

// hmacWith(hash, key, data): HMAC of data under key, a digest long.
static ak_val hmac_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int k = hash_of(args[0].i);
	if (!k) return (ak_val){.p = NULL};
	ak_bytes *out = ak_bytes_new(NULL, ak_hash_size(k));
	ak_hmac(k, data(BYTES(1)), len(BYTES(1)), data(BYTES(2)), len(BYTES(2)), out->data);
	return (ak_val){.p = out};
}

static ak_val sha384_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *out = ak_bytes_new(NULL, 48);
	ak_sha384(data(BYTES(0)), len(BYTES(0)), out->data);
	return (ak_val){.p = out};
}

static ak_val md5_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *out = ak_bytes_new(NULL, 16);
	ak_md5(data(BYTES(0)), len(BYTES(0)), out->data);
	return (ak_val){.p = out};
}

// hkdfExtractWith(hash, salt, secret): the pseudorandom key, HMAC(salt,
// secret); no salt is a digest of zeros (RFC 5869's).
static ak_val hkdf_extract_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int k = hash_of(args[0].i);
	if (!k) return (ak_val){.p = NULL};
	ak_bytes *out = ak_bytes_new(NULL, ak_hash_size(k));
	ak_hmac(k, data(BYTES(1)), len(BYTES(1)), data(BYTES(2)), len(BYTES(2)), out->data);
	return (ak_val){.p = out};
}

// hkdfExpandWith(hash, key, info, length): length bytes (the Asuka side
// keeps it within 255 digests).
static ak_val hkdf_expand_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int k = hash_of(args[0].i);
	if (!k) return (ak_val){.p = NULL};
	int64_t want = args[3].i;
	if (want < 0 || (uint64_t)want > 255 * ak_hash_size(k)) {
		ak_throw_msg("crypto: HKDF gives 0 to %zu bytes, not %lld", 255 * ak_hash_size(k), (long long)want);
		return (ak_val){.p = NULL};
	}
	ak_bytes *out = ak_bytes_new(NULL, (size_t)want);
	ak_hkdf_expand(k, data(BYTES(1)), len(BYTES(1)), data(BYTES(2)), len(BYTES(2)), out->data, (size_t)want);
	return (ak_val){.p = out};
}

// pbkdf2With(hash, password, salt, iterations, length).
static ak_val pbkdf2_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int k = hash_of(args[0].i);
	if (!k) return (ak_val){.p = NULL};
	int64_t it = args[3].i, want = args[4].i;
	if (it < 1 || want < 0 || want > INT32_MAX) {
		ak_throw_msg("crypto: pbkdf2 takes 1 or more iterations and a length of 0 or more");
		return (ak_val){.p = NULL};
	}
	ak_bytes *out = ak_bytes_new(NULL, (size_t)want);
	ak_pbkdf2(k, data(BYTES(1)), len(BYTES(1)), data(BYTES(2)), len(BYTES(2)), (uint64_t)it, out->data, (size_t)want);
	return (ak_val){.p = out};
}

// ---- Randomness ----

bool ak_os_random(void *p, size_t n) {
	uint8_t *b = p;
#if defined(__APPLE__)
	arc4random_buf(b, n);
	return true;
#elif defined(_WIN32)
	while (n) {
		unsigned int v;
		if (rand_s(&v) != 0) return false;
		size_t k = n < sizeof v ? n : sizeof v;
		memcpy(b, &v, k);
		b += k, n -= k;
	}
	return true;
#elif defined(__wasm__)
	while (n) {
		size_t k = n < 256 ? n : 256;  // getentropy's most
		if (getentropy(b, k) != 0) return false;
		b += k, n -= k;
	}
	return true;
#else
	while (n) {
		long r = syscall(SYS_getrandom, b, n, 0);
		if (r < 0) {
			if (errno == EINTR) continue;
			break;  // an old kernel: /dev/urandom
		}
		b += r, n -= (size_t)r;
	}
	if (!n) return true;
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0) return false;
	while (n) {
		ssize_t r = read(fd, b, n);
		if (r <= 0) {
			if (r < 0 && errno == EINTR) continue;
			close(fd);
			return false;
		}
		b += r, n -= (size_t)r;
	}
	close(fd);
	return true;
#endif
}

static ak_val random_bytes(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t k = args[0].i;
	if (k < 0 || k > INT32_MAX) {
		ak_throw_msg("crypto: randomBytes(%lld): the count must be 0 or more", (long long)k);
		return (ak_val){0};
	}
	ak_bytes *out = ak_bytes_new(NULL, (size_t)k);
	if (!ak_os_random(out->data, (size_t)k)) ak_panic("crypto: the system has no randomness to give");
	return (ak_val){.p = out};
}

// uuid: a random (version 4) UUID, lower-case.
static ak_val uuid(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	uint8_t b[16];
	if (!ak_os_random(b, sizeof b)) ak_panic("crypto: the system has no randomness to give");
	b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);
	b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);
	char s[37];
	snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
	return (ak_val){.p = ak_str_new(s, 36)};
}

// equal: a and b compared in time that depends on their length only.
static ak_val equal(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	size_t la = len(BYTES(0)), lb = len(BYTES(1));
	if (la != lb) return (ak_val){.i = 0};
	const volatile uint8_t *a = data(BYTES(0)), *b = data(BYTES(1));
	uint8_t diff = 0;
	for (size_t i = 0; i < la; i++) diff |= a[i] ^ b[i];
	return (ak_val){.i = diff == 0};
}

const ak_lib_entry ak_std_crypto[] = {
	{"crypto.sha256", sha256_fn},
	{"crypto.sha512", sha512_fn},
	{"crypto.sha1", sha1_fn},
	{"crypto.ed25519PublicKey", ed25519_public_fn},
	{"crypto.ed25519Sign", ed25519_sign_fn},
	{"crypto.ed25519Verify", ed25519_verify_fn},
	{"crypto.ed25519NewKey", ed25519_new_fn},
	{"crypto.hmacSHA256", hmac_sha256},
	{"crypto.sha384", sha384_fn},
	{"crypto.md5", md5_fn},
	{"crypto.hmacWith", hmac_fn},
	{"crypto.hkdfExtractWith", hkdf_extract_fn},
	{"crypto.hkdfExpandWith", hkdf_expand_fn},
	{"crypto.pbkdf2With", pbkdf2_fn},
	{"crypto.randomBytes", random_bytes},
	{"crypto.uuid", uuid},
	{"crypto.equal", equal},
	{NULL, NULL},
};
