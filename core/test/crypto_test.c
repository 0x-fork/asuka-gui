// SHA-512 (FIPS 180-4's vectors, and lengths around its padding) and
// Ed25519 (RFC 8032's vectors), as the core's crypto (core/src/std) has
// them, and std/crypto's builtins of them.
#include "t.h"
#include "std/std.h"

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

T_MAIN()
