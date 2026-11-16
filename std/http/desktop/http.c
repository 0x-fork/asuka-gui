// asuka/http on Windows and Linux: the shared HTTP client (asuka_fetch:
// WinHTTP, libsoup 3), with the system's proxy settings and
// certificates, on a thread of the request's.
#include "http.asuka.h"

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

typedef struct {
	const char *method, *url;
	http_map_string headers;
	asuka_bytes body;
	int64_t timeout;  // nanoseconds
	asuka_call *call;
} request;

static void run(void *p) {
	request *q = p;
	asuka_header *hs = calloc(q->headers.len ? q->headers.len : 1, sizeof *hs);
	for (size_t i = 0; i < q->headers.len; i++) hs[i] = (asuka_header){q->headers.keys[i], q->headers.values[i]};
	asuka_response r;
	asuka_error e = {0};
	int ms = q->timeout > 0 ? (int)((q->timeout + 999999) / 1000000) : 0;
	if (asuka_fetch(q->method, q->url, hs, q->headers.len, q->body.data, q->body.len, ms, q->call, &r, &e)) {
		http_Response out = {.status = r.status, .url = r.url, .body = r.body};
		out.headers = (http_map_string){r.names, r.values, r.nheaders};
		http_Transport_send_done(q->call, out);  // takes r's strings
	} else {
		asuka_call_fail(q->call, &e);
	}
	free(hs);
	free(q);
}

static void send_(http_Transport *self, const char *method, const char *url, http_map_string headers, asuka_bytes body, int64_t timeout, asuka_call *call) {
	request *q = malloc(sizeof *q);
	*q = (request){method, url, headers, body, timeout, call};
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

static bool sink(void *ctx, const asuka_response *head, const void *p, size_t n, asuka_error *err) {
	download *d = ctx;
	if (!d->f) {
		d->total = content_length(head);
		d->f = open_file(d->part);
		if (!d->f) {
			asuka_error_failed(err, "http", 0, "can't write %s", d->path);
			return false;
		}
	}
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
	bool ok = asuka_fetch_to(q->method, q->url, hs, q->headers.len, q->body.data, q->body.len, ms, q->call, sink, q, &r, &e);
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
	(void)self;
	download *q = calloc(1, sizeof *q);
	*q = (download){.method = method, .url = url, .path = path, .headers = headers, .body = body, .timeout = timeout, .progress = progress, .call = call, .total = -1};
	if (!asuka_desktop_spawn(run_download, q)) {
		free(q);
		asuka_error e = {0};
		asuka_error_failed(&e, "http", 0, "no thread for the download");
		asuka_call_fail(call, &e);
	}
}

static const http_Transport_vtable vt = {.send = send_, .download = download_};

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
	http_Transport *t = malloc(sizeof *t);
	t->vt = &vt;
	return t;
}
