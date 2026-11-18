// std/crypto's password hashing (core/src/std/password.c): Argon2 (RFC
// 9106's vectors, through Monocypher; PHC strings Go's x/crypto/argon2
// made), bcrypt (crypt_blowfish's own vectors, and one Go's
// x/crypto/bcrypt made), and what they refuse.
#include "t.h"
#include "std/std.h"
#include "std/monocypher.h"

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
		crypto_argon2(tag, sizeof tag, work, cfg, in, ex);
		char h[65];
		hex(tag, 32, h);
		CHECK_EQ_STR(h, cases[i].want);
	}
	free(work);
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
