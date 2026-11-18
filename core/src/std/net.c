// net.c: std/net's builtins: TCP connections and listeners, UDP sockets
// and name lookups, the same C on every system that has sockets (macOS,
// iOS, Linux, Android, Windows); the web has none, and says so.
//
// One thread does every socket's I/O: the net thread, started with the
// first socket, its sockets non-blocking in one poll() loop (WSAPoll on
// Windows). The main thread never waits on a socket. Each operation (a
// connect, a read, a write, an accept, a datagram sent or received) is
// an op: made on the main thread with the future its caller awaits,
// handed to the net thread on a queue it's woken for, done there when
// the socket is ready, and posted back to the main thread, which settles
// the future. A cancelled future (its task cancelled, its owner gone)
// asks the net thread for its op back: the future is settled once it has
// it, so what the op got by then isn't lost: a read's bytes go back to
// the front of the connection's buffer, an accepted connection waits for
// the next accept, a datagram for the next receive. A timeout is an op's
// deadline on the net thread's clock.
//
// A connection's reads come from a buffer the net thread fills as a
// read asks for more: some bytes, exactly n, up to a delimiter (a line),
// or everything to the end, each done on the net thread, so a read is
// one crossing whatever its size. A connection has one read at a time;
// its writes queue, each written whole before the next.
//
// Names are looked up on a thread of each lookup's own (getaddrinfo
// blocks): a connect's, a listen's and a UDP socket's hand their
// addresses to the net thread, which keeps the op's deadline meanwhile;
// lookups' answer the main thread. A connect tries a name's addresses as
// Happy Eyeballs does (RFC 8305): the families interleaved, the first
// family's first, the next started 250 ms after the last if none has
// connected yet, the first to connect winning.
//
// Every socket belongs to the task that made it (the owner current
// when its op began): when that ends, the socket closes. A server's
// connections belong to the server, each served by a task the server
// owns, as std/http's are, and closed as that task ends.
//
// A test's loop (core/runner, the fake host's) waits for work other
// threads will post back while the scheduler is held, and otherwise
// moves its clock to the next timer. The net thread holds it while it
// has work: a command not yet done, a connect, a write, a deadline it
// keeps; and for a moment after (LINGER_MS), so what a socket of this
// process wrote reaches another of its sockets (the system's loopback
// delivers it) before the loop decides nothing will come. Waiting for
// another computer, or for nothing (a read whose peer is idle, an
// accept), doesn't hold it: a test's withTimeout around such a read
// times out. A lookup's thread holds it while it runs.
//
//	net.available() -> bool                               false on the web
//	net.ipParse(text) -> bytes?                           4 or 16 bytes
//	net.ipText(octets) -> string
//	net.tcpConnect(host, port, timeoutNs, noDelay, keepAliveNs) -> Task<int>
//	net.tcpListen(host, port, tls) -> Task<int>           tls: a TLS configuration (tls.server's), 0 none
//	net.tcpAccept(listener, timeoutNs) -> Task<int>
//	net.addresses(h) -> [string]                          local ip, port, remote ip, port
//	net.tcpRead(h, mode, n, delimiter, timeoutNs) -> Task<bytes?>
//	net.tcpWrite(h, data, timeoutNs) -> Task<void>
//	net.tcpCloseWrite(h)
//	net.sockClose(h)
//	net.setOption(h, option, value) throws                1 no delay, 2 keep-alive (ns), 3 broadcast
//	net.udpOpen(host, port, reuse) -> Task<int>
//	net.udpConnect(h, ip, port) throws
//	net.udpSend(h, data, ip, port, timeoutNs) -> Task<void>   ip "": the connected peer
//	net.udpReceive(h, timeoutNs) -> Task<[bytes]>          data, the sender's ip, its port
//	net.udpGroup(h, group, join) throws
//	net.lookupHost(host) -> Task<[string]>
//	net.lookupAddr(ip) -> Task<[string]>
//	net.lookupRecords(name, type) -> Task<[string]>        net.h's fields
//	net.lookupCanonical(host) -> Task<string>
//	net.serveStart(listener, dispatch) -> int             a server's handle
//	net.serveClose(server, graceNs) -> Task<void>
//	net.serveWait(server) -> Task<void>
//
// A failure rejects with "std/net:<case>:<detail>" (net.h's ak_net_err),
// which std/net's Asuka throws as its NetError.
//
// TLS is a layer between a connection's socket and its buffers (lsock's
// in, and what a write sends), on the net thread: the reads and writes
// above it the same (tls.h; tls.c's builtins start it). A client's
// starts on a connection that's open (net.connectTLS, conn.startTLS: a
// STARTTLS's bytes read before it are the handshake's); a TLS
// listener's connections start theirs as they're accepted, the
// handshake done as their first read or write waits on it, as Go's are.
// A read or a write waits for the handshake; one that fails fails them.
// That's nettls.c's, which net.c reaches only through ak_net_tls
// (netint.h's), set by tls.c once a configuration exists: an app without
// TLS links neither, nor Mbed TLS.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif

#if !defined(__wasm__)
#include "std/sock.h"  // before windows.h (base/thread.h's)
#include "std/tls.h"
#endif
#include "std/net.h"
#if !defined(__wasm__)
#include "std/netint.h"
#define set_err ak_net_set_err
#define post_op ak_net_post_op
#define dup_c ak_net_dup
#endif
#include "std/std.h"
#include "rt/sched.h"
#include "base/thread.h"
#include "signal/signal.h"
#include "vm/vm.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__wasm__) && !defined(_WIN32)
#include <net/if.h>
#endif
#if defined(_WIN32) && !defined(__wasm__)
#include <mstcpip.h>
#if !defined(SIO_UDP_CONNRESET)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

#define STR(i) ((ak_str *)args[i].p)

// ---- IP addresses ----

static int parse_v4(const char *s, size_t n, uint8_t out[4]) {
	size_t i = 0;
	for (int part = 0; part < 4; part++) {
		if (part) {
			if (i >= n || s[i] != '.') return 0;
			i++;
		}
		size_t st = i;
		unsigned v = 0;
		while (i < n && s[i] >= '0' && s[i] <= '9' && i - st < 3) v = v * 10 + (unsigned)(s[i++] - '0');
		if (i == st || v > 255 || (i - st > 1 && s[st] == '0')) return 0;
		out[part] = (uint8_t)v;
	}
	return i == n ? 4 : 0;
}

static int hexval(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int parse_v6(const char *s, size_t n, uint8_t out[16]) {
	uint16_t g[8];
	int ng = 0, gap = -1;
	size_t i = 0;
	if (n >= 2 && s[0] == ':' && s[1] == ':') {
		gap = 0;
		i = 2;
	} else if (n == 0 || s[0] == ':') {
		return 0;
	}
	while (i < n) {
		size_t st = i;
		unsigned v = 0;
		while (i < n && hexval(s[i]) >= 0 && i - st < 4) v = v << 4 | (unsigned)hexval(s[i++]);
		if (i < n && s[i] == '.') {
			// The last 32 bits written as IPv4.
			uint8_t v4[4];
			if (ng > 6 || !parse_v4(s + st, n - st, v4)) return 0;
			g[ng++] = (uint16_t)(v4[0] << 8 | v4[1]);
			g[ng++] = (uint16_t)(v4[2] << 8 | v4[3]);
			i = n;
			break;
		}
		if (i == st || (i < n && hexval(s[i]) >= 0) || ng == 8) return 0;
		g[ng++] = (uint16_t)v;
		if (i == n) break;
		if (s[i] != ':') return 0;
		i++;
		if (i < n && s[i] == ':') {
			if (gap >= 0) return 0;
			gap = ng;
			i++;
		} else if (i == n) {
			return 0;  // a lone trailing colon
		}
	}
	if (gap < 0 ? ng != 8 : ng > 7) return 0;
	memset(out, 0, 16);
	int head = gap < 0 ? ng : gap;
	for (int j = 0; j < head; j++) {
		out[2 * j] = (uint8_t)(g[j] >> 8);
		out[2 * j + 1] = (uint8_t)g[j];
	}
	for (int j = head; j < ng; j++) {
		int at = 8 - (ng - j);
		out[2 * at] = (uint8_t)(g[j] >> 8);
		out[2 * at + 1] = (uint8_t)g[j];
	}
	return 16;
}

int ak_ip_parse(const char *s, size_t n, uint8_t out[16]) {
	if (n == 0 || n > 45) return 0;
	if (memchr(s, ':', n)) return parse_v6(s, n, out);
	return parse_v4(s, n, out);
}

void ak_ip_format(const uint8_t *b, int len, char out[46]) {
	if (len == 4) {
		snprintf(out, 46, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
		return;
	}
	static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
	if (memcmp(b, mapped, 12) == 0) {
		snprintf(out, 46, "::ffff:%d.%d.%d.%d", b[12], b[13], b[14], b[15]);
		return;
	}
	uint16_t g[8];
	for (int i = 0; i < 8; i++) g[i] = (uint16_t)(b[2 * i] << 8 | b[2 * i + 1]);
	int best = -1, bestlen = 0;
	for (int i = 0; i < 8;) {
		if (g[i] != 0) {
			i++;
			continue;
		}
		int j = i;
		while (j < 8 && g[j] == 0) j++;
		if (j - i > bestlen) best = i, bestlen = j - i;
		i = j;
	}
	if (bestlen < 2) best = -1;
	size_t w = 0;
	for (int i = 0; i < 8; i++) {
		if (i == best) {
			w += (size_t)snprintf(out + w, 46 - w, "::");
			i += bestlen - 1;
			continue;
		}
		if (i > 0 && !(best >= 0 && i == best + bestlen)) out[w++] = ':';
		w += (size_t)snprintf(out + w, 46 - w, "%x", g[i]);
	}
	out[w] = 0;
}

const char *ak_net_err_name(ak_net_err e) {
	static const char *const names[] = {"", "cancelled", "refused", "reset", "timedOut", "unreachable", "notFound", "lookupFailed", "closed", "ended", "addressInUse", "addressUnavailable", "denied", "tooLarge", "invalid", "unsupported", "failed", "tls", "untrustedCertificate"};
	return (unsigned)e < sizeof names / sizeof names[0] ? names[e] : "failed";
}

static ak_val ip_parse_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	uint8_t b[16];
	int k = ak_ip_parse(STR(0)->data, STR(0)->len, b);
	return (ak_val){.p = k ? ak_bytes_new(b, (size_t)k) : NULL};
}

static ak_val ip_text_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *b = args[0].p;
	char t[46] = "";
	if (b->len == 4 || b->len == 16) ak_ip_format(b->data, (int)b->len, t);
	return (ak_val){.p = ak_str_c(t)};
}

// fail makes an error of e and detail, as std/net's Asuka reads it.
static void *net_error(ak_net_err e, const char *detail) {
	ak_throw_msg("std/net:%s:%s", ak_net_err_name(e), detail ? detail : "");
	return ak_error_take();
}

static ak_future *failed_future(ak_kind k, ak_net_err e, const char *detail) {
	ak_future *f = ak_future_new(k);
	ak_future_reject(f, net_error(e, detail));
	return f;
}

static void throw_net(ak_net_err e, const char *detail) { ak_throw(net_error(e, detail)); }

#if defined(__wasm__)

// A page has no sockets: std/net's Asuka checks available() first.
static ak_val available_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.i = 0};
}
static ak_val unsupported_task(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.p = failed_future(AK_K_REF, AK_NE_UNSUPPORTED, "")};
}
static ak_val unsupported_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	throw_net(AK_NE_UNSUPPORTED, "");
	return (ak_val){0};
}
static ak_val nothing_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){0};
}
static ak_val strs_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.p = ak_str_arr(0)};
}

const ak_lib_entry ak_std_net[] = {
	{"net.available", available_fn},
	{"net.ipParse", ip_parse_fn},
	{"net.ipText", ip_text_fn},
	{"net.tcpConnect", unsupported_task},
	{"net.tcpListen", unsupported_task},
	{"net.tcpAccept", unsupported_task},
	{"net.addresses", strs_fn},
	{"net.tcpRead", unsupported_task},
	{"net.tcpWrite", unsupported_task},
	{"net.tcpCloseWrite", nothing_fn},
	{"net.sockClose", nothing_fn},
	{"net.setOption", unsupported_fn},
	{"net.udpOpen", unsupported_task},
	{"net.udpConnect", unsupported_fn},
	{"net.udpSend", unsupported_task},
	{"net.udpReceive", unsupported_task},
	{"net.udpGroup", unsupported_fn},
	{"net.lookupHost", unsupported_task},
	{"net.lookupAddr", unsupported_task},
	{"net.lookupRecords", unsupported_task},
	{"net.lookupCanonical", unsupported_task},
	{"net.serveStart", nothing_fn},
	{"net.serveClose", unsupported_task},
	{"net.serveWait", unsupported_task},
	{NULL, NULL},
};

#else

// The VM's closures' invoke (vm_int.h): a closure compiled to C, passed
// as an async fn, returns its task's future.
ak_val ak_vm_closure_invoke(ak_closure *c, ak_val *args, uint32_t nargs);

#define ATTEMPT_DELAY_NS INT64_C(250000000)  // RFC 8305's Connection Attempt Delay
#define ACCEPT_PAUSE_NS INT64_C(100000000)   // out of descriptors: accepting again after
#define LINGER_MS 10                         // busy a moment longer: what loopback delivers

static void op_free(op *o) {
	ak_free(o->host);
	ak_free(o->data);
	ak_free(o->addrs);
	ak_free(o->lens);
	for (uint32_t i = 0; i < o->tries.len; i++) ak_sock_close(o->tries.data[i].fd);
	ak_vec_free(&o->tries);
	ak_free(o->detail);
	ak_buf_free(&o->out);
	ak_free(o);
}

char *ak_net_dup(const char *s) {
	size_t n = strlen(s);
	char *d = ak_alloc(n + 1);
	memcpy(d, s, n + 1);
	return d;
}

void ak_net_set_err(op *o, ak_net_err e, const char *fmt, ...) {
	o->err = e;
	ak_free(o->detail);
	o->detail = NULL;
	if (fmt) {
		char b[512];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(b, sizeof b, fmt, ap);
		va_end(ap);
		o->detail = dup_c(b);
	}
}

// host_port writes host and port as an address's text: "example.com:80",
// "[::1]:80".
static void host_port(const char *host, int port, char *out, size_t cap) {
	if (strchr(host, ':')) snprintf(out, cap, "[%s]:%d", host, port);
	else snprintf(out, cap, "%s:%d", host, port);
}

// sys_text is the system's text for error e.
static void sys_text(int e, char *out, size_t cap) {
#if defined(_WIN32)
	char b[256];
	DWORD k = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)e, 0, b, sizeof b, NULL);
	while (k && (b[k - 1] == '\n' || b[k - 1] == '\r' || b[k - 1] == ' ' || b[k - 1] == '.')) b[--k] = 0;
	if (k) snprintf(out, cap, "%s", b);
	else snprintf(out, cap, "error %d", e);
#else
	static ak_mutex mu = AK_MUTEX_INIT;
	ak_mutex_lock(&mu);
	snprintf(out, cap, "%s", strerror(e));
	ak_mutex_unlock(&mu);
#endif
}

#if defined(_WIN32)
#define ERR(x) WSA##x
#else
#define ERR(x) x
#endif

// sys_err sets o's error from the system's e, a failure at address
// (text, for the cases that say it).
static void sys_err(op *o, int e, const char *address) {
	char why[256];
	switch (e) {
	case ERR(ECONNREFUSED): set_err(o, AK_NE_REFUSED, "%s", address); return;
	case ERR(ETIMEDOUT): set_err(o, AK_NE_TIMEDOUT, NULL); return;
	case ERR(EHOSTUNREACH):
	case ERR(ENETUNREACH):
	case ERR(ENETDOWN):
	case ERR(EHOSTDOWN): set_err(o, AK_NE_UNREACHABLE, "%s", address); return;
	case ERR(ECONNRESET):
	case ERR(ECONNABORTED):
	case ERR(ESHUTDOWN):
#if !defined(_WIN32)
	case EPIPE:
#endif
		set_err(o, AK_NE_RESET, NULL);
		return;
	case ERR(EADDRINUSE): set_err(o, AK_NE_INUSE, "%s", address); return;
	case ERR(EADDRNOTAVAIL): set_err(o, AK_NE_UNAVAILABLE, "%s", address); return;
	case ERR(EACCES):
#if !defined(_WIN32)
	case EPERM:
#endif
		set_err(o, AK_NE_DENIED, "%s", address[0] ? "not allowed (a port below 1024 needs privileges; a firewall may refuse)" : "not allowed");
		return;
	case ERR(EMSGSIZE): set_err(o, AK_NE_TOOLARGE, "65507"); return;
	case ERR(EAFNOSUPPORT): set_err(o, AK_NE_UNREACHABLE, "%s", address); return;
	}
	sys_text(e, why, sizeof why);
	set_err(o, AK_NE_FAILED, "%s", why);
}

// gai_err sets o's error from getaddrinfo's g, looking up host.
static void gai_err(op *o, int g, const char *host) {
	switch (g) {
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
	case EAI_NODATA:
#endif
#if defined(EAI_ADDRFAMILY)
	case EAI_ADDRFAMILY:
#endif
	case EAI_NONAME: set_err(o, AK_NE_NOTFOUND, "%s", host); return;
	case EAI_AGAIN: set_err(o, AK_NE_LOOKUP, "%s:the name couldn't be looked up now (no DNS server answered)", host); return;
	case EAI_FAIL: set_err(o, AK_NE_LOOKUP, "%s:the DNS server failed", host); return;
	}
#if defined(EAI_SYSTEM)
	if (g == EAI_SYSTEM) {
		sys_err(o, errno, host);
		return;
	}
#endif
	set_err(o, AK_NE_LOOKUP, "%s:%s", host, gai_strerror(g));
}

// sock_text writes a's address (an IP's text, with a zone for a scoped
// IPv6 one) and its port: an IPv4 address mapped into IPv6 as IPv4.
static void sock_text(const struct sockaddr *a, char *ip, size_t cap, int *port) {
	ip[0] = 0;
	*port = 0;
	if (a->sa_family == AF_INET) {
		const struct sockaddr_in *v4 = (const struct sockaddr_in *)a;
		char t[46];
		ak_ip_format((const uint8_t *)&v4->sin_addr, 4, t);
		snprintf(ip, cap, "%s", t);
		*port = ntohs(v4->sin_port);
	} else if (a->sa_family == AF_INET6) {
		const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)a;
		const uint8_t *b = (const uint8_t *)&v6->sin6_addr;
		static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
		char t[46];
		if (memcmp(b, mapped, 12) == 0) ak_ip_format(b + 12, 4, t);
		else ak_ip_format(b, 16, t);
		*port = ntohs(v6->sin6_port);
		if (v6->sin6_scope_id) {
#if defined(_WIN32)
			snprintf(ip, cap, "%s%%%lu", t, (unsigned long)v6->sin6_scope_id);
#else
			char name[IF_NAMESIZE + 1];
			if (if_indextoname(v6->sin6_scope_id, name)) snprintf(ip, cap, "%s%%%s", t, name);
			else snprintf(ip, cap, "%s%%%u", t, (unsigned)v6->sin6_scope_id);
#endif
		} else {
			snprintf(ip, cap, "%s", t);
		}
	}
}

// numeric fills a with ip's address (an IP's text, maybe with a zone)
// and port, without a lookup: false when ip isn't an address.
static bool numeric(const char *ip, int port, struct sockaddr_storage *a, socklen_t *al) {
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
	char ps[16];
	snprintf(ps, sizeof ps, "%d", port);
	if (getaddrinfo(ip, ps, &hints, &res) != 0 || !res) return false;
	memcpy(a, res->ai_addr, res->ai_addrlen);
	*al = (socklen_t)res->ai_addrlen;
	freeaddrinfo(res);
	return true;
}

// keepalive turns s's keep-alive probes on after ns idle (and between
// them), or off for 0.
static int set_keepalive(ak_sock s, int64_t ns) {
	int on = ns > 0, secs = ns > 0 ? (int)((ns + 999999999) / 1000000000) : 0;
#if defined(_WIN32)
	struct tcp_keepalive k = {(ULONG)on, (ULONG)secs * 1000, (ULONG)secs * 1000};
	DWORD got = 0;
	if (on) return WSAIoctl(s, SIO_KEEPALIVE_VALS, &k, sizeof k, NULL, 0, &got, NULL, NULL) == 0 ? 0 : -1;
	return setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char *)&on, sizeof on);
#else
	if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on) != 0) return -1;
	if (!on) return 0;
#if defined(TCP_KEEPIDLE)
	setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &secs, sizeof secs);
#elif defined(TCP_KEEPALIVE)
	setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE, &secs, sizeof secs);
#endif
#if defined(TCP_KEEPINTVL)
	setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &secs, sizeof secs);
#endif
	return 0;
#endif
}

static void new_socket_flags(ak_sock s) {
	ak_sock_no_inherit(s);
	ak_sock_nonblocking(s);
	ak_sock_nosigpipe(s);
}

// ---- The net thread ----

enum { C_OP, C_CANCEL, C_CLOSE, C_SHUT, C_UNREAD, C_RESOLVED, C_TLS };

typedef struct cmd {
	struct cmd *next;
	uint8_t kind;
	int64_t handle;
	uint64_t id;
	op *op;
	uint8_t *data;  // an unread's
	size_t len;
	// A lookup's answer.
	ak_net_err err;
	char *detail;
	struct sockaddr_storage *addrs;
	socklen_t *lens;
	uint32_t naddrs;
} cmd;

static void cmd_free(cmd *c) {
	ak_free(c->data);
	ak_free(c->detail);
	ak_free(c->addrs);
	ak_free(c->lens);
	ak_free(c);
}

static struct {
	// The queue the main thread and lookups fill (under mu), and what
	// wakes the thread for it.
	ak_mutex mu;
	cmd *head, *tail;
	ak_waker wake;
	bool started, broken;  // main's
	// The thread's own.
	lsock **slots;
	uint32_t nslots;
	AK_VEC(uint32_t) free_slots;
	ak_idmap byhandle;
	opq waiting;  // connects, listens and UDP sockets: being looked up, or connecting
	int64_t next_handle;
} L = {.mu = AK_MUTEX_INIT};

static void op_done(void *p);

void ak_net_post_op(op *o) {
	ak_post(op_done, o);
}

static lsock *lsock_new(ak_sock fd, uint8_t kind) {
	lsock *s = ak_alloc(sizeof *s);
	memset(s, 0, sizeof *s);
	s->fd = fd;
	s->kind = kind;
	s->handle = ++L.next_handle;
	if (L.free_slots.len) {
		s->slot = L.free_slots.data[--L.free_slots.len];
	} else {
		s->slot = L.nslots++;
		L.slots = ak_realloc(L.slots, sizeof(lsock *) * L.nslots);
	}
	L.slots[s->slot] = s;
	ak_idmap_put(&L.byhandle, s->handle, s);
	return s;
}

static void fail_all(opq *q, ak_net_err e) {
	op *o;
	while ((o = opq_pop(q))) {
		set_err(o, e, NULL);
		post_op(o);
	}
}

static void lsock_close(lsock *s) {
	fail_all(&s->reads, AK_NE_CLOSED);
	fail_all(&s->writes, AK_NE_CLOSED);
	if (s->tls) ak_net_tls->close(s);
	ak_idmap_del(&L.byhandle, s->handle);
	ak_sock_close(s->fd);
	L.slots[s->slot] = NULL;
	ak_vec_push(&L.free_slots, s->slot);
	ak_buf_free(&s->in);
	ak_free(s->rdetail);
	ak_free(s);
}

static void consume(ak_buf *b, size_t n) {
	memmove(b->data, b->data + n, b->len - n);
	b->len -= n;
}

static void take(lsock *s, op *o, size_t n) {
	ak_buf_put(&o->out, s->in.data, n);
	consume(&s->in, n);
	s->scan = 0;
	if (s->in.len == 0 && s->in.cap > 4 * READ_CHUNK) ak_buf_free(&s->in);
}

// read_done reports whether s's buffer answers read o (filling its
// answer); false: it needs more.
static bool read_done(lsock *s, op *o) {
	ak_buf *in = &s->in;
	switch (o->mode) {
	case RD_SOME:
		if (in->len) {
			take(s, o, in->len < o->n ? in->len : o->n);
			return true;
		}
		if (s->eof) {
			o->none = true;
			return true;
		}
		break;
	case RD_EXACT:
		if (in->len >= o->n) {
			take(s, o, o->n);
			return true;
		}
		if (s->eof) {
			set_err(o, AK_NE_ENDED, "%zu", o->n);
			return true;
		}
		break;
	case RD_UNTIL: {
		size_t limit = in->len < o->n ? in->len : o->n;
		size_t from = s->scan >= o->len ? s->scan - o->len + 1 : 0;
		for (size_t i = from; i + o->len <= limit; i++) {
			if (memcmp(in->data + i, o->data, o->len) == 0) {
				take(s, o, i + o->len);
				return true;
			}
		}
		s->scan = limit;
		if (in->len >= o->n) {
			set_err(o, AK_NE_TOOLARGE, "%zu", o->n);
			return true;
		}
		if (s->eof) {
			if (in->len) take(s, o, in->len);
			else o->none = true;
			return true;
		}
		break;
	}
	case RD_ALL:
		if (in->len > o->n) {
			set_err(o, AK_NE_TOOLARGE, "%zu", o->n);
			return true;
		}
		if (s->eof) {
			take(s, o, in->len);
			return true;
		}
		break;
	}
	if (s->rerr) {
		set_err(o, s->rerr, "%s", s->rdetail ? s->rdetail : "");
		return true;
	}
	return false;
}

static void conn_reads(lsock *s) {
	op *o;
	while ((o = s->reads.head)) {
		if (o->kind == OP_TLS || s->tls) {
			// A TLS start, or a read of a TLS connection: TLS's (nettls.c).
			int r = ak_net_tls->gate(s, o);
			if (r < 0) return;
			if (r == 0 && !read_done(s, o)) return;
		} else if (!read_done(s, o)) {
			return;
		}
		post_op(opq_pop(&s->reads));
	}
}

// wants_data: s's next read needs more than its buffer has.
static bool wants_data(lsock *s) { return s->reads.head && s->reads.head->kind != OP_TLS && !s->eof && !s->rerr && (!s->tls || s->tphase == T_OPEN); }

static void conn_fill(lsock *s) {
	op *o = s->reads.head;
	size_t want = READ_CHUNK;
	if (o && o->mode == RD_EXACT && o->n > s->in.len) {
		size_t need = o->n - s->in.len;
		want = need > (1u << 20) ? (1u << 20) : need < READ_CHUNK ? READ_CHUNK : need;
	}
	if (s->tls) {
		ak_net_tls->fill(s, want);
		return;
	}
	for (;;) {
		ak_buf_reserve(&s->in, want);
		int r = (int)recv(s->fd, (char *)s->in.data + s->in.len, (int)want, 0);
		if (r > 0) {
			s->in.len += (size_t)r;
			return;
		}
		if (r == 0) {
			s->eof = true;
			return;
		}
		int e = ak_sock_err();
		if (AK_SOCK_INTR(e)) continue;
		if (AK_SOCK_AGAIN(e)) return;
		op tmp = {0};
		sys_err(&tmp, e, "");
		s->rerr = tmp.err;
		s->rdetail = tmp.detail;
		return;
	}
}

static void conn_writes(lsock *s) {
	op *o;
	if (s->tls) {
		ak_net_tls->writes(s);
		return;
	}
	while ((o = s->writes.head)) {
		while (o->off < o->len) {
			size_t left = o->len - o->off;
			int chunk = left > (1u << 30) ? (1 << 30) : (int)left;
			int w = (int)send(s->fd, (const char *)o->data + o->off, chunk, AK_SEND_FLAGS);
			if (w < 0) {
				int e = ak_sock_err();
				if (AK_SOCK_INTR(e)) continue;
				if (AK_SOCK_AGAIN(e)) return;
				sys_err(o, e, "");
				break;
			}
			o->off += (size_t)w;
		}
		post_op(opq_pop(&s->writes));
	}
	if (s->shut_wanted && !s->shut) {
		shutdown(s->fd, AK_SHUT_WR);
		s->shut = true;
	}
}

static void accepts(lsock *s, int64_t now) {
	op *o;
	while ((o = s->reads.head) && now >= s->pause_until) {
		struct sockaddr_storage a;
		socklen_t al = sizeof a;
		ak_sock c = accept(s->fd, (struct sockaddr *)&a, &al);
		if (c == AK_BAD_SOCK) {
			int e = ak_sock_err();
			if (AK_SOCK_INTR(e)) continue;
			if (AK_SOCK_AGAIN(e)) return;
#if defined(_WIN32)
			if (e == WSAECONNRESET) continue;
			if (e == WSAEMFILE || e == WSAENOBUFS) {
#else
			if (e == ECONNABORTED || e == EPROTO) continue;
			if (e == EMFILE || e == ENFILE || e == ENOBUFS || e == ENOMEM) {
#endif
				s->pause_until = now + ACCEPT_PAUSE_NS;
				return;
			}
			sys_err(o, e, "");
			post_op(opq_pop(&s->reads));
			continue;
		}
		new_socket_flags(c);
		int one = 1;
		setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
		set_keepalive(c, INT64_C(15000000000));
		lsock *n = lsock_new(c, K_CONN);
		if (s->tls_conf) ak_net_tls->accepted(s, n);
		o->got = n->handle;
		o->got_fd = c;
		o->got_family = a.ss_family;
		post_op(opq_pop(&s->reads));
	}
}

static void receives(lsock *s) {
	op *o;
	while ((o = s->reads.head)) {
		struct sockaddr_storage a;
		socklen_t al = sizeof a;
		ak_buf_reserve(&o->out, 65536);
		int r = (int)recvfrom(s->fd, (char *)o->out.data, 65536, 0, (struct sockaddr *)&a, &al);
		if (r < 0) {
			int e = ak_sock_err();
			if (AK_SOCK_INTR(e)) continue;
			if (AK_SOCK_AGAIN(e)) return;
#if defined(_WIN32)
			if (e == WSAEMSGSIZE) r = 65536;  // cut short to what fits: what's there is the datagram's start
			else
#endif
			{
				sys_err(o, e, "");
				post_op(opq_pop(&s->reads));
				continue;
			}
		}
		o->out.len = (size_t)r;
		sock_text((struct sockaddr *)&a, o->from, sizeof o->from, &o->from_port);
		post_op(opq_pop(&s->reads));
	}
}

static void sends(lsock *s) {
	op *o;
	while ((o = s->writes.head)) {
		int w = o->tolen ? (int)sendto(s->fd, (const char *)o->data, (int)o->len, AK_SEND_FLAGS, (struct sockaddr *)&o->to, o->tolen) : (int)send(s->fd, (const char *)o->data, (int)o->len, AK_SEND_FLAGS);
		if (w < 0) {
			int e = ak_sock_err();
			if (AK_SOCK_INTR(e)) continue;
			if (AK_SOCK_AGAIN(e)) return;
			char where[80] = "";
			if (o->tolen) {
				int port;
				char ip[64];
				sock_text((struct sockaddr *)&o->to, ip, sizeof ip, &port);
				host_port(ip, port, where, sizeof where);
			}
			sys_err(o, e, where);
		}
		post_op(opq_pop(&s->writes));
	}
}

// progress does what s's queues ask, as far as it can now.
static void progress(lsock *s, int64_t now) {
	switch (s->kind) {
	case K_CONN:
		// A handshake goes on while something waits on it.
		if (s->tls && s->tphase == T_HS && (s->reads.head || s->writes.head)) ak_net_tls->drive(s);
		conn_reads(s);
		conn_writes(s);
		break;
	case K_LISTEN: accepts(s, now); break;
	case K_UDP:
		receives(s);
		sends(s);
		break;
	}
}

// ---- Connecting, listening, binding ----

// order puts a's addresses as Happy Eyeballs tries them: the families
// interleaved, the first address's family first (RFC 8305, 4).
static void order(op *o) {
	uint32_t n = o->naddrs;
	if (n < 2) return;
	struct sockaddr_storage *a = ak_alloc(sizeof *a * n);
	socklen_t *l = ak_alloc(sizeof *l * n);
	int first = o->addrs[0].ss_family;
	uint32_t i = 0, j = 0, k = 0;  // the first family's next, the other's next
	while (k < n) {
		bool want_first = (k % 2 == 0);
		uint32_t *p = want_first ? &i : &j;
		while (*p < n && (o->addrs[*p].ss_family == first) != want_first) (*p)++;
		if (*p >= n) {
			// That family has no more: the rest of the other, in order.
			p = want_first ? &j : &i;
			while (*p < n && (o->addrs[*p].ss_family == first) == want_first) (*p)++;
		}
		a[k] = o->addrs[*p];
		l[k] = o->lens[*p];
		(*p)++;
		k++;
	}
	ak_free(o->addrs);
	ak_free(o->lens);
	o->addrs = a;
	o->lens = l;
}

static void connected(op *o, ak_sock fd, int family) {
	int on = o->nodelay;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
	set_keepalive(fd, o->keepalive_ns);
	lsock *s = lsock_new(fd, K_CONN);
	o->got = s->handle;
	o->got_fd = fd;
	o->got_family = family;
}

static void connect_fail(op *o) {
	char where[300];
	host_port(o->host, o->port, where, sizeof where);
	sys_err(o, o->first_err ? o->first_err : ERR(ECONNREFUSED), where);
}

// attempt_next starts the next address's attempt: false once there are
// none left to start. *won: it connected at once (o's answer filled).
static bool attempt_next(op *o, int64_t now, bool *won) {
	*won = false;
	while (o->next_addr < o->naddrs) {
		uint32_t i = o->next_addr++;
		const struct sockaddr_storage *a = &o->addrs[i];
		ak_sock s = socket(a->ss_family, SOCK_STREAM, IPPROTO_TCP);
		if (s == AK_BAD_SOCK) {
			if (!o->first_err) o->first_err = ak_sock_err();
			continue;
		}
		new_socket_flags(s);
		if (connect(s, (const struct sockaddr *)a, o->lens[i]) == 0) {
			connected(o, s, a->ss_family);
			*won = true;
			return true;
		}
		int e = ak_sock_err();
		if (AK_SOCK_INPROGRESS(e) || AK_SOCK_AGAIN(e)) {
			ak_vec_push(&o->tries, ((attempt){s, i}));
			o->next_at = now + ATTEMPT_DELAY_NS;
			return true;
		}
		if (!o->first_err) o->first_err = e;
		ak_sock_close(s);
	}
	return false;
}

// connect_step moves connect o on: true once it's done (posted).
static bool connect_step(op *o, int64_t now) {
	bool won;
	if (o->tries.len == 0 || (o->next_at && now >= o->next_at)) {
		if (!attempt_next(o, now, &won) && o->tries.len == 0) {
			connect_fail(o);
			return true;
		}
		if (won) return true;
		if (o->next_addr >= o->naddrs) o->next_at = 0;
	}
	return false;
}

// try_ready reads attempt t of o's outcome, once poll says it's done:
// true once o is done.
static bool attempt_ready(op *o, uint32_t t, int64_t now) {
	attempt a = o->tries.data[t];
	int err = 0;
	socklen_t el = sizeof err;
	if (getsockopt(a.fd, SOL_SOCKET, SO_ERROR, (char *)&err, &el) != 0) err = ak_sock_err();
	if (err == 0) {
		// Connected: the others go.
		o->tries.data[t] = o->tries.data[--o->tries.len];
		for (uint32_t i = 0; i < o->tries.len; i++) ak_sock_close(o->tries.data[i].fd);
		o->tries.len = 0;
		connected(o, a.fd, o->addrs[a.idx].ss_family);
		return true;
	}
	if (!o->first_err) o->first_err = err;
	ak_sock_close(a.fd);
	o->tries.data[t] = o->tries.data[--o->tries.len];
	if (o->tries.len == 0) {
		// The next starts now, not after the delay.
		o->next_at = now;
		return connect_step(o, now);
	}
	return false;
}

// bind_now opens o's listener or UDP socket at its addresses: done (o's
// answer filled) whether it could or not.
static void bind_now(op *o) {
	bool listener = o->kind == OP_LISTEN;
	int last = 0;
	char where[300];
	host_port(o->host[0] ? o->host : "*", o->port, where, sizeof where);
	// An empty host is every interface: IPv6's, dual-stack, first, so both
	// families' clients come. Otherwise IPv4's first: "localhost" is
	// 127.0.0.1 too, which every client tries.
	bool v6_first = o->host[0] == 0;
	for (int pass = 0; pass < 2; pass++) {
		for (uint32_t i = 0; i < o->naddrs; i++) {
			const struct sockaddr_storage *a = &o->addrs[i];
			if ((a->ss_family == (v6_first ? AF_INET6 : AF_INET)) != (pass == 0)) continue;
			ak_sock s = socket(a->ss_family, listener ? SOCK_STREAM : SOCK_DGRAM, 0);
			if (s == AK_BAD_SOCK) {
				last = ak_sock_err();
				continue;
			}
			new_socket_flags(s);
			int one = 1, zero = 0;
			if (listener || o->reuse) {
#if !defined(_WIN32)
				// A server started again binds while its last connections
				// wait out TIME_WAIT. (Windows' SO_REUSEADDR would let another
				// process take the port; its default binds as this does.)
				setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#if defined(SO_REUSEPORT) && defined(__APPLE__)
				if (o->reuse) setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
#else
				if (o->reuse) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#endif
			}
			if (a->ss_family == AF_INET6) setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&zero, sizeof zero);
#if defined(_WIN32)
			if (!listener) {
				// A datagram to a port nothing listens at would make the next
				// receive fail (WSAECONNRESET): a UDP socket has no connection.
				BOOL off = FALSE;
				DWORD got = 0;
				WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &got, NULL, NULL);
			}
#endif
			if (bind(s, (const struct sockaddr *)a, o->lens[i]) != 0 || (listener && listen(s, SOMAXCONN) != 0)) {
				last = ak_sock_err();
				ak_sock_close(s);
				continue;
			}
			lsock *l = lsock_new(s, listener ? K_LISTEN : K_UDP);
			if (listener) l->tls_conf = o->config;
			o->got = l->handle;
			o->got_fd = s;
			o->got_family = a->ss_family;
			return;
		}
	}
	sys_err(o, last ? last : ERR(EADDRNOTAVAIL), where);
}

// resolved goes on with o once its addresses are known.
static void resolved(op *o, int64_t now) {
	o->resolving = false;
	if (o->err) {
		opq_remove(&L.waiting, o);
		post_op(o);
		return;
	}
	if (o->kind != OP_CONNECT) {
		opq_remove(&L.waiting, o);
		bind_now(o);
		post_op(o);
		return;
	}
	order(o);
	if (connect_step(o, now)) {
		opq_remove(&L.waiting, o);
		post_op(o);
	}
}

// addresses_of looks up host (numeric: at once; a name: blocking, on a
// lookup's thread) for o's kind: o's answer, its addresses or its error.
static void addresses_of(const char *host, int port, uint8_t kind, ak_net_err *err, char **detail, struct sockaddr_storage **addrs, socklen_t **lens, uint32_t *naddrs) {
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = kind == OP_UDP ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV | (kind == OP_CONNECT ? 0 : AI_PASSIVE);
	char ps[16];
	snprintf(ps, sizeof ps, "%d", port);
	int g = getaddrinfo(host[0] ? host : NULL, ps, &hints, &res);
	*naddrs = 0;
	if (g != 0 || !res) {
		op tmp = {0};
		gai_err(&tmp, g ? g : EAI_NONAME, host);
		*err = tmp.err;
		*detail = tmp.detail;
		return;
	}
	uint32_t n = 0;
	for (struct addrinfo *a = res; a; a = a->ai_next)
		if (a->ai_family == AF_INET || a->ai_family == AF_INET6) n++;
	*addrs = ak_alloc(sizeof **addrs * (n ? n : 1));
	*lens = ak_alloc(sizeof **lens * (n ? n : 1));
	for (struct addrinfo *a = res; a; a = a->ai_next) {
		if (a->ai_family != AF_INET && a->ai_family != AF_INET6) continue;
		// The same address twice (getaddrinfo's, for each protocol) once.
		bool dup = false;
		for (uint32_t i = 0; i < *naddrs && !dup; i++) dup = (*lens)[i] == (socklen_t)a->ai_addrlen && memcmp(&(*addrs)[i], a->ai_addr, a->ai_addrlen) == 0;
		if (dup) continue;
		memcpy(&(*addrs)[*naddrs], a->ai_addr, a->ai_addrlen);
		(*lens)[*naddrs] = (socklen_t)a->ai_addrlen;
		(*naddrs)++;
	}
	freeaddrinfo(res);
	if (*naddrs == 0) {
		*err = AK_NE_NOTFOUND;
		*detail = dup_c(host);
	}
}

// is_numeric reports whether host needs no lookup: "" (every interface)
// or an IP address.
static bool is_numeric(const char *host) {
	if (!host[0]) return true;
	uint8_t b[16];
	const char *pct = strchr(host, '%');
	return ak_ip_parse(host, pct ? (size_t)(pct - host) : strlen(host), b) != 0;
}

// ---- The net thread's queue and loop ----

static void handle_op(op *o, int64_t now) {
	switch (o->kind) {
	case OP_CONNECT:
	case OP_LISTEN:
	case OP_UDP:
		opq_push(&L.waiting, o);
		if (!o->resolving) {
			addresses_of(o->host, o->port, o->kind, &o->err, &o->detail, &o->addrs, &o->lens, &o->naddrs);
			resolved(o, now);
		}
		return;
	}
	lsock *s = ak_idmap_get(&L.byhandle, o->handle);
	if (!s) {
		set_err(o, AK_NE_CLOSED, NULL);
		post_op(o);
		return;
	}
	if ((o->kind == OP_WRITE && (s->shut_wanted || s->shut))) {
		set_err(o, AK_NE_CLOSED, NULL);
		post_op(o);
		return;
	}
	if (o->kind == OP_READ) s->scan = 0;  // its delimiter's own search
	if (o->kind == OP_TLS && s->kind != K_CONN) {
		set_err(o, AK_NE_INVALID, "TLS is a connection's");
		post_op(o);
		return;
	}
	opq_push(o->kind == OP_WRITE || o->kind == OP_SEND ? &s->writes : &s->reads, o);
	progress(s, now);
}

static void cancel_op(cmd *k) {
	op *o = opq_find(&L.waiting, k->id);
	if (o) {
		opq_remove(&L.waiting, o);
	} else {
		lsock *s = ak_idmap_get(&L.byhandle, k->handle);
		if (!s) return;
		if ((o = opq_find(&s->reads, k->id))) {
			opq_remove(&s->reads, o);
		} else if ((o = opq_find(&s->writes, k->id))) {
			// A write that's begun goes on to its end: half of it would
			// leave the connection's stream torn. (Its caller's cancelled
			// all the same.)
			if (o->off > 0) return;
			opq_remove(&s->writes, o);
		} else {
			return;
		}
	}
	for (uint32_t i = 0; i < o->tries.len; i++) ak_sock_close(o->tries.data[i].fd);
	o->tries.len = 0;
	set_err(o, AK_NE_CANCELLED, NULL);
	post_op(o);
}

static void commands(int64_t now) {
	ak_mutex_lock(&L.mu);
	cmd *q = L.head;
	L.head = L.tail = NULL;
	ak_mutex_unlock(&L.mu);
	while (q) {
		cmd *k = q;
		q = q->next;
		switch (k->kind) {
		case C_OP:
			handle_op(k->op, now);
			break;
		case C_CANCEL:
			cancel_op(k);
			break;
		case C_CLOSE: {
			lsock *s = ak_idmap_get(&L.byhandle, k->handle);
			if (s) lsock_close(s);
			break;
		}
		case C_SHUT: {
			lsock *s = ak_idmap_get(&L.byhandle, k->handle);
			if (s && s->kind == K_CONN) {
				s->shut_wanted = true;
				conn_writes(s);
			}
			break;
		}
		case C_UNREAD: {
			lsock *s = ak_idmap_get(&L.byhandle, k->handle);
			if (s) {
				ak_buf b = {0};
				ak_buf_put(&b, k->data, k->len);
				ak_buf_put(&b, s->in.data, s->in.len);
				ak_buf_free(&s->in);
				s->in = b;
				s->scan = 0;
				progress(s, now);
			}
			break;
		}
		case C_TLS: {
			// A certificate checked on another thread: the handshake goes on.
			lsock *s = ak_idmap_get(&L.byhandle, k->handle);
			if (s && s->tls) {
				ak_net_tls->drive(s);
				progress(s, now);
			}
			break;
		}
		case C_RESOLVED: {
			op *o = opq_find(&L.waiting, k->id);
			if (o && o->resolving) {
				o->err = k->err;
				o->detail = k->detail;
				o->addrs = k->addrs;
				o->lens = k->lens;
				o->naddrs = k->naddrs;
				k->detail = NULL;
				k->addrs = NULL;
				k->lens = NULL;
				resolved(o, now);
			}
			break;
		}
		}
		cmd_free(k);
		ak_sched_unhold();  // send_cmd's, or a lookup's for its addresses
	}
}

// expire ends what's waited past its deadline.
static void expire(opq *q, int64_t now) {
	for (op *o = q->head; o;) {
		op *next = o->next;
		if (o->deadline && now >= o->deadline) {
			opq_remove(q, o);
			for (uint32_t i = 0; i < o->tries.len; i++) ak_sock_close(o->tries.data[i].fd);
			o->tries.len = 0;
			set_err(o, AK_NE_TIMEDOUT, NULL);
			post_op(o);
		}
		o = next;
	}
}

static int64_t soonest_of(opq *q, int64_t soonest) {
	for (op *o = q->head; o; o = o->next) {
		if (o->deadline && (!soonest || o->deadline < soonest)) soonest = o->deadline;
		if (o->next_at && (!soonest || o->next_at < soonest)) soonest = o->next_at;
	}
	return soonest;
}

// What each pollfd is: a socket (by slot and handle), or an attempt of
// a connect in the waiting list.
typedef struct {
	lsock *s;
	int64_t handle;
	op *o;
	uint64_t id;
} who_t;

static void loop_main(void *p) {
	AK_UNUSED(p);
	AK_VEC(ak_pollfd) pfd = {0};
	AK_VEC(who_t) who = {0};
	AK_VEC(int64_t) ready = {0};  // TLS connections with plaintext for a read already
	int64_t now = ak_now_ns();
	bool busy = true;
	ak_sched_hold();
	for (;;) {
		pfd.len = who.len = ready.len = 0;
		ak_vec_push(&pfd, ((ak_pollfd){.fd = ak_waker_fd(&L.wake), .events = POLLIN}));
		ak_vec_push(&who, ((who_t){0}));
		int64_t soonest = soonest_of(&L.waiting, 0);
		bool working = false;  // what will post back without another computer
		for (op *o = L.waiting.head; o; o = o->next) {
			if (o->tries.len) working = true;
			for (uint32_t t = 0; t < o->tries.len; t++) {
				ak_vec_push(&pfd, ((ak_pollfd){.fd = o->tries.data[t].fd, .events = POLLOUT}));
				ak_vec_push(&who, ((who_t){.o = o, .id = o->id}));
			}
		}
		for (uint32_t i = 0; i < L.nslots; i++) {
			lsock *s = L.slots[i];
			if (!s) continue;
			short ev = 0;
			soonest = soonest_of(&s->reads, soonest_of(&s->writes, soonest));
			switch (s->kind) {
			case K_CONN:
				if (s->tls) {
					bool now_ready = false;
					ev = ak_net_tls->events(s, &working, &now_ready);
					if (now_ready) ak_vec_push(&ready, s->handle);
					break;
				}
				if (wants_data(s)) ev |= POLLIN;
				if (s->writes.head) ev |= POLLOUT, working = true;
				break;
			case K_LISTEN:
				if (s->reads.head) {
					if (now >= s->pause_until) ev |= POLLIN;
					else if (!soonest || s->pause_until < soonest) soonest = s->pause_until;
				}
				break;
			case K_UDP:
				if (s->reads.head) ev |= POLLIN;
				if (s->writes.head) ev |= POLLOUT, working = true;
				break;
			}
			if (!ev) continue;
			ak_vec_push(&pfd, ((ak_pollfd){.fd = s->fd, .events = ev}));
			ak_vec_push(&who, ((who_t){.s = s, .handle = s->handle}));
		}
		int timeout = -1;
		if (soonest) {
			int64_t ms = (soonest - now + 999999) / 1000000;
			timeout = ms < 0 ? 0 : ms > 3600000 ? 3600000 : (int)ms;
		}
		if (ready.len) timeout = 0;
		int r;
		if (busy && !working && !soonest) {
			// Idle: a moment for what's on its way, then the scheduler's
			// let go until something comes.
			r = ak_sock_poll(pfd.data, pfd.len, LINGER_MS);
			if (r == 0) {
				busy = false;
				ak_sched_unhold();
				r = ak_sock_poll(pfd.data, pfd.len, -1);
			}
		} else {
			r = ak_sock_poll(pfd.data, pfd.len, timeout);
		}
		if (!busy) {
			busy = true;
			ak_sched_hold();
		}
		now = ak_now_ns();
		if (r < 0 && !AK_SOCK_INTR(ak_sock_err())) {
			// A descriptor that went bad under it (closed while polled):
			// start over with what's there.
			continue;
		}
		if (r > 0) {
			if (pfd.data[0].revents) {
				ak_waker_drain(&L.wake);
				commands(now);
			}
			for (uint32_t i = 1; i < pfd.len; i++) {
				short rv = pfd.data[i].revents;
				if (!rv) continue;
				who_t w = who.data[i];
				if (w.o) {
					// An attempt: its op may have ended meanwhile (cancelled,
					// another attempt won), its attempts moved.
					op *o = opq_find(&L.waiting, w.id);
					if (o != w.o) continue;
					uint32_t t = 0;
					while (t < o->tries.len && o->tries.data[t].fd != pfd.data[i].fd) t++;
					if (t == o->tries.len) continue;
					if (attempt_ready(o, t, now)) {
						opq_remove(&L.waiting, o);
						post_op(o);
					}
					continue;
				}
				// A command may have closed it, and another taken its slot.
				lsock *s = ak_idmap_get(&L.byhandle, w.handle);
				if (!s) continue;
				if (s->kind == K_CONN && s->tls) {
					ak_net_tls->ready(s);
					if (wants_data(s)) conn_fill(s);
				} else if (s->kind == K_CONN && (rv & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) && wants_data(s)) {
					conn_fill(s);
				}
				progress(s, now);
			}
		}
		for (uint32_t i = 0; i < ready.len; i++) {
			lsock *s = ak_idmap_get(&L.byhandle, ready.data[i]);
			if (!s || !s->tls) continue;
			if (wants_data(s)) conn_fill(s);
			progress(s, now);
		}
		// Connects whose next attempt is due, and deadlines.
		for (op *o = L.waiting.head; o;) {
			op *next = o->next;
			if (!o->resolving && o->next_at && now >= o->next_at && connect_step(o, now)) {
				opq_remove(&L.waiting, o);
				post_op(o);
			}
			o = next;
		}
		expire(&L.waiting, now);
		for (uint32_t i = 0; i < L.nslots; i++) {
			lsock *s = L.slots[i];
			if (!s) continue;
			expire(&s->reads, now);
			expire(&s->writes, now);
			if (s->kind == K_LISTEN && s->reads.head && now >= s->pause_until) accepts(s, now);
		}
	}
}

// send_cmd queues k for the net thread, starting it first if it hasn't:
// false when it can't (no threads, no sockets), k freed.
static bool send_cmd(cmd *k) {
	if (!L.started && !L.broken) {
		if (ak_sock_startup() && ak_waker_open(&L.wake) && ak_thread_start(loop_main, NULL)) L.started = true;
		else L.broken = true;
	}
	if (!L.started) {
		cmd_free(k);
		return false;
	}
	// Held until the net thread has done it.
	ak_sched_hold();
	ak_mutex_lock(&L.mu);
	if (L.tail) L.tail->next = k;
	else L.head = k;
	L.tail = k;
	ak_mutex_unlock(&L.mu);
	ak_waker_wake(&L.wake);
	return true;
}

static cmd *cmd_new(uint8_t kind, int64_t handle) {
	cmd *k = ak_alloc(sizeof *k);
	memset(k, 0, sizeof *k);
	k->kind = kind;
	k->handle = handle;
	return k;
}

// ---- Lookups' threads ----

typedef struct {
	uint64_t id;
	char *host;
	int port;
	uint8_t kind;
} resolve_job;

static void resolve_main(void *p) {
	resolve_job *j = p;
	cmd *k = cmd_new(C_RESOLVED, 0);
	k->id = j->id;
	addresses_of(j->host, j->port, j->kind, &k->err, &k->detail, &k->addrs, &k->lens, &k->naddrs);
	ak_mutex_lock(&L.mu);
	if (L.tail) L.tail->next = k;
	else L.head = k;
	L.tail = k;
	ak_mutex_unlock(&L.mu);
	ak_waker_wake(&L.wake);
	ak_free(j->host);
	ak_free(j);
}

// The TLS glue's hooks (nettls.c's, set by tls.c once a configuration
// is made): NULL in an app without TLS, which never has a TLS socket.
const ak_net_tls_hooks *ak_net_tls = NULL;

void ak_net_tls_notify(void *ctx) {
	cmd *k = cmd_new(C_TLS, (int64_t)(intptr_t)ctx);
	ak_mutex_lock(&L.mu);
	if (L.tail) L.tail->next = k;
	else L.head = k;
	L.tail = k;
	ak_mutex_unlock(&L.mu);
	ak_waker_wake(&L.wake);
}

// ---- The main thread's sockets ----

typedef struct group group;

typedef struct {
	int64_t handle;
	ak_sock fd;
} spare;

typedef struct {
	ak_bytes *data;
	char from[64];
	int port;
} spare_dgram;

typedef struct {
	int64_t handle;
	ak_sock fd;
	uint8_t kind;
	int family;
	bool connected;  // a UDP socket's: connect() set its peer
	group *g;
	uint32_t gi;
	// Its reads (a connection's), accepts (a listener's) or receives (a
	// UDP socket's) go to the net thread one at a time: rop is the one
	// there, rq those after it. So one cancelled comes back (what it got
	// put back: a read's bytes, the spares) before the next goes.
	struct op *rop;
	AK_VEC(struct op *) rq;
	AK_VEC(spare) spares;           // a listener's: accepted for an accept that was cancelled
	AK_VEC(spare_dgram) dgrams;     // a UDP socket's: received for a receive that was cancelled
	srv *server;
} msock;

// A group is the sockets an owner holds: they close as it goes.
struct group {
	ak_owner *owner;
	AK_VEC(msock *) socks;
};

static ak_idmap msocks;  // handle → msock
static ak_idmap groups;  // owner → group
static uint64_t next_op = 0;

static void close_handle(int64_t h);

static void group_gone(void *p) {
	group *g = p;
	ak_idmap_del(&groups, (int64_t)(uintptr_t)g->owner);
	while (g->socks.len) {
		msock *m = g->socks.data[g->socks.len - 1];
		close_handle(m->handle);  // takes it out of g
	}
	ak_vec_free(&g->socks);
	ak_free(g);
}

static void group_watch(void *p) { ak_on_cleanup(group_gone, p); }

static void group_add(ak_owner *o, msock *m) {
	if (!o || ak_owner_disposed(o)) return;
	group *g = ak_idmap_get(&groups, (int64_t)(uintptr_t)o);
	if (!g) {
		g = ak_alloc(sizeof *g);
		memset(g, 0, sizeof *g);
		g->owner = o;
		ak_idmap_put(&groups, (int64_t)(uintptr_t)o, g);
		ak_with_owner(o, group_watch, g);
	}
	m->g = g;
	m->gi = g->socks.len;
	ak_vec_push(&g->socks, m);
}

static void group_remove(msock *m) {
	group *g = m->g;
	if (!g) return;
	msock *last = g->socks.data[--g->socks.len];
	if (last != m) {
		g->socks.data[m->gi] = last;
		last->gi = m->gi;
	}
	m->g = NULL;
}

static msock *msock_new(int64_t handle, ak_sock fd, uint8_t kind, int family, ak_owner *owner) {
	msock *m = ak_alloc(sizeof *m);
	memset(m, 0, sizeof *m);
	m->handle = handle;
	m->fd = fd;
	m->kind = kind;
	m->family = family;
	ak_idmap_put(&msocks, handle, m);
	group_add(owner, m);
	return m;
}

static msock *find(int64_t h) { return h > 0 ? ak_idmap_get(&msocks, h) : NULL; }

static void srv_listener_closed(srv *s);

static void settle_queued(op *o, ak_net_err e);

static void close_handle(int64_t h) {
	msock *m = find(h);
	if (!m) {
		if (h > 0) send_cmd(cmd_new(C_CLOSE, h));
		return;
	}
	ak_idmap_del(&msocks, h);
	group_remove(m);
	// What waits behind the one the net thread has: closed. (That one
	// comes back closed.)
	for (uint32_t i = 0; i < m->rq.len; i++) settle_queued(m->rq.data[i], AK_NE_CLOSED);
	ak_vec_free(&m->rq);
	for (uint32_t i = 0; i < m->spares.len; i++) send_cmd(cmd_new(C_CLOSE, m->spares.data[i].handle));
	ak_vec_free(&m->spares);
	for (uint32_t i = 0; i < m->dgrams.len; i++) ak_release(m->dgrams.data[i].data);
	ak_vec_free(&m->dgrams);
	send_cmd(cmd_new(C_CLOSE, h));
	srv *s = m->server;
	ak_free(m);
	if (s) srv_listener_closed(s);
}

// ---- Ops on the main thread ----

// settle_queued settles an op that never went to the net thread: with
// e, or cancelled (AK_NE_CANCELLED).
static void settle_queued(op *o, ak_net_err e) {
	ak_future *f = o->fut;
	f->cancel = NULL;
	f->ctx = NULL;
	if (e == AK_NE_CANCELLED) {
		ak_throw_cancel();
		ak_future_reject(f, ak_error_take());
	} else {
		ak_future_reject(f, net_error(e, NULL));
	}
	ak_release(f);
	if (o->owner) ak_owner_release(o->owner);
	op_free(o);
}

static void op_cancel(ak_future *f) {
	op *o = f->ctx;
	if (!o) return;
	o->cancelled = true;
	if (o->queued) {
		msock *m = find(o->handle);
		for (uint32_t i = 0; m && i < m->rq.len; i++) {
			if (m->rq.data[i] != o) continue;
			memmove(m->rq.data + i, m->rq.data + i + 1, sizeof(op *) * (m->rq.len - i - 1));
			m->rq.len--;
			break;
		}
		settle_queued(o, AK_NE_CANCELLED);
		return;
	}
	cmd *k = cmd_new(C_CANCEL, o->handle);
	k->id = o->id;
	send_cmd(k);
}

static op *op_new(uint8_t kind, int64_t handle, int64_t timeout_ns) {
	op *o = ak_alloc(sizeof *o);
	memset(o, 0, sizeof *o);
	o->id = ++next_op;
	o->kind = kind;
	o->handle = handle;
	o->got_fd = AK_BAD_SOCK;
	if (timeout_ns > 0) o->deadline = ak_now_ns() + timeout_ns;
	return o;
}

// arm gives o a future of kind k for its caller, and its owner.
static ak_future *arm(op *o, ak_kind k) {
	ak_future *f = ak_future_new(k);
	o->fut = ak_retain(f);
	f->held = 1;  // the op's reference, until it comes back
	f->cancel = op_cancel;
	f->ctx = o;
	o->owner = ak_current_owner();
	if (o->owner) ak_owner_retain(o->owner);
	return f;
}

// dispatch sends o to the net thread.
static void dispatch(op *o) {
	cmd *c = cmd_new(C_OP, 0);
	c->op = o;
	if (!send_cmd(c)) {
		// No net thread: the op fails as it starts.
		set_err(o, AK_NE_UNSUPPORTED, "no sockets here");
		ak_post(op_done, o);
	}
}

// from_spare answers accept or receive o with what m kept for it, if it
// kept anything: whether it did (o settled and freed).
static bool from_spare(msock *m, op *o) {
	ak_future *f = o->fut;
	if (o->kind == OP_ACCEPT && m->spares.len) {
		spare sp = m->spares.data[0];
		memmove(m->spares.data, m->spares.data + 1, sizeof(spare) * (m->spares.len - 1));
		m->spares.len--;
		msock_new(sp.handle, sp.fd, K_CONN, AF_UNSPEC, o->owner);
		ak_future_resolve(f, (ak_val){.i = sp.handle});
	} else if (o->kind == OP_RECV && m->dgrams.len) {
		spare_dgram d = m->dgrams.data[0];
		memmove(m->dgrams.data, m->dgrams.data + 1, sizeof(spare_dgram) * (m->dgrams.len - 1));
		m->dgrams.len--;
		ak_arr *a = ak_arr_new(AK_K_REF, NULL, 3);
		char port[16];
		snprintf(port, sizeof port, "%d", d.port);
		ak_arr_push(&a, (ak_val){.p = d.data});
		ak_arr_push(&a, (ak_val){.p = ak_bytes_new(d.from, strlen(d.from))});
		ak_arr_push(&a, (ak_val){.p = ak_bytes_new(port, strlen(port))});
		ak_future_resolve(f, (ak_val){.p = a});
	} else {
		return false;
	}
	f->cancel = NULL;
	f->ctx = NULL;
	ak_release(f);
	if (o->owner) ak_owner_release(o->owner);
	op_free(o);
	return true;
}

// pump sends m's next waiting op to the net thread, once the last has
// come back.
static void pump(msock *m) {
	while (!m->rop && m->rq.len) {
		op *o = m->rq.data[0];
		memmove(m->rq.data, m->rq.data + 1, sizeof(op *) * (m->rq.len - 1));
		m->rq.len--;
		o->queued = false;
		if (from_spare(m, o)) continue;
		m->rop = o;
		dispatch(o);
	}
}

// serial is a read's, an accept's or a receive's start: m's, one at a
// time.
static ak_future *serial(msock *m, op *o, ak_kind k) {
	ak_future *f = arm(o, k);
	o->queued = true;
	ak_vec_push(&m->rq, o);
	pump(m);
	return f;
}

// start sends o to the net thread, with a future of kind k for its
// caller (none for a server's own accept): the future.
static ak_future *start(op *o, ak_kind k) {
	ak_future *f = o->server ? NULL : arm(o, k);
	cmd *c = cmd_new(C_OP, 0);
	c->op = o;
	if (!send_cmd(c)) {
		// No net thread: the op fails as it starts.
		set_err(o, AK_NE_UNSUPPORTED, "no sockets here");
		ak_post(op_done, o);
	}
	return f;
}

static void srv_accepted(op *o);

static void op_done(void *p) {
	op *o = p;
	if (o->server) {
		srv_accepted(o);
		return;
	}
	ak_future *f = o->fut;
	f->cancel = NULL;
	f->ctx = NULL;
	msock *m = find(o->handle);
	bool was_rop = m && m->rop == o;
	if (was_rop) m->rop = NULL;
	bool cancelled = o->cancelled || o->err == AK_NE_CANCELLED || (o->owner && ak_owner_disposed(o->owner) && o->got);
	if (cancelled) {
		// What it got by then isn't lost: it goes back.
		if (!o->err) {
			switch (o->kind) {
			case OP_READ:
				if (o->out.len && m) {
					cmd *k = cmd_new(C_UNREAD, o->handle);
					k->data = o->out.data;
					k->len = o->out.len;
					o->out = (ak_buf){0};
					send_cmd(k);
				}
				break;
			case OP_ACCEPT:
				if (m) ak_vec_push(&m->spares, ((spare){o->got, o->got_fd}));
				else send_cmd(cmd_new(C_CLOSE, o->got));
				break;
			case OP_RECV:
				if (m) {
					spare_dgram d = {.data = ak_bytes_new(o->out.data, o->out.len), .port = o->from_port};
					snprintf(d.from, sizeof d.from, "%s", o->from);
					ak_vec_push(&m->dgrams, d);
				}
				break;
			case OP_CONNECT:
			case OP_LISTEN:
			case OP_UDP:
				send_cmd(cmd_new(C_CLOSE, o->got));
				break;
			}
		}
		ak_throw_cancel();
		ak_future_reject(f, ak_error_take());
	} else if (o->err) {
		ak_future_reject(f, net_error(o->err, o->detail));
	} else {
		switch (o->kind) {
		case OP_CONNECT:
		case OP_ACCEPT:
		case OP_LISTEN:
		case OP_UDP: {
			uint8_t kind = o->kind == OP_LISTEN ? K_LISTEN : o->kind == OP_UDP ? K_UDP : K_CONN;
			msock_new(o->got, o->got_fd, kind, o->got_family, o->owner);
			ak_future_resolve(f, (ak_val){.i = o->got});
			break;
		}
		case OP_READ:
			ak_future_resolve(f, (ak_val){.p = o->none ? NULL : ak_bytes_new(o->out.data, o->out.len)});
			break;
		case OP_TLS:
			ak_future_resolve(f, (ak_val){.p = ak_net_tls->answer(o)});
			break;
		case OP_RECV: {
			ak_arr *a = ak_arr_new(AK_K_REF, NULL, 3);
			char port[16];
			snprintf(port, sizeof port, "%d", o->from_port);
			ak_arr_push(&a, (ak_val){.p = ak_bytes_new(o->out.data, o->out.len)});
			ak_arr_push(&a, (ak_val){.p = ak_bytes_new(o->from, strlen(o->from))});
			ak_arr_push(&a, (ak_val){.p = ak_bytes_new(port, strlen(port))});
			ak_future_resolve(f, (ak_val){.p = a});
			break;
		}
		default:
			ak_future_resolve(f, (ak_val){0});
		}
	}
	ak_release(f);
	if (o->owner) ak_owner_release(o->owner);
	op_free(o);
	if (was_rop) pump(m);
}

// ---- Servers: a task per connection ----

struct srv {
	int64_t id;
	int64_t listener;
	ak_owner *owner;  // the handlers' tasks', and their connections'
	ak_closure *dispatch;
	uint32_t tasks;
	bool accepting, closing, done;
	uint64_t timer;
	AK_VEC(ak_future *) waiters;  // close's and wait's
};

static AK_VEC(srv *) srvs;
static int64_t next_srv = 0;

typedef struct {
	srv *s;
	int64_t conn;
	ak_future *task;
} conn_task;

static srv *find_srv(int64_t id) {
	for (uint32_t i = 0; i < srvs.len; i++)
		if (srvs.data[i]->id == id) return srvs.data[i];
	return NULL;
}

static void srv_accept(srv *s) {
	if (s->accepting || s->closing) return;
	s->accepting = true;
	op *o = op_new(OP_ACCEPT, s->listener, 0);
	o->server = s;
	start(o, AK_K_VOID);
}

// srv_check lets s go once it has stopped: no accept out, no task left.
static void srv_check(srv *s) {
	if (s->done || !s->closing || s->accepting || s->tasks) return;
	s->done = true;
	if (s->timer) ak_timer_cancel(s->timer);
	s->timer = 0;
	for (uint32_t i = 0; i < srvs.len; i++) {
		if (srvs.data[i] == s) {
			srvs.data[i] = srvs.data[--srvs.len];
			break;
		}
	}
	// Its owner goes first (its cleanup reads s), then s.
	if (!ak_owner_disposed(s->owner)) ak_owner_dispose(s->owner);
	for (uint32_t i = 0; i < s->waiters.len; i++) {
		ak_future *f = s->waiters.data[i];
		if (f->state == AK_FUT_PENDING) ak_future_resolve(f, (ak_val){0});
		ak_release(f);
	}
	ak_vec_free(&s->waiters);
	ak_release(s->dispatch);
	ak_owner_release(s->owner);
	ak_free(s);
}

// srv_stop stops accepting: the listener closes.
static void srv_stop(srv *s) {
	if (s->closing) return;
	s->closing = true;
	msock *m = find(s->listener);
	if (m) {
		m->server = NULL;
		close_handle(s->listener);
	}
}

static void srv_listener_closed(srv *s) {
	// Closed under it (its owner went): it stops.
	s->closing = true;
	srv_check(s);
}

// srv_abort stops s at once: its handlers' tasks cancelled (their
// defers run), their connections closed.
static void srv_abort(srv *s) {
	if (s->done) return;
	srv_stop(s);
	if (s->timer) {
		ak_timer_cancel(s->timer);
		s->timer = 0;
	}
	if (!ak_owner_disposed(s->owner)) ak_owner_dispose(s->owner);
	srv_check(s);
}

static void srv_owner_gone(void *p) { srv_abort(p); }
static void srv_watch(void *p) { ak_on_cleanup(srv_owner_gone, p); }

static void srv_deadline(void *p) {
	srv *s = p;
	s->timer = 0;
	srv_abort(s);
}

static void conn_task_done(void *p) {
	conn_task *t = p;
	srv *s = t->s;
	close_handle(t->conn);
	if (t->task) ak_release(t->task);
	ak_free(t);
	s->tasks--;
	srv_check(s);
}

static void conn_task_start(void *p) {
	conn_task *t = p;
	ak_val arg = {.i = t->conn};
	ak_closure *d = t->s->dispatch;
	bool async = d->invoke != ak_vm_closure_invoke || ak_closure_async(d);
	ak_val v = ak_call(d, &arg, 1);
	if (async && !ak_error_pending()) {
		t->task = v.p;
		return;
	}
	t->task = ak_future_new(AK_K_VOID);
	if (ak_error_pending()) ak_future_reject(t->task, ak_error_take());
	else ak_future_resolve(t->task, (ak_val){0});
}

static void srv_accepted(op *o) {
	srv *s = o->server;
	s->accepting = false;
	if (o->err || s->closing || ak_owner_disposed(s->owner)) {
		if (!o->err) send_cmd(cmd_new(C_CLOSE, o->got));
		// A listener that fails (closed under it): the server stops.
		if (o->err) srv_stop(s);
		op_free(o);
		srv_check(s);
		return;
	}
	msock_new(o->got, o->got_fd, K_CONN, o->got_family, s->owner);
	conn_task *t = ak_alloc(sizeof *t);
	t->s = s;
	t->conn = o->got;
	t->task = NULL;
	op_free(o);
	s->tasks++;
	ak_with_owner(s->owner, conn_task_start, t);
	ak_future_wait(t->task, conn_task_done, t);
	srv_accept(s);
}

// ---- Lookups ----

enum { LK_HOST, LK_ADDR, LK_RECORDS, LK_CANON };

typedef struct {
	uint8_t kind;
	char *name;
	int type;
	ak_future *fut;
	bool cancelled;
	ak_dns_result r;
	char *canon;
} lookup;

static void lookup_post(void *p);

static void lookup_cancel(ak_future *f) {
	lookup *l = f->ctx;
	f->ctx = NULL;
	if (l) l->cancelled = true;
	// The system's lookup can't be stopped: its answer, when it comes, is
	// let go.
	ak_throw_cancel();
	ak_future_reject(f, ak_error_take());
}

static void lookup_main(void *p) {
	lookup *l = p;
	ak_dns_result *r = &l->r;
	switch (l->kind) {
	case LK_HOST:
	case LK_CANON: {
		struct addrinfo hints = {0}, *res = NULL;
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (l->kind == LK_CANON) hints.ai_flags = AI_CANONNAME;
		int g = getaddrinfo(l->name, NULL, &hints, &res);
		if (g != 0 || !res) {
			op tmp = {0};
			gai_err(&tmp, g ? g : EAI_NONAME, l->name);
			r->err = tmp.err;
			ak_dns_why(r, "%s", tmp.detail ? tmp.detail : "");
			ak_free(tmp.detail);
			break;
		}
		if (l->kind == LK_CANON) {
			const char *c = res->ai_canonname ? res->ai_canonname : l->name;
			l->canon = dup_c(c);
			size_t n = strlen(l->canon);
			if (n && l->canon[n - 1] == '.') l->canon[n - 1] = 0;
		}
		for (struct addrinfo *a = res; a && l->kind == LK_HOST; a = a->ai_next) {
			char ip[64];
			int port;
			if (a->ai_family != AF_INET && a->ai_family != AF_INET6) continue;
			sock_text(a->ai_addr, ip, sizeof ip, &port);
			bool dup = false;
			for (uint32_t i = 0; i < r->fields.len && !dup; i++) dup = strcmp(r->fields.data[i], ip) == 0;
			if (!dup) ak_vec_push(&r->fields, dup_c(ip));
		}
		freeaddrinfo(res);
		break;
	}
	case LK_ADDR: {
		struct sockaddr_storage a;
		socklen_t al;
		char host[1025];
		if (!numeric(l->name, 0, &a, &al)) {
			r->err = AK_NE_INVALID;
			ak_dns_why(r, "%s isn't an IP address", l->name);
			break;
		}
		int g = getnameinfo((struct sockaddr *)&a, al, host, sizeof host, NULL, 0, NI_NAMEREQD);
		if (g != 0) {
			op tmp = {0};
			gai_err(&tmp, g, l->name);
			r->err = tmp.err;
			ak_dns_why(r, "%s", tmp.detail ? tmp.detail : "");
			ak_free(tmp.detail);
			break;
		}
		size_t n = strlen(host);
		if (n && host[n - 1] == '.') host[n - 1] = 0;
		ak_vec_push(&r->fields, dup_c(host));
		break;
	}
	case LK_RECORDS:
		ak_dns_lookup(l->name, l->type, r);
		if (r->err && r->err != AK_NE_NOTFOUND && r->err != AK_NE_INVALID && r->err != AK_NE_UNSUPPORTED && r->err != AK_NE_TIMEDOUT) {
			// lookupFailed's detail: the name, then why.
			char *why = dup_c(r->why);
			ak_dns_why(r, "%s:%s", l->name, why);
			ak_free(why);
		} else if (r->err == AK_NE_NOTFOUND) {
			ak_dns_why(r, "%s", l->name);
		}
		break;
	}
	ak_post(lookup_post, l);
}

static void lookup_post(void *p) {
	lookup *l = p;
	ak_sched_unhold();
	ak_future *f = l->fut;
	if (!l->cancelled) {
		f->cancel = NULL;
		f->ctx = NULL;
		if (l->r.err) {
			ak_future_reject(f, net_error(l->r.err, l->r.why));
		} else if (l->kind == LK_CANON) {
			ak_future_resolve(f, (ak_val){.p = ak_str_c(l->canon ? l->canon : "")});
		} else {
			ak_arr *a = ak_str_arr(l->r.fields.len);
			for (uint32_t i = 0; i < l->r.fields.len; i++) ak_str_arr_push(&a, l->r.fields.data[i], strlen(l->r.fields.data[i]));
			ak_future_resolve(f, (ak_val){.p = a});
		}
	}
	ak_release(f);
	ak_dns_result_free(&l->r);
	ak_free(l->canon);
	ak_free(l->name);
	ak_free(l);
}

static ak_val lookup_start(uint8_t kind, const ak_str *name, int type) {
	ak_kind k = AK_K_REF;
	if (!ak_sock_startup()) return (ak_val){.p = failed_future(k, AK_NE_UNSUPPORTED, "")};
	lookup *l = ak_alloc(sizeof *l);
	memset(l, 0, sizeof *l);
	l->kind = kind;
	l->name = dup_c(name->data);
	l->type = type;
	ak_future *f = ak_future_new(k);
	l->fut = ak_retain(f);
	f->held = 1;  // the lookup's reference, until it answers
	f->cancel = lookup_cancel;
	f->ctx = l;
	ak_sched_hold();
	if (!ak_thread_start(lookup_main, l)) lookup_main(l);
	return (ak_val){.p = f};
}

// ---- The builtins ----

static ak_val available_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.i = 1};
}

static bool port_ok(int64_t port) { return port >= 0 && port <= 65535; }

// begin sends o, made for host and port, to the net thread, and for a
// host that's a name starts its lookup's thread: it hands the addresses
// to the net thread, which has o (queued before them), and holds the
// scheduler until it has. (o is the net thread's once it's sent: what
// the lookup needs is read before.)
static ak_future *begin(op *o, const char *host, int port) {
	resolve_job *j = NULL;
	if (o->resolving) {
		j = ak_alloc(sizeof *j);
		j->id = o->id;
		j->host = dup_c(host);
		j->port = port;
		j->kind = o->kind;
	}
	ak_future *f = start(o, AK_K_INT);
	if (j && !L.started) {
		ak_free(j->host);
		ak_free(j);
	} else if (j) {
		ak_sched_hold();
		if (!ak_thread_start(resolve_main, j)) resolve_main(j);
	}
	return f;
}

static bool host_ok(const ak_str *host) { return strlen(host->data) == host->len; }

static ak_val connect_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *host = STR(0);
	int64_t port = args[1].i;
	if (!port_ok(port) || port == 0) return (ak_val){.p = failed_future(AK_K_INT, AK_NE_INVALID, "a port to connect to is 1 to 65535")};
	if (host->len == 0 || !host_ok(host)) return (ak_val){.p = failed_future(AK_K_INT, AK_NE_INVALID, "a host to connect to is a name or an address")};
	op *o = op_new(OP_CONNECT, 0, args[2].i);
	o->host = dup_c(host->data);
	o->port = (int)port;
	o->nodelay = args[3].i != 0;
	o->keepalive_ns = args[4].i;
	o->resolving = !is_numeric(o->host);
	return (ak_val){.p = begin(o, host->data, (int)port)};
}

// bind_op is a listen's or a UDP socket's: host and port to bind.
static ak_val bind_op(uint8_t kind, ak_val *args, uint32_t nargs, bool reuse) {
	const ak_str *host = STR(0);
	int64_t port = args[1].i;
	if (!port_ok(port)) return (ak_val){.p = failed_future(AK_K_INT, AK_NE_INVALID, "a port is 0 to 65535")};
	if (!host_ok(host)) return (ak_val){.p = failed_future(AK_K_INT, AK_NE_INVALID, "a host is a name or an address")};
	op *o = op_new(kind, 0, 0);
	o->host = dup_c(host->data);
	o->port = (int)port;
	o->reuse = reuse;
	if (kind == OP_LISTEN && nargs > 2) o->config = args[2].i;  // a TLS listener's configuration
	o->resolving = !is_numeric(o->host);
	return (ak_val){.p = begin(o, host->data, (int)port)};
}

static ak_val listen_fn(ak_val *args, uint32_t n) {
	return bind_op(OP_LISTEN, args, n, false);
}

static ak_val udp_open_fn(ak_val *args, uint32_t n) {
	return bind_op(OP_UDP, args, n, args[2].i != 0);
}

static ak_val accept_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m || m->kind != K_LISTEN) return (ak_val){.p = failed_future(AK_K_INT, AK_NE_CLOSED, "")};
	if (m->server) return (ak_val){.p = failed_future(AK_K_INT, AK_NE_INVALID, "the listener is a server's: it accepts for it")};
	op *o = op_new(OP_ACCEPT, m->handle, args[1].i);
	return (ak_val){.p = serial(m, o, AK_K_INT)};
}

static void push_addr(ak_arr **a, const struct sockaddr *sa, bool ok) {
	char ip[64] = "", port[16] = "";
	int p = 0;
	if (ok) {
		sock_text(sa, ip, sizeof ip, &p);
		snprintf(port, sizeof port, "%d", p);
	}
	ak_str_arr_push(a, ip, strlen(ip));
	ak_str_arr_push(a, port, strlen(port));
}

static ak_val addresses_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	ak_arr *a = ak_str_arr(4);
	if (!m) return (ak_val){.p = a};
	struct sockaddr_storage s;
	socklen_t sl = sizeof s;
	bool ok = getsockname(m->fd, (struct sockaddr *)&s, &sl) == 0;
	push_addr(&a, (struct sockaddr *)&s, ok);
	sl = sizeof s;
	ok = m->kind != K_LISTEN && getpeername(m->fd, (struct sockaddr *)&s, &sl) == 0;
	push_addr(&a, (struct sockaddr *)&s, ok);
	return (ak_val){.p = a};
}

static ak_val read_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m || m->kind != K_CONN) return (ak_val){.p = failed_future(AK_K_REF, AK_NE_CLOSED, "")};
	// One read at a time: another may wait only behind one that was
	// cancelled (it comes back first, what it read put back).
	bool busy = (m->rop && !m->rop->cancelled) || m->rq.len;
	if (busy) return (ak_val){.p = failed_future(AK_K_REF, AK_NE_INVALID, "a connection has one read at a time: another is waiting")};
	int64_t mode = args[1].i, want = args[2].i;
	const ak_bytes *delim = args[3].p;
	if (want < 0 || (mode == RD_UNTIL && delim->len == 0)) return (ak_val){.p = failed_future(AK_K_REF, AK_NE_INVALID, "a read's size is 0 or more, its delimiter not empty")};
	op *o = op_new(OP_READ, m->handle, args[4].i);
	o->mode = (uint8_t)mode;
	o->n = (size_t)want;
	if (mode == RD_UNTIL) {
		o->data = ak_alloc(delim->len);
		memcpy(o->data, delim->data, delim->len);
		o->len = delim->len;
	}
	if (mode == RD_EXACT && want == 0) {
		op_free(o);
		ak_future *f = ak_future_new(AK_K_REF);
		ak_future_resolve(f, (ak_val){.p = ak_bytes_new(NULL, 0)});
		return (ak_val){.p = f};
	}
	return (ak_val){.p = serial(m, o, AK_K_REF)};
}

static ak_val write_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	const ak_bytes *b = args[1].p;
	if (!m || m->kind != K_CONN) return (ak_val){.p = failed_future(AK_K_VOID, AK_NE_CLOSED, "")};
	if (b->len == 0) {
		ak_future *f = ak_future_new(AK_K_VOID);
		ak_future_resolve(f, (ak_val){0});
		return (ak_val){.p = f};
	}
	op *o = op_new(OP_WRITE, m->handle, args[2].i);
	o->data = ak_alloc(b->len);
	memcpy(o->data, b->data, b->len);
	o->len = b->len;
	return (ak_val){.p = start(o, AK_K_VOID)};
}

static ak_val close_write_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (m && m->kind == K_CONN) send_cmd(cmd_new(C_SHUT, m->handle));
	return (ak_val){0};
}

static ak_val close_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m) return (ak_val){0};
	if (m->server) {
		srv *s = m->server;
		m->server = NULL;
		close_handle(m->handle);
		srv_listener_closed(s);
		return (ak_val){0};
	}
	close_handle(m->handle);
	return (ak_val){0};
}

static void option_error(int e) {
	op tmp = {0};
	sys_err(&tmp, e, "");
	throw_net(tmp.err, tmp.detail);
	ak_free(tmp.detail);
}

static ak_val option_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m) {
		throw_net(AK_NE_CLOSED, "");
		return (ak_val){0};
	}
	int64_t v = args[2].i;
	int r = 0, on = v != 0;
	switch (args[1].i) {
	case OPT_NODELAY:
		if (m->kind != K_CONN) break;
		r = setsockopt(m->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
		break;
	case OPT_KEEPALIVE:
		if (m->kind != K_CONN) break;
		r = set_keepalive(m->fd, v);
		break;
	case OPT_BROADCAST:
		if (m->kind != K_UDP) break;
		r = setsockopt(m->fd, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on);
		break;
	}
	if (r != 0) option_error(ak_sock_err());
	return (ak_val){0};
}

// dest fills a with ip's address and port for m to send to or connect
// to: an IPv4 address mapped into IPv6 for a dual-stack socket. false,
// with an error thrown, for one it can't.
static bool dest(msock *m, const ak_str *ip, int64_t port, struct sockaddr_storage *a, socklen_t *al) {
	if (port <= 0 || port > 65535) {
		throw_net(AK_NE_INVALID, "a port to send to is 1 to 65535");
		return false;
	}
	if (!numeric(ip->data, (int)port, a, al)) {
		char b[300];
		snprintf(b, sizeof b, "%s isn't an IP address", ip->data);
		throw_net(AK_NE_INVALID, b);
		return false;
	}
	if (m->family == AF_INET6 && a->ss_family == AF_INET) {
		struct sockaddr_in v4;
		memcpy(&v4, a, sizeof v4);
		struct sockaddr_in6 v6;
		memset(&v6, 0, sizeof v6);
		v6.sin6_family = AF_INET6;
		v6.sin6_port = v4.sin_port;
		uint8_t *b = (uint8_t *)&v6.sin6_addr;
		b[10] = b[11] = 0xff;
		memcpy(b + 12, &v4.sin_addr, 4);
		memset(a, 0, sizeof *a);
		memcpy(a, &v6, sizeof v6);
		*al = sizeof v6;
	} else if (m->family == AF_INET && a->ss_family == AF_INET6) {
		throw_net(AK_NE_INVALID, "a socket bound to an IPv4 address sends to IPv4 addresses only");
		return false;
	}
	return true;
}

static ak_val udp_connect_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m || m->kind != K_UDP) {
		throw_net(AK_NE_CLOSED, "");
		return (ak_val){0};
	}
	struct sockaddr_storage a;
	socklen_t al;
	if (!dest(m, STR(1), args[2].i, &a, &al)) return (ak_val){0};
	if (connect(m->fd, (struct sockaddr *)&a, al) != 0) option_error(ak_sock_err());
	else m->connected = true;
	return (ak_val){0};
}

static ak_val udp_send_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	const ak_bytes *b = args[1].p;
	if (!m || m->kind != K_UDP) return (ak_val){.p = failed_future(AK_K_VOID, AK_NE_CLOSED, "")};
	if (b->len > 65507) return (ak_val){.p = failed_future(AK_K_VOID, AK_NE_TOOLARGE, "65507")};
	// A connected socket sends to its peer alone (BSD's sendto refuses
	// another address; Linux's would send there): the same everywhere.
	if (STR(2)->len && m->connected) return (ak_val){.p = failed_future(AK_K_VOID, AK_NE_INVALID, "a connected UDP socket sends to the address it's connected to: send(data) without to:")};
	if (!STR(2)->len && !m->connected) return (ak_val){.p = failed_future(AK_K_VOID, AK_NE_INVALID, "send needs an address to send to: to:, or connect() first")};
	op *o = op_new(OP_SEND, m->handle, args[4].i);
	if (STR(2)->len) {
		if (!dest(m, STR(2), args[3].i, &o->to, &o->tolen)) {
			op_free(o);
			ak_future *f = ak_future_new(AK_K_VOID);
			ak_future_reject(f, ak_error_take());
			return (ak_val){.p = f};
		}
	}
	o->data = ak_alloc(b->len ? b->len : 1);
	memcpy(o->data, b->data, b->len);
	o->len = b->len;
	return (ak_val){.p = start(o, AK_K_VOID)};
}

static ak_val udp_receive_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m || m->kind != K_UDP) return (ak_val){.p = failed_future(AK_K_REF, AK_NE_CLOSED, "")};
	op *o = op_new(OP_RECV, m->handle, args[1].i);
	return (ak_val){.p = serial(m, o, AK_K_REF)};
}

static ak_val udp_group_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m || m->kind != K_UDP) {
		throw_net(AK_NE_CLOSED, "");
		return (ak_val){0};
	}
	const ak_str *g = STR(1);
	bool join = args[2].i != 0;
	uint8_t b[16];
	int k = ak_ip_parse(g->data, g->len, b);
	if (!k || (k == 4 ? (b[0] & 0xf0) != 0xe0 : b[0] != 0xff)) {
		throw_net(AK_NE_INVALID, "a group is a multicast address: 224.0.0.0/4, ff00::/8");
		return (ak_val){0};
	}
	int r;
	if (k == 4) {
		struct ip_mreq q;
		memset(&q, 0, sizeof q);
		memcpy(&q.imr_multiaddr, b, 4);
		q.imr_interface.s_addr = htonl(INADDR_ANY);
		r = setsockopt(m->fd, IPPROTO_IP, join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, (const char *)&q, sizeof q);
	} else {
		if (m->family != AF_INET6) {
			throw_net(AK_NE_INVALID, "an IPv6 group needs a socket bound to an IPv6 address (\"::\")");
			return (ak_val){0};
		}
		struct ipv6_mreq q;
		memset(&q, 0, sizeof q);
		memcpy(&q.ipv6mr_multiaddr, b, 16);
		q.ipv6mr_interface = 0;
#if defined(IPV6_JOIN_GROUP)
		r = setsockopt(m->fd, IPPROTO_IPV6, join ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP, (const char *)&q, sizeof q);
#else
		r = setsockopt(m->fd, IPPROTO_IPV6, join ? IPV6_ADD_MEMBERSHIP : IPV6_DROP_MEMBERSHIP, (const char *)&q, sizeof q);
#endif
	}
	if (r != 0) option_error(ak_sock_err());
	return (ak_val){0};
}

static ak_val lookup_host_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return lookup_start(LK_HOST, STR(0), 0);
}

static ak_val lookup_addr_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return lookup_start(LK_ADDR, STR(0), 0);
}

static ak_val lookup_records_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return lookup_start(LK_RECORDS, STR(0), (int)args[1].i);
}

static ak_val lookup_canonical_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return lookup_start(LK_CANON, STR(0), 0);
}

ak_future *ak_net_tls_start(int64_t h, int64_t config, const char *server_name, bool server, int64_t timeout_ns) {
	msock *m = find(h);
	if (!m || m->kind != K_CONN) return failed_future(AK_K_REF, AK_NE_CLOSED, "");
	bool busy = (m->rop && !m->rop->cancelled) || m->rq.len;
	if (busy) return failed_future(AK_K_REF, AK_NE_INVALID, "TLS starts on a connection with no read waiting");
	op *o = op_new(OP_TLS, m->handle, timeout_ns);
	o->config = config;
	o->tls_server = server;
	o->host = dup_c(server_name);
	return serial(m, o, AK_K_REF);
}

static ak_val serve_start_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	msock *m = find(args[0].i);
	if (!m || m->kind != K_LISTEN || m->server) return (ak_val){.i = 0};
	srv *s = ak_alloc(sizeof *s);
	memset(s, 0, sizeof *s);
	s->id = ++next_srv;
	s->listener = m->handle;
	s->dispatch = ak_retain(args[1].p);
	s->owner = ak_owner_new();
	ak_owner_retain(s->owner);
	ak_with_owner(s->owner, srv_watch, s);
	m->server = s;
	ak_vec_push(&srvs, s);
	srv_accept(s);
	return (ak_val){.i = s->id};
}

static ak_future *done_void(void) {
	ak_future *f = ak_future_new(AK_K_VOID);
	ak_future_resolve(f, (ak_val){0});
	return f;
}

static ak_val serve_close_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	srv *s = find_srv(args[0].i);
	if (!s) return (ak_val){.p = done_void()};
	ak_future *f = ak_future_new(AK_K_VOID);
	ak_vec_push(&s->waiters, ak_retain(f));
	if (!s->closing) {
		srv_stop(s);
		int64_t grace = args[1].i;
		if (grace <= 0 || s->tasks == 0) srv_abort(s);
		else s->timer = ak_after(grace, srv_deadline, s);
	}
	srv_check(s);
	return (ak_val){.p = f};
}

static ak_val serve_wait_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	srv *s = find_srv(args[0].i);
	if (!s) return (ak_val){.p = done_void()};
	ak_future *f = ak_future_new(AK_K_VOID);
	ak_vec_push(&s->waiters, ak_retain(f));
	return (ak_val){.p = f};
}

const ak_lib_entry ak_std_net[] = {
	{"net.available", available_fn},
	{"net.ipParse", ip_parse_fn},
	{"net.ipText", ip_text_fn},
	{"net.tcpConnect", connect_fn},
	{"net.tcpListen", listen_fn},
	{"net.tcpAccept", accept_fn},
	{"net.addresses", addresses_fn},
	{"net.tcpRead", read_fn},
	{"net.tcpWrite", write_fn},
	{"net.tcpCloseWrite", close_write_fn},
	{"net.sockClose", close_fn},
	{"net.setOption", option_fn},
	{"net.udpOpen", udp_open_fn},
	{"net.udpConnect", udp_connect_fn},
	{"net.udpSend", udp_send_fn},
	{"net.udpReceive", udp_receive_fn},
	{"net.udpGroup", udp_group_fn},
	{"net.lookupHost", lookup_host_fn},
	{"net.lookupAddr", lookup_addr_fn},
	{"net.lookupRecords", lookup_records_fn},
	{"net.lookupCanonical", lookup_canonical_fn},
	{"net.serveStart", serve_start_fn},
	{"net.serveClose", serve_close_fn},
	{"net.serveWait", serve_wait_fn},
	{NULL, NULL},
};

#endif
