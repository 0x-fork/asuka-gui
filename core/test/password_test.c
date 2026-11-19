// std/crypto's password hashing (core/src/std/password.c): Argon2 (RFC
// 9106's vectors, through argon2.c and Monocypher, and the two compared
// over many parameters; PHC strings Go's x/crypto/argon2 made), bcrypt (crypt_blowfish's own vectors, and one Go's
// x/crypto/bcrypt made), and what they refuse.
#include "t.h"
#include "std/std.h"
#include "std/argon2.h"
#include "std/cryptohw.h"

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

static void hex(const uint8_t *p, size_t n, char *out) {
	for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

TEST(argon2_rfc9106) {
	// RFC 9106, 5.1 to 5.3: m = 32 KiB, t = 3, p = 4, a secret and
	// associated data.
	static const struct { uint32_t alg; const char *want; } cases[] = {
		{CRYPTO_ARGON2_D, "512b391b6f1162975371d30919734294f868e3be3984f3c1a13a4db9fabe4acb"},
		{CRYPTO_ARGON2_I, "c814d9d1dc7f37aa13f0d77f2494bda1c8de6b016dd388d29952a4c4672b6ce8"},
		{CRYPTO_ARGON2_ID, "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659"},
	};
	uint8_t pw[32], salt[16], secret[8], ad[12], tag[32];
	memset(pw, 1, sizeof pw), memset(salt, 2, sizeof salt), memset(secret, 3, sizeof secret), memset(ad, 4, sizeof ad);
	void *work = malloc(32 * 1024);
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		crypto_argon2_config cfg = {.algorithm = cases[i].alg, .nb_blocks = 32, .nb_passes = 3, .nb_lanes = 4};
		crypto_argon2_inputs in = {.pass = pw, .salt = salt, .pass_size = sizeof pw, .salt_size = sizeof salt};
		crypto_argon2_extras ex = {.key = secret, .ad = ad, .key_size = sizeof secret, .ad_size = sizeof ad};
		char h[65];
		crypto_argon2(tag, sizeof tag, work, cfg, in, ex);
		hex(tag, 32, h);
		CHECK_EQ_STR(h, cases[i].want);
		for (int mode = 0; mode < 4; mode++) {
			ak_cryptohw_off = mode & 1;
			ak_argon2_threads = mode & 2 ? 1 : 0;
			memset(tag, 0, sizeof tag);
			ak_argon2(tag, sizeof tag, work, cfg, in, ex);
			hex(tag, 32, h);
			CHECK_EQ_STR(h, cases[i].want);
		}
		ak_cryptohw_off = false;
		ak_argon2_threads = 0;
	}
	free(work);
}

// argon2.c makes Monocypher's bytes: each variant, lanes 1 to 9 (on
// threads and not), memory not a multiple of four lanes, passes 1 to 3,
// the address blocks' refresh (past 128 blocks a segment), tags short and
// long (H' past 64 bytes), with a secret and associated data or without;
// the vector unit's rounds (on x86-64 AVX2's and SSE2's) and the C's.
TEST(argon2_as_monocypher) {
	uint64_t r = 0x853c49e6748fea9bull;
	static const struct { uint32_t m, t, p; } shapes[] = {
		{8, 1, 1}, {16, 2, 2}, {33, 3, 3}, {64, 1, 4}, {72, 2, 9}, {600, 2, 1}, {1100, 1, 2}, {1024, 3, 4}, {2048, 1, 8},
	};
	uint8_t pw[40], salt[24], key[16], ad[20], want[200], got[200];
	for (size_t i = 0; i < sizeof pw; i++) r ^= r << 13, r ^= r >> 7, r ^= r << 17, pw[i] = (uint8_t)r;
	memcpy(salt, pw + 3, sizeof salt), memcpy(key, pw + 7, sizeof key), memcpy(ad, pw + 11, sizeof ad);
	for (size_t s = 0; s < AK_LEN(shapes); s++)
		for (uint32_t alg = 0; alg < 3; alg++) {
			uint32_t n = s % 3 == 0 ? 32 : s % 3 == 1 ? 4 + (uint32_t)s * 3 : 100 + (uint32_t)s * 11;
			crypto_argon2_config cfg = {.algorithm = alg, .nb_blocks = shapes[s].m, .nb_passes = shapes[s].t, .nb_lanes = shapes[s].p};
			crypto_argon2_inputs in = {.pass = pw, .salt = salt, .pass_size = (uint32_t)(s * 4), .salt_size = 8 + (uint32_t)s};
			crypto_argon2_extras ex = {.key = key, .ad = ad, .key_size = s % 2 ? 16 : 0, .ad_size = s % 2 ? 0 : 20};
			void *work = malloc((size_t)shapes[s].m * 1024);
			crypto_argon2(want, n, work, cfg, in, ex);
			for (int mode = 0; mode < 6; mode++) {
				ak_cryptohw_off = mode == 4;
				ak_cryptohw_sse2 = mode == 5;
				ak_argon2_threads = mode & 1 ? 1 : 0;
				memset(got, 0, sizeof got);
				ak_argon2(got, n, work, cfg, in, ex);
				if (memcmp(got, want, n) != 0) T_FAIL("m %u, t %u, p %u, argon2 %u, mode %d: other bytes", shapes[s].m, shapes[s].t, shapes[s].p, alg, mode);
			}
			// The work area is wiped after.
			bool zero = true;
			for (size_t k = 0; k < (size_t)shapes[s].m * 1024; k++) zero &= ((uint8_t *)work)[k] == 0;
			CHECK(zero);
			free(work);
		}
	ak_cryptohw_off = ak_cryptohw_sse2 = false;
	ak_argon2_threads = 0;
}

static int verify(const char *pw, size_t pn, const char *hash) {
	ak_str *p = ak_str_new(pw, pn), *h = ak_str_c(hash);
	int r = (int)fn("crypto_password.verifyHash")((ak_val[]){{.p = p}, {.p = h}}, 2).i;
	ak_release(p), ak_release(h);
	return r;
}

static const char *failure(void) {
	static char why[200];
	ak_str *s = fn("crypto_password.passwordFailure")(NULL, 0).p;
	snprintf(why, sizeof why, "%s", s->data);
	ak_release(s);
	return why;
}

TEST(argon2_phc) {
	// PHC strings Go's x/crypto/argon2 made: they verify, others don't.
	const char *go[][2] = {
		{"password", "$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHRzb21lc2FsdA$yrdGtGIZk/3JHsUHh5gLQUqQppLwvGjf4Z+cJbPLqew"},
		{"hunter2", "$argon2id$v=19$m=512,t=1,p=2$c29tZXNhbHRzb21lc2FsdA$6Kv9l5ppMVLuJW77SBz0Nw"},
		{"password", "$argon2i$v=19$m=256,t=3,p=1$c29tZXNhbHRzb21lc2FsdA$ju7us0XNrHIy622jJrn8w1hFqEZ3/88FzQdz3ZrcA1k"},
	};
	for (size_t i = 0; i < AK_LEN(go); i++) {
		CHECK_EQ_INT(verify(go[i][0], strlen(go[i][0]), go[i][1]), 1);
		CHECK_EQ_INT(verify("Password", 8, go[i][1]), 0);
	}
	// What hashArgon2 makes, verifyHash reads; each salt its own.
	ak_str *pw = ak_str_c("correct horse battery staple");
	ak_str *a = fn("crypto_password.hashArgon2")((ak_val[]){{.p = pw}, {.i = 64}, {.i = 2}, {.i = 2}}, 4).p;
	ak_str *b = fn("crypto_password.hashArgon2")((ak_val[]){{.p = pw}, {.i = 64}, {.i = 2}, {.i = 2}}, 4).p;
	REQUIRE(a && b);
	CHECK(strncmp(a->data, "$argon2id$v=19$m=64,t=2,p=2$", 28) == 0);
	CHECK(strcmp(a->data, b->data) != 0);
	CHECK_EQ_INT(verify(pw->data, pw->len, a->data), 1);
	CHECK_EQ_INT(verify(pw->data, pw->len - 1, a->data), 0);
	// Parameters out of range: none, saying why.
	CHECK(!fn("crypto_password.hashArgon2")((ak_val[]){{.p = pw}, {.i = 8}, {.i = 1}, {.i = 2}}, 4).p);
	CHECK(strstr(failure(), "memory") != NULL);
	CHECK(!fn("crypto_password.hashArgon2")((ak_val[]){{.p = pw}, {.i = 64}, {.i = 0}, {.i = 1}}, 4).p);
	CHECK(!fn("crypto_password.hashArgon2")((ak_val[]){{.p = pw}, {.i = 64}, {.i = 1}, {.i = 0}}, 4).p);
	CHECK(!fn("crypto_password.hashArgon2")((ak_val[]){{.p = pw}, {.i = (int64_t)1 << 23}, {.i = 1}, {.i = 1}}, 4).p);
	// What isn't a PHC string this reads: -1, saying why.
	const char *bad[] = {
		"", "$argon2id$", "$argon2id$v=19$m=256,t=2$c29tZXNhbHQ$aGFzaGhhc2g", "$argon2id$v=16$m=256,t=2,p=1$c29tZXNhbHRzb21l$aGFzaGhhc2g",
		"$argon2id$v=19$m=256,t=2,p=1$c29tZQ$aGFzaGhhc2g",            // a salt under 8 bytes
		"$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHRzb21l$aGFzaGhhc2g=", // padding
		"$argon2id$v=19$m=0256,t=2,p=1$c29tZXNhbHRzb21l$aGFzaGhhc2g", // a leading zero
		"$argon2id$v=19$m=4,t=2,p=1$c29tZXNhbHRzb21l$aGFzaGhhc2g",    // memory under 8 KiB a lane
		"$argon2id$v=19$m=256,t=2,p=1$c29tZXNhbHRzb21l$",             // no hash
		"$argon2x$v=19$m=256,t=2,p=1$c29tZXNhbHRzb21l$aGFzaGhhc2g", "$scrypt$ln=16,r=8,p=1$aM15713r3Xsvxbi31lqr1Q$nFNh2CVHVjNldFVKDHDlm4CbdRSCdEBsjjJxD+iCs5E",
	};
	for (size_t i = 0; i < AK_LEN(bad); i++) {
		if (verify("password", 8, bad[i]) != -1) T_FAIL("read %s", bad[i]);
		if (!failure()[0]) T_FAIL("%s: no reason", bad[i]);
	}
	// The raw key: RFC 9106's tag length bounds.
	ak_bytes *p = ak_bytes_new("password", 8), *salt = ak_bytes_new("somesaltsomesalt", 16), *s4 = ak_bytes_new("salt", 4);
	ak_bytes *k = fn("crypto_password.argon2")((ak_val[]){{.i = CRYPTO_ARGON2_ID}, {.p = p}, {.p = salt}, {.i = 32}, {.i = 256}, {.i = 2}, {.i = 1}}, 7).p;
	REQUIRE(k && k->len == 32);
	char h[65];
	hex(k->data, 32, h);
	CHECK_EQ_STR(h, "cab746b4621993fdc91ec50787980b414a90a692f0bc68dfe19f9c25b3cba9ec");  // the first PHC string's hash
	CHECK(!fn("crypto_password.argon2")((ak_val[]){{.i = CRYPTO_ARGON2_ID}, {.p = p}, {.p = s4}, {.i = 32}, {.i = 256}, {.i = 2}, {.i = 1}}, 7).p);
	CHECK(!fn("crypto_password.argon2")((ak_val[]){{.i = 7}, {.p = p}, {.p = salt}, {.i = 32}, {.i = 256}, {.i = 2}, {.i = 1}}, 7).p);
	ak_release(k), ak_release(p), ak_release(salt), ak_release(s4);
	ak_release(pw), ak_release(a), ak_release(b);
}

TEST(bcrypt) {
	// crypt_blowfish's own vectors (its wrapper.c), but $2x$ (the variant
	// its 8-bit bug made, which isn't read), and one Go made.
	static const struct { const char *hash, *pw; } cases[] = {
		{"$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW", "U*U"},
		{"$2a$05$CCCCCCCCCCCCCCCCCCCCC.VGOzA784oUp/Z0DY336zx7pLYAy0lwK", "U*U*"},
		{"$2a$05$XXXXXXXXXXXXXXXXXXXXXOAcXxm9kjPGEMsLznoKqmqw7tc8WCx4a", "U*U*U"},
		{"$2a$05$abcdefghijklmnopqrstuu5s2v8.iXieOjg/.AySBTTZIIVFJeBui",
		 "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789chars after 72 are ignored"},
		{"$2y$05$/OK.fbVrR/bpIqNJ5ianF.CE5elHaaO4EbggVDjb8P19RukzXSM3e", "\xff\xff\xa3"},
		{"$2a$05$/OK.fbVrR/bpIqNJ5ianF.nqd1wy.pTMdcvrRWxyiGL2eMz.2a85.", "\xff\xff\xa3"},
		{"$2b$05$/OK.fbVrR/bpIqNJ5ianF.CE5elHaaO4EbggVDjb8P19RukzXSM3e", "\xff\xff\xa3"},
		{"$2y$05$/OK.fbVrR/bpIqNJ5ianF.Sa7shbm4.OzKpvFnX1pQLmQW96oUlCq", "\xa3"},
		{"$2b$05$/OK.fbVrR/bpIqNJ5ianF.Sa7shbm4.OzKpvFnX1pQLmQW96oUlCq", "\xa3"},
		{"$2a$05$/OK.fbVrR/bpIqNJ5ianF.6IflQkJytoRVc1yuaNtHfiuq.FRlSIS", "\xa3"
										  "ab"},
		{"$2a$05$CCCCCCCCCCCCCCCCCCCCC.7uG0VCzI2bS7j6ymqJi9CdcdxiRTWNy", ""},
		{"$2a$04$VejEAP.xI73txC6VB4Y3rOZ61EZHlpUFMV2Vx7JfXfCPiL45lIGVm", "correct horse"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		if (verify(cases[i].pw, strlen(cases[i].pw), cases[i].hash) != 1) T_FAIL("%s didn't verify", cases[i].hash);
		if (verify("U*U*U*", 6, cases[i].hash) != 0) T_FAIL("%s verified another password", cases[i].hash);
	}
	// $2x$, and what isn't bcrypt's: -1.
	const char *bad[] = {"$2x$05$/OK.fbVrR/bpIqNJ5ianF.CE5elHaaO4EbggVDjb8P19RukzXSM3e", "$2a$03$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW",
			     "$2a$32$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW", "$2c$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW",
			     "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOe"};
	for (size_t i = 0; i < AK_LEN(bad); i++)
		if (verify("U*U", 3, bad[i]) != -1) T_FAIL("read %s", bad[i]);
	// A NUL in the password never matches the hash of what's before it.
	CHECK_EQ_INT(verify("U*U\0x", 5, cases[0].hash), 0);
	// What hashBcrypt makes ($2b$, the cost asked for) verifies.
	ak_str *pw = ak_str_c("correct horse battery staple");
	ak_str *h = fn("crypto_password.hashBcrypt")((ak_val[]){{.p = pw}, {.i = 4}}, 2).p;
	REQUIRE(h && h->len == 60);
	CHECK(strncmp(h->data, "$2b$04$", 7) == 0);
	CHECK_EQ_INT(verify(pw->data, pw->len, h->data), 1);
	CHECK_EQ_INT(verify(pw->data, pw->len - 1, h->data), 0);
	// Over 72 bytes, a NUL, a cost out of range: none, saying why.
	char long_[80];
	memset(long_, 'a', sizeof long_);
	ak_str *l73 = ak_str_new(long_, 73), *l72 = ak_str_new(long_, 72), *nul = ak_str_new("a\0b", 3);
	CHECK(!fn("crypto_password.hashBcrypt")((ak_val[]){{.p = l73}, {.i = 4}}, 2).p);
	CHECK(strstr(failure(), "72") != NULL);
	CHECK(!fn("crypto_password.hashBcrypt")((ak_val[]){{.p = nul}, {.i = 4}}, 2).p);
	CHECK(!fn("crypto_password.hashBcrypt")((ak_val[]){{.p = pw}, {.i = 3}}, 2).p);
	CHECK(!fn("crypto_password.hashBcrypt")((ak_val[]){{.p = pw}, {.i = 32}}, 2).p);
	// A stored hash of 72 bytes verifies a longer password as every bcrypt
	// did: by its first 72.
	ak_str *h72 = fn("crypto_password.hashBcrypt")((ak_val[]){{.p = l72}, {.i = 4}}, 2).p;
	REQUIRE(h72);
	CHECK_EQ_INT(verify(long_, 80, h72->data), 1);
	ak_release(pw), ak_release(h), ak_release(l73), ak_release(l72), ak_release(nul), ak_release(h72);
}

T_MAIN()
