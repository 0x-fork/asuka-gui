// tls.h: std/net's TLS (tls.c), as the threads that own sockets call it:
// std/net's net thread (net.c) and each HTTP server's (httpserver.c). A
// session sits between a connection's socket and its buffers: it reads
// and writes the socket itself (non-blocking), and what's above it reads
// and writes plaintext.
//
// The layer is reached through ak_tls_layer, which tls.c sets as an app
// first calls one of its builtins (a configuration's: tls.client,
// tls.server): nothing else names tls.c, so an app that never calls them
// doesn't link it, or Mbed TLS under it (core/link/link.c's
// AK_USES_TLS). A configuration's handle (> 0) exists only once one was
// made, so a socket that has one finds the layer set.
#ifndef AK_TLS_H
#define AK_TLS_H

#if !defined(__wasm__)
#include "std/sock.h"
#endif
#include "std/net.h"

typedef struct ak_tls ak_tls;

// What a step of a session says, beside a count of bytes (> 0).
enum {
	AK_TLS_DONE = 0,         // the handshake is done (and the certificate checked)
	AK_TLS_WANT_READ = -1,   // again once the socket is readable
	AK_TLS_WANT_WRITE = -2,  // again once the socket is writable
	AK_TLS_WAIT = -3,        // the certificate is being checked on a thread: again once notified
	AK_TLS_EOF = -4,         // the other side's end (a read's)
	AK_TLS_FAIL = -5,        // it failed: error says why
};

// What a finished handshake agreed: ak_tls_layer's state.
typedef struct {
	int version;        // 12, 13
	char cipher[64];    // the cipher suite's IANA name: "TLS_AES_128_GCM_SHA256"
	char protocol[64];  // ALPN's: "h2", "" for none
	char server_name[256];  // the name the client asked for (SNI): the server's; "" none
	bool resumed;           // a session resumed: no certificate was sent, none checked but its name and dates
} ak_tls_info;

#if !defined(__wasm__)
typedef struct {
	// open starts a session over fd for configuration config (a
	// tls.client's or a tls.server's handle; server says which side),
	// the client's verifying server_name (a host name or an IP address,
	// which goes in SNI unless it's an IP). A client checked by the
	// system's trust store checks on a thread of its own: notify(ctx) is
	// called on it when that's done (the step after says how it went),
	// with the scheduler held for the caller to let go of once it has
	// stepped (ak_sched_unhold). NULL, with err set, when it can't.
	ak_tls *(*open)(int64_t config, bool server, const char *server_name, ak_sock fd, void (*notify)(void *ctx), void *ctx, char *err, size_t cap);
	// preload gives it bytes already read from the socket before TLS
	// began (a STARTTLS's), read first.
	void (*preload)(ak_tls *t, const uint8_t *p, size_t n);
	// handshake moves the handshake on: DONE, WANT_READ, WANT_WRITE,
	// WAIT, FAIL.
	int (*handshake)(ak_tls *t);
	// read reads plaintext into p (at most n): a count, EOF, WANT_READ,
	// WANT_WRITE, FAIL.
	int (*read)(ak_tls *t, uint8_t *p, size_t n);
	// write writes plaintext from p[0:n]: a count (it may take less than
	// n: call again with the rest), WANT_WRITE, FAIL. After WANT_WRITE,
	// the next call passes the same bytes.
	int (*write)(ak_tls *t, const uint8_t *p, size_t n);
	// pending reports whether a read would get something without the
	// socket: plaintext or a record it holds already.
	bool (*pending)(const ak_tls *t);
	// shutdown sends close_notify, as far as the socket takes it now.
	void (*shutdown)(ak_tls *t);
	// close ends the session (not the socket).
	void (*close)(ak_tls *t);
	// error is why it failed (after FAIL): the NetError case, and its
	// detail ("<problem>:<why>" for UNTRUSTED).
	ak_net_err (*error)(const ak_tls *t, char *detail, size_t cap);
	// info is what the handshake agreed (after DONE), and the other
	// side's certificates (DER, the leaf first): *ncerts of them, each
	// certs[i] of lens[i] bytes, the session's.
	void (*info)(const ak_tls *t, ak_tls_info *out, const uint8_t *const **certs, const size_t **lens, uint32_t *ncerts);
} ak_tls_ops;

extern const ak_tls_ops *ak_tls_layer;

// ---- The system's trust store (tlstrust.c) ----

struct mbedtls_x509_crt;

// ak_tls_system_verifier reports whether the system checks certificates
// itself: Apple's (SecTrust) and Windows' (CertGetCertificateChain).
bool ak_tls_system_verifier(void);
// ak_tls_system_verify checks a server's chain (n certificates, DER, the
// leaf first) for host (a name or an IP address) as the system does:
// true when it trusts it; else *problem (tls.c's problems: 1 expired, 2
// not yet valid, 3 the wrong host, 4 an unknown issuer, 5 revoked, 7
// another) and why, the system's words. It may block (fetching
// intermediates, revocation): a thread of its own calls it.
bool ak_tls_system_verify(const char *host, const uint8_t *const *der, const size_t *len, uint32_t n, int *problem, char *why, size_t cap);
// ak_tls_system_roots is the system's trusted authorities, read once
// (any thread): Linux's bundle (SSL_CERT_FILE, SSL_CERT_DIR, the usual
// paths, as Go reads them), Android's store. NULL when there are none,
// why saying where it looked.
struct mbedtls_x509_crt *ak_tls_system_roots(char *why, size_t cap);

// For tests: seconds added to the clock a client's kept sessions expire
// by, and how many sessions client configuration config keeps (-1: no
// such configuration).
extern int64_t ak_tls_clock_shift;
int ak_tls_cached(int64_t config);
#endif

// ak_net_tls_start upgrades connection h (std/net's) to TLS on the net
// thread, with configuration config as the client (verifying
// server_name) or the server, and returns the future of its handshake
// (timeout_ns > 0: given up past it): what it agreed, as std/net's Asuka
// reads it (tls.c's tls.upgrade). config 0: h is TLS already (a TLS
// listener's): its handshake's. tls.c's builtins call it; it's net.c's.
struct ak_future *ak_net_tls_start(int64_t h, int64_t config, const char *server_name, bool server, int64_t timeout_ns);

#endif
