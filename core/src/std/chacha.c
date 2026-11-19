// chacha.c: ChaCha20-Poly1305 (RFC 8439) and XChaCha20-Poly1305
// (draft-irtf-cfrg-xchacha) by the CPU's vector unit, for aead.c, which
// runs Monocypher's (third_party/monocypher) where this can't: other
// CPUs (32-bit ARM, the web's WebAssembly) and ak_cryptohw_off (the tests
// compare the two). Byte for byte what Monocypher makes, at every length.
//
// ChaCha20 runs several blocks at once, one register a state word across
// the blocks (each lane a block, as Go's chacha20 for arm64 and
// BoringSSL's ChaCha20_ctr32 do): eight with NEON (arm64, every chip;
// two sets of four, as one set's quarter rounds are a dependency chain
// that leaves the vector pipes half idle), four with SSE2 (every
// x86-64), eight with AVX2 (x86-64, asked at run time).
// Poly1305 is radix 2^64 with 64x64->128-bit multiplies (as Go's
// poly1305 sum_generic.go and poly1305-donna's 64-bit code), on the
// integer unit; it runs stitched into the cipher's rounds, a few of its
// blocks between each double round, so the integer unit's multiply
// chain and the vector unit's rounds go at once (Go's
// chacha20poly1305_amd64.s does the same). Opening authenticates the
// ciphertext as it decrypts it, one pass over the bytes; aead.c wipes
// what was decrypted when the tag doesn't match.
//
// No branch or memory address depends on the key or the data: what
// varies with them is arithmetic only (Poly1305's last subtraction is
// masked, as Monocypher's).
#include "std/std.h"
#include "std/cryptohw.h"
#include "std/monocypher.h"

#if (defined(__aarch64__) || defined(_M_ARM64)) && defined(__SIZEOF_INT128__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define CC_NEON 1
#include <arm_neon.h>
#elif (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__)) && defined(__SIZEOF_INT128__)
#define CC_X86 1
#include <immintrin.h>
#endif

#if CC_NEON || CC_X86

typedef unsigned __int128 u128;

static inline uint64_t le64(const uint8_t *p) {
	uint64_t v;
	memcpy(&v, p, 8);
	return v;
}

static inline uint32_t le32(const uint8_t *p) {
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}

// ---- Poly1305, radix 2^64 ----
//
// h is h0 + h1·2^64 + h2·2^128 (h2 small), r is r0 + r1·2^64, clamped:
// so h·r's partial products fit, and 2^130 ≡ 5 folds the top back in
// as (t >> 130)·5 = (t >> 128 & ~3) + (t >> 130).

typedef struct {
	uint64_t h0, h1, h2, r0, r1, s0, s1;
} poly;

static void poly_init(poly *p, const uint8_t key[32]) {
	p->r0 = le64(key) & UINT64_C(0x0ffffffc0fffffff);
	p->r1 = le64(key + 8) & UINT64_C(0x0ffffffc0ffffffc);
	p->s0 = le64(key + 16);
	p->s1 = le64(key + 24);
	p->h0 = p->h1 = p->h2 = 0;
}

// One full 16-byte block (its 2^128 bit set), into h.
#define POLY_BLOCK(h0, h1, h2, r0, r1, m) \
	do { \
		u128 t_ = (u128)h0 + le64(m); \
		h0 = (uint64_t)t_; \
		t_ = (u128)h1 + le64((m) + 8) + (uint64_t)(t_ >> 64); \
		h1 = (uint64_t)t_; \
		h2 += (uint64_t)(t_ >> 64) + 1; \
		u128 m0_ = (u128)h0 * r0, m1_ = (u128)h1 * r0 + (u128)h0 * r1, m2_ = (u128)h1 * r1 + (u128)(h2 * r0); \
		uint64_t m3_ = h2 * r1; \
		u128 c_ = (u128)(uint64_t)m1_ + (uint64_t)(m0_ >> 64); \
		uint64_t t1_ = (uint64_t)c_; \
		c_ = (u128)(uint64_t)m2_ + (uint64_t)(m1_ >> 64) + (uint64_t)(c_ >> 64); \
		uint64_t t2_ = (uint64_t)c_, t3_ = m3_ + (uint64_t)(m2_ >> 64) + (uint64_t)(c_ >> 64); \
		uint64_t k0_ = t2_ & ~UINT64_C(3), k1_ = t3_; \
		h0 = (uint64_t)m0_, h1 = t1_, h2 = t2_ & 3; \
		t_ = (u128)h0 + k0_; \
		h0 = (uint64_t)t_; \
		t_ = (u128)h1 + k1_ + (uint64_t)(t_ >> 64); \
		h1 = (uint64_t)t_; \
		h2 += (uint64_t)(t_ >> 64); \
		k0_ = k0_ >> 2 | k1_ << 62, k1_ >>= 2; \
		t_ = (u128)h0 + k0_; \
		h0 = (uint64_t)t_; \
		t_ = (u128)h1 + k1_ + (uint64_t)(t_ >> 64); \
		h1 = (uint64_t)t_; \
		h2 += (uint64_t)(t_ >> 64); \
	} while (0)

static void poly_blocks(poly *p, const uint8_t *m, size_t blocks) {
	uint64_t h0 = p->h0, h1 = p->h1, h2 = p->h2, r0 = p->r0, r1 = p->r1;
	for (; blocks--; m += 16) POLY_BLOCK(h0, h1, h2, r0, r1, m);
	p->h0 = h0, p->h1 = h1, p->h2 = h2;
}

// poly_padded: n bytes, the last block padded with zeros to 16 (as the
// AEAD pads the additional data and the ciphertext).
static void poly_padded(poly *p, const uint8_t *m, size_t n) {
	poly_blocks(p, m, n / 16);
	if (n % 16) {
		uint8_t last[16] = {0};
		memcpy(last, m + n / 16 * 16, n % 16);
		poly_blocks(p, last, 1);
	}
}

static void poly_final(poly *p, uint8_t tag[16]) {
	// h - (2^130 - 5): kept when it doesn't borrow, h is the smaller.
	uint64_t h0 = p->h0, h1 = p->h1, h2 = p->h2;
	u128 t = (u128)h0 + 5;
	uint64_t g0 = (uint64_t)t;
	t = (u128)h1 + (uint64_t)(t >> 64);
	uint64_t g1 = (uint64_t)t;
	uint64_t g2 = h2 + (uint64_t)(t >> 64);
	uint64_t keep = (uint64_t)0 - (g2 >> 2);  // all ones when h + 5 reached 2^130
	h0 = (g0 & keep) | (h0 & ~keep);
	h1 = (g1 & keep) | (h1 & ~keep);
	t = (u128)h0 + p->s0;
	h0 = (uint64_t)t;
	h1 = h1 + p->s1 + (uint64_t)(t >> 64);
	memcpy(tag, &h0, 8);
	memcpy(tag + 8, &h1, 8);
	ak_wipe(p, sizeof *p);
}

// ---- ChaCha20, one block ----

#define ROTL(x, n) ((x) << (n) | (x) >> (32 - (n)))
#define QR(a, b, c, d) \
	a += b, d = ROTL(d ^ a, 16), c += d, b = ROTL(b ^ c, 12), a += b, d = ROTL(d ^ a, 8), c += d, b = ROTL(b ^ c, 7)

// block: the keystream block of st into out.
static void block(const uint32_t st[16], uint8_t out[64]) {
	uint32_t x[16];
	memcpy(x, st, sizeof x);
	for (int i = 0; i < 10; i++) {
		QR(x[0], x[4], x[8], x[12]), QR(x[1], x[5], x[9], x[13]), QR(x[2], x[6], x[10], x[14]), QR(x[3], x[7], x[11], x[15]);
		QR(x[0], x[5], x[10], x[15]), QR(x[1], x[6], x[11], x[12]), QR(x[2], x[7], x[8], x[13]), QR(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++) x[i] += st[i];
	memcpy(out, x, 64);
	ak_wipe(x, sizeof x);
}

// set_key: "expand 32-byte k", then the key, as the state's first words.
static void set_key(uint32_t st[16], const uint8_t key[32]) {
	st[0] = 0x61707865, st[1] = 0x3320646e, st[2] = 0x79622d32, st[3] = 0x6b206574;
	for (int i = 0; i < 8; i++) st[4 + i] = le32(key + 4 * i);
}

// next: st's 64-bit block counter (words 12 and 13) plus one, as
// Monocypher's (and so the IETF counter's carry into the nonce's first
// word, 256 GiB in, is Monocypher's too).
static inline void next(uint32_t st[16]) {
	if (++st[12] == 0) st[13]++;
}

// advance: the counter plus b, which may carry (the chunk before
// reached the low word's last value).
static inline void advance(uint32_t st[16], uint32_t b) {
	st[12] += b;
	st[13] += st[12] < b;
}

// Poly1305's blocks stitched into a chunk's double rounds: of pb in all,
// round i's share.
#define POLY_SHARE(i, pb) ((pb) * ((size_t)(i) + 1) / 10 - (pb) * (size_t)(i) / 10)

// A chunk: B blocks' keystream from st (its counter word plus 0 … B-1,
// no carry: the caller sees to it) over in into out, and pb blocks of
// Poly1305 at pm into p. in, out and pm are B·64 bytes (pm: pb·16).
typedef void chunk_fn(const uint32_t st[16], const uint8_t *in, uint8_t *out, poly *p, const uint8_t *pm, size_t pb);

// ---- NEON: eight blocks ----
#if CC_NEON

#define V uint32x4_t
#define ADD vaddq_u32
#define XOR veorq_u32
#define R16(x) vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32(x)))
#define R8(x) vreinterpretq_u32_u8(vqtbl1q_u8(vreinterpretq_u8_u32(x), rot8))
#define RN(x, n) vsriq_n_u32(vshlq_n_u32(x, n), x, 32 - (n))
#define VQR(a, b, c, d) \
	a = ADD(a, b), d = XOR(d, a), d = R16(d), c = ADD(c, d), b = XOR(b, c), b = RN(b, 12), \
	a = ADD(a, b), d = XOR(d, a), d = R8(d), c = ADD(c, d), b = XOR(b, c), b = RN(b, 7)

// out = in ^ the four blocks x holds (lane j block j): a 4x4 transpose
// of each four words.
static inline void store_neon(const uint32x4_t x[16], const uint8_t *in, uint8_t *out) {
	for (int g = 0; g < 4; g++) {
		V a0 = x[4 * g], a1 = x[4 * g + 1], a2 = x[4 * g + 2], a3 = x[4 * g + 3];
		uint64x2_t u0 = vreinterpretq_u64_u32(vtrn1q_u32(a0, a1)), u1 = vreinterpretq_u64_u32(vtrn2q_u32(a0, a1)),
			   u2 = vreinterpretq_u64_u32(vtrn1q_u32(a2, a3)), u3 = vreinterpretq_u64_u32(vtrn2q_u32(a2, a3));
		uint8x16_t b[4] = {
			vreinterpretq_u8_u64(vtrn1q_u64(u0, u2)),
			vreinterpretq_u8_u64(vtrn1q_u64(u1, u3)),
			vreinterpretq_u8_u64(vtrn2q_u64(u0, u2)),
			vreinterpretq_u8_u64(vtrn2q_u64(u1, u3)),
		};
		for (int j = 0; j < 4; j++) vst1q_u8(out + 64 * j + 16 * g, veorq_u8(vld1q_u8(in + 64 * j + 16 * g), b[j]));
	}
}

// Eight blocks, two sets of four whose rounds interleave: one set's
// quarter rounds are a chain of dependent instructions too short to
// keep the vector unit's pipes full.
static void chunk_neon(const uint32_t st[16], const uint8_t *in, uint8_t *out, poly *p, const uint8_t *pm, size_t pb) {
	static const uint8_t rot8_[16] = {3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14};
	static const uint32_t lanes[4] = {0, 1, 2, 3};
	const uint8x16_t rot8 = vld1q_u8(rot8_);
	// Named, not an array: the compiler keeps each in a register.
	V x0 = vdupq_n_u32(st[0]), x1 = vdupq_n_u32(st[1]), x2 = vdupq_n_u32(st[2]), x3 = vdupq_n_u32(st[3]);
	V x4 = vdupq_n_u32(st[4]), x5 = vdupq_n_u32(st[5]), x6 = vdupq_n_u32(st[6]), x7 = vdupq_n_u32(st[7]);
	V x8 = vdupq_n_u32(st[8]), x9 = vdupq_n_u32(st[9]), x10 = vdupq_n_u32(st[10]), x11 = vdupq_n_u32(st[11]);
	V x12 = ADD(vdupq_n_u32(st[12]), vld1q_u32(lanes)), x13 = vdupq_n_u32(st[13]), x14 = vdupq_n_u32(st[14]), x15 = vdupq_n_u32(st[15]);
	V y0 = x0, y1 = x1, y2 = x2, y3 = x3, y4 = x4, y5 = x5, y6 = x6, y7 = x7, y8 = x8, y9 = x9, y10 = x10, y11 = x11;
	V y12 = ADD(x12, vdupq_n_u32(4)), y13 = x13, y14 = x14, y15 = x15;
	const V cx = x12, cy = y12;
	uint64_t h0 = p->h0, h1 = p->h1, h2 = p->h2, r0 = p->r0, r1 = p->r1;
	for (int i = 0; i < 10; i++) {
		VQR(x0, x4, x8, x12), VQR(y0, y4, y8, y12), VQR(x1, x5, x9, x13), VQR(y1, y5, y9, y13);
		VQR(x2, x6, x10, x14), VQR(y2, y6, y10, y14), VQR(x3, x7, x11, x15), VQR(y3, y7, y11, y15);
		VQR(x0, x5, x10, x15), VQR(y0, y5, y10, y15), VQR(x1, x6, x11, x12), VQR(y1, y6, y11, y12);
		VQR(x2, x7, x8, x13), VQR(y2, y7, y8, y13), VQR(x3, x4, x9, x14), VQR(y3, y4, y9, y14);
		for (size_t k = POLY_SHARE(i, pb); k--; pm += 16) POLY_BLOCK(h0, h1, h2, r0, r1, pm);
	}
	p->h0 = h0, p->h1 = h1, p->h2 = h2;
	V xs[16] = {x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15};
	V ys[16] = {y0, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10, y11, y12, y13, y14, y15};
	for (int i = 0; i < 16; i++) {
		V s = vdupq_n_u32(st[i]);
		xs[i] = ADD(xs[i], i == 12 ? cx : s), ys[i] = ADD(ys[i], i == 12 ? cy : s);
	}
	store_neon(xs, in, out);
	store_neon(ys, in + 256, out + 256);
}

#undef V
#undef ADD
#undef XOR
#endif

// ---- x86-64: SSE2, four blocks; AVX2, eight ----
#if CC_X86

#define V __m128i
#define ADD _mm_add_epi32
#define XOR _mm_xor_si128
#define R16(x) _mm_shufflehi_epi16(_mm_shufflelo_epi16(x, 0xb1), 0xb1)
#define RN(x, n) _mm_or_si128(_mm_slli_epi32(x, n), _mm_srli_epi32(x, 32 - (n)))
#define VQR(a, b, c, d) \
	a = ADD(a, b), d = XOR(d, a), d = R16(d), c = ADD(c, d), b = XOR(b, c), b = RN(b, 12), \
	a = ADD(a, b), d = XOR(d, a), d = RN(d, 8), c = ADD(c, d), b = XOR(b, c), b = RN(b, 7)

static void chunk_sse2(const uint32_t st[16], const uint8_t *in, uint8_t *out, poly *p, const uint8_t *pm, size_t pb) {
	V x[16];
	for (int i = 0; i < 16; i++) x[i] = _mm_set1_epi32((int)st[i]);
	V ctr = ADD(x[12], _mm_set_epi32(3, 2, 1, 0));
	x[12] = ctr;
	uint64_t h0 = p->h0, h1 = p->h1, h2 = p->h2, r0 = p->r0, r1 = p->r1;
	for (int i = 0; i < 10; i++) {
		VQR(x[0], x[4], x[8], x[12]), VQR(x[1], x[5], x[9], x[13]), VQR(x[2], x[6], x[10], x[14]), VQR(x[3], x[7], x[11], x[15]);
		VQR(x[0], x[5], x[10], x[15]), VQR(x[1], x[6], x[11], x[12]), VQR(x[2], x[7], x[8], x[13]), VQR(x[3], x[4], x[9], x[14]);
		for (size_t k = POLY_SHARE(i, pb); k--; pm += 16) POLY_BLOCK(h0, h1, h2, r0, r1, pm);
	}
	p->h0 = h0, p->h1 = h1, p->h2 = h2;
	for (int i = 0; i < 16; i++) x[i] = ADD(x[i], i == 12 ? ctr : _mm_set1_epi32((int)st[i]));
	for (int g = 0; g < 4; g++) {
		V a0 = x[4 * g], a1 = x[4 * g + 1], a2 = x[4 * g + 2], a3 = x[4 * g + 3];
		V t0 = _mm_unpacklo_epi32(a0, a1), t1 = _mm_unpacklo_epi32(a2, a3), t2 = _mm_unpackhi_epi32(a0, a1), t3 = _mm_unpackhi_epi32(a2, a3);
		V b[4] = {_mm_unpacklo_epi64(t0, t1), _mm_unpackhi_epi64(t0, t1), _mm_unpacklo_epi64(t2, t3), _mm_unpackhi_epi64(t2, t3)};
		for (int j = 0; j < 4; j++) {
			const uint8_t *s = in + 64 * j + 16 * g;
			uint8_t *d = out + 64 * j + 16 * g;
			_mm_storeu_si128((V *)(void *)d, XOR(_mm_loadu_si128((const V *)(const void *)s), b[j]));
		}
	}
}

#undef V
#undef ADD
#undef XOR
#undef R16
#undef RN
#undef VQR

#define T_AVX2 __attribute__((target("avx2")))
#define V __m256i
#define ADD _mm256_add_epi32
#define XOR _mm256_xor_si256
#define R16(x) _mm256_shuffle_epi8(x, rot16)
#define R8(x) _mm256_shuffle_epi8(x, rot8)
#define RN(x, n) _mm256_or_si256(_mm256_slli_epi32(x, n), _mm256_srli_epi32(x, 32 - (n)))
#define VQR(a, b, c, d) \
	a = ADD(a, b), d = XOR(d, a), d = R16(d), c = ADD(c, d), b = XOR(b, c), b = RN(b, 12), \
	a = ADD(a, b), d = XOR(d, a), d = R8(d), c = ADD(c, d), b = XOR(b, c), b = RN(b, 7)

T_AVX2 static void chunk_avx2(const uint32_t st[16], const uint8_t *in, uint8_t *out, poly *p, const uint8_t *pm, size_t pb) {
	const V rot16 = _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2, 13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
	const V rot8 = _mm256_set_epi8(14, 13, 12, 15, 10, 9, 8, 11, 6, 5, 4, 7, 2, 1, 0, 3, 14, 13, 12, 15, 10, 9, 8, 11, 6, 5, 4, 7, 2, 1, 0, 3);
	V x[16];
	for (int i = 0; i < 16; i++) x[i] = _mm256_set1_epi32((int)st[i]);
	V ctr = ADD(x[12], _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0));
	x[12] = ctr;
	uint64_t h0 = p->h0, h1 = p->h1, h2 = p->h2, r0 = p->r0, r1 = p->r1;
	for (int i = 0; i < 10; i++) {
		VQR(x[0], x[4], x[8], x[12]), VQR(x[1], x[5], x[9], x[13]), VQR(x[2], x[6], x[10], x[14]), VQR(x[3], x[7], x[11], x[15]);
		VQR(x[0], x[5], x[10], x[15]), VQR(x[1], x[6], x[11], x[12]), VQR(x[2], x[7], x[8], x[13]), VQR(x[3], x[4], x[9], x[14]);
		for (size_t k = POLY_SHARE(i, pb); k--; pm += 16) POLY_BLOCK(h0, h1, h2, r0, r1, pm);
	}
	p->h0 = h0, p->h1 = h1, p->h2 = h2;
	for (int i = 0; i < 16; i++) x[i] = ADD(x[i], i == 12 ? ctr : _mm256_set1_epi32((int)st[i]));
	// Each 128-bit half a 4x4 transpose (blocks 0-3 low, 4-7 high), then
	// the halves of two word groups put together: a block's 32 bytes.
	V b[4][4];
	for (int g = 0; g < 4; g++) {
		V a0 = x[4 * g], a1 = x[4 * g + 1], a2 = x[4 * g + 2], a3 = x[4 * g + 3];
		V t0 = _mm256_unpacklo_epi32(a0, a1), t1 = _mm256_unpacklo_epi32(a2, a3), t2 = _mm256_unpackhi_epi32(a0, a1),
		  t3 = _mm256_unpackhi_epi32(a2, a3);
		b[g][0] = _mm256_unpacklo_epi64(t0, t1), b[g][1] = _mm256_unpackhi_epi64(t0, t1);
		b[g][2] = _mm256_unpacklo_epi64(t2, t3), b[g][3] = _mm256_unpackhi_epi64(t2, t3);
	}
	for (int j = 0; j < 4; j++)
		for (int g = 0; g < 4; g += 2) {
			V lo = _mm256_permute2x128_si256(b[g][j], b[g + 1][j], 0x20), hi = _mm256_permute2x128_si256(b[g][j], b[g + 1][j], 0x31);
			const uint8_t *s0 = in + 64 * j + 16 * g, *s1 = in + 64 * (j + 4) + 16 * g;
			uint8_t *d0 = out + 64 * j + 16 * g, *d1 = out + 64 * (j + 4) + 16 * g;
			_mm256_storeu_si256((V *)(void *)d0, XOR(_mm256_loadu_si256((const V *)(const void *)s0), lo));
			_mm256_storeu_si256((V *)(void *)d1, XOR(_mm256_loadu_si256((const V *)(const void *)s1), hi));
		}
}

#undef V
#undef ADD
#undef XOR
#undef R16
#undef R8
#undef RN
#undef VQR
#endif

// run: ChaCha20 from st over in into out (n bytes), and Poly1305 over
// the ciphertext (out sealing, in opening) into p, its last block padded
// with zeros. Sealing, Poly1305 reads a chunk behind the cipher, what it
// has written; opening, the chunk the cipher is reading.
static void run(chunk_fn *chunk, size_t B, uint32_t st[16], bool encrypt, const uint8_t *in, uint8_t *out, size_t n, poly *p) {
	const size_t C = 64 * B;
	const uint8_t *ct = encrypt ? out : in;
	size_t done = 0, polled = 0;
	uint8_t buf[512];
	while (done < n) {
		size_t m = n - done < C ? n - done : C;
		size_t ready = encrypt ? done : done + m;
		size_t pb = (ready - polled) / 16;
		if (st[12] > UINT32_MAX - (B - 1)) {
			// The counter's low word would wrap within the chunk: block by
			// block, the carry taken.
			poly_blocks(p, ct + polled, pb);
			polled += 16 * pb;
			for (size_t i = 0; i < m; i += 64, next(st)) {
				block(st, buf);
				size_t k = m - i < 64 ? m - i : 64;
				for (size_t j = 0; j < k; j++) out[done + i + j] = in[done + i + j] ^ buf[j];
			}
		} else if (m == C) {
			chunk(st, in + done, out + done, p, ct + polled, pb);
			polled += 16 * pb;
			advance(st, (uint32_t)B);
		} else {
			// The last, short chunk, through the buffer.
			memset(buf, 0, C);
			memcpy(buf, in + done, m);
			chunk(st, buf, buf, p, ct + polled, pb);
			polled += 16 * pb;
			memcpy(out + done, buf, m);
		}
		done += m;
	}
	if (polled < n) poly_padded(p, ct + polled, n - polled);
	ak_wipe(buf, sizeof buf);
}

bool ak_chacha_poly_hw(bool encrypt, const uint8_t key[32], const uint8_t *nonce, size_t nn, const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]) {
	if (ak_cryptohw_off) return false;
	chunk_fn *chunk;
	size_t B;
#if CC_NEON
	chunk = chunk_neon, B = 8;
#else
	if (ak_cpu_avx2() && !ak_cryptohw_sse2) chunk = chunk_avx2, B = 8;
	else chunk = chunk_sse2, B = 4;
#endif
	uint32_t st[16];
	uint8_t sub[32];
	const uint8_t *k = key, *iv;
	if (nn == 24) {
		// XChaCha20: HChaCha20 (one block: Monocypher's) makes the key from
		// the nonce's first 16 bytes; its last 8 are the nonce, the
		// counter's 64 bits from 0.
		crypto_chacha20_h(sub, key, nonce);
		k = sub, iv = nonce + 16;
		st[12] = st[13] = 0;
	} else {
		// RFC 8439: a 32-bit counter from 0, then the nonce's 96 bits.
		iv = nonce + 4;
		st[12] = 0, st[13] = le32(nonce);
	}
	set_key(st, k);
	st[14] = le32(iv), st[15] = le32(iv + 4);
	// Block 0's first 32 bytes are Poly1305's key; the data's start at 1.
	uint8_t first[64];
	block(st, first);
	next(st);
	poly p;
	poly_init(&p, first);
	poly_padded(&p, ad, an);
	run(chunk, B, st, encrypt, in, out, n, &p);
	uint8_t lens[16];
	uint64_t a64 = an, n64 = n;
	memcpy(lens, &a64, 8);
	memcpy(lens + 8, &n64, 8);
	poly_blocks(&p, lens, 1);
	poly_final(&p, tag);
	ak_wipe(st, sizeof st);
	ak_wipe(sub, sizeof sub);
	ak_wipe(first, sizeof first);
	return true;
}

// ak_chacha20_hw: ChaCha20 with Monocypher's crypto_chacha20_djb's
// arguments (a 64-bit counter from ctr), by the vector unit, for the
// tests: a counter's carry out of its low word, 256 GiB into a message,
// is reached only so.
bool ak_chacha20_hw(const uint8_t key[32], const uint8_t nonce[8], uint64_t ctr, const uint8_t *in, uint8_t *out, size_t n) {
	if (ak_cryptohw_off) return false;
	uint32_t st[16];
	set_key(st, key);
	st[12] = (uint32_t)ctr, st[13] = (uint32_t)(ctr >> 32), st[14] = le32(nonce), st[15] = le32(nonce + 4);
	uint8_t zero[32] = {0};
	poly p;
	poly_init(&p, zero);
#if CC_NEON
	run(chunk_neon, 8, st, true, in, out, n, &p);
#else
	if (ak_cpu_avx2() && !ak_cryptohw_sse2) run(chunk_avx2, 8, st, true, in, out, n, &p);
	else run(chunk_sse2, 4, st, true, in, out, n, &p);
#endif
	ak_wipe(st, sizeof st);
	return true;
}

#else

bool ak_chacha20_hw(const uint8_t key[32], const uint8_t nonce[8], uint64_t ctr, const uint8_t *in, uint8_t *out, size_t n) {
	AK_UNUSED(key), AK_UNUSED(nonce), AK_UNUSED(ctr), AK_UNUSED(in), AK_UNUSED(out), AK_UNUSED(n);
	return false;
}

bool ak_chacha_poly_hw(bool encrypt, const uint8_t key[32], const uint8_t *nonce, size_t nn, const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]) {
	AK_UNUSED(encrypt), AK_UNUSED(key), AK_UNUSED(nonce), AK_UNUSED(nn), AK_UNUSED(ad), AK_UNUSED(an);
	AK_UNUSED(in), AK_UNUSED(out), AK_UNUSED(n), AK_UNUSED(tag);
	return false;
}

#endif
