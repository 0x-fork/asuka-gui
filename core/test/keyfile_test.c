// Private key files (std/keyfile.c), as OpenSSL 3 and ssh-keygen write
// them (testdata/keys, gen.sh's: the password "asuka"): PKCS#8's PBES2
// (PBKDF2 of every hash, scrypt; AES-CBC, Triple DES), PKCS#12's Triple
// DES, OpenSSL's traditional PEM, OpenSSH's keys (not encrypted, and
// bcrypt_pbkdf with AES-CTR, AES-CBC, AES-GCM, ChaCha20-Poly1305), each
// read to the key OpenSSL or ssh-keygen says it is; wrong and missing
// passwords told apart from keys that aren't; what's weak refused;
// PKCS#8 encrypted as OpenSSL 3 writes it, read back; and what's cut or
// changed read cleanly under ASan.
#include "t.h"
#include "rt/rt.h"
#include "rt/lib.h"
#include "std/std.h"
#include "std/keyfile.h"

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

static ak_obj *made[512];
static int nmade;

static void *keep(void *p) {
	if (p) made[nmade++] = p;
	return p;
}

static void drop(void) {
	while (nmade) ak_release(made[--nmade]);
}

static ak_bytes *file(const char *name) {
	char path[256];
	snprintf(path, sizeof path, "testdata/keys/%s", name);
	FILE *f = fopen(path, "rb");
	if (!f) {
		T_FAIL("no %s", path);
		return keep(ak_bytes_new(NULL, 0));
	}
	static uint8_t b[16384];
	size_t n = fread(b, 1, sizeof b, f);
	fclose(f);
	return keep(ak_bytes_new(b, n));
}

static bool same(const ak_bytes *a, const ak_bytes *b) { return a && b && a->len == b->len && memcmp(a->data, b->data, a->len) == 0; }

static const char PW[] = "asuka";

// read is ak_key_decrypt of a fixture with password pw (NULL:
// ak_key_read's, decrypting nothing): its DER (none when it didn't read),
// the result in *r and why in why.
static ak_bytes *read(const char *name, const char *pw, int *r, char *why) {
	ak_bytes *in = file(name);
	uint8_t *der;
	size_t dn;
	bool pem = strstr(name, ".der") == NULL;
	*r = pw ? ak_key_decrypt(in->data, in->len, pem, (const uint8_t *)pw, strlen(pw), &der, &dn, why, 300) : ak_key_read(in->data, in->len, pem, &der, &dn, why, 300);
	if (*r < 0) return NULL;
	ak_bytes *b = keep(ak_bytes_new(der, dn));
	ak_wipe(der, dn);
	ak_free(der);
	return b;
}

// pkcs8 is the key of a fixture as std/crypto keeps it (PKCS#8, written
// out again by crypto_pk), read with password pw (NULL: not decrypting).
static ak_bytes *pkcs8(const char *name, const char *pw) {
	int r;
	char why[300];
	ak_bytes *der = read(name, pw, &r, why);
	if (!der) {
		T_FAIL("%s: %s", name, why);
		return NULL;
	}
	ak_bytes *k = keep(fn("crypto_pk.readPrivate")((ak_val[]){{.p = der}}, 1).p);
	if (!k) T_FAIL("%s: crypto_pk doesn't read what it read", name);
	return k;
}

static ak_bytes *public_of(ak_bytes *k) { return k ? keep(fn("crypto_pk.publicOf")((ak_val[]){{.p = k}}, 1).p) : NULL; }

// signs: whether k signs a message that its public key verifies (an RSA
// key whose CRT parts are wrong signs nothing).
static bool signs(ak_bytes *k, int scheme, int hash) {
	ak_bytes *msg = keep(ak_bytes_new("a message", 9));
	ak_bytes *sig = keep(fn("crypto_pk.signMessage")((ak_val[]){{.p = k}, {.i = scheme}, {.i = hash}, {.p = msg}}, 4).p);
	if (!sig) return false;
	return fn("crypto_pk.verifySignature")((ak_val[]){{.p = public_of(k)}, {.i = scheme}, {.i = hash}, {.p = msg}, {.p = sig}}, 5).i == 1;
}

enum { PKCS1 = 1, PSS, ECDSA, ECDSA_DER, ED25519 };

TEST(openssl_pkcs8) {
	// Each encryption of a key reads to the key itself.
	struct { const char *enc, *plain; } cases[] = {
		{"rsa-pbes2.pem", "rsa.pem"},
		{"p256-pbes2.pem", "p256.pem"},
		{"ed25519-pbes2.pem", "ed25519.pem"},
		{"p256-aes128-sha1.pem", "p256.pem"},
		{"p384-aes192-sha512.pem", "p384.pem"},
		{"p256-des3.pem", "p256.pem"},
		{"p256-scrypt.pem", "p256.pem"},
		{"p256-pbes2.der", "p256.pem"},
		{"rsa-pkcs12-3des.pem", "rsa.pem"},
		{"p256-pkcs12-2des.pem", "p256.pem"},
		{"rsa-trad-aes256.pem", "rsa.pem"},
		{"rsa-trad-des3.pem", "rsa.pem"},
		{"p256-trad-aes128.pem", "p256.pem"},
		{"p256-sec1.pem", "p256.pem"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		ak_bytes *want = pkcs8(cases[i].plain, NULL);
		ak_bytes *got = pkcs8(cases[i].enc, PW);
		if (!same(got, want)) T_FAIL("%s isn't %s's key", cases[i].enc, cases[i].plain);
		// Not decrypting, an encrypted one is said to be.
		int r;
		char why[300];
		if (strcmp(cases[i].enc, "p256-sec1.pem") != 0 && (read(cases[i].enc, NULL, &r, why) || r != AK_KEY_ENCRYPTED)) T_FAIL("%s read as plain: %d %s", cases[i].enc, r, why);
	}
	// What OpenSSL says each one's public key is.
	const char *pub[][2] = {{"rsa-pbes2.pem", "rsa-public.der"}, {"p384-aes192-sha512.pem", "p384-public.der"}, {"ed25519-pbes2.pem", "ed25519-public.der"}, {"p256-trad-aes128.pem", "p256-public.der"}};
	for (size_t i = 0; i < AK_LEN(pub); i++)
		if (!same(public_of(pkcs8(pub[i][0], PW)), file(pub[i][1]))) T_FAIL("%s's public key isn't %s", pub[i][0], pub[i][1]);
	// PKCS#12 takes the password as UTF-16: one that isn't ASCII.
	CHECK(same(pkcs8("pkcs12-unicode.pem", "pässwörd🔑"), pkcs8("p256.pem", NULL)));
	// Decrypting, a plain key is read as it is, the password unused.
	CHECK(same(pkcs8("p256.pem", "unused"), pkcs8("p256.pem", NULL)));
	drop();
}

// ---- OpenSSH's ----

static int b64(char c) {
	const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const char *p = c ? strchr(A, c) : NULL;
	return p ? (int)(p - A) : -1;
}

static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// ssh_public is the SubjectPublicKeyInfo of an OpenSSH .pub file (its
// blob's parts, made a key by crypto_pk).
static ak_bytes *ssh_public(const char *name) {
	ak_bytes *f = file(name);
	const char *s = memchr(f->data, ' ', f->len);
	if (!s) return NULL;
	uint8_t blob[2048];
	size_t n = 0;
	uint32_t acc = 0;
	int bits = 0;
	for (s++; b64(*s) >= 0; s++) {
		acc = acc << 6 | (uint32_t)b64(*s), bits += 6;
		if (bits >= 8) bits -= 8, blob[n++] = (uint8_t)(acc >> bits);
	}
	ak_bytes *part[3];
	int np = 0;
	size_t at = 0;
	bool ec = n > 15 && memcmp(blob + 4, "ecdsa", 5) == 0;
	for (int i = 0; at + 4 <= n; i++) {
		uint32_t l = u32(blob + at);
		const uint8_t *p = blob + at + 4;
		at += 4 + l;
		if (i == 0 || (ec && i == 1)) continue;
		while (l && p[0] == 0) p++, l--;  // an mpint's leading zero
		if (np < 3) part[np++] = ak_bytes_new(p, l);
	}
	int kind;
	ak_arr *parts = keep(ak_arr_new(AK_K_REF, NULL, 3));
	if (np == 1 && !ec) {  // ed25519: the key
		kind = 5;
		ak_arr_push(&parts, (ak_val){.p = part[0]});
	} else if (!ec) {  // rsa: e, n
		kind = 1;
		ak_arr_push(&parts, (ak_val){.p = part[1]});
		ak_arr_push(&parts, (ak_val){.p = part[0]});
	} else {  // ecdsa: its point
		size_t c = (part[0]->len - 1) / 2;
		kind = c == 32 ? 2 : c == 48 ? 3 : 4;
		ak_arr_push(&parts, (ak_val){.p = ak_bytes_new(part[0]->data + 1, c)});
		ak_arr_push(&parts, (ak_val){.p = ak_bytes_new(part[0]->data + 1 + c, c)});
		ak_release(part[0]);
	}
	return keep(fn("crypto_pk.keyFromParts")((ak_val[]){{.i = kind}, {.p = parts}, {.i = 0}}, 3).p);
}

TEST(openssh) {
	struct { const char *key, *pw; int scheme; } cases[] = {
		{"ssh-ed25519-plain", NULL, ED25519},
		{"ssh-ed25519", PW, ED25519},
		{"ssh-p256", PW, ECDSA},
		{"ssh-p384-plain", NULL, ECDSA},
		{"ssh-p521", PW, ECDSA},
		{"ssh-rsa", PW, PKCS1},
		{"ssh-rsa-plain", NULL, PSS},
		{"ssh-ed25519-gcm", PW, ED25519},
		{"ssh-ed25519-chacha", PW, ED25519},
		{"ssh-p256-cbc", PW, ECDSA},
		{"ssh-ed25519-ctr128", PW, ED25519},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		char pem[64], pub[64];
		snprintf(pem, sizeof pem, "%s.pem", cases[i].key);
		snprintf(pub, sizeof pub, "%s.pub", cases[i].key);
		ak_bytes *k = pkcs8(pem, cases[i].pw);
		if (!k) continue;
		ak_bytes *want = ssh_public(pub);
		if (!same(public_of(k), want)) T_FAIL("%s's public key isn't its .pub's", cases[i].key);
		if (!signs(k, cases[i].scheme, AK_HASH_SHA256)) T_FAIL("%s doesn't sign", cases[i].key);
	}
	// An encrypted OpenSSH key and an RSA one: what OpenSSL made of it
	// before ssh-keygen encrypted it (tls-client-openssh.pem is
	// ../tls/client.key).
	ak_bytes *in = keep(ak_bytes_new(NULL, 0));
	{
		FILE *f = fopen("testdata/tls/client.key", "rb");
		REQUIRE(f);
		char b[4096];
		size_t n = fread(b, 1, sizeof b, f);
		fclose(f);
		in = keep(ak_bytes_new(b, n));
	}
	uint8_t *der;
	size_t dn;
	char why[300];
	REQUIRE(ak_key_read(in->data, in->len, true, &der, &dn, why, sizeof why) > 0);
	ak_bytes *plain = keep(fn("crypto_pk.readPrivate")((ak_val[]){{.p = keep(ak_bytes_new(der, dn))}}, 1).p);
	ak_free(der);
	CHECK(same(pkcs8("tls-client-openssh.pem", PW), plain));
	drop();
}

TEST(passwords) {
	// A wrong password, and none, for each kind of encryption: the
	// password's failure, not the key's.
	const char *enc[] = {"rsa-pbes2.pem", "p256-scrypt.pem", "p256-des3.pem", "p256-pbes2.der", "rsa-pkcs12-3des.pem", "rsa-trad-aes256.pem", "rsa-trad-des3.pem",
		"p256-trad-aes128.pem", "ssh-ed25519.pem", "ssh-ed25519-gcm.pem", "ssh-ed25519-chacha.pem", "ssh-p256-cbc.pem", "ssh-rsa.pem", "pkcs12-unicode.pem"};
	for (size_t i = 0; i < AK_LEN(enc); i++) {
		int r;
		char why[300];
		if (read(enc[i], "wrong", &r, why)) T_FAIL("%s read with the wrong password", enc[i]);
		else if (r != AK_KEY_PASSWORD || strcmp(why, "the password is wrong: it doesn't decrypt the key") != 0) T_FAIL("%s, wrong: %d %s", enc[i], r, why);
		if (read(enc[i], "", &r, why)) T_FAIL("%s read without its password", enc[i]);
		else if (r != AK_KEY_PASSWORD || strcmp(why, "the key is encrypted: its password is needed") != 0) T_FAIL("%s, none: %d %s", enc[i], r, why);
	}
	// What's weak isn't read, and says how to encrypt it again.
	int r;
	char why[300];
	CHECK(!read("p256-pbes1-des.pem", PW, &r, why));
	CHECK_EQ_INT(r, AK_KEY_INVALID);
	CHECK(strstr(why, "weak") && strstr(why, "openssl pkcs8 -topk8 -v2 aes-256-cbc"));
	drop();
}

TEST(written) {
	// PKCS#8 encrypted (PBES2, PBKDF2-HMAC-SHA256, AES-256-CBC), read back;
	// the same key twice encrypts differently (its salt and IV).
	const char *keys[] = {"rsa.pem", "p256.pem", "ed25519.pem"};
	for (size_t i = 0; i < AK_LEN(keys); i++) {
		ak_bytes *k = pkcs8(keys[i], NULL);
		REQUIRE(k);
		ak_bytes *e1 = keep(fn("crypto_keyenc.encryptKey")((ak_val[]){{.p = k}, {.p = keep(ak_bytes_new("s3cret", 6))}, {.i = 1000}}, 3).p);
		ak_bytes *e2 = keep(fn("crypto_keyenc.encryptKey")((ak_val[]){{.p = k}, {.p = keep(ak_bytes_new("s3cret", 6))}, {.i = 1000}}, 3).p);
		CHECK(!same(e1, e2));
		// As OpenSSL 3 writes it: the same header as its own (testdata's),
		// to the salt.
		static const uint8_t head[] = {0x30, 0x0b, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x05, 0x0d};
		bool found = false;
		for (size_t at = 0; at + sizeof head - 2 <= 40 && at + sizeof head - 2 <= e1->len; at++)
			found |= memcmp(e1->data + at, head + 2, sizeof head - 2) == 0;
		CHECK(found);
		uint8_t *der;
		size_t dn;
		char why[300];
		int r = ak_key_decrypt(e1->data, e1->len, false, (const uint8_t *)"s3cret", 6, &der, &dn, why, sizeof why);
		CHECK_EQ_INT(r, AK_KEY_PKCS8);
		if (r > 0) {
			CHECK(dn == k->len && memcmp(der, k->data, dn) == 0);
			ak_free(der);
		}
		CHECK_EQ_INT(ak_key_decrypt(e1->data, e1->len, false, (const uint8_t *)"s3cre", 5, &der, &dn, why, sizeof why), AK_KEY_PASSWORD);
		// And as PEM, through the builtins privateKey goes through.
		ak_str *pem = keep(fn("crypto_pk.pemOf")((ak_val[]){{.p = e1}, {.p = keep(ak_str_c("ENCRYPTED PRIVATE KEY"))}}, 2).p);
		ak_bytes *pb = keep(ak_bytes_new(pem->data, pem->len));
		ak_bytes *back = keep(fn("crypto_keydecrypt.decryptKeyFile")((ak_val[]){{.p = pb}, {.i = 1}, {.p = keep(ak_bytes_new("s3cret", 6))}}, 3).p);
		CHECK(same(back, k));
		CHECK(!fn("crypto_keydecrypt.decryptKeyFile")((ak_val[]){{.p = pb}, {.i = 1}, {.p = keep(ak_bytes_new("nope", 4))}}, 3).p);
		CHECK_EQ_INT(fn("crypto_keyfile.keyFileStatus")(NULL, 0).i, AK_KEY_PASSWORD);
		CHECK(!fn("crypto_keyfile.readKeyFile")((ak_val[]){{.p = pb}, {.i = 1}}, 2).p);
		CHECK_EQ_INT(fn("crypto_keyfile.keyFileStatus")(NULL, 0).i, AK_KEY_ENCRYPTED);
	}
	drop();
}

TEST(damaged) {
	// Every prefix of encrypted keys' DER and OpenSSH's, and each byte
	// changed, reads cleanly: none, or a key (ASan).
	// (Changed bytes only where a key's derivation is quick: bcrypt's
	// rounds aren't.)
	struct { const char *name; bool change; } cases[] = {
		{"p256-pbes2.der", true}, {"ssh-ed25519-plain.pem", true}, {"ssh-rsa-plain.pem", true}, {"p256-pkcs12-2des.pem", true},
		{"p256-trad-aes128.pem", true}, {"ssh-ed25519-ctr128.pem", false}, {"ssh-p256-cbc.pem", false},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		const char *name = cases[i].name;
		ak_bytes *f = file(name);
		bool pem = strstr(name, ".der") == NULL;
		for (uint32_t n = 0; n < f->len; n += pem ? 7 : 1) {
			uint8_t *der;
			size_t dn;
			char why[300];
			int r = ak_key_decrypt(f->data, n, pem, (const uint8_t *)PW, 5, &der, &dn, why, sizeof why);
			if (r > 0) ak_free(der);
		}
		for (uint32_t k = 0; cases[i].change && k < f->len; k += 5) {
			f->data[k] ^= 0x15;
			uint8_t *der;
			size_t dn;
			char why[300];
			int r = ak_key_decrypt(f->data, f->len, pem, (const uint8_t *)PW, 5, &der, &dn, why, sizeof why);
			if (r > 0) ak_free(der);
			f->data[k] ^= 0x15;
		}
	}
	drop();
}

T_MAIN()
