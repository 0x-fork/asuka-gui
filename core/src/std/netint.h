// netint.h: net.c's internals that its TLS glue (nettls.c) shares: an
// operation, a socket as the net thread holds it, and the few functions
// both call. Not the web's.
#ifndef AK_NETINT_H
#define AK_NETINT_H

#include "std/sock.h"
#include "std/tls.h"
#include "std/net.h"
#include "rt/sched.h"
#include "signal/signal.h"

#define READ_CHUNK 65536

enum { K_CONN = 1, K_LISTEN, K_UDP };
enum { OP_CONNECT, OP_LISTEN, OP_UDP, OP_ACCEPT, OP_READ, OP_WRITE, OP_SEND, OP_RECV, OP_TLS };
enum { T_NONE, T_HS, T_OPEN, T_FAILED };  // a connection's TLS
enum { RD_SOME, RD_EXACT, RD_UNTIL, RD_ALL };
enum { OPT_NODELAY = 1, OPT_KEEPALIVE, OPT_BROADCAST };

typedef struct srv srv;

typedef struct {
	ak_sock fd;
	uint32_t idx;  // its address's
} attempt;

// An operation, made on the main thread, done on the net thread, posted
// back. The fields after "Main's" are the main thread's alone.
typedef struct op {
	struct op *next;  // in a socket's queue, or the net thread's waiting list
	uint64_t id;
	uint8_t kind;
	int64_t handle;    // the socket's (an accept's: its listener's)
	int64_t deadline;  // ak_now_ns() when it gives up; 0: never
	// What it's asked.
	char *host;  // connect, listen, udp: as given ("" none); TLS's: the server name
	int port;
	int64_t config;  // a listen's TLS configuration (0 none); a TLS start's (0: wait for its handshake)
	bool tls_server;
	bool nodelay, reuse;
	int64_t keepalive_ns;
	uint8_t mode;  // a read's
	size_t n;      // a read's: exactly n, or at most n
	uint8_t *data;  // a write's, a send's: what goes; a read's: its delimiter
	size_t len, off;
	struct sockaddr_storage to;  // a send's (tolen 0: the connected peer)
	socklen_t tolen;
	// A connect's, a listen's, a UDP socket's addresses, and a connect's
	// attempts.
	bool resolving;
	struct sockaddr_storage *addrs;
	socklen_t *lens;
	uint32_t naddrs, next_addr;
	AK_VEC(attempt) tries;
	int64_t next_at;
	int first_err;
	// The answer.
	ak_net_err err;
	char *detail;
	int64_t got;  // the socket it made (connect, listen, udp, accept)
	ak_sock got_fd;
	int got_family;
	ak_buf out;  // what a read, a receive got
	bool none;   // a read: the connection's end, nothing more
	char from[64];
	int from_port;
	// Main's.
	ak_future *fut;
	ak_owner *owner;
	bool cancelled;
	bool queued;  // in its socket's rq: not sent yet
	srv *server;  // a server's own accept (no future)
} op;

typedef struct {
	op *head, *tail;
} opq;

static inline void opq_push(opq *q, op *o) {
	o->next = NULL;
	if (q->tail) q->tail->next = o;
	else q->head = o;
	q->tail = o;
}

static inline op *opq_pop(opq *q) {
	op *o = q->head;
	if (!o) return NULL;
	q->head = o->next;
	if (!q->head) q->tail = NULL;
	o->next = NULL;
	return o;
}

static inline bool opq_remove(opq *q, op *o) {
	op *prev = NULL;
	for (op *p = q->head; p; prev = p, p = p->next) {
		if (p != o) continue;
		if (prev) prev->next = p->next;
		else q->head = p->next;
		if (q->tail == p) q->tail = prev;
		p->next = NULL;
		return true;
	}
	return false;
}

static inline op *opq_find(opq *q, uint64_t id) {
	for (op *p = q->head; p; p = p->next)
		if (p->id == id) return p;
	return NULL;
}

// A socket, as the net thread holds it.
typedef struct {
	int64_t handle;
	ak_sock fd;
	uint8_t kind;
	uint32_t slot;
	ak_buf in;    // a connection's: read, not yet taken
	size_t scan;  // where a read up to a delimiter looks on from
	bool eof;     // the other side's end came
	ak_net_err rerr;  // a read's failure, which the reads after it get too
	char *rdetail;
	bool shut_wanted, shut;
	opq reads;   // a connection's reads (and TLS starts), a listener's accepts, a UDP socket's receives
	opq writes;  // a connection's writes, a UDP socket's sends
	int64_t pause_until;  // a listener's: accepting again after running out of descriptors
	// TLS: a connection's session, its handshake's phase and what it last
	// wanted, its failure; a listener's configuration.
	ak_tls *tls;
	uint8_t tphase;
	int twant;
	bool tls_ww;  // a read wants the socket writable
	ak_net_err terr;
	char *tdetail;
	int64_t tls_conf;
} lsock;

// set_err sets o's error: e, and its detail (fmt NULL: none).
void ak_net_set_err(op *o, ak_net_err e, const char *fmt, ...) AK_PRINTF(3, 4);
// post_op hands o back to the main thread.
void ak_net_post_op(op *o);
char *ak_net_dup(const char *s);
// ak_net_tls_notify is told a certificate's check on another thread is
// done for the socket of handle ctx (the scheduler's hold handed over).
void ak_net_tls_notify(void *ctx);

// TLS on the net thread's sockets: nettls.c's, which tls.c sets as a
// configuration is first made, so an app without TLS links none of it.
typedef struct ak_net_tls_hooks {
	// gate answers a TLS start o (1: answered, -1: not yet), or says
	// whether read o of TLS connection s waits for its handshake (-1),
	// fails with it (1, answered), or reads (0).
	int (*gate)(lsock *s, op *o);
	void (*drive)(lsock *s);                  // the handshake, on
	void (*fill)(lsock *s, size_t want);      // conn_fill's
	void (*writes)(lsock *s);                 // conn_writes's
	void (*accepted)(lsock *l, lsock *n);     // a TLS listener's new connection
	void (*close)(lsock *s);                  // before its socket closes
	// events are what s polls for; *working when it waits on this side,
	// *ready when a read gets plaintext without the socket.
	short (*events)(lsock *s, bool *working, bool *ready);
	void (*ready)(lsock *s);  // its socket's ready, some way: before a read
	ak_arr *(*answer)(op *o);  // a TLS start's answer, on the main thread
} ak_net_tls_hooks;

extern const ak_net_tls_hooks *ak_net_tls;
extern const ak_net_tls_hooks ak_net_tls_impl;

#endif
