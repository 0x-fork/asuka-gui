// dns.c: std/net's DNS records (net.lookupMX, TXT, SRV, NS): what
// getaddrinfo doesn't answer. The system's resolver asks on every
// platform that has one for records: Apple's DNSService (mDNSResponder,
// so a VPN's and a scoped resolver's settings count), Android's
// android_res_nquery (API 29, the system's resolver, Private DNS
// included), Windows' DnsQuery_W (dnsapi.dll, loaded when it's first
// needed). Linux has none apart from libresolv, which isn't on every
// system nor in a static build: there, as Go does, a stub resolver of
// its own asks the servers /etc/resolv.conf names, over UDP and TCP when
// the answer is truncated. Answers in DNS's wire format (Android's,
// Linux's, and Apple's records' data) are read by one parser here. The
// web has no lookups (net.c says so before any of this runs).
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif

#if !defined(__wasm__)
#include "std/sock.h"
#endif
#include "std/net.h"
#include "std/std.h"
#include "base/thread.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <dns_sd.h>
#elif defined(__ANDROID__)
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windns.h>
#endif

void ak_dns_result_free(ak_dns_result *r) {
	for (uint32_t i = 0; i < r->fields.len; i++) ak_free(r->fields.data[i]);
	ak_vec_free(&r->fields);
}

void ak_dns_why(ak_dns_result *r, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(r->why, sizeof r->why, fmt, ap);
	va_end(ap);
}

static void add_field(ak_dns_result *r, const char *s, size_t n) {
	char *d = ak_alloc(n + 1);
	memcpy(d, s, n);
	d[n] = 0;
	ak_vec_push(&r->fields, d);
}

static void add_int(ak_dns_result *r, unsigned v) {
	char b[16];
	int k = snprintf(b, sizeof b, "%u", v);
	add_field(r, b, (size_t)k);
}

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

// read_name reads the name at msg[*off] (its labels, and the names its
// pointers lead to) as text: labels joined by dots, without the trailing
// one ("" for the root); a dot or a backslash in a label, and a byte that
// isn't printable ASCII, escaped as \. and \DDD. *off moves past it (past
// its first pointer).
static bool read_name(const uint8_t *msg, size_t n, size_t *off, char *out, size_t cap) {
	size_t p = *off, w = 0, total = 0;
	bool jumped = false;
	int hops = 0;
	for (;;) {
		if (p >= n) return false;
		uint8_t l = msg[p];
		if ((l & 0xc0) == 0xc0) {
			if (p + 1 >= n) return false;
			size_t q = (size_t)(l & 0x3f) << 8 | msg[p + 1];
			if (!jumped) *off = p + 2;
			jumped = true;
			if (++hops > 64 || q >= n) return false;
			p = q;
			continue;
		}
		if (l & 0xc0) return false;  // the extended label types: none in use
		p++;
		if (l == 0) {
			if (!jumped) *off = p;
			break;
		}
		if (p + l > n) return false;
		total += (size_t)l + 1;
		if (total > 255) return false;
		if (w) {
			if (w + 1 >= cap) return false;
			out[w++] = '.';
		}
		for (size_t i = 0; i < l; i++) {
			uint8_t c = msg[p + i];
			if (w + 5 >= cap) return false;
			if (c == '.' || c == '\\') {
				out[w++] = '\\';
				out[w++] = (char)c;
			} else if (c < 0x21 || c > 0x7e) {
				w += (size_t)snprintf(out + w, cap - w, "\\%03d", c);
			} else {
				out[w++] = (char)c;
			}
		}
		p += l;
	}
	out[w] = 0;
	return true;
}

#define NAME_CAP 1100

bool ak_dns_add_rdata(ak_dns_result *r, int type, const uint8_t *msg, size_t n, size_t off, size_t len) {
	if (off + len > n) return false;
	char name[NAME_CAP];
	size_t p = off, end = off + len;
	switch (type) {
	case AK_DNS_MX: {
		if (len < 3) return false;
		unsigned pref = u16(msg + p);
		p += 2;
		if (!read_name(msg, n, &p, name, sizeof name) || p > end) return false;
		add_int(r, pref);
		add_field(r, name, strlen(name));
		return true;
	}
	case AK_DNS_SRV: {
		if (len < 7) return false;
		unsigned prio = u16(msg + p), weight = u16(msg + p + 2), port = u16(msg + p + 4);
		p += 6;
		if (!read_name(msg, n, &p, name, sizeof name) || p > end) return false;
		add_int(r, prio);
		add_int(r, weight);
		add_int(r, port);
		add_field(r, name, strlen(name));
		return true;
	}
	case AK_DNS_TXT: {
		ak_buf b = {0};
		while (p < end) {
			size_t l = msg[p++];
			if (p + l > end) {
				ak_buf_free(&b);
				return false;
			}
			ak_buf_put(&b, msg + p, l);
			p += l;
		}
		add_field(r, b.len ? (const char *)b.data : "", b.len);
		ak_buf_free(&b);
		return true;
	}
	case AK_DNS_NS:
	case AK_DNS_CNAME:
	case AK_DNS_PTR:
		if (!read_name(msg, n, &p, name, sizeof name) || p > end) return false;
		add_field(r, name, strlen(name));
		return true;
	case AK_DNS_A:
	case AK_DNS_AAAA: {
		if (len != (type == AK_DNS_A ? 4u : 16u)) return false;
		char t[46];
		ak_ip_format(msg + off, (int)len, t);
		add_field(r, t, strlen(t));
		return true;
	}
	}
	return false;
}

static const char *type_name(int type) {
	switch (type) {
	case AK_DNS_MX: return "MX";
	case AK_DNS_TXT: return "TXT";
	case AK_DNS_SRV: return "SRV";
	case AK_DNS_NS: return "NS";
	case AK_DNS_CNAME: return "CNAME";
	case AK_DNS_PTR: return "PTR";
	case AK_DNS_A: return "A";
	case AK_DNS_AAAA: return "AAAA";
	}
	return "such";
}

// same_name compares a name as read_name writes it with one asked for
// (a trailing dot or not), ignoring ASCII case.
static bool same_name(const char *got, const char *asked) {
	size_t a = strlen(asked);
	if (a && asked[a - 1] == '.') a--;
	if (strlen(got) != a) return false;
	for (size_t i = 0; i < a; i++) {
		char x = got[i], y = asked[i];
		if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
		if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
		if (x != y) return false;
	}
	return true;
}

static void not_found(ak_dns_result *r, int type) {
	r->err = AK_NE_NOTFOUND;
	ak_dns_why(r, "no %s records", type_name(type));
}

bool ak_dns_parse(const uint8_t *msg, size_t n, int id, const char *name, int type, ak_dns_result *r, bool *truncated) {
	*truncated = false;
	if (n < 12) return false;
	if (id >= 0 && u16(msg) != id) return false;
	uint16_t flags = u16(msg + 2);
	if (!(flags & 0x8000)) return false;  // not an answer
	unsigned qd = u16(msg + 4), an = u16(msg + 6);
	size_t p = 12;
	char got[NAME_CAP];
	if (qd != 1) return false;
	if (!read_name(msg, n, &p, got, sizeof got) || p + 4 > n) return false;
	if (u16(msg + p) != type || u16(msg + p + 2) != 1 || !same_name(got, name)) return false;
	p += 4;
	if (flags & 0x0200) {
		*truncated = true;
		return true;
	}
	int rcode = flags & 0xf;
	if (rcode == 3) {
		r->err = AK_NE_NOTFOUND;
		ak_dns_why(r, "no such host");
		return true;
	}
	if (rcode != 0) {
		r->err = AK_NE_LOOKUP;
		const char *why = rcode == 2 ? "the DNS server failed (SERVFAIL)" : rcode == 5 ? "the DNS server refused to answer" : NULL;
		if (why) ak_dns_why(r, "%s", why);
		else ak_dns_why(r, "the DNS server answered with error %d", rcode);
		return true;
	}
	uint32_t before = r->fields.len;
	for (unsigned i = 0; i < an; i++) {
		if (!read_name(msg, n, &p, got, sizeof got) || p + 10 > n) goto bad;
		unsigned t = u16(msg + p), cls = u16(msg + p + 2);
		size_t len = u16(msg + p + 8);
		p += 10;
		if (p + len > n) goto bad;
		if ((int)t == type && cls == 1 && !ak_dns_add_rdata(r, type, msg, n, p, len)) goto bad;
		p += len;
	}
	if (r->fields.len == before) not_found(r, type);
	return true;
bad:
	while (r->fields.len > before) ak_free(r->fields.data[--r->fields.len]);
	return false;
}

bool ak_dns_query(ak_buf *b, int id, const char *name, int type) {
	size_t start = b->len;
	ak_buf_u8(b, (uint8_t)(id >> 8));
	ak_buf_u8(b, (uint8_t)id);
	static const uint8_t head[10] = {0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 1};  // RD; one question, one additional (OPT)
	ak_buf_put(b, head, sizeof head);
	size_t n = strlen(name), total = 0;
	if (n && name[n - 1] == '.') n--;
	size_t i = 0;
	while (i < n) {
		size_t j = i;
		while (j < n && name[j] != '.') j++;
		size_t l = j - i;
		if (l == 0 || l > 63) {
			b->len = start;
			return false;
		}
		total += l + 1;
		ak_buf_u8(b, (uint8_t)l);
		ak_buf_put(b, name + i, l);
		i = j + 1;
	}
	if (total > 253) {
		b->len = start;
		return false;
	}
	ak_buf_u8(b, 0);
	static const uint8_t tail_opt[11] = {0, 0, 41, 0x04, 0xd0, 0, 0, 0, 0, 0, 0};  // the root, OPT, 1232 bytes, no flags, no data
	ak_buf_u8(b, (uint8_t)(type >> 8));
	ak_buf_u8(b, (uint8_t)type);
	ak_buf_u8(b, 0);
	ak_buf_u8(b, 1);  // IN
	ak_buf_put(b, tail_opt, sizeof tail_opt);
	return true;
}

// ---- resolv.conf ----

static bool word(const char **p, const char *end, const char **w, size_t *wn) {
	const char *s = *p;
	while (s < end && (*s == ' ' || *s == '\t' || *s == '\r')) s++;
	const char *e = s;
	while (e < end && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n') e++;
	*p = e;
	*w = s;
	*wn = (size_t)(e - s);
	return e > s;
}

static bool is(const char *w, size_t n, const char *s) { return strlen(s) == n && memcmp(w, s, n) == 0; }

void ak_resolv_conf_parse(const char *text, size_t n, ak_resolv_conf *c) {
	*c = (ak_resolv_conf){.ndots = 1, .timeout_ms = 5000, .attempts = 2};
	const char *p = text, *end = text + n;
	while (p < end) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		if (!eol) eol = end;
		const char *w;
		size_t wn;
		const char *q = p;
		if (word(&q, eol, &w, &wn) && w[0] != '#' && w[0] != ';') {
			if (is(w, wn, "nameserver")) {
				if (word(&q, eol, &w, &wn) && c->nservers < 3 && wn < sizeof c->servers[0]) {
					uint8_t ip[16];
					memcpy(c->servers[c->nservers], w, wn);
					c->servers[c->nservers][wn] = 0;
					// A zone (fe80::1%eth0) is the address's too.
					const char *pct = memchr(w, '%', wn);
					if (ak_ip_parse(w, pct ? (size_t)(pct - w) : wn, ip)) c->nservers++;
				}
			} else if (is(w, wn, "search") || is(w, wn, "domain")) {
				c->nsearch = 0;
				while (word(&q, eol, &w, &wn)) {
					if (c->nsearch < 6 && wn < sizeof c->search[0] && !(wn == 1 && w[0] == '.')) {
						memcpy(c->search[c->nsearch], w, wn);
						c->search[c->nsearch][wn] = 0;
						c->nsearch++;
					}
				}
			} else if (is(w, wn, "options")) {
				while (word(&q, eol, &w, &wn)) {
					int v;
					char opt[32];
					if (wn >= sizeof opt) continue;
					memcpy(opt, w, wn);
					opt[wn] = 0;
					if (sscanf(opt, "ndots:%d", &v) == 1) c->ndots = v < 0 ? 0 : v > 15 ? 15 : v;
					else if (sscanf(opt, "timeout:%d", &v) == 1) c->timeout_ms = (v < 1 ? 1 : v > 30 ? 30 : v) * 1000;
					else if (sscanf(opt, "attempts:%d", &v) == 1) c->attempts = v < 1 ? 1 : v > 5 ? 5 : v;
				}
			}
		}
		p = eol + 1;
	}
	if (c->nservers == 0) {
		snprintf(c->servers[0], sizeof c->servers[0], "127.0.0.1");
		c->nservers = 1;
	}
}

// ---- The stub resolver ----

#if !defined(__wasm__)

static int64_t ms_left(int64_t until) {
	int64_t left = (until - ak_now_ns()) / 1000000;
	return left < 0 ? 0 : left;
}

// server_addr fills a with server's address (an IP, maybe with a zone)
// and port.
static bool server_addr(const char *server, int port, struct sockaddr_storage *a, socklen_t *al) {
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
	hints.ai_socktype = SOCK_DGRAM;
	char ps[16];
	snprintf(ps, sizeof ps, "%d", port);
	if (getaddrinfo(server, ps, &hints, &res) != 0 || !res) return false;
	memcpy(a, res->ai_addr, res->ai_addrlen);
	*al = (socklen_t)res->ai_addrlen;
	freeaddrinfo(res);
	return true;
}

// wait_fd waits until s is readable (or writable) or until passes: false
// when it's passed.
static bool wait_fd(ak_sock s, bool out, int64_t until) {
	for (;;) {
		ak_pollfd p = {.fd = s, .events = out ? POLLOUT : POLLIN};
		int r = ak_sock_poll(&p, 1, (int)ms_left(until));
		if (r > 0) return true;
		if (r == 0) return false;
		if (!AK_SOCK_INTR(ak_sock_err())) return false;
	}
}

static bool read_full(ak_sock s, uint8_t *p, size_t n, int64_t until) {
	size_t got = 0;
	while (got < n) {
		if (!wait_fd(s, false, until)) return false;
		int r = (int)recv(s, (char *)p + got, (int)(n - got), 0);
		if (r <= 0) {
			if (r < 0 && (AK_SOCK_AGAIN(ak_sock_err()) || AK_SOCK_INTR(ak_sock_err()))) continue;
			return false;
		}
		got += (size_t)r;
	}
	return true;
}

// over_tcp asks the server over TCP (an answer UDP truncated): true when
// it answered.
static bool over_tcp(const struct sockaddr_storage *a, socklen_t al, const ak_buf *q, int id, const char *name, int type, int64_t until, ak_dns_result *r) {
	ak_sock s = socket(a->ss_family, SOCK_STREAM, 0);
	if (s == AK_BAD_SOCK) return false;
	ak_sock_no_inherit(s);
	ak_sock_nonblocking(s);
	ak_sock_nosigpipe(s);
	bool ok = false;
	uint8_t *msg = NULL;
	if (connect(s, (const struct sockaddr *)a, al) != 0) {
		int e = ak_sock_err();
		if (!AK_SOCK_INPROGRESS(e) && !AK_SOCK_AGAIN(e)) goto out;
		if (!wait_fd(s, true, until)) goto out;
		int err = 0;
		socklen_t el = sizeof err;
		if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el) != 0 || err) goto out;
	}
	uint8_t len2[2] = {(uint8_t)(q->len >> 8), (uint8_t)q->len};
	ak_buf out = {0};
	ak_buf_put(&out, len2, 2);
	ak_buf_put(&out, q->data, q->len);
	size_t off = 0;
	while (off < out.len) {
		if (!wait_fd(s, true, until)) break;
		int w = (int)send(s, (const char *)out.data + off, (int)(out.len - off), AK_SEND_FLAGS);
		if (w < 0) {
			if (AK_SOCK_AGAIN(ak_sock_err()) || AK_SOCK_INTR(ak_sock_err())) continue;
			break;
		}
		off += (size_t)w;
	}
	bool sent = off == out.len;
	ak_buf_free(&out);
	if (!sent || !read_full(s, len2, 2, until)) goto out;
	size_t n = (size_t)len2[0] << 8 | len2[1];
	msg = ak_alloc(n ? n : 1);
	if (!read_full(s, msg, n, until)) goto out;
	bool trunc;
	ok = ak_dns_parse(msg, n, id, name, type, r, &trunc) && !trunc;
out:
	ak_free(msg);
	ak_sock_close(s);
	return ok;
}

// ask asks one server once: true when it answered (r says what).
static bool ask(const char *server, int port, const char *name, int type, int timeout_ms, ak_dns_result *r) {
	struct sockaddr_storage a;
	socklen_t al;
	if (!server_addr(server, port, &a, &al)) return false;
	uint8_t rid[2] = {0, 0};
	ak_os_random(rid, 2);
	int id = rid[0] << 8 | rid[1];
	ak_buf q = {0};
	if (!ak_dns_query(&q, id, name, type)) {
		r->err = AK_NE_INVALID;
		ak_dns_why(r, "%s isn't a name DNS can look up", name);
		ak_buf_free(&q);
		return true;
	}
	int64_t until = ak_now_ns() + (int64_t)timeout_ms * 1000000;
	bool answered = false;
	ak_sock s = socket(a.ss_family, SOCK_DGRAM, 0);
	if (s == AK_BAD_SOCK) goto out;
	ak_sock_no_inherit(s);
	ak_sock_nonblocking(s);
	// Connected: only the server's answers come, and a server that isn't
	// there says so (a refusal) rather than waiting out the time.
	if (connect(s, (struct sockaddr *)&a, al) != 0 || send(s, (const char *)q.data, (int)q.len, 0) != (int)q.len) {
		ak_sock_close(s);
		goto out;
	}
	uint8_t buf[4096];
	while (wait_fd(s, false, until)) {
		int got = (int)recv(s, (char *)buf, sizeof buf, 0);
		if (got < 0) {
			int e = ak_sock_err();
			if (AK_SOCK_AGAIN(e) || AK_SOCK_INTR(e)) continue;
			break;  // refused: nothing there
		}
		bool trunc;
		if (!ak_dns_parse(buf, (size_t)got, id, name, type, r, &trunc)) continue;  // not the answer: another wait
		answered = trunc ? over_tcp(&a, al, &q, id, name, type, until, r) : true;
		break;
	}
	ak_sock_close(s);
out:
	ak_buf_free(&q);
	return answered;
}

// ask_all asks c's servers, attempts times each, until one answers
// other than with a failure of its own (SERVFAIL: the next one may
// answer).
static void ask_all(const ak_resolv_conf *c, int port, const char *name, int type, ak_dns_result *r) {
	bool any = false;
	for (int t = 0; t < c->attempts; t++) {
		for (int i = 0; i < c->nservers; i++) {
			ak_dns_result one = {0};
			if (!ask(c->servers[i], port, name, type, c->timeout_ms, &one)) continue;
			any = true;
			ak_dns_result_free(r);
			*r = one;
			if (one.err != AK_NE_LOOKUP) return;
		}
	}
	if (!any) {
		r->err = AK_NE_LOOKUP;
		ak_dns_why(r, "no DNS server answered");
	}
}

void ak_dns_stub(const ak_resolv_conf *c, int port, const char *name, int type, ak_dns_result *r) {
	*r = (ak_dns_result){0};
	size_t n = strlen(name);
	if (n == 0) {
		r->err = AK_NE_INVALID;
		ak_dns_why(r, "a name to look up is empty");
		return;
	}
	// The names to ask for: as given when it's rooted (a trailing dot) or
	// has ndots dots, then with each search domain; else those first.
	int dots = 0;
	for (size_t i = 0; i < n; i++) dots += name[i] == '.';
	bool rooted = name[n - 1] == '.';
	char cands[8][512];
	int nc = 0;
	if (rooted || dots >= c->ndots) snprintf(cands[nc++], sizeof cands[0], "%s", name);
	if (!rooted) {
		for (int i = 0; i < c->nsearch && nc < 7; i++) {
			if (n + strlen(c->search[i]) + 2 > sizeof cands[0]) continue;
			snprintf(cands[nc++], sizeof cands[0], "%s.%s", name, c->search[i]);
		}
		if (dots < c->ndots) snprintf(cands[nc++], sizeof cands[0], "%s", name);
	}
	for (int i = 0; i < nc; i++) {
		ak_dns_result one = {0};
		ask_all(c, port, cands[i], type, &one);
		ak_dns_result_free(r);
		*r = one;
		if (one.err != AK_NE_NOTFOUND) return;
	}
}

#else

void ak_dns_stub(const ak_resolv_conf *c, int port, const char *name, int type, ak_dns_result *r) {
	AK_UNUSED(c), AK_UNUSED(port), AK_UNUSED(name), AK_UNUSED(type);
	*r = (ak_dns_result){.err = AK_NE_UNSUPPORTED};
}

#endif

// ---- The system's resolver ----

#if defined(__APPLE__)

typedef struct {
	ak_dns_result *r;
	int type;
	bool done;
	int got;
} dnssd_ctx;

static void dnssd_reply(DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifindex, DNSServiceErrorType err, const char *fullname, uint16_t rrtype, uint16_t rrclass, uint16_t rdlen, const void *rdata, uint32_t ttl, void *p) {
	AK_UNUSED(ref), AK_UNUSED(ifindex), AK_UNUSED(fullname), AK_UNUSED(ttl);
	dnssd_ctx *c = p;
	if (err == kDNSServiceErr_NoSuchRecord || err == kDNSServiceErr_NoSuchName) {
		if (!(flags & kDNSServiceFlagsMoreComing) && c->got == 0) {
			c->done = true;
			if (err == kDNSServiceErr_NoSuchName) {
				c->r->err = AK_NE_NOTFOUND;
				ak_dns_why(c->r, "no such host");
			} else {
				not_found(c->r, c->type);
			}
		}
		return;
	}
	if (err != kDNSServiceErr_NoError) {
		c->done = true;
		c->r->err = AK_NE_LOOKUP;
		ak_dns_why(c->r, "DNSService error %d", (int)err);
		return;
	}
	// Its data, the names in it written out whole.
	if (rrtype == c->type && rrclass == kDNSServiceClass_IN && (flags & kDNSServiceFlagsAdd)) {
		if (ak_dns_add_rdata(c->r, c->type, rdata, rdlen, 0, rdlen)) c->got++;
	}
	if (!(flags & kDNSServiceFlagsMoreComing) && c->got > 0) c->done = true;
}

void ak_dns_lookup(const char *name, int type, ak_dns_result *r) {
	*r = (ak_dns_result){0};
	dnssd_ctx c = {.r = r, .type = type};
	DNSServiceRef ref = NULL;
	DNSServiceErrorType e = DNSServiceQueryRecord(&ref, kDNSServiceFlagsReturnIntermediates | kDNSServiceFlagsTimeout, 0, name, (uint16_t)type, kDNSServiceClass_IN, dnssd_reply, &c);
	if (e != kDNSServiceErr_NoError) {
		r->err = AK_NE_LOOKUP;
		ak_dns_why(r, "DNSService error %d", (int)e);
		return;
	}
	int fd = DNSServiceRefSockFD(ref);
	int64_t until = ak_now_ns() + INT64_C(10000000000);
	while (!c.done) {
		int64_t left = (until - ak_now_ns()) / 1000000;
		if (left <= 0) {
			ak_dns_result_free(r);
			*r = (ak_dns_result){.err = AK_NE_TIMEDOUT};
			break;
		}
		struct pollfd p = {.fd = fd, .events = POLLIN};
		int pr = poll(&p, 1, (int)left);
		if (pr < 0 && errno == EINTR) continue;
		if (pr <= 0) continue;
		e = DNSServiceProcessResult(ref);
		if (e == kDNSServiceErr_Timeout) {
			if (c.got == 0) not_found(r, type);
			break;
		}
		if (e != kDNSServiceErr_NoError) {
			r->err = AK_NE_LOOKUP;
			ak_dns_why(r, "DNSService error %d", (int)e);
			break;
		}
	}
	DNSServiceRefDeallocate(ref);
}

#elif defined(__ANDROID__)

typedef int (*nquery_fn)(uint64_t net, const char *dname, int ns_class, int ns_type, uint32_t flags);
typedef int (*nresult_fn)(int fd, int *rcode, uint8_t *answer, size_t anslen);
typedef void (*cancel_fn)(int fd);

void ak_dns_lookup(const char *name, int type, ak_dns_result *r) {
	*r = (ak_dns_result){0};
	static nquery_fn nquery;
	static nresult_fn nresult;
	static cancel_fn ncancel;
	static ak_mutex mu = AK_MUTEX_INIT;
	ak_mutex_lock(&mu);
	if (!nquery) {
		void *lib = dlopen("libandroid.so", RTLD_NOW);
		if (lib) {
			nresult = (nresult_fn)dlsym(lib, "android_res_nresult");
			ncancel = (cancel_fn)dlsym(lib, "android_res_cancel");
			nquery = (nquery_fn)dlsym(lib, "android_res_nquery");
		}
	}
	ak_mutex_unlock(&mu);
	if (!nquery || !nresult || !ncancel) {
		r->err = AK_NE_UNSUPPORTED;
		ak_dns_why(r, "this Android has no android_res_nquery");
		return;
	}
	int fd = nquery(0, name, 1, type, 0);  // NETWORK_UNSPECIFIED: the default network's resolver
	if (fd < 0) {
		r->err = AK_NE_LOOKUP;
		ak_dns_why(r, "%s", strerror(-fd));
		return;
	}
	struct pollfd p = {.fd = fd, .events = POLLIN};
	int pr;
	do {
		pr = poll(&p, 1, 10000);
	} while (pr < 0 && errno == EINTR);
	if (pr <= 0) {
		ncancel(fd);
		r->err = AK_NE_TIMEDOUT;
		return;
	}
	uint8_t *ans = ak_alloc(65536);
	int rcode = 0;
	int n = nresult(fd, &rcode, ans, 65536);
	if (n < 0) {
		r->err = AK_NE_LOOKUP;
		ak_dns_why(r, "%s", strerror(-n));
	} else {
		bool trunc;
		if (!ak_dns_parse(ans, (size_t)n, -1, name, type, r, &trunc)) {
			r->err = AK_NE_LOOKUP;
			ak_dns_why(r, "a malformed answer");
		}
	}
	ak_free(ans);
}

#elif defined(_WIN32)

typedef DNS_STATUS(WINAPI *query_fn)(PCWSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORDW *results, PVOID *reserved);
typedef VOID(WINAPI *free_fn)(PVOID data, DNS_FREE_TYPE type);

static void add_wide(ak_dns_result *r, const WCHAR *w) {
	if (!w) {
		add_field(r, "", 0);
		return;
	}
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	char *s = ak_alloc(n > 0 ? (size_t)n : 1);
	s[0] = 0;
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
	size_t l = strlen(s);
	if (l && s[l - 1] == '.') s[--l] = 0;
	ak_vec_push(&r->fields, s);
}

void ak_dns_lookup(const char *name, int type, ak_dns_result *r) {
	*r = (ak_dns_result){0};
	static query_fn query;
	static free_fn dfree;
	static ak_mutex mu = AK_MUTEX_INIT;
	ak_mutex_lock(&mu);
	if (!query) {
		HMODULE m = LoadLibraryExW(L"dnsapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (m) {
			dfree = (free_fn)(void *)GetProcAddress(m, "DnsFree");
			query = (query_fn)(void *)GetProcAddress(m, "DnsQuery_W");
		}
	}
	ak_mutex_unlock(&mu);
	if (!query || !dfree) {
		r->err = AK_NE_UNSUPPORTED;
		ak_dns_why(r, "no dnsapi.dll");
		return;
	}
	int wn = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
	WCHAR *w = ak_alloc(sizeof(WCHAR) * (size_t)(wn > 0 ? wn : 1));
	w[0] = 0;
	if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, name, -1, w, wn);
	PDNS_RECORDW rec = NULL;
	DNS_STATUS st = query(w, (WORD)type, DNS_QUERY_STANDARD, NULL, &rec, NULL);
	ak_free(w);
	if (st == DNS_ERROR_RCODE_NAME_ERROR) {
		r->err = AK_NE_NOTFOUND;
		ak_dns_why(r, "no such host");
	} else if (st == DNS_INFO_NO_RECORDS) {
		not_found(r, type);
	} else if (st == ERROR_TIMEOUT) {
		r->err = AK_NE_TIMEDOUT;
	} else if (st != 0) {
		r->err = AK_NE_LOOKUP;
		ak_dns_why(r, "DnsQuery error %ld", (long)st);
	} else {
		for (PDNS_RECORDW p = rec; p; p = p->pNext) {
			if (p->wType != type || p->Flags.S.Section != DnsSectionAnswer) continue;
			switch (type) {
			case AK_DNS_MX:
				add_int(r, p->Data.MX.wPreference);
				add_wide(r, p->Data.MX.pNameExchange);
				break;
			case AK_DNS_SRV:
				add_int(r, p->Data.SRV.wPriority);
				add_int(r, p->Data.SRV.wWeight);
				add_int(r, p->Data.SRV.wPort);
				add_wide(r, p->Data.SRV.pNameTarget);
				break;
			case AK_DNS_TXT: {
				ak_buf b = {0};
				for (DWORD i = 0; i < p->Data.TXT.dwStringCount; i++) {
					const WCHAR *s = p->Data.TXT.pStringArray[i];
					int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
					if (n <= 1) continue;
					ak_buf_reserve(&b, (size_t)n);
					WideCharToMultiByte(CP_UTF8, 0, s, -1, (char *)b.data + b.len, n, NULL, NULL);
					b.len += (size_t)n - 1;
				}
				add_field(r, b.len ? (const char *)b.data : "", b.len);
				ak_buf_free(&b);
				break;
			}
			default:
				add_wide(r, p->Data.PTR.pNameHost);
			}
		}
		if (r->fields.len == 0) not_found(r, type);
	}
	if (rec) dfree(rec, DnsFreeRecordList);
}

#elif !defined(__wasm__)

// Linux (and another Unix): the servers resolv.conf names.
void ak_dns_lookup(const char *name, int type, ak_dns_result *r) {
	ak_resolv_conf c;
	FILE *f = fopen("/etc/resolv.conf", "rb");
	char buf[8192];
	size_t n = 0;
	if (f) {
		n = fread(buf, 1, sizeof buf, f);
		fclose(f);
	}
	ak_resolv_conf_parse(buf, n, &c);
	ak_dns_stub(&c, 53, name, type, r);
}

#else

void ak_dns_lookup(const char *name, int type, ak_dns_result *r) {
	AK_UNUSED(name), AK_UNUSED(type);
	*r = (ak_dns_result){.err = AK_NE_UNSUPPORTED};
}

#endif
