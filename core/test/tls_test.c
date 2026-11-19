// std/net's TLS (std/tls.c over Mbed TLS) on loopback, a client and a
// server in one process, the net thread between them, with the test
// certificates (testdata/tls, gen.go's): TLS 1.3 and 1.2 handshakes, what
// they agreed (ALPN, the cipher suite, the server's choice by SNI),
// plaintext of every size through them, a STARTTLS upgrade, a client's
// certificate (required, and not given), and the failures that say why:
// the wrong host, an expired certificate, an unknown authority (the
// test's own, and the system's trust store), no version in common, a
// server that doesn't speak TLS. And configurations that can't be made
// (an encrypted key or OpenSSH's: std/crypto's to read); sessions
// resumed (TLS 1.3's tickets, TLS 1.2's), counted: kept per configuration,
// server name and port, bounded, expired, and not where tickets are off.
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "t.h"
#include "rt/rt.h"
#include "rt/sched.h"
#include "rt/lib.h"
#include "std/std.h"
#include "std/net.h"
#include "std/tls.h"
#include "signal/signal.h"
#include "base/thread.h"

static ak_lib_fn lib(const char *name) {
	for (const ak_lib_entry *e = ak_std_net; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	for (const ak_lib_entry *e = ak_std_tls; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	T_FAIL("no builtin %s", name);
	return NULL;
}

static void pump(void) {
	ak_sched_wait_posted();
	ak_sched_run_posted();
	ak_sched_run();
}

static bool settle(ak_future *f) {
	int64_t until = ak_now_ns() + INT64_C(20000000000);
	while (f->state == AK_FUT_PENDING && ak_now_ns() < until) pump();
	return f->state != AK_FUT_PENDING;
}

static const char *failure(ak_future *f) {
	static char b[600];
	if (f->state != AK_FUT_FAILED) return "";
	snprintf(b, sizeof b, "%s", ak_error_message(f->error)->data);
	return b;
}

static ak_val str(const char *s) { return (ak_val){.p = ak_str_c(s)}; }

static ak_str *file(const char *name) {
	char path[256];
	snprintf(path, sizeof path, "testdata/tls/%s", name);
	FILE *f = fopen(path, "rb");
	if (!f) {
		T_FAIL("no %s", path);
		return ak_str_c("");
	}
	char b[8192];
	size_t n = fread(b, 1, sizeof b, f);
	fclose(f);
	return ak_str_new(b, n);
}

static ak_arr *strs(const char *const *s, int n) {
	ak_arr *a = ak_str_arr((uint32_t)n);
	for (int i = 0; i < n; i++) ak_str_arr_push(&a, s[i], strlen(s[i]));
	return a;
}

// client_with makes a client's configuration (ca, certificate and key:
// files of testdata/tls, or NULL; how many servers' sessions it keeps);
// its error into err, 0.
static int64_t client_with(const char *ca, const char *cert, const char *key, const char *alpn, int minv, int maxv, bool insecure, int cache, char *err) {
	const char *protos[1] = {alpn};
	ak_val a[8] = {{.p = ca ? file(ca) : ak_str_c("")}, {.p = cert ? file(cert) : ak_str_c("")}, {.p = key ? file(key) : ak_str_c("")},
		{.p = strs(protos, alpn ? 1 : 0)}, {.i = minv}, {.i = maxv}, {.i = insecure}, {.i = cache}};
	int64_t h = lib("tls.clientConfig")(a, 8).i;
	for (int i = 0; i < 4; i++) ak_release(a[i].p);
	if (err) err[0] = 0;
	if (ak_error_pending()) {
		void *e = ak_error_take();
		if (err) snprintf(err, 300, "%s", ak_error_message(e)->data);
		ak_release(e);
		return 0;
	}
	return h;
}

static int64_t client(const char *ca, const char *cert, const char *key, const char *alpn, int minv, int maxv, bool insecure, char *err) {
	return client_with(ca, cert, key, alpn, minv, maxv, insecure, 64, err);
}

// server_with makes a server's configuration of pairs (certificate and
// key files: n of each), with tickets or none.
static int64_t server_with(const char *const *pairs, int n, const char *alpn, const char *client_ca, bool require, int minv, int maxv, bool tickets, char *err) {
	ak_arr *p = ak_str_arr((uint32_t)(2 * n));
	for (int i = 0; i < 2 * n; i++) {
		ak_str *s = file(pairs[i]);
		ak_str_arr_push(&p, s->data, s->len);
		ak_release(s);
	}
	const char *protos[1] = {alpn};
	ak_val a[7] = {{.p = p}, {.p = strs(protos, alpn ? 1 : 0)}, {.p = client_ca ? file(client_ca) : ak_str_c("")}, {.i = require}, {.i = minv}, {.i = maxv}, {.i = tickets}};
	int64_t h = lib("tls.serverConfig")(a, 7).i;
	for (int i = 0; i < 3; i++) ak_release(a[i].p);
	if (err) err[0] = 0;
	if (ak_error_pending()) {
		void *e = ak_error_take();
		if (err) snprintf(err, 300, "%s", ak_error_message(e)->data);
		ak_release(e);
		return 0;
	}
	return h;
}

static int64_t server(const char *const *pairs, int n, const char *alpn, const char *client_ca, bool require, int minv, int maxv, char *err) {
	return server_with(pairs, n, alpn, client_ca, require, minv, maxv, true, err);
}

static int64_t call_int(const char *name, ak_val *args, uint32_t n) {
	ak_future *f = lib(name)(args, n).p;
	int64_t v = -1;
	if (!settle(f)) T_FAIL("%s never settled", name);
	else if (f->state == AK_FUT_FAILED) T_FAIL("%s: %s", name, failure(f));
	else v = f->result.i;
	ak_release(f);
	return v;
}

// listen_on listens at 127.0.0.1 (with a TLS configuration, or 0): its
// handle, and its port in *port.
static int64_t listen_on(int64_t tls, int64_t *port) {
	ak_val la[3] = {str("127.0.0.1"), {.i = 0}, {.i = tls}};
	int64_t l = call_int("net.tcpListen", la, 3);
	ak_release(la[0].p);
	ak_val ha = {.i = l};
	ak_arr *addrs = lib("net.addresses")(&ha, 1).p;
	ak_str_to_int(addrs->data[1].p, port);
	ak_release(addrs);
	return l;
}

// pair connects to l's port and accepts: *c the client's, *s the server's.
static bool pair(int64_t l, int64_t port, int64_t *c, int64_t *s) {
	ak_val ca[5] = {str("127.0.0.1"), {.i = port}, {.i = 0}, {.i = 1}, {.i = 0}};
	ak_future *cf = lib("net.tcpConnect")(ca, 5).p;
	ak_release(ca[0].p);
	ak_val aa[2] = {{.i = l}, {.i = 0}};
	ak_future *af = lib("net.tcpAccept")(aa, 2).p;
	bool ok = settle(cf) && settle(af) && cf->state == AK_FUT_DONE && af->state == AK_FUT_DONE;
	if (ok) {
		*c = cf->result.i;
		*s = af->result.i;
	} else {
		T_FAIL("connect: %s, accept: %s", failure(cf), failure(af));
	}
	ak_release(cf);
	ak_release(af);
	return ok;
}

static ak_future *upgrade(int64_t h, int64_t config, const char *name, bool as_server) {
	ak_val a[5] = {{.i = h}, {.i = config}, str(name), {.i = as_server}, {.i = 0}};
	ak_future *f = lib("tls.upgrade")(a, 5).p;
	ak_release(a[2].p);
	return f;
}

static ak_future *handshake(int64_t h) {
	ak_val a[2] = {{.i = h}, {.i = 0}};
	return lib("tls.awaitHandshake")(a, 2).p;
}

// field is a handshake's answer's field i as text.
static const char *field(ak_future *f, uint32_t i) {
	static char b[4][256];
	static int k;
	if (f->state != AK_FUT_DONE) return "<failed>";
	ak_arr *a = f->result.p;
	if (i >= a->len) return "<none>";
	ak_bytes *x = a->data[i].p;
	char *out = b[k++ % 4];
	snprintf(out, 256, "%.*s", (int)x->len, (const char *)x->data);
	return out;
}

static uint32_t nfields(ak_future *f) { return f->state == AK_FUT_DONE ? ((ak_arr *)f->result.p)->len : 0; }

static ak_future *read_op(int64_t h, int mode, int64_t n, const char *delim) {
	ak_val a[5] = {{.i = h}, {.i = mode}, {.i = n}, {.p = ak_bytes_new(delim, strlen(delim))}, {.i = 0}};
	ak_future *f = lib("net.tcpRead")(a, 5).p;
	ak_release(a[3].p);
	return f;
}

static const char *read_text(int64_t h, int mode, int64_t n, const char *delim) {
	static char out[512];
	ak_future *f = read_op(h, mode, n, delim);
	if (!settle(f)) snprintf(out, sizeof out, "<pending>");
	else if (f->state == AK_FUT_FAILED) snprintf(out, sizeof out, "!%s", failure(f));
	else if (!f->result.p) snprintf(out, sizeof out, "<none>");
	else snprintf(out, sizeof out, "%.*s", (int)((ak_bytes *)f->result.p)->len, (char *)((ak_bytes *)f->result.p)->data);
	ak_release(f);
	return out;
}

static ak_future *write_op(int64_t h, const void *p, size_t n) {
	ak_val a[3] = {{.i = h}, {.p = ak_bytes_new(p, n)}, {.i = 0}};
	ak_future *f = lib("net.tcpWrite")(a, 3).p;
	ak_release(a[1].p);
	return f;
}

static void write_text(int64_t h, const char *s) {
	ak_future *f = write_op(h, s, strlen(s));
	if (!settle(f) || f->state != AK_FUT_DONE) T_FAIL("write: %s", failure(f));
	ak_release(f);
}

static void close_h(int64_t h) {
	ak_val a = {.i = h};
	lib("net.sockClose")(&a, 1);
}

static void drain(void) {
	while (ak_sched_held()) pump();
}

static const char *const server_pair[] = {"server.pem", "server.key"};

// hs runs a client's and a server's handshakes at once over a new pair of
// connections to l: the client's future in *cf, the server's in *sf.
static void hs(int64_t l, int64_t port, int64_t cconf, const char *name, int64_t *c, int64_t *s, ak_future **cf, ak_future **sf) {
	*cf = *sf = NULL;
	if (!pair(l, port, c, s)) return;
	*cf = upgrade(*c, cconf, name, false);
	*sf = handshake(*s);
	settle(*cf);
	settle(*sf);
}

TEST(tls_handshakes) {
	ak_signal_bind_thread();
	char err[300];
	const char *const pairs[] = {"server.pem", "server.key", "rsa.pem", "rsa.key"};
	int64_t sconf = server(pairs, 2, "http/1.1", NULL, false, 12, 13, err);
	CHECK_EQ_STR(err, "");
	REQUIRE(sconf > 0);
	int64_t port, l = listen_on(sconf, &port);
	REQUIRE(l > 0);

	// TLS 1.3, the chain checked against the test's root, ALPN agreed.
	int64_t cconf = client("ca.pem", NULL, NULL, "http/1.1", 12, 13, false, err);
	CHECK_EQ_STR(err, "");
	REQUIRE(cconf > 0);
	CHECK_EQ_INT(client("ca.pem", NULL, NULL, "http/1.1", 12, 13, false, NULL), cconf);  // the same inputs, the same one
	int64_t c, s;
	ak_future *cf, *sf;
	hs(l, port, cconf, "localhost", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK_EQ_STR(failure(cf), "");
	CHECK_EQ_STR(failure(sf), "");
	CHECK_EQ_STR(field(cf, 0), "13");
	CHECK(strncmp(field(cf, 1), "TLS_AES_", 8) == 0 || strcmp(field(cf, 1), "TLS_CHACHA20_POLY1305_SHA256") == 0);
	CHECK_EQ_STR(field(cf, 2), "http/1.1");
	CHECK_EQ_STR(field(cf, 3), "localhost");
	CHECK_EQ_INT(nfields(cf), 7);  // the leaf and the intermediate
	CHECK_EQ_STR(field(sf, 0), "13");
	CHECK_EQ_STR(field(sf, 2), "http/1.1");
	CHECK_EQ_STR(field(sf, 3), "localhost");
	CHECK_EQ_INT(nfields(sf), 5);  // no client certificate
	ak_release(cf);
	ak_release(sf);

	// Plaintext both ways: lines, exact counts, and a megabyte (many
	// records) while the other side reads.
	write_text(c, "hello\r\nworld\n");
	CHECK_EQ_STR(read_text(s, 2, 100, "\n"), "hello\r\n");
	CHECK_EQ_STR(read_text(s, 1, 6, ""), "world\n");
	write_text(s, "back");
	CHECK_EQ_STR(read_text(c, 1, 4, ""), "back");
	size_t big = (size_t)1 << 20;
	uint8_t *data = malloc(big);
	for (size_t i = 0; i < big; i++) data[i] = (uint8_t)(i * 7 + i / 251);
	ak_future *wf = write_op(c, data, big);
	ak_future *rf = read_op(s, 1, (int64_t)big, "");
	REQUIRE(settle(rf) && settle(wf));
	CHECK_EQ_STR(failure(wf), "");
	REQUIRE(rf->state == AK_FUT_DONE);
	ak_bytes *got = rf->result.p;
	CHECK(got->len == big && memcmp(got->data, data, big) == 0);
	ak_release(rf);
	ak_release(wf);
	free(data);

	// A half close: close_notify, then the other side's end.
	ak_val cw = {.i = c};
	lib("net.tcpCloseWrite")(&cw, 1);
	CHECK_EQ_STR(read_text(s, 0, 100, ""), "<none>");
	write_text(s, "still");
	CHECK_EQ_STR(read_text(c, 1, 5, ""), "still");
	close_h(c);
	close_h(s);

	// The name the client asks for picks the server's certificate: RSA's,
	// over TLS 1.2 (an ECDHE-RSA suite).
	int64_t c12 = client("ca.pem", NULL, NULL, NULL, 12, 12, false, err);
	REQUIRE(c12 > 0);
	hs(l, port, c12, "rsa.example", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK_EQ_STR(failure(cf), "");
	CHECK_EQ_STR(field(cf, 0), "12");
	CHECK(strncmp(field(cf, 1), "TLS_ECDHE_RSA_WITH_", 19) == 0);
	CHECK_EQ_STR(field(cf, 2), "");
	CHECK_EQ_STR(field(sf, 3), "rsa.example");
	ak_release(cf);
	ak_release(sf);
	write_text(c, "tls12");
	CHECK_EQ_STR(read_text(s, 1, 5, ""), "tls12");
	close_h(c);
	close_h(s);

	// An IP address is checked against the certificate's IP addresses.
	hs(l, port, cconf, "127.0.0.1", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK_EQ_STR(failure(cf), "");
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	close_h(l);
	drain();
}

// fails runs a handshake of client configuration cconf, for name, with a
// server of sconf, and says what the client's failed with.
static void fails(int64_t sconf, int64_t cconf, const char *name, const char *want_client, const char *want_server) {
	int64_t port, l = listen_on(sconf, &port);
	int64_t c, s;
	ak_future *cf, *sf;
	hs(l, port, cconf, name, &c, &s, &cf, &sf);
	if (!cf || !sf) return;
	const char *got = failure(cf);
	if (strncmp(got, want_client, strlen(want_client)) != 0) T_FAIL("client: %s\nwant: %s...", got, want_client);
	if (want_server) {
		// TLS 1.3's server finishes before the client checks what it
		// got: it hears the client's alert as it reads.
		char r[600];
		snprintf(r, sizeof r, "%s", sf->state == AK_FUT_DONE ? read_text(s, 0, 10, "") + 1 : failure(sf));
		if (strncmp(r, want_server, strlen(want_server)) != 0) T_FAIL("server: %s\nwant: %s...", r, want_server);
	}
	// What waits on a connection whose handshake failed fails with it.
	if (cf->state == AK_FUT_FAILED) {
		char r[600];
		snprintf(r, sizeof r, "%s", read_text(c, 0, 10, ""));
		if (strncmp(r + 1, want_client, strlen(want_client)) != 0) T_FAIL("read after: %s", r);
	}
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	close_h(l);
	drain();
}

TEST(tls_untrusted) {
	ak_signal_bind_thread();
	char err[300];
	int64_t sconf = server(server_pair, 1, NULL, NULL, false, 12, 13, err);
	int64_t cconf = client("ca.pem", NULL, NULL, NULL, 12, 13, false, err);
	REQUIRE(sconf > 0 && cconf > 0);
	fails(sconf, cconf, "wrong.example", "std/net:untrustedCertificate:wrongHost:wrong.example's certificate is for localhost, test.example, 127.0.0.1, ::1, not wrong.example", NULL);
	fails(sconf, cconf, "10.0.0.1", "std/net:untrustedCertificate:wrongHost:", NULL);
	const char *const expired[] = {"expired.pem", "expired.key"};
	int64_t econf = server(expired, 1, NULL, NULL, false, 12, 13, err);
	REQUIRE(econf > 0);
	fails(econf, cconf, "localhost", "std/net:untrustedCertificate:expired:localhost's certificate expired on 2001-01-01", NULL);
	int64_t other = client("other-ca.pem", NULL, NULL, NULL, 12, 13, false, err);
	REQUIRE(other > 0);
	fails(sconf, other, "localhost", "std/net:untrustedCertificate:unknownIssuer:localhost's certificate is signed by an authority that isn't trusted (Asuka Test Intermediate CA)", "std/net:tls:the other side doesn't trust");
	// The system's authorities don't have the test's root.
	int64_t sys = client(NULL, NULL, NULL, NULL, 12, 13, false, err);
	REQUIRE(sys > 0);
	fails(sconf, sys, "localhost", "std/net:untrustedCertificate:unknownIssuer:", NULL);
	// Unless nothing's checked.
	int64_t insecure = client(NULL, NULL, NULL, NULL, 12, 13, true, err);
	REQUIRE(insecure > 0);
	int64_t port, l = listen_on(sconf, &port);
	int64_t c, s;
	ak_future *cf, *sf;
	hs(l, port, insecure, "anything", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK_EQ_STR(failure(cf), "");
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	close_h(l);
	drain();
}

TEST(tls_versions_and_protocols) {
	ak_signal_bind_thread();
	char err[300];
	// A server of TLS 1.3 alone, a client of 1.2 alone.
	int64_t s13 = server(server_pair, 1, NULL, NULL, false, 13, 13, err);
	int64_t c12 = client("ca.pem", NULL, NULL, NULL, 12, 12, false, err);
	REQUIRE(s13 > 0 && c12 > 0);
	fails(s13, c12, "localhost", "std/net:tls:", "std/net:tls:");
	// No ALPN protocol in common.
	int64_t sh2 = server(server_pair, 1, "h2", NULL, false, 12, 13, err);
	int64_t chttp = client("ca.pem", NULL, NULL, "http/1.1", 12, 13, false, err);
	REQUIRE(sh2 > 0 && chttp > 0);
	fails(sh2, chttp, "localhost", "std/net:tls:no application protocol (ALPN) in common", "std/net:tls:no application protocol (ALPN) in common");
	drain();
}

TEST(tls_not_tls) {
	ak_signal_bind_thread();
	char err[300];
	int64_t cconf = client("ca.pem", NULL, NULL, NULL, 12, 13, false, err);
	REQUIRE(cconf > 0);
	// A server that answers in plaintext.
	int64_t port, l = listen_on(0, &port);
	int64_t c, s;
	REQUIRE(pair(l, port, &c, &s));
	ak_future *cf = upgrade(c, cconf, "localhost", false);
	CHECK_EQ_STR(read_text(s, 1, 1, ""), "\x16");  // a ClientHello's record
	write_text(s, "HTTP/1.1 400 Bad Request\r\n\r\n");
	REQUIRE(settle(cf));
	CHECK_EQ_STR(failure(cf), "std/net:tls:what the other side sent isn't TLS (is it TLS there, on that port?)");
	ak_release(cf);
	close_h(c);
	close_h(s);
	// One that closes.
	REQUIRE(pair(l, port, &c, &s));
	cf = upgrade(c, cconf, "localhost", false);
	close_h(s);
	REQUIRE(settle(cf));
	const char *f = failure(cf);
	CHECK(strcmp(f, "std/net:tls:the other side closed the connection during the handshake") == 0 || strcmp(f, "std/net:reset:") == 0);
	ak_release(cf);
	close_h(c);
	close_h(l);
	drain();
}

TEST(tls_starttls) {
	ak_signal_bind_thread();
	char err[300];
	int64_t sconf = server(server_pair, 1, NULL, NULL, false, 12, 13, err);
	int64_t cconf = client("ca.pem", NULL, NULL, NULL, 12, 13, false, err);
	REQUIRE(sconf > 0 && cconf > 0);
	int64_t port, l = listen_on(0, &port);
	int64_t c, s;
	REQUIRE(pair(l, port, &c, &s));
	// A plaintext exchange first, as SMTP's.
	write_text(s, "220 ready\r\n");
	CHECK_EQ_STR(read_text(c, 2, 100, "\n"), "220 ready\r\n");
	write_text(c, "STARTTLS\r\n");
	CHECK_EQ_STR(read_text(s, 2, 100, "\n"), "STARTTLS\r\n");
	write_text(s, "220 go ahead\r\n");
	CHECK_EQ_STR(read_text(c, 2, 100, "\n"), "220 go ahead\r\n");
	ak_future *cf = upgrade(c, cconf, "localhost", false);
	ak_future *sf = upgrade(s, sconf, "", true);
	REQUIRE(settle(cf) && settle(sf));
	CHECK_EQ_STR(failure(cf), "");
	CHECK_EQ_STR(failure(sf), "");
	CHECK_EQ_STR(field(sf, 0), "13");
	ak_release(cf);
	ak_release(sf);
	write_text(c, "EHLO secure\r\n");
	CHECK_EQ_STR(read_text(s, 2, 100, "\n"), "EHLO secure\r\n");
	// Twice is an error.
	cf = upgrade(c, cconf, "localhost", false);
	REQUIRE(settle(cf));
	CHECK_EQ_STR(failure(cf), "std/net:invalid:the connection speaks TLS already");
	ak_release(cf);
	close_h(c);
	close_h(s);
	close_h(l);
	drain();
}

TEST(tls_client_certificates) {
	ak_signal_bind_thread();
	char err[300];
	int64_t sconf = server(server_pair, 1, NULL, "ca.pem", true, 12, 13, err);
	CHECK_EQ_STR(err, "");
	int64_t with = client("ca.pem", "client.pem", "client.key", NULL, 12, 13, false, err);
	CHECK_EQ_STR(err, "");
	int64_t without = client("ca.pem", NULL, NULL, NULL, 12, 13, false, err);
	REQUIRE(sconf > 0 && with > 0 && without > 0);
	int64_t port, l = listen_on(sconf, &port);
	int64_t c, s;
	ak_future *cf, *sf;
	hs(l, port, with, "localhost", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK_EQ_STR(failure(cf), "");
	CHECK_EQ_STR(failure(sf), "");
	CHECK_EQ_INT(nfields(sf), 7);  // the client's leaf and intermediate
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	close_h(l);
	// Required, and not given: the server says so.
	int64_t port2, l2 = listen_on(sconf, &port2);
	hs(l2, port2, without, "localhost", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK_EQ_STR(failure(sf), "std/net:untrustedCertificate:missing:the client gave no certificate, and one is required");
	// TLS 1.3's client finishes before the server checks: it hears as it
	// reads.
	if (cf->state == AK_FUT_DONE) CHECK(strncmp(read_text(c, 0, 10, ""), "!std/net:tls:", 13) == 0);
	else CHECK(strncmp(failure(cf), "std/net:tls:", 12) == 0);
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	close_h(l2);
	drain();
}

TEST(tls_configurations) {
	ak_signal_bind_thread();
	char err[300];
	const char *const bad_key[] = {"server.pem", "rsa.key"};
	CHECK_EQ_INT(server(bad_key, 1, NULL, NULL, false, 12, 13, err), 0);
	CHECK_EQ_STR(err, "std/net:invalid:the key isn't the certificate's (its first, the leaf's)");
	const char *const no_cert[] = {"server.key", "server.key"};
	CHECK_EQ_INT(server(no_cert, 1, NULL, NULL, false, 12, 13, err), 0);
	CHECK_EQ_STR(err, "std/net:invalid:the certificate has no PEM certificate (-----BEGIN CERTIFICATE-----)");
	CHECK_EQ_INT(client("ca.pem", "client.pem", NULL, NULL, 12, 13, false, err), 0);
	CHECK_EQ_STR(err, "std/net:invalid:a client's certificate goes with its key: both, or neither");
	CHECK_EQ_INT(client("ca.pem", NULL, NULL, NULL, 13, 12, false, err), 0);
	CHECK_EQ_STR(err, "std/net:invalid:minVersion is past maxVersion");
	// A client's configuration isn't a server's.
	int64_t cconf = client("ca.pem", NULL, NULL, NULL, 12, 13, false, err);
	REQUIRE(cconf > 0);
	int64_t port, l = listen_on(0, &port);
	int64_t c, s;
	REQUIRE(pair(l, port, &c, &s));
	ak_future *f = upgrade(s, cconf, "", true);
	REQUIRE(settle(f));
	CHECK_EQ_STR(failure(f), "std/net:invalid:a server's TLS takes net.serverTLS's configuration");
	ak_release(f);
	f = handshake(c);
	REQUIRE(settle(f));
	CHECK_EQ_STR(failure(f), "std/net:invalid:the connection doesn't speak TLS");
	ak_release(f);
	close_h(c);
	close_h(s);
	close_h(l);
	drain();
}

TEST(tls_encrypted_keys) {
	ak_signal_bind_thread();
	char err[300];
	// An encrypted key, or OpenSSH's, is std/crypto's to read: the error
	// says how (testdata/keys: ../tls's keys, gen.sh's).
	const char *const enc[] = {"server.pem", "../keys/tls-server-pbes2.pem"};
	CHECK_EQ_INT(server(enc, 1, NULL, NULL, false, 12, 13, err), 0);
	CHECK_EQ_STR(err, "std/net:invalid:the key is encrypted: give it decrypted, what crypto.decryptPrivateKey(key, password:) reads, as its pem()");
	CHECK_EQ_INT(client("ca.pem", "client.pem", "../keys/tls-client-openssh.pem", NULL, 12, 13, false, err), 0);
	CHECK_EQ_STR(err, "std/net:invalid:the key is OpenSSH's: give it as PKCS#8, what crypto.privateKey(key).pem() makes of it (crypto.decryptPrivateKey's for an encrypted one)");
	const char *const trad[] = {"rsa.pem", "../keys/rsa-trad-aes256.pem"};
	CHECK_EQ_INT(server(trad, 1, NULL, NULL, false, 12, 13, err), 0);
	CHECK(strstr(err, "the key is encrypted") != NULL);
	drain();
}

// resumes runs a handshake of client configuration cconf with l's server
// for name, then the server writes a byte the client reads (a TLS 1.3
// client takes its tickets as it reads): 1 when the client's resumed, 0
// when it didn't, -1 when it failed; *sr whether the server's did.
static int resumes(int64_t l, int64_t port, int64_t cconf, const char *name, bool *sr) {
	int64_t c, s;
	ak_future *cf, *sf;
	hs(l, port, cconf, name, &c, &s, &cf, &sf);
	if (!cf || !sf) return -1;
	int r = -1;
	if (cf->state == AK_FUT_DONE && sf->state == AK_FUT_DONE) {
		r = strcmp(field(cf, 4), "1") == 0;
		if (sr) *sr = strcmp(field(sf, 4), "1") == 0;
		// What the server sent then is what it was sent now.
		if (nfields(cf) != 7) T_FAIL("%s: %u fields", name, nfields(cf));
		write_text(s, "x");
		if (strcmp(read_text(c, 1, 1, ""), "x") != 0) T_FAIL("%s: no byte", name);
	} else {
		T_FAIL("%s: %s %s", name, failure(cf), failure(sf));
	}
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	return r;
}

TEST(tls_resumption) {
	ak_signal_bind_thread();
	char err[300];
	int64_t sconf = server(server_pair, 1, NULL, NULL, false, 12, 13, err);
	// (Configurations of their own: the same inputs give other tests' back.)
	int64_t cconf = client_with("ca.pem", NULL, NULL, NULL, 12, 13, false, 61, err);
	REQUIRE(sconf > 0 && cconf > 0);
	int64_t port, l = listen_on(sconf, &port);
	bool sr = false;
	// TLS 1.3: a full handshake, its server's two tickets kept; then one
	// taken and resumed (the server's side too), and one more given.
	CHECK_EQ_INT(resumes(l, port, cconf, "localhost", &sr), 0);
	CHECK(!sr);
	CHECK_EQ_INT(ak_tls_cached(cconf), 2);
	CHECK_EQ_INT(resumes(l, port, cconf, "localhost", &sr), 1);
	CHECK(sr);
	CHECK_EQ_INT(ak_tls_cached(cconf), 2);
	CHECK_EQ_INT(resumes(l, port, cconf, "localhost", NULL), 1);
	// Another name isn't the session's server, though one certificate
	// has both names.
	CHECK_EQ_INT(resumes(l, port, cconf, "test.example", NULL), 0);
	CHECK_EQ_INT(ak_tls_cached(cconf), 4);
	// Nor is another configuration's: one that trusts another authority
	// does a full handshake, and fails it.
	int64_t other = client_with("other-ca.pem", NULL, NULL, NULL, 12, 13, false, 61, err);
	REQUIRE(other > 0);
	int64_t c, s;
	ak_future *cf, *sf;
	hs(l, port, other, "localhost", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK(strncmp(failure(cf), "std/net:untrustedCertificate:unknownIssuer:", 43) == 0);
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	// Past a ticket's lifetime (the server's: a day) it isn't offered,
	// and goes.
	ak_tls_clock_shift = 24 * 3600 + 1;
	CHECK_EQ_INT(resumes(l, port, cconf, "localhost", NULL), 0);
	CHECK_EQ_INT(resumes(l, port, cconf, "localhost", NULL), 1);
	ak_tls_clock_shift = 0;

	// TLS 1.2: its session (a ticket) kept, and used again.
	int64_t c12 = client_with("ca.pem", NULL, NULL, NULL, 12, 12, false, 61, err);
	REQUIRE(c12 > 0);
	CHECK_EQ_INT(resumes(l, port, c12, "localhost", NULL), 0);
	CHECK_EQ_INT(ak_tls_cached(c12), 1);
	CHECK_EQ_INT(resumes(l, port, c12, "localhost", &sr), 1);
	CHECK(sr);
	CHECK_EQ_INT(resumes(l, port, c12, "localhost", NULL), 1);
	CHECK_EQ_INT(ak_tls_cached(c12), 1);

	// A configuration that keeps none resumes none.
	int64_t none = client_with("ca.pem", NULL, NULL, NULL, 12, 13, false, 0, err);
	REQUIRE(none > 0);
	CHECK_EQ_INT(resumes(l, port, none, "localhost", NULL), 0);
	CHECK_EQ_INT(resumes(l, port, none, "localhost", NULL), 0);
	CHECK_EQ_INT(ak_tls_cached(none), 0);

	// Two servers kept at most: a third's handshake drops the least
	// recently used's.
	int64_t two = client_with("ca.pem", NULL, NULL, NULL, 12, 13, false, 2, err);
	REQUIRE(two > 0);
	CHECK_EQ_INT(resumes(l, port, two, "localhost", NULL), 0);
	CHECK_EQ_INT(resumes(l, port, two, "test.example", NULL), 0);
	CHECK_EQ_INT(resumes(l, port, two, "localhost", NULL), 1);
	CHECK_EQ_INT(resumes(l, port, two, "127.0.0.1", NULL), 0);  // test.example's dropped
	CHECK_EQ_INT(ak_tls_cached(two), 4);
	CHECK_EQ_INT(resumes(l, port, two, "test.example", NULL), 0);
	CHECK_EQ_INT(resumes(l, port, two, "127.0.0.1", NULL), 1);

	// The server changes (TLS 1.2 alone now, on the same port): the
	// ticket offered (TLS 1.3's alone) fails the handshake, and its
	// server's sessions go, so the next is a full handshake.
	int64_t c13 = client_with("ca.pem", NULL, NULL, NULL, 12, 13, false, 60, err);
	REQUIRE(c13 > 0);
	CHECK_EQ_INT(resumes(l, port, c13, "localhost", NULL), 0);
	CHECK_EQ_INT(ak_tls_cached(c13), 2);
	close_h(l);
	drain();
	int64_t s12 = server(server_pair, 1, NULL, NULL, false, 12, 12, err);
	REQUIRE(s12 > 0);
	ak_val la[3] = {str("127.0.0.1"), {.i = port}, {.i = s12}};
	l = call_int("net.tcpListen", la, 3);
	ak_release(la[0].p);
	REQUIRE(l > 0);
	hs(l, port, c13, "localhost", &c, &s, &cf, &sf);
	REQUIRE(cf && sf);
	CHECK(strncmp(failure(cf), "std/net:tls:", 12) == 0);
	CHECK_EQ_INT(ak_tls_cached(c13), 0);
	ak_release(cf);
	ak_release(sf);
	close_h(c);
	close_h(s);
	CHECK_EQ_INT(resumes(l, port, c13, "localhost", NULL), 0);
	CHECK_EQ_INT(resumes(l, port, c13, "localhost", NULL), 1);
	close_h(l);

	// A server that gives no tickets: nothing to resume.
	int64_t plain = server_with(server_pair, 1, NULL, NULL, false, 12, 13, false, err);
	REQUIRE(plain > 0);
	int64_t port2, l2 = listen_on(plain, &port2);
	CHECK_EQ_INT(resumes(l2, port2, cconf, "localhost", NULL), 0);
	CHECK_EQ_INT(resumes(l2, port2, cconf, "localhost", NULL), 0);
	CHECK_EQ_INT(resumes(l2, port2, c12, "localhost", NULL), 0);
	CHECK_EQ_INT(resumes(l2, port2, c12, "localhost", NULL), 0);
	close_h(l2);
	drain();
}

T_MAIN()
