// std/crypto's authenticated encryption (core/src/std/aead.c): AES-GCM
// (the GCM specification's test cases, McGrew and Viega's, as NIST
// publishes them), ChaCha20-Poly1305 (RFC 8439, 2.8.2), XChaCha20-Poly1305
// (draft-irtf-cfrg-xchacha-03, A.3.1), and Wycheproof's cases of each
// (testdata/wycheproof): what's valid opens to its message and seals to
// its ciphertext, what isn't doesn't open.
#include "t.h"
#include "std/std.h"

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

static ak_bytes *hexval(const char *h) {
	size_t n = strcmp(h, "-") == 0 ? 0 : strlen(h) / 2;
	ak_bytes *b = ak_bytes_new(NULL, n);
	for (size_t i = 0; i < n; i++) {
		unsigned v;
		sscanf(h + 2 * i, "%2x", &v);
		b->data[i] = (uint8_t)v;
	}
	return b;
}

static bool same(const ak_bytes *a, const ak_bytes *b) { return a && b && a->len == b->len && memcmp(a->data, b->data, a->len) == 0; }

enum { XCHACHA = 1, CHACHA, AES256, AES128 };

static ak_bytes *seal(int c, ak_bytes *k, ak_bytes *n, ak_bytes *p, ak_bytes *ad) {
	return fn("crypto_aead.sealBytes")((ak_val[]){{.i = c}, {.p = k}, {.p = n}, {.p = p}, {.p = ad}}, 5).p;
}

static ak_bytes *open_(int c, ak_bytes *k, ak_bytes *n, ak_bytes *ct, ak_bytes *ad) {
	return fn("crypto_aead.openBytes")((ak_val[]){{.i = c}, {.p = k}, {.p = n}, {.p = ct}, {.p = ad}}, 5).p;
}

// known checks one case both ways, and that changing any bit of what was
// sealed, or the additional data, keeps it from opening.
static void known(int c, const char *key, const char *nonce, const char *ad, const char *pt, const char *want) {
	ak_bytes *k = hexval(key), *n = hexval(nonce), *a = hexval(ad), *p = hexval(pt), *w = hexval(want);
	ak_bytes *got = seal(c, k, n, p, a);
	if (!same(got, w)) T_FAIL("%d %s: sealed to other bytes", c, key);
	ak_bytes *back = open_(c, k, n, w, a);
	if (!same(back, p)) T_FAIL("%d %s: didn't open", c, key);
	for (uint32_t bit = 0; bit < 8 * w->len; bit += 7) {
		w->data[bit / 8] ^= (uint8_t)(1 << bit % 8);
		ak_bytes *bad = open_(c, k, n, w, a);
		if (bad) {
			T_FAIL("%d %s: opened with bit %u changed", c, key, bit);
			ak_release(bad);
		}
		w->data[bit / 8] ^= (uint8_t)(1 << bit % 8);
	}
	ak_bytes *other = hexval("00");
	ak_bytes *bad = open_(c, k, n, w, other);
	if (bad) {
		T_FAIL("%d %s: opened with other additional data", c, key);
		ak_release(bad);
	}
	ak_bytes *all[] = {k, n, a, p, w, got, back, other};
	for (size_t i = 0; i < AK_LEN(all); i++)
		if (all[i]) ak_release(all[i]);
}

TEST(gcm_spec) {
	const char *P = "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255";
	const char *P60 = "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39";
	const char *AD = "feedfacedeadbeeffeedfacedeadbeefabaddad2";
	// Test cases 1, 2, 3, 4 (AES-128), 13, 14, 15, 16 (AES-256).
	known(AES128, "00000000000000000000000000000000", "000000000000000000000000", "-", "-", "58e2fccefa7e3061367f1d57a4e7455a");
	known(AES128, "00000000000000000000000000000000", "000000000000000000000000", "-", "00000000000000000000000000000000",
		"0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf");
	known(AES128, "feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "-", P,
		"42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985"
		"4d5c2af327cd64a62cf35abd2ba6fab4");
	known(AES128, "feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", AD, P60,
		"42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091"
		"5bc94fbc3221a5db94fae95ae7121a47");
	known(AES256, "0000000000000000000000000000000000000000000000000000000000000000", "000000000000000000000000", "-", "-",
		"530f8afbc74536b9a963b4f1c4cb738b");
	known(AES256, "0000000000000000000000000000000000000000000000000000000000000000", "000000000000000000000000", "-",
		"00000000000000000000000000000000", "cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919");
	known(AES256, "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "-", P,
		"522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad"
		"b094dac5d93471bdec1a502270e3cc6c");
	known(AES256, "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", AD, P60,
		"522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662"
		"76fc6ece0f4e1768cddf8853bb2d551b");
}

TEST(chacha_rfc8439) {
	const char *key = "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", *ad = "50515253c0c1c2c3c4c5c6c7";
	const char *msg = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
	char pt[2 * 114 + 1];
	for (size_t i = 0; i < strlen(msg); i++) snprintf(pt + 2 * i, 3, "%02x", (unsigned char)msg[i]);
	known(CHACHA, key, "070000004041424344454647", ad, pt,
		"d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
		"92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116"
		"1ae10b594f09e26a7e902ecbd0600691");
	known(XCHACHA, key, "404142434445464748494a4b4c4d4e4f5051525354555657", ad, pt,
		"bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb731c7f1b0b4aa6440bf3a82f4eda7e39ae64c6708c54c216cb96b72e1213b452"
		"2f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b2383565d3fff921f9664c97637da9768812f615c68b13b52e"
		"c0875924c1c7987947deafd8780acf49");
}

// wycheproof runs a file of testdata/wycheproof (convert.go's lines).
static void wycheproof(const char *file, int c, int c128) {
	char path[256];
	snprintf(path, sizeof path, "testdata/wycheproof/%s", file);
	FILE *f = fopen(path, "r");
	if (!f) {
		T_FAIL("no %s", path);
		return;
	}
	static char line[1 << 16];
	int cases = 0, valid = 0;
	while (fgets(line, sizeof line, f)) {
		char result[4], key[200], iv[200], ad[1 << 12], msg[1 << 12], ct[1 << 12];
		if (sscanf(line, "%3s %199s %199s %4095s %4095s %4095s", result, key, iv, ad, msg, ct) != 6) continue;
		int cipher = strlen(key) == 32 ? c128 : c;
		ak_bytes *k = hexval(key), *n = hexval(iv), *a = hexval(ad), *m = hexval(msg), *w = hexval(ct);
		ak_bytes *back = open_(cipher, k, n, w, a);
		if (result[0] == 'v') {
			ak_bytes *got = seal(cipher, k, n, m, a);
			if (!same(back, m) || !same(got, w)) T_FAIL("%s: a valid case (key %s) didn't open or seal as it should", file, key);
			if (got) ak_release(got);
			valid++;
		} else if (result[0] == 'i' && back) {
			T_FAIL("%s: an invalid case (key %s, ct %s) opened", file, key, ct);
		}
		ak_bytes *all[] = {k, n, a, m, w, back};
		for (size_t i = 0; i < AK_LEN(all); i++)
			if (all[i]) ak_release(all[i]);
		cases++;
	}
	fclose(f);
	CHECK(cases > 100 && valid > 10);
}

TEST(wycheproof_aes_gcm) { wycheproof("aes_gcm_test.txt", AES256, AES128); }
TEST(wycheproof_chacha20_poly1305) { wycheproof("chacha20_poly1305_test.txt", CHACHA, CHACHA); }
TEST(wycheproof_xchacha20_poly1305) { wycheproof("xchacha20_poly1305_test.txt", XCHACHA, XCHACHA); }

TEST(sizes) {
	// A key or a nonce of the wrong size, or sealed bytes shorter than a
	// tag: none, for every cipher.
	ak_bytes *k32 = ak_bytes_new(NULL, 32), *k16 = ak_bytes_new(NULL, 16), *n12 = ak_bytes_new(NULL, 12), *n24 = ak_bytes_new(NULL, 24),
		 *short_ = ak_bytes_new(NULL, 15), *e = ak_bytes_new(NULL, 0);
	memset(k32->data, 1, 32), memset(k16->data, 1, 16), memset(n12->data, 2, 12), memset(n24->data, 2, 24), memset(short_->data, 0, 15);
	CHECK(!seal(XCHACHA, k32, n12, e, e));
	CHECK(!seal(CHACHA, k32, n24, e, e));
	CHECK(!seal(AES256, k16, n12, e, e));
	CHECK(!seal(AES128, k32, n12, e, e));
	CHECK(!seal(9, k32, n12, e, e));
	CHECK(!open_(AES256, k32, n12, short_, e));
	CHECK(!open_(XCHACHA, k32, n24, short_, e));
	// Empty plaintext: the tag alone, which opens to nothing.
	int ciphers[] = {XCHACHA, CHACHA, AES256, AES128};
	for (size_t i = 0; i < AK_LEN(ciphers); i++) {
		int c = ciphers[i];
		ak_bytes *k = c == AES128 ? k16 : k32, *n = c == XCHACHA ? n24 : n12;
		ak_bytes *s = seal(c, k, n, e, e);
		REQUIRE(s && s->len == 16);
		ak_bytes *o = open_(c, k, n, s, e);
		CHECK(o && o->len == 0);
		ak_release(s);
		if (o) ak_release(o);
	}
	ak_bytes *all[] = {k32, k16, n12, n24, short_, e};
	for (size_t i = 0; i < AK_LEN(all); i++) ak_release(all[i]);
}

T_MAIN()
