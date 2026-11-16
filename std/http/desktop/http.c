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

http_Transport *http_make_Transport(asuka_error *err) {
	http_Transport *t = malloc(sizeof *t);
	t->vt = &vt;
	return t;
}
