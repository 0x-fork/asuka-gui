// net.h: std/net's C (net.c, dns.c): what the two files share, and what
// the core's tests reach (core/test/net_test.c): IP addresses as text,
// DNS messages, the resolver Linux uses.
#ifndef AK_NET_H
#define AK_NET_H

#include "base/base.h"

// ---- IP addresses ----

// ak_ip_parse reads an IP address written as text, without a zone:
// IPv4's four decimal parts ("192.0.2.1", no leading zeros), IPv6's
// groups (RFC 4291: "2001:db8::1", "::ffff:192.0.2.1"). It writes its
// bytes to out and returns how many: 4, 16, or 0 for text that isn't an
// address.
int ak_ip_parse(const char *s, size_t n, uint8_t out[16]);
// ak_ip_format writes an address of 4 or 16 bytes as text: IPv6 as RFC
// 5952 says (lower case, the longest run of zero groups as ::, an IPv4
// address mapped into IPv6 as ::ffff:192.0.2.1). out has room for 46.
void ak_ip_format(const uint8_t *b, int len, char out[46]);

// ---- Errors ----

// What went wrong, as std/net's NetError says it: the builtins reject
// with "std/net:<case>:<detail>", which the package's Asuka reads back
// as its enum.
typedef enum {
	AK_NE_OK,
	AK_NE_CANCELLED,  // not an error: the task's cancellation
	AK_NE_REFUSED,
	AK_NE_RESET,
	AK_NE_TIMEDOUT,
	AK_NE_UNREACHABLE,
	AK_NE_NOTFOUND,
	AK_NE_LOOKUP,
	AK_NE_CLOSED,
	AK_NE_ENDED,
	AK_NE_INUSE,
	AK_NE_UNAVAILABLE,
	AK_NE_DENIED,
	AK_NE_TOOLARGE,
	AK_NE_INVALID,
	AK_NE_UNSUPPORTED,
	AK_NE_FAILED,
	AK_NE_TLS,        // TLS failed: the handshake, a record (tls.c)
	AK_NE_UNTRUSTED,  // a certificate isn't trusted: "<problem>:<why>"
} ak_net_err;

// ak_net_err_name is the NetError case's name of e ("refused").
const char *ak_net_err_name(ak_net_err e);

// ---- DNS ----

// The record types std/net looks up.
enum { AK_DNS_A = 1, AK_DNS_NS = 2, AK_DNS_CNAME = 5, AK_DNS_PTR = 12, AK_DNS_MX = 15, AK_DNS_TXT = 16, AK_DNS_AAAA = 28, AK_DNS_SRV = 33 };

// A lookup's answer: its records as strings, each record's fields in a
// row (MX: preference, host; SRV: priority, weight, port, target; TXT:
// its text, its strings joined; NS, CNAME, PTR: the name), names without
// their trailing dot; or the error and why.
typedef struct {
	AK_VEC(char *) fields;
	ak_net_err err;
	char why[160];
} ak_dns_result;

void ak_dns_result_free(ak_dns_result *r);
// ak_dns_why sets r's why (cut short at its size).
void ak_dns_why(ak_dns_result *r, const char *fmt, ...) AK_PRINTF(2, 3);
// ak_dns_add_rdata adds a record of type's data (rd[0:len], at offset
// off in msg[0:n], whose names may point back into msg) to r: false for
// data that's malformed.
bool ak_dns_add_rdata(ak_dns_result *r, int type, const uint8_t *msg, size_t n, size_t off, size_t len);
// ak_dns_parse reads msg[0:n], the answer to a query of id for name's
// records of type: their fields into r, or r->err (NOTFOUND: no such
// name, or no records of type; LOOKUP: the server failed). *truncated
// says it was cut short (TC: asked again over TCP). false: it isn't that
// answer (another id or question, malformed).
bool ak_dns_parse(const uint8_t *msg, size_t n, int id, const char *name, int type, ak_dns_result *r, bool *truncated);
// ak_dns_query writes a query (id, recursion desired, EDNS0's 1232-byte
// payload) for name's records of type into b; false for a name that
// can't be one (a label past 63 bytes, the whole past 253).
bool ak_dns_query(ak_buf *b, int id, const char *name, int type);

// The resolver's configuration, as /etc/resolv.conf says it: its name
// servers (IP addresses as text; port 53), search domains, ndots,
// timeout (ms) and attempts.
typedef struct {
	char servers[3][64];
	int nservers;
	char search[6][256];
	int nsearch;
	int ndots, timeout_ms, attempts;
} ak_resolv_conf;

// ak_resolv_conf_parse reads a resolv.conf's text into c (with
// glibc's defaults: 127.0.0.1 when it names no server, ndots 1, 5s, 2
// attempts).
void ak_resolv_conf_parse(const char *text, size_t n, ak_resolv_conf *c);
// ak_dns_stub looks up name's records of type as Linux's own resolver
// does (Go's too): UDP to c's servers in turn, TCP when an answer is
// truncated, the search domains for a name with fewer than ndots dots.
// Blocking: a lookup's thread calls it. port is the servers' (53; a
// test's server elsewhere).
void ak_dns_stub(const ak_resolv_conf *c, int port, const char *name, int type, ak_dns_result *r);
// ak_dns_lookup looks up name's records of type with the system's
// resolver: Apple's DNSService, Android's android_res_nquery, Windows'
// DnsQuery_W, the stub resolver over /etc/resolv.conf on Linux.
// Blocking: a lookup's thread calls it.
void ak_dns_lookup(const char *name, int type, ak_dns_result *r);

#endif
