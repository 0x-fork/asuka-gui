// ffi.c: C from Asuka's runtime (ffi.h): extern types' objects, and
// callbacks.
#include "vm/ffi.h"
#include "rt/sched.h"
#include "base/thread.h"
#include "signal/signal.h"

#include <stdatomic.h>
#include <stdio.h>

static uint64_t main_tid;
static AK_TLS uint32_t depth;  // extern calls in progress on this thread

void ak_ffi_init(void) {
	main_tid = ak_thread_id();
	ak_mark_main_thread();
}

void ak_ffi_enter(void) { depth++; }
void ak_ffi_leave(void) { depth--; }

// ---- Extern types ----

static void obj_drop(ak_opaque *o) {
	ak_ffi_obj *x = (ak_ffi_obj *)o;
	if (x->ptr && x->release) x->release(x->ptr);
	x->ptr = NULL;
}

ak_val ak_ffi_obj_new(void *ptr, void (*release)(void *), const char *type, bool opt, const char *fn) {
	if (!ptr) {
		if (!opt) ak_throw_msg("%s returned NULL, and its result is a %s (a %s? may be none)", fn, type, type);
		return (ak_val){.p = NULL};
	}
	ak_ffi_obj *o = ak_opaque_new(AK_TAG_OPAQUE, sizeof *o, obj_drop);
	o->ptr = ptr;
	o->release = release;
	o->type = type;
	return (ak_val){.p = o};
}

static ak_ffi_obj *obj_of(ak_val v) {
	ak_opaque *o = v.p;
	return o && o->h.tag == AK_TAG_OPAQUE && o->drop == obj_drop ? (ak_ffi_obj *)o : NULL;
}

void *ak_ffi_ptr(ak_val v, const char *fn) {
	if (!v.p) return NULL;
	ak_ffi_obj *o = obj_of(v);
	if (!o) ak_panic("%s: not an extern type's value", fn);
	if (!o->ptr) ak_throw_msg("%s: the %s was released", fn, o->type);
	return o->ptr;
}

void ak_ffi_released(ak_val v) {
	ak_ffi_obj *o = obj_of(v);
	if (o) o->ptr = NULL;
}

ak_val ak_ffi_str(const char *s, bool opt, const char *fn) {
	if (!s) {
		if (!opt) ak_throw_msg("%s returned NULL, and its result is a CString (a CString? may be none)", fn);
		return (ak_val){.p = NULL};
	}
	return (ak_val){.p = ak_str_c(s)};
}

ak_val ak_ffi_str_arg(const char *s, bool opt) {
	if (!s) return (ak_val){.p = opt ? NULL : ak_str_empty()};
	return (ak_val){.p = ak_str_c(s)};
}

// ---- Callbacks ----

struct ak_ffi_cb {
	atomic_uint refs;   // the extern call's, C's retains, posted calls
	ak_closure *fn;
	ak_owner *owner;    // what made it (main thread), or NULL
	uint64_t tid;       // the thread that made it
};

ak_ffi_cb *ak_ffi_cb_new(ak_val fn) {
	if (!fn.p) return NULL;
	ak_ffi_cb *cb = ak_alloc(sizeof *cb);
	atomic_init(&cb->refs, 1);
	cb->fn = ak_retain(fn.p);
	cb->tid = ak_thread_id();
	if (cb->tid == main_tid) {
		cb->owner = ak_current_owner();
		if (cb->owner) ak_owner_retain(cb->owner);
	}
	return cb;
}

static void cb_free(void *ctx) {
	ak_ffi_cb *cb = ctx;
	ak_release(cb->fn);
	if (cb->owner) ak_owner_release(cb->owner);
	ak_free(cb);
}

static void cb_unref(ak_ffi_cb *cb) {
	if (atomic_fetch_sub(&cb->refs, 1) != 1) return;
	// Its closure and owner are its thread's.
	if (ak_thread_id() == cb->tid) cb_free(cb);
	else ak_post(cb_free, cb);
}

void ak_ffi_cb_done(ak_ffi_cb *cb) {
	if (cb) cb_unref(cb);
}

void asuka_callback_retain(void *ctx) {
	ak_ffi_cb *cb = ctx;
	if (!cb) return;
	if (cb->tid != main_tid)
		ak_panic("asuka_callback_retain: a callback made off the main thread (in spawn) is valid during its call only");
	atomic_fetch_add(&cb->refs, 1);
}

void asuka_callback_release(void *ctx) {
	if (ctx) cb_unref(ctx);
}

typedef struct {
	ak_ffi_cb *cb;
	uint32_t n;
	const uint8_t *kinds;
	ak_kind rk;
	ak_val args[];
} posted;

static void release_args(ak_val *args, const uint8_t *kinds, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) ak_release_val(args[i], (ak_kind)kinds[i]);
}

static void run_posted(void *ctx) {
	posted *p = ctx;
	ak_val r = ak_call(p->cb->fn, p->args, p->n);
	if (ak_error_pending()) {
		void *err = ak_error_take();
		if (ak_is_cancel(err)) ak_release(err);
		else ak_raise(err);  // to the owner's boundary
	} else {
		ak_release_val(r, p->rk);
	}
}

static void deliver(void *ctx) {
	posted *p = ctx;
	ak_owner *o = p->cb->owner;
	if (!o) run_posted(p);
	else if (!ak_owner_disposed(o)) ak_with_owner(o, run_posted, p);
	// A callback whose owner is gone isn't called.
	release_args(p->args, p->kinds, p->n);
	cb_unref(p->cb);
	ak_free(p);
}

ak_val ak_ffi_cb_call(void *ctx, ak_val *args, const uint8_t *kinds, uint32_t n, ak_kind rk) {
	ak_ffi_cb *cb = ctx;
	uint64_t tid = ak_thread_id();
	if (depth > 0 && tid == cb->tid) {
		ak_val r = {0};
		if (!ak_error_pending()) {
			r = ak_call(cb->fn, args, n);
			if (ak_error_pending()) {
				ak_release_val(r, rk);
				r = (ak_val){0};
			}
		}
		release_args(args, kinds, n);
		return r;
	}
	if (cb->tid != main_tid) {
		fprintf(stderr, "asuka: a callback made in spawn was called after its extern call, or from another thread: it isn't called\n");
		release_args(args, kinds, n);
		return (ak_val){0};
	}
	posted *p = ak_alloc(sizeof *p + n * sizeof(ak_val));
	atomic_fetch_add(&cb->refs, 1);
	p->cb = cb;
	p->n = n;
	p->kinds = kinds;
	p->rk = rk;
	for (uint32_t i = 0; i < n; i++) p->args[i] = args[i];
	ak_post(deliver, p);
	return (ak_val){0};
}
