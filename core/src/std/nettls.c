// nettls.c: TLS on std/net's sockets, on the net thread (net.c's
// netint.h): a connection's session (tls.h's layer) between its socket
// and its buffers. net.c calls it through ak_net_tls, which tls.c sets
// as a configuration is first made: an app without TLS links none of
// this, nor tls.c, nor Mbed TLS.
//
// A connection's TLS starts as a TLS start (OP_TLS) reaches the head of
// its reads: a client's (connectTLS, startTLS) or a server's
// (startServerTLS), what was read before it (a STARTTLS's) handed to the
// handshake; or as a TLS listener accepts it, its handshake going once
// a read or a write waits on it. A read or a write waits for the
// handshake, and fails with it.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif

#if !defined(__wasm__)

#include "std/netint.h"

#include <stdio.h>
#include <string.h>

#define set_err ak_net_set_err
#define post_op ak_net_post_op

static void fail_with(lsock *s) {
	char d[512];
	s->tphase = T_FAILED;
	s->terr = ak_tls_layer->error(s->tls, d, sizeof d);
	s->tdetail = ak_net_dup(d);
}

// drive moves s's handshake on.
static void drive(lsock *s) {
	if (!s->tls || s->tphase != T_HS) return;
	int r = ak_tls_layer->handshake(s->tls);
	s->twant = r;
	if (r == AK_TLS_DONE) s->tphase = T_OPEN;
	else if (r == AK_TLS_FAIL) fail_with(s);
}

static void put_field(ak_buf *b, const void *p, size_t n) {
	uint8_t len[4] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
	ak_buf_put(b, len, 4);
	ak_buf_put(b, p, n);
}

// state writes what s's handshake agreed into o's answer: fields of a
// 4-byte length each, which answer makes an array of bytes.
static void state(lsock *s, op *o) {
	ak_tls_info in;
	const uint8_t *const *certs;
	const size_t *lens;
	uint32_t n;
	ak_tls_layer->info(s->tls, &in, &certs, &lens, &n);
	char v[8];
	snprintf(v, sizeof v, "%d", in.version);
	put_field(&o->out, v, strlen(v));
	put_field(&o->out, in.cipher, strlen(in.cipher));
	put_field(&o->out, in.protocol, strlen(in.protocol));
	put_field(&o->out, in.server_name, strlen(in.server_name));
	put_field(&o->out, in.resumed ? "1" : "0", 1);
	for (uint32_t i = 0; i < n; i++) put_field(&o->out, certs[i], lens[i]);
}

// start answers TLS start o once it can: true once it has.
static bool start(lsock *s, op *o) {
	if (o->config) {
		if (s->tls) {
			set_err(o, AK_NE_INVALID, "the connection speaks TLS already");
			return true;
		}
		// What this side wrote before goes first, as it is.
		if (s->writes.head) return false;
		char err[300] = "";
		s->tls = ak_tls_layer->open(o->config, o->tls_server, o->host, s->fd, ak_net_tls_notify, (void *)(intptr_t)s->handle, err, sizeof err);
		if (!s->tls) {
			set_err(o, AK_NE_INVALID, "%s", err);
			return true;
		}
		// What came before TLS began (after a STARTTLS) is the handshake's.
		ak_tls_layer->preload(s->tls, s->in.data, s->in.len);
		s->in.len = 0;
		s->scan = 0;
		s->tphase = T_HS;
		o->config = 0;
		drive(s);
	}
	switch (s->tphase) {
	case T_NONE: set_err(o, AK_NE_INVALID, "the connection doesn't speak TLS"); return true;
	case T_HS: return false;
	case T_OPEN: state(s, o); return true;
	}
	set_err(o, s->terr, "%s", s->tdetail ? s->tdetail : "");
	return true;
}

static int gate(lsock *s, op *o) {
	if (o->kind == OP_TLS) return start(s, o) ? 1 : -1;
	if (s->tphase == T_OPEN) return 0;
	if (s->tphase != T_FAILED) return -1;  // the handshake first
	set_err(o, s->terr, "%s", s->tdetail ? s->tdetail : "");
	return 1;
}

// fill reads what s's session has for it.
static void fill(lsock *s, size_t want) {
	size_t got = 0;
	for (;;) {
		ak_buf_reserve(&s->in, want);
		int r = ak_tls_layer->read(s->tls, s->in.data + s->in.len, want);
		if (r > 0) {
			s->in.len += (size_t)r;
			got += (size_t)r;
			if (got >= want) return;
			continue;
		}
		if (r == AK_TLS_EOF) {
			s->eof = true;
		} else if (r == AK_TLS_WANT_WRITE) {
			s->tls_ww = true;
		} else if (r == AK_TLS_FAIL) {
			char d[512];
			s->rerr = ak_tls_layer->error(s->tls, d, sizeof d);
			s->rdetail = ak_net_dup(d);
		}
		return;
	}
}

// writes writes s's queue through its session.
static void writes(lsock *s) {
	op *o;
	if (s->tphase == T_HS) return;  // after the handshake
	while ((o = s->writes.head)) {
		if (s->tphase == T_FAILED) {
			set_err(o, s->terr, "%s", s->tdetail ? s->tdetail : "");
		} else {
			while (o->off < o->len) {
				int w = ak_tls_layer->write(s->tls, o->data + o->off, o->len - o->off);
				if (w > 0) {
					o->off += (size_t)w;
					continue;
				}
				if (w != AK_TLS_FAIL) return;
				char d[512];
				ak_net_err e = ak_tls_layer->error(s->tls, d, sizeof d);
				set_err(o, e, "%s", d);
				break;
			}
		}
		post_op(opq_pop(&s->writes));
	}
	if (s->shut_wanted && !s->shut) {
		if (s->tphase == T_OPEN) ak_tls_layer->shutdown(s->tls);
		shutdown(s->fd, AK_SHUT_WR);
		s->shut = true;
	}
}

// accepted starts a TLS listener's connection's session: its handshake
// goes as a read or a write waits on it.
static void accepted(lsock *l, lsock *n) {
	char err[300] = "";
	n->tls = ak_tls_layer->open(l->tls_conf, true, NULL, n->fd, NULL, NULL, err, sizeof err);
	n->tphase = n->tls ? T_HS : T_FAILED;
	n->twant = AK_TLS_WANT_READ;
	if (!n->tls) {
		n->terr = AK_NE_TLS;
		n->tdetail = ak_net_dup(err);
	}
}

static void close_tls(lsock *s) {
	if (!s->shut) ak_tls_layer->shutdown(s->tls);
	ak_tls_layer->close(s->tls);
	s->tls = NULL;
	ak_free(s->tdetail);
	s->tdetail = NULL;
	// net.c's lsock_close lingers on the socket after: what came and
	// wasn't read doesn't turn the close into a reset that loses the alert.
}

static bool wants(lsock *s) { return s->reads.head && s->reads.head->kind != OP_TLS && !s->eof && !s->rerr && s->tphase == T_OPEN; }

static short events(lsock *s, bool *working, bool *ready) {
	short ev = 0;
	if (s->tphase == T_HS) {
		// A handshake that something waits on: as it wants.
		if (s->reads.head || s->writes.head) {
			if (s->twant == AK_TLS_WANT_WRITE) ev |= POLLOUT, *working = true;
			else if (s->twant == AK_TLS_WANT_READ) ev |= POLLIN;
		}
		return ev;
	}
	if (wants(s)) {
		if (ak_tls_layer->pending(s->tls)) *ready = *working = true;
		else ev |= POLLIN;
	}
	if (s->writes.head || s->tls_ww) ev |= POLLOUT, *working = true;
	return ev;
}

// ready is s's socket ready, some way: the handshake goes on, and a
// read reads whatever the socket's state (the session says when it needs
// more).
static void ready(lsock *s) {
	s->tls_ww = false;
	if (s->tphase == T_HS) drive(s);
}

static ak_arr *answer(op *o) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 8);
	size_t at = 0;
	while (at + 4 <= o->out.len) {
		const uint8_t *p = o->out.data + at;
		size_t n = (size_t)p[0] << 24 | (size_t)p[1] << 16 | (size_t)p[2] << 8 | p[3];
		if (at + 4 + n > o->out.len) break;
		ak_arr_push(&a, (ak_val){.p = ak_bytes_new(p + 4, n)});
		at += 4 + n;
	}
	return a;
}

const ak_net_tls_hooks ak_net_tls_impl = {
	.gate = gate,
	.drive = drive,
	.fill = fill,
	.writes = writes,
	.accepted = accepted,
	.close = close_tls,
	.events = events,
	.ready = ready,
	.answer = answer,
};

#endif
