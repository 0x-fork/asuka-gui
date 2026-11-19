// tls.c: std/net's TLS: configurations, the sessions std/net's net thread
// and the HTTP servers' threads run between a connection's socket and
// its buffers (tls.h's layer), and the builtins (tls.…) that make them.
// Mbed TLS 4.1 (third_party/mbedtls) speaks TLS 1.2 and 1.3; the same C
// on every system with sockets. The web has none, and says so.
//
// A configuration is made once, on the thread that calls its builtin,
// and kept: a client's (whom it trusts, the certificate it shows, ALPN,
// the versions) or a server's (its certificates, chosen by the name the
// client asks for, ALPN, the authorities its clients' certificates are
// checked against). The same inputs give the same configuration back,
// so a program that makes one per connection doesn't grow.
//
// A client keeps the sessions its handshakes made, in its configuration,
// to resume them (a shorter handshake: no certificate sent, none to
// check, one round trip less in TLS 1.2): TLS 1.3's tickets (PSK, with
// (EC)DHE still), each used once, and TLS 1.2's session (a ticket, RFC
// 5077, or the server's session ID). They're kept by the server's name
// and port, for as many servers as the configuration says (the least
// recently used go first), each until its ticket's lifetime ends, its
// certificate expires, or a time past the full handshake that checked
// it (TLS 1.3's 7 days, RFC 8446's bound; TLS 1.2's a day, as a 1.2
// session pins the version). A configuration's sessions were checked as
// it checks (its authorities, the name), and a resumed one is checked
// again for its name and its dates. A server issues tickets (Mbed TLS's
// ticket module: AES-256-GCM, its keys random, in memory only, made new
// each day, the day's before still read) unless told not to. No early
// data (0-RTT): what's sent in it can be replayed by anyone who saw it,
// and neither side here has a way to say a request is safe to repeat.
//
// A client checks the server's certificate once the handshake is done,
// before a byte of the app's goes or comes: against the authorities the
// configuration gives (Mbed TLS checks the chain, the name, the dates),
// or by default against the system's: Apple's and Windows' own checks
// (SecTrust, CertGetCertificateChain), which may fetch over the network
// (intermediates, revocation), on a thread of their own; elsewhere the
// system's authorities (tlstrust.c: Linux's bundle, Android's store) and
// Mbed TLS's check. A failure says why: expired, the wrong host, an
// unknown issuer...
//
//	tls.clientConfig(ca, certificate, key, alpn, minVersion, maxVersion, insecure, sessionCache) throws -> int
//	tls.serverConfig(pairs, alpn, clientCA, requireClient, minVersion, maxVersion, tickets) throws -> int
//	                                         pairs: a certificate chain, its key, ... (PEM)
//	tls.upgrade(h, config, serverName, server, timeoutNs) -> Task<[bytes]>
//	tls.awaitHandshake(h, timeoutNs) -> Task<[bytes]>  what it agreed: version ("12", "13"),
//	                                         cipher suite, ALPN protocol, server name,
//	                                         resumed ("1", "0"), then the other side's
//	                                         certificates (DER)
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif

#if !defined(__wasm__)
#include "std/sock.h"  // before windows.h (base/thread.h's)
#endif
#include "std/tls.h"
#include "std/std.h"
#include "rt/sched.h"
#include "base/thread.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STR(i) ((ak_str *)args[i].p)

#if defined(__wasm__)

static ak_val unsupported_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	ak_throw_msg("std/net:unsupported:");
	return (ak_val){0};
}

static ak_val unsupported_task(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	ak_throw_msg("std/net:unsupported:");
	ak_future *f = ak_future_new(AK_K_REF);
	ak_future_reject(f, ak_error_take());
	return (ak_val){.p = f};
}

const ak_lib_entry ak_std_tls[] = {
	{"tls.clientConfig", unsupported_fn},
	{"tls.serverConfig", unsupported_fn},
	{"tls.upgrade", unsupported_task},
	{"tls.awaitHandshake", unsupported_task},
	{NULL, NULL},
};

#else

// Mbed TLS's own header for its handshake's state (whether a session
// resumed: handshake_step's), which takes its private fields as fields.
#include "std/mbedtls/names.h"
#include "../../third_party/mbedtls/src/ssl_misc.h"
#include "std/mbedtls/mbedtls.h"
#include "mbedtls/oid.h"
#include "mbedtls/ssl_ticket.h"
#include "std/netint.h"

static void throw_invalid(const char *why) { ak_throw_msg("std/net:invalid:%s", why); }

// ---- Mbed TLS's locks and randomness: the core's ----

static int mu_init(mbedtls_platform_mutex_t *m) {
	ak_mutex *p = ak_alloc(sizeof *p);
	ak_mutex_init(p);
	m->p = p;
	return 0;
}
static void mu_destroy(mbedtls_platform_mutex_t *m) {
	if (!m->p) return;
	ak_mutex_destroy(m->p);
	ak_free(m->p);
	m->p = NULL;
}
static int mu_lock(mbedtls_platform_mutex_t *m) {
	ak_mutex_lock(m->p);
	return 0;
}
static int mu_unlock(mbedtls_platform_mutex_t *m) {
	ak_mutex_unlock(m->p);
	return 0;
}
static int cv_init(mbedtls_platform_condition_variable_t *c) {
	ak_cond *p = ak_alloc(sizeof *p);
	ak_cond_init(p);
	c->p = p;
	return 0;
}
static void cv_destroy(mbedtls_platform_condition_variable_t *c) {
	if (!c->p) return;
	ak_cond_destroy(c->p);
	ak_free(c->p);
	c->p = NULL;
}
static int cv_signal(mbedtls_platform_condition_variable_t *c) {
	ak_cond_signal(c->p);
	return 0;
}
static int cv_broadcast(mbedtls_platform_condition_variable_t *c) {
	ak_cond_broadcast(c->p);
	return 0;
}
static int cv_wait(mbedtls_platform_condition_variable_t *c, mbedtls_platform_mutex_t *m) {
	ak_cond_wait(c->p, m->p);
	return 0;
}

// PSA's random generator is the system's (MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG).
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *ctx, uint8_t *output, size_t output_size, size_t *output_length) {
	AK_UNUSED(ctx);
	if (!ak_os_random(output, output_size)) return PSA_ERROR_INSUFFICIENT_ENTROPY;
	*output_length = output_size;
	return PSA_SUCCESS;
}

// ---- Configurations ----
//
// A configuration's inputs are hashed first: the same ones give the one
// made before, without reading its PEM again.

typedef struct {
	mbedtls_x509_crt chain;
	mbedtls_pk_context key;
} keypair;

// How long a server's tickets are good for, and how often its ticket keys
// are made new (Mbed TLS's ticket module keeps the one before too).
#define TICKET_LIFETIME (24 * 3600)

// A session a client keeps, to resume: until expires (clock_s's), and
// the server's certificates as its full handshake had them (Mbed TLS's
// copy of a session keeps the first alone), each a 4-byte length and its
// DER.
typedef struct {
	mbedtls_ssl_session s;
	int64_t expires;
	uint8_t *chain;
	size_t chain_len;
} kept;

// A server's sessions (cache_key's: its name and port): TLS 1.2's one,
// used again and again, or TLS 1.3's tickets, each once, the newest
// last; born is when the full handshake they come of checked the
// server's certificate.
typedef struct {
	char *key;
	int64_t born, used;
	kept *tls12;
	kept *tls13[2];
	int n13;
} cached;

typedef struct tls_conf {
	int64_t id;
	uint8_t digest[32];  // of what made it
	bool server;
	bool insecure;  // a client's that checks no certificate
	bool system;    // a client's that trusts the system's authorities
	bool require;   // a server's: a client's certificate required
	mbedtls_ssl_config ssl;
	mbedtls_x509_crt ca;  // a client's authorities, or a server's for its clients
	bool has_ca;
	keypair *pairs;
	uint32_t npairs;
	char **alpn;  // NULL-terminated
	// A client's sessions, of cache_size servers at most (0: none kept).
	int64_t cache_size;
	ak_mutex cache_mu;
	AK_VEC(cached *) cache;
	int64_t ticks;
	// A server's tickets.
	bool tickets;
	mbedtls_ssl_ticket_context ticket;
} tls_conf;

static ak_mutex confs_mu = AK_MUTEX_INIT;
static AK_VEC(tls_conf *) confs;

static tls_conf *find_conf(int64_t id) {
	ak_mutex_lock(&confs_mu);
	tls_conf *c = id > 0 && (uint64_t)id <= confs.len ? confs.data[id - 1] : NULL;
	ak_mutex_unlock(&confs_mu);
	return c;
}

static const ak_tls_ops layer;
static ak_mutex init_mu = AK_MUTEX_INIT;
static int inited;  // 0 not yet, 1 ready, -1 failed

// ready readies Mbed TLS once: its locks, PSA's crypto, the layer.
static bool ready(void) {
	ak_mutex_lock(&init_mu);
	if (!inited) {
		mbedtls_threading_set_alt(mu_init, mu_destroy, mu_lock, mu_unlock, cv_init, cv_destroy, cv_signal, cv_broadcast, cv_wait);
		inited = psa_crypto_init() == PSA_SUCCESS ? 1 : -1;
		if (inited == 1) {
			ak_tls_layer = &layer;
			ak_net_tls = &ak_net_tls_impl;  // std/net's sockets' TLS (nettls.c)
		}
	}
	bool ok = inited == 1;
	ak_mutex_unlock(&init_mu);
	if (!ok) ak_throw_msg("std/net:failed:TLS couldn't start (no randomness from the system)");
	return ok;
}

// pem reads s's PEM certificates into c: false, with why, when there are
// none or one can't be read.
static bool pem_certs(const ak_str *s, mbedtls_x509_crt *c, const char *what, char *why, size_t cap) {
	if (!strstr(s->data, "-----BEGIN CERTIFICATE-----")) {
		snprintf(why, cap, "%.48s has no PEM certificate (-----BEGIN CERTIFICATE-----)", what);
		return false;
	}
	int r = mbedtls_x509_crt_parse(c, (const unsigned char *)s->data, (size_t)s->len + 1);
	if (r != 0) {
		snprintf(why, cap, "%.48s has a certificate that can't be read (Mbed TLS error -0x%04X)", what, (unsigned)(r < 0 ? -r : r));
		return false;
	}
	return true;
}

// pem_pair reads a certificate chain and its key (PEM: PKCS#8, PKCS#1 or
// SEC 1, Mbed TLS's reading); which (others[i]'s: "others[0]: "; "" for
// the first) says whose in why. An encrypted key, or OpenSSH's, is read
// by std/crypto (decryptPrivateKey, privateKey), whose key's pem() this
// reads: its ciphers and key derivations aren't every TLS app's.
static bool pem_pair(const ak_str *cert, const ak_str *key, keypair *p, const char *which, char *why, size_t cap) {
	char what[48];
	snprintf(what, sizeof what, "%sthe certificate", which);
	if (!pem_certs(cert, &p->chain, what, why, cap)) return false;
	if (!strstr(key->data, "-----BEGIN")) {
		snprintf(why, cap, "%.24sthe key isn't PEM (-----BEGIN PRIVATE KEY-----)", which);
		return false;
	}
	if (strstr(key->data, "-----BEGIN OPENSSH PRIVATE KEY-----")) {
		snprintf(why, cap, "%.24sthe key is OpenSSH's: give it as PKCS#8, what crypto.privateKey(key).pem() makes of it (crypto.decryptPrivateKey's for an encrypted one)", which);
		return false;
	}
	int r = mbedtls_pk_parse_key(&p->key, (const unsigned char *)key->data, (size_t)key->len + 1, NULL, 0);
	if (r != 0 && (r == MBEDTLS_ERR_PK_PASSWORD_REQUIRED || r == MBEDTLS_ERR_PK_PASSWORD_MISMATCH || strstr(key->data, "-----BEGIN ENCRYPTED PRIVATE KEY-----") ||
			  strstr(key->data, "Proc-Type: 4,ENCRYPTED"))) {
		snprintf(why, cap, "%.24sthe key is encrypted: give it decrypted, what crypto.decryptPrivateKey(key, password:) reads, as its pem()", which);
		return false;
	}
	if (r != 0) {
		snprintf(why, cap, "%.24sthe key can't be read: RSA or ECDSA (P-256, P-384, P-521), PKCS#8, PKCS#1 or SEC 1 (Mbed TLS error -0x%04X)", which, (unsigned)-r);
		return false;
	}
	if (mbedtls_pk_check_pair(&p->chain.pk, &p->key) != 0) {
		snprintf(why, cap, "%.24sthe key isn't the certificate's (its first, the leaf's)", which);
		return false;
	}
	return true;
}

static int version_of(int64_t v) { return v >= 13 ? MBEDTLS_SSL_VERSION_TLS1_3 : MBEDTLS_SSL_VERSION_TLS1_2; }

static void digest_str(ak_sha256_ctx *h, const char *s, size_t n) {
	uint8_t len[8];
	for (int i = 0; i < 8; i++) len[i] = (uint8_t)((uint64_t)n >> (8 * i));
	ak_sha256_update(h, len, 8);
	ak_sha256_update(h, s, n);
}

static void digest_int(ak_sha256_ctx *h, int64_t v) {
	char b[24];
	int n = snprintf(b, sizeof b, "%lld", (long long)v);
	digest_str(h, b, (size_t)n);
}

static void kept_free(kept *k) {
	if (!k) return;
	mbedtls_ssl_session_free(&k->s);
	ak_free(k->chain);
	ak_free(k);
}

static void cached_free(cached *e) {
	kept_free(e->tls12);
	for (int i = 0; i < e->n13; i++) kept_free(e->tls13[i]);
	ak_free(e->key);
	ak_free(e);
}

static void conf_free(tls_conf *c) {
	for (uint32_t i = 0; i < c->cache.len; i++) cached_free(c->cache.data[i]);
	ak_vec_free(&c->cache);
	ak_mutex_destroy(&c->cache_mu);
	if (c->tickets) mbedtls_ssl_ticket_free(&c->ticket);
	mbedtls_ssl_config_free(&c->ssl);
	mbedtls_x509_crt_free(&c->ca);
	for (uint32_t i = 0; i < c->npairs; i++) {
		mbedtls_x509_crt_free(&c->pairs[i].chain);
		mbedtls_pk_free(&c->pairs[i].key);
	}
	ak_free(c->pairs);
	for (char **p = c->alpn; p && *p; p++) ak_free(*p);
	ak_free(c->alpn);
	ak_free(c);
}

static int sni(void *p, mbedtls_ssl_context *ssl, const unsigned char *name, size_t n);

// known is the handle of the configuration made of inputs of digest d,
// 0 for none.
static int64_t known(const uint8_t d[32]) {
	int64_t id = 0;
	ak_mutex_lock(&confs_mu);
	for (uint32_t i = 0; i < confs.len && !id; i++)
		if (memcmp(confs.data[i]->digest, d, 32) == 0) id = confs.data[i]->id;
	ak_mutex_unlock(&confs_mu);
	return id;
}

// keep adds c to the configurations, or returns the one made of the same
// inputs meanwhile (c freed): its handle.
static int64_t keep(tls_conf *c) {
	ak_mutex_lock(&confs_mu);
	for (uint32_t i = 0; i < confs.len; i++) {
		if (memcmp(confs.data[i]->digest, c->digest, 32) == 0) {
			int64_t id = confs.data[i]->id;
			ak_mutex_unlock(&confs_mu);
			conf_free(c);
			return id;
		}
	}
	ak_vec_push(&confs, c);
	c->id = (int64_t)confs.len;
	ak_mutex_unlock(&confs_mu);
	return c->id;
}

static tls_conf *conf_new(bool server, const ak_arr *alpn, int64_t minv, int64_t maxv, char *why, size_t cap) {
	tls_conf *c = ak_alloc(sizeof *c);
	memset(c, 0, sizeof *c);
	c->server = server;
	ak_mutex_init(&c->cache_mu);
	mbedtls_ssl_config_init(&c->ssl);
	mbedtls_x509_crt_init(&c->ca);
	if (mbedtls_ssl_config_defaults(&c->ssl, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
		snprintf(why, cap, "TLS's defaults couldn't be set");
		conf_free(c);
		return NULL;
	}
	if (minv > maxv) {
		snprintf(why, cap, "minVersion is past maxVersion");
		conf_free(c);
		return NULL;
	}
	mbedtls_ssl_conf_min_tls_version(&c->ssl, (mbedtls_ssl_protocol_version)version_of(minv));
	mbedtls_ssl_conf_max_tls_version(&c->ssl, (mbedtls_ssl_protocol_version)version_of(maxv));
	if (alpn->len) {
		c->alpn = ak_alloc(sizeof(char *) * (alpn->len + 1));
		for (uint32_t i = 0; i < alpn->len; i++) {
			const ak_str *p = ak_arr_vals(alpn)[i].p;
			if (p->len == 0 || p->len > 255 || strlen(p->data) != p->len) {
				c->alpn[i] = NULL;
				snprintf(why, cap, "an ALPN protocol is 1 to 255 bytes: \"h2\", \"http/1.1\"");
				conf_free(c);
				return NULL;
			}
			c->alpn[i] = ak_alloc(p->len + 1);
			memcpy(c->alpn[i], p->data, p->len + 1);
			c->alpn[i + 1] = NULL;
		}
		c->alpn[alpn->len] = NULL;
		if (mbedtls_ssl_conf_alpn_protocols(&c->ssl, (const char **)c->alpn) != 0) {
			snprintf(why, cap, "the ALPN protocols can't be used");
			conf_free(c);
			return NULL;
		}
	}
	return c;
}

static void digest_common(ak_sha256_ctx *h, bool server, const ak_arr *alpn, int64_t minv, int64_t maxv) {
	digest_int(h, server);
	digest_int(h, minv);
	digest_int(h, maxv);
	digest_int(h, alpn->len);
	for (uint32_t i = 0; i < alpn->len; i++) digest_str(h, ((ak_str *)ak_arr_vals(alpn)[i].p)->data, ((ak_str *)ak_arr_vals(alpn)[i].p)->len);
}

static ak_val client_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!ready()) return (ak_val){0};
	const ak_str *ca = STR(0), *cert = STR(1), *key = STR(2);
	const ak_arr *alpn = args[3].p;
	int64_t minv = args[4].i, maxv = args[5].i;
	bool insecure = args[6].i != 0;
	int64_t cache_size = args[7].i;
	char why[300] = "";
	if ((cert->len == 0) != (key->len == 0)) {
		throw_invalid("a client's certificate goes with its key: both, or neither");
		return (ak_val){0};
	}
	if (cache_size < 0) {
		throw_invalid("sessionCache is 0 (none kept) or more");
		return (ak_val){0};
	}
	uint8_t d[32];
	ak_sha256_ctx h;
	ak_sha256_init(&h);
	digest_common(&h, false, alpn, minv, maxv);
	digest_str(&h, ca->data, ca->len);
	digest_str(&h, cert->data, cert->len);
	digest_str(&h, key->data, key->len);
	digest_int(&h, insecure);
	digest_int(&h, cache_size);
	ak_sha256_final(&h, d);
	int64_t id = known(d);
	if (id) return (ak_val){.i = id};
	tls_conf *c = conf_new(false, alpn, minv, maxv, why, sizeof why);
	if (!c) {
		throw_invalid(why);
		return (ak_val){0};
	}
	c->insecure = insecure;
	// Sessions resumed: tickets asked for (TLS 1.2's) and taken (1.3's),
	// and TLS 1.3's PSK with (EC)DHE offered; never PSK alone.
	c->cache_size = cache_size;
	mbedtls_ssl_conf_session_tickets(&c->ssl, cache_size ? MBEDTLS_SSL_SESSION_TICKETS_ENABLED : MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
	mbedtls_ssl_conf_tls13_key_exchange_modes(&c->ssl, cache_size ? MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ALL : MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);
	// The client checks the certificate itself, once the handshake is
	// done (tls_handshake): Mbed TLS checks none.
	mbedtls_ssl_conf_authmode(&c->ssl, MBEDTLS_SSL_VERIFY_NONE);
	if (ca->len) {
		if (!pem_certs(ca, &c->ca, "ca", why, sizeof why)) goto fail;
		c->has_ca = true;
	}
	c->system = !insecure && !c->has_ca;
	if (cert->len) {
		c->pairs = ak_alloc(sizeof(keypair));
		c->npairs = 1;
		mbedtls_x509_crt_init(&c->pairs[0].chain);
		mbedtls_pk_init(&c->pairs[0].key);
		if (!pem_pair(cert, key, &c->pairs[0], "", why, sizeof why)) goto fail;
		if (mbedtls_ssl_conf_own_cert(&c->ssl, &c->pairs[0].chain, &c->pairs[0].key) != 0) {
			snprintf(why, sizeof why, "the certificate can't be used");
			goto fail;
		}
	}
	memcpy(c->digest, d, 32);
	return (ak_val){.i = keep(c)};
fail:
	conf_free(c);
	throw_invalid(why);
	return (ak_val){0};
}

static ak_val server_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!ready()) return (ak_val){0};
	const ak_arr *pairs = args[0].p;
	const ak_arr *alpn = args[1].p;
	const ak_str *client_ca = STR(2);
	bool require = args[3].i != 0;
	int64_t minv = args[4].i, maxv = args[5].i;
	bool tickets = args[6].i != 0;
	char why[300] = "";
	if (pairs->len < 2 || pairs->len % 2) {
		throw_invalid("a server needs a certificate and its key");
		return (ak_val){0};
	}
	uint8_t d[32];
	ak_sha256_ctx h;
	ak_sha256_init(&h);
	digest_common(&h, true, alpn, minv, maxv);
	digest_int(&h, pairs->len);
	for (uint32_t i = 0; i < pairs->len; i++) digest_str(&h, ((ak_str *)ak_arr_vals(pairs)[i].p)->data, ((ak_str *)ak_arr_vals(pairs)[i].p)->len);
	digest_str(&h, client_ca->data, client_ca->len);
	digest_int(&h, require);
	digest_int(&h, tickets);
	ak_sha256_final(&h, d);
	int64_t id = known(d);
	if (id) return (ak_val){.i = id};
	tls_conf *c = conf_new(true, alpn, minv, maxv, why, sizeof why);
	if (!c) {
		throw_invalid(why);
		return (ak_val){0};
	}
	c->npairs = pairs->len / 2;
	c->pairs = ak_alloc(sizeof(keypair) * c->npairs);
	for (uint32_t i = 0; i < c->npairs; i++) {
		mbedtls_x509_crt_init(&c->pairs[i].chain);
		mbedtls_pk_init(&c->pairs[i].key);
	}
	for (uint32_t i = 0; i < c->npairs; i++) {
		char which[24] = "";
		if (i > 0) snprintf(which, sizeof which, "others[%u]: ", i - 1);
		if (!pem_pair(ak_arr_vals(pairs)[2 * i].p, ak_arr_vals(pairs)[2 * i + 1].p, &c->pairs[i], which, why, sizeof why)) goto fail;
	}
	// The first is shown when the client asks for no name, or one no
	// certificate has (sni adds those that match).
	if (mbedtls_ssl_conf_own_cert(&c->ssl, &c->pairs[0].chain, &c->pairs[0].key) != 0) {
		snprintf(why, sizeof why, "the certificate can't be used");
		goto fail;
	}
	mbedtls_ssl_conf_sni(&c->ssl, sni, c);
	// Tickets: TLS 1.2's (RFC 5077) and two of TLS 1.3's after each full
	// handshake (one after a resumed one), so a client can open two
	// connections resumed; TLS 1.3's PSK only with (EC)DHE.
	if (tickets) {
		mbedtls_ssl_ticket_init(&c->ticket);
		c->tickets = true;
		if (mbedtls_ssl_ticket_setup(&c->ticket, PSA_ALG_GCM, PSA_KEY_TYPE_AES, 256, TICKET_LIFETIME) != 0) {
			snprintf(why, sizeof why, "the session tickets' key couldn't be made");
			goto fail;
		}
		mbedtls_ssl_conf_session_tickets_cb(&c->ssl, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse, &c->ticket);
		mbedtls_ssl_conf_new_session_tickets(&c->ssl, 2);
		mbedtls_ssl_conf_tls13_key_exchange_modes(&c->ssl, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ALL);
	} else {
		mbedtls_ssl_conf_new_session_tickets(&c->ssl, 0);
		mbedtls_ssl_conf_tls13_key_exchange_modes(&c->ssl, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);
	}
	if (client_ca->len) {
		if (!pem_certs(client_ca, &c->ca, "clientCA", why, sizeof why)) goto fail;
		c->has_ca = true;
		c->require = require;
		mbedtls_ssl_conf_ca_chain(&c->ssl, &c->ca, NULL);
		mbedtls_ssl_conf_authmode(&c->ssl, require ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
	} else {
		mbedtls_ssl_conf_authmode(&c->ssl, MBEDTLS_SSL_VERIFY_NONE);
	}
	memcpy(c->digest, d, 32);
	return (ak_val){.i = keep(c)};
fail:
	conf_free(c);
	throw_invalid(why);
	return (ak_val){0};
}

// ---- Sessions ----

enum { PH_HS, PH_VERIFY, PH_OPEN, PH_FAILED };

// The problems a certificate has (std/net's CertificateProblem).
static const char *const problems[] = {"", "expired", "notYetValid", "wrongHost", "unknownIssuer", "revoked", "missing", "invalid"};

// A check by the system's trust store, on a thread of its own: what it
// found, and whom to tell. The session and the thread each hold it.
typedef struct {
	ak_mutex mu;
	int refs;
	bool done, ok;
	int problem;
	char why[256];
	void (*notify)(void *ctx);
	void *ctx;
} vbox;

struct ak_tls {
	tls_conf *conf;
	mbedtls_ssl_context ssl;
	ak_sock fd;
	ak_buf pre;  // read before TLS began: the handshake's first bytes
	size_t pre_off;
	int phase;
	bool starved;  // the last read wanted the socket
	int first;     // the first byte that came (-1: none yet): TLS's is a record's type
	char *host;
	bool ip;
	int sock_err;
	ak_net_err err;
	char why[400];
	vbox *box;
	void (*notify)(void *ctx);
	void *ctx;
	ak_tls_info info;
	AK_VEC(const uint8_t *) certs;
	AK_VEC(size_t) lens;
	// A client's sessions: its cache's key, and when the full handshake
	// its session comes of checked the certificate (now's, for one that
	// didn't resume).
	char *cache_key;
	int64_t born;
	bool offered;    // a session was offered
	uint8_t *chain;  // the offered session's certificates (kept's)
	size_t chain_len;
};

#define ERR_SOCKET (-0x004C)  // a socket's failure (sock_err says which)

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
	ak_tls *t = ctx;
	for (;;) {
		int chunk = len > (1u << 30) ? (1 << 30) : (int)len;
		int w = (int)send(t->fd, (const char *)buf, chunk, AK_SEND_FLAGS);
		if (w >= 0) return w;
		int e = ak_sock_err();
		if (AK_SOCK_INTR(e)) continue;
		if (AK_SOCK_AGAIN(e)) return MBEDTLS_ERR_SSL_WANT_WRITE;
		t->sock_err = e;
		return ERR_SOCKET;
	}
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
	ak_tls *t = ctx;
	if (t->pre_off < t->pre.len) {
		size_t k = t->pre.len - t->pre_off < len ? t->pre.len - t->pre_off : len;
		memcpy(buf, t->pre.data + t->pre_off, k);
		t->pre_off += k;
		if (t->pre_off == t->pre.len) {
			ak_buf_free(&t->pre);
			t->pre_off = 0;
		}
		if (t->first < 0 && k) t->first = buf[0];
		return (int)k;
	}
	for (;;) {
		int chunk = len > (1u << 30) ? (1 << 30) : (int)len;
		int r = (int)recv(t->fd, (char *)buf, chunk, 0);
		if (r > 0 && t->first < 0) t->first = buf[0];
		if (r >= 0) return r;
		int e = ak_sock_err();
		if (AK_SOCK_INTR(e)) continue;
		if (AK_SOCK_AGAIN(e)) return MBEDTLS_ERR_SSL_WANT_READ;
		t->sock_err = e;
		return ERR_SOCKET;
	}
}

// ---- Sessions kept, to resume ----

// How long a client keeps what a full handshake began: TLS 1.3's tickets
// (RFC 8446, 4.6.1's bound), TLS 1.2's sessions (as one pins the version,
// a full handshake a day sees a server's new one).
#define TLS13_MAX (7 * 24 * 3600)
#define TLS12_MAX (24 * 3600)

int64_t ak_tls_clock_shift;

static int64_t clock_s(void) { return ak_now_ns() / INT64_C(1000000000) + ak_tls_clock_shift; }

// find_cached is the entry of key (c's cache_mu held), or NULL.
static cached *find_cached(tls_conf *c, const char *key, uint32_t *at) {
	for (uint32_t i = 0; i < c->cache.len; i++)
		if (strcmp(c->cache.data[i]->key, key) == 0) {
			if (at) *at = i;
			return c->cache.data[i];
		}
	return NULL;
}

static void drop_cached(tls_conf *c, uint32_t i) {
	cached_free(c->cache.data[i]);
	c->cache.data[i] = c->cache.data[c->cache.len - 1];
	c->cache.len--;
}

// usable reports whether k may be offered now: not past its time, its
// certificate (that of the full handshake it comes of) not expired.
static bool usable(const tls_conf *c, const kept *k, int64_t now) {
	if (now >= k->expires) return false;
	const mbedtls_x509_crt *leaf = k->s.peer_cert;
	return c->insecure || (leaf && !mbedtls_x509_time_is_past(&leaf->valid_to));
}

// offer sets a session of t's server on t, if its configuration keeps
// one that's usable: a TLS 1.3 ticket (the newest, taken) before TLS 1.2's
// session.
static void offer(ak_tls *t) {
	tls_conf *c = t->conf;
	int64_t now = clock_s();
	ak_mutex_lock(&c->cache_mu);
	uint32_t at;
	cached *e = find_cached(c, t->cache_key, &at);
	if (e) {
		while (e->n13 && !usable(c, e->tls13[e->n13 - 1], now)) kept_free(e->tls13[--e->n13]);
		if (e->tls12 && !usable(c, e->tls12, now)) kept_free(e->tls12), e->tls12 = NULL;
		kept *k = e->n13 ? e->tls13[e->n13 - 1] : e->tls12;
		if (k && mbedtls_ssl_set_session(&t->ssl, &k->s) == 0) {
			t->born = e->born;
			t->chain = ak_alloc(k->chain_len ? k->chain_len : 1);
			if (k->chain_len) memcpy(t->chain, k->chain, k->chain_len);
			t->chain_len = k->chain_len;
			t->offered = true;
			e->used = ++c->ticks;
			if (k != e->tls12) kept_free(e->tls13[--e->n13]);
		}
		if (!e->n13 && !e->tls12) drop_cached(c, at);
	}
	ak_mutex_unlock(&c->cache_mu);
}

// forget drops t's server's sessions: a handshake that offered one
// failed (a resumed one offers only its own version: a server that's
// changed since is met with a full handshake next).
static void forget(ak_tls *t) {
	if (!t->offered) return;
	tls_conf *c = t->conf;
	ak_mutex_lock(&c->cache_mu);
	uint32_t at;
	if (find_cached(c, t->cache_key, &at)) drop_cached(c, at);
	ak_mutex_unlock(&c->cache_mu);
	t->offered = false;
}

// keep_session keeps t's session (the last ticket a TLS 1.3 server sent,
// or TLS 1.2's), for its server, the least recently used server's
// dropped when there are as many as the configuration keeps.
static void keep_session(ak_tls *t) {
	tls_conf *c = t->conf;
	if (!t->cache_key) return;
	kept *k = ak_alloc(sizeof *k);
	mbedtls_ssl_session_init(&k->s);
	if (mbedtls_ssl_get_session(&t->ssl, &k->s) != 0) {
		kept_free(k);
		return;
	}
	size_t n = 0;
	for (uint32_t i = 0; i < t->certs.len; i++) n += 4 + t->lens.data[i];
	k->chain = ak_alloc(n ? n : 1);
	for (uint32_t i = 0; i < t->certs.len; i++) {
		size_t l = t->lens.data[i];
		uint8_t *p = k->chain + k->chain_len;
		p[0] = (uint8_t)(l >> 24), p[1] = (uint8_t)(l >> 16), p[2] = (uint8_t)(l >> 8), p[3] = (uint8_t)l;
		memcpy(p + 4, t->certs.data[i], l);
		k->chain_len += 4 + l;
	}
	int64_t now = clock_s();
	bool tls13 = k->s.tls_version == MBEDTLS_SSL_VERSION_TLS1_3;
	if (tls13) {
		int64_t life = k->s.ticket_lifetime;
		k->expires = now + life < t->born + TLS13_MAX ? now + life : t->born + TLS13_MAX;
	} else {
		int64_t life = k->s.ticket_len && k->s.ticket_lifetime && k->s.ticket_lifetime < TLS12_MAX ? k->s.ticket_lifetime : TLS12_MAX;
		k->expires = now + life < t->born + TLS12_MAX ? now + life : t->born + TLS12_MAX;
	}
	ak_mutex_lock(&c->cache_mu);
	cached *e = find_cached(c, t->cache_key, NULL);
	if (e && e->born != t->born) {
		// A full handshake since: what came before it goes.
		kept_free(e->tls12);
		for (int i = 0; i < e->n13; i++) kept_free(e->tls13[i]);
		e->tls12 = NULL, e->n13 = 0;
	}
	if (!e) {
		if (c->cache.len >= (uint64_t)c->cache_size) {
			uint32_t oldest = 0;
			for (uint32_t i = 1; i < c->cache.len; i++)
				if (c->cache.data[i]->used < c->cache.data[oldest]->used) oldest = i;
			drop_cached(c, oldest);
		}
		e = ak_alloc(sizeof *e);
		memset(e, 0, sizeof *e);
		size_t n = strlen(t->cache_key);
		e->key = ak_alloc(n + 1);
		memcpy(e->key, t->cache_key, n + 1);
		ak_vec_push(&c->cache, e);
	}
	e->born = t->born;
	e->used = ++c->ticks;
	if (tls13) {
		if (e->n13 == (int)AK_LEN(e->tls13)) {
			kept_free(e->tls13[0]);
			memmove(e->tls13, e->tls13 + 1, sizeof e->tls13[0] * (AK_LEN(e->tls13) - 1));
			e->n13--;
		}
		e->tls13[e->n13++] = k;
	} else {
		kept_free(e->tls12);
		e->tls12 = k;
	}
	ak_mutex_unlock(&c->cache_mu);
}

int ak_tls_cached(int64_t config) {
	tls_conf *c = find_conf(config);
	if (!c) return -1;
	ak_mutex_lock(&c->cache_mu);
	int n = 0;
	for (uint32_t i = 0; i < c->cache.len; i++) n += c->cache.data[i]->n13 + (c->cache.data[i]->tls12 != NULL);
	ak_mutex_unlock(&c->cache_mu);
	return n;
}

// cache_key is "name:port", the port the socket's peer's.
static char *cache_key(const char *host, ak_sock fd) {
	struct sockaddr_storage ss;
	socklen_t sl = sizeof ss;
	unsigned port = 0;
	if (getpeername(fd, (struct sockaddr *)&ss, &sl) == 0) {
		if (ss.ss_family == AF_INET) port = ntohs(((struct sockaddr_in *)&ss)->sin_port);
		else if (ss.ss_family == AF_INET6) port = ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
	}
	size_t n = strlen(host) + 8;
	char *k = ak_alloc(n);
	snprintf(k, n, "%s:%u", host, port);
	return k;
}

static bool is_ip(const char *host) {
	const char *pct = strchr(host, '%');
	uint8_t b[16];
	return ak_ip_parse(host, pct ? (size_t)(pct - host) : strlen(host), b) != 0;
}

static ak_tls *t_open(int64_t config, bool server, const char *name, ak_sock fd, void (*notify)(void *), void *ctx, char *err, size_t cap) {
	tls_conf *c = find_conf(config);
	if (!c || c->server != server) {
		snprintf(err, cap, server ? "the configuration isn't a server's (net.serverTLS)" : "the configuration isn't a client's (net.clientTLS)");
		return NULL;
	}
	if (!server && !c->insecure && (!name || !name[0])) {
		snprintf(err, cap, "a server name is needed to check its certificate against");
		return NULL;
	}
	ak_tls *t = ak_alloc(sizeof *t);
	memset(t, 0, sizeof *t);
	t->conf = c;
	t->fd = fd;
	t->notify = notify;
	t->ctx = ctx;
	t->first = -1;
	mbedtls_ssl_init(&t->ssl);
	int r = mbedtls_ssl_setup(&t->ssl, &c->ssl);
	if (r != 0) {
		mbedtls_ssl_free(&t->ssl);
		ak_free(t);
		snprintf(err, cap, "TLS couldn't start (Mbed TLS error -0x%04X)", (unsigned)-r);
		return NULL;
	}
	mbedtls_ssl_set_user_data_p(&t->ssl, t);
	mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
	if (!server) {
		const char *h = name ? name : "";
		size_t n = strlen(h);
		t->host = ak_alloc(n + 1);
		memcpy(t->host, h, n + 1);
		t->ip = is_ip(h);
		// The name goes in SNI; an IP address doesn't (RFC 6066, 3).
		r = mbedtls_ssl_set_hostname(&t->ssl, n && !t->ip ? h : NULL);
		if (r != 0) {
			mbedtls_ssl_free(&t->ssl);
			ak_free(t->host);
			ak_free(t);
			snprintf(err, cap, "the server name is too long");
			return NULL;
		}
		snprintf(t->info.server_name, sizeof t->info.server_name, "%s", h);
		t->born = clock_s();
		if (c->cache_size) {
			t->cache_key = cache_key(h, fd);
			offer(t);
		}
	}
	return t;
}

static void t_preload(ak_tls *t, const uint8_t *p, size_t n) {
	if (n) ak_buf_put(&t->pre, p, n);
}

static void fail(ak_tls *t, ak_net_err e, const char *fmt, ...) AK_PRINTF(3, 4);
static void fail(ak_tls *t, ak_net_err e, const char *fmt, ...) {
	t->phase = PH_FAILED;
	t->err = e;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(t->why, sizeof t->why, fmt, ap);
	va_end(ap);
}

static void sock_fail(ak_tls *t) {
	int e = t->sock_err;
#if defined(_WIN32)
	bool reset = e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAESHUTDOWN;
	bool timed = e == WSAETIMEDOUT;
#else
	bool reset = e == ECONNRESET || e == ECONNABORTED || e == EPIPE;
	bool timed = e == ETIMEDOUT;
#endif
	if (reset) fail(t, AK_NE_RESET, "%s", "");
	else if (timed) fail(t, AK_NE_TIMEDOUT, "%s", "");
	else fail(t, AK_NE_FAILED, "the connection failed (error %d)", e);
}

static const char *alert_text(int a) {
	switch (a) {
	case MBEDTLS_SSL_ALERT_MSG_HANDSHAKE_FAILURE: return "the other side refused the handshake: no cipher suite, key exchange or signature in common";
	case MBEDTLS_SSL_ALERT_MSG_NO_CERT:
	case MBEDTLS_SSL_ALERT_MSG_BAD_CERT:
	case MBEDTLS_SSL_ALERT_MSG_UNSUPPORTED_CERT:
	case MBEDTLS_SSL_ALERT_MSG_CERT_UNKNOWN: return "the other side didn't accept this side's certificate";
	case MBEDTLS_SSL_ALERT_MSG_CERT_REVOKED: return "the other side says this side's certificate is revoked";
	case MBEDTLS_SSL_ALERT_MSG_CERT_EXPIRED: return "the other side says this side's certificate has expired";
	case MBEDTLS_SSL_ALERT_MSG_UNKNOWN_CA: return "the other side doesn't trust the authority that signed this side's certificate";
	case MBEDTLS_SSL_ALERT_MSG_ACCESS_DENIED: return "the other side denied access";
	case MBEDTLS_SSL_ALERT_MSG_PROTOCOL_VERSION: return "the other side speaks no TLS version this side allows";
	case MBEDTLS_SSL_ALERT_MSG_INSUFFICIENT_SECURITY: return "the other side wants stronger ciphers than this side offers";
	case MBEDTLS_SSL_ALERT_MSG_INTERNAL_ERROR: return "the other side failed (its internal error)";
	case MBEDTLS_SSL_ALERT_MSG_UNRECOGNIZED_NAME: return "the server has no certificate for the name asked for (SNI)";
	case MBEDTLS_SSL_ALERT_MSG_CERT_REQUIRED: return "the other side requires a certificate of this side's (a client certificate)";
	case MBEDTLS_SSL_ALERT_MSG_NO_APPLICATION_PROTOCOL: return "no application protocol (ALPN) in common with the other side";
	case MBEDTLS_SSL_ALERT_MSG_DECODE_ERROR:
	case MBEDTLS_SSL_ALERT_MSG_ILLEGAL_PARAMETER:
	case MBEDTLS_SSL_ALERT_MSG_UNEXPECTED_MESSAGE: return "the other side couldn't read what this side sent";
	case MBEDTLS_SSL_ALERT_MSG_BAD_RECORD_MAC:
	case MBEDTLS_SSL_ALERT_MSG_DECRYPT_ERROR: return "the other side couldn't decrypt or check what this side sent";
	}
	return NULL;
}

static void cert_fail(ak_tls *t, uint32_t flags, const char *whose);
static int opened(ak_tls *t);

// ssl_fail sets why Mbed TLS's r failed.
static int ssl_fail(ak_tls *t, int r) {
	if (t->sock_err) {
		sock_fail(t);
		return AK_TLS_FAIL;
	}
	bool hs = t->phase == PH_HS;
	switch (r) {
	case MBEDTLS_ERR_SSL_CONN_EOF:
		if (hs) fail(t, AK_NE_TLS, "the other side closed the connection during the handshake");
		else fail(t, AK_NE_RESET, "%s", "");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE: {
		int a = mbedtls_ssl_get_fatal_alert(&t->ssl);
		const char *s = alert_text(a);
		if (s) fail(t, AK_NE_TLS, "%s", s);
		else fail(t, AK_NE_TLS, "the other side ended the connection (TLS alert %d)", a);
		return AK_TLS_FAIL;
	}
	case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
		fail(t, AK_NE_TLS, "no TLS version in common with the other side");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
		fail(t, AK_NE_TLS, "no cipher suite, key exchange or signature in common with the other side");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_SSL_NO_APPLICATION_PROTOCOL:
		fail(t, AK_NE_TLS, "no application protocol (ALPN) in common with the other side");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_SSL_INVALID_MAC:
		fail(t, AK_NE_TLS, "a record failed its integrity check: the bytes were changed on the way");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_SSL_NO_CLIENT_CERTIFICATE:
		fail(t, AK_NE_UNTRUSTED, "missing:the client gave no certificate, and one is required");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
		cert_fail(t, mbedtls_ssl_get_verify_result(&t->ssl), "the client's certificate");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_SSL_BAD_CERTIFICATE:
		fail(t, AK_NE_UNTRUSTED, "invalid:the other side's certificate can't be read");
		return AK_TLS_FAIL;
	case MBEDTLS_ERR_SSL_INVALID_RECORD:
	case MBEDTLS_ERR_SSL_DECODE_ERROR:
	case MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE:
	case MBEDTLS_ERR_SSL_UNEXPECTED_RECORD:
		// A TLS record starts with its type: a handshake's (22) or an
		// alert's (21). Anything else is another protocol.
		if (hs && t->first >= 0 && t->first != 22 && t->first != 21) fail(t, AK_NE_TLS, "what the other side sent isn't TLS (is it TLS there, on that port?)");
		else fail(t, AK_NE_TLS, "the other side sent a TLS message that can't be read");
		return AK_TLS_FAIL;
	}
	fail(t, AK_NE_TLS, "TLS failed (Mbed TLS error -0x%04X)", (unsigned)(r < 0 ? -r : r));
	return AK_TLS_FAIL;
}

// names writes crt's names (its DNS and IP subject alternative names, or
// its common name) into out.
static void names_of(const mbedtls_x509_crt *crt, char *out, size_t cap) {
	size_t w = 0;
	int count = 0;
	out[0] = 0;
	for (const mbedtls_x509_sequence *s = &crt->subject_alt_names; s && s->buf.p && w + 4 < cap; s = s->next) {
		mbedtls_x509_subject_alternative_name san;
		if (mbedtls_x509_parse_subject_alt_name(&s->buf, &san) != 0) continue;
		char one[64] = "";
		if (san.type == MBEDTLS_X509_SAN_DNS_NAME) {
			snprintf(one, sizeof one, "%.*s", (int)(san.san.unstructured_name.len < 60 ? san.san.unstructured_name.len : 60), san.san.unstructured_name.p);
		} else if (san.type == MBEDTLS_X509_SAN_IP_ADDRESS && (san.san.unstructured_name.len == 4 || san.san.unstructured_name.len == 16)) {
			char ip[46];
			ak_ip_format(san.san.unstructured_name.p, (int)san.san.unstructured_name.len, ip);
			snprintf(one, sizeof one, "%s", ip);
		}
		mbedtls_x509_free_subject_alt_name(&san);
		if (!one[0]) continue;
		if (++count > 4) {
			w += (size_t)snprintf(out + w, cap - w, ", ...");
			break;
		}
		w += (size_t)snprintf(out + w, cap - w, "%s%s", w ? ", " : "", one);
		if (w >= cap) break;
	}
	if (!out[0]) {
		for (const mbedtls_x509_name *n = &crt->subject; n; n = n->next) {
			if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0) {
				snprintf(out, cap, "%.*s", (int)n->val.len, n->val.p);
				break;
			}
		}
	}
}

// issuer_of writes crt's issuer (its common name, else its whole name).
static void issuer_of(const mbedtls_x509_crt *crt, char *out, size_t cap) {
	out[0] = 0;
	for (const mbedtls_x509_name *n = &crt->issuer; n; n = n->next) {
		if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0) {
			snprintf(out, cap, "%.*s", (int)n->val.len, n->val.p);
			return;
		}
	}
	if (mbedtls_x509_dn_gets(out, cap, &crt->issuer) < 0) out[0] = 0;
}

static int problem_of(uint32_t flags) {
	if (flags & MBEDTLS_X509_BADCERT_MISSING) return 6;
	if (flags & MBEDTLS_X509_BADCERT_EXPIRED) return 1;
	if (flags & MBEDTLS_X509_BADCERT_FUTURE) return 2;
	if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) return 3;
	if (flags & MBEDTLS_X509_BADCERT_REVOKED) return 5;
	if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) return 4;
	return 7;
}

// untrusted fails t with problem p, said of whose certificate.
static void untrusted(ak_tls *t, int p, const char *whose, const char *system_why) {
	const mbedtls_x509_crt *leaf = mbedtls_ssl_get_peer_cert(&t->ssl);
	char names[200] = "", issuer[200] = "";
	if (leaf) {
		names_of(leaf, names, sizeof names);
		issuer_of(leaf, issuer, sizeof issuer);
	}
	unsigned char alert = MBEDTLS_SSL_ALERT_MSG_BAD_CERT;
	switch (p) {
	case 1:
		alert = MBEDTLS_SSL_ALERT_MSG_CERT_EXPIRED;
		fail(t, AK_NE_UNTRUSTED, "%s:%s expired on %04d-%02d-%02d", problems[p], whose, leaf ? leaf->valid_to.year : 0, leaf ? leaf->valid_to.mon : 0, leaf ? leaf->valid_to.day : 0);
		break;
	case 2:
		fail(t, AK_NE_UNTRUSTED, "%s:%s isn't valid until %04d-%02d-%02d", problems[p], whose, leaf ? leaf->valid_from.year : 0, leaf ? leaf->valid_from.mon : 0, leaf ? leaf->valid_from.day : 0);
		break;
	case 3: fail(t, AK_NE_UNTRUSTED, "%s:%s is for %s, not %s", problems[p], whose, names[0] ? names : "other names", t->host ? t->host : "this host"); break;
	case 4:
		alert = MBEDTLS_SSL_ALERT_MSG_UNKNOWN_CA;
		fail(t, AK_NE_UNTRUSTED, "%s:%s is signed by an authority that isn't trusted (%s)%s%s", problems[p], whose, issuer[0] ? issuer : "unnamed", system_why && system_why[0] ? ": " : "", system_why ? system_why : "");
		break;
	case 5:
		alert = MBEDTLS_SSL_ALERT_MSG_CERT_REVOKED;
		fail(t, AK_NE_UNTRUSTED, "%s:%s has been revoked", problems[p], whose);
		break;
	case 6: fail(t, AK_NE_UNTRUSTED, "%s:%s wasn't given", problems[p], whose); break;
	default:
		fail(t, AK_NE_UNTRUSTED, "%s:%s isn't allowed for this use, or is signed with a weak key or hash%s%s", problems[7], whose, system_why && system_why[0] ? ": " : "", system_why ? system_why : "");
	}
	// The other side hears why (best effort: the connection ends).
	mbedtls_ssl_send_alert_message(&t->ssl, MBEDTLS_SSL_ALERT_LEVEL_FATAL, alert);
}

static void cert_fail(ak_tls *t, uint32_t flags, const char *whose) { untrusted(t, problem_of(flags), whose, NULL); }

static const struct {
	int id;
	const char *name;
} suites[] = {
	{0x1301, "TLS_AES_128_GCM_SHA256"},
	{0x1302, "TLS_AES_256_GCM_SHA384"},
	{0x1303, "TLS_CHACHA20_POLY1305_SHA256"},
	{0xC02B, "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"},
	{0xC02C, "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"},
	{0xC02F, "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"},
	{0xC030, "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"},
	{0xCCA8, "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256"},
	{0xCCA9, "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256"},
};

// agreed fills t's info: what the handshake agreed.
static void agreed(ak_tls *t) {
	ak_tls_info *in = &t->info;
	in->version = mbedtls_ssl_get_version_number(&t->ssl) == MBEDTLS_SSL_VERSION_TLS1_3 ? 13 : 12;
	int id = mbedtls_ssl_get_ciphersuite_id_from_ssl(&t->ssl);
	const char *name = mbedtls_ssl_get_ciphersuite(&t->ssl);
	for (size_t i = 0; i < sizeof suites / sizeof suites[0]; i++)
		if (suites[i].id == id) name = suites[i].name;
	snprintf(in->cipher, sizeof in->cipher, "%s", name ? name : "");
	const char *p = mbedtls_ssl_get_alpn_protocol(&t->ssl);
	snprintf(in->protocol, sizeof in->protocol, "%s", p ? p : "");
	t->certs.len = t->lens.len = 0;
	if (in->resumed && t->chain) {
		// A resumed session's certificates: its full handshake's.
		for (size_t at = 0; at + 4 <= t->chain_len;) {
			const uint8_t *p = t->chain + at;
			size_t l = (size_t)p[0] << 24 | (size_t)p[1] << 16 | (size_t)p[2] << 8 | p[3];
			ak_vec_push(&t->certs, p + 4);
			ak_vec_push(&t->lens, l);
			at += 4 + l;
		}
		return;
	}
	for (const mbedtls_x509_crt *c = mbedtls_ssl_get_peer_cert(&t->ssl); c && c->raw.p; c = c->next) {
		ak_vec_push(&t->certs, c->raw.p);
		ak_vec_push(&t->lens, c->raw.len);
	}
}

// The server's choice of certificate by the name the client asks for:
// those whose names match it (as a client checks them), else the first.
static int sni(void *p, mbedtls_ssl_context *ssl, const unsigned char *name, size_t n) {
	tls_conf *c = p;
	ak_tls *t = mbedtls_ssl_get_user_data_p(ssl);
	char host[256];
	if (n >= sizeof host) return 0;
	memcpy(host, name, n);
	host[n] = 0;
	if (t) snprintf(t->info.server_name, sizeof t->info.server_name, "%s", host);
	for (uint32_t i = 0; i < c->npairs && c->npairs > 1; i++) {
		uint32_t flags = 0;
		mbedtls_x509_crt_verify(&c->pairs[i].chain, NULL, NULL, host, &flags, NULL, NULL);
		if (!(flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)) mbedtls_ssl_set_hs_own_cert(ssl, &c->pairs[i].chain, &c->pairs[i].key);
	}
	return 0;
}

static void box_release(vbox *b) {
	ak_mutex_lock(&b->mu);
	bool last = --b->refs == 0;
	ak_mutex_unlock(&b->mu);
	if (last) {
		ak_mutex_destroy(&b->mu);
		ak_free(b);
	}
}

typedef struct {
	vbox *box;
	char *host;
	uint32_t n;
	uint8_t **der;
	size_t *len;
} vjob;

static void verify_main(void *p) {
	vjob *j = p;
	int problem = 0;
	char why[256] = "";
	bool ok = ak_tls_system_verify(j->host, (const uint8_t *const *)j->der, j->len, j->n, &problem, why, sizeof why);
	vbox *b = j->box;
	ak_mutex_lock(&b->mu);
	b->done = true;
	b->ok = ok;
	b->problem = problem;
	snprintf(b->why, sizeof b->why, "%s", why);
	// The scheduler's hold (verify's) is the told's to let go of, or, with
	// no one to tell (the session closed), this thread's.
	bool told = b->notify != NULL;
	if (told) b->notify(b->ctx);
	ak_mutex_unlock(&b->mu);
	if (!told) ak_sched_unhold();
	box_release(b);
	for (uint32_t i = 0; i < j->n; i++) ak_free(j->der[i]);
	ak_free(j->der);
	ak_free(j->len);
	ak_free(j->host);
	ak_free(j);
}

// verify_system hands the chain to the system's check, on a thread.
static int verify_system(ak_tls *t) {
	vbox *b = ak_alloc(sizeof *b);
	memset(b, 0, sizeof *b);
	ak_mutex_init(&b->mu);
	b->refs = 2;
	b->notify = t->notify;
	b->ctx = t->ctx;
	vjob *j = ak_alloc(sizeof *j);
	memset(j, 0, sizeof *j);
	j->box = b;
	size_t hn = strlen(t->host);
	j->host = ak_alloc(hn + 1);
	memcpy(j->host, t->host, hn + 1);
	j->n = t->certs.len;
	j->der = ak_alloc(sizeof(uint8_t *) * (j->n ? j->n : 1));
	j->len = ak_alloc(sizeof(size_t) * (j->n ? j->n : 1));
	for (uint32_t i = 0; i < j->n; i++) {
		j->der[i] = ak_alloc(t->lens.data[i]);
		memcpy(j->der[i], t->certs.data[i], t->lens.data[i]);
		j->len[i] = t->lens.data[i];
	}
	t->box = b;
	t->phase = PH_VERIFY;
	ak_sched_hold();
	if (!ak_thread_start(verify_main, j)) verify_main(j);
	return AK_TLS_WAIT;
}

static int verified(ak_tls *t) {
	vbox *b = t->box;
	ak_mutex_lock(&b->mu);
	bool done = b->done, ok = b->ok;
	int problem = b->problem;
	char why[256];
	snprintf(why, sizeof why, "%s", b->why);
	ak_mutex_unlock(&b->mu);
	if (!done) return AK_TLS_WAIT;
	t->box = NULL;
	box_release(b);
	if (ok) return opened(t);
	// The dates and the name as Mbed TLS reads them say it more surely
	// than the system's codes; the rest is the system's.
	uint32_t flags = 0;
	mbedtls_x509_crt_verify((mbedtls_x509_crt *)mbedtls_ssl_get_peer_cert(&t->ssl), NULL, NULL, t->host, &flags, NULL, NULL);
	int p = flags & MBEDTLS_X509_BADCERT_EXPIRED ? 1 : flags & MBEDTLS_X509_BADCERT_FUTURE ? 2 : flags & MBEDTLS_X509_BADCERT_CN_MISMATCH ? 3 : problem ? problem : 4;
	char whose[300];
	snprintf(whose, sizeof whose, "%s's certificate", t->host);
	untrusted(t, p, whose, why);
	return AK_TLS_FAIL;
}

// opened ends t's handshake, the certificate checked: a client keeps its
// TLS 1.2 session (TLS 1.3's tickets come after, as it reads).
static int opened(ak_tls *t) {
	t->phase = PH_OPEN;
	if (!t->conf->server && t->info.version == 12) keep_session(t);
	return AK_TLS_DONE;
}

// resuming reports whether ssl's handshake, so far, resumes a session:
// TLS 1.3's PSK, TLS 1.2's ticket or session ID taken.
static bool resuming(mbedtls_ssl_context *ssl) {
	if (!ssl->handshake) return false;
	if (ssl->tls_version == MBEDTLS_SSL_VERSION_TLS1_3) return mbedtls_ssl_tls13_key_exchange_mode_with_psk(ssl);
	return ssl->handshake->resume != 0;
}

// handshake is mbedtls_ssl_handshake, step by step, to see before its
// last whether it resumed (its state goes with it).
static int handshake(ak_tls *t) {
	mbedtls_ssl_context *ssl = &t->ssl;
	while (ssl->state != MBEDTLS_SSL_HANDSHAKE_OVER) {
		t->info.resumed = resuming(ssl);
		int r = mbedtls_ssl_handshake_step(ssl);
		if (r != 0) return r;
	}
	return 0;
}

// resumed_check checks a resumed session's certificate as this side's
// configuration does what's left to check of it: its name, its dates
// (its chain was checked as its full handshake was).
static int resumed_check(ak_tls *t) {
	const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&t->ssl);
	char whose[300];
	snprintf(whose, sizeof whose, "%s's certificate", t->host);
	uint32_t flags = 0;
	if (peer) mbedtls_x509_crt_verify((mbedtls_x509_crt *)peer, NULL, NULL, t->host, &flags, NULL, NULL);
	int p = !peer ? 6 : mbedtls_x509_time_is_past(&peer->valid_to) ? 1 : mbedtls_x509_time_is_future(&peer->valid_from) ? 2 : flags & MBEDTLS_X509_BADCERT_CN_MISMATCH ? 3 : 0;
	if (p) {
		forget(t);
		untrusted(t, p, whose, NULL);
		return AK_TLS_FAIL;
	}
	return opened(t);
}

static int t_handshake(ak_tls *t) {
	switch (t->phase) {
	case PH_OPEN: return AK_TLS_DONE;
	case PH_FAILED: return AK_TLS_FAIL;
	case PH_VERIFY: return verified(t);
	}
	int r = handshake(t);
	if (r == MBEDTLS_ERR_SSL_WANT_READ) return AK_TLS_WANT_READ;
	if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return AK_TLS_WANT_WRITE;
	if (r != 0) {
		forget(t);
		return ssl_fail(t, r);
	}
	agreed(t);
	tls_conf *c = t->conf;
	if (c->server) {
		// A client's certificate that was given and isn't trusted (an
		// optional one's) is refused all the same.
		if (c->has_ca && mbedtls_ssl_get_peer_cert(&t->ssl)) {
			uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
			if (flags) {
				cert_fail(t, flags, "the client's certificate");
				return AK_TLS_FAIL;
			}
		}
		t->phase = PH_OPEN;
		return AK_TLS_DONE;
	}
	if (c->insecure) return opened(t);
	if (t->info.resumed) return resumed_check(t);
	const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&t->ssl);
	char whose[300];
	snprintf(whose, sizeof whose, "%s's certificate", t->host);
	if (!peer) {
		untrusted(t, 6, whose, NULL);
		return AK_TLS_FAIL;
	}
	if (c->system && ak_tls_system_verifier()) return verify_system(t);
	char why[300] = "";
	mbedtls_x509_crt *roots = c->has_ca ? &c->ca : ak_tls_system_roots(why, sizeof why);
	if (!roots) {
		fail(t, AK_NE_UNTRUSTED, "unknownIssuer:%s", why);
		mbedtls_ssl_send_alert_message(&t->ssl, MBEDTLS_SSL_ALERT_LEVEL_FATAL, MBEDTLS_SSL_ALERT_MSG_UNKNOWN_CA);
		return AK_TLS_FAIL;
	}
	uint32_t flags = 0;
	mbedtls_x509_crt_verify_with_profile((mbedtls_x509_crt *)peer, roots, NULL, &mbedtls_x509_crt_profile_default, t->host, &flags, NULL, NULL);
	if (flags) {
		cert_fail(t, flags, whose);
		return AK_TLS_FAIL;
	}
	return opened(t);
}

static int t_read(ak_tls *t, uint8_t *p, size_t n) {
	if (t->phase == PH_FAILED) return AK_TLS_FAIL;
	if (t->phase != PH_OPEN) return AK_TLS_WANT_READ;
	for (;;) {
		int r = mbedtls_ssl_read(&t->ssl, p, n);
		t->starved = r == MBEDTLS_ERR_SSL_WANT_READ;
		if (r > 0) return r;
		if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF) return AK_TLS_EOF;
		if (r == MBEDTLS_ERR_SSL_WANT_READ) return AK_TLS_WANT_READ;
		if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return AK_TLS_WANT_WRITE;
		if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
			keep_session(t);
			continue;
		}
		return ssl_fail(t, r);
	}
}

static int t_write(ak_tls *t, const uint8_t *p, size_t n) {
	if (t->phase == PH_FAILED) return AK_TLS_FAIL;
	if (t->phase != PH_OPEN) return AK_TLS_WANT_WRITE;
	int r = mbedtls_ssl_write(&t->ssl, p, n);
	if (r > 0) return r;
	if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return AK_TLS_WANT_WRITE;
	if (r == MBEDTLS_ERR_SSL_WANT_READ) return AK_TLS_WANT_WRITE;
	if (r == 0) return AK_TLS_WANT_WRITE;
	return ssl_fail(t, r);
}

static bool t_pending(const ak_tls *t) {
	if (t->phase != PH_OPEN) return false;
	return t->pre_off < t->pre.len || mbedtls_ssl_get_bytes_avail(&t->ssl) > 0 || (!t->starved && mbedtls_ssl_check_pending(&t->ssl));
}

static void t_shutdown(ak_tls *t) {
	if (t->phase == PH_OPEN) mbedtls_ssl_close_notify(&t->ssl);
}

static void t_close(ak_tls *t) {
	if (t->box) {
		ak_mutex_lock(&t->box->mu);
		t->box->notify = NULL;
		ak_mutex_unlock(&t->box->mu);
		box_release(t->box);
	}
	mbedtls_ssl_free(&t->ssl);
	ak_buf_free(&t->pre);
	ak_vec_free(&t->certs);
	ak_vec_free(&t->lens);
	ak_free(t->host);
	ak_free(t->cache_key);
	ak_free(t->chain);
	ak_free(t);
}

static ak_net_err t_error(const ak_tls *t, char *detail, size_t cap) {
	snprintf(detail, cap, "%s", t->why);
	return t->err ? t->err : AK_NE_TLS;
}

static void t_info(const ak_tls *t, ak_tls_info *out, const uint8_t *const **certs, const size_t **lens, uint32_t *n) {
	*out = t->info;
	*certs = t->certs.data;
	*lens = t->lens.data;
	*n = t->certs.len;
}

static const ak_tls_ops layer = {
	.open = t_open,
	.preload = t_preload,
	.handshake = t_handshake,
	.read = t_read,
	.write = t_write,
	.pending = t_pending,
	.shutdown = t_shutdown,
	.close = t_close,
	.error = t_error,
	.info = t_info,
};

// ---- The builtins on connections ----

static ak_future *failed(const char *why) {
	throw_invalid(why);
	ak_future *f = ak_future_new(AK_K_REF);
	ak_future_reject(f, ak_error_take());
	return f;
}

static ak_val start_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!ready()) {
		ak_future *f = ak_future_new(AK_K_REF);
		ak_future_reject(f, ak_error_take());
		return (ak_val){.p = f};
	}
	int64_t config = args[1].i;
	const ak_str *name = STR(2);
	bool server = args[3].i != 0;
	tls_conf *c = find_conf(config);
	if (!c) return (ak_val){.p = failed("no such TLS configuration")};
	if (c->server != server) return (ak_val){.p = failed(server ? "a server's TLS takes net.serverTLS's configuration" : "a client's TLS takes net.clientTLS's configuration")};
	if (strlen(name->data) != name->len) return (ak_val){.p = failed("a server name is a host's name or an IP address")};
	if (!server && !c->insecure && name->len == 0) return (ak_val){.p = failed("a server name is needed to check its certificate against")};
	return (ak_val){.p = ak_net_tls_start(args[0].i, config, name->data, server, args[4].i)};
}

static ak_val handshake_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = ak_net_tls_start(args[0].i, 0, "", false, args[1].i)};
}

const ak_lib_entry ak_std_tls[] = {
	{"tls.clientConfig", client_fn},
	{"tls.serverConfig", server_fn},
	{"tls.upgrade", start_fn},
	{"tls.awaitHandshake", handshake_fn},
	{NULL, NULL},
};

#endif
