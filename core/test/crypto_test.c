// SHA-512 (FIPS 180-4's vectors, and lengths around its padding),
// SHA-384, MD5 (RFC 1321's), HMAC (RFC 2202's and RFC 4231's), HKDF (RFC
// 5869's), PBKDF2 (RFC 6070's and RFC 7914's) and Ed25519 (RFC 8032's
// vectors), as the core's crypto (core/src/std) has them, and std/crypto's
// builtins of them. Vectors without an RFC's (HKDF and PBKDF2 over
// SHA-384 and SHA-512) are Go's crypto/hkdf's and crypto/pbkdf2's.
#include "t.h"
#include "std/std.h"
#include "std/cryptohw.h"

static void hex(const uint8_t *p, size_t n, char *out) {
	for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

static size_t unhex(const char *s, uint8_t *out) {
	size_t n = strlen(s) / 2;
	for (size_t i = 0; i < n; i++) {
		unsigned v;
		sscanf(s + 2 * i, "%2x", &v);
		out[i] = (uint8_t)v;
	}
	return n;
}

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

TEST(sha512) {
	struct { const char *in; size_t repeat; const char *want; } cases[] = {
		{"abc", 1, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
		{"", 1, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
		{"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1,
		 "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"},
		{"a", 1000000, "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"},
		// The padding's boundaries: the length fits after 111 bytes, not 112.
		{"a", 111, "fa9121c7b32b9e01733d034cfc78cbf67f926c7ed83e82200ef86818196921760b4beff48404df811b953828274461673c68d04e297b0eb7b2b4d60fc6b566a2"},
		{"a", 112, "c01d080efd492776a1c43bd23dd99d0a2e626d481e16782e75d54c2503b5dc32bd05f0f1ba33e568b88fd2d970929b719ecbb152f58f130a407c8830604b70ca"},
		{"a", 127, "828613968b501dc00a97e08c73b118aa8876c26b8aac93df128502ab360f91bab50a51e088769a5c1eff4782ace147dce3642554199876374291f5d921629502"},
		{"a", 128, "b73d1929aa615934e61a871596b3f3b33359f42b8175602e89f7e06e5f658a243667807ed300314b95cacdd579f3e33abdfbe351909519a846d465c59582f321"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		size_t one = strlen(cases[i].in), n = one * cases[i].repeat;
		uint8_t *p = malloc(n + 1), out[64];
		for (size_t k = 0; k < cases[i].repeat; k++) memcpy(p + k * one, cases[i].in, one);
		ak_sha512(p, n, out);
		char h[129];
		hex(out, 64, h);
		CHECK_EQ_STR(h, cases[i].want);
		// In pieces of every size up to 300 bytes: the same.
		ak_sha512_ctx c;
		ak_sha512_init(&c);
		for (size_t off = 0, step = 1; off < n; off += step, step = step % 300 + 1)
			ak_sha512_update(&c, p + off, off + step <= n ? step : n - off);
		ak_sha512_final(&c, out);
		hex(out, 64, h);
		CHECK_EQ_STR(h, cases[i].want);
		free(p);
	}
}

TEST(sha256_in_pieces) {
	uint8_t *p = malloc(1000000), one[32], pieces[32];
	memset(p, 'a', 1000000);
	ak_sha256(p, 1000000, one);
	ak_sha256_ctx c;
	ak_sha256_init(&c);
	for (size_t off = 0, step = 1; off < 1000000; off += step, step = step % 150 + 1)
		ak_sha256_update(&c, p + off, off + step <= 1000000 ? step : 1000000 - off);
	ak_sha256_final(&c, pieces);
	char h[65];
	hex(pieces, 32, h);
	CHECK_EQ_STR(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	CHECK(memcmp(one, pieces, 32) == 0);
	free(p);
}

// RFC 8032, section 7.1: TEST 1, 2, 3 and 1024.
static const struct { const char *seed, *pub, *msg, *sig; } rfc8032[] = {
	{"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
	 "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
	{"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
	 "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
	{"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
	 "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
	// TEST SHA(abc): the message is SHA-512("abc").
	{"833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42", "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
	 "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
	 "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"},
};

// The CPU's SHA-256 and SHA-512 (cryptohw.c), where it has them, hash
// as the software does, whole and in pieces.
TEST(sha2_hardware_as_software) {
	static uint8_t data[3000];
	uint64_t r = 0x9e3779b97f4a7c15ull;
	for (size_t i = 0; i < sizeof data; i++) {
		r ^= r << 13, r ^= r >> 7, r ^= r << 17;
		data[i] = (uint8_t)r;
	}
	for (size_t n = 0; n < sizeof data; n += n < 300 ? 1 : 97) {
		uint8_t a[64], b[64], c[64], d[64];
		ak_sha256(data, n, a);
		ak_sha512(data, n, c);
		ak_cryptohw_off = true;
		ak_sha256(data, n, b);
		ak_sha512(data, n, d);
		ak_cryptohw_off = false;
		if (memcmp(a, b, 32)) T_FAIL("sha256 of %zu bytes", n);
		if (memcmp(c, d, 64)) T_FAIL("sha512 of %zu bytes", n);
		// In pieces: what's buffered, then whole blocks, then the rest.
		ak_sha256_ctx s;
		ak_sha256_init(&s);
		size_t k = n / 3;
		ak_sha256_update(&s, data, k);
		ak_sha256_update(&s, data + k, n - k);
		ak_sha256_final(&s, b);
		if (memcmp(a, b, 32)) T_FAIL("sha256 of %zu bytes in pieces", n);
		ak_sha512_ctx t;
		ak_sha512_init(&t);
		ak_sha512_update(&t, data, k);
		ak_sha512_update(&t, data + k, n - k);
		ak_sha512_final(&t, d);
		if (memcmp(c, d, 64)) T_FAIL("sha512 of %zu bytes in pieces", n);
	}
}

TEST(ed25519_rfc8032) {
	for (size_t i = 0; i < AK_LEN(rfc8032); i++) {
		uint8_t seed[32], pub[32], want_pub[32], msg[64], sig[64], want_sig[64];
		unhex(rfc8032[i].seed, seed);
		unhex(rfc8032[i].pub, want_pub);
		size_t n = unhex(rfc8032[i].msg, msg);
		unhex(rfc8032[i].sig, want_sig);
		ak_ed25519_public(seed, pub);
		CHECK(memcmp(pub, want_pub, 32) == 0);
		ak_ed25519_sign(seed, pub, msg, n, sig);
		char h[129];
		hex(sig, 64, h);
		CHECK_EQ_STR(h, rfc8032[i].sig);
		CHECK(ak_ed25519_verify(pub, msg, n, want_sig));
		// Any bit changed, in the signature, the message or the key, fails.
		for (int bit = 0; bit < 512; bit += 37) {
			memcpy(sig, want_sig, 64);
			sig[bit / 8] ^= (uint8_t)(1 << (bit % 8));
			if (ak_ed25519_verify(pub, msg, n, sig)) T_FAIL("vector %zu verified with bit %d of its signature changed", i, bit);
		}
		if (n) {
			msg[0] ^= 1;
			CHECK(!ak_ed25519_verify(pub, msg, n, want_sig));
			msg[0] ^= 1;
		}
		pub[3] ^= 0x10;
		CHECK(!ak_ed25519_verify(pub, msg, n, want_sig));
	}
}

TEST(ed25519_strict) {
	uint8_t seed[32], pub[32], sig[64];
	unhex(rfc8032[0].seed, seed);
	unhex(rfc8032[0].pub, pub);
	unhex(rfc8032[0].sig, sig);
	// S + L is the same signature to the arithmetic, but not canonical:
	// refused, as Go's crypto/ed25519 refuses it.
	static const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	                              0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
	unsigned carry = 0;
	for (int i = 0; i < 32; i++) {
		unsigned v = sig[32 + i] + L[i] + carry;
		sig[32 + i] = (uint8_t)v;
		carry = v >> 8;
	}
	CHECK(carry == 0);
	CHECK(!ak_ed25519_verify(pub, NULL, 0, sig));
	// A key that's no point of the curve (y = 2 has no x).
	uint8_t bad[32] = {2};
	unhex(rfc8032[0].sig, sig);
	CHECK(!ak_ed25519_verify(bad, NULL, 0, sig));
}

TEST(ed25519_builtins) {
	uint8_t seed[32], msg[2];
	unhex(rfc8032[2].seed, seed);
	unhex(rfc8032[2].msg, msg);
	ak_bytes *key = ak_bytes_new(seed, 32), *m = ak_bytes_new(msg, 2);
	ak_bytes *pub = fn("crypto.ed25519PublicKey")((ak_val[]){{.p = key}}, 1).p;
	ak_bytes *sig = fn("crypto.ed25519Sign")((ak_val[]){{.p = key}, {.p = m}}, 2).p;
	REQUIRE(pub && sig && pub->len == 32 && sig->len == 64);
	char h[129];
	hex(sig->data, 64, h);
	CHECK_EQ_STR(h, rfc8032[2].sig);
	CHECK(fn("crypto.ed25519Verify")((ak_val[]){{.p = pub}, {.p = m}, {.p = sig}}, 3).i);
	// Go's 64-byte private key (the seed, then the public key) signs the same.
	uint8_t full[64];
	memcpy(full, seed, 32);
	memcpy(full + 32, pub->data, 32);
	ak_bytes *key64 = ak_bytes_new(full, 64);
	ak_bytes *sig64 = fn("crypto.ed25519Sign")((ak_val[]){{.p = key64}, {.p = m}}, 2).p;
	CHECK(sig64 && memcmp(sig64->data, sig->data, 64) == 0);
	// A key or a signature of the wrong size doesn't verify.
	CHECK(!fn("crypto.ed25519Verify")((ak_val[]){{.p = m}, {.p = m}, {.p = sig}}, 3).i);
	CHECK(!fn("crypto.ed25519Verify")((ak_val[]){{.p = pub}, {.p = m}, {.p = m}}, 3).i);
	// A new key signs what its public key verifies.
	ak_bytes *fresh = fn("crypto.ed25519NewKey")(NULL, 0).p;
	REQUIRE(fresh && fresh->len == 64);
	ak_bytes *fs = fn("crypto.ed25519Sign")((ak_val[]){{.p = fresh}, {.p = m}}, 2).p;
	ak_bytes *fp = ak_bytes_new(fresh->data + 32, 32);
	CHECK(fn("crypto.ed25519Verify")((ak_val[]){{.p = fp}, {.p = m}, {.p = fs}}, 3).i);
	// A private key of the wrong size throws.
	fn("crypto.ed25519Sign")((ak_val[]){{.p = m}, {.p = m}}, 2);
	CHECK(ak_error_pending());
	if (ak_error_pending()) ak_release(ak_error_take());
	ak_bytes *digest = fn("crypto.sha512")((ak_val[]){{.p = m}}, 1).p;
	CHECK(digest && digest->len == 64);
	ak_bytes *all[] = {key, m, pub, sig, key64, sig64, fresh, fs, fp, digest};
	for (size_t i = 0; i < AK_LEN(all); i++) ak_release(all[i]);
}

// hexval: the bytes of hex as a new ak_bytes.
static ak_bytes *hexval(const char *h) {
	size_t n = strlen(h) / 2;
	ak_bytes *b = ak_bytes_new(NULL, n);
	unhex(h, b->data);
	return b;
}

static ak_bytes *text(const char *s) { return ak_bytes_new(s, strlen(s)); }

// check_hex: b is want's bytes; b released.
#define CHECK_HEX(b, want) \
	do { \
		ak_bytes *b_ = (b); \
		char *h_ = malloc(2 * (b_ ? b_->len : 0) + 1); \
		h_[0] = 0; \
		if (b_) hex(b_->data, b_->len, h_); \
		CHECK_EQ_STR(h_, want); \
		free(h_); \
		if (b_) ak_release(b_); \
	} while (0)

TEST(sha384_md5) {
	uint8_t out[48];
	char h[97];
	ak_sha384("abc", 3, out);
	hex(out, 48, h);
	CHECK_EQ_STR(h, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
	const char *long448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	ak_sha384(long448, strlen(long448), out);
	hex(out, 48, h);
	CHECK_EQ_STR(h, "3391fdddfc8dc7393707a65b1b4709397cf8b1d162af05abfe8f450de5f36bc6b0455a8520bc4e6f5fe95b1fe3c8452b");
	ak_bytes *e = text("");
	CHECK_HEX(fn("crypto.sha384")((ak_val[]){{.p = e}}, 1).p,
		"38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b");
	// RFC 1321's test suite.
	static const struct { const char *in, *want; } md5[] = {
		{"", "d41d8cd98f00b204e9800998ecf8427e"},
		{"a", "0cc175b9c0f1b6a831c399e269772661"},
		{"abc", "900150983cd24fb0d6963f7d28e17f72"},
		{"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
		{"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
		{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f"},
		{"12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a"},
	};
	for (size_t i = 0; i < AK_LEN(md5); i++) {
		ak_bytes *m = text(md5[i].in);
		CHECK_HEX(fn("crypto.md5")((ak_val[]){{.p = m}}, 1).p, md5[i].want);
		ak_release(m);
	}
	// Streamed SHA-1, in pieces of every size: the same as at once.
	uint8_t *p = malloc(1000), one[20], pieces[20];
	for (int i = 0; i < 1000; i++) p[i] = (uint8_t)(i * 7);
	ak_sha1(p, 1000, one);
	ak_sha1_ctx c;
	ak_sha1_init(&c);
	for (size_t off = 0, step = 1; off < 1000; off += step, step = step % 70 + 1) ak_sha1_update(&c, p + off, off + step <= 1000 ? step : 1000 - off);
	ak_sha1_final(&c, pieces);
	CHECK(memcmp(one, pieces, 20) == 0);
	free(p);
	ak_release(e);
}

TEST(hmac) {
	// RFC 4231's test cases 1, 2 and 6 (and RFC 2202's 1, SHA-1's).
	ak_bytes *k1 = hexval("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"), *m1 = text("Hi There");
	ak_bytes *k2 = text("Jefe"), *m2 = text("what do ya want for nothing?");
	ak_bytes *k6 = ak_bytes_new(NULL, 131), *m6 = text("Test Using Larger Than Block-Size Key - Hash Key First");
	memset(k6->data, 0xaa, 131);
	static const struct { int hash, which; const char *want; } cases[] = {
		{AK_HASH_SHA1, 1, "b617318655057264e28bc0b6fb378c8ef146be00"},
		{AK_HASH_SHA256, 1, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"},
		{AK_HASH_SHA384, 1, "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59cfaea9ea9076ede7f4af152e8b2fa9cb6"},
		{AK_HASH_SHA512, 1, "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854"},
		{AK_HASH_SHA256, 2, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"},
		{AK_HASH_SHA512, 2, "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737"},
		{AK_HASH_SHA256, 6, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"},
		{AK_HASH_SHA384, 6, "4ece084485813e9088d2c63a041bc5b44f9ef1012a2b588f3cd11f05033ac4c60c2ef6ab4030fe8296248df163f44952"},
		{AK_HASH_SHA512, 6, "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		ak_bytes *k = cases[i].which == 1 ? k1 : cases[i].which == 2 ? k2 : k6, *m = cases[i].which == 1 ? m1 : cases[i].which == 2 ? m2 : m6;
		CHECK_HEX(fn("crypto.hmacWith")((ak_val[]){{.i = cases[i].hash}, {.p = k}, {.p = m}}, 3).p, cases[i].want);
		if (cases[i].hash == AK_HASH_SHA256) CHECK_HEX(fn("crypto.hmacSHA256")((ak_val[]){{.p = k}, {.p = m}}, 2).p, cases[i].want);
	}
	// No such hash throws.
	CHECK(fn("crypto.hmacWith")((ak_val[]){{.i = 9}, {.p = k1}, {.p = m1}}, 3).p == NULL);
	CHECK(ak_error_pending());
	if (ak_error_pending()) ak_release(ak_error_take());
	ak_bytes *all[] = {k1, m1, k2, m2, k6, m6};
	for (size_t i = 0; i < AK_LEN(all); i++) ak_release(all[i]);
}

static ak_bytes *hkdf(int hash, const char *ikm, const char *salt, const char *info, int64_t n) {
	ak_bytes *i = hexval(ikm), *s = hexval(salt), *f = hexval(info);
	ak_bytes *prk = fn("crypto.hkdfExtractWith")((ak_val[]){{.i = hash}, {.p = s}, {.p = i}}, 3).p;
	ak_bytes *out = fn("crypto.hkdfExpandWith")((ak_val[]){{.i = hash}, {.p = prk}, {.p = f}, {.i = n}}, 4).p;
	ak_release(i), ak_release(s), ak_release(f), ak_release(prk);
	return out;
}

TEST(hkdf) {
	const char *ikm = "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b";
	// RFC 5869's A.1, A.2 and A.3.
	CHECK_HEX(hkdf(AK_HASH_SHA256, ikm, "000102030405060708090a0b0c", "f0f1f2f3f4f5f6f7f8f9", 42),
		"3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
	char a[161], b[161], c[161];
	for (int i = 0; i < 80; i++) snprintf(a + 2 * i, 3, "%02x", i), snprintf(b + 2 * i, 3, "%02x", 0x60 + i), snprintf(c + 2 * i, 3, "%02x", 0xb0 + i);
	CHECK_HEX(hkdf(AK_HASH_SHA256, a, b, c, 82),
		"b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87");
	CHECK_HEX(hkdf(AK_HASH_SHA256, ikm, "", "", 42), "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8");
	CHECK_HEX(hkdf(AK_HASH_SHA512, ikm, "000102030405060708090a0b0c", "f0f1f2f3f4f5f6f7f8f9", 42),
		"832390086cda71fb47625bb5ceb168e4c8e26a1a16ed34d9fc7fe92c1481579338da362cb8d9f925d7cb");
	CHECK_HEX(hkdf(AK_HASH_SHA384, ikm, "000102030405060708090a0b0c", "f0f1f2f3f4f5f6f7f8f9", 42),
		"9b5097a86038b805309076a44b3a9f38063e25b516dcbf369f394cfab43685f748b6457763e4f0204fc5");
	// Its most, 255 digests, and past it: a throw.
	ak_bytes *most = hkdf(AK_HASH_SHA256, ikm, "", "", 255 * 32);
	CHECK(most && most->len == 255 * 32);
	if (most) ak_release(most);
	CHECK(hkdf(AK_HASH_SHA256, ikm, "", "", 255 * 32 + 1) == NULL);
	CHECK(ak_error_pending());
	if (ak_error_pending()) ak_release(ak_error_take());
}

TEST(pbkdf2) {
	static const struct { int hash; const char *pw; size_t pn; const char *salt; size_t sn; int64_t c, n; const char *want; } cases[] = {
		// RFC 6070 (HMAC-SHA1).
		{AK_HASH_SHA1, "password", 8, "salt", 4, 1, 20, "0c60c80f961f0e71f3a9b524af6012062fe037a6"},
		{AK_HASH_SHA1, "password", 8, "salt", 4, 2, 20, "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"},
		{AK_HASH_SHA1, "password", 8, "salt", 4, 4096, 20, "4b007901b765489abead49d926f721d065a429c1"},
		{AK_HASH_SHA1, "passwordPASSWORDpassword", 24, "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096, 25, "3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038"},
		{AK_HASH_SHA1, "pass\0word", 9, "sa\0lt", 5, 4096, 16, "56fa6aa75548099dcc37d7f03425e0c3"},
		// RFC 7914, section 11 (HMAC-SHA256).
		{AK_HASH_SHA256, "passwd", 6, "salt", 4, 1, 64,
		 "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"},
		{AK_HASH_SHA256, "Password", 8, "NaCl", 4, 80000, 64,
		 "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d"},
		{AK_HASH_SHA512, "password", 8, "salt", 4, 1000, 64,
		 "afe6c5530785b6cc6b1c6453384731bd5ee432ee549fd42fb6695779ad8a1c5bf59de69c48f774efc4007d5298f9033c0241d5ab69305e7b64eceeb8d834cfec"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		ak_bytes *pw = ak_bytes_new(cases[i].pw, cases[i].pn), *salt = ak_bytes_new(cases[i].salt, cases[i].sn);
		CHECK_HEX(fn("crypto.pbkdf2With")((ak_val[]){{.i = cases[i].hash}, {.p = pw}, {.p = salt}, {.i = cases[i].c}, {.i = cases[i].n}}, 5).p,
			cases[i].want);
		ak_release(pw), ak_release(salt);
	}
	ak_bytes *e = text("");
	CHECK(fn("crypto.pbkdf2With")((ak_val[]){{.i = AK_HASH_SHA256}, {.p = e}, {.p = e}, {.i = 0}, {.i = 32}}, 5).p == NULL);
	CHECK(ak_error_pending());
	if (ak_error_pending()) ak_release(ak_error_take());
	ak_release(e);
}

T_MAIN()
