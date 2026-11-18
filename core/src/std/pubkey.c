// pubkey.c: std/crypto's public-key signatures, the builtins
// crypto_pk.* (their own table: an app that checks no signatures links
// none of it).
//
// Keys are read from PEM (PUBLIC KEY, RSA PUBLIC KEY, CERTIFICATE: its
// subject's key; PRIVATE KEY, RSA PRIVATE KEY, EC PRIVATE KEY) or DER of
// the same, and from a JWK's parts (the Asuka side reads the JSON), and
// kept in one form each: a public key as X.509's SubjectPublicKeyInfo
// (RFC 5280), a private key as PKCS#8 (RFC 5208), both DER, so the Asuka
// side holds bytes it can write out again. The kinds: RSA (1024 to 4096
// bits), ECDSA over P-256, P-384 and P-521, and Ed25519.
//
// Signatures: RSA's PKCS#1 v1.5 and PSS (RFC 8017; PSS verifies any salt
// length, as Go's and OpenSSL's do by default, and signs with a salt as
// long as the hash, as JWS's PS256 needs), over BearSSL's constant-time
// modular arithmetic (third_party/bearssl, i31); ECDSA (FIPS 186-4) over
// BearSSL, its signatures r ‖ s (JWS's, WebCrypto's) or ASN.1 DER
// (X.509's, OpenSSL's), signing deterministic (RFC 6979); Ed25519 over
// ed25519.c. A key's kind and the scheme must agree: the Asuka side says
// so (crypto_pk.keyFailure) rather than a signature just not verifying.
#include "std/std.h"

#include <stdio.h>

#include "std/bearssl/bearssl.h"

// The largest RSA modulus read (BearSSL's i31 code's own bound).
#define RSA_MAX_BITS 4096
#define RSA_MAX (RSA_MAX_BITS / 8)

#define BYTES(i) ((ak_bytes *)args[i].p)

static const uint8_t *data(const ak_bytes *b) { return b ? b->data : (const uint8_t *)""; }
static size_t len(const ak_bytes *b) { return b ? b->len : 0; }

// The calling thread's last failure's reason (crypto_pk.keyFailure).
static AK_TLS char failure[200];

static bool fail(const char *why) {
	snprintf(failure, sizeof failure, "%s", why);
	return false;
}

// std/crypto's KeyKind and SignatureScheme, by their codes.
enum { KIND_RSA = 1, KIND_P256, KIND_P384, KIND_P521, KIND_ED25519 };
enum { SCHEME_PKCS1 = 1, SCHEME_PSS, SCHEME_ECDSA, SCHEME_ECDSA_DER, SCHEME_ED25519 };

// ---- DER (X.690), read ----

typedef struct {
	const uint8_t *p;
	size_t n;
} span;

// der_get reads the element at *s, of tag, into out (its contents), and
// moves past it; false for another tag or what isn't DER's (an
// indefinite length, one longer than what's left).
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

// der_tlv is der_get keeping the whole element (its tag and length too).
static bool der_tlv(span *s, uint8_t tag, span *whole) {
	const uint8_t *start = s->p;
	if (!der_get(s, tag, NULL)) return false;
	*whole = (span){start, (size_t)(s->p - start)};
	return true;
}

// der_uint reads an INTEGER that isn't negative, its leading zeros left
// out (an empty span for 0).
static bool der_uint(span *s, span *out) {
	span v;
	if (!der_get(s, 0x02, &v) || v.n == 0 || (v.p[0] & 0x80)) return false;
	while (v.n && v.p[0] == 0) v.p++, v.n--;
	*out = v;
	return true;
}

static bool span_is(span s, const uint8_t *p, size_t n) { return s.n == n && memcmp(s.p, p, n) == 0; }

// The algorithms' and the curves' object identifiers, DER's contents.
static const uint8_t OID_RSA[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
static const uint8_t OID_RSA_PSS[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a};
static const uint8_t OID_EC[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
static const uint8_t OID_P256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const uint8_t OID_P384[] = {0x2b, 0x81, 0x04, 0x00, 0x22};
static const uint8_t OID_P521[] = {0x2b, 0x81, 0x04, 0x00, 0x23};
static const uint8_t OID_ED25519[] = {0x2b, 0x65, 0x70};

// ---- Keys ----

typedef struct {
	int kind;
	span n, e, d, p, q, dp, dq, qi;  // RSA
	int curve;                       // BR_EC_secp256r1, ...: ECDSA
	span point;                      // ECDSA's public point (uncompressed), Ed25519's public key
	span priv;                       // ECDSA's scalar, Ed25519's seed
	uint8_t made[145];               // a public key computed from the private one
} key;

static int curve_kind(int curve) {
	return curve == BR_EC_secp256r1 ? KIND_P256 : curve == BR_EC_secp384r1 ? KIND_P384 : KIND_P521;
}

static int kind_curve(int kind) {
	return kind == KIND_P256 ? BR_EC_secp256r1 : kind == KIND_P384 ? BR_EC_secp384r1 : BR_EC_secp521r1;
}

// The curve's field size in bytes: a coordinate's, a scalar's.
static size_t curve_bytes(int curve) { return curve == BR_EC_secp256r1 ? 32 : curve == BR_EC_secp384r1 ? 48 : 66; }

static int curve_of(span oid) {
	if (span_is(oid, OID_P256, sizeof OID_P256)) return BR_EC_secp256r1;
	if (span_is(oid, OID_P384, sizeof OID_P384)) return BR_EC_secp384r1;
	if (span_is(oid, OID_P521, sizeof OID_P521)) return BR_EC_secp521r1;
	return 0;
}

static size_t bitlen(span v) {
	if (!v.n) return 0;
	size_t b = 8 * (v.n - 1);
	for (uint8_t top = v.p[0]; top; top >>= 1) b++;
	return b;
}

static size_t key_bits(const key *k) {
	switch (k->kind) {
	case KIND_RSA: return bitlen(k->n);
	case KIND_P256: return 256;
	case KIND_P384: return 384;
	case KIND_P521: return 521;
	}
	return 256;
}

static bool rsa_check(key *k) {
	size_t bits = bitlen(k->n);
	if (bits < 1024) return fail("RSA keys under 1024 bits aren't safe and aren't read");
	if (bits > RSA_MAX_BITS) return fail("RSA keys over 4096 bits aren't read");
	if (k->e.n == 0 || k->e.n > 4 || !(k->e.p[k->e.n - 1] & 1) || (k->e.n == 1 && k->e.p[0] < 3))
		return fail("the RSA key's public exponent isn't an odd number from 3 to 2^32");
	k->kind = KIND_RSA;
	return true;
}

// ec_point_check: whether k's point is one of its curve's.
static bool ec_point_check(key *k) {
	size_t c = curve_bytes(k->curve);
	if (k->point.n == 1 + c && (k->point.p[0] == 2 || k->point.p[0] == 3))
		return fail("the EC key's point is compressed, which isn't read: write it uncompressed");
	if (k->point.n != 1 + 2 * c || k->point.p[0] != 4) return fail("the EC key's point isn't one of its curve's");
	uint8_t p[133], one = 1;
	memcpy(p, k->point.p, k->point.n);
	if (!br_ec_prime_i31.mul(p, k->point.n, &one, 1, k->curve)) return fail("the EC key's point isn't one of its curve's");
	k->kind = curve_kind(k->curve);
	return true;
}

// rsa_public reads PKCS#1's RSAPublicKey (n, e).
static bool rsa_public(span s, key *k) {
	span seq;
	if (!der_get(&s, 0x30, &seq) || !der_uint(&seq, &k->n) || !der_uint(&seq, &k->e) || seq.n) return fail("it isn't an RSA public key");
	return rsa_check(k);
}

// spki reads X.509's SubjectPublicKeyInfo.
static bool spki(span s, key *k) {
	span seq, alg, oid, bits;
	if (!der_get(&s, 0x30, &seq) || s.n || !der_get(&seq, 0x30, &alg) || !der_get(&alg, 0x06, &oid) || !der_get(&seq, 0x03, &bits) ||
	    seq.n || bits.n < 1 || bits.p[0] != 0)
		return fail("it isn't a public key (X.509's SubjectPublicKeyInfo)");
	span pub = {bits.p + 1, bits.n - 1};
	if (span_is(oid, OID_RSA, sizeof OID_RSA) || span_is(oid, OID_RSA_PSS, sizeof OID_RSA_PSS)) return rsa_public(pub, k);
	if (span_is(oid, OID_EC, sizeof OID_EC)) {
		span curve;
		if (!der_get(&alg, 0x06, &curve) || !(k->curve = curve_of(curve)))
			return fail("the EC key's curve isn't P-256, P-384 or P-521");
		k->point = pub;
		return ec_point_check(k);
	}
	if (span_is(oid, OID_ED25519, sizeof OID_ED25519)) {
		if (pub.n != 32) return fail("an Ed25519 public key is 32 bytes");
		k->point = pub;
		k->kind = KIND_ED25519;
		return true;
	}
	return fail("the key isn't RSA, ECDSA (P-256, P-384, P-521) or Ed25519");
}

// certificate reads an X.509 certificate's subject's key (the
// certificate itself isn't checked: who signed it, when it's valid).
static bool certificate(span s, key *k) {
	span cert, tbs, whole;
	if (!der_get(&s, 0x30, &cert) || !der_get(&cert, 0x30, &tbs)) return fail("it isn't an X.509 certificate");
	if (tbs.n && tbs.p[0] == 0xa0) der_get(&tbs, 0xa0, NULL);  // [0] version
	if (!der_get(&tbs, 0x02, NULL) || !der_get(&tbs, 0x30, NULL) || !der_get(&tbs, 0x30, NULL) || !der_get(&tbs, 0x30, NULL) ||
	    !der_get(&tbs, 0x30, NULL) || !der_tlv(&tbs, 0x30, &whole))
		return fail("it isn't an X.509 certificate");
	return spki(whole, k);
}

// rsa_private reads PKCS#1's RSAPrivateKey (two primes: version 0).
static bool rsa_private(span s, key *k) {
	span seq, version;
	if (!der_get(&s, 0x30, &seq) || !der_get(&seq, 0x02, &version) || !span_is(version, (const uint8_t[]){0}, 1) ||
	    !der_uint(&seq, &k->n) || !der_uint(&seq, &k->e) || !der_uint(&seq, &k->d) || !der_uint(&seq, &k->p) ||
	    !der_uint(&seq, &k->q) || !der_uint(&seq, &k->dp) || !der_uint(&seq, &k->dq) || !der_uint(&seq, &k->qi))
		return fail("it isn't an RSA private key (PKCS#1's, of two primes)");
	if (!k->p.n || !k->q.n || !k->dp.n || !k->dq.n || !k->qi.n) return fail("the RSA private key's parts are zero");
	return rsa_check(k);
}

// ec_scalar_check: whether k's private scalar is from 1 to the curve's
// order less one.
static bool ec_scalar_check(const key *k) {
	size_t on;
	const uint8_t *order = br_ec_prime_i31.order(k->curve, &on);
	span x = k->priv;
	while (x.n && x.p[0] == 0) x.p++, x.n--;
	while (on && order[0] == 0) order++, on--;
	if (!x.n || x.n > on || (x.n == on && memcmp(x.p, order, on) >= 0)) return fail("the EC private key isn't below its curve's order");
	return true;
}

// ec_private reads SEC 1's ECPrivateKey; curve is the curve PKCS#8's
// algorithm said (0: the key's own [0] says).
static bool ec_private(span s, int curve, key *k) {
	span seq, version, d;
	if (!der_get(&s, 0x30, &seq) || !der_get(&seq, 0x02, &version) || !span_is(version, (const uint8_t[]){1}, 1) ||
	    !der_get(&seq, 0x04, &d))
		return fail("it isn't an EC private key (SEC 1's)");
	span tagged, oid, bits;
	if (seq.n && seq.p[0] == 0xa0) {
		if (!der_get(&seq, 0xa0, &tagged) || !der_get(&tagged, 0x06, &oid)) return fail("it isn't an EC private key (SEC 1's)");
		int own = curve_of(oid);
		if (!own) return fail("the EC key's curve isn't P-256, P-384 or P-521");
		if (curve && own != curve) return fail("the EC key names two curves");
		curve = own;
	}
	if (!curve) return fail("the EC private key doesn't say its curve");
	k->curve = curve;
	if (d.n > curve_bytes(curve)) return fail("the EC private key is longer than its curve's");
	k->priv = d;
	if (!ec_scalar_check(k)) return false;
	if (seq.n && seq.p[0] == 0xa1) {
		if (!der_get(&seq, 0xa1, &tagged) || !der_get(&tagged, 0x03, &bits) || bits.n < 1 || bits.p[0] != 0)
			return fail("it isn't an EC private key (SEC 1's)");
		k->point = (span){bits.p + 1, bits.n - 1};
	} else {
		// No public point in it: computed from the scalar.
		uint8_t x[66] = {0};
		size_t c = curve_bytes(curve);
		memcpy(x + c - d.n, d.p, d.n);
		br_ec_private_key sk = {curve, x, c};
		br_ec_public_key pk;
		size_t made = br_ec_compute_pub(&br_ec_prime_i31, &pk, k->made, &sk);
		ak_wipe(x, sizeof x);
		if (!made) return fail("the EC private key isn't one of its curve's");
		k->point = (span){k->made, pk.qlen};
	}
	return ec_point_check(k);
}

// pkcs8 reads PKCS#8's PrivateKeyInfo (or OneAsymmetricKey, RFC 5958).
static bool pkcs8(span s, key *k) {
	span seq, alg, oid, priv;
	if (!der_get(&s, 0x30, &seq) || s.n || !der_get(&seq, 0x02, NULL) || !der_get(&seq, 0x30, &alg) || !der_get(&alg, 0x06, &oid) ||
	    !der_get(&seq, 0x04, &priv))
		return fail("it isn't a private key (PKCS#8's)");
	if (span_is(oid, OID_RSA, sizeof OID_RSA) || span_is(oid, OID_RSA_PSS, sizeof OID_RSA_PSS)) return rsa_private(priv, k);
	if (span_is(oid, OID_EC, sizeof OID_EC)) {
		span curve;
		int c = der_get(&alg, 0x06, &curve) ? curve_of(curve) : 0;
		if (!c) return fail("the EC key's curve isn't P-256, P-384 or P-521");
		return ec_private(priv, c, k);
	}
	if (span_is(oid, OID_ED25519, sizeof OID_ED25519)) {
		span seed;
		if (!der_get(&priv, 0x04, &seed) || seed.n != 32) return fail("an Ed25519 private key is a 32-byte seed");
		k->priv = seed;
		ak_ed25519_public(seed.p, k->made);
		k->point = (span){k->made, 32};
		k->kind = KIND_ED25519;
		return true;
	}
	return fail("the key isn't RSA, ECDSA (P-256, P-384, P-521) or Ed25519");
}

// ---- DER, written ----

typedef struct {
	uint8_t *p;
	size_t n, cap;
} buf;

static void put(buf *b, const void *p, size_t n) {
	if (b->n + n > b->cap) {
		b->cap = (b->n + n) * 2 + 64;
		b->p = ak_realloc(b->p, b->cap);
	}
	if (n) memcpy(b->p + b->n, p, n);
	b->n += n;
}

static void put_header(buf *b, uint8_t tag, size_t n) {
	uint8_t h[6] = {tag};
	size_t k = 1;
	if (n < 0x80) h[k++] = (uint8_t)n;
	else if (n < 0x100) h[k++] = 0x81, h[k++] = (uint8_t)n;
	else if (n < 0x10000) h[k++] = 0x82, h[k++] = (uint8_t)(n >> 8), h[k++] = (uint8_t)n;
	else h[k++] = 0x83, h[k++] = (uint8_t)(n >> 16), h[k++] = (uint8_t)(n >> 8), h[k++] = (uint8_t)n;
	put(b, h, k);
}

static void put_tlv(buf *b, uint8_t tag, const void *p, size_t n) {
	put_header(b, tag, n);
	put(b, p, n);
}

// wrap puts inner as the contents of an element of tag, and frees it.
static void wrap(buf *b, uint8_t tag, buf *inner) {
	put_tlv(b, tag, inner->p, inner->n);
	ak_free(inner->p);
	*inner = (buf){0};
}

// put_uint puts an unsigned big-endian integer as an INTEGER.
static void put_uint(buf *b, span v) {
	while (v.n && v.p[0] == 0) v.p++, v.n--;
	if (!v.n) {
		put_tlv(b, 0x02, (const uint8_t[]){0}, 1);
		return;
	}
	bool pad = v.p[0] & 0x80;
	put_header(b, 0x02, v.n + pad);
	if (pad) put(b, (const uint8_t[]){0}, 1);
	put(b, v.p, v.n);
}

// put_alg puts an AlgorithmIdentifier: oid, then param (an OID), NULL
// (RSA's) or nothing.
static void put_alg(buf *b, const uint8_t *oid, size_t on, const uint8_t *param, size_t pn, bool null) {
	buf a = {0};
	put_tlv(&a, 0x06, oid, on);
	if (param) put_tlv(&a, 0x06, param, pn);
	if (null) put(&a, (const uint8_t[]){0x05, 0x00}, 2);
	wrap(b, 0x30, &a);
}

static const uint8_t *curve_oid(int curve, size_t *n) {
	if (curve == BR_EC_secp256r1) return *n = sizeof OID_P256, OID_P256;
	if (curve == BR_EC_secp384r1) return *n = sizeof OID_P384, OID_P384;
	return *n = sizeof OID_P521, OID_P521;
}

static ak_bytes *finish(buf *b) {
	ak_bytes *out = ak_bytes_new(b->p, b->n);
	ak_wipe(b->p, b->n);
	ak_free(b->p);
	return out;
}

// public_der: k's public key as SubjectPublicKeyInfo.
static ak_bytes *public_der(const key *k) {
	buf b = {0}, bits = {0};
	put(&bits, (const uint8_t[]){0}, 1);
	if (k->kind == KIND_RSA) {
		put_alg(&b, OID_RSA, sizeof OID_RSA, NULL, 0, true);
		buf seq = {0};
		put_uint(&seq, k->n);
		put_uint(&seq, k->e);
		wrap(&bits, 0x30, &seq);
	} else if (k->kind == KIND_ED25519) {
		put_alg(&b, OID_ED25519, sizeof OID_ED25519, NULL, 0, false);
		put(&bits, k->point.p, k->point.n);
	} else {
		size_t cn;
		const uint8_t *c = curve_oid(k->curve, &cn);
		put_alg(&b, OID_EC, sizeof OID_EC, c, cn, false);
		put(&bits, k->point.p, k->point.n);
	}
	wrap(&b, 0x03, &bits);
	buf out = {0};
	wrap(&out, 0x30, &b);
	return finish(&out);
}

// private_der: k as PKCS#8.
static ak_bytes *private_der(const key *k) {
	buf b = {0}, priv = {0};
	put_tlv(&b, 0x02, (const uint8_t[]){0}, 1);
	if (k->kind == KIND_RSA) {
		put_alg(&b, OID_RSA, sizeof OID_RSA, NULL, 0, true);
		buf seq = {0};
		put_tlv(&seq, 0x02, (const uint8_t[]){0}, 1);
		const span *ps[] = {&k->n, &k->e, &k->d, &k->p, &k->q, &k->dp, &k->dq, &k->qi};
		for (size_t i = 0; i < AK_LEN(ps); i++) put_uint(&seq, *ps[i]);
		wrap(&priv, 0x30, &seq);
	} else if (k->kind == KIND_ED25519) {
		put_alg(&b, OID_ED25519, sizeof OID_ED25519, NULL, 0, false);
		put_tlv(&priv, 0x04, k->priv.p, k->priv.n);
	} else {
		size_t cn, c = curve_bytes(k->curve);
		const uint8_t *oid = curve_oid(k->curve, &cn);
		put_alg(&b, OID_EC, sizeof OID_EC, oid, cn, false);
		buf seq = {0}, tagged = {0}, bits = {0};
		uint8_t x[66] = {0};
		memcpy(x + c - k->priv.n, k->priv.p, k->priv.n);
		put_tlv(&seq, 0x02, (const uint8_t[]){1}, 1);
		put_tlv(&seq, 0x04, x, c);
		ak_wipe(x, sizeof x);
		put(&bits, (const uint8_t[]){0}, 1);
		put(&bits, k->point.p, k->point.n);
		wrap(&tagged, 0x03, &bits);
		wrap(&seq, 0xa1, &tagged);
		wrap(&priv, 0x30, &seq);
	}
	wrap(&b, 0x04, &priv);
	buf out = {0};
	wrap(&out, 0x30, &b);
	return finish(&out);
}

// ---- PEM (RFC 7468) ----

static bool starts(const uint8_t *p, size_t n, const char *s) {
	size_t k = strlen(s);
	return n >= k && memcmp(p, s, k) == 0;
}

static int b64_value(uint8_t c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

// pem reads the first of text's blocks whose label is one of labels
// (NULL-terminated): its DER into *der (ak_free it), and its label's
// index; -1 (failure says why) when there's none, or it's encrypted.
static int pem(const uint8_t *p, size_t n, const char *const *labels, buf *der) {
	const uint8_t *end = p + n;
	for (;;) {
		const uint8_t *at = NULL;
		for (const uint8_t *q = p; q + 11 <= end; q++)
			if (starts(q, (size_t)(end - q), "-----BEGIN ")) {
				at = q;
				break;
			}
		if (!at) break;
		const uint8_t *label = at + 11, *le = label;
		while (le < end && *le != '-' && *le != '\n') le++;
		if (!starts(le, (size_t)(end - le), "-----")) return fail("the PEM's BEGIN line isn't whole"), -1;
		size_t ln = (size_t)(le - label);
		const uint8_t *body = le + 5;
		int found = -1;
		for (int i = 0; labels[i]; i++)
			if (strlen(labels[i]) == ln && memcmp(labels[i], label, ln) == 0) found = i;
		// Its END line.
		const uint8_t *stop = NULL;
		for (const uint8_t *q = body; q + 9 <= end; q++)
			if (starts(q, (size_t)(end - q), "-----END ")) {
				stop = q;
				break;
			}
		if (!stop) return fail("the PEM has no END line"), -1;
		if (found < 0) {
			if (ln == 21 && memcmp(label, "ENCRYPTED PRIVATE KEY", 21) == 0)
				return fail("the private key is encrypted: decrypt it first (openssl pkcs8 -in key.pem -out plain.pem)"), -1;
			if (ln == 19 && memcmp(label, "OPENSSH PRIVATE KEY", 19) == 0)
				return fail("it's an OpenSSH key, which isn't read: write it as PKCS#8 (ssh-keygen -p -m PKCS8 -f key)"), -1;
			p = stop + 9;
			continue;
		}
		// Headers (Proc-Type: 4,ENCRYPTED): an encrypted key.
		if (memchr(body, ':', (size_t)(stop - body))) return fail("the key is encrypted (its PEM has headers): decrypt it first"), -1;
		uint32_t acc = 0;
		int bits = 0;
		bool padded = false;
		for (const uint8_t *q = body; q < stop; q++) {
			uint8_t c = *q;
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
			if (c == '=') {
				padded = true;
				continue;
			}
			int v = b64_value(c);
			if (v < 0 || padded) {
				ak_free(der->p);
				*der = (buf){0};
				return fail("the PEM's Base64 isn't Base64"), -1;
			}
			acc = acc << 6 | (uint32_t)v;
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				uint8_t byte = (uint8_t)(acc >> bits);
				put(der, &byte, 1);
			}
		}
		return found;
	}
	return -1;
}

static bool is_pem(const uint8_t *p, size_t n) {
	while (n && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++, n--;
	return starts(p, n, "-----BEGIN ");
}

static void release_buf(buf *b) {
	if (b->p) ak_wipe(b->p, b->n);
	ak_free(b->p);
	*b = (buf){0};
}

// ---- Builtins: keys ----

// readPublic(data, pem): the key in data (PEM, or DER: SubjectPublicKeyInfo,
// PKCS#1's RSAPublicKey or a certificate) as SubjectPublicKeyInfo; none,
// with failure saying why.
static ak_val read_public(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *in = BYTES(0);
	key k = {0};
	buf der = {0};
	bool ok;
	if (args[1].i) {
		static const char *const labels[] = {"PUBLIC KEY", "RSA PUBLIC KEY", "CERTIFICATE", NULL};
		if (!is_pem(data(in), len(in))) return fail("it isn't PEM (-----BEGIN PUBLIC KEY-----)"), (ak_val){.p = NULL};
		failure[0] = 0;
		int which = pem(data(in), len(in), labels, &der);
		if (which < 0) {
			if (!failure[0]) fail("the PEM has no PUBLIC KEY, RSA PUBLIC KEY or CERTIFICATE in it");
			return (ak_val){.p = NULL};
		}
		span s = {der.p, der.n};
		ok = which == 0 ? spki(s, &k) : which == 1 ? rsa_public(s, &k) : certificate(s, &k);
	} else {
		// Each form in turn; none: the first's reason.
		span s = {data(in), len(in)};
		ok = spki(s, &k) || (k = (key){0}, rsa_public(s, &k)) || (k = (key){0}, certificate(s, &k));
		if (!ok) k = (key){0}, spki(s, &k);
	}
	ak_val out = {.p = ok ? public_der(&k) : NULL};
	release_buf(&der);
	return out;
}

// readPrivate(data, pem): the key in data (PEM, or DER: PKCS#8, PKCS#1's
// RSAPrivateKey, SEC 1's ECPrivateKey) as PKCS#8; none, with failure.
static ak_val read_private(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *in = BYTES(0);
	key k = {0};
	buf der = {0};
	bool ok;
	if (args[1].i) {
		static const char *const labels[] = {"PRIVATE KEY", "RSA PRIVATE KEY", "EC PRIVATE KEY", NULL};
		if (!is_pem(data(in), len(in))) return fail("it isn't PEM (-----BEGIN PRIVATE KEY-----)"), (ak_val){.p = NULL};
		failure[0] = 0;
		int which = pem(data(in), len(in), labels, &der);
		if (which < 0) {
			if (!failure[0]) fail("the PEM has no PRIVATE KEY, RSA PRIVATE KEY or EC PRIVATE KEY in it");
			release_buf(&der);
			return (ak_val){.p = NULL};
		}
		span s = {der.p, der.n};
		ok = which == 0 ? pkcs8(s, &k) : which == 1 ? rsa_private(s, &k) : ec_private(s, 0, &k);
	} else {
		span s = {data(in), len(in)};
		ok = pkcs8(s, &k) || (k = (key){0}, rsa_private(s, &k)) || (k = (key){0}, ec_private(s, 0, &k));
		if (!ok) k = (key){0}, pkcs8(s, &k);
	}
	ak_val out = {.p = ok ? private_der(&k) : NULL};
	ak_wipe(&k, sizeof k);
	release_buf(&der);
	return out;
}

// read_key reads a key the Asuka side holds (what readPublic or
// readPrivate made, or a struct someone wrote by hand).
static bool read_key(const ak_bytes *der, bool private, key *k) {
	span s = {data(der), len(der)};
	return private ? pkcs8(s, k) : spki(s, k);
}

// keyInfo(der, private): [kind, bits], or none (failure).
static ak_val info(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	key k = {0};
	if (!read_key(BYTES(0), args[1].i, &k)) return (ak_val){.p = NULL};
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, 2);
	ak_arr_push(&a, (ak_val){.i = k.kind});
	ak_arr_push(&a, (ak_val){.i = (int64_t)key_bits(&k)});
	ak_wipe(&k, sizeof k);
	return (ak_val){.p = a};
}

// publicOf(pkcs8): the private key's public key.
static ak_val public_of(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	key k = {0};
	if (!read_key(BYTES(0), true, &k)) return (ak_val){.p = NULL};
	ak_val out = {.p = public_der(&k)};
	ak_wipe(&k, sizeof k);
	return out;
}

static void push_bytes(ak_arr **a, span s) { ak_arr_push(a, (ak_val){.p = ak_bytes_new(s.p, s.n)}); }

// keyParts(der, private): a JWK's parts, unsigned big-endian: RSA's n, e
// (then d, p, q, dp, dq, qi); ECDSA's x, y (then d); Ed25519's x (then d).
static ak_val parts(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	bool private = args[1].i;
	key k = {0};
	if (!read_key(BYTES(0), private, &k)) return (ak_val){.p = NULL};
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 8);
	if (k.kind == KIND_RSA) {
		const span *ps[] = {&k.n, &k.e, &k.d, &k.p, &k.q, &k.dp, &k.dq, &k.qi};
		for (size_t i = 0; i < (private ? AK_LEN(ps) : 2); i++) push_bytes(&a, *ps[i]);
	} else if (k.kind == KIND_ED25519) {
		push_bytes(&a, k.point);
		if (private) push_bytes(&a, k.priv);
	} else {
		size_t c = curve_bytes(k.curve);
		push_bytes(&a, (span){k.point.p + 1, c});
		push_bytes(&a, (span){k.point.p + 1 + c, c});
		if (private) {
			uint8_t x[66] = {0};
			memcpy(x + c - k.priv.n, k.priv.p, k.priv.n);
			push_bytes(&a, (span){x, c});
			ak_wipe(x, sizeof x);
		}
	}
	ak_wipe(&k, sizeof k);
	return (ak_val){.p = a};
}

static span part(const ak_arr *a, uint32_t i) {
	const ak_bytes *b = a && i < a->len ? a->data[i].p : NULL;
	return (span){data(b), len(b)};
}

// keyFromParts(kind, parts, private): a JWK's key, from its parts in the
// order keyParts gives them, as SubjectPublicKeyInfo or PKCS#8.
static ak_val from_parts(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t kind = args[0].i;
	const ak_arr *a = args[1].p;
	bool private = args[2].i;
	uint32_t count = a ? a->len : 0;
	key k = {0};
	uint8_t point[133];
	bool ok = false;
	if (kind == KIND_RSA) {
		if (count != (private ? 8u : 2u)) return fail("an RSA JWK has n and e (and d, p, q, dp, dq and qi)"), (ak_val){.p = NULL};
		span *ps[] = {&k.n, &k.e, &k.d, &k.p, &k.q, &k.dp, &k.dq, &k.qi};
		for (uint32_t i = 0; i < count; i++) {
			*ps[i] = part(a, i);
			while (ps[i]->n && ps[i]->p[0] == 0) ps[i]->p++, ps[i]->n--;
		}
		ok = rsa_check(&k) && (!private || (k.d.n && k.p.n && k.q.n && k.dp.n && k.dq.n && k.qi.n) ||
					  fail("an RSA JWK's private parts are zero"));
	} else if (kind >= KIND_P256 && kind <= KIND_P521) {
		k.curve = kind_curve((int)kind);
		size_t c = curve_bytes(k.curve);
		if (count != (private ? 3u : 2u) || part(a, 0).n != c || part(a, 1).n != c || (private && part(a, 2).n != c))
			return fail("an EC JWK's x and y (and d) are as long as its curve's coordinates"), (ak_val){.p = NULL};
		point[0] = 4;
		memcpy(point + 1, part(a, 0).p, c);
		memcpy(point + 1 + c, part(a, 1).p, c);
		k.point = (span){point, 1 + 2 * c};
		ok = ec_point_check(&k);
		if (ok && private) {
			k.priv = part(a, 2);
			ok = ec_scalar_check(&k);
			if (ok) {
				// The point must be the scalar's.
				br_ec_private_key sk = {k.curve, (unsigned char *)k.priv.p, k.priv.n};
				br_ec_public_key pk;
				ok = br_ec_compute_pub(&br_ec_prime_i31, &pk, k.made, &sk) && pk.qlen == k.point.n &&
				     memcmp(k.made, k.point.p, pk.qlen) == 0;
				if (!ok) fail("the EC JWK's d isn't the private key of its x and y");
			}
		}
	} else if (kind == KIND_ED25519) {
		if (count != (private ? 2u : 1u) || part(a, 0).n != 32 || (private && part(a, 1).n != 32))
			return fail("an Ed25519 JWK's x (and d) are 32 bytes"), (ak_val){.p = NULL};
		k.kind = KIND_ED25519;
		k.point = part(a, 0);
		ok = true;
		if (private) {
			k.priv = part(a, 1);
			uint8_t pub[32];
			ak_ed25519_public(k.priv.p, pub);
			ok = memcmp(pub, k.point.p, 32) == 0 || fail("the Ed25519 JWK's d isn't the private key of its x");
		}
	} else {
		fail("no such kind of key");
	}
	ak_val out = {.p = ok ? (private ? private_der(&k) : public_der(&k)) : NULL};
	ak_wipe(&k, sizeof k);
	return out;
}

// A PRNG over the system's randomness, for BearSSL's key generation.
static void rng_init(const br_prng_class **ctx, const void *params, const void *seed, size_t n) {
	AK_UNUSED(ctx), AK_UNUSED(params), AK_UNUSED(seed), AK_UNUSED(n);
}
static void rng_generate(const br_prng_class **ctx, void *out, size_t n) {
	AK_UNUSED(ctx);
	if (!ak_os_random(out, n)) ak_panic("crypto: the system has no randomness to give");
}
static void rng_update(const br_prng_class **ctx, const void *seed, size_t n) { AK_UNUSED(ctx), AK_UNUSED(seed), AK_UNUSED(n); }
static const br_prng_class rng_class = {sizeof(const br_prng_class *), rng_init, rng_generate, rng_update};

// generateKey(kind): a new private key (ECDSA's, Ed25519's) from the system's
// randomness, as PKCS#8; none for RSA (failure).
static ak_val new_key(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t kind = args[0].i;
	key k = {0};
	uint8_t x[BR_EC_KBUF_PRIV_MAX_SIZE], seed[32];
	if (kind >= KIND_P256 && kind <= KIND_P521) {
		const br_prng_class *rng = &rng_class;
		br_ec_private_key sk;
		br_ec_public_key pk;
		k.curve = kind_curve((int)kind);
		if (!br_ec_keygen(&rng, &br_ec_prime_i31, &sk, x, k.curve) || !br_ec_compute_pub(&br_ec_prime_i31, &pk, k.made, &sk))
			return fail("the key couldn't be made"), (ak_val){.p = NULL};
		k.kind = (int)kind;
		k.priv = (span){sk.x, sk.xlen};
		k.point = (span){k.made, pk.qlen};
	} else if (kind == KIND_ED25519) {
		if (!ak_os_random(seed, sizeof seed)) ak_panic("crypto: the system has no randomness to give");
		k.kind = KIND_ED25519;
		k.priv = (span){seed, 32};
		ak_ed25519_public(seed, k.made);
		k.point = (span){k.made, 32};
	} else {
		return fail("RSA keys aren't made here (it takes long, and ECDSA's or Ed25519's are smaller and quicker): make one with openssl genpkey -algorithm RSA"),
		       (ak_val){.p = NULL};
	}
	ak_val out = {.p = private_der(&k)};
	ak_wipe(x, sizeof x);
	ak_wipe(seed, sizeof seed);
	ak_wipe(&k, sizeof k);
	return out;
}

// pemOf(der, label): der as PEM, its Base64 in lines of 64.
static ak_val pem_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const ak_bytes *der = BYTES(0);
	const ak_str *label = args[1].p;
	buf b = {0};
	put(&b, "-----BEGIN ", 11);
	put(&b, label->data, label->len);
	put(&b, "-----\n", 6);
	const uint8_t *p = data(der);
	size_t dn = len(der), col = 0;
	for (size_t i = 0; i < dn; i += 3) {
		uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < dn ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < dn ? p[i + 2] : 0);
		char q[4] = {A[v >> 18 & 63], A[v >> 12 & 63], i + 1 < dn ? A[v >> 6 & 63] : '=', i + 2 < dn ? A[v & 63] : '='};
		put(&b, q, 4);
		if ((col += 4) == 64 && i + 3 < dn) put(&b, "\n", 1), col = 0;
	}
	put(&b, "\n-----END ", 10);
	put(&b, label->data, label->len);
	put(&b, "-----\n", 6);
	ak_str *s = ak_str_new((const char *)b.p, b.n);
	release_buf(&b);
	return (ak_val){.p = s};
}

// ---- Builtins: signatures ----

// EMSA-PKCS1-v1_5's DigestInfo before each hash (RFC 8017, 9.2).
static const uint8_t *digest_info(int hash, size_t *n) {
	static const uint8_t SHA1[] = {0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14};
	static const uint8_t SHA256[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
	static const uint8_t SHA384[] = {0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
	static const uint8_t SHA512[] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};
	switch (hash) {
	case AK_HASH_SHA1: return *n = sizeof SHA1, SHA1;
	case AK_HASH_SHA256: return *n = sizeof SHA256, SHA256;
	case AK_HASH_SHA384: return *n = sizeof SHA384, SHA384;
	}
	return *n = sizeof SHA512, SHA512;
}

// pkcs1_em writes EMSA-PKCS1-v1_5's encoding of digest into em (k bytes);
// false when k is too short for it.
static bool pkcs1_em(int hash, const uint8_t *digest, uint8_t *em, size_t k) {
	size_t dn, hn = ak_hash_size(hash);
	const uint8_t *di = digest_info(hash, &dn);
	if (k < dn + hn + 11) return false;
	em[0] = 0, em[1] = 1;
	memset(em + 2, 0xff, k - dn - hn - 3);
	em[k - dn - hn - 1] = 0;
	memcpy(em + k - dn - hn, di, dn);
	memcpy(em + k - hn, digest, hn);
	return true;
}

// mgf1 xors MGF1(seed) (RFC 8017, B.2.1) into out[0:n].
static void mgf1(int hash, const uint8_t *seed, size_t sn, uint8_t *out, size_t n) {
	uint8_t in[68], h[64];
	size_t hn = ak_hash_size(hash);
	memcpy(in, seed, sn);
	for (uint32_t c = 0; n; c++) {
		in[sn] = (uint8_t)(c >> 24), in[sn + 1] = (uint8_t)(c >> 16), in[sn + 2] = (uint8_t)(c >> 8), in[sn + 3] = (uint8_t)c;
		ak_hash(hash, in, sn + 4, h);
		size_t m = n < hn ? n : hn;
		for (size_t i = 0; i < m; i++) out[i] ^= h[i];
		out += m, n -= m;
	}
}

// pss_hash: H = Hash(8 zeros ‖ digest ‖ salt).
static void pss_hash(int hash, const uint8_t *digest, const uint8_t *salt, size_t sn, uint8_t *h) {
	size_t hn = ak_hash_size(hash);
	uint8_t m[8 + 64 + 512] = {0};
	memcpy(m + 8, digest, hn);
	memcpy(m + 8 + hn, salt, sn);
	ak_hash(hash, m, 8 + hn + sn, h);
}

// pss_verify: whether x (k bytes, the signature's public operation) is
// EMSA-PSS's encoding of digest for a modulus of bits, any salt length.
static bool pss_verify(int hash, const uint8_t *digest, uint8_t *x, size_t k, size_t bits) {
	size_t hn = ak_hash_size(hash), embits = bits - 1, emlen = (embits + 7) / 8;
	uint8_t *em = x + (k - emlen);
	if (k > emlen && x[0] != 0) return false;
	if (emlen < hn + 2 || em[emlen - 1] != 0xbc) return false;
	size_t dblen = emlen - hn - 1;
	uint8_t *db = em, *h = em + dblen, top = (uint8_t)(0xff >> (8 * emlen - embits));
	if (db[0] & ~top) return false;
	mgf1(hash, h, hn, db, dblen);
	db[0] &= top;
	size_t i = 0;
	while (i < dblen && db[i] == 0) i++;
	if (i == dblen || db[i] != 1) return false;
	size_t sn = dblen - i - 1;
	if (sn > 512) return false;
	uint8_t want[64];
	pss_hash(hash, digest, db + i + 1, sn, want);
	return memcmp(want, h, hn) == 0;
}

// pss_encode writes EMSA-PSS's encoding of digest (a salt as long as the
// hash, from the system's randomness) into x (k bytes).
static bool pss_encode(int hash, const uint8_t *digest, uint8_t *x, size_t k, size_t bits) {
	size_t hn = ak_hash_size(hash), embits = bits - 1, emlen = (embits + 7) / 8;
	if (emlen < 2 * hn + 2) return fail("the RSA key is too small for PSS with this hash");
	uint8_t salt[64];
	if (!ak_os_random(salt, hn)) return fail("the system gave no randomness");
	memset(x, 0, k);
	uint8_t *em = x + (k - emlen), *db = em, *h = em + emlen - hn - 1;
	size_t dblen = emlen - hn - 1;
	pss_hash(hash, digest, salt, hn, h);
	db[dblen - hn - 1] = 1;
	memcpy(db + dblen - hn, salt, hn);
	mgf1(hash, h, hn, db, dblen);
	db[0] &= (uint8_t)(0xff >> (8 * emlen - embits));
	em[emlen - 1] = 0xbc;
	return true;
}

// der_signature reads an ECDSA signature in DER (SEQUENCE { INTEGER r,
// INTEGER s }) into r ‖ s, each c bytes; false for what isn't strict DER
// (a long length where a short one fits, a zero before a byte that
// doesn't need it, a negative number, bytes after it), as Go and
// Wycheproof hold it: BER's other forms of one signature aren't it.
static bool der_signature(const uint8_t *p, size_t n, size_t c, uint8_t *out) {
	size_t len, h;
	if (n < 2 || p[0] != 0x30) return false;
	if (p[1] < 0x80) len = p[1], h = 2;
	else if (p[1] == 0x81 && n > 2 && p[2] >= 0x80) len = p[2], h = 3;
	else return false;
	if (h + len != n) return false;
	const uint8_t *q = p + h, *end = p + n;
	for (size_t i = 0; i < 2; i++) {
		if (end - q < 2 || q[0] != 0x02) return false;
		size_t l = q[1];
		if (l == 0 || l >= 0x80 || (size_t)(end - q - 2) < l) return false;
		const uint8_t *v = q + 2;
		q = v + l;
		if (v[0] & 0x80) return false;
		if (l > 1 && v[0] == 0 && !(v[1] & 0x80)) return false;
		if (v[0] == 0) v++, l--;
		if (l > c) return false;
		memset(out + i * c, 0, c - l);
		memcpy(out + i * c + c - l, v, l);
	}
	return q == end;
}

static const br_hash_class *hash_class(int hash) {
	return hash == AK_HASH_SHA256 ? &br_sha256_vtable : hash == AK_HASH_SHA384 ? &br_sha384_vtable : &br_sha512_vtable;
}

static bool scheme_fits(const key *k, int64_t scheme, int hash) {
	switch (k->kind) {
	case KIND_RSA:
		if (scheme != SCHEME_PKCS1 && scheme != SCHEME_PSS) return fail("an RSA key signs with rsaPKCS1v15 or rsaPSS");
		return true;
	case KIND_ED25519:
		if (scheme != SCHEME_ED25519) return fail("an Ed25519 key signs with ed25519");
		return true;
	}
	if (scheme != SCHEME_ECDSA && scheme != SCHEME_ECDSA_DER) return fail("an ECDSA key signs with ecdsa or ecdsaDER");
	if (hash == AK_HASH_SHA1) return fail("ECDSA with SHA-1 isn't safe and isn't done here");
	return true;
}

// verifySignature(spki, scheme, hash, message, signature): 1 when signature is
// the key's of message, 0 when it isn't, -1 when the key isn't one
// (failure says why) or doesn't sign with scheme.
static ak_val verify(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t scheme = args[1].i, hash = args[2].i;
	const ak_bytes *msg = BYTES(3), *sig = BYTES(4);
	key k = {0};
	if (!read_key(BYTES(0), false, &k)) return (ak_val){.i = -1};
	if (!ak_hash_size((int)hash) && scheme != SCHEME_ED25519) return fail("no such hash"), (ak_val){.i = -1};
	if (!scheme_fits(&k, scheme, (int)hash)) return (ak_val){.i = -1};
	if (k.kind == KIND_ED25519)
		return (ak_val){.i = len(sig) == 64 && ak_ed25519_verify(k.point.p, data(msg), len(msg), data(sig))};
	uint8_t digest[64];
	ak_hash((int)hash, data(msg), len(msg), digest);
	if (k.kind == KIND_RSA) {
		size_t kn = k.n.n, bits = bitlen(k.n);
		if (len(sig) != kn) return (ak_val){.i = 0};
		uint8_t x[RSA_MAX], em[RSA_MAX];
		memcpy(x, data(sig), kn);
		br_rsa_public_key pk = {(unsigned char *)k.n.p, k.n.n, (unsigned char *)k.e.p, k.e.n};
		if (!br_rsa_i31_public(x, kn, &pk)) return (ak_val){.i = 0};
		if (scheme == SCHEME_PSS) return (ak_val){.i = pss_verify((int)hash, digest, x, kn, bits)};
		return (ak_val){.i = pkcs1_em((int)hash, digest, em, kn) && memcmp(em, x, kn) == 0};
	}
	uint8_t raw[132];
	size_t rn = len(sig), c = curve_bytes(k.curve);
	if (scheme == SCHEME_ECDSA_DER) {
		if (!der_signature(data(sig), rn, c, raw)) return (ak_val){.i = 0};
		rn = 2 * c;
	} else {
		if (rn != 2 * c) return (ak_val){.i = 0};
		memcpy(raw, data(sig), rn);
	}
	br_ec_public_key pk = {k.curve, (unsigned char *)k.point.p, k.point.n};
	return (ak_val){.i = br_ecdsa_i31_vrfy_raw(&br_ec_prime_i31, digest, ak_hash_size((int)hash), &pk, raw, rn) == 1};
}

// signMessage(pkcs8, scheme, hash, message): the signature; none (failure says
// why) when the key isn't one or doesn't sign with scheme.
static ak_val sign(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t scheme = args[1].i, hash = args[2].i;
	const ak_bytes *msg = BYTES(3);
	key k = {0};
	ak_val none = {.p = NULL};
	if (!read_key(BYTES(0), true, &k)) return none;
	if (!ak_hash_size((int)hash) && scheme != SCHEME_ED25519) return fail("no such hash"), none;
	if (!scheme_fits(&k, scheme, (int)hash)) return none;
	ak_bytes *out = NULL;
	if (k.kind == KIND_ED25519) {
		out = ak_bytes_new(NULL, 64);
		ak_ed25519_sign(k.priv.p, k.point.p, data(msg), len(msg), out->data);
		ak_wipe(&k, sizeof k);
		return (ak_val){.p = out};
	}
	uint8_t digest[64];
	ak_hash((int)hash, data(msg), len(msg), digest);
	if (k.kind == KIND_RSA) {
		size_t kn = k.n.n, bits = bitlen(k.n);
		uint8_t x[RSA_MAX], em[RSA_MAX];
		bool ok = scheme == SCHEME_PSS ? pss_encode((int)hash, digest, em, kn, bits)
					       : pkcs1_em((int)hash, digest, em, kn) || fail("the RSA key is too small for this hash");
		if (!ok) return ak_wipe(&k, sizeof k), none;
		memcpy(x, em, kn);
		br_rsa_private_key sk = {(uint32_t)bits,       (unsigned char *)k.p.p,  k.p.n,  (unsigned char *)k.q.p,
					 k.q.n,                (unsigned char *)k.dp.p, k.dp.n, (unsigned char *)k.dq.p,
					 k.dq.n,               (unsigned char *)k.qi.p, k.qi.n};
		ok = br_rsa_i31_private(x, &sk) == 1;
		if (ok) {
			// What it made, checked with the public key: a key whose parts
			// don't agree (one written by hand) signs nothing.
			uint8_t check[RSA_MAX];
			memcpy(check, x, kn);
			br_rsa_public_key pk = {(unsigned char *)k.n.p, k.n.n, (unsigned char *)k.e.p, k.e.n};
			ok = br_rsa_i31_public(check, kn, &pk) && memcmp(check, em, kn) == 0;
		}
		if (ok) out = ak_bytes_new(x, kn);
		else fail("the RSA private key's parts don't agree");
		ak_wipe(x, sizeof x);
		ak_wipe(&k, sizeof k);
		return (ak_val){.p = out};
	}
	size_t c = curve_bytes(k.curve);
	uint8_t x[66] = {0}, sig[160];
	memcpy(x + c - k.priv.n, k.priv.p, k.priv.n);
	br_ec_private_key sk = {k.curve, x, c};
	size_t sn = br_ecdsa_i31_sign_raw(&br_ec_prime_i31, hash_class((int)hash), digest, &sk, sig);
	ak_wipe(x, sizeof x);
	ak_wipe(&k, sizeof k);
	if (!sn) return fail("ECDSA failed to sign"), none;
	if (scheme == SCHEME_ECDSA_DER) sn = br_ecdsa_raw_to_asn1(sig, sn);
	return (ak_val){.p = ak_bytes_new(sig, sn)};
}

static ak_val failure_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.p = ak_str_c(failure)};
}

const ak_lib_entry ak_std_crypto_pk[] = {
	{"crypto_pk.readPublic", read_public},
	{"crypto_pk.readPrivate", read_private},
	{"crypto_pk.keyInfo", info},
	{"crypto_pk.publicOf", public_of},
	{"crypto_pk.keyParts", parts},
	{"crypto_pk.keyFromParts", from_parts},
	{"crypto_pk.generateKey", new_key},
	{"crypto_pk.pemOf", pem_fn},
	{"crypto_pk.verifySignature", verify},
	{"crypto_pk.signMessage", sign},
	{"crypto_pk.keyFailure", failure_fn},
	{NULL, NULL},
};
