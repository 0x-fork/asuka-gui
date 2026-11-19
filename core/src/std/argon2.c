// argon2.c: Argon2 (RFC 9106), what Monocypher's crypto_argon2
// computes (its arguments, its bytes) but faster, two ways:
//
// Lanes in parallel. Within a slice each lane's segment depends only on
// its own lane and on slices already finished, so the lanes of a slice
// run on threads at once (RFC 9106 3.4; the reference implementation,
// libsodium and Go's x/crypto/argon2 do the same), and all of them finish
// before the next slice begins. The thread that asks hashes too; where
// threads can't be had (the web's WebAssembly) it hashes every lane
// itself, the same bytes. A hash's threads are its own, started for it
// and gone when it returns.
//
// The compression function G on the vector unit: BLAKE2b's rounds with
// BlaMka's multiply, the 1 KiB block as 64 pairs of 64-bit words, as
// libsodium's argon2-fill-block-ssse3.c and -avx2.c lay it out: NEON on
// arm64 (every chip), SSE2 on x86-64 (every chip), AVX2 there when the
// CPU has it (asked at run time); the portable C (Monocypher's rounds)
// elsewhere and with ak_cryptohw_off, which the tests compare.
//
// Argon2i, and argon2id's first half pass, choose blocks by data that
// doesn't depend on the password (a counter's blocks); argon2d and
// argon2id after choose them by the data, as the algorithm means to.
// Nothing else varies with the password: no branch, no table.
#include "std/std.h"
#include "std/argon2.h"
#include "std/cryptohw.h"
#include "base/thread.h"

#if !defined(_WIN32) && !defined(__wasm__)
#include <unistd.h>
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define A2_NEON 1
#include <arm_neon.h>
#elif (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
#define A2_X86 1
#include <immintrin.h>
#endif

typedef struct {
	uint64_t v[128];
} blk;

// A fill makes block cur from prev and ref: G(prev, ref), XORed into
// what cur held (passes after the first) or written over it. Its
// working copies are in tmp (two blocks), which its caller wipes once
// it's done with them all.
typedef void fill_fn(blk *cur, const blk *prev, const blk *ref, bool xor_, blk tmp[2]);

// ---- The portable C ----

static inline uint64_t rotr64(uint64_t x, int n) { return x >> n | x << (64 - n); }

#define LSB(x) ((uint64_t)(uint32_t)(x))
#define GB(a, b, c, d) \
	a += b + ((LSB(a) * LSB(b)) << 1), d = rotr64(d ^ a, 32), c += d + ((LSB(c) * LSB(d)) << 1), b = rotr64(b ^ c, 24), \
	a += b + ((LSB(a) * LSB(b)) << 1), d = rotr64(d ^ a, 16), c += d + ((LSB(c) * LSB(d)) << 1), b = rotr64(b ^ c, 63)
#define P(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15) \
	GB(v0, v4, v8, v12), GB(v1, v5, v9, v13), GB(v2, v6, v10, v14), GB(v3, v7, v11, v15), GB(v0, v5, v10, v15), \
		GB(v1, v6, v11, v12), GB(v2, v7, v8, v13), GB(v3, v4, v9, v14)

static void fill_c(blk *cur, const blk *prev, const blk *ref, bool xor_, blk tmp[2]) {
	uint64_t *r = tmp[0].v, *a = tmp[1].v;
	for (int i = 0; i < 128; i++) r[i] = a[i] = prev->v[i] ^ ref->v[i];
	for (int i = 0; i < 128; i += 16)
		P(a[i], a[i + 1], a[i + 2], a[i + 3], a[i + 4], a[i + 5], a[i + 6], a[i + 7], a[i + 8], a[i + 9], a[i + 10], a[i + 11],
			a[i + 12], a[i + 13], a[i + 14], a[i + 15]);
	for (int i = 0; i < 16; i += 2)
		P(a[i], a[i + 1], a[i + 16], a[i + 17], a[i + 32], a[i + 33], a[i + 48], a[i + 49], a[i + 64], a[i + 65], a[i + 80],
			a[i + 81], a[i + 96], a[i + 97], a[i + 112], a[i + 113]);
	if (xor_)
		for (int i = 0; i < 128; i++) cur->v[i] ^= r[i] ^ a[i];
	else
		for (int i = 0; i < 128; i++) cur->v[i] = r[i] ^ a[i];
}

// ---- 128-bit vectors (NEON, SSE2): the block as 64 of them ----
//
// BLAKE2b's 16 words of a round in 8 vectors, two words each: A0 A1 B0
// B1 C0 C1 D0 D1 (a's words 0-3, b's 4-7, ...). The column step is G on
// (A0 B0 C0 D0) and (A1 B1 C1 D1); the diagonal step the same after
// rotating b, c and d a word along (HILO: one vector's high word and the
// next's low).

#if A2_NEON

typedef uint64x2_t Q;
#define QXOR veorq_u64
// BlaMka, x + y + 2·lo32(x)·lo32(y), on two pairs: their low words
// gathered two vectors to one (uzp1), multiplied a half at a time.
#define BLA2(X0, Y0, X1, Y1) \
	do { \
		uint32x4_t u_ = vuzp1q_u32(vreinterpretq_u32_u64(X0), vreinterpretq_u32_u64(X1)); \
		uint32x4_t v_ = vuzp1q_u32(vreinterpretq_u32_u64(Y0), vreinterpretq_u32_u64(Y1)); \
		Q p0_ = vmull_u32(vget_low_u32(u_), vget_low_u32(v_)), p1_ = vmull_high_u32(u_, v_); \
		X0 = vaddq_u64(vaddq_u64(X0, Y0), vaddq_u64(p0_, p0_)); \
		X1 = vaddq_u64(vaddq_u64(X1, Y1), vaddq_u64(p1_, p1_)); \
	} while (0)
#define R32(x) vreinterpretq_u64_u32(vrev64q_u32(vreinterpretq_u32_u64(x)))
#define R24(x) vreinterpretq_u64_u8(vqtbl1q_u8(vreinterpretq_u8_u64(x), r24))
#define R16(x) vreinterpretq_u64_u8(vqtbl1q_u8(vreinterpretq_u8_u64(x), r16))
#define R63(x) vsriq_n_u64(vaddq_u64(x, x), x, 63)
#define HILO(x, y) vextq_u64(x, y, 1)
#define QLOAD(p) vld1q_u64(p)
#define QSTORE(p, x) vst1q_u64(p, x)
#define QTABLES \
	static const uint8_t r24_[16] = {3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10}; \
	static const uint8_t r16_[16] = {2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9}; \
	const uint8x16_t r24 = vld1q_u8(r24_), r16 = vld1q_u8(r16_);

#elif A2_X86

typedef __m128i Q;
#define QXOR _mm_xor_si128
static inline Q bla(Q x, Q y) {
	Q p = _mm_mul_epu32(x, y);
	return _mm_add_epi64(_mm_add_epi64(x, y), _mm_add_epi64(p, p));
}
#define BLA2(X0, Y0, X1, Y1) X0 = bla(X0, Y0), X1 = bla(X1, Y1)
#define R32(x) _mm_shuffle_epi32(x, _MM_SHUFFLE(2, 3, 0, 1))
#define R24(x) _mm_or_si128(_mm_srli_epi64(x, 24), _mm_slli_epi64(x, 40))
#define R16(x) _mm_shufflehi_epi16(_mm_shufflelo_epi16(x, _MM_SHUFFLE(0, 3, 2, 1)), _MM_SHUFFLE(0, 3, 2, 1))
#define R63(x) _mm_xor_si128(_mm_srli_epi64(x, 63), _mm_add_epi64(x, x))
#define HILO(x, y) _mm_castpd_si128(_mm_shuffle_pd(_mm_castsi128_pd(x), _mm_castsi128_pd(y), 1))
#define QLOAD(p) _mm_loadu_si128((const __m128i *)(const void *)(p))
#define QSTORE(p, x) _mm_storeu_si128((__m128i *)(void *)(p), x)
#define QTABLES

#endif

#if A2_NEON || A2_X86

// XRn(d, a): d ^ a rotated right n bits.
#define XR32(d, a) R32(QXOR(d, a))
#define XR24(d, a) R24(QXOR(d, a))
#define XR16(d, a) R16(QXOR(d, a))
#define XR63(d, a) R63(QXOR(d, a))
#define QG(A0, B0, C0, D0, A1, B1, C1, D1, XRD, XRB) \
	BLA2(A0, B0, A1, B1); \
	D0 = XRD(D0, A0), D1 = XRD(D1, A1); \
	BLA2(C0, D0, C1, D1); \
	B0 = XRB(B0, C0), B1 = XRB(B1, C1)

#define QROUND(A0, A1, B0, B1, C0, C1, D0, D1) \
	do { \
		QG(A0, B0, C0, D0, A1, B1, C1, D1, XR32, XR24); \
		QG(A0, B0, C0, D0, A1, B1, C1, D1, XR16, XR63); \
		Q t0_ = HILO(B0, B1), t1_ = HILO(B1, B0); \
		B0 = t0_, B1 = t1_, t0_ = C0, C0 = C1, C1 = t0_; \
		t0_ = HILO(D1, D0), t1_ = HILO(D0, D1); \
		D0 = t0_, D1 = t1_; \
		QG(A0, B0, C0, D0, A1, B1, C1, D1, XR32, XR24); \
		QG(A0, B0, C0, D0, A1, B1, C1, D1, XR16, XR63); \
		t0_ = HILO(B1, B0), t1_ = HILO(B0, B1); \
		B0 = t0_, B1 = t1_, t0_ = C0, C0 = C1, C1 = t0_; \
		t0_ = HILO(D0, D1), t1_ = HILO(D1, D0); \
		D0 = t0_, D1 = t1_; \
	} while (0)

// QFILL_BODY: a fill on 128-bit vectors (QROUND's macros as they're
// defined where it's expanded).
#define QFILL_BODY \
	QTABLES \
	Q *s = (Q *)(void *)tmp[0].v, *t = (Q *)(void *)tmp[1].v; \
	for (int i = 0; i < 64; i++) { \
		s[i] = QXOR(QLOAD(prev->v + 2 * i), QLOAD(ref->v + 2 * i)); \
		t[i] = xor_ ? QXOR(s[i], QLOAD(cur->v + 2 * i)) : s[i]; \
	} \
	/* The rows (each 16 words in order), then the columns (two words of each row). */ \
	for (int i = 0; i < 64; i += 8) QROUND(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5], s[i + 6], s[i + 7]); \
	for (int i = 0; i < 8; i++) QROUND(s[i], s[i + 8], s[i + 16], s[i + 24], s[i + 32], s[i + 40], s[i + 48], s[i + 56]); \
	for (int i = 0; i < 64; i++) QSTORE(cur->v + 2 * i, QXOR(s[i], t[i]));

#if A2_NEON
static void fill_neon(blk *cur, const blk *prev, const blk *ref, bool xor_, blk tmp[2]) { QFILL_BODY }

// With ARMv8.2's SHA-3 instructions (every Apple chip from the M1 and
// A13 on): XAR, an XOR and a rotation in one.
#undef XR32
#undef XR24
#undef XR16
#undef XR63
#define XR32(d, a) vxarq_u64(d, a, 32)
#define XR24(d, a) vxarq_u64(d, a, 24)
#define XR16(d, a) vxarq_u64(d, a, 16)
#define XR63(d, a) vxarq_u64(d, a, 63)
#undef QTABLES
#define QTABLES
__attribute__((target("+sha3"))) static void fill_neon_sha3(blk *cur, const blk *prev, const blk *ref, bool xor_, blk tmp[2]) {
	QFILL_BODY
}
#else
static void fill_sse2(blk *cur, const blk *prev, const blk *ref, bool xor_, blk tmp[2]) { QFILL_BODY }
#endif

#endif

// ---- AVX2: the block as 32 vectors of four words ----
//
// A row's 16 words are four vectors (a b c d), G on all four columns at
// once, the diagonal step after a vector permute; two rows at once. The
// columns pair as two-word halves of the 128-bit lanes: their diagonal
// step is the 128-bit one's, lane by lane (alignr).
#if A2_X86

#define T_AVX2 __attribute__((target("avx2")))
typedef __m256i Y;
#define YXOR _mm256_xor_si256

T_AVX2 static inline Y ybla(Y x, Y y) {
	Y p = _mm256_mul_epu32(x, y);
	return _mm256_add_epi64(_mm256_add_epi64(x, y), _mm256_add_epi64(p, p));
}

#define Y32(x) _mm256_shuffle_epi32(x, _MM_SHUFFLE(2, 3, 0, 1))
#define Y24(x) _mm256_shuffle_epi8(x, r24)
#define Y16(x) _mm256_shuffle_epi8(x, r16)
#define Y63(x) _mm256_xor_si256(_mm256_srli_epi64(x, 63), _mm256_add_epi64(x, x))
#define YHILO(x, y) _mm256_alignr_epi8(y, x, 8)
#define YP(x, a, b, c, d) _mm256_permute4x64_epi64(x, _MM_SHUFFLE(a, b, c, d))

#define YG(A0, B0, C0, D0, A1, B1, C1, D1, RD, RB) \
	A0 = ybla(A0, B0), A1 = ybla(A1, B1), D0 = YXOR(D0, A0), D1 = YXOR(D1, A1), D0 = RD(D0), D1 = RD(D1); \
	C0 = ybla(C0, D0), C1 = ybla(C1, D1), B0 = YXOR(B0, C0), B1 = YXOR(B1, C1), B0 = RB(B0), B1 = RB(B1)

#define YGG(A0, B0, C0, D0, A1, B1, C1, D1) \
	YG(A0, B0, C0, D0, A1, B1, C1, D1, Y32, Y24); \
	YG(A0, B0, C0, D0, A1, B1, C1, D1, Y16, Y63)

// Two rows: (A0 B0 C0 D0) and (A1 B1 C1 D1), each a row's four vectors.
#define YROWS(A0, B0, C0, D0, A1, B1, C1, D1) \
	do { \
		YGG(A0, B0, C0, D0, A1, B1, C1, D1); \
		B0 = YP(B0, 0, 3, 2, 1), C0 = YP(C0, 1, 0, 3, 2), D0 = YP(D0, 2, 1, 0, 3); \
		B1 = YP(B1, 0, 3, 2, 1), C1 = YP(C1, 1, 0, 3, 2), D1 = YP(D1, 2, 1, 0, 3); \
		YGG(A0, B0, C0, D0, A1, B1, C1, D1); \
		B0 = YP(B0, 2, 1, 0, 3), C0 = YP(C0, 1, 0, 3, 2), D0 = YP(D0, 0, 3, 2, 1); \
		B1 = YP(B1, 2, 1, 0, 3), C1 = YP(C1, 1, 0, 3, 2), D1 = YP(D1, 0, 3, 2, 1); \
	} while (0)

// Two columns' pairs, as QROUND's eight vectors, each lane of 128 bits.
#define YCOLS(A0, A1, B0, B1, C0, C1, D0, D1) \
	do { \
		YGG(A0, B0, C0, D0, A1, B1, C1, D1); \
		Y t0_ = YHILO(B0, B1), t1_ = YHILO(B1, B0); \
		B0 = t0_, B1 = t1_, t0_ = C0, C0 = C1, C1 = t0_; \
		t0_ = YHILO(D1, D0), t1_ = YHILO(D0, D1); \
		D0 = t0_, D1 = t1_; \
		YGG(A0, B0, C0, D0, A1, B1, C1, D1); \
		t0_ = YHILO(B1, B0), t1_ = YHILO(B0, B1); \
		B0 = t0_, B1 = t1_, t0_ = C0, C0 = C1, C1 = t0_; \
		t0_ = YHILO(D0, D1), t1_ = YHILO(D1, D0); \
		D0 = t0_, D1 = t1_; \
	} while (0)

T_AVX2 static void fill_avx2(blk *cur, const blk *prev, const blk *ref, bool xor_, blk tmp[2]) {
	const Y r24 = _mm256_setr_epi8(3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10, 3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10);
	const Y r16 = _mm256_setr_epi8(2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9, 2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9);
	Y *s = (Y *)(void *)tmp[0].v, *t = (Y *)(void *)tmp[1].v;
	for (int i = 0; i < 32; i++) {
		s[i] = YXOR(_mm256_loadu_si256((const Y *)(const void *)(prev->v + 4 * i)), _mm256_loadu_si256((const Y *)(const void *)(ref->v + 4 * i)));
		t[i] = xor_ ? YXOR(s[i], _mm256_loadu_si256((const Y *)(const void *)(cur->v + 4 * i))) : s[i];
	}
	for (int i = 0; i < 32; i += 8) YROWS(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5], s[i + 6], s[i + 7]);
	for (int i = 0; i < 4; i++) YCOLS(s[i], s[i + 4], s[i + 8], s[i + 12], s[i + 16], s[i + 20], s[i + 24], s[i + 28]);
	for (int i = 0; i < 32; i++) _mm256_storeu_si256((Y *)(void *)(cur->v + 4 * i), YXOR(s[i], t[i]));
}

#endif

static fill_fn *choose_fill(void) {
	if (ak_cryptohw_off) return fill_c;
#if A2_NEON
	return ak_cpu_sha3() ? fill_neon_sha3 : fill_neon;
#elif A2_X86
	if (ak_cpu_avx2() && !ak_cryptohw_sse2) return fill_avx2;
	return fill_sse2;
#else
	return fill_c;
#endif
}

// ---- Argon2 ----

static void store32(uint8_t *p, uint32_t v) {
	for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i);
}

static void blake_u32(crypto_blake2b_ctx *c, uint32_t v) {
	uint8_t b[4];
	store32(b, v);
	crypto_blake2b_update(c, b, 4);
}

// H' (RFC 9106 3.3): a hash of any length, BLAKE2b's 64 bytes at a time.
static void hprime(uint8_t *out, uint32_t n, const uint8_t *in, uint32_t in_n) {
	crypto_blake2b_ctx c;
	crypto_blake2b_init(&c, n < 64 ? n : 64);
	blake_u32(&c, n);
	crypto_blake2b_update(&c, in, in_n);
	crypto_blake2b_final(&c, out);
	if (n > 64) {
		uint32_t r = (uint32_t)(((uint64_t)n + 31) >> 5) - 2, at = 0;
		for (uint32_t i = 1; i < r; i++, at += 32) crypto_blake2b(out + at + 32, 64, out + at, 64);
		crypto_blake2b(out + at + 32, n - 32 * r, out + at, 64);
	}
}

typedef struct {
	blk *blocks;
	fill_fn *fill;
	uint32_t lanes, segment, lane_size, passes, algorithm, nb_blocks;
} job;

// segment fills lane's segment of slice in pass: Monocypher's loop,
// for one lane.
static void segment(const job *a, uint32_t pass, uint32_t slice, uint32_t lane) {
	static blk zero;  // never written: in .bss, not the file
	blk *blocks = a->blocks;
	const uint32_t seg = a->segment, lane_size = a->lane_size;
	bool independent = a->algorithm == CRYPTO_ARGON2_I || (a->algorithm == CRYPTO_ARGON2_ID && pass == 0 && slice < 2);
	uint32_t first = pass == 0 && slice == 0 ? 2 : 0, slice_offset = slice * seg;
	blk input, addr, tmp[2];
	uint64_t ctr = 1;
	blk *start = blocks + (size_t)lane * lane_size + slice_offset;
	for (uint32_t b = first; b < seg; b++) {
		blk *cur = start + b;
		const blk *prev = b == 0 && slice_offset == 0 ? start + lane_size - 1 : start + b - 1;
		uint64_t seed;
		if (independent) {
			if (b == first || b % 128 == 0) {
				// The address block: G(0, G(0, the counter's block)).
				memset(&input, 0, sizeof input);
				input.v[0] = pass, input.v[1] = lane, input.v[2] = slice, input.v[3] = a->nb_blocks;
				input.v[4] = a->passes, input.v[5] = a->algorithm, input.v[6] = ctr++;
				a->fill(&addr, &zero, &input, false, tmp);
				a->fill(&addr, &zero, &addr, false, tmp);
			}
			seed = addr.v[b % 128];
		} else {
			seed = prev->v[0];
		}
		// The reference set (RFC 9106 3.4.1.2): the last three slices (or as
		// many as there are yet), and of this one, this segment's blocks
		// before prev, or none of another lane's.
		uint32_t next_slice = ((slice + 1) % 4) * seg;
		uint32_t window_start = pass == 0 ? 0 : next_slice, nb_segments = pass == 0 ? slice : 3;
		uint64_t ref_lane = pass == 0 && slice == 0 ? lane : (seed >> 32) % a->lanes;
		uint32_t window = nb_segments * seg + (ref_lane == lane ? b - 1 : b == 0 ? (uint32_t)-1 : 0);
		uint64_t j1 = seed & 0xffffffff, x = (j1 * j1) >> 32, y = ((uint64_t)window * x) >> 32, z = (window - 1) - y;
		uint64_t ref = (window_start + z) % lane_size;
		a->fill(cur, prev, blocks + (size_t)ref_lane * lane_size + ref, pass > 0, tmp);
	}
	ak_wipe(&addr, sizeof addr);
	ak_wipe(tmp, sizeof tmp);
}

// ---- The lanes on threads ----
//
// The slice's lanes are taken one at a time, under the lock, by
// whichever thread is free; the caller's waits for the last to finish
// before it opens the next slice.

typedef struct {
	const job *a;
	ak_mutex mu;
	ak_cond work, done;
	uint32_t pass, slice;  // what's being filled; slice 4: the wipe
	uint32_t next, finished, helpers;
	bool quit;
} pool;

static void wipe_lane(const job *a, uint32_t lane) {
	ak_wipe(a->blocks + (size_t)lane * a->lane_size, (size_t)a->lane_size * sizeof(blk));
}

static void run_lane(const job *a, uint32_t pass, uint32_t slice, uint32_t lane) {
	if (slice < 4) segment(a, pass, slice, lane);
	else wipe_lane(a, lane);
}

// take runs lanes while there are any to take; mu held.
static void take(pool *p) {
	while (p->next < p->a->lanes) {
		uint32_t lane = p->next++, pass = p->pass, slice = p->slice;
		ak_mutex_unlock(&p->mu);
		run_lane(p->a, pass, slice, lane);
		ak_mutex_lock(&p->mu);
		if (++p->finished == p->a->lanes) ak_cond_broadcast(&p->done);
	}
}

static void helper(void *arg) {
	pool *p = arg;
	ak_mutex_lock(&p->mu);
	for (;;) {
		take(p);
		if (p->quit) break;
		ak_cond_wait(&p->work, &p->mu);
	}
	p->helpers--;
	ak_cond_broadcast(&p->done);
	ak_mutex_unlock(&p->mu);
}

// round fills (or wipes) every lane's segment of a slice.
static void round_(pool *p, uint32_t pass, uint32_t slice) {
	if (!p->helpers) {
		for (uint32_t l = 0; l < p->a->lanes; l++) run_lane(p->a, pass, slice, l);
		return;
	}
	ak_mutex_lock(&p->mu);
	p->pass = pass, p->slice = slice, p->next = 0, p->finished = 0;
	ak_cond_broadcast(&p->work);
	take(p);
	while (p->finished < p->a->lanes) ak_cond_wait(&p->done, &p->mu);
	ak_mutex_unlock(&p->mu);
}

int ak_argon2_threads;

static uint32_t cpus(void) {
#if defined(_WIN32)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
#elif defined(__wasm__)
	return 1;
#else
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? (uint32_t)n : 1;
#endif
}

void ak_argon2(uint8_t *hash, uint32_t hash_size, void *work, crypto_argon2_config config, crypto_argon2_inputs inputs,
	crypto_argon2_extras extras) {
	job a = {.blocks = work, .fill = choose_fill(), .lanes = config.nb_lanes, .passes = config.nb_passes, .algorithm = config.algorithm};
	a.segment = config.nb_blocks / config.nb_lanes / 4;
	a.lane_size = a.segment * 4;
	a.nb_blocks = a.lane_size * a.lanes;  // rounded down
	// H0, then each lane's first two blocks.
	{
		uint8_t h0[72], first[1024];
		crypto_blake2b_ctx c;
		crypto_blake2b_init(&c, 64);
		blake_u32(&c, config.nb_lanes);
		blake_u32(&c, hash_size);
		blake_u32(&c, config.nb_blocks);
		blake_u32(&c, config.nb_passes);
		blake_u32(&c, 0x13);
		blake_u32(&c, config.algorithm);
		blake_u32(&c, inputs.pass_size);
		crypto_blake2b_update(&c, inputs.pass, inputs.pass_size);
		blake_u32(&c, inputs.salt_size);
		crypto_blake2b_update(&c, inputs.salt, inputs.salt_size);
		blake_u32(&c, extras.key_size);
		crypto_blake2b_update(&c, extras.key, extras.key_size);
		blake_u32(&c, extras.ad_size);
		crypto_blake2b_update(&c, extras.ad, extras.ad_size);
		crypto_blake2b_final(&c, h0);
		for (uint32_t l = 0; l < a.lanes; l++)
			for (uint32_t i = 0; i < 2; i++) {
				store32(h0 + 64, i);
				store32(h0 + 68, l);
				hprime(first, 1024, h0, 72);
				blk *b = a.blocks + (size_t)l * a.lane_size + i;
				for (int k = 0; k < 128; k++) {
					uint64_t w = 0;
					for (int j = 7; j >= 0; j--) w = w << 8 | first[8 * k + j];
					b->v[k] = w;
				}
			}
		ak_wipe(h0, sizeof h0);
		ak_wipe(first, sizeof first);
	}

	pool p = {.a = &a};
	uint32_t want = ak_argon2_threads > 0 ? (uint32_t)ak_argon2_threads : cpus();
	if (want > a.lanes) want = a.lanes;
	if (want > 1) {
		ak_mutex_init(&p.mu);
		ak_cond_init(&p.work);
		ak_cond_init(&p.done);
		p.next = p.finished = a.lanes;  // nothing to take until the first slice
		for (uint32_t i = 1; i < want; i++) {
			ak_mutex_lock(&p.mu);
			bool ok = ak_thread_start(helper, &p);
			if (ok) p.helpers++;
			ak_mutex_unlock(&p.mu);
			if (!ok) break;
		}
	}
	for (uint32_t pass = 0; pass < a.passes; pass++)
		for (uint32_t slice = 0; slice < 4; slice++) round_(&p, pass, slice);

	// The last blocks of the lanes XORed, hashed by H' into the tag.
	blk last = a.blocks[a.lane_size - 1];
	for (uint32_t l = 1; l < a.lanes; l++) {
		const blk *b = a.blocks + (size_t)l * a.lane_size + a.lane_size - 1;
		for (int k = 0; k < 128; k++) last.v[k] ^= b->v[k];
	}
	// The work area wiped, a lane a thread.
	round_(&p, 0, 4);
	if (want > 1) {
		ak_mutex_lock(&p.mu);
		p.quit = true;
		ak_cond_broadcast(&p.work);
		while (p.helpers) ak_cond_wait(&p.done, &p.mu);
		ak_mutex_unlock(&p.mu);
		ak_cond_destroy(&p.work);
		ak_cond_destroy(&p.done);
		ak_mutex_destroy(&p.mu);
	}
	uint8_t final[1024];
	for (int k = 0; k < 128; k++)
		for (int j = 0; j < 8; j++) final[8 * k + j] = (uint8_t)(last.v[k] >> 8 * j);
	hprime(hash, hash_size, final, 1024);
	ak_wipe(final, sizeof final);
	ak_wipe(&last, sizeof last);
}
