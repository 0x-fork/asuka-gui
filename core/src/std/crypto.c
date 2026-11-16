// crypto.c: asuka/crypto's builtins: SHA-256 and SHA-1 (FIPS 180-4),
// HMAC (RFC 2104), the system's randomness and version 4 UUIDs.
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

static void sha256_block(void *state, const uint8_t *b) {
	uint32_t *h = state, w[64];
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

void ak_sha256(const void *p, size_t n, uint8_t out[32]) {
	uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	md_pad(p, n, h, sha256_block);
	for (int i = 0; i < 8; i++) put_be32(out + 4 * i, h[i]);
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

// hmacSHA256(key, data): SHA-256(key' ^ opad, SHA-256(key' ^ ipad, data)).
static ak_val hmac_sha256(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	uint8_t k[64] = {0};
	if (len(BYTES(0)) > 64) ak_sha256(data(BYTES(0)), len(BYTES(0)), k);
	else if (len(BYTES(0))) memcpy(k, data(BYTES(0)), len(BYTES(0)));
	size_t dn = len(BYTES(1));
	uint8_t *inner = ak_alloc(64 + dn);
	for (int i = 0; i < 64; i++) inner[i] = k[i] ^ 0x36;
	if (dn) memcpy(inner + 64, data(BYTES(1)), dn);
	uint8_t outer[64 + 32];
	for (int i = 0; i < 64; i++) outer[i] = k[i] ^ 0x5c;
	ak_sha256(inner, 64 + dn, outer + 64);
	ak_free(inner);
	ak_bytes *out = ak_bytes_new(NULL, 32);
	ak_sha256(outer, sizeof outer, out->data);
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
	{"crypto.sha1", sha1_fn},
	{"crypto.hmacSHA256", hmac_sha256},
	{"crypto.randomBytes", random_bytes},
	{"crypto.uuid", uuid},
	{"crypto.equal", equal},
	{NULL, NULL},
};
