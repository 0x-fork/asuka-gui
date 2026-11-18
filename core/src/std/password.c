// password.c: std/crypto's password hashing, the builtins
// crypto_password.* (their own table: an app that stores no passwords
// links none of it). Argon2 (RFC 9106: argon2id, and argon2i and argon2d
// to verify) over Monocypher (third_party/monocypher), its hashes in the
// PHC string format ($argon2id$v=19$m=65536,t=3,p=4$salt$hash, the
// Base64 without padding); bcrypt ($2b$, and $2a$ and $2y$ to verify)
// over Solar Designer's crypt_blowfish (third_party/crypt_blowfish), for
// the hashes databases already hold. Both take time and memory on purpose,
// so the Asuka side runs them in spawn's workers.
#include "std/std.h"

#include <stdio.h>
#include <stdlib.h>

#include "std/monocypher.h"

// crypt_blowfish, compiled in here, its exported names the core's.
#define _crypt_output_magic ak_bf_output_magic
#define _crypt_blowfish_rn ak_bf_blowfish_rn
#define _crypt_gensalt_blowfish_rn ak_bf_gensalt_blowfish_rn
#include "../../third_party/crypt_blowfish/crypt_blowfish.c"

#define STR(i) ((ak_str *)args[i].p)
#define BYTES(i) ((ak_bytes *)args[i].p)

static const uint8_t *data(const ak_bytes *b) { return b ? b->data : (const uint8_t *)""; }
static size_t len(const ak_bytes *b) { return b ? b->len : 0; }

// The calling thread's last failure's reason (crypto_password.passwordFailure).
static AK_TLS char failure[160];

static void fail(const char *why) { snprintf(failure, sizeof failure, "%s", why); }

// ---- Argon2 ----

// The bounds RFC 9106 sets, but memory, which is at most 4 GiB here.
#define MAX_MEMORY (UINT32_C(1) << 22)  // KiB
#define MAX_LANES ((UINT32_C(1) << 24) - 1)

static bool argon2_params(int64_t memory, int64_t iterations, int64_t lanes) {
	if (lanes < 1 || lanes > MAX_LANES) {
		fail("argon2's parallelism is 1 to 16777215");
		return false;
	}
	if (memory < 8 * lanes || memory > MAX_MEMORY) {
		fail("argon2's memory is 8 KiB a lane (its parallelism) to 4 GiB (4194304 KiB)");
		return false;
	}
	if (iterations < 1 || iterations > UINT32_MAX) {
		fail("argon2's iterations are 1 to 4294967295");
		return false;
	}
	return true;
}

// argon2 writes the n-byte tag into out; false (failure says why) when
// memory can't be had. The work area is the only large allocation the
// core lets fail: its size comes from the caller, or from a stored hash.
static bool argon2(uint32_t variant, const uint8_t *pw, size_t pn, const uint8_t *salt, size_t sn, uint32_t memory,
	uint32_t iterations, uint32_t lanes, uint8_t *out, size_t n) {
	void *work = (uint64_t)memory * 1024 <= SIZE_MAX ? malloc((size_t)memory * 1024) : NULL;
	if (!work) {
		snprintf(failure, sizeof failure, "argon2: there isn't %u KiB of memory to hash with", memory);
		return false;
	}
	crypto_argon2_config cfg = {.algorithm = variant, .nb_blocks = memory, .nb_passes = iterations, .nb_lanes = lanes};
	crypto_argon2_inputs in = {.pass = pw, .salt = salt, .pass_size = (uint32_t)pn, .salt_size = (uint32_t)sn};
	crypto_argon2(out, (uint32_t)n, work, cfg, in, crypto_argon2_no_extras);
	free(work);
	return true;
}

// argon2(variant, password, salt, length, memory, iterations,
// parallelism): the raw tag, a key derived from a password; none when
// memory can't be had or the parameters are out of range (failure).
static ak_val argon2_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t variant = args[0].i, want = args[3].i;
	const ak_bytes *pw = BYTES(1), *salt = BYTES(2);
	if (variant < CRYPTO_ARGON2_D || variant > CRYPTO_ARGON2_ID) {
		fail("no such argon2");
		return (ak_val){.p = NULL};
	}
	if (len(salt) < 8 || want < 4 || want > INT32_MAX || len(pw) > UINT32_MAX) {
		fail("argon2 takes a salt of 8 bytes or more and makes 4 bytes or more");
		return (ak_val){.p = NULL};
	}
	if (!argon2_params(args[4].i, args[5].i, args[6].i)) return (ak_val){.p = NULL};
	ak_bytes *out = ak_bytes_new(NULL, (size_t)want);
	if (!argon2((uint32_t)variant, data(pw), len(pw), data(salt), len(salt), (uint32_t)args[4].i, (uint32_t)args[5].i,
		    (uint32_t)args[6].i, out->data, (size_t)want)) {
		ak_release(out);
		return (ak_val){.p = NULL};
	}
	return (ak_val){.p = out};
}

// PHC's Base64: the standard alphabet, no padding.
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *p, size_t n, char *out) {
	size_t o = 0;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
		out[o++] = B64[v >> 18 & 63];
		out[o++] = B64[v >> 12 & 63];
		if (i + 1 < n) out[o++] = B64[v >> 6 & 63];
		if (i + 2 < n) out[o++] = B64[v & 63];
	}
	return o;
}

// b64_decode reads s[0:n] into out (room for n*3/4 bytes): its length,
// or -1 for what isn't PHC's Base64 (padding, other characters, or bits
// left over that aren't zero).
static long b64_decode(const char *s, size_t n, uint8_t *out) {
	uint32_t acc = 0;
	int bits = 0;
	long o = 0;
	if (n % 4 == 1) return -1;
	for (size_t i = 0; i < n; i++) {
		const char *at = s[i] ? strchr(B64, s[i]) : NULL;
		if (!at) return -1;
		acc = acc << 6 | (uint32_t)(at - B64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[o++] = (uint8_t)(acc >> bits);
		}
	}
	if (bits && (acc & ((1u << bits) - 1))) return -1;
	return o;
}

// hashArgon2(password, memory, iterations, parallelism): argon2id of
// password with a 16-byte salt from the system's randomness and a 32-byte
// tag (RFC 9106's), as a PHC string; none (failure says why) when memory
// can't be had.
static ak_val hash_argon2(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *pw = STR(0);
	if (!argon2_params(args[1].i, args[2].i, args[3].i)) return (ak_val){.p = NULL};
	uint8_t salt[16], tag[32];
	if (!ak_os_random(salt, sizeof salt)) {
		fail("the system gave no randomness");
		return (ak_val){.p = NULL};
	}
	uint32_t m = (uint32_t)args[1].i, t = (uint32_t)args[2].i, p = (uint32_t)args[3].i;
	if (!argon2(CRYPTO_ARGON2_ID, (const uint8_t *)pw->data, pw->len, salt, sizeof salt, m, t, p, tag, sizeof tag))
		return (ak_val){.p = NULL};
	char s[160];
	int k = snprintf(s, sizeof s, "$argon2id$v=19$m=%u,t=%u,p=%u$", m, t, p);
	k += (int)b64_encode(salt, sizeof salt, s + k);
	s[k++] = '$';
	k += (int)b64_encode(tag, sizeof tag, s + k);
	ak_wipe(tag, sizeof tag);
	return (ak_val){.p = ak_str_new(s, (size_t)k)};
}

// num reads a decimal number at *p (no sign, no leading zero) up to the
// byte stop; false for none, or one past max.
static bool num(const char **p, char stop, uint64_t max, uint64_t *v) {
	const char *s = *p;
	uint64_t x = 0;
	if (*s < '0' || *s > '9' || (*s == '0' && s[1] >= '0' && s[1] <= '9')) return false;
	for (; *s >= '0' && *s <= '9'; s++) {
		x = x * 10 + (uint64_t)(*s - '0');
		if (x > max) return false;
	}
	if (*s != stop) return false;
	*p = s + 1;
	*v = x;
	return true;
}

// verify_argon2: 1 when password's hash under h's parameters is h's, 0
// when it isn't, -1 when h isn't a PHC Argon2 string this reads.
static int verify_argon2(const ak_str *pw, const char *h) {
	uint32_t variant;
	const char *p;
	if (strncmp(h, "$argon2id$", 10) == 0) variant = CRYPTO_ARGON2_ID, p = h + 10;
	else if (strncmp(h, "$argon2i$", 9) == 0) variant = CRYPTO_ARGON2_I, p = h + 9;
	else if (strncmp(h, "$argon2d$", 9) == 0) variant = CRYPTO_ARGON2_D, p = h + 9;
	else return -1;
	uint64_t v = 0x13, m, t, l;
	if (strncmp(p, "v=", 2) == 0) {
		p += 2;
		if (!num(&p, '$', 0xff, &v)) return -1;
	}
	if (v != 0x13) {
		fail("argon2: only version 19 (0x13, RFC 9106's) is read");
		return -1;
	}
	if (strncmp(p, "m=", 2) != 0) return -1;
	p += 2;
	if (!num(&p, ',', UINT32_MAX, &m) || strncmp(p, "t=", 2) != 0) return -1;
	p += 2;
	if (!num(&p, ',', UINT32_MAX, &t) || strncmp(p, "p=", 2) != 0) return -1;
	p += 2;
	if (!num(&p, '$', UINT32_MAX, &l)) return -1;
	if (!argon2_params((int64_t)m, (int64_t)t, (int64_t)l)) return -1;
	const char *dollar = strchr(p, '$');
	if (!dollar) return -1;
	size_t sn = (size_t)(dollar - p), hn = strlen(dollar + 1);
	if (sn > 128 || hn > 128) return -1;
	uint8_t salt[96], want[96], got[96];
	long sl = b64_decode(p, sn, salt), hl = b64_decode(dollar + 1, hn, want);
	if (sl < 8 || hl < 4) return -1;
	if (!argon2(variant, (const uint8_t *)pw->data, pw->len, salt, (size_t)sl, (uint32_t)m, (uint32_t)t, (uint32_t)l, got,
		    (size_t)hl))
		return -2;
	uint8_t diff = 0;
	for (long i = 0; i < hl; i++) diff |= got[i] ^ want[i];
	ak_wipe(got, sizeof got);
	return diff == 0;
}

// ---- bcrypt ----

// hashBcrypt(password, cost): $2b$ with a 16-byte salt from the system's
// randomness; none (failure says why) for a password crypt_blowfish
// can't take whole: over 72 bytes, or with a NUL in it.
static ak_val hash_bcrypt(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *pw = STR(0);
	int64_t cost = args[1].i;
	if (cost < 4 || cost > 31) {
		fail("bcrypt's cost is 4 to 31");
		return (ak_val){.p = NULL};
	}
	if (pw->len > 72 || memchr(pw->data, 0, pw->len)) {
		fail(pw->len > 72 ? "bcrypt hashes 72 bytes of a password at most: this one is longer (argon2id hashes any length)"
				  : "bcrypt can't hash a password with a NUL character in it");
		return (ak_val){.p = NULL};
	}
	uint8_t salt[16];
	if (!ak_os_random(salt, sizeof salt)) {
		fail("the system gave no randomness");
		return (ak_val){.p = NULL};
	}
	char setting[32], out[64];
	if (!_crypt_gensalt_blowfish_rn("$2b$", (unsigned long)cost, (const char *)salt, sizeof salt, setting, sizeof setting) ||
	    !_crypt_blowfish_rn(pw->data, setting, out, sizeof out)) {
		fail("bcrypt failed");
		return (ak_val){.p = NULL};
	}
	return (ak_val){.p = ak_str_c(out)};
}

// verify_bcrypt: as verify_argon2, for $2a$, $2b$ and $2y$. A password
// over 72 bytes is read as its first 72, as every bcrypt reads it (what
// the stored hash was made of); one with a NUL in it never matches.
static int verify_bcrypt(const ak_str *pw, const char *h) {
	if (strlen(h) != 60 || h[0] != '$' || h[1] != '2' || (h[2] != 'a' && h[2] != 'b' && h[2] != 'y') || h[3] != '$')
		return -1;
	char out[64];
	if (!_crypt_blowfish_rn(pw->data, h, out, sizeof out)) return -1;
	if (memchr(pw->data, 0, pw->len)) return 0;
	uint8_t diff = 0;
	for (int i = 0; i < 60; i++) diff |= (uint8_t)(out[i] ^ h[i]);
	ak_wipe(out, sizeof out);
	return diff == 0;
}

// verifyHash(password, hash): 1 when hash is password's, 0 when it isn't,
// -1 when hash is no hash this reads (failure says why), -2 when memory
// to verify it can't be had (failure).
static ak_val verify(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *pw = STR(0), *h = STR(1);
	failure[0] = 0;
	int r = -1;
	if (memchr(h->data, 0, h->len)) r = -1;
	else if (strncmp(h->data, "$argon2", 7) == 0) r = verify_argon2(pw, h->data);
	else if (strncmp(h->data, "$2", 2) == 0) r = verify_bcrypt(pw, h->data);
	if (r == -1 && !failure[0]) fail("it isn't an argon2 hash in the PHC format ($argon2id$v=19$...) or a bcrypt hash ($2b$...)");
	return (ak_val){.i = r};
}

static ak_val failure_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.p = ak_str_c(failure)};
}

const ak_lib_entry ak_std_crypto_password[] = {
	{"crypto_password.argon2", argon2_fn},
	{"crypto_password.hashArgon2", hash_argon2},
	{"crypto_password.hashBcrypt", hash_bcrypt},
	{"crypto_password.verifyHash", verify},
	{"crypto_password.passwordFailure", failure_fn},
	{NULL, NULL},
};
