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
// AES-GCM runs eight blocks at a time: their counters' AES, each round
// across the eight, then their GHASH, the eight blocks' products (by H^8
// … H^1) summed before one reduction, which is two carry-less
// multiplies by a constant (as Go's and Linux's ARMv8 GHASH;
// gh_reduce says how). x86's and ARMv8's are the same steps, by
// AES-NI and PCLMULQDQ or AESE and PMULL.
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

enum { CPU_KNOWN = 1, CPU_AES = 2, CPU_SHA256 = 4, CPU_SHA512 = 8, CPU_AVX2 = 16, CPU_SHA3 = 32 };

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
	v = 0, n = sizeof v;
	if (sysctlbyname("hw.optional.armv8_2_sha3", &v, &n, NULL, 0) == 0 && v) f |= CPU_SHA3;
#elif defined(_WIN32)
	if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE)) f |= CPU_AES | CPU_SHA256;
#ifdef PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE
	if (IsProcessorFeaturePresent(PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE)) f |= CPU_SHA3;
#endif
#elif defined(__linux__)
	unsigned long hw = getauxval(AT_HWCAP);
	if ((hw & (1ul << 3)) && (hw & (1ul << 4))) f |= CPU_AES;  // HWCAP_AES, HWCAP_PMULL
	if (hw & (1ul << 6)) f |= CPU_SHA256;                        // HWCAP_SHA2
	if (hw & (1ul << 21)) f |= CPU_SHA512;                       // HWCAP_SHA512
	if (hw & (1ul << 17)) f |= CPU_SHA3;                         // HWCAP_SHA3
#endif
#elif HW_X86
	unsigned a, b, c, d;
	if (__get_cpuid(1, &a, &b, &c, &d) && (c & bit_AES) && (c & bit_PCLMUL) && (c & bit_SSSE3)) f |= CPU_AES;
	// AVX2: the CPU's (leaf 7), and the OS saving the YMM registers
	// (OSXSAVE, then XCR0's SSE and AVX state).
	if (__get_cpuid(1, &a, &b, &c, &d) && (c & bit_OSXSAVE) && (c & bit_AVX)) {
		unsigned lo, hi;
		__asm__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
		if ((lo & 6) == 6 && __get_cpuid_count(7, 0, &a, &b, &c, &d) && (b & bit_AVX2)) f |= CPU_AVX2;
	}
#endif
	__atomic_store_n(&known, f, __ATOMIC_RELAXED);
	return f;
}

bool ak_cryptohw_off;
bool ak_cryptohw_sse2;

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
bool ak_cpu_avx2(void) { return !ak_cryptohw_off && cpu_features() & CPU_AVX2; }
bool ak_cpu_sha3(void) { return !ak_cryptohw_off && !ak_cryptohw_sse2 && cpu_features() & CPU_SHA3; }

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

// sha1h_v is SHA1H with its result in a vector's first lane: the
// intrinsic's is a uint32_t, which the block's last e then went through
// a general register to be added to e's start (fmov, add, fmov back),
// on the chain into the next block's first round: 89 cycles a block on
// the M5, 84 kept in the vector unit as Go's assembly keeps it (82).
#if defined(__GNUC__) || defined(__clang__)
T_SHA2 static inline uint32x4_t sha1h_v(uint32x4_t abcd) {
	uint32x4_t r;
	__asm__("sha1h %s0, %s1" : "=w"(r) : "w"(abcd));
	return r;
}
#else
T_SHA2 static inline uint32x4_t sha1h_v(uint32x4_t abcd) { return vdupq_n_u32(vsha1h_u32(vgetq_lane_u32(abcd, 0))); }
#endif

// Four rounds an instruction: the message's next four words from the
// sixteen before them (SU0 then SU1), e's next from a's (SHA1H), the
// round's function by its group: choose, parity, majority, parity.
T_SHA2 void ak_sha1_blocks_hw(uint32_t h[5], const uint8_t *p, size_t blocks) {
	const uint32x4_t k0 = vdupq_n_u32(0x5a827999), k1 = vdupq_n_u32(0x6ed9eba1), k2 = vdupq_n_u32(0x8f1bbcdc), k3 = vdupq_n_u32(0xca62c1d6);
	uint32x4_t abcd = vld1q_u32(h);
	uint32x4_t ev = vdupq_n_u32(h[4]);  // e, in its first lane
	for (; blocks--; p += 64) {
		uint32x4_t abcd0 = abcd, e0 = ev;
		uint32_t e = vgetq_lane_u32(ev, 0);
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
		ev = sha1h_v(abcd), abcd = vsha1pq_u32(abcd, e1, vaddq_u32(m3, k3));
		abcd = vaddq_u32(abcd, abcd0);
		ev = vaddq_u32(ev, e0);
	}
	vst1q_u32(h, abcd);
	h[4] = vgetq_lane_u32(ev, 0);
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

// The carry-less products of the vectors' low words and of their high
// ones (PMULL, PMULL2). In assembly where the compiler takes it: through
// the intrinsics' lanes, clang keeps a loop's constant operands (the
// powers of H) in general registers and moves each into a vector
// before each multiply.
#if defined(__GNUC__) || defined(__clang__)
T_AES static inline blk clmul_lo(blk a, blk b) {
	blk r;
	__asm__("pmull %0.1q, %1.1d, %2.1d" : "=w"(r) : "w"(a), "w"(b));
	return r;
}

T_AES static inline blk clmul_hi(blk a, blk b) {
	blk r;
	__asm__("pmull2 %0.1q, %1.2d, %2.2d" : "=w"(r) : "w"(a), "w"(b));
	return r;
}
#else
T_AES static inline blk clmul_lo(blk a, blk b) {
	return vreinterpretq_u8_p128(vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u8(a), 0), vgetq_lane_p64(vreinterpretq_p64_u8(b), 0)));
}

T_AES static inline blk clmul_hi(blk a, blk b) {
	return vreinterpretq_u8_p128(vmull_high_p64(vreinterpretq_p64_u8(a), vreinterpretq_p64_u8(b)));
}
#endif

#define SWAP64(x) vextq_u8(x, x, 8)
#define LO_TO_HI(x) vextq_u8(ZERO(), x, 8)  // the low word moved high, zeros below
#define HI_TO_LO(x) vextq_u8(x, ZERO(), 8)
#define WORDS(lo, hi) vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(lo), vcreate_u64(hi)))

// The counter: a vector of four native words (the nonce's, byte-swapped,
// then the count), added to as numbers and byte-swapped back into a
// block.
#define CTR_BLOCK(c) vrev32q_u8(c)
#define CTR_ADD(c, d) vreinterpretq_u8_u32(vaddq_u32(vreinterpretq_u32_u8(c), vreinterpretq_u32_u8(d)))

// The round keys sit at the array's end (an AES-128 key's 11 at 4 … 14),
// so every round's index is a constant and the rounds unroll.
typedef struct {
	blk rk[15];
	int nr;
} aes_key;

#define AES_ROUND(s, key) s = vaesmcq_u8(vaeseq_u8(s, key))

T_AES static inline blk aes_enc(const aes_key *k, blk s) {
	if (k->nr > 12) AES_ROUND(s, k->rk[0]), AES_ROUND(s, k->rk[1]);
	if (k->nr > 10) AES_ROUND(s, k->rk[2]), AES_ROUND(s, k->rk[3]);
	for (int i = 4; i < 13; i++) AES_ROUND(s, k->rk[i]);
	return XOR(vaeseq_u8(s, k->rk[13]), k->rk[14]);
}

// Eight blocks at once, each round across them, so the AES unit's
// pipeline stays full; the last round key is the caller's to add (with
// the data, in one three-way XOR).
T_AES static inline void aes_enc8(const aes_key *k, blk s[8]) {
	if (k->nr > 12)
		for (int i = 0; i < 2; i++)
			for (int j = 0; j < 8; j++) AES_ROUND(s[j], k->rk[i]);
	if (k->nr > 10)
		for (int i = 2; i < 4; i++)
			for (int j = 0; j < 8; j++) AES_ROUND(s[j], k->rk[i]);
	for (int i = 4; i < 13; i++)
		for (int j = 0; j < 8; j++) AES_ROUND(s[j], k->rk[i]);
	for (int j = 0; j < 8; j++) s[j] = vaeseq_u8(s[j], k->rk[13]);
}

// The last round's output XORed with the data: the last round key and
// the data in one three-way XOR (EOR3, where the CPU has it).
#define AES_LAST(k, s, d) XOR(XOR(s, (k)->rk[14]), d)

#define AES_ROUND_KEYS(k, sched) \
	for (int i_ = 0; i_ <= (k)->nr; i_++) (k)->rk[14 - (k)->nr + i_] = vld1q_u8(sched[i_])
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

#define clmul_lo(a, b) _mm_clmulepi64_si128(a, b, 0x00)
#define clmul_hi(a, b) _mm_clmulepi64_si128(a, b, 0x11)
#define SWAP64(x) _mm_shuffle_epi32(x, 0x4e)
#define LO_TO_HI(x) _mm_slli_si128(x, 8)
#define HI_TO_LO(x) _mm_srli_si128(x, 8)
#define WORDS(lo, hi) _mm_set_epi64x((long long)(hi), (long long)(lo))

#define CTR_BLOCK(c) _mm_shuffle_epi8(c, _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3))
#define CTR_ADD(c, d) _mm_add_epi32(c, d)

typedef struct {
	blk rk[15];
	int nr;
} aes_key;

T_AES static inline blk aes_enc(const aes_key *k, blk s) {
	int first = 14 - k->nr;
	s = XOR(s, k->rk[first]);
	for (int i = first + 1; i < 14; i++) s = _mm_aesenc_si128(s, k->rk[i]);
	return _mm_aesenclast_si128(s, k->rk[14]);
}

// All but the last round, which AESENCLAST takes with the data XORed
// into its key.
T_AES static inline void aes_enc8(const aes_key *k, blk s[8]) {
	int first = 14 - k->nr;
	for (int j = 0; j < 8; j++) s[j] = XOR(s[j], k->rk[first]);
	for (int i = first + 1; i < 5; i++)
		for (int j = 0; j < 8; j++) s[j] = _mm_aesenc_si128(s[j], k->rk[i]);
	for (int i = 5; i < 14; i++)
		for (int j = 0; j < 8; j++) s[j] = _mm_aesenc_si128(s[j], k->rk[i]);
}

#define AES_LAST(k, s, d) _mm_aesenclast_si128(s, XOR((k)->rk[14], d))

#define AES_ROUND_KEYS(k, sched) \
	for (int i_ = 0; i_ <= (k)->nr; i_++) (k)->rk[14 - (k)->nr + i_] = LOAD(sched[i_])
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

// GHASH works on blocks with their bytes reversed: a block is then the
// number whose bit 127 - i is the field element's coefficient of x^i, so
// the carry-less product of two is their product's coefficients in the
// same order, one bit short of 256 (bit 254 - i is x^i's). Multiplying
// by H·x^-1 (gh_twist) in place of H makes up the bit, and the 256-bit
// product reduces by x^128 = x^7 + x^2 + x + 1 a word at a time, its low
// word (the highest powers) carry-less multiplied by 0xc2 << 56 (x^7,
// x^2 and x, reflected) and added a word up with itself, twice
// (gh_reduce): no shifts, as Linux's and Go's arm64 GHASH do. A
// block's product is four word products, the middle two by the key's
// words swapped, precomputed (Karatsuba's three would take two more
// instructions a block to fold the block's words); eight blocks'
// products are summed (by H^8 … H^1) before one reduction, as GHASH is
// linear.
// An empty assembly statement the compiler must assume changes x: no
// value it computed from x before carries past it.
#if defined(__GNUC__) || defined(__clang__)
#define OPAQUE_PTR(x) __asm__("" : "+r"(x))
#if HW_ARM
#define OPAQUE_BLK(x) __asm__("" : "+w"(x))
#else
#define OPAQUE_BLK(x) __asm__("" : "+x"(x))
#endif
#else
#define OPAQUE_PTR(x) ((void)0)
#define OPAQUE_BLK(x) ((void)0)
#endif

typedef struct {
	blk hk[16];  // H^(i+1)·x^-1 at 2i, its words swapped at 2i + 1
} gh_key;

#define GH_H(g, i) (g)->hk[2 * (i)]
#define GH_S(g, i) (g)->hk[2 * (i) + 1]

T_AES static inline void gh_mul_acc(blk a, blk h, blk hs, blk *lo, blk *mid, blk *hi) {
	*lo = XOR(*lo, clmul_lo(a, h));
	*hi = XOR(*hi, clmul_hi(a, h));
	*mid = XOR(*mid, XOR(clmul_lo(a, hs), clmul_hi(a, hs)));
}

T_AES static inline blk gh_reduce(blk lo, blk mid, blk hi) {
	lo = XOR(lo, LO_TO_HI(mid));
	hi = XOR(hi, HI_TO_LO(mid));
	blk poly = WORDS(0xc200000000000000ull, 0);
	blk m = XOR(SWAP64(lo), clmul_lo(lo, poly));
	return XOR(XOR(hi, SWAP64(m)), clmul_lo(m, poly));
}

T_AES static inline blk gh_mul(blk a, blk h, blk hs) {
	blk lo = ZERO(), mid = ZERO(), hi = ZERO();
	gh_mul_acc(a, h, hs, &lo, &mid, &hi);
	return gh_reduce(lo, mid, hi);
}

// x·x^-1: the number shifted up a bit, x^-1 = x^127 + x^6 + x + 1 added
// when its x^0 (bit 127) shifts out. Constant time.
T_AES static blk gh_twist(blk x) {
	uint64_t w[2];
	STORE((uint8_t *)w, x);
	uint64_t carry = 0 - (w[1] >> 63);
	return WORDS(w[0] << 1 ^ (carry & 1), (w[1] << 1 | w[0] >> 63) ^ (carry & 0xc200000000000000ull));
}

T_AES static void gh_init(gh_key *g, blk h) {
	blk p = bswap(h);
	GH_H(g, 0) = gh_twist(p);
	GH_S(g, 0) = SWAP64(GH_H(g, 0));
	for (int i = 1; i < 8; i++) {
		p = gh_mul(p, GH_H(g, 0), GH_S(g, 0));
		GH_H(g, i) = gh_twist(p);
		GH_S(g, i) = SWAP64(GH_H(g, i));
	}
}

// ghash: the GHASH of n bytes (a partial last block padded with
// zeros) into *x.
T_AES static void ghash(blk *x, const gh_key *g, const uint8_t *p, size_t n) {
	blk acc = *x;
	for (; n >= 128; p += 128, n -= 128) {
		blk lo = ZERO(), mid = ZERO(), hi = ZERO();
		gh_mul_acc(XOR(acc, bswap(LOAD(p))), GH_H(g, 7), GH_S(g, 7), &lo, &mid, &hi);
#pragma GCC unroll 8
		for (int j = 1; j < 8; j++) gh_mul_acc(bswap(LOAD(p + 16 * j)), GH_H(g, 7 - j), GH_S(g, 7 - j), &lo, &mid, &hi);
		acc = gh_reduce(lo, mid, hi);
	}
	for (; n >= 16; p += 16, n -= 16) acc = gh_mul(XOR(acc, bswap(LOAD(p))), GH_H(g, 0), GH_S(g, 0));
	if (n) {
		uint8_t last[16] = {0};
		memcpy(last, p, n);
		acc = gh_mul(XOR(acc, bswap(LOAD(last))), GH_H(g, 0), GH_S(g, 0));
	}
	*x = acc;
}

// gcm8: the whole 128-byte stretches of in into out from counter *c,
// GHASHed into *x: the eight blocks' keystream, then the ciphertext's
// GHASH (what was written, or what's read), one reduction for the
// eight. Its own loop for each direction (encrypt a constant), as
// decrypting must read the ciphertext before out (which may be in) is
// written. The bytes it did.
T_AES static inline __attribute__((always_inline)) size_t gcm8(bool encrypt, const aes_key *k, const gh_key *g, blk *c, blk *x,
	const uint8_t *in, uint8_t *out, size_t n) {
	blk one = WORDS(0, 1ull << 32), ctr = *c, acc = *x;
	const uint8_t *end = in + (n & ~(size_t)127);
	for (; in < end; in += 128, out += 128) {
		// What the compiler would otherwise keep in registers it hasn't
		// got (the powers of H, the counter's eight sums), spilling the
		// round keys: the powers read where they are, the counter
		// counted up a block at a time.
		const gh_key *gi = g;
		OPAQUE_PTR(gi);
		blk s[8], d[8];
		for (int j = 0; j < 8; j++) {
			s[j] = CTR_BLOCK(ctr), ctr = CTR_ADD(ctr, one);
			OPAQUE_BLK(ctr);
		}
		aes_enc8(k, s);
		for (int j = 0; j < 8; j++) d[j] = LOAD(in + 16 * j);
		for (int j = 0; j < 8; j++) s[j] = AES_LAST(k, s[j], d[j]);
		for (int j = 0; j < 8; j++) STORE(out + 16 * j, s[j]);
		blk lo = ZERO(), mid = ZERO(), hi = ZERO();
		gh_mul_acc(XOR(acc, bswap(encrypt ? s[0] : d[0])), GH_H(gi, 7), GH_S(gi, 7), &lo, &mid, &hi);
#pragma GCC unroll 8
		for (int j = 1; j < 8; j++) gh_mul_acc(bswap(encrypt ? s[j] : d[j]), GH_H(gi, 7 - j), GH_S(gi, 7 - j), &lo, &mid, &hi);
		acc = gh_reduce(lo, mid, hi);
	}
	*c = ctr, *x = acc;
	return n & ~(size_t)127;
}

T_AES bool ak_gcm_hw(bool encrypt, const uint8_t *key, size_t kn, const uint8_t nonce[12], const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]) {
	if (!ak_cpu_aes()) return false;
	uint8_t sched[15][16];
	aes_key k;
	k.nr = key_schedule(key, kn, sched);
	AES_ROUND_KEYS(&k, sched);
	ak_wipe(sched, sizeof sched);
	gh_key g;
	gh_init(&g, aes_enc(&k, ZERO()));
	blk x = ZERO();
	ghash(&x, &g, ad, an);
	// Counter 1 makes the tag's mask; the data's begin at 2.
	uint8_t cb[16] = {0};
	memcpy(cb, nonce, 12);
	cb[15] = 1;
	blk one = WORDS(0, 1ull << 32), c = CTR_BLOCK(LOAD(cb));
	blk mask = aes_enc(&k, CTR_BLOCK(c));
	c = CTR_ADD(c, one);
	size_t i = encrypt ? gcm8(true, &k, &g, &c, &x, in, out, n) : gcm8(false, &k, &g, &c, &x, in, out, n);
	if (!encrypt) ghash(&x, &g, in + i, n - i);
	for (size_t t = i; t < n; t += 16, c = CTR_ADD(c, one)) {
		uint8_t b[16] = {0}, e[16];
		size_t m = n - t < 16 ? n - t : 16;
		memcpy(b, in + t, m);
		STORE(e, XOR(aes_enc(&k, CTR_BLOCK(c)), LOAD(b)));
		memcpy(out + t, e, m);
	}
	if (encrypt) ghash(&x, &g, out + i, n - i);
	// The lengths in bits, as GCM's last block: in reversed byte order,
	// the ciphertext's in the low half.
	x = gh_mul(XOR(x, WORDS((uint64_t)n * 8, (uint64_t)an * 8)), GH_H(&g, 0), GH_S(&g, 0));
	STORE(tag, XOR(bswap(x), mask));
	ak_wipe(&k, sizeof k);
	ak_wipe(&g, sizeof g);
	ak_wipe(&mask, sizeof mask);
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
