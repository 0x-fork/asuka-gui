// cryptohw.h: SHA-2 and AES-GCM by the CPU's instructions (cryptohw.c),
// ChaCha20-Poly1305 by its vector unit (chacha.c), for crypto.c and
// aead.c, which run their software when the CPU has none of them. Any
// thread.
#ifndef AK_CRYPTOHW_H
#define AK_CRYPTOHW_H

#include "base/base.h"

// ak_cryptohw_off: the software alone, whatever the CPU has (the tests
// compare the two). Set it before any thread hashes.
extern bool ak_cryptohw_off;

bool ak_cpu_aes(void);     // AES and a carry-less multiply: ak_gcm_hw runs
bool ak_cpu_sha256(void);  // ak_sha256_blocks_hw runs
bool ak_cpu_sha512(void);  // ak_sha512_blocks_hw runs
bool ak_cpu_avx2(void);    // x86-64's AVX2, the OS saving its registers
bool ak_cpu_sha3(void);    // ARMv8.2's SHA-3 instructions (XAR)

// ak_cryptohw_sse2: the base vector unit alone (SSE2 on x86-64, NEON
// without the SHA-3 instructions on arm64), where the CPU has more (the
// tests run both).
extern bool ak_cryptohw_sse2;

// The compression function over blocks 64-byte (128-byte) blocks.
void ak_sha256_blocks_hw(uint32_t h[8], const uint8_t *p, size_t blocks);
void ak_sha512_blocks_hw(uint64_t h[8], const uint8_t *p, size_t blocks);

// ak_gcm_hw: AES-GCM (a 16- or 32-byte key, a 12-byte nonce) over in into
// out (which may be in), its tag into tag: the tag of the ciphertext,
// which decrypting reads from in. false, doing nothing, when the CPU
// can't.
bool ak_gcm_hw(bool encrypt, const uint8_t *key, size_t kn, const uint8_t nonce[12], const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]);

// ak_chacha_poly_hw: ChaCha20-Poly1305 (a 12-byte nonce, nn 12) or
// XChaCha20-Poly1305 (24) over in into out, its tag into tag, as
// ak_gcm_hw: decrypting, the tag of in, which the caller compares (and
// wipes out when it doesn't match). false, doing nothing, when the CPU
// can't (Monocypher's then).
bool ak_chacha_poly_hw(bool encrypt, const uint8_t key[32], const uint8_t *nonce, size_t nn, const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]);
// ak_chacha20_hw: ChaCha20 alone (Monocypher's crypto_chacha20_djb), for
// the tests.
bool ak_chacha20_hw(const uint8_t key[32], const uint8_t nonce[8], uint64_t ctr, const uint8_t *in, uint8_t *out, size_t n);

#endif
