// native.c (platform/desktop/src): the native package registry (asuka_desktop.h's providers'
// half). Packages' generated code registers a provider each as the program
// starts; the host routes asuka.h's native_* callbacks here
// (asuka_native_callbacks), and its Native views (asuka_native_view…).
//
// The registry keeps the objects the core holds by handle, the async
// calls running, and the native views shown; it answers in the wire
// format of core/src/native/native.h, status first.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "asuka_desktop.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Locks and threads ----

#ifdef _WIN32
typedef SRWLOCK lock_t;
#define LOCK_INIT SRWLOCK_INIT
static void lock(lock_t *l) { AcquireSRWLockExclusive(l); }
static void unlock(lock_t *l) { ReleaseSRWLockExclusive(l); }
typedef CONDITION_VARIABLE cond_t;
#define COND_INIT CONDITION_VARIABLE_INIT
static void cond_wait(cond_t *c, lock_t *l) { SleepConditionVariableSRW(c, l, INFINITE, 0); }
static void cond_broadcast(cond_t *c) { WakeAllConditionVariable(c); }

typedef struct {
	void (*fn)(void *);
	void *ctx;
} spawned;

static DWORD WINAPI spawn_main(LPVOID p) {
	spawned s = *(spawned *)p;
	free(p);
	s.fn(s.ctx);
	return 0;
}

bool asuka_desktop_spawn(void (*fn)(void *ctx), void *ctx) {
	spawned *s = malloc(sizeof *s);
	if (!s) return false;
	s->fn = fn;
	s->ctx = ctx;
	HANDLE h = CreateThread(NULL, 0, spawn_main, s, 0, NULL);
	if (!h) {
		free(s);
		return false;
	}
	CloseHandle(h);
	return true;
}
#else
typedef pthread_mutex_t lock_t;
#define LOCK_INIT PTHREAD_MUTEX_INITIALIZER
static void lock(lock_t *l) { pthread_mutex_lock(l); }
static void unlock(lock_t *l) { pthread_mutex_unlock(l); }
typedef pthread_cond_t cond_t;
#define COND_INIT PTHREAD_COND_INITIALIZER
static void cond_wait(cond_t *c, lock_t *l) { pthread_cond_wait(c, l); }
static void cond_broadcast(cond_t *c) { pthread_cond_broadcast(c); }

typedef struct {
	void (*fn)(void *);
	void *ctx;
} spawned;

static void *spawn_main(void *p) {
	spawned s = *(spawned *)p;
	free(p);
	s.fn(s.ctx);
	return NULL;
}

bool asuka_desktop_spawn(void (*fn)(void *ctx), void *ctx) {
	spawned *s = malloc(sizeof *s);
	if (!s) return false;
	s->fn = fn;
	s->ctx = ctx;
	pthread_t t;
	if (pthread_create(&t, NULL, spawn_main, s) != 0) {
		free(s);
		return false;
	}
	pthread_detach(t);
	return true;
}
#endif

static void *xrealloc(void *p, size_t n) {
	void *q = realloc(p, n ? n : 1);
	if (!q) {
		fputs("asuka: out of memory\n", stderr);
		abort();
	}
	return q;
}

// ---- Values ----

char *asuka_strndup(const char *s, size_t n) {
	char *d = xrealloc(NULL, n + 1);
	if (n) memcpy(d, s, n);
	d[n] = 0;
	return d;
}

char *asuka_strdup(const char *s) { return asuka_strndup(s ? s : "", s ? strlen(s) : 0); }

static char *vformat(const char *format, va_list ap) {
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(NULL, 0, format, ap2);
	va_end(ap2);
	if (n < 0) return asuka_strdup("");
	char *s = xrealloc(NULL, (size_t)n + 1);
	vsnprintf(s, (size_t)n + 1, format, ap);
	return s;
}

char *asuka_sprintf(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	char *s = vformat(format, ap);
	va_end(ap);
	return s;
}

asuka_bytes asuka_bytes_copy(const void *p, size_t n) {
	asuka_bytes b = {xrealloc(NULL, n), n};
	if (n) memcpy(b.data, p, n);
	return b;
}

// ---- The codec ----

static void put(asuka_writer *w, const void *p, size_t n) {
	if (w->len + n > w->cap) {
		size_t c = w->cap ? w->cap * 2 : 64;
		while (c < w->len + n) c *= 2;
		w->data = xrealloc(w->data, c);
		w->cap = c;
	}
	if (n) memcpy(w->data + w->len, p, n);
	w->len += n;
}

void asuka_writer_u8(asuka_writer *w, uint8_t v) { put(w, &v, 1); }

void asuka_writer_u32(asuka_writer *w, uint32_t v) {
	uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
	put(w, b, 4);
}

void asuka_writer_u64(asuka_writer *w, uint64_t v) {
	uint8_t b[8];
	for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
	put(w, b, 8);
}

void asuka_writer_i64(asuka_writer *w, int64_t v) { asuka_writer_u64(w, (uint64_t)v); }

void asuka_writer_f64(asuka_writer *w, double v) {
	uint64_t u;
	memcpy(&u, &v, 8);
	asuka_writer_u64(w, u);
}

void asuka_writer_bool(asuka_writer *w, bool v) { asuka_writer_u8(w, v ? 1 : 0); }

void asuka_writer_bytes(asuka_writer *w, const void *p, size_t n) {
	asuka_writer_u32(w, (uint32_t)n);
	put(w, p, n);
}

void asuka_writer_put(asuka_writer *w, const void *p, size_t n) { put(w, p, n); }

void asuka_writer_string(asuka_writer *w, const char *s) { asuka_writer_bytes(w, s ? s : "", s ? strlen(s) : 0); }

void asuka_writer_free(asuka_writer *w) {
	free(w->data);
	*w = (asuka_writer){0};
}

static const uint8_t *take(asuka_reader *r, size_t n) {
	if (r->failed || r->len - r->off < n) {
		r->failed = true;
		return NULL;
	}
	const uint8_t *p = r->p + r->off;
	r->off += n;
	return p;
}

uint8_t asuka_reader_u8(asuka_reader *r) {
	const uint8_t *p = take(r, 1);
	return p ? p[0] : 0;
}

uint32_t asuka_reader_u32(asuka_reader *r) {
	const uint8_t *p = take(r, 4);
	return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}

uint64_t asuka_reader_u64(asuka_reader *r) {
	const uint8_t *p = take(r, 8);
	uint64_t v = 0;
	for (int i = 0; p && i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
	return v;
}

int64_t asuka_reader_i64(asuka_reader *r) { return (int64_t)asuka_reader_u64(r); }

double asuka_reader_f64(asuka_reader *r) {
	uint64_t u = asuka_reader_u64(r);
	double v;
	memcpy(&v, &u, 8);
	return v;
}

bool asuka_reader_bool(asuka_reader *r) { return asuka_reader_u8(r) != 0; }

char *asuka_reader_string(asuka_reader *r) {
	uint32_t n = asuka_reader_u32(r);
	const uint8_t *p = take(r, n);
	return p ? asuka_strndup((const char *)p, n) : asuka_strdup("");
}

asuka_bytes asuka_reader_bytes(asuka_reader *r) {
	uint32_t n = asuka_reader_u32(r);
	const uint8_t *p = take(r, n);
	return p ? asuka_bytes_copy(p, n) : asuka_bytes_copy("", 0);
}

uint32_t asuka_reader_count(asuka_reader *r) {
	uint32_t n = asuka_reader_u32(r);
	if (n > r->len - r->off) {
		r->failed = true;
		return 0;
	}
	return n;
}

// ---- Errors ----

static void error_reset(asuka_error *e) {
	free(e->domain);
	free(e->message);
	*e = (asuka_error){0};
}

void asuka_error_free(asuka_error *e) { error_reset(e); }

void asuka_error_declared(asuka_error *e, uint32_t index) {
	error_reset(e);
	e->status = ASUKA_DECLARED;
	e->index = index;
}

void asuka_error_failed(asuka_error *e, const char *domain, int64_t code, const char *format, ...) {
	error_reset(e);
	e->status = ASUKA_FAILED;
	e->domain = asuka_strdup(domain);
	e->code = code;
	va_list ap;
	va_start(ap, format);
	e->message = vformat(format, ap);
	va_end(ap);
}

void asuka_error_unsupported(asuka_error *e, const char *format, ...) {
	error_reset(e);
	e->status = ASUKA_UNSUPPORTED;
	va_list ap;
	va_start(ap, format);
	e->message = vformat(format, ap);
	va_end(ap);
}

void asuka_error_cancelled(asuka_error *e) {
	error_reset(e);
	e->status = ASUKA_CANCELLED;
}

// answer_error rewrites w as e's answer, and frees e's strings.
static void answer_error(asuka_writer *w, asuka_error *e) {
	w->len = 0;
	asuka_writer_u8(w, e->status);
	switch (e->status) {
	case ASUKA_DECLARED:
		asuka_writer_u32(w, e->index);
		break;
	case ASUKA_FAILED:
		asuka_writer_string(w, e->domain);
		asuka_writer_i64(w, e->code);
		asuka_writer_string(w, e->message);
		break;
	case ASUKA_UNSUPPORTED:
		asuka_writer_string(w, e->message);
		break;
	}
	error_reset(e);
}

// ---- Providers ----

static const asuka_native_provider **providers;
static size_t nproviders;

void asuka_native_register(const asuka_native_provider *p) {
	for (size_t i = 0; i < nproviders; i++)
		if (strcmp(providers[i]->path, p->path) == 0) {
			providers[i] = p;  // the last one wins (a package linked twice)
			return;
		}
	providers = xrealloc((void *)providers, (nproviders + 1) * sizeof *providers);
	providers[nproviders++] = p;
}

static const asuka_native_provider *provider_of(const char *path, size_t n) {
	for (size_t i = 0; i < nproviders; i++)
		if (strlen(providers[i]->path) == n && memcmp(providers[i]->path, path, n) == 0) return providers[i];
	return NULL;
}

// ---- Objects ----

typedef struct {
	uint64_t handle;
	const asuka_native_provider *p;
	uint32_t iface;
	void *obj;
	uint32_t given;  // handles given the core it hasn't let go of
} entry;

static lock_t objects_mu = LOCK_INIT;
static entry *objects;
static size_t nobjects, capobjects;
static uint64_t next_handle;

uint64_t asuka_native_handle(const asuka_native_provider *p, uint32_t iface, void *obj) {
	if (!obj) return 0;
	lock(&objects_mu);
	for (size_t i = 0; i < nobjects; i++) {
		if (objects[i].obj == obj && objects[i].p == p && objects[i].iface == iface) {
			objects[i].given++;
			uint64_t h = objects[i].handle;
			unlock(&objects_mu);
			return h;
		}
	}
	if (nobjects == capobjects) {
		capobjects = capobjects ? capobjects * 2 : 16;
		objects = xrealloc(objects, capobjects * sizeof *objects);
	}
	uint64_t h = ++next_handle;
	objects[nobjects++] = (entry){h, p, iface, obj, 1};
	unlock(&objects_mu);
	return h;
}

static bool find(uint64_t handle, entry *out) {
	lock(&objects_mu);
	for (size_t i = 0; i < nobjects; i++) {
		if (objects[i].handle == handle) {
			*out = objects[i];
			unlock(&objects_mu);
			return true;
		}
	}
	unlock(&objects_mu);
	return false;
}

void *asuka_native_object(const asuka_native_provider *p, uint32_t iface, uint64_t handle) {
	entry e;
	if (!handle || !find(handle, &e) || e.p != p || e.iface != iface) return NULL;
	return e.obj;
}

// ---- Answers ----

static asuka_writer scratch;  // a synchronous answer's (main thread)

static size_t give(asuka_writer *w, uint8_t *out, size_t cap, uint8_t **big) {
	if (w->len <= cap) {
		memcpy(out, w->data, w->len);
		return w->len;
	}
	*big = malloc(w->len);
	memcpy(*big, w->data, w->len);
	return w->len;
}

static void unsupported(asuka_writer *w, const char *format, ...) {
	w->len = 0;
	asuka_writer_u8(w, ASUKA_UNSUPPORTED);
	va_list ap;
	va_start(ap, format);
	char *m = vformat(format, ap);
	va_end(ap);
	asuka_writer_string(w, m);
	free(m);
}

static size_t n_new(const uint8_t *cls, size_t len, uint32_t iface, uint8_t *out, size_t cap, uint8_t **big) {
	asuka_writer *w = &scratch;
	w->len = 0;
	const asuka_native_provider *p = provider_of((const char *)cls, len);
	if (!p) {
		unsupported(w, "%.*s has no native code for this platform (no C in its desktop/)", (int)len, (const char *)cls);
		return give(w, out, cap, big);
	}
	asuka_error err = {0};
	void *obj = p->make(iface, &err);
	if (err.status || !obj) {
		if (!err.status) asuka_error_unsupported(&err, "%s: no interface %u here", p->path, iface);
		answer_error(w, &err);
		return give(w, out, cap, big);
	}
	asuka_writer_u8(w, ASUKA_OK);
	asuka_writer_u64(w, asuka_native_handle(p, iface, obj));
	return give(w, out, cap, big);
}

static size_t n_call(uint64_t handle, uint32_t method, const uint8_t *in, size_t len, uint8_t *out, size_t cap, uint8_t **big) {
	asuka_writer *w = &scratch;
	w->len = 0;
	entry e;
	if (!find(handle, &e)) {
		unsupported(w, "the object was let go");
		return give(w, out, cap, big);
	}
	asuka_writer_u8(w, ASUKA_OK);
	asuka_reader r = {in, len, 0, false};
	asuka_error err = {0};
	e.p->call(e.obj, e.iface, method, &r, w, &err);
	if (err.status) answer_error(w, &err);
	size_t n = give(w, out, cap, big);
	if (w->cap > 64 << 10) asuka_writer_free(w);  // a big answer's buffer goes
	return n;
}

// ---- Started calls ----

struct asuka_call {
	uint64_t id;
	atomic_int refs;
	atomic_bool cancelled;
	atomic_bool answered;
	// What the task's going runs: set on any thread (asuka_fetch's
	// worker sets its own), so read and written under calls_mu, both
	// together.
	void (*on_cancel)(void *);
	void *cancel_ctx;
	// The main thread is running the cancel: an answer from another
	// thread returns only once it has, since its context may go then.
	atomic_int cancelling;
	void *args;
	void (*free_args)(void *);
	asuka_call *next;  // in calls
};

static lock_t calls_mu = LOCK_INIT;
static asuka_call *calls;
static lock_t cancel_mu = LOCK_INIT;  // cancelling's ends, waited for on cancel_cv
static cond_t cancel_cv = COND_INIT;

static void call_unref(asuka_call *c) {
	if (atomic_fetch_sub(&c->refs, 1) == 1) free(c);
}

// call_unlink takes c out of calls: false if it was already.
static bool call_unlink(asuka_call *c) {
	lock(&calls_mu);
	for (asuka_call **p = &calls; *p; p = &(*p)->next) {
		if (*p == c) {
			*p = c->next;
			unlock(&calls_mu);
			return true;
		}
	}
	unlock(&calls_mu);
	return false;
}

static void n_start(uint64_t handle, uint32_t method, const uint8_t *in, size_t len, uint64_t id) {
	asuka_call *c = calloc(1, sizeof *c);
	c->id = id;
	atomic_init(&c->refs, 2);  // calls', and the package's until it answers
	lock(&calls_mu);
	c->next = calls;
	calls = c;
	unlock(&calls_mu);
	entry e;
	if (!find(handle, &e)) {
		asuka_error err = {0};
		asuka_error_unsupported(&err, "the object was let go");
		asuka_call_fail(c, &err);
		return;
	}
	asuka_reader r = {in, len, 0, false};
	e.p->start(e.obj, e.iface, method, &r, c);
}

// take_cancel is c's cancel, counted as running (cancelling) under the
// lock it's set and taken off under: once asuka_call_off_cancel has
// taken it off, none is taken, and one taken before is waited for. NULL:
// none. calls_mu is held.
static void (*take_cancel(asuka_call *c, void **ctx))(void *) {
	void (*fn)(void *) = c->on_cancel;
	*ctx = c->cancel_ctx;
	if (fn) atomic_fetch_add(&c->cancelling, 1);
	return fn;
}

// cancel_now runs a call's cancel, taken (take_cancel), unless it has
// answered (main thread). Cancelling is set before answered is read, and
// an answer sets answered before it reads cancelling: either the cancel
// doesn't run, or the answer waits for it to end.
static void cancel_now(asuka_call *c, void (*fn)(void *), void *ctx) {
	if (!atomic_load(&c->answered)) fn(ctx);
	lock(&cancel_mu);
	atomic_fetch_sub(&c->cancelling, 1);
	cond_broadcast(&cancel_cv);
	unlock(&cancel_mu);
}

// wait_cancels returns once no cancel of c runs (off the main thread,
// where one can run meanwhile).
static void wait_cancels(asuka_call *c) {
	if (!atomic_load(&c->cancelling) || asuka_desktop_on_main()) return;
	lock(&cancel_mu);
	while (atomic_load(&c->cancelling)) cond_wait(&cancel_cv, &cancel_mu);
	unlock(&cancel_mu);
}

static void n_cancel(uint64_t id) {
	asuka_call *c = NULL;
	void (*fn)(void *) = NULL;
	void *ctx = NULL;
	// Cancelled and the cancel read under the lock asuka_call_on_cancel
	// sets it under: either this sees its cancel, or it sees cancelled.
	lock(&calls_mu);
	for (asuka_call *p = calls; p; p = p->next)
		if (p->id == id) {
			c = p;
			atomic_fetch_add(&c->refs, 1);
			atomic_store(&c->cancelled, true);
			fn = take_cancel(c, &ctx);
			break;
		}
	unlock(&calls_mu);
	if (!c) return;
	if (fn) cancel_now(c, fn, ctx);
	call_unref(c);
}

bool asuka_call_cancelled(asuka_call *c) { return atomic_load(&c->cancelled); }

// run_cancel runs the cancel a call has as it runs: the one set after it
// was posted, or none if it was taken off meanwhile.
static void run_cancel(void *p) {
	asuka_call *c = p;
	void *ctx;
	lock(&calls_mu);
	void (*fn)(void *) = take_cancel(c, &ctx);
	unlock(&calls_mu);
	if (fn) cancel_now(c, fn, ctx);
	call_unref(c);
}

void asuka_call_on_cancel(asuka_call *c, void (*fn)(void *ctx), void *ctx) {
	lock(&calls_mu);
	c->cancel_ctx = ctx;
	c->on_cancel = fn;
	bool now = atomic_load(&c->cancelled);
	unlock(&calls_mu);
	if (now && fn) {
		atomic_fetch_add(&c->refs, 1);
		asuka_desktop_post(run_cancel, c);
	}
}

void asuka_call_off_cancel(asuka_call *c) {
	lock(&calls_mu);
	c->on_cancel = NULL;
	c->cancel_ctx = NULL;
	unlock(&calls_mu);
	wait_cancels(c);
}

void asuka_call_set_args(asuka_call *c, void *args, void (*free_args)(void *args)) {
	c->args = args;
	c->free_args = free_args;
}

asuka_writer asuka_call_begin(void) {
	asuka_writer w = {0};
	asuka_writer_u8(&w, ASUKA_OK);
	return w;
}

void asuka_call_done(asuka_call *c, asuka_writer *out) {
	if (atomic_exchange(&c->answered, true)) {
		fputs("asuka: a native call answered twice\n", stderr);
		asuka_writer_free(out);
		return;
	}
	// Answered from another thread while the main thread runs the call's
	// cancel: not before it ends, so the package may let the cancel's
	// context go once this returns. (On the main thread, the cancel has
	// ended, or is what answers.)
	wait_cancels(c);
	asuka_native_done(c->id, out->data, out->len);
	asuka_writer_free(out);
	if (c->free_args) c->free_args(c->args);
	c->args = NULL;
	if (call_unlink(c)) call_unref(c);
	call_unref(c);
}

void asuka_call_fail(asuka_call *c, asuka_error *e) {
	asuka_writer w = {0};
	if (!e->status) asuka_error_failed(e, "asuka", 0, "a native call failed without an error");
	answer_error(&w, e);
	asuka_call_done(c, &w);
}

// ---- Releases ----

static void n_release(uint64_t handle) {
	lock(&objects_mu);
	for (size_t i = 0; i < nobjects; i++) {
		if (objects[i].handle != handle) continue;
		if (--objects[i].given > 0) break;
		entry e = objects[i];
		objects[i] = objects[--nobjects];
		unlock(&objects_mu);
		e.p->release(e.obj, e.iface);
		return;
	}
	unlock(&objects_mu);
}

static void n_free(void *p) { free(p); }

void asuka_native_callbacks(asuka_host_callbacks *cb) {
	cb->native_new = n_new;
	cb->native_call = n_call;
	cb->native_start = n_start;
	cb->native_cancel = n_cancel;
	cb->native_release = n_release;
	cb->native_free = n_free;
}

// ---- Callbacks ----

typedef struct {
	uint64_t id;
	uint8_t *p;
	size_t len;
	// A question from another thread waits for its answer.
	asuka_writer *answer;
	bool answered;
} invoke_t;

static lock_t ask_mu = LOCK_INIT;
static cond_t ask_cv = COND_INIT;

static void invoke_now(uint64_t id, const uint8_t *p, size_t len, asuka_writer *answer) {
	uint8_t *res = NULL;
	size_t n = asuka_native_invoke(id, p, len, &res);
	if (answer) {
		answer->len = 0;
		if (res && n) put(answer, res, n);
	}
	if (res) asuka_free(res);
}

static void run_notify(void *ctx) {
	invoke_t *v = ctx;
	invoke_now(v->id, v->p, v->len, NULL);
	free(v->p);
	free(v);
}

void asuka_native_notify(uint64_t callback, asuka_writer *args) {
	if (!callback) return;
	if (asuka_desktop_on_main()) {
		invoke_now(callback, args->data, args->len, NULL);
		return;
	}
	invoke_t *v = calloc(1, sizeof *v);
	v->id = callback;
	v->len = args->len;
	v->p = malloc(args->len ? args->len : 1);
	if (args->len) memcpy(v->p, args->data, args->len);
	asuka_desktop_post(run_notify, v);
}

static void run_ask(void *ctx) {
	invoke_t *v = ctx;
	invoke_now(v->id, v->p, v->len, v->answer);
	lock(&ask_mu);
	v->answered = true;
	cond_broadcast(&ask_cv);
	unlock(&ask_mu);
}

void asuka_native_ask(uint64_t callback, asuka_writer *args, asuka_writer *answer) {
	*answer = (asuka_writer){0};
	if (!callback) return;
	if (asuka_desktop_on_main()) {
		invoke_now(callback, args->data, args->len, answer);
		return;
	}
	invoke_t v = {callback, args->data, args->len, answer, false};
	asuka_desktop_post(run_ask, &v);
	lock(&ask_mu);
	while (!v.answered) cond_wait(&ask_cv, &ask_mu);
	unlock(&ask_mu);
}

// ---- Views ----

typedef struct {
	void *widget;
	void *obj;
	const asuka_native_provider *p;
	uint32_t index;
} shown;

static shown *views;
static size_t nviews, capviews;

static shown *view_of(void *widget) {
	for (size_t i = 0; i < nviews; i++)
		if (views[i].widget == widget) return &views[i];
	return NULL;
}

void *asuka_native_view(const char *kind, uint32_t view, void *parent) {
	const char *hash = strrchr(kind, '#');
	if (!hash) return NULL;
	const asuka_native_provider *p = provider_of(kind, (size_t)(hash - kind));
	if (!p || !p->views) return NULL;
	void *widget = NULL;
	uint32_t index = 0;
	void *obj = p->views->make(hash + 1, view, parent, &widget, &index);
	if (!obj) return NULL;
	if (!widget) {
		p->views->destroy(obj, index);
		return NULL;
	}
	if (nviews == capviews) {
		capviews = capviews ? capviews * 2 : 16;
		views = xrealloc(views, capviews * sizeof *views);
	}
	views[nviews++] = (shown){widget, obj, p, index};
	return widget;
}

void asuka_native_view_set(const char *kind, void *widget, const char *prop, const uint8_t *value, size_t len) {
	(void)kind;
	shown *s = view_of(widget);
	if (!s) return;
	asuka_reader r = {value, len, 0, false};
	s->p->views->set(s->obj, s->index, prop, &r);
}

void asuka_native_view_prop(void *widget, const uint8_t *op, size_t len) {
	asuka_reader r = {op, len, 0, false};
	char *name = asuka_reader_string(&r);
	uint8_t type = asuka_reader_u8(&r);
	uint32_t n = asuka_reader_u32(&r);
	if (!r.failed && type == 5 && n <= r.len - r.off) asuka_native_view_set(NULL, widget, name, r.p + r.off, n);
	free(name);
}

bool asuka_native_view_measure(void *widget, float width, float height, float *out_width, float *out_height) {
	shown *s = view_of(widget);
	if (!s || !s->p->views->measure) return false;
	return s->p->views->measure(s->obj, s->index, width, height, out_width, out_height);
}

void asuka_native_view_destroy(const char *kind, void *widget) {
	(void)kind;
	shown *s = view_of(widget);
	if (!s) return;
	shown v = *s;
	*s = views[--nviews];
	v.p->views->destroy(v.obj, v.index);
}

// The Native event (ops/schema.go): u32 view, u16 kind, u32 the payload's
// length, then the payload: string name, u32 count (1), string "v", u8
// type (5: bytes), u32 length, the values.
enum { EVENT_NATIVE = 23 };

typedef struct {
	uint32_t view;
	char *event;
	asuka_bytes values;
} emitted;

static void emit_later(void *p) {
	emitted *e = p;
	asuka_native_emit(e->view, e->event, e->values.data, e->values.len);
	free(e->event);
	free(e->values.data);
	free(e);
}

void asuka_native_emit(uint32_t view, const char *event, const uint8_t *values, size_t len) {
	if (!asuka_desktop_on_main()) {
		emitted *e = malloc(sizeof *e);
		*e = (emitted){view, asuka_strdup(event), asuka_bytes_copy(values, len)};
		asuka_desktop_post(emit_later, e);
		return;
	}
	asuka_writer payload = {0};
	asuka_writer_string(&payload, event);
	asuka_writer_u32(&payload, 1);
	asuka_writer_string(&payload, "v");
	asuka_writer_u8(&payload, 5);
	asuka_writer_bytes(&payload, values, len);
	asuka_writer w = {0};
	asuka_writer_u32(&w, view);
	asuka_writer_u8(&w, EVENT_NATIVE & 0xff);
	asuka_writer_u8(&w, EVENT_NATIVE >> 8);
	asuka_writer_bytes(&w, payload.data, payload.len);
	asuka_event(w.data, w.len);
	asuka_writer_free(&payload);
	asuka_writer_free(&w);
}
