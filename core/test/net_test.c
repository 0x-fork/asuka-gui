// std/net's C (std/net.c, std/dns.c): IP addresses as text, DNS
// messages (queries written, answers read, the malformed refused),
// resolv.conf, the stub resolver against a DNS server of the test's own
// (over UDP, and over TCP when its UDP answer is truncated), and the
// builtins over real sockets: a listener, connections, reads of each
// kind, a cancelled read whose bytes the next read gets, a UDP
// round trip, a closed socket's waits.
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
#include "std/sock.h"
#include "signal/signal.h"
#include "base/thread.h"

#include <stdatomic.h>

static ak_lib_fn lib(const char *name) {
	for (const ak_lib_entry *e = ak_std_net; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	T_FAIL("no builtin %s", name);
	return NULL;
}

// ---- IP addresses ----

static const char *fmt(const char *in) {
	static char out[46];
	uint8_t b[16];
	int n = ak_ip_parse(in, strlen(in), b);
	if (!n) return "-";
	ak_ip_format(b, n, out);
	return out;
}

TEST(net_ip_text) {
	CHECK_EQ_STR(fmt("192.0.2.1"), "192.0.2.1");
	CHECK_EQ_STR(fmt("0.0.0.0"), "0.0.0.0");
	CHECK_EQ_STR(fmt("255.255.255.255"), "255.255.255.255");
	CHECK_EQ_STR(fmt("2001:0DB8:0000:0000:0000:0000:0000:0001"), "2001:db8::1");
	CHECK_EQ_STR(fmt("::"), "::");
	CHECK_EQ_STR(fmt("::1"), "::1");
	CHECK_EQ_STR(fmt("1::"), "1::");
	CHECK_EQ_STR(fmt("1:2:3:4:5:6:7::"), "1:2:3:4:5:6:7:0");     // one zero group isn't ::
	CHECK_EQ_STR(fmt("1:0:0:2:0:0:0:3"), "1:0:0:2::3");          // the longest run
	CHECK_EQ_STR(fmt("1:0:0:2:0:0:3:4"), "1::2:0:0:3:4");        // the first of two as long
	CHECK_EQ_STR(fmt("::ffff:192.0.2.1"), "::ffff:192.0.2.1");
	CHECK_EQ_STR(fmt("::ffff:c000:201"), "::ffff:192.0.2.1");
	CHECK_EQ_STR(fmt("64:ff9b::192.0.2.1"), "64:ff9b::c000:201");
	CHECK_EQ_STR(fmt("1:2:3:4:5:6:1.2.3.4"), "1:2:3:4:5:6:102:304");
	const char *bad[] = {"", "1.2.3", "1.2.3.4.5", "01.2.3.4", "1.2.3.256", "1.2.3.4 ", " 1.2.3.4", "1..2.3", "a.b.c.d",
	                     ":::", "1:::2", "1::2::3", ":1::", "1::2:", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7", "12345::", "::g",
	                     "1:2:3:4:5:6:7:1.2.3.4", "::1.2.3", "[::1]", "fe80::1%en0"};
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		uint8_t b[16];
		if (ak_ip_parse(bad[i], strlen(bad[i]), b)) T_FAIL("parsed %s", bad[i]);
	}
}

// ---- DNS messages ----

static void put16(ak_buf *b, unsigned v) {
	ak_buf_u8(b, (uint8_t)(v >> 8));
	ak_buf_u8(b, (uint8_t)v);
}

static void put_name(ak_buf *b, const char *name) {
	while (*name) {
		const char *dot = strchr(name, '.');
		size_t l = dot ? (size_t)(dot - name) : strlen(name);
		ak_buf_u8(b, (uint8_t)l);
		ak_buf_put(b, name, l);
		name += l + (dot ? 1 : 0);
	}
	ak_buf_u8(b, 0);
}

// answer writes the head of an answer to id's question (name, type):
// flags, then the question; its records follow.
static void answer(ak_buf *b, int id, unsigned flags, const char *name, int type, unsigned an) {
	put16(b, (unsigned)id);
	put16(b, flags);
	put16(b, 1);
	put16(b, an);
	put16(b, 0);
	put16(b, 0);
	put_name(b, name);
	put16(b, (unsigned)type);
	put16(b, 1);
}

// rr writes a record's head: its name a pointer to the question's (12),
// its type, IN, a TTL, the data's length.
static void rr(ak_buf *b, int type, unsigned len) {
	put16(b, 0xc00c);
	put16(b, (unsigned)type);
	put16(b, 1);
	put16(b, 0);
	put16(b, 300);
	put16(b, len);
}

TEST(net_dns_query) {
	ak_buf b = {0};
	CHECK(ak_dns_query(&b, 0x1234, "example.com.", AK_DNS_MX));
	static const uint8_t want[] = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 1, 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 15, 0, 1,
	                               0, 0, 41, 0x04, 0xd0, 0, 0, 0, 0, 0, 0};
	CHECK_EQ_INT(b.len, sizeof want);
	CHECK(b.len == sizeof want && memcmp(b.data, want, sizeof want) == 0);
	b.len = 0;
	char long_label[80];
	memset(long_label, 'a', 64);
	strcpy(long_label + 64, ".com");
	CHECK(!ak_dns_query(&b, 1, long_label, AK_DNS_MX));
	CHECK(!ak_dns_query(&b, 1, "a..b", AK_DNS_MX));
	ak_buf_free(&b);
}

TEST(net_dns_parse) {
	ak_buf b = {0};
	ak_dns_result r = {0};
	bool trunc;
	// MX: two, the second's name partly a pointer.
	answer(&b, 7, 0x8180, "example.com", AK_DNS_MX, 2);
	rr(&b, AK_DNS_MX, 2 + 16);
	put16(&b, 10);
	put_name(&b, "mx1.example.co");  // 16 bytes, not compressed
	size_t mx2 = b.len;
	rr(&b, AK_DNS_MX, 2 + 6);
	put16(&b, 20);
	ak_buf_u8(&b, 3);
	ak_buf_put(&b, "mx2", 3);
	put16(&b, 0xc00c);  // .example.com
	(void)mx2;
	CHECK(ak_dns_parse(b.data, b.len, 7, "example.com.", AK_DNS_MX, &r, &trunc));
	CHECK(!trunc && r.err == AK_NE_OK);
	REQUIRE(r.fields.len == 4);
	CHECK_EQ_STR(r.fields.data[0], "10");
	CHECK_EQ_STR(r.fields.data[1], "mx1.example.co");
	CHECK_EQ_STR(r.fields.data[2], "20");
	CHECK_EQ_STR(r.fields.data[3], "mx2.example.com");
	ak_dns_result_free(&r);
	// Another id, another question: not this answer.
	CHECK(!ak_dns_parse(b.data, b.len, 8, "example.com", AK_DNS_MX, &r, &trunc));
	CHECK(!ak_dns_parse(b.data, b.len, 7, "example.org", AK_DNS_MX, &r, &trunc));
	CHECK(!ak_dns_parse(b.data, b.len, 7, "example.com", AK_DNS_TXT, &r, &trunc));
	// Case doesn't matter.
	CHECK(ak_dns_parse(b.data, b.len, 7, "EXAMPLE.com", AK_DNS_MX, &r, &trunc));
	ak_dns_result_free(&r);
	r = (ak_dns_result){0};
	// Cut short anywhere: refused, nothing kept.
	for (size_t n = 0; n < b.len; n++) {
		if (ak_dns_parse(b.data, n, 7, "example.com", AK_DNS_MX, &r, &trunc) && r.err == AK_NE_OK && r.fields.len) T_FAIL("parsed %zu of %zu bytes", n, b.len);
		ak_dns_result_free(&r);
		r = (ak_dns_result){0};
	}

	// SRV and TXT.
	b.len = 0;
	answer(&b, 1, 0x8180, "_x._tcp.example.com", AK_DNS_SRV, 1);
	rr(&b, AK_DNS_SRV, 6 + 2);
	put16(&b, 5);
	put16(&b, 60);
	put16(&b, 5222);
	put16(&b, 0xc00c);
	CHECK(ak_dns_parse(b.data, b.len, 1, "_x._tcp.example.com", AK_DNS_SRV, &r, &trunc));
	REQUIRE(r.fields.len == 4);
	CHECK_EQ_STR(r.fields.data[2], "5222");
	CHECK_EQ_STR(r.fields.data[3], "_x._tcp.example.com");
	ak_dns_result_free(&r);
	r = (ak_dns_result){0};
	b.len = 0;
	answer(&b, 1, 0x8180, "example.com", AK_DNS_TXT, 1);
	rr(&b, AK_DNS_TXT, 1 + 5 + 1 + 3);
	ak_buf_u8(&b, 5);
	ak_buf_put(&b, "v=spf", 5);
	ak_buf_u8(&b, 3);
	ak_buf_put(&b, "1 a", 3);
	CHECK(ak_dns_parse(b.data, b.len, 1, "example.com", AK_DNS_TXT, &r, &trunc));
	REQUIRE(r.fields.len == 1);
	CHECK_EQ_STR(r.fields.data[0], "v=spf1 a");
	ak_dns_result_free(&r);
	r = (ak_dns_result){0};

	// What servers say: no such name, a failure, none of the type,
	// truncated.
	b.len = 0;
	answer(&b, 1, 0x8183, "nope.example", AK_DNS_MX, 0);
	CHECK(ak_dns_parse(b.data, b.len, 1, "nope.example", AK_DNS_MX, &r, &trunc));
	CHECK_EQ_INT(r.err, AK_NE_NOTFOUND);
	r = (ak_dns_result){0};
	b.len = 0;
	answer(&b, 1, 0x8182, "x.example", AK_DNS_MX, 0);
	CHECK(ak_dns_parse(b.data, b.len, 1, "x.example", AK_DNS_MX, &r, &trunc));
	CHECK_EQ_INT(r.err, AK_NE_LOOKUP);
	r = (ak_dns_result){0};
	b.len = 0;
	answer(&b, 1, 0x8180, "x.example", AK_DNS_MX, 0);
	CHECK(ak_dns_parse(b.data, b.len, 1, "x.example", AK_DNS_MX, &r, &trunc));
	CHECK_EQ_INT(r.err, AK_NE_NOTFOUND);
	r = (ak_dns_result){0};
	b.len = 0;
	answer(&b, 1, 0x8380, "x.example", AK_DNS_MX, 0);
	CHECK(ak_dns_parse(b.data, b.len, 1, "x.example", AK_DNS_MX, &r, &trunc));
	CHECK(trunc);

	// A pointer to itself: refused, not followed forever.
	b.len = 0;
	answer(&b, 1, 0x8180, "x.example", AK_DNS_NS, 1);
	size_t at = b.len;
	rr(&b, AK_DNS_NS, 2);
	put16(&b, 0xc000 | (unsigned)(at + 12));
	CHECK(!ak_dns_parse(b.data, b.len, 1, "x.example", AK_DNS_NS, &r, &trunc));
	ak_dns_result_free(&r);
	ak_buf_free(&b);
}

TEST(net_resolv_conf) {
	const char *text = "# a comment\nnameserver 10.0.0.1\nnameserver fe80::1%eth0\nnameserver not-an-ip\nsearch corp.example lab.example\noptions ndots:2 timeout:1 attempts:3 rotate\n";
	ak_resolv_conf c;
	ak_resolv_conf_parse(text, strlen(text), &c);
	CHECK_EQ_INT(c.nservers, 2);
	CHECK_EQ_STR(c.servers[0], "10.0.0.1");
	CHECK_EQ_STR(c.servers[1], "fe80::1%eth0");
	CHECK_EQ_INT(c.nsearch, 2);
	CHECK_EQ_STR(c.search[1], "lab.example");
	CHECK_EQ_INT(c.ndots, 2);
	CHECK_EQ_INT(c.timeout_ms, 1000);
	CHECK_EQ_INT(c.attempts, 3);
	ak_resolv_conf_parse("", 0, &c);
	CHECK_EQ_INT(c.nservers, 1);
	CHECK_EQ_STR(c.servers[0], "127.0.0.1");
	CHECK_EQ_INT(c.ndots, 1);
	CHECK_EQ_INT(c.timeout_ms, 5000);
}

// ---- A DNS server of the test's own ----
//
// It answers MX questions for big.example (over UDP, truncated: over
// TCP, whole), mx.example, and mx.corp.example; any other name is NXDOMAIN.

typedef struct {
	ak_sock udp, tcp;
	int port;
	atomic_bool stop;
	atomic_int udp_asked, tcp_asked;
} dns_server;

static void reply_to(const uint8_t *q, size_t n, bool over_udp, ak_buf *out) {
	if (n < 17) return;
	int id = q[0] << 8 | q[1];
	char name[256];
	size_t p = 12, w = 0;
	while (p < n && q[p] && w < 200) {
		size_t l = q[p++];
		if (w) name[w++] = '.';
		memcpy(name + w, q + p, l);
		w += l;
		p += l;
	}
	name[w] = 0;
	int type = q[p + 1] << 8 | q[p + 2];
	bool big = strcmp(name, "big.example") == 0;
	bool known = big || strcmp(name, "mx.example") == 0 || strcmp(name, "mx.corp.example") == 0;
	if (!known) {
		answer(out, id, 0x8183, name, type, 0);
		return;
	}
	if (big && over_udp) {
		answer(out, id, 0x8380, name, type, 0);  // TC
		return;
	}
	unsigned count = big ? 40 : 1;
	answer(out, id, 0x8180, name, type, count);
	for (unsigned i = 0; i < count; i++) {
		rr(out, AK_DNS_MX, 2 + 2 + 2);
		put16(out, 10 + i);
		ak_buf_u8(out, 1);
		ak_buf_u8(out, 'm');
		put16(out, 0xc00c);
	}
}

static void dns_server_main(void *p) {
	dns_server *s = p;
	while (!atomic_load(&s->stop)) {
		ak_pollfd fds[2] = {{.fd = s->udp, .events = POLLIN}, {.fd = s->tcp, .events = POLLIN}};
		if (ak_sock_poll(fds, 2, 50) <= 0) continue;
		uint8_t buf[2048];
		if (fds[0].revents) {
			struct sockaddr_storage from;
			socklen_t fl = sizeof from;
			int n = (int)recvfrom(s->udp, (char *)buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
			if (n > 0) {
				atomic_fetch_add(&s->udp_asked, 1);
				ak_buf out = {0};
				reply_to(buf, (size_t)n, true, &out);
				sendto(s->udp, (const char *)out.data, (int)out.len, 0, (struct sockaddr *)&from, fl);
				ak_buf_free(&out);
			}
		}
		if (fds[1].revents) {
			ak_sock c = accept(s->tcp, NULL, NULL);
			if (c == AK_BAD_SOCK) continue;
			uint8_t len2[2];
			int got = (int)recv(c, (char *)len2, 2, MSG_WAITALL);
			size_t n = got == 2 ? (size_t)(len2[0] << 8 | len2[1]) : 0;
			if (n && n <= sizeof buf && (int)recv(c, (char *)buf, (int)n, MSG_WAITALL) == (int)n) {
				atomic_fetch_add(&s->tcp_asked, 1);
				ak_buf out = {0};
				reply_to(buf, n, false, &out);
				uint8_t l[2] = {(uint8_t)(out.len >> 8), (uint8_t)out.len};
				send(c, (const char *)l, 2, 0);
				send(c, (const char *)out.data, (int)out.len, 0);
				ak_buf_free(&out);
			}
			ak_sock_close(c);
		}
	}
	atomic_store(&s->stop, false);
}

// dns_start opens the server's UDP and TCP sockets, both on one port of
// 127.0.0.1.
static bool dns_start(dns_server *s) {
	memset(s, 0, sizeof *s);
	for (int tries = 0; tries < 20; tries++) {
		struct sockaddr_in a = {0};
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		s->udp = socket(AF_INET, SOCK_DGRAM, 0);
		socklen_t al = sizeof a;
		if (bind(s->udp, (struct sockaddr *)&a, sizeof a) != 0 || getsockname(s->udp, (struct sockaddr *)&a, &al) != 0) return false;
		s->tcp = socket(AF_INET, SOCK_STREAM, 0);
		if (bind(s->tcp, (struct sockaddr *)&a, sizeof a) == 0 && listen(s->tcp, 8) == 0) {
			s->port = ntohs(a.sin_port);
			return ak_thread_start(dns_server_main, s);
		}
		ak_sock_close(s->udp);
		ak_sock_close(s->tcp);
	}
	return false;
}

static void dns_stop(dns_server *s) {
	atomic_store(&s->stop, true);
	while (atomic_load(&s->stop)) {
	}
	ak_sock_close(s->udp);
	ak_sock_close(s->tcp);
}

TEST(net_dns_stub) {
	REQUIRE(ak_sock_startup());
	dns_server s;
	REQUIRE(dns_start(&s));
	ak_resolv_conf c;
	const char *conf = "nameserver 127.0.0.1\nsearch corp.example\noptions timeout:2 attempts:1\n";
	ak_resolv_conf_parse(conf, strlen(conf), &c);
	ak_dns_result r;
	ak_dns_stub(&c, s.port, "mx.example", AK_DNS_MX, &r);
	CHECK_EQ_INT(r.err, AK_NE_OK);
	REQUIRE(r.fields.len == 2);
	CHECK_EQ_STR(r.fields.data[1], "m.mx.example");
	ak_dns_result_free(&r);
	// Truncated over UDP: asked again over TCP, whole there.
	ak_dns_stub(&c, s.port, "big.example", AK_DNS_MX, &r);
	CHECK_EQ_INT(r.err, AK_NE_OK);
	CHECK_EQ_INT(r.fields.len, 80);
	CHECK_EQ_INT(atomic_load(&s.tcp_asked), 1);
	ak_dns_result_free(&r);
	// A name without a dot (fewer than ndots): the search domain's first.
	ak_dns_stub(&c, s.port, "mx", AK_DNS_MX, &r);
	CHECK_EQ_INT(r.err, AK_NE_OK);
	REQUIRE(r.fields.len == 2);
	CHECK_EQ_STR(r.fields.data[1], "m.mx.corp.example");
	ak_dns_result_free(&r);
	ak_dns_stub(&c, s.port, "nope.example", AK_DNS_MX, &r);
	CHECK_EQ_INT(r.err, AK_NE_NOTFOUND);
	ak_dns_result_free(&r);
	dns_stop(&s);
	// No server: it says so, once the time's out.
	ak_resolv_conf none;
	const char *conf2 = "nameserver 127.0.0.1\noptions timeout:1 attempts:1\n";
	ak_resolv_conf_parse(conf2, strlen(conf2), &none);
	ak_dns_stub(&none, s.port, "mx.example", AK_DNS_MX, &r);
	CHECK_EQ_INT(r.err, AK_NE_LOOKUP);
	ak_dns_result_free(&r);
}

// ---- The builtins over sockets ----

static void pump(void) {
	ak_sched_wait_posted();
	ak_sched_run_posted();
	ak_sched_run();
}

// wait settles f (pumping what the net thread posts), for at most 10s.
static bool settle(ak_future *f) {
	int64_t until = ak_now_ns() + INT64_C(10000000000);
	while (f->state == AK_FUT_PENDING && ak_now_ns() < until) pump();
	return f->state != AK_FUT_PENDING;
}

static const char *failure(ak_future *f) {
	static char b[256];
	if (f->state != AK_FUT_FAILED) return "";
	snprintf(b, sizeof b, "%s", ak_error_message(f->error)->data);
	return b;
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

static ak_val str(const char *s) { return (ak_val){.p = ak_str_c(s)}; }

static ak_future *read_op(int64_t h, int mode, int64_t n, const char *delim, int64_t timeout_ns) {
	ak_val a[5] = {{.i = h}, {.i = mode}, {.i = n}, {.p = ak_bytes_new(delim, strlen(delim))}, {.i = timeout_ns}};
	ak_future *f = lib("net.tcpRead")(a, 5).p;
	ak_release(a[3].p);
	return f;
}

// read_text reads as mode says, and gives what came as text ("<none>",
// or "!" and the error).
static const char *read_text(int64_t h, int mode, int64_t n, const char *delim) {
	static char out[512];
	ak_future *f = read_op(h, mode, n, delim, 0);
	if (!settle(f)) snprintf(out, sizeof out, "<pending>");
	else if (f->state == AK_FUT_FAILED) snprintf(out, sizeof out, "!%s", failure(f));
	else if (!f->result.p) snprintf(out, sizeof out, "<none>");
	else snprintf(out, sizeof out, "%.*s", (int)((ak_bytes *)f->result.p)->len, (char *)((ak_bytes *)f->result.p)->data);
	ak_release(f);
	return out;
}

static void write_text(int64_t h, const char *s) {
	ak_val a[3] = {{.i = h}, {.p = ak_bytes_new(s, strlen(s))}, {.i = 0}};
	ak_future *f = lib("net.tcpWrite")(a, 3).p;
	ak_release(a[1].p);
	if (!settle(f) || f->state != AK_FUT_DONE) T_FAIL("write: %s", failure(f));
	ak_release(f);
}

static void close_h(int64_t h) {
	ak_val a = {.i = h};
	lib("net.sockClose")(&a, 1);
}

TEST(net_tcp) {
	ak_signal_bind_thread();
	ak_val la[2] = {str("127.0.0.1"), {.i = 0}};
	int64_t l = call_int("net.tcpListen", la, 2);
	ak_release(la[0].p);
	REQUIRE(l > 0);
	ak_val ha = {.i = l};
	ak_arr *addrs = lib("net.addresses")(&ha, 1).p;
	REQUIRE(addrs->len == 4);
	CHECK_EQ_STR(((ak_str *)addrs->data[0].p)->data, "127.0.0.1");
	int64_t port = 0;
	ak_str_to_int(addrs->data[1].p, &port);
	ak_release(addrs);
	REQUIRE(port > 0);

	// A connect and an accept, at once.
	ak_val ca[5] = {str("localhost"), {.i = port}, {.i = 0}, {.i = 1}, {.i = INT64_C(15000000000)}};
	ak_future *cf = lib("net.tcpConnect")(ca, 5).p;
	ak_release(ca[0].p);
	ak_val aa[2] = {{.i = l}, {.i = 0}};
	ak_future *af = lib("net.tcpAccept")(aa, 2).p;
	REQUIRE(settle(cf) && settle(af));
	CHECK_EQ_STR(failure(cf), "");
	CHECK_EQ_STR(failure(af), "");
	int64_t c = cf->result.i, s = af->result.i;
	ak_release(cf);
	ak_release(af);
	REQUIRE(c > 0 && s > 0);

	write_text(c, "line one\r\nline two\nabcdefXYZ");
	CHECK_EQ_STR(read_text(s, 2, 100, "\n"), "line one\r\n");
	CHECK_EQ_STR(read_text(s, 2, 4, "\n"), "!std/net:tooLarge:4");
	CHECK_EQ_STR(read_text(s, 2, 100, "\n"), "line two\n");
	CHECK_EQ_STR(read_text(s, 1, 3, ""), "abc");
	CHECK_EQ_STR(read_text(s, 0, 2, ""), "de");

	// A read that waits, cancelled: what comes next is the next read's.
	ak_future *pending = read_op(s, 1, 6, "", 0);
	for (int i = 0; i < 5; i++) pump();
	CHECK(pending->state == AK_FUT_PENDING);
	ak_future_cancel(pending);
	REQUIRE(settle(pending));
	CHECK(pending->state == AK_FUT_FAILED);
	ak_release(pending);
	write_text(c, "123");
	CHECK_EQ_STR(read_text(s, 1, 6, ""), "fXYZ12");
	CHECK_EQ_STR(read_text(s, 0, 100, ""), "3");

	// A timeout keeps what had come.
	write_text(c, "ab");
	ak_future *t = read_op(s, 1, 3, "", INT64_C(30000000));
	REQUIRE(settle(t));
	CHECK_EQ_STR(failure(t), "std/net:timedOut:");
	ak_release(t);
	CHECK_EQ_STR(read_text(s, 0, 100, ""), "ab");

	// Half closed: the reader sees its end, and the other way still goes.
	ak_val cw = {.i = c};
	lib("net.tcpCloseWrite")(&cw, 1);
	CHECK_EQ_STR(read_text(s, 3, 100, ""), "");
	CHECK_EQ_STR(read_text(s, 0, 100, ""), "<none>");
	write_text(s, "back");
	CHECK_EQ_STR(read_text(c, 1, 4, ""), "back");
	close_h(s);
	CHECK_EQ_STR(read_text(c, 0, 100, ""), "<none>");
	CHECK_EQ_STR(read_text(s, 0, 100, ""), "!std/net:closed:");

	close_h(c);

	// A closed socket's waits end.
	ak_val wa[2] = {{.i = l}, {.i = 0}};
	ak_future *w = lib("net.tcpAccept")(wa, 2).p;
	for (int i = 0; i < 5; i++) pump();
	CHECK(w->state == AK_FUT_PENDING);
	close_h(l);
	REQUIRE(settle(w));
	CHECK_EQ_STR(failure(w), "std/net:closed:");
	ak_release(w);

	// Nothing listens there now.
	ak_val ra[5] = {str("127.0.0.1"), {.i = port}, {.i = 0}, {.i = 1}, {.i = 0}};
	ak_future *rf = lib("net.tcpConnect")(ra, 5).p;
	ak_release(ra[0].p);
	REQUIRE(settle(rf));
	char want[64];
	snprintf(want, sizeof want, "std/net:refused:127.0.0.1:%lld", (long long)port);
	CHECK_EQ_STR(failure(rf), want);
	ak_release(rf);
	while (ak_sched_held()) pump();
}

TEST(net_udp) {
	ak_signal_bind_thread();
	ak_val oa[3] = {str("127.0.0.1"), {.i = 0}, {.i = 0}};
	int64_t a = call_int("net.udpOpen", oa, 3);
	int64_t b = call_int("net.udpOpen", oa, 3);
	ak_release(oa[0].p);
	REQUIRE(a > 0 && b > 0);
	ak_val ha = {.i = b};
	ak_arr *addrs = lib("net.addresses")(&ha, 1).p;
	int64_t port = 0;
	ak_str_to_int(addrs->data[1].p, &port);
	ak_release(addrs);
	ak_val ra[2] = {{.i = b}, {.i = 0}};
	ak_future *rf = lib("net.udpReceive")(ra, 2).p;
	ak_val sa[5] = {{.i = a}, {.p = ak_bytes_new("ping", 4)}, str("127.0.0.1"), {.i = port}, {.i = 0}};
	ak_future *sf = lib("net.udpSend")(sa, 5).p;
	ak_release(sa[1].p);
	ak_release(sa[2].p);
	REQUIRE(settle(sf) && settle(rf));
	CHECK_EQ_STR(failure(sf), "");
	REQUIRE(rf->state == AK_FUT_DONE);
	ak_arr *got = rf->result.p;
	REQUIRE(got->len == 3);
	CHECK(((ak_bytes *)got->data[0].p)->len == 4 && memcmp(((ak_bytes *)got->data[0].p)->data, "ping", 4) == 0);
	CHECK(((ak_bytes *)got->data[1].p)->len == 9 && memcmp(((ak_bytes *)got->data[1].p)->data, "127.0.0.1", 9) == 0);
	ak_release(sf);
	ak_release(rf);
	close_h(a);
	close_h(b);
	while (ak_sched_held()) pump();
}

TEST(net_lookup) {
	ak_signal_bind_thread();
	ak_val a = str("localhost");
	ak_future *f = lib("net.lookupHost")(&a, 1).p;
	ak_release(a.p);
	REQUIRE(settle(f));
	REQUIRE(f->state == AK_FUT_DONE);
	ak_arr *ips = f->result.p;
	CHECK(ips->len >= 1);
	for (uint32_t i = 0; i < ips->len; i++) {
		const char *ip = ((ak_str *)ips->data[i].p)->data;
		CHECK(strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "::1") == 0);
	}
	ak_release(f);
	while (ak_sched_held()) pump();
}

T_MAIN()
