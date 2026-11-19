// chacha.h: ChaCha20, Poly1305 and their AEADs (chacha.c), for aead.c.
// Any thread.
#ifndef AK_STD_CHACHA_H
#define AK_STD_CHACHA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ak_chacha20_xor: in XOR ChaCha20's keystream (RFC 8439: a 32-byte
// key, a 12-byte nonce, the block counter to start at) into out (which
// may be in).
void ak_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t *in, uint8_t *out, size_t n);
// ak_hchacha20: XChaCha's subkey of key and 16 bytes.
void ak_hchacha20(uint8_t out[32], const uint8_t key[32], const uint8_t in[16]);

// Where the compiler has 128-bit products (64-bit targets): Poly1305 and
// the AEADs here; elsewhere (wasm32, armv7, i686, MSVC) aead.c has
// Monocypher's.
#if defined(__SIZEOF_INT128__) && UINTPTR_MAX == UINT64_MAX
#define AK_CHACHA_FAST 1

typedef struct {
	uint64_t r[3], h[3], pad[2];
	uint8_t buf[16];
	size_t n;
} ak_poly1305;

void ak_poly1305_init(ak_poly1305 *p, const uint8_t key[32]);
void ak_poly1305_update(ak_poly1305 *p, const uint8_t *m, size_t n);
void ak_poly1305_final(ak_poly1305 *p, uint8_t tag[16]);

// ChaCha20-Poly1305 (x: XChaCha20-Poly1305, a 24-byte nonce; else 12):
// the ciphertext into ct and its tag. ak_chacha_open checks the tag (in
// constant time) and only then decrypts; false when it doesn't match.
void ak_chacha_seal(bool x, const uint8_t key[32], const uint8_t *nonce, const uint8_t *ad, size_t an, const uint8_t *pt, uint8_t *ct, size_t n, uint8_t tag[16]);
bool ak_chacha_open(bool x, const uint8_t key[32], const uint8_t *nonce, const uint8_t *ad, size_t an, const uint8_t *ct, uint8_t *pt, size_t n, const uint8_t tag[16]);
#endif

#endif
