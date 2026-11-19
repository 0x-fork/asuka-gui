// aead.c: std/crypto's authenticated encryption, the builtins
// crypto_aead.* (their own table: an app that never seals links none of
// it): AES-GCM (NIST SP 800-38D) with 128- and 256-bit keys, over
// BearSSL's constant-time AES (bitsliced) and GHASH (third_party/bearssl);
// ChaCha20-Poly1305 (RFC 8439) and XChaCha20-Poly1305 (its 24-byte
// nonce, draft-irtf-cfrg-xchacha), over Monocypher (third_party/monocypher).
// The same C on every platform, the web's WebAssembly too, so what one
// seals any other opens.
//
// A cipher's code is std/crypto's (cipherCode): 1 XChaCha20-Poly1305, 2
// ChaCha20-Poly1305, 3 AES-256-GCM, 4 AES-128-GCM. The sealed bytes are
// the ciphertext, then the 16-byte tag; the Asuka side puts the nonce
// before them when it made it.
#include "std/std.h"

#include "std/bearssl/bearssl.h"
#include "std/monocypher.h"

#define BYTES(i) ((ak_bytes *)args[i].p)

static const uint8_t *data(const ak_bytes *b) { return b ? b->data : (const uint8_t *)""; }
static size_t len(const ak_bytes *b) { return b ? b->len : 0; }

enum { XCHACHA = 1, CHACHA, AES256, AES128 };

static size_t key_size(int64_t c) { return c == AES128 ? 16 : 32; }
static size_t nonce_size(int64_t c) { return c == XCHACHA ? 24 : 12; }

// gcm runs AES-GCM over in (n bytes) into out, writing its tag into tag
// (encrypt) or checking it (decrypt): false when it doesn't match, and
// then out is zeroed.
static bool gcm(bool encrypt, const uint8_t *key, size_t kn, const uint8_t nonce[12], const uint8_t *ad, size_t an,
	const uint8_t *in, uint8_t *out, size_t n, uint8_t tag[16]) {
	br_aes_ct64_ctr_keys aes;
	br_gcm_context g;
	br_aes_ct64_ctr_init(&aes, key, kn);
	br_gcm_init(&g, &aes.vtable, br_ghash_ctmul64);
	br_gcm_reset(&g, nonce, 12);
	br_gcm_aad_inject(&g, ad, an);
	br_gcm_flip(&g);
	if (n) {
		memmove(out, in, n);
		br_gcm_run(&g, encrypt, out, n);
	}
	bool ok = true;
	if (encrypt) br_gcm_get_tag(&g, tag);
	else ok = br_gcm_check_tag(&g, tag) == 1;
	if (!ok) ak_wipe(out, n);
	ak_wipe(&aes, sizeof aes);
	ak_wipe(&g, sizeof g);
	return ok;
}

// sealBytes(cipher, key, nonce, plaintext, additionalData): the ciphertext
// and its tag; none for a key or a nonce of the wrong size (which the
// Asuka side checks first). Past those checks, key and nonce aren't NULL:
// their data is read whole (data() would show the compiler a 1-byte "").
static ak_val seal(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t c = args[0].i;
	const ak_bytes *key = BYTES(1), *nonce = BYTES(2), *pt = BYTES(3), *ad = BYTES(4);
	if (c < XCHACHA || c > AES128 || len(key) != key_size(c) || len(nonce) != nonce_size(c)) return (ak_val){.p = NULL};
	size_t m = len(pt);
	ak_bytes *out = ak_bytes_new(NULL, m + 16);
	uint8_t *tag = out->data + m;
	switch (c) {
	case XCHACHA:
		crypto_aead_lock(out->data, tag, key->data, nonce->data, data(ad), len(ad), data(pt), m);
		break;
	case CHACHA: {
		crypto_aead_ctx x;
		crypto_aead_init_ietf(&x, key->data, nonce->data);
		crypto_aead_write(&x, out->data, tag, data(ad), len(ad), data(pt), m);
		crypto_wipe(&x, sizeof x);
		break;
	}
	default:
		gcm(true, key->data, len(key), nonce->data, data(ad), len(ad), data(pt), out->data, m, tag);
	}
	return (ak_val){.p = out};
}

// openBytes(cipher, key, nonce, sealed, additionalData): the plaintext, or
// none when the tag doesn't match: a wrong key, nonce or additional data,
// or bytes that were changed (or a key or nonce of the wrong size).
static ak_val open_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t c = args[0].i;
	const ak_bytes *key = BYTES(1), *nonce = BYTES(2), *ct = BYTES(3), *ad = BYTES(4);
	if (c < XCHACHA || c > AES128 || len(key) != key_size(c) || len(nonce) != nonce_size(c) || len(ct) < 16) return (ak_val){.p = NULL};
	size_t m = len(ct) - 16;
	uint8_t tag[16];
	memcpy(tag, data(ct) + m, 16);
	ak_bytes *out = ak_bytes_new(NULL, m);
	bool ok;
	switch (c) {
	case XCHACHA:
		ok = crypto_aead_unlock(out->data, tag, key->data, nonce->data, data(ad), len(ad), data(ct), m) == 0;
		break;
	case CHACHA: {
		crypto_aead_ctx x;
		crypto_aead_init_ietf(&x, key->data, nonce->data);
		ok = crypto_aead_read(&x, out->data, tag, data(ad), len(ad), data(ct), m) == 0;
		crypto_wipe(&x, sizeof x);
		break;
	}
	default:
		ok = gcm(false, key->data, len(key), nonce->data, data(ad), len(ad), data(ct), out->data, m, tag);
	}
	if (!ok) {
		ak_release(out);
		return (ak_val){.p = NULL};
	}
	return (ak_val){.p = out};
}

const ak_lib_entry ak_std_crypto_aead[] = {
	{"crypto_aead.sealBytes", seal},
	{"crypto_aead.openBytes", open_fn},
	{NULL, NULL},
};
