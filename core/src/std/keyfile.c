// keyfile.c: private keys as files hold them, encrypted or not (keyfile.h),
// in three tables of builtins, so an app links only what it calls:
// crypto_keyfile.* reads keys that aren't encrypted (std/crypto's
// privateKey, privateKeyFromDER), crypto_keydecrypt.* reads encrypted
// ones too (decryptPrivateKey: the ciphers and key derivations, reached
// only from it), crypto_keyenc.* writes them (encryptedPEM).
//
// Read, from PEM or DER:
//   - PKCS#8 (RFC 5208) as it is (PRIVATE KEY), PKCS#1's RSAPrivateKey
//     (RSA PRIVATE KEY) and SEC 1's ECPrivateKey (EC PRIVATE KEY);
//   - those encrypted as OpenSSL's traditional PEM writes them (headers
//     Proc-Type: 4,ENCRYPTED and DEK-Info: AES-128-CBC, AES-192-CBC,
//     AES-256-CBC or DES-EDE3-CBC, the key from the password by
//     EVP_BytesToKey's MD5), what openssl genrsa -aes256 and older tools
//     write;
//   - PKCS#8's EncryptedPrivateKeyInfo (ENCRYPTED PRIVATE KEY): PBES2
//     (RFC 8018) with PBKDF2 (HMAC-SHA-1, -SHA-256, -SHA-384, -SHA-512) or
//     scrypt (RFC 7914), and AES-CBC (128, 192, 256) or DES-EDE3-CBC, what
//     OpenSSL 1.1 and 3 write; and PKCS#12's pbeWithSHAAnd3-KeyTripleDES-CBC
//     (and its two-key one: RFC 7292, appendix B), what OpenSSL 1.0 and
//     Java wrote. Single DES, RC2 and RC4 (PBES1, PKCS#12's others) are
//     weak and refused, saying how to encrypt the key again;
//   - OpenSSH's own (OPENSSH PRIVATE KEY, PROTOCOL.key), what ssh-keygen
//     writes: Ed25519, ECDSA (P-256, P-384, P-521) and RSA keys, not
//     encrypted, or encrypted with bcrypt_pbkdf (password.c) and
//     aes256-ctr (ssh-keygen's), or AES-CTR, AES-CBC, AES-GCM or
//     chacha20-poly1305@openssh.com. An Ed25519 key becomes PKCS#8, an
//     ECDSA key SEC 1, an RSA key PKCS#1 (its CRT exponents, d mod p-1
//     and d mod q-1, computed with BearSSL's constant-time arithmetic).
// A password that doesn't decrypt the key is told apart from a key that
// isn't one (AK_KEY_PASSWORD): what it decrypted to must be the key's DER
// (and OpenSSH's check words agree, AES-GCM's and Poly1305's tags check).
//
// Written: PKCS#8 encrypted as OpenSSL 3 writes it (ak_key_encrypt).
//
// AES and Triple DES are BearSSL's constant-time ones (third_party/bearssl);
// ChaCha20 and Poly1305 Monocypher's; the hashes, HMAC and PBKDF2 the
// core's (crypto.c). The same C on every platform, the web's too.
#include "std/std.h"
#include "std/keyfile.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "std/bearssl/bearssl.h"
#include "std/monocypher.h"

// BearSSL's big integers (its inner.h's, the names its own: names.h), for
// an OpenSSH RSA key's CRT exponents.
void br_i31_decode(uint32_t *x, const void *src, size_t len);
void br_i31_reduce(uint32_t *x, const uint32_t *a, const uint32_t *m);
void br_i31_encode(void *dst, size_t len, const uint32_t *x);

#define BYTES(i) ((ak_bytes *)args[i].p)

static const uint8_t *data(const ak_bytes *b) { return b ? b->data : (const uint8_t *)""; }
static size_t len(const ak_bytes *b) { return b ? b->len : 0; }

struct decryptor;

// What a read says when it fails (why, and whether it's the password's),
// and what decrypts what's encrypted: NULL for a read that doesn't
// (ak_key_read's), so its link has none of the ciphers.
typedef struct {
	char *why;
	size_t cap;
	int code;
	const struct decryptor *dec;
} kf;

static int fail(kf *f, int code, const char *fmt, ...) AK_PRINTF(3, 4);
static int fail(kf *f, int code, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(f->why, f->cap, fmt, ap);
	va_end(ap);
	f->code = code;
	return code;
}

// ---- Bytes that hold secrets: grown by copying, wiped as they go ----

typedef struct {
	uint8_t *p;
	size_t n, cap;
} sbuf;

static void sfree(sbuf *b) {
	if (b->p) {
		ak_wipe(b->p, b->cap);
		ak_free(b->p);
	}
	*b = (sbuf){0};
}

static void sput(sbuf *b, const void *p, size_t n) {
	if (b->n + n > b->cap) {
		size_t cap = (b->n + n) * 2 + 64;
		uint8_t *q = ak_alloc(cap);
		if (b->n) memcpy(q, b->p, b->n);
		size_t keep = b->n;
		sfree(b);
		b->p = q, b->n = keep, b->cap = cap;
	}
	if (n) memcpy(b->p + b->n, p, n);
	b->n += n;
}

// ---- DER (X.690), read ----

typedef struct {
	const uint8_t *p;
	size_t n;
} span;

// der_get reads the element at *s, of tag, into out (its contents), and
// moves past it; false for another tag or what isn't DER's.
static bool der_get(span *s, uint8_t tag, span *out) {
	if (s->n < 2 || s->p[0] != tag) return false;
	size_t n = s->p[1], h = 2;
	if (n & 0x80) {
		size_t k = n & 0x7f;
		if (k == 0 || k > 4 || s->n < 2 + k) return false;
		n = 0;
		for (size_t i = 0; i < k; i++) n = n << 8 | s->p[2 + i];
		h += k;
	}
	if (n > s->n - h) return false;
	if (out) *out = (span){s->p + h, n};
	s->p += h + n, s->n -= h + n;
	return true;
}

// der_count reads an INTEGER from 1 to max.
static bool der_count(span *s, uint64_t max, uint64_t *v) {
	span x;
	if (!der_get(s, 0x02, &x) || x.n == 0 || (x.p[0] & 0x80)) return false;
	while (x.n > 1 && x.p[0] == 0) x.p++, x.n--;
	if (x.n > 8) return false;
	uint64_t r = 0;
	for (size_t i = 0; i < x.n; i++) r = r << 8 | x.p[i];
	if (r < 1 || r > max) return false;
	*v = r;
	return true;
}

static bool span_is(span s, const uint8_t *p, size_t n) { return s.n == n && memcmp(s.p, p, n) == 0; }

#define IS(s, oid) span_is(s, oid, sizeof oid)

// The object identifiers, DER's contents.
static const uint8_t OID_PBES2[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x05, 0x0d};
static const uint8_t OID_PBKDF2[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x05, 0x0c};
static const uint8_t OID_SCRYPT[] = {0x2b, 0x06, 0x01, 0x04, 0x01, 0xda, 0x47, 0x04, 0x0b};
static const uint8_t OID_HMAC_SHA1[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x07};
static const uint8_t OID_HMAC_SHA224[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x08};
static const uint8_t OID_HMAC_SHA256[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x09};
static const uint8_t OID_HMAC_SHA384[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x0a};
static const uint8_t OID_HMAC_SHA512[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x0b};
static const uint8_t OID_AES128_CBC[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x01, 0x02};
static const uint8_t OID_AES192_CBC[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x01, 0x16};
static const uint8_t OID_AES256_CBC[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x01, 0x2a};
static const uint8_t OID_DES_EDE3_CBC[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x03, 0x07};
static const uint8_t OID_DES_CBC[] = {0x2b, 0x0e, 0x03, 0x02, 0x07};
static const uint8_t OID_RC2_CBC[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x03, 0x02};
// PKCS#12's (1.2.840.113549.1.12.1.n) and PBES1's (1.2.840.113549.1.5.n).
static const uint8_t OID_PKCS12_PBE[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x0c, 0x01};
static const uint8_t OID_PBES1[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x05};
static const uint8_t OID_ED25519[] = {0x2b, 0x65, 0x70};
static const uint8_t OID_P256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const uint8_t OID_P384[] = {0x2b, 0x81, 0x04, 0x00, 0x22};
static const uint8_t OID_P521[] = {0x2b, 0x81, 0x04, 0x00, 0x23};

// key_form says what DER is a private key's (its outer SEQUENCE whole,
// nothing after it): PKCS#8 (a version, then an AlgorithmIdentifier),
// PKCS#1 (a version, then the modulus) or SEC 1 (a version, then the
// scalar); 0 for none. What a decryption gave is checked so: with the
// wrong password it's noise.
static int key_form(span s) {
	span seq;
	if (!der_get(&s, 0x30, &seq) || s.n || !der_get(&seq, 0x02, NULL) || !seq.n) return 0;
	switch (seq.p[0]) {
	case 0x30: return AK_KEY_PKCS8;
	case 0x02: return AK_KEY_PKCS1;
	case 0x04: return AK_KEY_SEC1;
	}
	return 0;
}

// ---- DER, written ----

static void put_header(sbuf *b, uint8_t tag, size_t n) {
	uint8_t h[6] = {tag};
	size_t k = 1;
	if (n < 0x80) h[k++] = (uint8_t)n;
	else if (n < 0x100) h[k++] = 0x81, h[k++] = (uint8_t)n;
	else if (n < 0x10000) h[k++] = 0x82, h[k++] = (uint8_t)(n >> 8), h[k++] = (uint8_t)n;
	else if (n < 0x1000000) h[k++] = 0x83, h[k++] = (uint8_t)(n >> 16), h[k++] = (uint8_t)(n >> 8), h[k++] = (uint8_t)n;
	else h[k++] = 0x84, h[k++] = (uint8_t)(n >> 24), h[k++] = (uint8_t)(n >> 16), h[k++] = (uint8_t)(n >> 8), h[k++] = (uint8_t)n;
	sput(b, h, k);
}

static void put_tlv(sbuf *b, uint8_t tag, const void *p, size_t n) {
	put_header(b, tag, n);
	sput(b, p, n);
}

// wrap puts inner as the contents of an element of tag, and frees it.
static void wrap(sbuf *b, uint8_t tag, sbuf *inner) {
	put_tlv(b, tag, inner->p, inner->n);
	sfree(inner);
}

// put_uint puts an unsigned big-endian integer as an INTEGER.
static void put_uint(sbuf *b, span v) {
	while (v.n && v.p[0] == 0) v.p++, v.n--;
	if (!v.n) {
		put_tlv(b, 0x02, (const uint8_t[]){0}, 1);
		return;
	}
	bool pad = v.p[0] & 0x80;
	put_header(b, 0x02, v.n + pad);
	if (pad) sput(b, (const uint8_t[]){0}, 1);
	sput(b, v.p, v.n);
}

static void put_small(sbuf *b, uint64_t v) {
	uint8_t x[8];
	for (int i = 0; i < 8; i++) x[i] = (uint8_t)(v >> (56 - 8 * i));
	put_uint(b, (span){x, 8});
}

// What decrypts (decrypting, below).
struct decryptor {
	int (*pkcs8)(kf *f, span s, const uint8_t *pw, size_t pn, sbuf *out);
	int (*traditional)(kf *f, span dek, sbuf *body, int form, const uint8_t *pw, size_t pn, sbuf *out);
	int (*openssh)(kf *f, span cname, span kdf, span kopts, span enc, span rest, const uint8_t *pw, size_t pn, sbuf *b);
};

// ---- Ciphers ----

enum { C_AES = 1, C_DES3 };

// cbc_open decrypts data[0:n] in place, CBC with key and iv (cipher's
// block: 16, or 8 for Triple DES), and takes off PKCS#7's padding: the
// plaintext's length, or -1 when it isn't padded (a wrong password, or
// bytes that aren't the key's).
static long cbc_open(int cipher, const uint8_t *key, size_t kn, const uint8_t *iv, uint8_t *data, size_t n) {
	size_t bs = cipher == C_AES ? 16 : 8;
	if (n == 0 || n % bs) return -1;
	uint8_t v[16];
	memcpy(v, iv, bs);
	if (cipher == C_AES) {
		br_aes_ct64_cbcdec_keys k;
		br_aes_ct64_cbcdec_init(&k, key, kn);
		br_aes_ct64_cbcdec_run(&k, v, data, n);
		ak_wipe(&k, sizeof k);
	} else {
		br_des_ct_cbcdec_keys k;
		br_des_ct_cbcdec_init(&k, key, kn);
		br_des_ct_cbcdec_run(&k, v, data, n);
		ak_wipe(&k, sizeof k);
	}
	uint8_t pad = data[n - 1];
	if (pad == 0 || pad > bs) return -1;
	for (size_t i = n - pad; i < n; i++)
		if (data[i] != pad) return -1;
	return (long)(n - pad);
}

// ctr runs AES-CTR over data[0:n] in place: OpenSSH's, its 16-byte
// counter big-endian and whole (BearSSL's counts in the last 4 bytes, so
// a carry out of them goes into the 12 before it here).
static void ctr(const uint8_t *key, size_t kn, const uint8_t iv[16], uint8_t *data, size_t n) {
	br_aes_ct64_ctr_keys k;
	br_aes_ct64_ctr_init(&k, key, kn);
	uint8_t head[12];
	memcpy(head, iv, 12);
	uint32_t cc = (uint32_t)iv[12] << 24 | (uint32_t)iv[13] << 16 | (uint32_t)iv[14] << 8 | iv[15];
	while (n) {
		uint64_t blocks = cc ? (UINT64_C(1) << 32) - cc : UINT64_C(1) << 32;
		size_t m = (uint64_t)n / 16 + (n % 16 != 0) <= blocks ? n : (size_t)(blocks * 16);
		cc = br_aes_ct64_ctr_run(&k, head, cc, data, m);
		data += m, n -= m;
		if (n && cc == 0)
			for (int i = 11; i >= 0 && ++head[i] == 0; i--) {}
	}
	ak_wipe(&k, sizeof k);
}

// gcm_open decrypts data[0:n] in place with AES-GCM, its tag after it:
// false (data wiped) when the tag doesn't check.
static bool gcm_open(const uint8_t *key, size_t kn, const uint8_t iv[12], uint8_t *data, size_t n, const uint8_t tag[16]) {
	br_aes_ct64_ctr_keys aes;
	br_gcm_context g;
	br_aes_ct64_ctr_init(&aes, key, kn);
	br_gcm_init(&g, &aes.vtable, br_ghash_ctmul64);
	br_gcm_reset(&g, iv, 12);
	br_gcm_flip(&g);
	br_gcm_run(&g, 0, data, n);
	bool ok = br_gcm_check_tag(&g, tag) == 1;
	if (!ok) ak_wipe(data, n);
	ak_wipe(&aes, sizeof aes);
	ak_wipe(&g, sizeof g);
	return ok;
}

// chachapoly_open is chacha20-poly1305@openssh.com's (PROTOCOL.chacha20poly1305)
// for a key file: its first 32 bytes of key, the sequence number 0, no
// length to decrypt; the tag over the ciphertext.
static bool chachapoly_open(const uint8_t key[64], uint8_t *data, size_t n, const uint8_t tag[16]) {
	static const uint8_t nonce[8], zero[32];
	uint8_t polykey[32], mac[16];
	crypto_chacha20_djb(polykey, zero, 32, key, nonce, 0);
	crypto_poly1305(mac, data, n, polykey);
	uint8_t diff = 0;
	for (int i = 0; i < 16; i++) diff |= (uint8_t)(mac[i] ^ tag[i]);
	ak_wipe(polykey, sizeof polykey);
	if (diff) return false;
	crypto_chacha20_djb(data, data, n, key, nonce, 1);
	return true;
}

// ---- Key derivation ----

// evp_bytes_to_key is OpenSSL's EVP_BytesToKey(MD5, salt, 1 iteration):
// traditional PEM's key from its password and its IV's first 8 bytes.
static void evp_bytes_to_key(const uint8_t *pw, size_t pn, const uint8_t salt[8], uint8_t *key, size_t kn) {
	uint8_t d[16];
	size_t have = 0;
	sbuf m = {0};
	for (bool first = true; have < kn; first = false) {
		m.n = 0;
		if (!first) sput(&m, d, 16);
		sput(&m, pw, pn);
		sput(&m, salt, 8);
		ak_md5(m.p, m.n, d);
		size_t k = kn - have < 16 ? kn - have : 16;
		memcpy(key + have, d, k);
		have += k;
	}
	sfree(&m);
	ak_wipe(d, sizeof d);
}

// pkcs12_kdf is PKCS#12's key derivation (RFC 7292, B.2) with SHA-1:
// n bytes of purpose id (1 a key, 2 an IV) from a password (BMPString,
// with its terminating zero) and a salt.
static void pkcs12_kdf(const uint8_t *pw, size_t pn, span salt, uint64_t iterations, uint8_t id, uint8_t *out, size_t n) {
	enum { U = 20, V = 64 };
	size_t sl = salt.n ? V * ((salt.n + V - 1) / V) : 0, pl = pn ? V * ((pn + V - 1) / V) : 0, il = sl + pl;
	uint8_t *I = ak_alloc(il ? il : 1), D[V], A[U], B[V];
	for (size_t i = 0; i < sl; i++) I[i] = salt.p[i % salt.n];
	for (size_t i = 0; i < pl; i++) I[sl + i] = pw[i % pn];
	memset(D, id, V);
	for (size_t done = 0;;) {
		ak_sha1_ctx h;
		ak_sha1_init(&h);
		ak_sha1_update(&h, D, V);
		ak_sha1_update(&h, I, il);
		ak_sha1_final(&h, A);
		for (uint64_t r = 1; r < iterations; r++) ak_hash(AK_HASH_SHA1, A, U, A);
		size_t k = n - done < U ? n - done : U;
		memcpy(out + done, A, k);
		done += k;
		if (done == n) break;
		for (size_t i = 0; i < V; i++) B[i] = A[i % U];
		// Each V-byte block of I becomes I_j + B + 1, mod 2^(8V).
		for (size_t j = 0; j < il; j += V) {
			unsigned c = 1;
			for (size_t i = V; i-- > 0;) {
				c += (unsigned)I[j + i] + B[i];
				I[j + i] = (uint8_t)c;
				c >>= 8;
			}
		}
	}
	ak_wipe(I, il ? il : 1);
	ak_free(I);
	ak_wipe(A, sizeof A);
	ak_wipe(B, sizeof B);
}

// bmp is pw (UTF-8) as PKCS#12 takes a password: UTF-16 big-endian, then
// a zero character.
static void bmp(const uint8_t *pw, size_t pn, sbuf *out) {
	for (size_t i = 0; i < pn;) {
		uint32_t c = pw[i], need = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
		if (need) c &= 0x3f >> need;
		i++;
		for (uint32_t k = 0; k < need && i < pn && (pw[i] & 0xc0) == 0x80; k++, i++) c = c << 6 | (pw[i] & 0x3f);
		if (c >= 0x10000) {
			c -= 0x10000;
			uint8_t s[4] = {(uint8_t)(0xd8 | (c >> 18)), (uint8_t)(c >> 10), (uint8_t)(0xdc | ((c >> 8) & 3)), (uint8_t)c};
			sput(out, s, 4);
		} else {
			uint8_t s[2] = {(uint8_t)(c >> 8), (uint8_t)c};
			sput(out, s, 2);
		}
	}
	sput(out, (const uint8_t[]){0, 0}, 2);
}

// salsa8 is Salsa20/8's core over b (16 words), in place.
static void salsa8(uint32_t b[16]) {
	uint32_t x[16];
	memcpy(x, b, sizeof x);
#define R(a, k) (((a) << (k)) | ((a) >> (32 - (k))))
	for (int i = 0; i < 8; i += 2) {
		x[4] ^= R(x[0] + x[12], 7), x[8] ^= R(x[4] + x[0], 9), x[12] ^= R(x[8] + x[4], 13), x[0] ^= R(x[12] + x[8], 18);
		x[9] ^= R(x[5] + x[1], 7), x[13] ^= R(x[9] + x[5], 9), x[1] ^= R(x[13] + x[9], 13), x[5] ^= R(x[1] + x[13], 18);
		x[14] ^= R(x[10] + x[6], 7), x[2] ^= R(x[14] + x[10], 9), x[6] ^= R(x[2] + x[14], 13), x[10] ^= R(x[6] + x[2], 18);
		x[3] ^= R(x[15] + x[11], 7), x[7] ^= R(x[3] + x[15], 9), x[11] ^= R(x[7] + x[3], 13), x[15] ^= R(x[11] + x[7], 18);
		x[1] ^= R(x[0] + x[3], 7), x[2] ^= R(x[1] + x[0], 9), x[3] ^= R(x[2] + x[1], 13), x[0] ^= R(x[3] + x[2], 18);
		x[6] ^= R(x[5] + x[4], 7), x[7] ^= R(x[6] + x[5], 9), x[4] ^= R(x[7] + x[6], 13), x[5] ^= R(x[4] + x[7], 18);
		x[11] ^= R(x[10] + x[9], 7), x[8] ^= R(x[11] + x[10], 9), x[9] ^= R(x[8] + x[11], 13), x[10] ^= R(x[9] + x[8], 18);
		x[12] ^= R(x[15] + x[14], 7), x[13] ^= R(x[12] + x[15], 9), x[14] ^= R(x[13] + x[12], 13), x[15] ^= R(x[14] + x[13], 18);
	}
#undef R
	for (int i = 0; i < 16; i++) b[i] += x[i];
}

// block_mix is scrypt's BlockMix over b (2r blocks of 16 words) into y.
static void block_mix(const uint32_t *b, uint32_t *y, size_t r) {
	uint32_t x[16];
	memcpy(x, b + (2 * r - 1) * 16, 64);
	for (size_t i = 0; i < 2 * r; i++) {
		for (int k = 0; k < 16; k++) x[k] ^= b[i * 16 + k];
		salsa8(x);
		memcpy(y + ((i & 1) * r + i / 2) * 16, x, 64);
	}
}

// scrypt is RFC 7914's: n bytes of key; false when its memory (128 r N
// bytes) can't be had, the one large allocation that may fail here.
static bool scrypt(const uint8_t *pw, size_t pn, span salt, uint64_t N, uint64_t r, uint64_t p, uint8_t *out, size_t n) {
	size_t words = 32 * (size_t)r;
	if (N > SIZE_MAX / 8 / words) return false;
	uint32_t *V = malloc(4 * words * (size_t)N);
	uint32_t *X = malloc(4 * words * 2);
	uint8_t *B = malloc(128 * (size_t)r * (size_t)p);
	if (!V || !X || !B) {
		free(V), free(X), free(B);
		return false;
	}
	ak_pbkdf2(AK_HASH_SHA256, pw, pn, salt.p, salt.n, 1, B, 128 * (size_t)r * (size_t)p);
	uint32_t *x = X, *y = X + words;
	for (uint64_t i = 0; i < p; i++) {
		uint8_t *bi = B + i * 128 * r;
		for (size_t k = 0; k < words; k++) x[k] = (uint32_t)bi[4 * k] | (uint32_t)bi[4 * k + 1] << 8 | (uint32_t)bi[4 * k + 2] << 16 | (uint32_t)bi[4 * k + 3] << 24;
		for (uint64_t k = 0; k < N; k++) {
			memcpy(V + k * words, x, 4 * words);
			block_mix(x, y, r);
			uint32_t *t = x;
			x = y, y = t;
		}
		for (uint64_t k = 0; k < N; k++) {
			uint64_t j = ((uint64_t)x[words - 16] | (uint64_t)x[words - 15] << 32) & (N - 1);
			for (size_t w = 0; w < words; w++) x[w] ^= V[j * words + w];
			block_mix(x, y, r);
			uint32_t *t = x;
			x = y, y = t;
		}
		for (size_t k = 0; k < words; k++) bi[4 * k] = (uint8_t)x[k], bi[4 * k + 1] = (uint8_t)(x[k] >> 8), bi[4 * k + 2] = (uint8_t)(x[k] >> 16), bi[4 * k + 3] = (uint8_t)(x[k] >> 24);
	}
	ak_pbkdf2(AK_HASH_SHA256, pw, pn, B, 128 * (size_t)r * (size_t)p, 1, out, n);
	ak_wipe(V, 4 * words * (size_t)N);
	ak_wipe(X, 4 * words * 2);
	ak_wipe(B, 128 * (size_t)r * (size_t)p);
	free(V), free(X), free(B);
	return true;
}

// ---- PKCS#8's EncryptedPrivateKeyInfo ----

static const char REENCRYPT[] = "encrypt it again with AES: openssl pkcs8 -topk8 -v2 aes-256-cbc -in key.pem -out new.pem";

// cbc_cipher reads PBES2's encryption scheme: AES-CBC's or DES-EDE3-CBC's
// key size and IV.
static int cbc_cipher(kf *f, span alg, int *cipher, size_t *kn, span *iv) {
	span oid;
	if (!der_get(&alg, 0x06, &oid)) return fail(f, AK_KEY_INVALID, "the encrypted key's cipher can't be read");
	size_t want;
	if (IS(oid, OID_AES128_CBC)) *cipher = C_AES, *kn = 16, want = 16;
	else if (IS(oid, OID_AES192_CBC)) *cipher = C_AES, *kn = 24, want = 16;
	else if (IS(oid, OID_AES256_CBC)) *cipher = C_AES, *kn = 32, want = 16;
	else if (IS(oid, OID_DES_EDE3_CBC)) *cipher = C_DES3, *kn = 24, want = 8;
	else if (IS(oid, OID_DES_CBC) || IS(oid, OID_RC2_CBC)) return fail(f, AK_KEY_INVALID, "the key is encrypted with a weak cipher (single DES or RC2), which isn't read: %s", REENCRYPT);
	else return fail(f, AK_KEY_INVALID, "the key is encrypted with a cipher that isn't read (AES-CBC and DES-EDE3-CBC are)");
	if (!der_get(&alg, 0x04, iv) || iv->n != want) return fail(f, AK_KEY_INVALID, "the encrypted key's IV isn't its cipher's");
	return 0;
}

// pbes2 derives the key and reads the cipher of PBES2's parameters.
static int pbes2(kf *f, span params, const uint8_t *pw, size_t pn, int *cipher, uint8_t key[32], size_t *kn, span *iv) {
	span seq, kdf, enc, oid, kp;
	if (!der_get(&params, 0x30, &seq) || !der_get(&seq, 0x30, &kdf) || !der_get(&seq, 0x30, &enc) || !der_get(&kdf, 0x06, &oid))
		return fail(f, AK_KEY_INVALID, "the encrypted key's PBES2 parameters can't be read");
	if (cbc_cipher(f, enc, cipher, kn, iv)) return f->code;
	if (!der_get(&kdf, 0x30, &kp)) return fail(f, AK_KEY_INVALID, "the encrypted key's key derivation can't be read");
	span salt;
	uint64_t want = 0;
	if (!der_get(&kp, 0x04, &salt)) return fail(f, AK_KEY_INVALID, "the encrypted key's salt isn't an octet string");
	if (IS(oid, OID_PBKDF2)) {
		uint64_t iterations;
		if (!der_count(&kp, UINT64_MAX >> 1, &iterations)) return fail(f, AK_KEY_INVALID, "the encrypted key's PBKDF2 iterations can't be read");
		if (kp.n && kp.p[0] == 0x02 && !der_count(&kp, 64, &want)) return fail(f, AK_KEY_INVALID, "the encrypted key's key length can't be read");
		int hash = AK_HASH_SHA1;
		span prf, poid;
		if (kp.n) {
			if (!der_get(&kp, 0x30, &prf) || !der_get(&prf, 0x06, &poid)) return fail(f, AK_KEY_INVALID, "the encrypted key's PBKDF2 hash can't be read");
			if (IS(poid, OID_HMAC_SHA1)) hash = AK_HASH_SHA1;
			else if (IS(poid, OID_HMAC_SHA256)) hash = AK_HASH_SHA256;
			else if (IS(poid, OID_HMAC_SHA384)) hash = AK_HASH_SHA384;
			else if (IS(poid, OID_HMAC_SHA512)) hash = AK_HASH_SHA512;
			else if (IS(poid, OID_HMAC_SHA224)) return fail(f, AK_KEY_INVALID, "the key's PBKDF2 hash is HMAC-SHA-224, which isn't read (SHA-1, SHA-256, SHA-384 and SHA-512 are)");
			else return fail(f, AK_KEY_INVALID, "the key's PBKDF2 hash isn't one that's read (HMAC-SHA-1, -SHA-256, -SHA-384, -SHA-512)");
		}
		if (want && want != *kn) return fail(f, AK_KEY_INVALID, "the encrypted key's key length isn't its cipher's");
		ak_pbkdf2(hash, pw, pn, salt.p, salt.n, iterations, key, *kn);
		return 0;
	}
	if (IS(oid, OID_SCRYPT)) {
		uint64_t N, r, p;
		if (!der_count(&kp, UINT64_MAX >> 1, &N) || !der_count(&kp, UINT32_MAX, &r) || !der_count(&kp, UINT32_MAX, &p) || N < 2 || (N & (N - 1)) ||
		    r * p >= (UINT64_C(1) << 30))
			return fail(f, AK_KEY_INVALID, "the encrypted key's scrypt parameters aren't scrypt's (N a power of 2, r times p under 2^30)");
		if (kp.n && (!der_count(&kp, 64, &want) || want != *kn)) return fail(f, AK_KEY_INVALID, "the encrypted key's key length isn't its cipher's");
		if (!scrypt(pw, pn, salt, N, r, p, key, *kn))
			return fail(f, AK_KEY_INVALID, "there isn't the memory the key's scrypt takes (%llu MiB)", (unsigned long long)(N * r / 8192));
		return 0;
	}
	return fail(f, AK_KEY_INVALID, "the encrypted key's key derivation isn't one that's read (PBKDF2 and scrypt are)");
}

// decrypted checks that what a password decrypted is the key's DER: its
// form, or AK_KEY_PASSWORD.
static int decrypted(kf *f, long n, const uint8_t *p, size_t pn) {
	int form = n > 0 ? key_form((span){p, (size_t)n}) : 0;
	if (form) return form;
	return pn ? fail(f, AK_KEY_PASSWORD, "the password is wrong: it doesn't decrypt the key")
		  : fail(f, AK_KEY_PASSWORD, "the key is encrypted: its password is needed");
}

// encrypted_pkcs8 decrypts EncryptedPrivateKeyInfo s into out.
static int encrypted_pkcs8(kf *f, span s, const uint8_t *pw, size_t pn, sbuf *out) {
	span seq, alg, oid, ct;
	if (!der_get(&s, 0x30, &seq) || s.n || !der_get(&seq, 0x30, &alg) || !der_get(&alg, 0x06, &oid) || !der_get(&seq, 0x04, &ct))
		return fail(f, AK_KEY_INVALID, "it isn't an encrypted private key (PKCS#8's EncryptedPrivateKeyInfo)");
	int cipher = 0;
	size_t kn = 0;
	uint8_t key[32], ivb[8];
	span iv;
	if (IS(oid, OID_PBES2)) {
		if (pbes2(f, alg, pw, pn, &cipher, key, &kn, &iv)) return f->code;
	} else if (oid.n == sizeof OID_PKCS12_PBE + 1 && memcmp(oid.p, OID_PKCS12_PBE, sizeof OID_PKCS12_PBE) == 0) {
		uint8_t which = oid.p[oid.n - 1];
		if (which != 3 && which != 4) return fail(f, AK_KEY_INVALID, "the key is encrypted with a weak cipher (PKCS#12's RC2 or RC4), which isn't read: %s", REENCRYPT);
		span params, salt;
		uint64_t iterations;
		if (!der_get(&alg, 0x30, &params) || !der_get(&params, 0x04, &salt) || !der_count(&params, UINT64_MAX >> 1, &iterations))
			return fail(f, AK_KEY_INVALID, "the encrypted key's PKCS#12 parameters can't be read");
		sbuf b = {0};
		bmp(pw, pn, &b);
		cipher = C_DES3;
		kn = which == 3 ? 24 : 16;
		pkcs12_kdf(b.p, b.n, salt, iterations, 1, key, kn);
		pkcs12_kdf(b.p, b.n, salt, iterations, 2, ivb, 8);
		sfree(&b);
		iv = (span){ivb, 8};
	} else if (oid.n == sizeof OID_PBES1 + 1 && memcmp(oid.p, OID_PBES1, sizeof OID_PBES1) == 0 && memchr("\x01\x03\x04\x06\x0a\x0b", oid.p[oid.n - 1], 6)) {
		return fail(f, AK_KEY_INVALID, "the key is encrypted with PBES1's single DES or RC2, which are weak and aren't read: %s", REENCRYPT);
	} else {
		return fail(f, AK_KEY_INVALID, "the key is encrypted with a scheme that isn't read (PBES2 is: what OpenSSL writes)");
	}
	sbuf b = {0};
	sput(&b, ct.p, ct.n);
	long n = cbc_open(cipher, key, kn, iv.p, b.p, b.n);
	ak_wipe(key, sizeof key);
	int form = decrypted(f, n, b.p, pn);
	if (form > 0) {
		b.n = (size_t)n;
		*out = b;
	} else {
		sfree(&b);
	}
	return form;
}

// ---- PEM (RFC 7468; RFC 1421's headers) ----

static int b64_value(uint8_t c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static bool starts(const uint8_t *p, const uint8_t *end, const char *s) {
	size_t k = strlen(s);
	return (size_t)(end - p) >= k && memcmp(p, s, k) == 0;
}

static const uint8_t *find(const uint8_t *p, const uint8_t *end, const char *s) {
	for (; p < end; p++)
		if (starts(p, end, s)) return p;
	return NULL;
}

typedef struct {
	span label;
	const uint8_t *body, *end;  // after the BEGIN line, to the END line
} block;

// next_block finds the next PEM block from *at: 1, 0 for none, or
// AK_KEY_INVALID for one that isn't whole.
static int next_block(kf *f, const uint8_t **at, const uint8_t *end, block *b) {
	const uint8_t *q = find(*at, end, "-----BEGIN ");
	if (!q) return 0;
	const uint8_t *label = q + 11, *le = label;
	while (le < end && *le != '-' && *le != '\n') le++;
	if (!starts(le, end, "-----")) return fail(f, AK_KEY_INVALID, "the PEM's BEGIN line isn't whole");
	b->label = (span){label, (size_t)(le - label)};
	b->body = le + 5;
	b->end = find(b->body, end, "-----END ");
	if (!b->end) return fail(f, AK_KEY_INVALID, "the PEM has no END line");
	*at = b->end + 9;
	return 1;
}

static bool label_is(const block *b, const char *s) { return b->label.n == strlen(s) && memcmp(b->label.p, s, b->label.n) == 0; }

// header finds the value of a block's header name ("DEK-Info"), its line's
// rest; the body's Base64 starts after the headers (*data).
static bool header(const block *b, const char *name, span *value, const uint8_t **data) {
	const uint8_t *p = b->body, *end = b->end;
	bool found = false;
	while (p < end && (*p == '\r' || *p == '\n')) p++;
	for (;;) {
		const uint8_t *eol = p;
		while (eol < end && *eol != '\n') eol++;
		const uint8_t *colon = memchr(p, ':', (size_t)(eol - p));
		if (!colon) break;
		size_t nn = strlen(name);
		if ((size_t)(colon - p) == nn && memcmp(p, name, nn) == 0) {
			const uint8_t *v = colon + 1, *ve = eol;
			while (v < ve && (*v == ' ' || *v == '\t')) v++;
			while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ' || ve[-1] == '\t')) ve--;
			*value = (span){v, (size_t)(ve - v)};
			found = true;
		}
		p = eol < end ? eol + 1 : eol;
	}
	if (data) *data = p;
	return found;
}

// base64 decodes p[0:end) (whitespace skipped) into out.
static int base64(kf *f, const uint8_t *p, const uint8_t *end, sbuf *out) {
	uint32_t acc = 0;
	int bits = 0;
	bool padded = false;
	for (; p < end; p++) {
		uint8_t c = *p;
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
		if (c == '=') {
			padded = true;
			continue;
		}
		int v = b64_value(c);
		if (v < 0 || padded) return fail(f, AK_KEY_INVALID, "the PEM's Base64 isn't Base64");
		acc = acc << 6 | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			uint8_t byte = (uint8_t)(acc >> bits);
			sput(out, &byte, 1);
		}
	}
	return 0;
}

static int hexval(uint8_t c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// traditional decrypts a traditional PEM's body as its DEK-Info says.
static int traditional(kf *f, span dek, sbuf *body, int form, const uint8_t *pw, size_t pn, sbuf *out) {
	const uint8_t *comma = memchr(dek.p, ',', dek.n);
	if (!comma) return fail(f, AK_KEY_INVALID, "the PEM's DEK-Info isn't a cipher and its IV");
	span name = {dek.p, (size_t)(comma - dek.p)}, hex = {comma + 1, dek.n - name.n - 1};
	int cipher;
	size_t kn, ivn;
#define NAMED(s) (name.n == strlen(s) && memcmp(name.p, s, name.n) == 0)
	if (NAMED("AES-128-CBC")) cipher = C_AES, kn = 16, ivn = 16;
	else if (NAMED("AES-192-CBC")) cipher = C_AES, kn = 24, ivn = 16;
	else if (NAMED("AES-256-CBC")) cipher = C_AES, kn = 32, ivn = 16;
	else if (NAMED("DES-EDE3-CBC")) cipher = C_DES3, kn = 24, ivn = 8;
	else if (NAMED("DES-CBC")) return fail(f, AK_KEY_INVALID, "the key is encrypted with single DES, which is weak and isn't read: %s", REENCRYPT);
	else return fail(f, AK_KEY_INVALID, "the PEM's cipher (DEK-Info) isn't one that's read (AES-128-CBC, AES-192-CBC, AES-256-CBC, DES-EDE3-CBC)");
#undef NAMED
	uint8_t iv[16], key[32];
	if (hex.n != 2 * ivn) return fail(f, AK_KEY_INVALID, "the PEM's IV (DEK-Info) isn't its cipher's");
	for (size_t i = 0; i < ivn; i++) {
		int hi = hexval(hex.p[2 * i]), lo = hexval(hex.p[2 * i + 1]);
		if (hi < 0 || lo < 0) return fail(f, AK_KEY_INVALID, "the PEM's IV (DEK-Info) isn't hexadecimal");
		iv[i] = (uint8_t)(hi << 4 | lo);
	}
	evp_bytes_to_key(pw, pn, iv, key, kn);
	long n = cbc_open(cipher, key, kn, iv, body->p, body->n);
	ak_wipe(key, sizeof key);
	int got = decrypted(f, n, body->p, pn);
	if (got < 0) return got;
	if (got != form) return fail(f, AK_KEY_INVALID, "the PEM's decrypted key isn't of its label's kind");
	body->n = (size_t)n;
	*out = *body;
	*body = (sbuf){0};
	return form;
}

// ---- OpenSSH's (PROTOCOL.key) ----

static bool ssh_u32(span *s, uint32_t *v) {
	if (s->n < 4) return false;
	*v = (uint32_t)s->p[0] << 24 | (uint32_t)s->p[1] << 16 | (uint32_t)s->p[2] << 8 | s->p[3];
	s->p += 4, s->n -= 4;
	return true;
}

static bool ssh_str(span *s, span *out) {
	uint32_t n;
	if (!ssh_u32(s, &n) || n > s->n) return false;
	*out = (span){s->p, n};
	s->p += n, s->n -= n;
	return true;
}

// ssh_mpint reads an mpint that isn't negative, its leading zeros left out.
static bool ssh_mpint(span *s, span *out) {
	if (!ssh_str(s, out) || (out->n && (out->p[0] & 0x80))) return false;
	while (out->n && out->p[0] == 0) out->p++, out->n--;
	return true;
}

static bool str_is(span s, const char *c) { return s.n == strlen(c) && memcmp(s.p, c, s.n) == 0; }

enum { SSH_NONE, SSH_CTR, SSH_CBC, SSH_GCM, SSH_CHACHA };

static const struct {
	const char *name;
	int kind;
	uint8_t key, iv, block, tag;
} ssh_ciphers[] = {
	{"none", SSH_NONE, 0, 0, 8, 0},
	{"aes128-ctr", SSH_CTR, 16, 16, 16, 0},
	{"aes192-ctr", SSH_CTR, 24, 16, 16, 0},
	{"aes256-ctr", SSH_CTR, 32, 16, 16, 0},
	{"aes128-cbc", SSH_CBC, 16, 16, 16, 0},
	{"aes192-cbc", SSH_CBC, 24, 16, 16, 0},
	{"aes256-cbc", SSH_CBC, 32, 16, 16, 0},
	{"aes128-gcm@openssh.com", SSH_GCM, 16, 12, 16, 16},
	{"aes256-gcm@openssh.com", SSH_GCM, 32, 12, 16, 16},
	{"chacha20-poly1305@openssh.com", SSH_CHACHA, 64, 0, 8, 16},
};

// ssh_rsa writes OpenSSH's RSA key (n, e, d, iqmp, p, q) as PKCS#1, with
// dp = d mod (p-1) and dq = d mod (q-1).
static int ssh_rsa(kf *f, span *s, sbuf *out) {
	span n, e, d, iqmp, p, q;
	if (!ssh_mpint(s, &n) || !ssh_mpint(s, &e) || !ssh_mpint(s, &d) || !ssh_mpint(s, &iqmp) || !ssh_mpint(s, &p) || !ssh_mpint(s, &q))
		return fail(f, AK_KEY_INVALID, "the OpenSSH RSA key's parts can't be read");
	if (!p.n || !q.n || !d.n || !(p.p[p.n - 1] & 1) || !(q.p[q.n - 1] & 1)) return fail(f, AK_KEY_INVALID, "the OpenSSH RSA key's primes aren't odd");
	sbuf seq = {0};
	put_tlv(&seq, 0x02, (const uint8_t[]){0}, 1);
	put_uint(&seq, n);
	put_uint(&seq, e);
	put_uint(&seq, d);
	put_uint(&seq, p);
	put_uint(&seq, q);
	const span *primes[] = {&p, &q};
	for (int i = 0; i < 2; i++) {
		span pr = *primes[i];
		size_t words = (8 * (d.n > pr.n ? d.n : pr.n) + 30) / 31 + 2;
		uint32_t *a = ak_alloc(4 * words), *m = ak_alloc(4 * words), *x = ak_alloc(4 * words);
		uint8_t *pm = ak_alloc(pr.n), *r = ak_alloc(pr.n);
		memcpy(pm, pr.p, pr.n);
		pm[pr.n - 1] &= 0xfe;  // p - 1: p is odd
		br_i31_decode(a, d.p, d.n);
		br_i31_decode(m, pm, pr.n);
		br_i31_reduce(x, a, m);
		br_i31_encode(r, pr.n, x);
		put_uint(&seq, (span){r, pr.n});
		ak_wipe(a, 4 * words), ak_wipe(m, 4 * words), ak_wipe(x, 4 * words), ak_wipe(r, pr.n), ak_wipe(pm, pr.n);
		ak_free(a), ak_free(m), ak_free(x), ak_free(pm), ak_free(r);
	}
	put_uint(&seq, iqmp);
	wrap(out, 0x30, &seq);
	return AK_KEY_PKCS1;
}

static int ssh_ecdsa(kf *f, span type, span *s, sbuf *out) {
	span curve, point, d;
	if (!ssh_str(s, &curve) || !ssh_str(s, &point) || !ssh_mpint(s, &d)) return fail(f, AK_KEY_INVALID, "the OpenSSH ECDSA key's parts can't be read");
	const uint8_t *oid;
	size_t on, c;
	if (str_is(curve, "nistp256") && str_is(type, "ecdsa-sha2-nistp256")) oid = OID_P256, on = sizeof OID_P256, c = 32;
	else if (str_is(curve, "nistp384") && str_is(type, "ecdsa-sha2-nistp384")) oid = OID_P384, on = sizeof OID_P384, c = 48;
	else if (str_is(curve, "nistp521") && str_is(type, "ecdsa-sha2-nistp521")) oid = OID_P521, on = sizeof OID_P521, c = 66;
	else return fail(f, AK_KEY_INVALID, "the OpenSSH ECDSA key's curve isn't P-256, P-384 or P-521");
	if (d.n > c) return fail(f, AK_KEY_INVALID, "the OpenSSH ECDSA key's scalar is longer than its curve's");
	uint8_t x[66] = {0};
	memcpy(x + c - d.n, d.p, d.n);
	sbuf seq = {0}, tagged = {0}, bits = {0};
	put_tlv(&seq, 0x02, (const uint8_t[]){1}, 1);
	put_tlv(&seq, 0x04, x, c);
	ak_wipe(x, sizeof x);
	put_tlv(&tagged, 0x06, oid, on);
	wrap(&seq, 0xa0, &tagged);
	sput(&bits, (const uint8_t[]){0}, 1);
	sput(&bits, point.p, point.n);
	wrap(&tagged, 0x03, &bits);
	wrap(&seq, 0xa1, &tagged);
	wrap(out, 0x30, &seq);
	return AK_KEY_SEC1;
}

static int ssh_ed25519(kf *f, span *s, sbuf *out) {
	span pub, priv;
	if (!ssh_str(s, &pub) || !ssh_str(s, &priv) || pub.n != 32 || priv.n != 64 || memcmp(priv.p + 32, pub.p, 32) != 0)
		return fail(f, AK_KEY_INVALID, "the OpenSSH Ed25519 key's parts can't be read");
	sbuf seq = {0}, alg = {0}, inner = {0};
	put_tlv(&seq, 0x02, (const uint8_t[]){0}, 1);
	put_tlv(&alg, 0x06, OID_ED25519, sizeof OID_ED25519);
	wrap(&seq, 0x30, &alg);
	put_tlv(&inner, 0x04, priv.p, 32);
	wrap(&seq, 0x04, &inner);
	wrap(out, 0x30, &seq);
	return AK_KEY_PKCS8;
}

static int openssh(kf *f, span s, const uint8_t *pw, size_t pn, sbuf *out) {
	static const char magic[] = "openssh-key-v1";
	if (s.n < sizeof magic || memcmp(s.p, magic, sizeof magic) != 0) return fail(f, AK_KEY_INVALID, "it isn't an OpenSSH private key (openssh-key-v1)");
	s.p += sizeof magic, s.n -= sizeof magic;
	span cname, kdf, kopts, pub, enc;
	uint32_t nkeys, elen;
	if (!ssh_str(&s, &cname) || !ssh_str(&s, &kdf) || !ssh_str(&s, &kopts) || !ssh_u32(&s, &nkeys)) return fail(f, AK_KEY_INVALID, "the OpenSSH key can't be read");
	if (nkeys != 1) return fail(f, AK_KEY_INVALID, "the OpenSSH file has %u keys: one is read", nkeys);
	if (!ssh_str(&s, &pub) || !ssh_u32(&s, &elen) || elen > s.n) return fail(f, AK_KEY_INVALID, "the OpenSSH key can't be read");
	enc = (span){s.p, elen};
	s.p += elen, s.n -= elen;
	bool plain = str_is(cname, "none");
	sbuf b = {0};
	if (plain) {
		if (s.n || enc.n % 8 || enc.n == 0) return fail(f, AK_KEY_INVALID, "the OpenSSH key's private part isn't whole");
		if (!str_is(kdf, "none")) return fail(f, AK_KEY_INVALID, "the OpenSSH key's key derivation isn't none, for no cipher");
		sput(&b, enc.p, enc.n);
	} else {
		if (!f->dec) return fail(f, AK_KEY_ENCRYPTED, "the key is encrypted");
		int r = f->dec->openssh(f, cname, kdf, kopts, enc, s, pw, pn, &b);
		if (r < 0) {
			sfree(&b);
			return r;
		}
	}
	span p = {b.p, b.n}, type, ptype;
	uint32_t c1, c2;
	if (!ssh_u32(&p, &c1) || !ssh_u32(&p, &c2) || c1 != c2) {
		sfree(&b);
		return plain ? fail(f, AK_KEY_INVALID, "the OpenSSH key's check words don't agree") : fail(f, AK_KEY_PASSWORD, "the password is wrong: it doesn't decrypt the key");
	}
	span pubs = pub;
	int form;
	if (!ssh_str(&p, &type) || !ssh_str(&pubs, &ptype) || !span_is(type, ptype.p, ptype.n)) form = fail(f, AK_KEY_INVALID, "the OpenSSH key's private part isn't its public key's");
	else if (str_is(type, "ssh-ed25519")) form = ssh_ed25519(f, &p, out);
	else if (str_is(type, "ssh-rsa")) form = ssh_rsa(f, &p, out);
	else if (type.n > 11 && memcmp(type.p, "ecdsa-sha2-", 11) == 0) form = ssh_ecdsa(f, type, &p, out);
	else if (type.n > 3 && memcmp(type.p, "sk-", 3) == 0) form = fail(f, AK_KEY_INVALID, "it's a security key's (%.*s): its private key is on the security key", (int)(type.n > 40 ? 40 : type.n), (const char *)type.p);
	else if (str_is(type, "ssh-dss")) form = fail(f, AK_KEY_INVALID, "DSA keys aren't read: RSA, ECDSA (P-256, P-384, P-521) or Ed25519");
	else form = fail(f, AK_KEY_INVALID, "the OpenSSH key's kind (%.*s) isn't read: RSA, ECDSA or Ed25519", (int)(type.n > 40 ? 40 : type.n), (const char *)type.p);
	// Then its comment, and padding of 1, 2, 3, ...
	span comment;
	if (form > 0 && !ssh_str(&p, &comment)) form = fail(f, AK_KEY_INVALID, "the OpenSSH key's private part isn't whole");
	for (size_t i = 0; form > 0 && i < p.n; i++)
		if (p.p[i] != (uint8_t)(i + 1)) form = fail(f, AK_KEY_INVALID, "the OpenSSH key's padding isn't");
	if (form < 0) sfree(out);
	sfree(&b);
	return form;
}

// ---- Decrypting ----
//
// What decrypts the encrypted forms, reached only through decrypting
// (ak_key_decrypt's): a link that reads no encrypted keys has none of
// it, nor the ciphers and key derivations under it.

// ssh_decrypt decrypts an OpenSSH key's private part (enc, its tag after
// it in rest) into b: cname's cipher, kdf's bcrypt of kopts.
static int ssh_decrypt(kf *f, span cname, span kdf, span kopts, span enc, span rest, const uint8_t *pw, size_t pn, sbuf *b) {
	int ci = -1;
	for (size_t i = 0; i < AK_LEN(ssh_ciphers); i++)
		if (str_is(cname, ssh_ciphers[i].name)) ci = (int)i;
	if (ci < 0) return fail(f, AK_KEY_INVALID, "the OpenSSH key's cipher (%.*s) isn't one that's read: ssh-keygen -p -Z aes256-ctr -f key encrypts it again", (int)(cname.n > 40 ? 40 : cname.n), (const char *)cname.p);
	int kind = ssh_ciphers[ci].kind;
	size_t tag = ssh_ciphers[ci].tag, bs = ssh_ciphers[ci].block;
	if (rest.n != tag || enc.n % bs || enc.n == 0) return fail(f, AK_KEY_INVALID, "the OpenSSH key's private part isn't whole");
	if (!str_is(kdf, "bcrypt")) return fail(f, AK_KEY_INVALID, "the OpenSSH key's key derivation isn't bcrypt");
	span salt;
	uint32_t rounds;
	if (!ssh_str(&kopts, &salt) || !ssh_u32(&kopts, &rounds) || rounds == 0) return fail(f, AK_KEY_INVALID, "the OpenSSH key's bcrypt parameters can't be read");
	// ssh-keygen encrypts no key with an empty passphrase.
	if (!pn) return fail(f, AK_KEY_PASSWORD, "the key is encrypted: its password is needed");
	sput(b, enc.p, enc.n);
	uint8_t k[80];
	size_t kn = ssh_ciphers[ci].key, ivn = ssh_ciphers[ci].iv;
	ak_bcrypt_pbkdf(pw, pn, salt.p, salt.n, rounds, k, kn + ivn);
	bool ok = true;
	switch (kind) {
	case SSH_CTR: ctr(k, kn, k + kn, b->p, b->n); break;
	case SSH_CBC: {
		br_aes_ct64_cbcdec_keys c;
		uint8_t iv[16];
		memcpy(iv, k + kn, 16);
		br_aes_ct64_cbcdec_init(&c, k, kn);
		br_aes_ct64_cbcdec_run(&c, iv, b->p, b->n);
		ak_wipe(&c, sizeof c);
		break;
	}
	case SSH_GCM: ok = gcm_open(k, kn, k + kn, b->p, b->n, rest.p); break;
	default: ok = chachapoly_open(k, b->p, b->n, rest.p);
	}
	ak_wipe(k, sizeof k);
	if (!ok) return fail(f, AK_KEY_PASSWORD, "the password is wrong: it doesn't decrypt the key");
	return 0;
}

static const struct decryptor decrypting = {encrypted_pkcs8, traditional, ssh_decrypt};

// ---- Reading ----

static int read_der(kf *f, span s, const uint8_t *pw, size_t pn, sbuf *out) {
	if (s.n >= 15 && memcmp(s.p, "openssh-key-v1", 15) == 0) return openssh(f, s, pw, pn, out);
	span t = s, seq;
	if (der_get(&t, 0x30, &seq) && seq.n && seq.p[0] == 0x30) return f->dec ? f->dec->pkcs8(f, s, pw, pn, out) : fail(f, AK_KEY_ENCRYPTED, "the key is encrypted");
	int form = key_form(s);
	if (!form) return fail(f, AK_KEY_INVALID, "it isn't a private key (PKCS#8's, PKCS#1's or SEC 1's DER)");
	sput(out, s.p, s.n);
	return form;
}

static int read_pem(kf *f, const uint8_t *p, size_t n, const uint8_t *pw, size_t pn, sbuf *out) {
	const uint8_t *at = p, *end = p + n;
	while (at < end && (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')) at++;
	if (!find(at, end, "-----BEGIN ")) return fail(f, AK_KEY_INVALID, "it isn't PEM (-----BEGIN PRIVATE KEY-----)");
	for (;;) {
		block b;
		int r = next_block(f, &at, end, &b);
		if (r < 0) return r;
		if (r == 0) break;
		int form = 0;
		bool plain = true;
		if (label_is(&b, "PRIVATE KEY")) form = AK_KEY_PKCS8;
		else if (label_is(&b, "RSA PRIVATE KEY")) form = AK_KEY_PKCS1;
		else if (label_is(&b, "EC PRIVATE KEY")) form = AK_KEY_SEC1;
		else if (label_is(&b, "ENCRYPTED PRIVATE KEY") || label_is(&b, "OPENSSH PRIVATE KEY")) plain = false;
		else if (label_is(&b, "DSA PRIVATE KEY")) return fail(f, AK_KEY_INVALID, "DSA keys aren't read: RSA, ECDSA (P-256, P-384, P-521) or Ed25519");
		else continue;
		span proc, dek;
		const uint8_t *body;
		bool enc = header(&b, "Proc-Type", &proc, &body);
		bool has_dek = header(&b, "DEK-Info", &dek, NULL);
		sbuf der = {0};
		if (base64(f, body, b.end, &der)) {
			sfree(&der);
			return f->code;
		}
		span s = {der.p, der.n};
		if (!plain) {
			if (label_is(&b, "OPENSSH PRIVATE KEY")) r = openssh(f, s, pw, pn, out);
			else r = f->dec ? f->dec->pkcs8(f, s, pw, pn, out) : fail(f, AK_KEY_ENCRYPTED, "the key is encrypted");
		} else if (enc) {
			if (!str_is(proc, "4,ENCRYPTED") || !has_dek) r = fail(f, AK_KEY_INVALID, "the PEM's headers aren't an encrypted key's (Proc-Type: 4,ENCRYPTED and DEK-Info)");
			else r = f->dec ? f->dec->traditional(f, dek, &der, form, pw, pn, out) : fail(f, AK_KEY_ENCRYPTED, "the key is encrypted");
		} else {
			int got = key_form(s);
			if (got != form) {
				static const char *const names[] = {"", "PKCS#8's PrivateKeyInfo", "PKCS#1's RSAPrivateKey", "SEC 1's ECPrivateKey"};
				r = fail(f, AK_KEY_INVALID, "the PEM's %.*s isn't %s", (int)b.label.n, (const char *)b.label.p, names[form]);
			} else {
				*out = der;
				der = (sbuf){0};
				r = form;
			}
		}
		sfree(&der);
		return r;
	}
	return fail(f, AK_KEY_INVALID, "the PEM has no private key in it (PRIVATE KEY, ENCRYPTED PRIVATE KEY, RSA PRIVATE KEY, EC PRIVATE KEY or OPENSSH PRIVATE KEY)");
}

static int read_key(const struct decryptor *dec, const uint8_t *p, size_t n, bool pem, const uint8_t *pw, size_t pn, uint8_t **der, size_t *dn, char *why, size_t cap) {
	kf f = {why, cap, 0, dec};
	if (cap) why[0] = 0;
	sbuf out = {0};
	int form = pem ? read_pem(&f, p, n, pw, pn, &out) : read_der(&f, (span){p, n}, pw, pn, &out);
	if (form < 0) {
		sfree(&out);
		*der = NULL, *dn = 0;
		return form;
	}
	*der = out.p, *dn = out.n;
	return form;
}

int ak_key_read(const uint8_t *p, size_t n, bool pem, uint8_t **der, size_t *dn, char *why, size_t cap) {
	return read_key(NULL, p, n, pem, NULL, 0, der, dn, why, cap);
}

int ak_key_decrypt(const uint8_t *p, size_t n, bool pem, const uint8_t *pw, size_t pn, uint8_t **der, size_t *dn, char *why, size_t cap) {
	return read_key(&decrypting, p, n, pem, pw, pn, der, dn, why, cap);
}

// ---- Writing ----

void ak_key_encrypt(const uint8_t *der, size_t n, const uint8_t *pw, size_t pn, uint32_t iterations, uint8_t **out, size_t *on) {
	uint8_t salt[16], iv[16], key[32];
	if (!ak_os_random(salt, sizeof salt) || !ak_os_random(iv, sizeof iv)) ak_panic("crypto: the system has no randomness to give");
	ak_pbkdf2(AK_HASH_SHA256, pw, pn, salt, sizeof salt, iterations, key, sizeof key);
	sbuf ct = {0};
	size_t pad = 16 - n % 16;
	sput(&ct, der, n);
	for (size_t i = 0; i < pad; i++) sput(&ct, (const uint8_t[]){(uint8_t)pad}, 1);
	br_aes_ct64_cbcenc_keys k;
	uint8_t v[16];
	memcpy(v, iv, 16);
	br_aes_ct64_cbcenc_init(&k, key, sizeof key);
	br_aes_ct64_cbcenc_run(&k, v, ct.p, ct.n);
	ak_wipe(&k, sizeof k);
	ak_wipe(key, sizeof key);
	// SEQUENCE { SEQUENCE { PBES2, SEQUENCE { SEQUENCE { PBKDF2, SEQUENCE {
	// salt, iterations, SEQUENCE { hmacWithSHA256, NULL } } }, SEQUENCE {
	// aes256-CBC, iv } } }, OCTET STRING ciphertext }
	sbuf prf = {0}, kp = {0}, kdf = {0}, enc = {0}, params = {0}, alg = {0}, all = {0};
	put_tlv(&prf, 0x06, OID_HMAC_SHA256, sizeof OID_HMAC_SHA256);
	sput(&prf, (const uint8_t[]){0x05, 0x00}, 2);
	put_tlv(&kp, 0x04, salt, sizeof salt);
	put_small(&kp, iterations);
	wrap(&kp, 0x30, &prf);
	put_tlv(&kdf, 0x06, OID_PBKDF2, sizeof OID_PBKDF2);
	wrap(&kdf, 0x30, &kp);
	put_tlv(&enc, 0x06, OID_AES256_CBC, sizeof OID_AES256_CBC);
	put_tlv(&enc, 0x04, iv, sizeof iv);
	wrap(&params, 0x30, &kdf);
	wrap(&params, 0x30, &enc);
	put_tlv(&alg, 0x06, OID_PBES2, sizeof OID_PBES2);
	wrap(&alg, 0x30, &params);
	wrap(&all, 0x30, &alg);
	put_tlv(&all, 0x04, ct.p, ct.n);
	sfree(&ct);
	sbuf top = {0};
	wrap(&top, 0x30, &all);
	*out = top.p, *on = top.n;
}

// ---- Builtins ----

// The calling thread's last failure (crypto_keyfile.keyFileFailure), and
// its status.
static AK_TLS char failure[300];
static AK_TLS int status;

static ak_val result(int r, uint8_t *der, size_t dn) {
	status = r < 0 ? r : 0;
	if (r < 0) return (ak_val){.p = NULL};
	ak_bytes *out = ak_bytes_new(der, dn);
	ak_wipe(der, dn);
	ak_free(der);
	return (ak_val){.p = out};
}

// readKeyFile(data, pem): the private key's DER (PKCS#8, PKCS#1 or SEC 1:
// crypto_pk.readPrivate makes PKCS#8 of it), not encrypted; none, with
// keyFileFailure and keyFileStatus saying why.
static ak_val read_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *in = BYTES(0);
	uint8_t *der;
	size_t dn;
	int r = ak_key_read(data(in), len(in), args[1].i != 0, &der, &dn, failure, sizeof failure);
	return result(r, der, dn);
}

static ak_val failure_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.p = ak_str_c(failure)};
}

// keyFileStatus(): the last read's: 0, or AK_KEY_INVALID, AK_KEY_PASSWORD,
// AK_KEY_ENCRYPTED.
static ak_val status_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.i = status};
}

const ak_lib_entry ak_std_crypto_keyfile[] = {
	{"crypto_keyfile.readKeyFile", read_fn},
	{"crypto_keyfile.keyFileFailure", failure_fn},
	{"crypto_keyfile.keyFileStatus", status_fn},
	{NULL, NULL},
};

// decryptKeyFile(data, pem, password): readKeyFile's, encrypted keys
// decrypted with password.
static ak_val decrypt_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *in = BYTES(0), *pw = BYTES(2);
	uint8_t *der;
	size_t dn;
	int r = ak_key_decrypt(data(in), len(in), args[1].i != 0, data(pw), len(pw), &der, &dn, failure, sizeof failure);
	return result(r, der, dn);
}

const ak_lib_entry ak_std_crypto_keydecrypt[] = {
	{"crypto_keydecrypt.decryptKeyFile", decrypt_fn},
	{NULL, NULL},
};

// encryptKey(der, password, iterations): PKCS#8 der as
// EncryptedPrivateKeyInfo (DER), for PEM's ENCRYPTED PRIVATE KEY.
static ak_val encrypt_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *der = BYTES(0), *pw = BYTES(1);
	int64_t it = args[2].i;
	uint8_t *out;
	size_t on;
	ak_key_encrypt(data(der), len(der), data(pw), len(pw), (uint32_t)(it < 1 ? 1 : it > UINT32_MAX ? UINT32_MAX : it), &out, &on);
	ak_bytes *b = ak_bytes_new(out, on);
	ak_free(out);
	return (ak_val){.p = b};
}

const ak_lib_entry ak_std_crypto_keyenc[] = {
	{"crypto_keyenc.encryptKey", encrypt_fn},
	{NULL, NULL},
};
