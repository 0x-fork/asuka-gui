// std/http on Windows and Linux: the shared HTTP client
// (asuka_fetch_stream: WinHTTP, libsoup 3), with the system's proxy
// settings and certificates, or a secured transport's TLS settings
// (asuka_tls), on a thread of the request's.
#include "http.asuka.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include "asuka_windows.h"
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define close_sock closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define close_sock close
#endif

// A transport: the system's TLS (tls NULL), or a secured one's settings.
typedef struct {
	http_Transport base;
	const asuka_tls *tls;
} transport;

static const asuka_tls *tls_of(http_Transport *self) { return ((transport *)self)->tls; }

typedef struct {
	const char *method, *url;
	http_map_string headers;
	asuka_bytes body;
	int64_t timeout;  // nanoseconds
	const asuka_tls *tls;
	asuka_call *call;
} request;

static bool keep(void *ctx, const void *p, size_t n, asuka_error *err) {
	(void)err;
	asuka_writer_put(ctx, p, n);
	return true;
}

static void run(void *p) {
	request *q = p;
	asuka_header *hs = calloc(q->headers.len ? q->headers.len : 1, sizeof *hs);
	for (size_t i = 0; i < q->headers.len; i++) hs[i] = (asuka_header){q->headers.keys[i], q->headers.values[i]};
	asuka_response r;
	asuka_error e = {0};
	asuka_writer w = {0};
	int ms = q->timeout > 0 ? (int)((q->timeout + 999999) / 1000000) : 0;
	asuka_request rq = {.method = q->method, .url = q->url, .headers = hs, .nheaders = q->headers.len, .body = q->body.data, .len = q->body.len,
	                    .timeout_ms = ms, .cancel = q->call, .tls = q->tls, .data = keep, .ctx = &w};
	if (asuka_fetch_stream(&rq, &r, &e)) {
		http_Response out = {.status = r.status, .url = r.url, .body = {w.data ? w.data : malloc(1), w.len}};
		out.headers = (http_map_string){r.names, r.values, r.nheaders};
		http_Transport_send_done(q->call, out);  // takes r's strings and w's bytes
	} else {
		asuka_writer_free(&w);
		asuka_call_fail(q->call, &e);
	}
	free(hs);
	free(q);
}

static void send_(http_Transport *self, const char *method, const char *url, http_map_string headers, asuka_bytes body, int64_t timeout, asuka_call *call) {
	request *q = malloc(sizeof *q);
	*q = (request){method, url, headers, body, timeout, tls_of(self), call};
	if (!asuka_desktop_spawn(run, q)) {
		free(q);
		asuka_error e = {0};
		asuka_error_failed(&e, "http", 0, "no thread for the request");
		asuka_call_fail(call, &e);
	}
}

// ---- Downloads: the body into a file as it comes ----

typedef struct {
	const char *method, *url, *path;
	http_map_string headers;
	asuka_bytes body;
	int64_t timeout;  // nanoseconds
	http_fn_int_int progress;
	const asuka_tls *tls;
	asuka_call *call;
	// What the sink keeps.
	FILE *f;
	char *part;  // path.download, renamed to path once it's all in
	int64_t got, total;
	double reported;  // seconds
} download;

static double seconds(void) {
	struct timespec t;
	timespec_get(&t, TIME_UTC);
	return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static int64_t content_length(const asuka_response *r) {
	for (size_t i = 0; i < r->nheaders; i++)
		if (strcmp(r->names[i], "content-length") == 0) return strtoll(r->values[i], NULL, 10);
	return -1;
}

static FILE *open_file(const char *path) {
#ifdef _WIN32
	wchar_t *w = asuka_wide(path);
	FILE *f = _wfopen(w, L"wb");
	free(w);
	return f;
#else
	return fopen(path, "wb");
#endif
}

static bool replace_file(const char *from, const char *to) {
#ifdef _WIN32
	wchar_t *wf = asuka_wide(from), *wt = asuka_wide(to);
	bool ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
	free(wf);
	free(wt);
	return ok;
#else
	return rename(from, to) == 0;
#endif
}

static void remove_file(const char *p) {
#ifdef _WIN32
	wchar_t *w = asuka_wide(p);
	DeleteFileW(w);
	free(w);
#else
	remove(p);
#endif
}

// head opens the file beside path once the response's head is in.
static bool head(void *ctx, const asuka_response *r, asuka_error *err) {
	download *d = ctx;
	d->total = content_length(r);
	d->f = open_file(d->part);
	if (!d->f) {
		asuka_error_failed(err, "http", 0, "can't write %s", d->path);
		return false;
	}
	return true;
}

// data writes a piece of the body, and reports the progress (ten times a
// second at most).
static bool data(void *ctx, const void *p, size_t n, asuka_error *err) {
	download *d = ctx;
	if (fwrite(p, 1, n, d->f) != n) {
		asuka_error_failed(err, "http", 0, "can't write %s", d->path);
		return false;
	}
	d->got += (int64_t)n;
	double now = seconds();
	if (now - d->reported > 0.1) {
		d->reported = now;
		http_fn_int_int_call(d->progress, d->got, d->total);
	}
	return true;
}

static void run_download(void *p) {
	download *q = p;
	q->part = asuka_sprintf("%s.download", q->path);
	asuka_header *hs = calloc(q->headers.len ? q->headers.len : 1, sizeof *hs);
	for (size_t i = 0; i < q->headers.len; i++) hs[i] = (asuka_header){q->headers.keys[i], q->headers.values[i]};
	asuka_response r;
	asuka_error e = {0};
	int ms = q->timeout > 0 ? (int)((q->timeout + 999999) / 1000000) : 0;
	asuka_request rq = {.method = q->method, .url = q->url, .headers = hs, .nheaders = q->headers.len, .body = q->body.data, .len = q->body.len,
	                    .timeout_ms = ms, .cancel = q->call, .tls = q->tls, .head = head, .data = data, .ctx = q};
	bool ok = asuka_fetch_stream(&rq, &r, &e);
	if (ok && !q->f) q->f = open_file(q->part);  // an empty body: an empty file
	if (q->f && fclose(q->f) != 0) ok = false;
	q->f = NULL;
	if (ok && !replace_file(q->part, q->path)) {
		asuka_error_failed(&e, "http", 0, "can't write %s", q->path);
		asuka_response_free(&r);
		ok = false;
	}
	if (ok) {
		http_fn_int_int_call(q->progress, q->got, q->total >= 0 ? q->total : q->got);
		http_Response out = {.status = r.status, .url = r.url, .body = r.body};
		out.headers = (http_map_string){r.names, r.values, r.nheaders};
		http_Transport_download_done(q->call, out);  // takes r's strings
	} else {
		remove_file(q->part);
		asuka_call_fail(q->call, &e);
	}
	free(q->part);
	free(hs);
	free(q);
}

static void download_(http_Transport *self, const char *method, const char *url, http_map_string headers, asuka_bytes body, int64_t timeout, const char *path,
                      http_fn_int_int progress, asuka_call *call) {
	download *q = calloc(1, sizeof *q);
	*q = (download){.method = method, .url = url, .path = path, .headers = headers, .body = body, .timeout = timeout, .progress = progress, .tls = tls_of(self), .call = call, .total = -1};
	if (!asuka_desktop_spawn(run_download, q)) {
		free(q);
		asuka_error e = {0};
		asuka_error_failed(&e, "http", 0, "no thread for the download");
		asuka_call_fail(call, &e);
	}
}

// ---- Bodies read as they come (open, Body) ----
//
// A worker runs the request (asuka_fetch_stream): its head answers open,
// each piece of the body goes to the read waiting for it, or into a
// queue the reads take from. The worker waits while the queue holds more
// than a megabyte, until reads take it. close() and letting the Body go
// abort the request. A Body is freed on the main thread, where the
// cancels that hold it (open's, a read's) run, once neither the worker
// nor the core holds it.

#ifdef _WIN32
typedef SRWLOCK mutex_t;
typedef CONDITION_VARIABLE cond_t;
#define mu_init(m) InitializeSRWLock(m)
#define mu_lock(m) AcquireSRWLockExclusive(m)
#define mu_unlock(m) ReleaseSRWLockExclusive(m)
#define mu_destroy(m) ((void)(m))
#define cv_init(c) InitializeConditionVariable(c)
#define cv_wait(c, m) SleepConditionVariableSRW(c, m, INFINITE, 0)
#define cv_broadcast(c) WakeAllConditionVariable(c)
#define cv_destroy(c) ((void)(c))
#else
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;
#define mu_init(m) pthread_mutex_init(m, NULL)
#define mu_lock(m) pthread_mutex_lock(m)
#define mu_unlock(m) pthread_mutex_unlock(m)
#define mu_destroy(m) pthread_mutex_destroy(m)
#define cv_init(c) pthread_cond_init(c, NULL)
#define cv_wait(c, m) pthread_cond_wait(c, m)
#define cv_broadcast(c) pthread_cond_broadcast(c)
#define cv_destroy(c) pthread_cond_destroy(c)
#endif

// The most the queue holds before the worker waits for reads.
enum { queue_most = 1 << 20 };

typedef struct piece {
	struct piece *next;
	asuka_bytes b;
} piece;

typedef struct {
	http_Body base;
	int refs;  // the worker's and the core's: the main thread's only
	bool given;  // open answered with it (the worker's, then read on the main thread)
	asuka_abort *abort;
	// The request, copied: the call's arguments go as open answers.
	char *method, *url;
	asuka_header *headers;
	size_t nheaders;
	asuka_bytes body;
	int timeout_ms;
	const asuka_tls *tls;
	asuka_call *open;     // until the head answers it (the worker's)
	asuka_response head;  // written before open answers, read only after
	mutex_t mu;
	cond_t cv;
	// Under mu:
	piece *first, *last;
	size_t queued;
	asuka_call *reading;  // a read waiting for a piece
	bool ended;           // the request is over (failure says how)
	bool closed;          // close(), or let go
	asuka_error failure;
} body;

static void copy_error(asuka_error *to, const asuka_error *from) {
	*to = *from;
	to->domain = from->domain ? asuka_strdup(from->domain) : NULL;
	to->message = from->message ? asuka_strdup(from->message) : NULL;
}

static asuka_bytes no_bytes(void) { return (asuka_bytes){NULL, 0}; }

static void free_body(body *b) {
	free(b->method);
	free(b->url);
	for (size_t i = 0; i < b->nheaders; i++) {
		free((char *)b->headers[i].name);
		free((char *)b->headers[i].value);
	}
	free(b->headers);
	free(b->body.data);
	asuka_response_free(&b->head);
	for (piece *p = b->first, *n; p; p = n) {
		n = p->next;
		free(p->b.data);
		free(p);
	}
	asuka_error_free(&b->failure);
	asuka_abort_free(b->abort);
	mu_destroy(&b->mu);
	cv_destroy(&b->cv);
	free(b);
}

// worker_gone drops the worker's hold (and the core's, which it never
// got when open failed), on the main thread.
static void worker_gone(void *p) {
	body *b = p;
	b->refs -= b->given ? 1 : 2;
	if (b->refs <= 0) free_body(b);
}

static bool body_head(void *ctx, const asuka_response *r, asuka_error *err) {
	body *b = ctx;
	if (asuka_call_cancelled(b->open)) {
		asuka_error_cancelled(err);
		return false;
	}
	asuka_response *h = &b->head;
	h->status = r->status;
	h->url = asuka_strdup(r->url);
	h->nheaders = r->nheaders;
	h->names = calloc(r->nheaders ? r->nheaders : 1, sizeof *h->names);
	h->values = calloc(r->nheaders ? r->nheaders : 1, sizeof *h->values);
	for (size_t i = 0; i < r->nheaders; i++) h->names[i] = asuka_strdup(r->names[i]), h->values[i] = asuka_strdup(r->values[i]);
	asuka_call *c = b->open;
	b->open = NULL;
	b->given = true;
	http_Transport_open_done(c, &b->base);
	return true;
}

static bool body_data(void *ctx, const void *p, size_t n, asuka_error *err) {
	(void)err;
	body *b = ctx;
	mu_lock(&b->mu);
	if (b->closed) {
		mu_unlock(&b->mu);
		return false;  // ends the request: no one reads it
	}
	asuka_call *c = b->reading;
	b->reading = NULL;
	if (c) {
		mu_unlock(&b->mu);
		http_Body_read_done(c, asuka_bytes_copy(p, n));
		return true;
	}
	piece *x = malloc(sizeof *x);
	*x = (piece){NULL, asuka_bytes_copy(p, n)};
	if (b->last) b->last->next = x;
	else b->first = x;
	b->last = x;
	b->queued += n;
	while (b->queued > queue_most && !b->closed) cv_wait(&b->cv, &b->mu);
	bool go = !b->closed;
	mu_unlock(&b->mu);
	return go;
}

static void run_open(void *p) {
	body *b = p;
	asuka_request rq = {.method = b->method, .url = b->url, .headers = b->headers, .nheaders = b->nheaders, .body = b->body.data, .len = b->body.len,
	                    .timeout_ms = b->timeout_ms, .abort = b->abort, .tls = b->tls, .head = body_head, .data = body_data, .ctx = b};
	asuka_error e = {0};
	bool ok = asuka_fetch_stream(&rq, NULL, &e);
	if (!b->given) {
		// No head: open fails.
		if (ok) asuka_error_failed(&e, "http", 0, "%s: no response", b->url);
		asuka_call_fail(b->open, &e);
		b->open = NULL;
	} else {
		mu_lock(&b->mu);
		b->ended = true;
		if (!ok) b->failure = e;
		asuka_call *c = b->reading;
		b->reading = NULL;
		asuka_error f = {0};
		if (c && !ok) copy_error(&f, &e);
		mu_unlock(&b->mu);
		if (c && !ok) asuka_call_fail(c, &f);
		else if (c) http_Body_read_done(c, no_bytes());
	}
	asuka_desktop_post(worker_gone, b);
}

static void cancel_open(void *p) { asuka_abort_now(((body *)p)->abort); }

static void cancel_read(void *p) {
	body *b = p;
	mu_lock(&b->mu);
	asuka_call *c = b->reading;
	b->reading = NULL;
	mu_unlock(&b->mu);
	if (c) {
		asuka_error e = {0};
		asuka_error_cancelled(&e);
		asuka_call_fail(c, &e);
	}
}

static http_Response body_head_(http_Body *self, asuka_error *err) {
	(void)err;
	body *b = (body *)self;
	const asuka_response *h = &b->head;
	http_Response r = {.status = h->status, .url = asuka_strdup(h->url)};
	r.headers.len = h->nheaders;
	r.headers.keys = calloc(h->nheaders ? h->nheaders : 1, sizeof *r.headers.keys);
	r.headers.values = calloc(h->nheaders ? h->nheaders : 1, sizeof *r.headers.values);
	for (size_t i = 0; i < h->nheaders; i++) r.headers.keys[i] = asuka_strdup(h->names[i]), r.headers.values[i] = asuka_strdup(h->values[i]);
	return r;
}

static void body_read(http_Body *self, asuka_call *call) {
	body *b = (body *)self;
	asuka_call_on_cancel(call, cancel_read, b);  // before the worker can see the call
	mu_lock(&b->mu);
	if (b->closed) {
		mu_unlock(&b->mu);
		http_Body_read_done(call, no_bytes());
		return;
	}
	if (b->first) {
		piece *x = b->first;
		b->first = x->next;
		if (!b->first) b->last = NULL;
		b->queued -= x->b.len;
		cv_broadcast(&b->cv);
		mu_unlock(&b->mu);
		http_Body_read_done(call, x->b);
		free(x);
		return;
	}
	if (b->failure.status) {
		asuka_error e = {0};
		copy_error(&e, &b->failure);
		mu_unlock(&b->mu);
		asuka_call_fail(call, &e);
		return;
	}
	if (b->ended) {
		mu_unlock(&b->mu);
		http_Body_read_done(call, no_bytes());
		return;
	}
	if (b->reading) {
		mu_unlock(&b->mu);
		asuka_error e = {0};
		asuka_error_failed(&e, "http", 0, "a read is already waiting for the body");
		asuka_call_fail(call, &e);
		return;
	}
	b->reading = call;
	mu_unlock(&b->mu);
}

// stop ends the request: a read waiting gets the body's end.
static void stop(body *b) {
	mu_lock(&b->mu);
	b->closed = true;
	asuka_call *c = b->reading;
	b->reading = NULL;
	cv_broadcast(&b->cv);
	mu_unlock(&b->mu);
	asuka_abort_now(b->abort);
	if (c) http_Body_read_done(c, no_bytes());
}

static void body_close(http_Body *self, asuka_error *err) {
	(void)err;
	stop((body *)self);
}

static void body_release(http_Body *self) {
	body *b = (body *)self;
	stop(b);
	if (--b->refs <= 0) free_body(b);
}

static const http_Body_vtable body_vt = {.release = body_release, .head = body_head_, .read = body_read, .close = body_close};

static void open_(http_Transport *self, const char *method, const char *url, http_map_string headers, asuka_bytes data, int64_t timeout,
                  asuka_call *call) {
	body *b = calloc(1, sizeof *b);
	b->base.vt = &body_vt;
	b->tls = tls_of(self);
	b->refs = 2;
	b->abort = asuka_abort_new();
	b->method = asuka_strdup(method);
	b->url = asuka_strdup(url);
	b->nheaders = headers.len;
	b->headers = calloc(headers.len ? headers.len : 1, sizeof *b->headers);
	for (size_t i = 0; i < headers.len; i++) b->headers[i] = (asuka_header){asuka_strdup(headers.keys[i]), asuka_strdup(headers.values[i])};
	b->body = asuka_bytes_copy(data.data, data.len);
	int64_t ms = timeout > 0 ? (timeout + 999999) / 1000000 : 0;
	b->timeout_ms = ms > INT_MAX ? INT_MAX : (int)ms;
	b->open = call;
	mu_init(&b->mu);
	cv_init(&b->cv);
	asuka_call_on_cancel(call, cancel_open, b);
	if (!asuka_desktop_spawn(run_open, b)) {
		asuka_error e = {0};
		asuka_error_failed(&e, "http", 0, "no thread for the request");
		asuka_call_fail(call, &e);
		free_body(b);
	}
}

http_Body *http_make_Body(asuka_error *err) {
	asuka_error_unsupported(err, "a Body comes from Transport.open");
	return NULL;
}

// ---- Secured transports: TLS settings, read on a thread ----

static const http_Transport_vtable vt;

typedef struct {
	const http_TLS *tls;
	asuka_call *call;
} securing;

static void run_secured(void *p) {
	securing *q = p;
	const http_TLS *t = q->tls;
	asuka_error e = {0};
	asuka_tls *tls = asuka_tls_get(t->ca, t->trustSystem, t->certificate, t->key, t->insecureSkipVerify, &e);
	if (tls) {
		transport *x = malloc(sizeof *x);
		*x = (transport){{&vt}, tls};
		http_Transport_secured_done(q->call, &x->base);
	} else {
		asuka_call_fail(q->call, &e);
	}
	free(q);
}

static void secured_(http_Transport *self, const http_TLS *tls, asuka_call *call) {
	(void)self;
	securing *q = malloc(sizeof *q);
	*q = (securing){tls, call};
	if (!asuka_desktop_spawn(run_secured, q)) {
		free(q);
		asuka_error e = {0};
		asuka_error_failed(&e, "http", 0, "no thread for the TLS settings");
		asuka_call_fail(call, &e);
	}
}

static const http_Transport_vtable vt = {.send = send_, .download = download_, .open = open_, .secured = secured_};

// ---- LocalServer: one-shot pages on the loopback, for a redirect ----

typedef struct server {
	struct server *next;
	int64_t port;
	sock_t fd;
	char *path, *page;
} server;

static server *servers;
#ifdef _WIN32
static SRWLOCK servers_mu = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&servers_mu)
#define UNLOCK() ReleaseSRWLockExclusive(&servers_mu)
#else
static pthread_mutex_t servers_mu = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&servers_mu)
#define UNLOCK() pthread_mutex_unlock(&servers_mu)
#endif

static bool sockets(asuka_error *err) {
#ifdef _WIN32
	static bool started;
	if (!started) {
		WSADATA w;
		if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
			asuka_error_failed(err, "http", 0, "no sockets (WSAStartup)");
			return false;
		}
		started = true;
	}
#endif
	(void)err;
	return true;
}

static void listen_(http_LocalServer *self, const char *path, const char *page, asuka_call *call) {
	(void)self;
	asuka_error e = {0};
	if (!sockets(&e)) {
		asuka_call_fail(call, &e);
		return;
	}
	sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = 0;  // the system's pick
	socklen_t n = sizeof a;
	if (fd == BAD_SOCK || bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(fd, 4) != 0 || getsockname(fd, (struct sockaddr *)&a, &n) != 0) {
		if (fd != BAD_SOCK) close_sock(fd);
		asuka_error_failed(&e, "http", 0, "can't listen on the loopback");
		asuka_call_fail(call, &e);
		return;
	}
	server *s = calloc(1, sizeof *s);
	s->port = ntohs(a.sin_port);
	s->fd = fd;
	s->path = asuka_strdup(path);
	s->page = asuka_strdup(page);
	LOCK();
	s->next = servers;
	servers = s;
	UNLOCK();
	http_LocalServer_listen_done(call, s->port);
}

// take removes port's server from the list (NULL: none).
static server *take(int64_t port) {
	LOCK();
	server **pp = &servers;
	while (*pp && (*pp)->port != port) pp = &(*pp)->next;
	server *s = *pp;
	if (s) *pp = s->next;
	UNLOCK();
	return s;
}

static void free_server(server *s) {
	if (s->fd != BAD_SOCK) close_sock(s->fd);
	free(s->path);
	free(s->page);
	free(s);
}

typedef struct {
	int64_t port;
	sock_t fd;  // the listener's, shared with its server
	char *path, *page;
	asuka_call *call;
} accepting;

// stop_accepting is a next call's cancel: closing the listener ends its
// accept.
static void stop_accepting(void *p) {
	accepting *x = p;
#ifdef _WIN32
	shutdown(x->fd, SD_BOTH);
#else
	shutdown(x->fd, SHUT_RDWR);
#endif
}

static void send_all(sock_t c, const char *p, size_t n) {
	while (n > 0) {
		int k = (int)send(c, p, (int)n, 0);
		if (k <= 0) return;
		p += k, n -= (size_t)k;
	}
}

static void run_next(void *p) {
	accepting *x = p;
	for (;;) {
		sock_t c = accept(x->fd, NULL, NULL);
		if (c == BAD_SOCK) {
			asuka_error e = {0};
			if (asuka_call_cancelled(x->call)) asuka_error_cancelled(&e);
			else asuka_error_failed(&e, "http", 0, "the loopback server stopped");
			asuka_call_fail(x->call, &e);
			break;
		}
		// The request's head: its first line is what we need.
		char buf[8192];
		size_t got = 0;
		while (got < sizeof buf - 1) {
			int k = (int)recv(c, buf + got, (int)(sizeof buf - 1 - got), 0);
			if (k <= 0) break;
			got += (size_t)k;
			buf[got] = 0;
			if (strstr(buf, "\r\n\r\n")) break;
		}
		buf[got] = 0;
		char *sp = strchr(buf, ' '), *target = sp ? sp + 1 : NULL, *end = target ? strchr(target, ' ') : NULL;
		size_t plen = strlen(x->path);
		bool ours = target && end && strncmp(target, x->path, plen) == 0 && (target[plen] == '?' || target + plen == end);
		if (!ours) {
			const char *nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			send_all(c, nf, strlen(nf));
			close_sock(c);
			continue;
		}
		char *head = asuka_sprintf("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(x->page));
		send_all(c, head, strlen(head));
		send_all(c, x->page, strlen(x->page));
		free(head);
		close_sock(c);
		char *url = asuka_sprintf("http://127.0.0.1:%lld%.*s", (long long)x->port, (int)(end - target), target);
		http_LocalServer_next_done(x->call, url);  // takes url
		break;
	}
	free(x->path);
	free(x->page);
	free(x);
}

static void next_(http_LocalServer *self, int64_t port, asuka_call *call) {
	(void)self;
	asuka_error e = {0};
	LOCK();
	server *s = servers;
	while (s && s->port != port) s = s->next;
	accepting *x = NULL;
	if (s) {
		x = calloc(1, sizeof *x);
		*x = (accepting){port, s->fd, asuka_strdup(s->path), asuka_strdup(s->page), call};
	}
	UNLOCK();
	if (!x) {
		asuka_error_failed(&e, "http", 0, "no loopback server at port %lld", (long long)port);
		asuka_call_fail(call, &e);
		return;
	}
	asuka_call_on_cancel(call, stop_accepting, x);
	if (!asuka_desktop_spawn(run_next, x)) {
		free(x->path);
		free(x->page);
		free(x);
		asuka_error_failed(&e, "http", 0, "no thread for the loopback server");
		asuka_call_fail(call, &e);
	}
}

static void close_(http_LocalServer *self, int64_t port, asuka_error *err) {
	(void)self, (void)err;
	server *s = take(port);
	if (s) {
#ifdef _WIN32
		shutdown(s->fd, SD_BOTH);
#else
		shutdown(s->fd, SHUT_RDWR);
#endif
		free_server(s);
	}
}

static const http_LocalServer_vtable local_vt = {.listen = listen_, .next = next_, .close = close_};

http_LocalServer *http_make_LocalServer(asuka_error *err) {
	(void)err;
	http_LocalServer *s = malloc(sizeof *s);
	s->vt = &local_vt;
	return s;
}

http_Transport *http_make_Transport(asuka_error *err) {
	(void)err;
	transport *t = malloc(sizeof *t);
	*t = (transport){{&vt}, NULL};
	return &t->base;
}
