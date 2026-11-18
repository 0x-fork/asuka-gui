// std/crypto's keys and signatures (core/src/std/pubkey.c): keys read
// from PEM, DER and their parts, and written as Go's crypto/x509 writes
// them; RSA PKCS#1 v1.5 and PSS, ECDSA (RFC 6979's deterministic
// signatures) and Ed25519, against signatures Go's crypto made and
// Wycheproof's cases (testdata/wycheproof); and what isn't a key.
//
// The keys below are Go's (crypto/rsa, crypto/ecdsa with RFC 6979's
// A.2.5 and A.2.6 private keys, crypto/ed25519), as Go's crypto/x509
// writes them, with what Go signed with them.
#include "t.h"
#include "std/std.h"

static const char RSA_PKCS1_PEM[] =
	"-----BEGIN RSA PRIVATE KEY-----\n"
	"MIIEogIBAAKCAQEAquPUG7diCjfRjm6vUWnMdXROHBAuMPwi6dhdN7uxI0K+4zn8\n"
	"18h25z1z4hLNgiH2VOX5VLuaC33xGIM4kv8K3ieuSuGkqZPlEPadLQF9YzSIbCAV\n"
	"mcbQHqImwliu8g1qFNxdJFhO7j+iFy2vyW/2ezzZoe76Z/AassOER/mfHeT724YD\n"
	"jbjEjzvTacrNvI8yAUU2ilkM1SbLN+RqxUDB9KDGhU9i/PXxTjY1H6pf2wrCRQMS\n"
	"hZ7Mj7LBUKufr1zB1xgPIGPoFlRVreeaW2vUAmHuDWwL6E6K0pBcP/hN0HjVuNSB\n"
	"M1hGq6Git7GgRubfRNMyvruwMS2oa1Kp5SyOqQIDAQABAoIBAEeqW91zvm3CkuNE\n"
	"j8l4sQHg0JD8SCLK2mlvD0i8McEz0JHCU8K6kwLOxrZcZEraq09XUoSyWBvRqTY6\n"
	"cZNT2d7ojTNiBYgIOdwvUCL7qPXdpgiSO/zNX6I5JkACqPthnMc60uTXE+LeVL+b\n"
	"rUiw55EvJ6W9bs3/53JohCUbDhueqFLU1Gnr9ltKVUP5NK8WbxdFojxUtulpJdBr\n"
	"L7pNtaVhmo3VSdKTTWmzDAsf3yvXltAjHXPnFQVucerg8pmiYGucw+4QJ8tBjVxI\n"
	"9cU+b/8Mw4QIihKdByLSpFoPOV1c7CaKiwg2duZjl6xHabykndwOOxw9/L3uZ1WK\n"
	"uzADhjECgYEAx8idyNO0uEUPo0NHOkGBPyN2AvoTO4hUj5fYIogjNvIAlUkgw6Op\n"
	"bSBlUe//pE1HxnXYxO+gn1OnG/OL/D6B6i8eddm9WLxoFuQczpBOarher1k6r1I0\n"
	"1OHbOtzXZ4qtTuEavi8mCSxvBSBG+QH3ImwalCgSQ49xX6vGFuCh9FcCgYEA2vnb\n"
	"523k5c8j39Z/eLoHXvkv8k5yklBvSGPd7ot0pxx9ZNe8ZHt9xYUuibWcQVAS6ibH\n"
	"OBjgGdF+EktAE5BBGLo4a4i1VYhCYU8xDNzZB/6YZvju8r5/HF0vt5FepDVq3YKH\n"
	"VkWR9uKKrKUZTOF5DCmlERjBtRl0GoLyxCw7tP8CgYB2vd5KBspa23E38DzdeyF+\n"
	"jW0Ox+O2dokL74weuht0BNIj6Jr4rMV+fsq5sJOY6/XFFzYHlwU4rVedXVvOSLie\n"
	"n/LSyN+pZhHXySz1+P8inwJn5fjjS1DrVnIewjOk7t+jClgCwbMxsJ8rxMtqJKjm\n"
	"ZKpHdKmmThNscYQDBPA9cQKBgD+oW1FQFTbpnNg5lx0Vl4b4kgOy4oO+dltpDN+I\n"
	"EwVxttJWFSJw9AMz0G+gKCikipsfYtLgCGX5DNlsntgRG5uAp147g9u1nPaE+xFu\n"
	"MnmsfcmRnIVc877fKDwJOqYqEUML6yQdXW2UNhQyesRq17G9eYsa01U+bQ43+zMk\n"
	"WMcpAoGARV5kwy5OsHoCejpBWYu+f4E3c79wLMdDNCuBqdsxIGbUnIlKu1t22/cc\n"
	"W8vgfOORjZdKJfWBUp0vMN/RD0aax0hVHv0eIXXyE+pe3Avc32X69Md29qJrtoQI\n"
	"hohg94fJ5SXTR6NEAvH7v1iJnLcXGz/ktDR3WBGzYSSrbuZgKFs=\n"
	"-----END RSA PRIVATE KEY-----\n"
;
static const char RSA_PUBLIC_PEM[] =
	"-----BEGIN PUBLIC KEY-----\n"
	"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAquPUG7diCjfRjm6vUWnM\n"
	"dXROHBAuMPwi6dhdN7uxI0K+4zn818h25z1z4hLNgiH2VOX5VLuaC33xGIM4kv8K\n"
	"3ieuSuGkqZPlEPadLQF9YzSIbCAVmcbQHqImwliu8g1qFNxdJFhO7j+iFy2vyW/2\n"
	"ezzZoe76Z/AassOER/mfHeT724YDjbjEjzvTacrNvI8yAUU2ilkM1SbLN+RqxUDB\n"
	"9KDGhU9i/PXxTjY1H6pf2wrCRQMShZ7Mj7LBUKufr1zB1xgPIGPoFlRVreeaW2vU\n"
	"AmHuDWwL6E6K0pBcP/hN0HjVuNSBM1hGq6Git7GgRubfRNMyvruwMS2oa1Kp5SyO\n"
	"qQIDAQAB\n"
	"-----END PUBLIC KEY-----\n"
;
static const char RSA_PKCS1_PUBLIC_PEM[] =
	"-----BEGIN RSA PUBLIC KEY-----\n"
	"MIIBCgKCAQEAquPUG7diCjfRjm6vUWnMdXROHBAuMPwi6dhdN7uxI0K+4zn818h2\n"
	"5z1z4hLNgiH2VOX5VLuaC33xGIM4kv8K3ieuSuGkqZPlEPadLQF9YzSIbCAVmcbQ\n"
	"HqImwliu8g1qFNxdJFhO7j+iFy2vyW/2ezzZoe76Z/AassOER/mfHeT724YDjbjE\n"
	"jzvTacrNvI8yAUU2ilkM1SbLN+RqxUDB9KDGhU9i/PXxTjY1H6pf2wrCRQMShZ7M\n"
	"j7LBUKufr1zB1xgPIGPoFlRVreeaW2vUAmHuDWwL6E6K0pBcP/hN0HjVuNSBM1hG\n"
	"q6Git7GgRubfRNMyvruwMS2oa1Kp5SyOqQIDAQAB\n"
	"-----END RSA PUBLIC KEY-----\n"
;
static const char RSA_PKCS8_DER[] = "308204bc020100300d06092a864886f70d0101010500048204a6308204a20201000282010100aae3d41bb7620a37d18e6eaf5169cc75744e1c102e30fc22e9d85d37bbb12342bee339fcd7c876e73d73e212cd8221f654e5f954bb9a0b7df118833892ff0ade27ae4ae1a4a993e510f69d2d017d6334886c201599c6d01ea226c258aef20d6a14dc5d24584eee3fa2172dafc96ff67b3cd9a1eefa67f01ab2c38447f99f1de4fbdb86038db8c48f3bd369cacdbc8f320145368a590cd526cb37e46ac540c1f4a0c6854f62fcf5f14e36351faa5fdb0ac2450312859ecc8fb2c150ab9faf5cc1d7180f2063e8165455ade79a5b6bd40261ee0d6c0be84e8ad2905c3ff84dd078d5b8d481335846aba1a2b7b1a046e6df44d332bebbb0312da86b52a9e52c8ea902030100010282010047aa5bdd73be6dc292e3448fc978b101e0d090fc4822cada696f0f48bc31c133d091c253c2ba9302cec6b65c644adaab4f575284b2581bd1a9363a719353d9dee88d336205880839dc2f5022fba8f5dda608923bfccd5fa239264002a8fb619cc73ad2e4d713e2de54bf9bad48b0e7912f27a5bd6ecdffe7726884251b0e1b9ea852d4d469ebf65b4a5543f934af166f1745a23c54b6e96925d06b2fba4db5a5619a8dd549d2934d69b30c0b1fdf2bd796d0231d73e715056e71eae0f299a2606b9cc3ee1027cb418d5c48f5c53e6fff0cc384088a129d0722d2a45a0f395d5cec268a8b083676e66397ac4769bca49ddc0e3b1c3dfcbdee67558abb3003863102818100c7c89dc8d3b4b8450fa343473a41813f237602fa133b88548f97d822882336f200954920c3a3a96d206551efffa44d47c675d8c4efa09f53a71bf38bfc3e81ea2f1e75d9bd58bc6816e41cce904e6ab85eaf593aaf5234d4e1db3adcd7678aad4ee11abe2f26092c6f052046f901f7226c1a942812438f715fabc616e0a1f45702818100daf9dbe76de4e5cf23dfd67f78ba075ef92ff24e7292506f4863ddee8b74a71c7d64d7bc647b7dc5852e89b59c415012ea26c73818e019d17e124b4013904118ba386b88b5558842614f310cdcd907fe9866f8eef2be7f1c5d2fb7915ea4356add8287564591f6e28aaca5194ce1790c29a51118c1b519741a82f2c42c3bb4ff02818076bdde4a06ca5adb7137f03cdd7b217e8d6d0ec7e3b676890bef8c1eba1b7404d223e89af8acc57e7ecab9b09398ebf5c5173607970538ad579d5d5bce48b89e9ff2d2c8dfa96611d7c92cf5f8ff229f0267e5f8e34b50eb56721ec233a4eedfa30a5802c1b331b09f2bc4cb6a24a8e664aa4774a9a64e136c71840304f03d710281803fa85b51501536e99cd839971d159786f89203b2e283be765b690cdf88130571b6d256152270f40333d06fa02828a48a9b1f62d2e00865f90cd96c9ed8111b9b80a75e3b83dbb59cf684fb116e3279ac7dc9919c855cf3bedf283c093aa62a11430beb241d5d6d943614327ac46ad7b1bd798b1ad3553e6d0e37fb332458c729028180455e64c32e4eb07a027a3a41598bbe7f813773bf702cc743342b81a9db312066d49c894abb5b76dbf71c5bcbe07ce3918d974a25f581529d2f30dfd10f469ac748551efd1e2175f213ea5edc0bdcdf65faf4c776f6a26bb68408868860f787c9e525d347a34402f1fbbf58899cb7171b3fe4b434775811b36124ab6ee660285b";
static const char RSA_SPKI_DER[] = "30820122300d06092a864886f70d01010105000382010f003082010a0282010100aae3d41bb7620a37d18e6eaf5169cc75744e1c102e30fc22e9d85d37bbb12342bee339fcd7c876e73d73e212cd8221f654e5f954bb9a0b7df118833892ff0ade27ae4ae1a4a993e510f69d2d017d6334886c201599c6d01ea226c258aef20d6a14dc5d24584eee3fa2172dafc96ff67b3cd9a1eefa67f01ab2c38447f99f1de4fbdb86038db8c48f3bd369cacdbc8f320145368a590cd526cb37e46ac540c1f4a0c6854f62fcf5f14e36351faa5fdb0ac2450312859ecc8fb2c150ab9faf5cc1d7180f2063e8165455ade79a5b6bd40261ee0d6c0be84e8ad2905c3ff84dd078d5b8d481335846aba1a2b7b1a046e6df44d332bebbb0312da86b52a9e52c8ea90203010001";
static const char RSA_SIG_PKCS1_SHA256[] = "42497d4d1154729bb2b79e3942c223f753bccba31bef2e08d44a005f573ef9047f8d41b6fab88995972b8bacc4736eb87e5b80575f2ac75430788a4ebc2ff1edf7273ff851dc78fd4a0dc892f76fe2dd88584c88ba2e87fe815e818751ee14f0523488f0fda26b2faab9af1e183698f20caa9422839b338fa9c2a383091c4522f775bae6c200afb776827d8fc947901d90ae905424113fed43e1442936ce689c2fe7ae3981e42e02b48e11505bcf4187a4592c4bff627f331c6166564e1d0d09873b796379648c6a771d334d6f37638f4ed66ab13bf90fdb23038ed97aac5d32f8873c39163fc78cc3f983878d6a31f770675ce610c834cab216669182e3acec";
static const char RSA_SIG_PKCS1_SHA512[] = "28d27b375be76d081665e0160a91f2dac38feaf618055251140fc94782ea2900d30a42721c34e6630a8f84c599b14e96af049916a8a94d9244dfed071eca15c643a92d0ca18a8683ba8dfa977c092710dccef106e7493a65ab8ff9fd03a25d560e9b672d18a293db67cf6071253aa009d0409a062973a463de1805858aacaa88fa45a8b94a56cdf455a03104f3eab099297b8441f7e1af5246cbc74149ca21e22b9cb1d25b5965f5915c3da1fa38b42511850017b11fd51fa23a18f7f25e3750d23980d3d42c4905a8e57327ba96b71b3222bb5aa0e00c5432cbf182f6fe7fa62124dc5e48fcec5a2c84f3dbb511279cce2ff048ee2100df8d24384d3332625a";
static const char RSA_SIG_PSS_SHA256[] = "24ac23bfaf09af4b64f5b9c3ac57ad370897e1c1ec339c7537e57b40850810c880501d3a2282851d5ac58a968ea5304481428e8237959e615c9b72e8b173ab643ea4a7e74fb4eba6842e75865529fc22137214484719996c805b27cb162f6d3f185f8b68dfecad97f58cd6756e6fcf6730bb0b9682309811ea31c3e90ed6e36f5de618f03dd1f9878716d86f1813b58d71a02dd937a44e85189edbfc3f2338af9934725de69510a0b206661fc73bdbb24842f94a6d434de6f5792c48f3481f571754025b1ff25226cd7efe8152bdd719cf75b6c7420f0319c6b7ec97d177591988be3df8e512faca644f505843160a1fed92c23d6b754c1575119f41bfdcf3a6";
static const char RSA_SIG_PSS0_SHA256[] = "8f4e92dd7e29dd234410da5ad4063a0ba9d3bf4c75b3b256aae4f295a6e7ea5af5ce6c8600f0810abdfdd12eb30924db925ca54eb4508c8e6853dbd1d5feb7cead7ce4ccff739796cdc1932f6ace97cd62d5ee921b4e34ea41f3112bc8ccddeb7fa5bd716af9c1113d8b1a8dd516a51953ce598d8fd6aa9d733cc3022c42c23c6d67aeba449338a8637a40b8cc032661f36a18afa868e3b1593f2fc999c6ce5b1aca8a17462e0d8f0b4def8a0e066372236a2cb2b0153026a1af4b57b66a0a9ee20d061a6cf4ad6966962bc4187251eb46618e0c8a373209ffee811fa04ebd35454ec456c526a7d25a89f01a36a7a22f9dc62d520224cc6fd60ded1dfac755b0";
static const char EC_SEC1_PEM[] =
	"-----BEGIN EC PARAMETERS-----\n"
	"BggqhkjOPQMBBw==\n"
	"-----END EC PARAMETERS-----\n"
	"-----BEGIN EC PRIVATE KEY-----\n"
	"MHcCAQEEIMmvqdhFunUWa1whV2ex1pNOUMPbNuibEnuKYisSD2choAoGCCqGSM49\n"
	"AwEHoUQDQgAEYP7UuiVanTHJYet0xjVtaMBJuJI7Yfps5mliLmDyn7Z5A/4QCLi8\n"
	"maQa6elWKLxk8vGyDC1+n1F3o8KU1EYimQ==\n"
	"-----END EC PRIVATE KEY-----\n"
;
static const char EC_PKCS8_DER[] = "308187020100301306072a8648ce3d020106082a8648ce3d030107046d306b0201010420c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721a1440342000460fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb67903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299";
static const char EC_SPKI_DER[] = "3059301306072a8648ce3d020106082a8648ce3d0301070342000460fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb67903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299";
static const char EC_SIG_SAMPLE_DER[] = "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
static const char P384_PKCS8_DER[] = "3081b6020100301006072a8648ce3d020106052b8104002204819e30819b02010104306b9d3dad2e1b8c1c05b19875b6659f4de23c3b667bf297ba9aa47740787137d896d5724e4c70a825f872c9ea60d2edf5a16403620004ec3a4e415b4e19a4568618029f427fa5da9a8bc4ae92e02e06aae5286b300c64def8f0ea9055866064a254515480bc138015d9b72d7d57244ea8ef9ac0c621896708a59367f9dfb9f54ca84b3f1c9db1288b231c3ae0d4fe7344fd2533264720";
static const char P384_SIG_SAMPLE_DER[] = "306602310094edbb92a5ecb8aad4736e56c691916b3f88140666ce9fa73d64c4ea95ad133c81a648152e44acf96e36dd1e80fabe4602310099ef4aeb15f178cea1fe40db2603138f130e740a19624526203b6351d0a3a94fa329c145786e679e7b82c71a38628ac8";
static const char ED_PKCS8_DER[] = "302e020100300506032b657004220420000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
static const char ED_SPKI_DER[] = "302a300506032b657003210003a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
static const char EC_CERT_PEM[] =
	"-----BEGIN CERTIFICATE-----\n"
	"MIIBFTCBvaADAgECAgEHMAoGCCqGSM49BAMCMBUxEzARBgNVBAMTCnN0ZC9jcnlw\n"
	"dG8wHhcNMjMxMTE0MjIxMzIwWhcNMzAwMzE3MTc0NjQwWjAVMRMwEQYDVQQDEwpz\n"
	"dGQvY3J5cHRvMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEYP7UuiVanTHJYet0\n"
	"xjVtaMBJuJI7Yfps5mliLmDyn7Z5A/4QCLi8maQa6elWKLxk8vGyDC1+n1F3o8KU\n"
	"1EYimTAKBggqhkjOPQQDAgNHADBEAiB4CVUwmEj23CNEVTz3hiqLcPezfABofHlE\n"
	"1cV0f2rTNAIgevaAqipKbz955etTGPwijj0SOJgCUZwb32Y75S/am50=\n"
	"-----END CERTIFICATE-----\n"
;

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

// What a test made, released at its end.
static ak_obj *made[256];
static int nmade;

static void *keep(void *p) {
	if (p) made[nmade++] = p;
	return p;
}

static void drop(void) {
	while (nmade) ak_release(made[--nmade]);
}

static ak_bytes *hexval(const char *h) {
	size_t n = strcmp(h, "-") == 0 ? 0 : strlen(h) / 2;
	ak_bytes *b = ak_bytes_new(NULL, n);
	for (size_t i = 0; i < n; i++) {
		unsigned v;
		sscanf(h + 2 * i, "%2x", &v);
		b->data[i] = (uint8_t)v;
	}
	return keep(b);
}

static ak_bytes *text(const char *s) { return keep(ak_bytes_new(s, strlen(s))); }

static bool same(const ak_bytes *a, const ak_bytes *b) { return a && b && a->len == b->len && memcmp(a->data, b->data, a->len) == 0; }

static const char *failure(void) {
	static char why[200];
	ak_str *s = fn("crypto_pk.keyFailure")(NULL, 0).p;
	snprintf(why, sizeof why, "%s", s->data);
	ak_release(s);
	return why;
}

static ak_bytes *read_public(ak_bytes *in, bool pem) { return keep(fn("crypto_pk.readPublic")((ak_val[]){{.p = in}, {.i = pem}}, 2).p); }
static ak_bytes *read_private(ak_bytes *in, bool pem) { return keep(fn("crypto_pk.readPrivate")((ak_val[]){{.p = in}, {.i = pem}}, 2).p); }
static ak_bytes *public_of(ak_bytes *k) { return keep(fn("crypto_pk.publicOf")((ak_val[]){{.p = k}}, 1).p); }

enum { KIND_RSA = 1, KIND_P256, KIND_P384, KIND_P521, KIND_ED25519 };
enum { PKCS1 = 1, PSS, ECDSA, ECDSA_DER, ED25519 };

// info: the key's kind * 10000 + its bits, or -1.
static int64_t info(ak_bytes *der, bool private) {
	ak_arr *a = fn("crypto_pk.keyInfo")((ak_val[]){{.p = der}, {.i = private}}, 2).p;
	if (!a) return -1;
	int64_t v = a->data[0].i * 10000 + a->data[1].i;
	ak_release(a);
	return v;
}

static int64_t verify(ak_bytes *spki, int scheme, int hash, ak_bytes *msg, ak_bytes *sig) {
	return fn("crypto_pk.verifySignature")((ak_val[]){{.p = spki}, {.i = scheme}, {.i = hash}, {.p = msg}, {.p = sig}}, 5).i;
}

static ak_bytes *sign(ak_bytes *pk8, int scheme, int hash, ak_bytes *msg) {
	return keep(fn("crypto_pk.signMessage")((ak_val[]){{.p = pk8}, {.i = scheme}, {.i = hash}, {.p = msg}}, 4).p);
}

TEST(read_keys) {
	// Each form reads to what Go's x509 writes for the key.
	ak_bytes *rsa8 = hexval(RSA_PKCS8_DER), *rsaspki = hexval(RSA_SPKI_DER);
	CHECK(same(read_private(text(RSA_PKCS1_PEM), true), rsa8));
	CHECK(same(read_private(rsa8, false), rsa8));
	CHECK(same(read_public(text(RSA_PUBLIC_PEM), true), rsaspki));
	CHECK(same(read_public(text(RSA_PKCS1_PUBLIC_PEM), true), rsaspki));
	CHECK(same(read_public(rsaspki, false), rsaspki));
	CHECK(same(public_of(rsa8), rsaspki));
	CHECK_EQ_INT(info(rsa8, true), KIND_RSA * 10000 + 2048);
	CHECK_EQ_INT(info(rsaspki, false), KIND_RSA * 10000 + 2048);
	// SEC 1's EC PRIVATE KEY, after the EC PARAMETERS block openssl writes.
	ak_bytes *ec8 = hexval(EC_PKCS8_DER), *ecspki = hexval(EC_SPKI_DER);
	CHECK(same(read_private(text(EC_SEC1_PEM), true), ec8));
	CHECK(same(public_of(ec8), ecspki));
	CHECK(same(read_public(text(EC_CERT_PEM), true), ecspki));
	CHECK_EQ_INT(info(ec8, true), KIND_P256 * 10000 + 256);
	ak_bytes *p384 = hexval(P384_PKCS8_DER);
	CHECK(same(read_private(p384, false), p384));
	CHECK_EQ_INT(info(p384, true), KIND_P384 * 10000 + 384);
	ak_bytes *ed8 = hexval(ED_PKCS8_DER), *edspki = hexval(ED_SPKI_DER);
	CHECK(same(read_private(ed8, false), ed8));
	CHECK(same(public_of(ed8), edspki));
	CHECK_EQ_INT(info(edspki, false), KIND_ED25519 * 10000 + 256);
	// The DER of a certificate, and SEC 1's DER, read as well.
	drop();
}

TEST(not_keys) {
	// What isn't a key: none, saying why.
	struct { const char *in; bool pem, private; const char *why; } cases[] = {
		{"", false, false, "SubjectPublicKeyInfo"},
		{"-----BEGIN PUBLIC KEY-----\nMFkw\n-----END PUBLIC KEY-----\n", true, false, "SubjectPublicKeyInfo"},
		{"-----BEGIN ENCRYPTED PRIVATE KEY-----\nMIIB\n-----END ENCRYPTED PRIVATE KEY-----\n", true, true, "encrypted"},
		{"-----BEGIN RSA PRIVATE KEY-----\nProc-Type: 4,ENCRYPTED\nDEK-Info: AES-128-CBC,00\n\nMIIB\n-----END RSA PRIVATE KEY-----\n", true, true, "encrypted"},
		{"-----BEGIN OPENSSH PRIVATE KEY-----\nb3Bl\n-----END OPENSSH PRIVATE KEY-----\n", true, true, "OpenSSH"},
		{"-----BEGIN PUBLIC KEY-----\nMFkw", true, false, "END"},
		{"-----BEGIN PUBLIC KEY-----\nM!kw\n-----END PUBLIC KEY-----\n", true, false, "Base64"},
		{"-----BEGIN CERTIFICATE REQUEST-----\nMFkw\n-----END CERTIFICATE REQUEST-----\n", true, false, "no PUBLIC KEY"},
		{"not pem", true, false, "isn't PEM"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		ak_bytes *in = text(cases[i].in);
		ak_bytes *k = cases[i].private ? read_private(in, cases[i].pem) : read_public(in, cases[i].pem);
		if (k) T_FAIL("case %zu read", i);
		else if (!strstr(failure(), cases[i].why)) T_FAIL("case %zu: %s", i, failure());
	}
	// Every prefix of a key's DER, and the DER with each byte changed,
	// reads cleanly (none, or a key) under ASan.
	const char *ders[] = {RSA_SPKI_DER, EC_SPKI_DER, ED_SPKI_DER, RSA_PKCS8_DER, EC_PKCS8_DER, ED_PKCS8_DER};
	for (size_t d = 0; d < AK_LEN(ders); d++) {
		ak_bytes *full = hexval(ders[d]);
		bool private = d >= 3;
		for (uint32_t n = 0; n < full->len; n++) {
			ak_bytes *cut = ak_bytes_new(full->data, n);
			ak_bytes *k = fn(private ? "crypto_pk.readPrivate" : "crypto_pk.readPublic")((ak_val[]){{.p = cut}, {.i = 0}}, 2).p;
			if (k) {
				T_FAIL("%zu: %u bytes of it read", d, n);
				ak_release(k);
			}
			ak_release(cut);
		}
		for (uint32_t i = 0; i < full->len; i += full->len > 300 ? 13 : 1) {
			full->data[i] ^= 0x41;
			ak_bytes *k = fn(private ? "crypto_pk.readPrivate" : "crypto_pk.readPublic")((ak_val[]){{.p = full}, {.i = 0}}, 2).p;
			if (k) ak_release(k);
			full->data[i] ^= 0x41;
		}
	}
	// A point that isn't on its curve.
	ak_bytes *ec = hexval(EC_SPKI_DER);
	ec->data[ec->len - 1] ^= 1;
	CHECK(!read_public(ec, false));
	CHECK(strstr(failure(), "curve") != NULL);
	drop();
}

TEST(rsa) {
	ak_bytes *pk8 = hexval(RSA_PKCS8_DER), *spki = hexval(RSA_SPKI_DER), *msg = text("std/crypto signs this"), *other = text("std/crypto signs that");
	ak_bytes *s256 = hexval(RSA_SIG_PKCS1_SHA256), *s512 = hexval(RSA_SIG_PKCS1_SHA512), *pss = hexval(RSA_SIG_PSS_SHA256),
		 *pss0 = hexval(RSA_SIG_PSS0_SHA256);
	// Go's signatures verify; PKCS#1 v1.5 signs as Go does (it's
	// deterministic).
	CHECK_EQ_INT(verify(spki, PKCS1, AK_HASH_SHA256, msg, s256), 1);
	CHECK_EQ_INT(verify(spki, PKCS1, AK_HASH_SHA512, msg, s512), 1);
	CHECK_EQ_INT(verify(spki, PSS, AK_HASH_SHA256, msg, pss), 1);
	CHECK_EQ_INT(verify(spki, PSS, AK_HASH_SHA256, msg, pss0), 1);
	CHECK(same(sign(pk8, PKCS1, AK_HASH_SHA256, msg), s256));
	CHECK(same(sign(pk8, PKCS1, AK_HASH_SHA512, msg), s512));
	// Not this message, not this hash, not this padding, not a changed bit.
	CHECK_EQ_INT(verify(spki, PKCS1, AK_HASH_SHA256, other, s256), 0);
	CHECK_EQ_INT(verify(spki, PKCS1, AK_HASH_SHA512, msg, s256), 0);
	CHECK_EQ_INT(verify(spki, PSS, AK_HASH_SHA256, msg, s256), 0);
	CHECK_EQ_INT(verify(spki, PKCS1, AK_HASH_SHA256, msg, pss), 0);
	for (uint32_t bit = 0; bit < 8 * s256->len; bit += 61) {
		s256->data[bit / 8] ^= (uint8_t)(1 << bit % 8);
		if (verify(spki, PKCS1, AK_HASH_SHA256, msg, s256) != 0) T_FAIL("verified with bit %u changed", bit);
		s256->data[bit / 8] ^= (uint8_t)(1 << bit % 8);
	}
	ak_bytes *cut = keep(ak_bytes_new(s256->data, s256->len - 1));
	CHECK_EQ_INT(verify(spki, PKCS1, AK_HASH_SHA256, msg, cut), 0);
	// PSS signs with a fresh salt, so each signature differs, and verifies.
	ak_bytes *p1 = sign(pk8, PSS, AK_HASH_SHA256, msg), *p2 = sign(pk8, PSS, AK_HASH_SHA256, msg);
	REQUIRE(p1 && p2 && p1->len == 256);
	CHECK(!same(p1, p2));
	CHECK_EQ_INT(verify(spki, PSS, AK_HASH_SHA256, msg, p1), 1);
	CHECK_EQ_INT(verify(spki, PSS, AK_HASH_SHA512, msg, sign(pk8, PSS, AK_HASH_SHA512, msg)), 1);
	CHECK_EQ_INT(verify(spki, PKCS1, AK_HASH_SHA1, msg, sign(pk8, PKCS1, AK_HASH_SHA1, msg)), 1);
	// An RSA key with another scheme: -1 (none), saying why.
	CHECK_EQ_INT(verify(spki, ECDSA, AK_HASH_SHA256, msg, s256), -1);
	CHECK(strstr(failure(), "rsaPKCS1v15") != NULL);
	CHECK(!sign(pk8, ED25519, 0, msg));
	drop();
}

static void ecdsa_sample(const char *pk8hex, int hash, const char *wantder, size_t c) {
	ak_bytes *pk8 = hexval(pk8hex), *msg = text("sample"), *want = hexval(wantder);
	ak_bytes *spki = public_of(pk8);
	// RFC 6979's deterministic signature, as DER and as r ‖ s.
	ak_bytes *der = sign(pk8, ECDSA_DER, hash, msg), *raw = sign(pk8, ECDSA, hash, msg);
	CHECK(same(der, want));
	REQUIRE(raw && raw->len == 2 * c);
	CHECK_EQ_INT(verify(spki, ECDSA_DER, hash, msg, want), 1);
	CHECK_EQ_INT(verify(spki, ECDSA, hash, msg, raw), 1);
	CHECK_EQ_INT(verify(spki, ECDSA, hash, msg, want), 0);   // DER isn't r ‖ s
	CHECK_EQ_INT(verify(spki, ECDSA_DER, hash, msg, raw), 0);
	CHECK_EQ_INT(verify(spki, ECDSA, hash, text("samplf"), raw), 0);
	raw->data[3] ^= 1;
	CHECK_EQ_INT(verify(spki, ECDSA, hash, msg, raw), 0);
	CHECK_EQ_INT(verify(spki, ECDSA, AK_HASH_SHA1, msg, raw), -1);
	CHECK_EQ_INT(verify(spki, PKCS1, hash, msg, raw), -1);
}

TEST(ecdsa) {
	ecdsa_sample(EC_PKCS8_DER, AK_HASH_SHA256, EC_SIG_SAMPLE_DER, 32);
	ecdsa_sample(P384_PKCS8_DER, AK_HASH_SHA384, P384_SIG_SAMPLE_DER, 48);
	drop();
}

TEST(new_keys) {
	// Each kind made signs what its public key verifies; RSA isn't made.
	int kinds[] = {KIND_P256, KIND_P384, KIND_P521, KIND_ED25519};
	int schemes[] = {ECDSA, ECDSA_DER, ECDSA, ED25519};
	int bits[] = {256, 384, 521, 256};
	ak_bytes *msg = text("a new key");
	for (size_t i = 0; i < AK_LEN(kinds); i++) {
		ak_bytes *k = keep(fn("crypto_pk.generateKey")((ak_val[]){{.i = kinds[i]}}, 1).p);
		REQUIRE(k);
		CHECK_EQ_INT(info(k, true), kinds[i] * 10000 + bits[i]);
		ak_bytes *pub = public_of(k), *sig = sign(k, schemes[i], AK_HASH_SHA512, msg);
		CHECK_EQ_INT(verify(pub, schemes[i], AK_HASH_SHA512, msg, sig), 1);
		ak_bytes *again = keep(fn("crypto_pk.generateKey")((ak_val[]){{.i = kinds[i]}}, 1).p);
		CHECK(!same(k, again));
		// Its PEM reads back to it.
		ak_str *label = ak_str_c("PRIVATE KEY");
		ak_str *pem = fn("crypto_pk.pemOf")((ak_val[]){{.p = k}, {.p = label}}, 2).p;
		ak_bytes *pemb = keep(ak_bytes_new(pem->data, pem->len));
		CHECK(same(read_private(pemb, true), k));
		ak_release(label), ak_release(pem);
	}
	CHECK(!fn("crypto_pk.generateKey")((ak_val[]){{.i = KIND_RSA}}, 1).p);
	CHECK(strstr(failure(), "RSA") != NULL);
	drop();
}

TEST(parts) {
	// A key's JWK parts make the same key again, private and public.
	const char *ders[] = {RSA_PKCS8_DER, EC_PKCS8_DER, P384_PKCS8_DER, ED_PKCS8_DER};
	for (size_t d = 0; d < AK_LEN(ders); d++) {
		ak_bytes *pk8 = hexval(ders[d]), *spki = public_of(pk8);
		int64_t kind = info(pk8, true) / 10000;
		for (int private = 0; private < 2; private++) {
			ak_bytes *der = private ? pk8 : spki;
			ak_arr *parts = keep(fn("crypto_pk.keyParts")((ak_val[]){{.p = der}, {.i = private}}, 2).p);
			REQUIRE(parts);
			ak_bytes *back = keep(fn("crypto_pk.keyFromParts")((ak_val[]){{.i = kind}, {.p = parts}, {.i = private}}, 3).p);
			if (!same(back, der)) T_FAIL("key %zu (private %d) didn't come back from its parts: %s", d, private, failure());
		}
	}
	// Parts that don't agree: an EC d that isn't x and y's, an Ed25519 d
	// that isn't x's, the wrong count.
	ak_bytes *ec8 = hexval(EC_PKCS8_DER), *ed8 = hexval(ED_PKCS8_DER);
	ak_arr *ecp = keep(fn("crypto_pk.keyParts")((ak_val[]){{.p = ec8}, {.i = 1}}, 2).p);
	ak_arr *edp = keep(fn("crypto_pk.keyParts")((ak_val[]){{.p = ed8}, {.i = 1}}, 2).p);
	((ak_bytes *)ecp->data[2].p)->data[31] ^= 1;
	((ak_bytes *)edp->data[1].p)->data[0] ^= 1;
	CHECK(!fn("crypto_pk.keyFromParts")((ak_val[]){{.i = KIND_P256}, {.p = ecp}, {.i = 1}}, 3).p);
	CHECK(!fn("crypto_pk.keyFromParts")((ak_val[]){{.i = KIND_ED25519}, {.p = edp}, {.i = 1}}, 3).p);
	CHECK(!fn("crypto_pk.keyFromParts")((ak_val[]){{.i = KIND_P384}, {.p = ecp}, {.i = 0}}, 3).p);
	CHECK(!fn("crypto_pk.keyFromParts")((ak_val[]){{.i = KIND_RSA}, {.p = ecp}, {.i = 0}}, 3).p);
	drop();
}

// wycheproof runs a signature file of testdata/wycheproof.
static void wycheproof(const char *file, int scheme, int hash) {
	char path[256];
	snprintf(path, sizeof path, "testdata/wycheproof/%s", file);
	FILE *f = fopen(path, "r");
	if (!f) {
		T_FAIL("no %s", path);
		return;
	}
	static char line[1 << 14];
	ak_bytes *key = NULL;
	int cases = 0, valid = 0, skipped = 0;
	while (fgets(line, sizeof line, f)) {
		char a[4096], b[4096], c[4096];
		if (sscanf(line, "key %4095s", a) == 1) {
			if (key) ak_release(key);
			ak_bytes *der = hexval(a);
			key = fn("crypto_pk.readPublic")((ak_val[]){{.p = der}, {.i = 0}}, 2).p;
			if (!key) skipped++;
			drop();
			continue;
		}
		if (sscanf(line, "%3s %4095s %4095s", a, b, c) != 3 || !key) continue;
		ak_bytes *msg = hexval(b), *sig = hexval(c);
		int64_t r = verify(key, scheme, hash, msg, sig);
		if (a[0] == 'v' && r != 1) T_FAIL("%s: a valid signature didn't verify: %s", file, c);
		if (a[0] == 'i' && r != 0) T_FAIL("%s: an invalid signature verified (%lld): %s", file, (long long)r, c);
		valid += a[0] == 'v';
		cases++;
		drop();
	}
	if (key) ak_release(key);
	fclose(f);
	CHECK(cases > 50 && valid > 5);
	CHECK(skipped == 0);
}

TEST(wycheproof_ecdsa) {
	wycheproof("ecdsa_secp256r1_sha256_p1363_test.txt", ECDSA, AK_HASH_SHA256);
	wycheproof("ecdsa_secp256r1_sha256_test.txt", ECDSA_DER, AK_HASH_SHA256);
	wycheproof("ecdsa_secp384r1_sha384_p1363_test.txt", ECDSA, AK_HASH_SHA384);
	wycheproof("ecdsa_secp521r1_sha512_p1363_test.txt", ECDSA, AK_HASH_SHA512);
}

TEST(wycheproof_rsa) {
	wycheproof("rsa_signature_2048_sha256_test.txt", PKCS1, AK_HASH_SHA256);
	wycheproof("rsa_pss_2048_sha256_mgf1_32_test.txt", PSS, AK_HASH_SHA256);
}

T_MAIN()
