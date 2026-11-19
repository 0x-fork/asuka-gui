// cryptohw.h: SHA-2 and AES-GCM by the CPU's instructions (cryptohw.c),
// for crypto.c and aead.c, which run their software when the CPU has
// none of them. Any thread.
#ifndef AK_CRYPTOHW_H
#define AK_CRYPTOHW_H

#include "base/base.h"

// ak_cryptohw_off: the software alone, whatever the CPU has (the tests
// compare the two). Set it before any thread hashes.
extern bool ak_cryptohw_off;

bool ak_cpu_aes(void);     // AES and a carry-less multiply: ak_gcm_hw runs
bool ak_cpu_sha256(void);  // ak_sha256_blocks_hw runs
bool ak_cpu_sha512(void);  // ak_sha512_blocks_hw runs

// The compression function over blocks 64-byte (128-byte) blocks.
void ak_sha256_blocks_hw(uint32_t h[8], const uint8_t *p, size_t blocks);
void ak_sha512_blocks_hw(uint64_t h[8], const uint8_t *p, size_t blocks);

// ak_gcm_hw: AES-GCM (a 16- or 32-byte key, a 12-byte nonce) over in into
// out (which may be in), its tag into tag: the tag of the ciphertext,
// which decrypting reads from in. false, doing nothing, when the CPU
// can't.
bool ak_gcm_hw(bool encrypt, const uint8_t *key, size_t kn, const uint8_t nonce[12], const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]);

#endif
