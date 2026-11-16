// asuka/http on Windows and Linux: the shared HTTP client (asuka_fetch:
// WinHTTP, libsoup 3), with the system's proxy settings and
// certificates, on a thread of the request's.
#include "http.asuka.h"

#include <stdlib.h>

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

static void send(http_Transport *self, const char *method, const char *url, http_map_string headers, asuka_bytes body, int64_t timeout, asuka_call *call) {
	request *q = malloc(sizeof *q);
	*q = (request){method, url, headers, body, timeout, call};
	if (!asuka_desktop_spawn(run, q)) {
		free(q);
		asuka_error e = {0};
		asuka_error_failed(&e, "http", 0, "no thread for the request");
		asuka_call_fail(call, &e);
	}
}

static const http_Transport_vtable vt = {.send = send};

http_Transport *http_make_Transport(asuka_error *err) {
	http_Transport *t = malloc(sizeof *t);
	t->vt = &vt;
	return t;
}
