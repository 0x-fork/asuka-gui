// std/websocket on Windows and Linux: WinHTTP's WebSockets on Windows
// (the system's proxy settings and certificates, as asuka_fetch's), on
// threads of the connection's; libsoup 3's on Linux, on GLib's main
// context (the GTK host's main loop: the main thread, where the calls
// come).
//
// What comes in (messages, then the connection's end) goes to the
// receive waiting for it, or into a queue receives take from. Once the
// connection is closed, every receive gets its closed frame (a failure
// first, once, if it failed). A Socket is freed on the main thread, where
// the cancels that hold it run, once nothing holds it.
#include "websocket.asuka.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "asuka_windows.h"
#include <winhttp.h>
#else
#include "asuka_linux.h"
#include <pthread.h>
#endif

#ifdef _WIN32
typedef SRWLOCK mutex_t;
typedef CONDITION_VARIABLE cond_t;
#define mu_init(m) InitializeSRWLock(m)
#define mu_lock(m) AcquireSRWLockExclusive(m)
#define mu_unlock(m) ReleaseSRWLockExclusive(m)
#define mu_destroy(m) ((void)(m))
#else
typedef pthread_mutex_t mutex_t;
#define mu_init(m) pthread_mutex_init(m, NULL)
#define mu_lock(m) pthread_mutex_lock(m)
#define mu_unlock(m) pthread_mutex_unlock(m)
#define mu_destroy(m) pthread_mutex_destroy(m)
#endif

// The close codes a frame reports when the server gave none, and when the
// connection ended without a close.
enum { no_status = 1005, dropped = 1006 };

typedef struct frame {
	struct frame *next;
	websocket_Frame f;
} frame;

#ifdef _WIN32
// What the sender sends, in order.
typedef struct op {
	struct op *next;
	enum { op_text, op_binary, op_close } kind;
	const void *data;  // the call's argument (valid until it answers)
	size_t len;
	int code;
	char *reason;
	asuka_call *call;  // NULL for a close
} op;
#endif

typedef struct {
	websocket_Socket base;
	atomic_int refs;
	char *url;
	char *protocol;
	mutex_t mu;
	// Under mu:
	frame *first, *last;
	asuka_call *receiving;  // a receive waiting for a frame
	bool closed;            // the connection ended: code, reason
	int code;
	char *reason;
	asuka_error failure;  // how it failed, for the next receive
	bool shut;            // a close was started (shutdown, close, the server's)
	bool released;
#ifdef _WIN32
	cond_t cv;
	HINTERNET session, connection, request, ws;
	bool cancelled;  // the connect's task went
	op *ops, *ops_last;
	int threads;  // the receiver and the sender: the last closes the handles
	bool receiver_done;
#else
	SoupSession *session;
	SoupWebsocketConnection *conn;
	GCancellable *cancel;
	guint timer;
	bool timed_out;
	asuka_error error;  // the connection's error signal, for its end
#endif
} sock;

static const websocket_Socket_vtable socket_vt;

static sock *new_sock(const char *url) {
	sock *s = calloc(1, sizeof *s);
	s->base.vt = &socket_vt;
	atomic_init(&s->refs, 1);
	s->url = asuka_strdup(url);
	s->protocol = asuka_strdup("");
	mu_init(&s->mu);
#ifdef _WIN32
	InitializeConditionVariable(&s->cv);
#endif
	return s;
}

static void free_sock(void *p) {
	sock *s = p;
	for (frame *f = s->first, *n; f; f = n) {
		n = f->next;
		websocket_Frame_free(&f->f);
		free(f);
	}
	free(s->url);
	free(s->protocol);
	free(s->reason);
	asuka_error_free(&s->failure);
#ifndef _WIN32
	asuka_error_free(&s->error);
	if (s->conn) G(g_object_unref)(s->conn);
	if (s->cancel) G(g_object_unref)(s->cancel);
	if (s->session) G(g_object_unref)(s->session);
#endif
	mu_destroy(&s->mu);
	free(s);
}

#ifdef _WIN32
static void ref(sock *s) { atomic_fetch_add(&s->refs, 1); }
#endif

// unref drops a hold; the last frees s on the main thread.
static void unref(sock *s) {
	if (atomic_fetch_sub(&s->refs, 1) != 1) return;
	if (asuka_desktop_on_main()) free_sock(s);
	else asuka_desktop_post(free_sock, s);
}

static websocket_Frame closed_frame(const sock *s) {
	return (websocket_Frame){websocket_FrameKind_closed, asuka_strdup(s->reason), {NULL, 0}, s->code};
}

static void fail_closed(asuka_call *c) {
	asuka_error e = {0};
	asuka_error_failed(&e, "websocket", 0, "the WebSocket is closed");
	asuka_call_fail(c, &e);
}

// got_message hands a message to the receive waiting, or queues it. Any
// thread.
static void got_message(sock *s, websocket_FrameKind kind, char *text, asuka_bytes data) {
	websocket_Frame f = {kind, text, data, 0};
	mu_lock(&s->mu);
	asuka_call *c = s->receiving;
	s->receiving = NULL;
	if (!c) {
		frame *x = malloc(sizeof *x);
		*x = (frame){NULL, f};
		if (s->last) s->last->next = x;
		else s->first = x;
		s->last = x;
	}
	mu_unlock(&s->mu);
	if (c) websocket_Socket_receive_done(c, f);
}

// got_end records the connection's end (with how it failed, taken, if
// it did), and answers the receive waiting. Any thread; once.
static void got_end(sock *s, int code, const char *reason, asuka_error *failure) {
	mu_lock(&s->mu);
	if (s->closed) {
		mu_unlock(&s->mu);
		if (failure) asuka_error_free(failure);
		return;
	}
	s->closed = true;
	s->shut = true;
	s->code = code;
	free(s->reason);
	s->reason = asuka_strdup(reason);
	if (failure) s->failure = *failure;
	asuka_call *c = s->receiving;
	s->receiving = NULL;
	asuka_error e = {0};
	websocket_Frame f = {0};
	if (c && s->failure.status) e = s->failure, s->failure = (asuka_error){0};
	else if (c) f = closed_frame(s);
#ifdef _WIN32
	WakeAllConditionVariable(&s->cv);
#endif
	mu_unlock(&s->mu);
	if (c && e.status) asuka_call_fail(c, &e);
	else if (c) websocket_Socket_receive_done(c, f);
}

static void cancel_receive(void *p) {
	sock *s = p;
	mu_lock(&s->mu);
	asuka_call *c = s->receiving;
	s->receiving = NULL;
	mu_unlock(&s->mu);
	if (c) {
		asuka_error e = {0};
		asuka_error_cancelled(&e);
		asuka_call_fail(c, &e);
	}
}

static void receive(websocket_Socket *self, asuka_call *call) {
	sock *s = (sock *)self;
	asuka_call_on_cancel(call, cancel_receive, s);  // before anyone else can answer it
	mu_lock(&s->mu);
	if (s->first) {
		frame *x = s->first;
		s->first = x->next;
		if (!s->first) s->last = NULL;
		mu_unlock(&s->mu);
		websocket_Socket_receive_done(call, x->f);
		free(x);
		return;
	}
	if (s->failure.status) {
		asuka_error e = s->failure;
		s->failure = (asuka_error){0};
		mu_unlock(&s->mu);
		asuka_call_fail(call, &e);
		return;
	}
	if (s->closed) {
		websocket_Frame f = closed_frame(s);
		mu_unlock(&s->mu);
		websocket_Socket_receive_done(call, f);
		return;
	}
	if (s->receiving) {
		mu_unlock(&s->mu);
		asuka_error e = {0};
		asuka_error_failed(&e, "websocket", 0, "a receive is already waiting");
		asuka_call_fail(call, &e);
		return;
	}
	s->receiving = call;
	mu_unlock(&s->mu);
}

static char *protocol(websocket_Socket *self, asuka_error *err) {
	(void)err;
	return asuka_strdup(((sock *)self)->protocol);
}

// valid_code reports whether a client may close with code (RFC 6455,
// 7.4: the defined codes an endpoint sends, and 3000 to 4999).
static bool valid_code(int64_t code) {
	return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
}

// close_reason is reason cut to a close frame's 123 bytes, at a
// character's start.
static char *close_reason(const char *reason) {
	size_t n = reason ? strlen(reason) : 0;
	if (n > 123) {
		n = 123;
		while (n > 0 && ((unsigned char)reason[n] & 0xc0) == 0x80) n--;
	}
	return asuka_strndup(reason ? reason : "", n);
}

// http_url is a ws: or wss: URL as http: or https: (what WinHTTP and
// libsoup open); NULL for another.
static char *http_url(const char *url) {
	if (strncmp(url, "ws://", 5) == 0) return asuka_sprintf("http://%s", url + 5);
	if (strncmp(url, "wss://", 6) == 0) return asuka_sprintf("https://%s", url + 6);
	if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) return asuka_strdup(url);
	return NULL;
}

#ifdef _WIN32

// ---- Windows: WinHTTP ----

static void push_op(sock *s, op *o) {
	if (s->ops_last) s->ops_last->next = o;
	else s->ops = o;
	s->ops_last = o;
	WakeAllConditionVariable(&s->cv);
}

// start_close queues a close frame, unless one was (under mu).
static void start_close(sock *s, int code, const char *reason) {
	if (s->shut) return;
	s->shut = true;
	op *o = calloc(1, sizeof *o);
	*o = (op){.kind = op_close, .code = code, .reason = close_reason(reason)};
	push_op(s, o);
}

// thread_done: the last of the receiver and the sender closes the handles.
static void thread_done(sock *s) {
	mu_lock(&s->mu);
	bool last = --s->threads == 0;
	HINTERNET ws = s->ws;
	s->ws = NULL;
	mu_unlock(&s->mu);
	if (last) {
		if (ws) WinHttpCloseHandle(ws);
		if (s->connection) WinHttpCloseHandle(s->connection);
		if (s->session) WinHttpCloseHandle(s->session);
		s->connection = s->session = NULL;
	}
	unref(s);
}

static void fail_win(asuka_error *e, DWORD code, const char *url) {
	if (code == ERROR_WINHTTP_TIMEOUT) asuka_error_failed(e, "websocket", 0, "%s: timed out", url);
	else asuka_win_fail(e, HRESULT_FROM_WIN32(code), url);
}

// run_send sends what's queued, in order, until the connection's end; a
// connection let go is closed after a grace for the server's answer to
// its close.
static void run_send(void *p) {
	sock *s = p;
	mu_lock(&s->mu);
	for (;;) {
		while (!s->ops && !s->receiver_done && !(s->released && s->shut)) SleepConditionVariableSRW(&s->cv, &s->mu, INFINITE, 0);
		op *o = s->ops;
		if (o) {
			s->ops = o->next;
			if (!s->ops) s->ops_last = NULL;
			HINTERNET ws = s->ws;
			mu_unlock(&s->mu);
			DWORD r = ERROR_INVALID_HANDLE;
			if (ws && o->kind == op_close) {
				r = WinHttpWebSocketShutdown(ws, (USHORT)o->code, o->reason[0] ? (void *)o->reason : NULL, (DWORD)strlen(o->reason));
			} else if (ws) {
				WINHTTP_WEB_SOCKET_BUFFER_TYPE t = o->kind == op_text ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
				r = WinHttpWebSocketSend(ws, t, (void *)o->data, (DWORD)o->len);
			}
			if (o->call && r == NO_ERROR) {
				if (o->kind == op_text) websocket_Socket_sendText_done(o->call);
				else websocket_Socket_sendBinary_done(o->call);
			} else if (o->call) {
				asuka_error e = {0};
				fail_win(&e, r, s->url);
				asuka_call_fail(o->call, &e);
			}
			free(o->reason);
			free(o);
			mu_lock(&s->mu);
			continue;
		}
		if (s->receiver_done) break;
		// Let go and closing: the server has a few seconds to answer the
		// close, then the connection is dropped.
		ULONGLONG until = GetTickCount64() + 5000;
		while (!s->receiver_done && !s->ops) {
			ULONGLONG now = GetTickCount64();
			if (now >= until) break;
			SleepConditionVariableSRW(&s->cv, &s->mu, (DWORD)(until - now), 0);
		}
		if (!s->receiver_done && !s->ops) {
			HINTERNET ws = s->ws;
			s->ws = NULL;
			if (ws) WinHttpCloseHandle(ws);  // ends the receive
			while (!s->receiver_done) SleepConditionVariableSRW(&s->cv, &s->mu, INFINITE, 0);
		}
	}
	mu_unlock(&s->mu);
	thread_done(s);
}

// run_receive reads messages until the connection's end.
static void run_receive(sock *s) {
	size_t cap = 64 * 1024, len = 0;
	uint8_t *buf = malloc(cap);
	for (;;) {
		if (cap - len < 4096) buf = realloc(buf, cap *= 2);
		mu_lock(&s->mu);
		HINTERNET ws = s->ws;
		mu_unlock(&s->mu);
		DWORD got = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE type = 0;
		DWORD r = ws ? WinHttpWebSocketReceive(ws, buf + len, (DWORD)(cap - len), &got, &type) : ERROR_WINHTTP_OPERATION_CANCELLED;
		if (r != NO_ERROR) {
			mu_lock(&s->mu);
			bool let_go = s->released || !s->ws;
			mu_unlock(&s->mu);
			// Let go, or the connection dropped without a close: 1006; any
			// other failure is the next receive's error.
			if (let_go || r == ERROR_WINHTTP_OPERATION_CANCELLED || r == ERROR_WINHTTP_CONNECTION_ERROR || r == ERROR_INVALID_HANDLE) {
				got_end(s, dropped, "", NULL);
			} else {
				asuka_error e = {0};
				fail_win(&e, r, s->url);
				got_end(s, dropped, "", &e);
			}
			break;
		}
		len += got;
		if (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
		if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
			got_message(s, websocket_FrameKind_binary, asuka_strdup(""), asuka_bytes_copy(buf, len));
			len = 0;
			continue;
		}
		if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
			got_message(s, websocket_FrameKind_text, asuka_strndup((const char *)buf, len), (asuka_bytes){NULL, 0});
			len = 0;
			continue;
		}
		// The server's close: answered with its code (unless the client's
		// started it), then the end.
		USHORT code = no_status;
		char reason[124] = {0};
		DWORD rn = 0;
		if (WinHttpWebSocketQueryCloseStatus(ws, &code, reason, sizeof reason - 1, &rn) != NO_ERROR) code = no_status, rn = 0;
		reason[rn] = 0;
		mu_lock(&s->mu);
		start_close(s, code, "");
		mu_unlock(&s->mu);
		got_end(s, code, reason, NULL);
		break;
	}
	free(buf);
	mu_lock(&s->mu);
	s->receiver_done = true;
	WakeAllConditionVariable(&s->cv);
	mu_unlock(&s->mu);
	thread_done(s);
}

typedef struct {
	sock *s;
	const char *url;
	websocket_map_string headers;
	websocket_list_string protocols;
	int timeout_ms;
	asuka_call *call;
} connecting;

static void cancel_connect(void *p) {
	sock *s = p;
	mu_lock(&s->mu);
	s->cancelled = true;
	if (s->request) WinHttpCloseHandle(s->request), s->request = NULL;
	mu_unlock(&s->mu);
}

// connect_fail fails the connect: its handles closed, s let go.
static void connect_fail(connecting *c, asuka_error *e) {
	sock *s = c->s;
	mu_lock(&s->mu);
	HINTERNET req = s->request;
	s->request = NULL;
	if (s->cancelled) asuka_error_free(e), asuka_error_cancelled(e);
	mu_unlock(&s->mu);
	if (req) WinHttpCloseHandle(req);
	if (s->connection) WinHttpCloseHandle(s->connection);
	if (s->session) WinHttpCloseHandle(s->session);
	s->connection = s->session = NULL;
	asuka_call_fail(c->call, e);
	free(c);
	unref(s);
}

static void run_connect(void *p) {
	connecting *c = p;
	sock *s = c->s;
	asuka_error e = {0};
	char *url = http_url(c->url);
	if (!url) {
		asuka_error_failed(&e, "websocket", 0, "%s isn't a ws or wss URL", c->url);
		connect_fail(c, &e);
		return;
	}
	wchar_t *wurl = asuka_wide(url);
	free(url);
	URL_COMPONENTS u;
	memset(&u, 0, sizeof u);
	u.dwStructSize = sizeof u;
	wchar_t host[256], path[4096], extra[4096];
	u.lpszHostName = host, u.dwHostNameLength = 256;
	u.lpszUrlPath = path, u.dwUrlPathLength = 4096;
	u.lpszExtraInfo = extra, u.dwExtraInfoLength = 4096;
	bool cracked = WinHttpCrackUrl(wurl, 0, 0, &u);
	free(wurl);
	if (!cracked) {
		asuka_error_failed(&e, "websocket", 0, "%s isn't a ws or wss URL", c->url);
		connect_fail(c, &e);
		return;
	}
	DWORD code = 0;
	s->session = WinHttpOpen(L"Asuka", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s->session) s->session = WinHttpOpen(L"Asuka", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s->session) goto fail;
	if (c->timeout_ms > 0) WinHttpSetTimeouts(s->session, c->timeout_ms, c->timeout_ms, c->timeout_ms, c->timeout_ms);
	s->connection = WinHttpConnect(s->session, host, u.nPort, 0);
	if (!s->connection) goto fail;
	size_t pn = wcslen(path), en = wcslen(extra);
	wchar_t *object = malloc((pn + en + 2) * sizeof(wchar_t));
	wcscpy(object, pn ? path : L"/");
	wcscat(object, extra);
	HINTERNET req = WinHttpOpenRequest(s->connection, L"GET", object, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                   u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
	free(object);
	if (!req) goto fail;
	mu_lock(&s->mu);
	bool cancelled = s->cancelled;
	if (!cancelled) s->request = req;
	mu_unlock(&s->mu);
	if (cancelled) {
		WinHttpCloseHandle(req);
		asuka_error_cancelled(&e);
		connect_fail(c, &e);
		return;
	}
	if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) goto fail;
	for (size_t i = 0; i < c->headers.len; i++) {
		char *line = asuka_sprintf("%s: %s", c->headers.keys[i], c->headers.values[i]);
		wchar_t *w = asuka_wide(line);
		WinHttpAddRequestHeaders(req, w, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
		free(w);
		free(line);
	}
	if (c->protocols.len > 0) {
		asuka_writer line = {0};
		asuka_writer_put(&line, "Sec-WebSocket-Protocol: ", 24);
		for (size_t i = 0; i < c->protocols.len; i++) {
			if (i) asuka_writer_put(&line, ", ", 2);
			asuka_writer_put(&line, c->protocols.data[i], strlen(c->protocols.data[i]));
		}
		asuka_writer_put(&line, "", 1);
		wchar_t *w = asuka_wide((const char *)line.data);
		WinHttpAddRequestHeaders(req, w, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
		free(w);
		asuka_writer_free(&line);
	}
	if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(req, NULL)) goto fail;
	DWORD status = 0, n = sizeof status;
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX);
	if (status != 101) {
		asuka_error_failed(&e, "websocket", status, "%s: the server answered %lu, not a WebSocket", c->url, (unsigned long)status);
		connect_fail(c, &e);
		return;
	}
	wchar_t chosen[256];
	n = sizeof chosen;
	if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"Sec-WebSocket-Protocol", chosen, &n, WINHTTP_NO_HEADER_INDEX)) {
		free(s->protocol);
		s->protocol = asuka_narrow(chosen, (int)(n / sizeof(wchar_t)));
	}
	// The connect's timeout was for the handshake: a connection waits for
	// messages as long as it's open.
	DWORD forever = 0;
	WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof forever);
	HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
	if (!ws) goto fail;
	mu_lock(&s->mu);
	req = s->request;
	s->request = NULL;
	cancelled = s->cancelled;
	s->ws = ws;
	mu_unlock(&s->mu);
	if (req) WinHttpCloseHandle(req);
	if (cancelled) {
		mu_lock(&s->mu);
		s->ws = NULL;
		mu_unlock(&s->mu);
		WinHttpCloseHandle(ws);
		asuka_error_cancelled(&e);
		connect_fail(c, &e);
		return;
	}
	// Connected: this thread receives, another sends; the core holds it.
	s->threads = 2;
	ref(s);  // the sender's
	ref(s);  // the core's
	asuka_call *call = c->call;
	free(c);
	if (!asuka_desktop_spawn(run_send, s)) {
		mu_lock(&s->mu);
		s->receiver_done = true;  // what run_send would have been told
		mu_unlock(&s->mu);
		thread_done(s);
	}
	websocket_Client_connect_done(call, &s->base);
	run_receive(s);
	return;
fail:
	code = GetLastError();
	fail_win(&e, code, c->url);
	connect_fail(c, &e);
}

static void connect_(websocket_Client *self, const char *url, websocket_map_string headers, websocket_list_string protocols, int64_t timeout,
                     asuka_call *call) {
	(void)self;
	sock *s = new_sock(url);
	connecting *c = calloc(1, sizeof *c);
	int64_t ms = timeout > 0 ? (timeout + 999999) / 1000000 : 0;
	*c = (connecting){s, url, headers, protocols, ms > INT32_MAX ? INT32_MAX : (int)ms, call};
	asuka_call_on_cancel(call, cancel_connect, s);
	if (!asuka_desktop_spawn(run_connect, c)) {
		asuka_error e = {0};
		asuka_error_failed(&e, "websocket", 0, "no thread for the connection");
		asuka_call_fail(call, &e);
		free(c);
		unref(s);
	}
}

static void send_op(sock *s, int kind, const void *data, size_t len, asuka_call *call) {
	mu_lock(&s->mu);
	if (s->closed || s->shut) {
		mu_unlock(&s->mu);
		fail_closed(call);
		return;
	}
	op *o = calloc(1, sizeof *o);
	*o = (op){.kind = kind, .data = data, .len = len, .call = call};
	push_op(s, o);
	mu_unlock(&s->mu);
}

static void send_text(websocket_Socket *self, const char *text, asuka_call *call) { send_op((sock *)self, op_text, text, strlen(text), call); }

static void send_binary(websocket_Socket *self, asuka_bytes data, asuka_call *call) { send_op((sock *)self, op_binary, data.data, data.len, call); }

static void shutdown_(websocket_Socket *self, int64_t code, const char *reason, asuka_error *err) {
	sock *s = (sock *)self;
	if (!valid_code(code)) {
		asuka_error_failed(err, "websocket", code, "%lld isn't a close code a client sends", (long long)code);
		return;
	}
	mu_lock(&s->mu);
	if (!s->closed) start_close(s, (int)code, reason);
	mu_unlock(&s->mu);
}

// let_go closes the connection if it's open, and has the sender drop it
// if the server doesn't answer.
static void let_go(sock *s) {
	mu_lock(&s->mu);
	s->released = true;
	if (!s->closed) start_close(s, 1000, "");
	WakeAllConditionVariable(&s->cv);
	mu_unlock(&s->mu);
}

#else

// ---- Linux: libsoup 3, on GLib's main context ----

static bool load_soup(void) {
	return asuka_linux() && asuka_linux_load(ASUKA_LIB_SOUP) && G(soup_session_new) && G(soup_message_new) &&
	       G(soup_session_websocket_connect_async) && G(soup_session_websocket_connect_finish) && G(soup_websocket_connection_send_text) &&
	       G(soup_websocket_connection_send_binary) && G(soup_websocket_connection_close) && G(soup_websocket_connection_get_close_code) &&
	       G(soup_websocket_connection_get_state) && G(g_cancellable_new) && G(g_cancellable_cancel);
}

enum { state_open = 1, state_closing = 2 };  // SoupWebsocketState
enum { data_text = 1 };   // SoupWebsocketDataType

static void on_message(SoupWebsocketConnection *conn, gint type, GBytes *message, gpointer p) {
	(void)conn;
	gsize n = 0;
	const char *d = message ? G(g_bytes_get_data)(message, &n) : NULL;
	if (type == data_text) got_message(p, websocket_FrameKind_text, asuka_strndup(d ? d : "", n), (asuka_bytes){NULL, 0});
	else got_message(p, websocket_FrameKind_binary, asuka_strdup(""), asuka_bytes_copy(d, n));
}

static void on_error(SoupWebsocketConnection *conn, GError *e, gpointer p) {
	(void)conn;
	sock *s = p;
	if (!s->error.status && e) asuka_error_failed(&s->error, "gio", e->code, "%s: %s", s->url, e->message);
}

static void on_closed(SoupWebsocketConnection *conn, gpointer p) {
	sock *s = p;
	int code = G(soup_websocket_connection_get_close_code)(conn);
	const char *reason = G(soup_websocket_connection_get_close_data) ? G(soup_websocket_connection_get_close_data)(conn) : NULL;
	asuka_error e = s->error;
	s->error = (asuka_error){0};
	// No close from the server: the connection dropped (or failed).
	got_end(s, code ? code : dropped, code && reason ? reason : "", code == 0 && e.status ? &e : NULL);
	if (code && e.status) asuka_error_free(&e);
}

typedef struct {
	sock *s;
	asuka_call *call;
} connecting;

static gboolean connect_timed_out(gpointer p) {
	sock *s = p;
	s->timer = 0;
	s->timed_out = true;
	G(g_cancellable_cancel)(s->cancel);
	return false;
}

static void cancel_connect(void *p) { G(g_cancellable_cancel)(((sock *)p)->cancel); }

static void connected(GObject *source, GAsyncResult *res, gpointer p) {
	(void)source;
	connecting *c = p;
	sock *s = c->s;
	asuka_call *call = c->call;
	free(c);
	if (s->timer) G(g_source_remove)(s->timer), s->timer = 0;
	GError *ge = NULL;
	SoupWebsocketConnection *conn = G(soup_session_websocket_connect_finish)(s->session, res, &ge);
	if (!conn) {
		asuka_error e = {0};
		if (s->timed_out) {
			asuka_error_failed(&e, "websocket", 0, "%s: timed out", s->url);
			if (ge) G(g_error_free)(ge);
		} else if (asuka_call_cancelled(call)) {
			asuka_error_cancelled(&e);
			if (ge) G(g_error_free)(ge);
		} else {
			asuka_linux_fail(&e, ge, s->url);
		}
		asuka_call_fail(call, &e);
		unref(s);
		return;
	}
	s->conn = conn;
	const char *proto = G(soup_websocket_connection_get_protocol) ? G(soup_websocket_connection_get_protocol)(conn) : NULL;
	free(s->protocol);
	s->protocol = asuka_strdup(proto);
	// As a browser's: no limit on a message's size (libsoup's is 128 KB).
	if (G(soup_websocket_connection_set_max_incoming_payload_size)) G(soup_websocket_connection_set_max_incoming_payload_size)(conn, 0);
	G(g_signal_connect_data)(conn, "message", (GCallback)on_message, s, NULL, 0);
	G(g_signal_connect_data)(conn, "error", (GCallback)on_error, s, NULL, 0);
	G(g_signal_connect_data)(conn, "closed", (GCallback)on_closed, s, NULL, 0);
	websocket_Client_connect_done(call, &s->base);  // the core holds the connect's hold now
}

static void connect_(websocket_Client *self, const char *url, websocket_map_string headers, websocket_list_string protocols, int64_t timeout,
                     asuka_call *call) {
	(void)self;
	asuka_error e = {0};
	if (!load_soup()) {
		asuka_error_unsupported(&e, "WebSockets need libsoup 3 (libsoup-3.0.so.0)");
		asuka_call_fail(call, &e);
		return;
	}
	char *u = http_url(url);
	SoupMessage *msg = u ? G(soup_message_new)("GET", u) : NULL;
	free(u);
	if (!msg) {
		asuka_error_failed(&e, "websocket", 0, "%s isn't a ws or wss URL", url);
		asuka_call_fail(call, &e);
		return;
	}
	SoupMessageHeaders *h = G(soup_message_get_request_headers)(msg);
	for (size_t i = 0; i < headers.len; i++) G(soup_message_headers_append)(h, headers.keys[i], headers.values[i]);
	char **protos = NULL;
	if (protocols.len > 0) {
		protos = calloc(protocols.len + 1, sizeof *protos);
		for (size_t i = 0; i < protocols.len; i++) protos[i] = protocols.data[i];
	}
	sock *s = new_sock(url);
	s->session = G(soup_session_new)();
	if (G(soup_session_set_user_agent)) G(soup_session_set_user_agent)(s->session, "Asuka");
	s->cancel = G(g_cancellable_new)();
	int64_t ms = timeout > 0 ? (timeout + 999999) / 1000000 : 0;
	if (ms > 0) s->timer = G(g_timeout_add)(ms > UINT32_MAX ? UINT32_MAX : (guint)ms, connect_timed_out, s);
	asuka_call_on_cancel(call, cancel_connect, s);
	connecting *c = malloc(sizeof *c);
	*c = (connecting){s, call};
	G(soup_session_websocket_connect_async)(s->session, msg, NULL, protos, 0, s->cancel, connected, c);
	free(protos);  // libsoup copied them into the request
	G(g_object_unref)(msg);
}

static bool open_conn(sock *s) { return s->conn && !s->shut && G(soup_websocket_connection_get_state)(s->conn) == state_open; }

static void send_text(websocket_Socket *self, const char *text, asuka_call *call) {
	sock *s = (sock *)self;
	if (!open_conn(s)) {
		fail_closed(call);
		return;
	}
	G(soup_websocket_connection_send_text)(s->conn, text);
	websocket_Socket_sendText_done(call);
}

static void send_binary(websocket_Socket *self, asuka_bytes data, asuka_call *call) {
	sock *s = (sock *)self;
	if (!open_conn(s)) {
		fail_closed(call);
		return;
	}
	G(soup_websocket_connection_send_binary)(s->conn, data.len ? (const void *)data.data : "", data.len);
	websocket_Socket_sendBinary_done(call);
}

static void shutdown_(websocket_Socket *self, int64_t code, const char *reason, asuka_error *err) {
	sock *s = (sock *)self;
	if (!valid_code(code)) {
		asuka_error_failed(err, "websocket", code, "%lld isn't a close code a client sends", (long long)code);
		return;
	}
	if (!open_conn(s)) return;
	s->shut = true;
	char *r = close_reason(reason);
	G(soup_websocket_connection_close)(s->conn, (gushort)code, r[0] ? r : NULL);
	free(r);
}

static void unref_on_closed(SoupWebsocketConnection *conn, gpointer p) {
	(void)p;
	G(g_object_unref)(conn);
}

static void let_go(sock *s) {
	if (s->released) return;
	s->released = true;
	if (open_conn(s)) {
		s->shut = true;
		G(soup_websocket_connection_close)(s->conn, 1000, NULL);
	}
}

#endif

static void close_(websocket_Socket *self, asuka_error *err) {
	(void)err;
	let_go((sock *)self);
}

static void release(websocket_Socket *self) {
	sock *s = (sock *)self;
	let_go(s);
#ifndef _WIN32
	if (s->conn) {
		// The connection lives on until the server answers its close (or
		// libsoup gives up on it); s doesn't hear of it.
		G(g_signal_handlers_disconnect_matched)(s->conn, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
		if (G(soup_websocket_connection_get_state)(s->conn) == state_closing) G(g_signal_connect_data)(s->conn, "closed", (GCallback)unref_on_closed, NULL, NULL, 0);
		else G(g_object_unref)(s->conn);
		s->conn = NULL;
	}
#endif
	unref(s);
}

static const websocket_Socket_vtable socket_vt = {
    .release = release, .protocol = protocol, .receive = receive, .sendText = send_text, .sendBinary = send_binary, .shutdown = shutdown_, .close = close_};

static const websocket_Client_vtable client_vt = {.connect = connect_};

websocket_Client *websocket_make_Client(asuka_error *err) {
	(void)err;
	websocket_Client *c = malloc(sizeof *c);
	c->vt = &client_vt;
	return c;
}

websocket_Socket *websocket_make_Socket(asuka_error *err) {
	asuka_error_unsupported(err, "a Socket comes from Client.connect");
	return NULL;
}
