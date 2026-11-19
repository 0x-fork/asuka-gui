// cryptohw.c: SHA-2 and AES-GCM by the CPU's own instructions, where it
// has them: ARMv8's AES, PMULL, SHA-256 and SHA-512 (every Apple
// silicon chip, most Android phones, Windows on ARM), x86's AES-NI and
// PCLMULQDQ. Each is constant time, as the software it stands in for
// (crypto.c's SHA-2, BearSSL's bitsliced AES and GHASH, aead.c) is, and
// gives the same bytes: what the CPU lacks still runs that.
//
// The CPU is asked once (ak_cpu_*), at run time: an Android app built
// for ARMv8 runs on phones with and without the extensions, an x86
// build on CPUs from before AES-NI.
//
// GHASH multiplies as Intel's white paper does (Gueron and Kounavis,
// "Intel Carry-Less Multiplication Instruction and its Usage for
// Computing the GCM Mode", rev. 2.02, its gfmul): on blocks with their
// bytes reversed, a 256-bit carry-less product shifted left one bit and
// reduced by x^128 + x^7 + x^2 + x + 1; eight blocks' products summed
// before one reduction (by H^8 … H^1), as it's linear. The ARMv8 code is
// the same steps in NEON.
#include "std/std.h"
#include "std/cryptohw.h"

#if defined(__aarch64__) || defined(_M_ARM64)
#define HW_ARM 1
#include <arm_neon.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/auxv.h>
#endif
#elif (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
#define HW_X86 1
#include <cpuid.h>
#include <immintrin.h>
#endif

enum { CPU_KNOWN = 1, CPU_AES = 2, CPU_SHA256 = 4, CPU_SHA512 = 8 };

static int cpu_features(void) {
	static int known;  // racing threads each work out the same answer
	int f = __atomic_load_n(&known, __ATOMIC_RELAXED);
	if (f) return f;
	f = CPU_KNOWN;
#if HW_ARM
#if defined(__APPLE__)
	f |= CPU_AES | CPU_SHA256;  // every arm64 Apple chip, A7 on
	int v = 0;
	size_t n = sizeof v;
	if (sysctlbyname("hw.optional.armv8_2_sha512", &v, &n, NULL, 0) == 0 && v) f |= CPU_SHA512;
#elif defined(_WIN32)
	if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE)) f |= CPU_AES | CPU_SHA256;
#elif defined(__linux__)
	unsigned long hw = getauxval(AT_HWCAP);
	if ((hw & (1ul << 3)) && (hw & (1ul << 4))) f |= CPU_AES;  // HWCAP_AES, HWCAP_PMULL
	if (hw & (1ul << 6)) f |= CPU_SHA256;                        // HWCAP_SHA2
	if (hw & (1ul << 21)) f |= CPU_SHA512;                       // HWCAP_SHA512
#endif
#elif HW_X86
	unsigned a, b, c, d;
	if (__get_cpuid(1, &a, &b, &c, &d) && (c & bit_AES) && (c & bit_PCLMUL) && (c & bit_SSSE3)) f |= CPU_AES;
#endif
	__atomic_store_n(&known, f, __ATOMIC_RELAXED);
	return f;
}

bool ak_cryptohw_off;

bool ak_cpu_aes(void) { return !ak_cryptohw_off && cpu_features() & CPU_AES; }
bool ak_cpu_sha256(void) { return !ak_cryptohw_off && cpu_features() & CPU_SHA256; }
// ARMv8's SHA-1 instructions come with its SHA-256 ones (FEAT_SHA1 and
// FEAT_SHA256: HWCAP_SHA2's set implies HWCAP_SHA1's on every core made).
bool ak_cpu_sha1(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
	return ak_cpu_sha256();
#else
	return false;
#endif
}
bool ak_cpu_sha512(void) { return !ak_cryptohw_off && cpu_features() & CPU_SHA512; }

// ---- ARMv8 ----
#if HW_ARM

#define T_AES __attribute__((target("+aes")))
#define T_SHA2 __attribute__((target("+sha2")))
#define T_SHA3 __attribute__((target("+sha3")))

T_AES static uint32_t sub_word_hw(uint32_t w) {
	// AESE with a zero key: SubBytes, then ShiftRows, which moves nothing
	// when every column is the same word.
	uint8x16_t x = vaeseq_u8(vreinterpretq_u8_u32(vdupq_n_u32(w)), vdupq_n_u8(0));
	return vgetq_lane_u32(vreinterpretq_u32_u8(x), 0);
}

static uint32_t sub_word(uint32_t w) { return sub_word_hw(w); }

// ---- SHA-256 ----

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

// Four rounds: wk is the message words and the constants added.
#define SHA256_4(wk) \
	do { \
		uint32x4_t t_ = s0; \
		s0 = vsha256hq_u32(s0, s1, wk); \
		s1 = vsha256h2q_u32(s1, t_, wk); \
	} while (0)

// The next four message words, from the sixteen before them.
#define SHA256_W(a, b, c, d) a = vsha256su1q_u32(vsha256su0q_u32(a, b), c, d)

T_SHA2 void ak_sha256_blocks_hw(uint32_t h[8], const uint8_t *p, size_t blocks) {
	uint32x4_t s0 = vld1q_u32(h), s1 = vld1q_u32(h + 4);
	for (; blocks--; p += 64) {
		uint32x4_t a0 = s0, a1 = s1;
		uint32x4_t m0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p)));
		uint32x4_t m1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 16)));
		uint32x4_t m2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 32)));
		uint32x4_t m3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 48)));
		for (int i = 0; i < 48; i += 16) {
			uint32x4_t wk = vaddq_u32(m0, vld1q_u32(K256 + i));
			SHA256_W(m0, m1, m2, m3);
			SHA256_4(wk);
			wk = vaddq_u32(m1, vld1q_u32(K256 + i + 4));
			SHA256_W(m1, m2, m3, m0);
			SHA256_4(wk);
			wk = vaddq_u32(m2, vld1q_u32(K256 + i + 8));
			SHA256_W(m2, m3, m0, m1);
			SHA256_4(wk);
			wk = vaddq_u32(m3, vld1q_u32(K256 + i + 12));
			SHA256_W(m3, m0, m1, m2);
			SHA256_4(wk);
		}
		SHA256_4(vaddq_u32(m0, vld1q_u32(K256 + 48)));
		SHA256_4(vaddq_u32(m1, vld1q_u32(K256 + 52)));
		SHA256_4(vaddq_u32(m2, vld1q_u32(K256 + 56)));
		SHA256_4(vaddq_u32(m3, vld1q_u32(K256 + 60)));
		s0 = vaddq_u32(s0, a0);
		s1 = vaddq_u32(s1, a1);
	}
	vst1q_u32(h, s0);
	vst1q_u32(h + 4, s1);
}

// ---- SHA-1 (ARMv8's SHA1C/P/M, SHA1H, SHA1SU0/1) ----

// Four rounds an instruction: the message's next four words from the
// sixteen before them (SU0 then SU1), e's next from a's (SHA1H), the
// round's function by its group: choose, parity, majority, parity.
T_SHA2 void ak_sha1_blocks_hw(uint32_t h[5], const uint8_t *p, size_t blocks) {
	const uint32x4_t k0 = vdupq_n_u32(0x5a827999), k1 = vdupq_n_u32(0x6ed9eba1), k2 = vdupq_n_u32(0x8f1bbcdc), k3 = vdupq_n_u32(0xca62c1d6);
	uint32x4_t abcd = vld1q_u32(h);
	uint32_t e = h[4];
	for (; blocks--; p += 64) {
		uint32x4_t abcd0 = abcd;
		uint32_t e0 = e;
		uint32x4_t m0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p))), m1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 16)));
		uint32x4_t m2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 32))), m3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 48)));
		uint32_t e1;
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1cq_u32(abcd, e, vaddq_u32(m0, k0));
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1cq_u32(abcd, e1, vaddq_u32(m1, k0));
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1cq_u32(abcd, e, vaddq_u32(m2, k0));
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1cq_u32(abcd, e1, vaddq_u32(m3, k0));
		m0 = vsha1su1q_u32(vsha1su0q_u32(m0, m1, m2), m3);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1cq_u32(abcd, e, vaddq_u32(m0, k0));
		m1 = vsha1su1q_u32(vsha1su0q_u32(m1, m2, m3), m0);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e1, vaddq_u32(m1, k1));
		m2 = vsha1su1q_u32(vsha1su0q_u32(m2, m3, m0), m1);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e, vaddq_u32(m2, k1));
		m3 = vsha1su1q_u32(vsha1su0q_u32(m3, m0, m1), m2);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e1, vaddq_u32(m3, k1));
		m0 = vsha1su1q_u32(vsha1su0q_u32(m0, m1, m2), m3);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e, vaddq_u32(m0, k1));
		m1 = vsha1su1q_u32(vsha1su0q_u32(m1, m2, m3), m0);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e1, vaddq_u32(m1, k1));
		m2 = vsha1su1q_u32(vsha1su0q_u32(m2, m3, m0), m1);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1mq_u32(abcd, e, vaddq_u32(m2, k2));
		m3 = vsha1su1q_u32(vsha1su0q_u32(m3, m0, m1), m2);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1mq_u32(abcd, e1, vaddq_u32(m3, k2));
		m0 = vsha1su1q_u32(vsha1su0q_u32(m0, m1, m2), m3);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1mq_u32(abcd, e, vaddq_u32(m0, k2));
		m1 = vsha1su1q_u32(vsha1su0q_u32(m1, m2, m3), m0);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1mq_u32(abcd, e1, vaddq_u32(m1, k2));
		m2 = vsha1su1q_u32(vsha1su0q_u32(m2, m3, m0), m1);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1mq_u32(abcd, e, vaddq_u32(m2, k2));
		m3 = vsha1su1q_u32(vsha1su0q_u32(m3, m0, m1), m2);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e1, vaddq_u32(m3, k3));
		m0 = vsha1su1q_u32(vsha1su0q_u32(m0, m1, m2), m3);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e, vaddq_u32(m0, k3));
		m1 = vsha1su1q_u32(vsha1su0q_u32(m1, m2, m3), m0);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e1, vaddq_u32(m1, k3));
		m2 = vsha1su1q_u32(vsha1su0q_u32(m2, m3, m0), m1);
		e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e, vaddq_u32(m2, k3));
		m3 = vsha1su1q_u32(vsha1su0q_u32(m3, m0, m1), m2);
		e = vsha1h_u32(vgetq_lane_u32(abcd, 0)), abcd = vsha1pq_u32(abcd, e1, vaddq_u32(m3, k3));
		abcd = vaddq_u32(abcd, abcd0);
		e += e0;
	}
	vst1q_u32(h, abcd);
	h[4] = e;
}

// ---- SHA-512 (ARMv8.2's SHA512 instructions) ----

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

// Two rounds, as Linux's sha512-ce (Ard Biesheuvel) and Go's
// sha512block_arm64.s do them: i0…i4 the state's vectors in this
// round's roles, r the round (its message words m[r % 8], and the four
// after them updated while r < 32).
#define SHA512_2(i0, i1, i2, i3, i4, r) \
	do { \
		uint64x2_t t5_ = vaddq_u64(m[(r) & 7], vld1q_u64(K512 + 2 * (r))); \
		uint64x2_t t6_ = vextq_u64(i2, i3, 1); \
		uint64x2_t t7_ = vextq_u64(i1, i2, 1); \
		t5_ = vextq_u64(t5_, t5_, 1); \
		i3 = vaddq_u64(i3, t5_); \
		if ((r) < 32) { \
			uint64x2_t u_ = vextq_u64(m[((r) + 4) & 7], m[((r) + 5) & 7], 1); \
			m[(r) & 7] = vsha512su0q_u64(m[(r) & 7], m[((r) + 1) & 7]); \
			i3 = vsha512hq_u64(i3, t6_, t7_); \
			m[(r) & 7] = vsha512su1q_u64(m[(r) & 7], m[((r) + 7) & 7], u_); \
		} else { \
			i3 = vsha512hq_u64(i3, t6_, t7_); \
		} \
		i4 = vaddq_u64(i1, i3); \
		i3 = vsha512h2q_u64(i3, i1, i0); \
	} while (0)

#define SHA512_10(r) \
	SHA512_2(v0, v1, v2, v3, v4, (r)); \
	SHA512_2(v3, v0, v4, v2, v1, (r) + 1); \
	SHA512_2(v2, v3, v1, v4, v0, (r) + 2); \
	SHA512_2(v4, v2, v0, v1, v3, (r) + 3); \
	SHA512_2(v1, v4, v3, v0, v2, (r) + 4)

T_SHA3 void ak_sha512_blocks_hw(uint64_t h[8], const uint8_t *p, size_t blocks) {
	uint64x2_t d0 = vld1q_u64(h), d1 = vld1q_u64(h + 2), d2 = vld1q_u64(h + 4), d3 = vld1q_u64(h + 6);
	for (; blocks--; p += 128) {
		uint64x2_t v0 = d0, v1 = d1, v2 = d2, v3 = d3, v4 = vdupq_n_u64(0);
		uint64x2_t m[8];
		for (int i = 0; i < 8; i++) m[i] = vreinterpretq_u64_u8(vrev64q_u8(vld1q_u8(p + 16 * i)));
		SHA512_10(0);
		SHA512_10(5);
		SHA512_10(10);
		SHA512_10(15);
		SHA512_10(20);
		SHA512_10(25);
		SHA512_10(30);
		SHA512_10(35);
		d0 = vaddq_u64(d0, v0);
		d1 = vaddq_u64(d1, v1);
		d2 = vaddq_u64(d2, v2);
		d3 = vaddq_u64(d3, v3);
	}
	vst1q_u64(h, d0);
	vst1q_u64(h + 2, d1);
	vst1q_u64(h + 4, d2);
	vst1q_u64(h + 6, d3);
}

// ---- AES-GCM ----

typedef uint8x16_t blk;

#define LOAD(p) vld1q_u8(p)
#define STORE(p, x) vst1q_u8(p, x)
#define XOR(a, b) veorq_u8(a, b)
#define ZERO() vdupq_n_u8(0)

T_AES static inline blk bswap(blk x) {
	static const uint8_t rev[16] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
	return vqtbl1q_u8(x, vld1q_u8(rev));
}

T_AES static inline blk clmul(blk a, blk b, int hi_a, int hi_b) {
	uint64x2_t x = vreinterpretq_u64_u8(a), y = vreinterpretq_u64_u8(b);
	poly64_t p = (poly64_t)(hi_a ? vgetq_lane_u64(x, 1) : vgetq_lane_u64(x, 0));
	poly64_t q = (poly64_t)(hi_b ? vgetq_lane_u64(y, 1) : vgetq_lane_u64(y, 0));
	return vreinterpretq_u8_p128(vmull_p64(p, q));
}

// The byte shifts of a whole vector, as SSE's _mm_slli_si128 and
// _mm_srli_si128 (toward the high bytes, and the low).
#define SLL_BYTES(x, n) vextq_u8(ZERO(), x, 16 - (n))
#define SRL_BYTES(x, n) vextq_u8(x, ZERO(), n)
#define SRL32(x, n) vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(x), n))
#define SLL32(x, n) vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(x), n))
#define OR(a, b) vorrq_u8(a, b)

// a × b, unreduced: the 256-bit product's halves added into *lo, *hi.
T_AES static inline void mul_acc(blk a, blk b, blk *lo, blk *hi) {
	blk t3 = clmul(a, b, 0, 0), t6 = clmul(a, b, 1, 1);
	blk t4 = XOR(clmul(a, b, 0, 1), clmul(a, b, 1, 0));
	*lo = XOR(*lo, XOR(t3, SLL_BYTES(t4, 8)));
	*hi = XOR(*hi, XOR(t6, SRL_BYTES(t4, 8)));
}

// The product's shift by one bit and its reduction.
T_AES static inline blk reduce(blk t3, blk t6) {
	blk t7 = SRL32(t3, 31), t8 = SRL32(t6, 31);
	t3 = SLL32(t3, 1);
	t6 = SLL32(t6, 1);
	blk t9 = SRL_BYTES(t7, 12);
	t8 = SLL_BYTES(t8, 4);
	t7 = SLL_BYTES(t7, 4);
	t3 = OR(t3, t7);
	t6 = OR(OR(t6, t8), t9);
	t7 = XOR(XOR(SLL32(t3, 31), SLL32(t3, 30)), SLL32(t3, 25));
	t8 = SRL_BYTES(t7, 4);
	t7 = SLL_BYTES(t7, 12);
	t3 = XOR(t3, t7);
	blk t2 = XOR(XOR(XOR(SRL32(t3, 1), SRL32(t3, 2)), SRL32(t3, 7)), t8);
	return XOR(t6, XOR(t3, t2));
}

T_AES static inline blk gfmul(blk a, blk b) {
	blk lo = ZERO(), hi = ZERO();
	mul_acc(a, b, &lo, &hi);
	return reduce(lo, hi);
}

typedef struct {
	blk rk[15];
	int nr;
} aes_key;

T_AES static inline blk aes_enc(const aes_key *k, blk s) {
	for (int i = 0; i < k->nr - 1; i++) s = vaesmcq_u8(vaeseq_u8(s, k->rk[i]));
	return XOR(vaeseq_u8(s, k->rk[k->nr - 1]), k->rk[k->nr]);
}

// Eight blocks at once, each round across them, so the AES unit's
// pipeline stays full.
T_AES static inline void aes_enc8(const aes_key *k, blk s[8]) {
	for (int i = 0; i < k->nr - 1; i++)
		for (int j = 0; j < 8; j++) s[j] = vaesmcq_u8(vaeseq_u8(s[j], k->rk[i]));
	for (int j = 0; j < 8; j++) s[j] = XOR(vaeseq_u8(s[j], k->rk[k->nr - 1]), k->rk[k->nr]);
}

#define AES_ROUND_KEYS(k, sched) \
	for (int i_ = 0; i_ <= (k)->nr; i_++) (k)->rk[i_] = vld1q_u8(sched[i_])

#elif HW_X86

#define T_AES __attribute__((target("aes,pclmul,ssse3")))

T_AES static uint32_t sub_word_hw(uint32_t w) {
	// AESKEYGENASSIST's first word is SubWord of the second.
	__m128i x = _mm_aeskeygenassist_si128(_mm_set1_epi32((int)w), 0);
	return (uint32_t)_mm_cvtsi128_si32(x);
}

static uint32_t sub_word(uint32_t w) { return sub_word_hw(w); }

void ak_sha256_blocks_hw(uint32_t h[8], const uint8_t *p, size_t blocks) {
	AK_UNUSED(h), AK_UNUSED(p), AK_UNUSED(blocks);
}

void ak_sha1_blocks_hw(uint32_t h[5], const uint8_t *p, size_t blocks) {
	AK_UNUSED(h), AK_UNUSED(p), AK_UNUSED(blocks);
}

void ak_sha512_blocks_hw(uint64_t h[8], const uint8_t *p, size_t blocks) {
	AK_UNUSED(h), AK_UNUSED(p), AK_UNUSED(blocks);
}

typedef __m128i blk;

#define LOAD(p) _mm_loadu_si128((const __m128i *)(const void *)(p))
#define STORE(p, x) _mm_storeu_si128((__m128i *)(void *)(p), x)
#define XOR(a, b) _mm_xor_si128(a, b)
#define ZERO() _mm_setzero_si128()

T_AES static inline blk bswap(blk x) { return _mm_shuffle_epi8(x, _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)); }

T_AES static inline void mul_acc(blk a, blk b, blk *lo, blk *hi) {
	blk t3 = _mm_clmulepi64_si128(a, b, 0x00), t6 = _mm_clmulepi64_si128(a, b, 0x11);
	blk t4 = XOR(_mm_clmulepi64_si128(a, b, 0x10), _mm_clmulepi64_si128(a, b, 0x01));
	*lo = XOR(*lo, XOR(t3, _mm_slli_si128(t4, 8)));
	*hi = XOR(*hi, XOR(t6, _mm_srli_si128(t4, 8)));
}

T_AES static inline blk reduce(blk t3, blk t6) {
	blk t7 = _mm_srli_epi32(t3, 31), t8 = _mm_srli_epi32(t6, 31);
	t3 = _mm_slli_epi32(t3, 1);
	t6 = _mm_slli_epi32(t6, 1);
	blk t9 = _mm_srli_si128(t7, 12);
	t8 = _mm_slli_si128(t8, 4);
	t7 = _mm_slli_si128(t7, 4);
	t3 = _mm_or_si128(t3, t7);
	t6 = _mm_or_si128(_mm_or_si128(t6, t8), t9);
	t7 = XOR(XOR(_mm_slli_epi32(t3, 31), _mm_slli_epi32(t3, 30)), _mm_slli_epi32(t3, 25));
	t8 = _mm_srli_si128(t7, 4);
	t7 = _mm_slli_si128(t7, 12);
	t3 = XOR(t3, t7);
	blk t2 = XOR(XOR(XOR(_mm_srli_epi32(t3, 1), _mm_srli_epi32(t3, 2)), _mm_srli_epi32(t3, 7)), t8);
	return XOR(t6, XOR(t3, t2));
}

T_AES static inline blk gfmul(blk a, blk b) {
	blk lo = ZERO(), hi = ZERO();
	mul_acc(a, b, &lo, &hi);
	return reduce(lo, hi);
}

typedef struct {
	blk rk[15];
	int nr;
} aes_key;

T_AES static inline blk aes_enc(const aes_key *k, blk s) {
	s = XOR(s, k->rk[0]);
	for (int i = 1; i < k->nr; i++) s = _mm_aesenc_si128(s, k->rk[i]);
	return _mm_aesenclast_si128(s, k->rk[k->nr]);
}

T_AES static inline void aes_enc8(const aes_key *k, blk s[8]) {
	for (int j = 0; j < 8; j++) s[j] = XOR(s[j], k->rk[0]);
	for (int i = 1; i < k->nr; i++)
		for (int j = 0; j < 8; j++) s[j] = _mm_aesenc_si128(s[j], k->rk[i]);
	for (int j = 0; j < 8; j++) s[j] = _mm_aesenclast_si128(s[j], k->rk[k->nr]);
}

#define AES_ROUND_KEYS(k, sched) \
	for (int i_ = 0; i_ <= (k)->nr; i_++) (k)->rk[i_] = LOAD(sched[i_])

#endif

#if HW_ARM || HW_X86

// The AES key schedule (FIPS-197 5.2), its S-box by the CPU's: round
// keys as bytes, nr + 1 of them. Where the CPU has AES, its SubWord
// takes no table, so no lookup's time depends on the key.
static int key_schedule(const uint8_t *key, size_t kn, uint8_t rk[15][16]) {
	int nk = (int)(kn / 4), nr = nk + 6;
	uint32_t w[60];
	for (int i = 0; i < nk; i++) memcpy(&w[i], key + 4 * i, 4);
	uint32_t rcon = 1;
	for (int i = nk; i < 4 * (nr + 1); i++) {
		uint32_t t = w[i - 1];
		if (i % nk == 0) {
			t = sub_word(t >> 8 | t << 24) ^ rcon;  // RotWord: the word's bytes, little-endian
			rcon = (rcon << 1) ^ (0x11b & -(rcon >> 7));
		} else if (nk > 6 && i % nk == 4) {
			t = sub_word(t);
		}
		w[i] = w[i - nk] ^ t;
	}
	memcpy(rk, w, (size_t)(4 * (nr + 1)) * 4);
	ak_wipe(w, sizeof w);
	return nr;
}

// ghash: the GHASH of n bytes (a partial last block padded with
// zeros) into *x, by hp (H^1 … H^8).
T_AES static void ghash(blk *x, const blk hp[8], const uint8_t *p, size_t n) {
	blk acc = *x;
	for (; n >= 128; p += 128, n -= 128) {
		blk lo = ZERO(), hi = ZERO();
		mul_acc(XOR(acc, bswap(LOAD(p))), hp[7], &lo, &hi);
		for (int j = 1; j < 8; j++) mul_acc(bswap(LOAD(p + 16 * j)), hp[7 - j], &lo, &hi);
		acc = reduce(lo, hi);
	}
	for (; n >= 16; p += 16, n -= 16) acc = gfmul(XOR(acc, bswap(LOAD(p))), hp[0]);
	if (n) {
		uint8_t last[16] = {0};
		memcpy(last, p, n);
		acc = gfmul(XOR(acc, bswap(LOAD(last))), hp[0]);
	}
	*x = acc;
}

// The counter block n: the nonce's words, then n big-endian.
T_AES static inline blk ctr(const uint32_t nw[3], uint32_t n) {
	uint32_t w[4] = {nw[0], nw[1], nw[2], __builtin_bswap32(n)};
	return LOAD((const uint8_t *)w);
}

T_AES bool ak_gcm_hw(bool encrypt, const uint8_t *key, size_t kn, const uint8_t nonce[12], const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]) {
	if (!ak_cpu_aes()) return false;
	uint8_t sched[15][16];
	aes_key k;
	k.nr = key_schedule(key, kn, sched);
	AES_ROUND_KEYS(&k, sched);
	ak_wipe(sched, sizeof sched);
	blk hp[8];
	hp[0] = bswap(aes_enc(&k, ZERO()));
	for (int i = 1; i < 8; i++) hp[i] = gfmul(hp[i - 1], hp[0]);
	uint32_t nw[3];
	memcpy(nw, nonce, 12);
	blk x = ZERO();
	ghash(&x, hp, ad, an);
	// Counter 1 makes the tag's mask; the data's begin at 2. Eight blocks
	// at a time: their keystream, then the ciphertext's GHASH (what was
	// written, or what's read), one reduction for the eight.
	uint32_t c = 2;
	size_t i = 0;
	for (; i + 128 <= n; i += 128, c += 8) {
		blk s[8], d[8];
		for (int j = 0; j < 8; j++) s[j] = ctr(nw, c + (uint32_t)j);
		aes_enc8(&k, s);
		for (int j = 0; j < 8; j++) {
			d[j] = LOAD(in + i + 16 * j);
			s[j] = XOR(s[j], d[j]);
		}
		for (int j = 0; j < 8; j++) STORE(out + i + 16 * j, s[j]);
		const blk *ct = encrypt ? s : d;
		blk lo = ZERO(), hi = ZERO();
		mul_acc(XOR(x, bswap(ct[0])), hp[7], &lo, &hi);
		for (int j = 1; j < 8; j++) mul_acc(bswap(ct[j]), hp[7 - j], &lo, &hi);
		x = reduce(lo, hi);
	}
	if (!encrypt) ghash(&x, hp, in + i, n - i);
	for (size_t t = i; t < n; t += 16, c++) {
		uint8_t b[16] = {0}, e[16];
		size_t m = n - t < 16 ? n - t : 16;
		memcpy(b, in + t, m);
		STORE(e, XOR(aes_enc(&k, ctr(nw, c)), LOAD(b)));
		memcpy(out + t, e, m);
	}
	if (encrypt) ghash(&x, hp, out + i, n - i);
	// The lengths in bits, as GCM's last block: in reversed byte order,
	// the ciphertext's in the low half.
	uint64_t lens[2] = {(uint64_t)n * 8, (uint64_t)an * 8};
	uint8_t lb[16];
	memcpy(lb, lens, 16);
	x = gfmul(XOR(x, LOAD(lb)), hp[0]);
	STORE(tag, XOR(bswap(x), aes_enc(&k, ctr(nw, 1))));
	ak_wipe(&k, sizeof k);
	ak_wipe(hp, sizeof hp);
	return true;
}

#else

bool ak_gcm_hw(bool encrypt, const uint8_t *key, size_t kn, const uint8_t nonce[12], const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]) {
	AK_UNUSED(encrypt), AK_UNUSED(key), AK_UNUSED(kn), AK_UNUSED(nonce), AK_UNUSED(ad), AK_UNUSED(an);
	AK_UNUSED(in), AK_UNUSED(out), AK_UNUSED(n), AK_UNUSED(tag);
	return false;
}

void ak_sha256_blocks_hw(uint32_t h[8], const uint8_t *p, size_t blocks) {
	AK_UNUSED(h), AK_UNUSED(p), AK_UNUSED(blocks);
}

void ak_sha1_blocks_hw(uint32_t h[5], const uint8_t *p, size_t blocks) {
	AK_UNUSED(h), AK_UNUSED(p), AK_UNUSED(blocks);
}

void ak_sha512_blocks_hw(uint64_t h[8], const uint8_t *p, size_t blocks) {
	AK_UNUSED(h), AK_UNUSED(p), AK_UNUSED(blocks);
}

#endif
