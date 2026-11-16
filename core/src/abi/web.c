// web.c: the core as WebAssembly, for the web host (platform/web). The
// JavaScript host is reached through imports (module "asuka"); it calls
// in through the C ABI (asuka.h), exported. Batches and measurements go
// through a small shared buffer (ak_web_io) the host reads in place, as
// the Go web host did: one crossing per batch, nothing allocated per
// measure.
//
// spawn runs on the page's Web Workers (platform/web/src/spawn.js), each
// an instance of this same module with the same app: the page's instance
// sends a job (the closure's function, by its index in the module, and
// its captures, which are values: vm/keep.c's encoding, which names its
// types), a worker's instance runs it (asuka_worker_start ran the
// module's init without a UI first) and answers its result or error in
// the same encoding, and the page settles the spawn's future.
//
// Only compiled into wasm builds.
#ifdef __wasm__

#include "asuka.h"
#include "rt/sched.h"
#include "rt/lib.h"
#include "base/thread.h"
#include "vm/vm_int.h"
#include "vm/keep.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMPORT(name) __attribute__((import_module("asuka"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name), visibility("default")))

IMPORT("apply") void js_apply(void);
IMPORT("measure") void js_measure(void);
IMPORT("post") void js_post(void);
IMPORT("timer_set") void js_timer_set(double id, double ms);
IMPORT("timer_clear") void js_timer_clear(double id);
IMPORT("zone_offset") double js_zone_offset(double unix_seconds);
IMPORT("spawn") void js_spawn(double id, const uint8_t *job, uint32_t len);
IMPORT("spawn_cancel") void js_spawn_cancel(double id);

// io: Apply's batch address and length (u32 0, 1); Measure's node (u32
// 2) and its constraints, then its answer (f32 3..6).
static union {
	uint32_t u32[8];
	float f32[8];
} io;

static void apply(const uint8_t *buf, size_t len) {
	io.u32[0] = (uint32_t)(uintptr_t)buf;
	io.u32[1] = (uint32_t)len;
	js_apply();
}

static void measure(uint32_t id, float w, float h, float aw, float ah, float *ow, float *oh) {
	io.u32[2] = id;
	io.f32[3] = w;
	io.f32[4] = h;
	io.f32[5] = aw;
	io.f32[6] = ah;
	js_measure();
	*ow = io.f32[3];
	*oh = io.f32[4];
}

static void post(void) { js_post(); }

static void timer_schedule(uint64_t id, int64_t ns) { js_timer_set((double)id, (double)ns / 1e6); }
static void timer_cancel(uint64_t id) { js_timer_clear((double)id); }

// The browser's zone, for dates (ak_zone_offset).
static int32_t zone_offset(int64_t t) { return (int32_t)js_zone_offset((double)t); }

static asuka_host_callbacks callbacks = {
	.apply = apply,
	.measure = measure,
	.post = post,
};

EXPORT("asuka_web_start") void asuka_web_start(void) {
	ak_sched_set_timer(timer_schedule, timer_cancel);
	ak_zone_offset_hook = zone_offset;
	asuka_start(&callbacks);
}

EXPORT("asuka_io") uint32_t asuka_io(void) { return (uint32_t)(uintptr_t)&io; }
EXPORT("asuka_timer_fire") void asuka_timer_fire(double id) { ak_timer_fire((uint64_t)id); }

// ---- spawn, on the page's workers ----
//
// A job: u32 the function's index, u8 the result's kind, u32 the
// captures' count, then each capture (ak_keep_encode, by the function's
// capture kinds). An answer: the result (of the job's kind; nothing for
// none), the error (a reference), or, when the worker couldn't run it,
// a message.

enum { ANSWER_OK, ANSWER_THREW, ANSWER_FAILED };

int ak_abi_init_only(ak_module *m, char **err);
ak_module *ak_abi_module(void);
void ak_vm_closure_drop(ak_closure *c);

// A shape by its name: the module's, or the runtime's own Error.
static const ak_shape *find_shape(void *ctx, const char *name) {
	const ak_shape *s = ctx ? ak_module_shape_named(ctx, name) : NULL;
	if (!s && strcmp(name, ak_shape_error.name) == 0) s = &ak_shape_error;
	return s;
}

// The page's side: the jobs out, by ID.
typedef struct remote {
	uint64_t id;
	ak_future *fut;  // retained
	ak_module *mod;  // retained: the shapes the answer is read with
	struct remote *next;
} remote;

static remote *remotes;
static uint64_t last_job;
static bool workers;  // the page runs spawn on workers (asuka_web_workers)

static remote *take_remote(uint64_t id) {
	for (remote **p = &remotes; *p; p = &(*p)->next) {
		if ((*p)->id == id) {
			remote *j = *p;
			*p = j->next;
			return j;
		}
	}
	return NULL;
}

static void drop_remote(remote *j) {
	ak_future *f = j->fut;
	f->cancel = NULL;
	f->ctx = NULL;
	ak_release(f);
	ak_module_release(j->mod);
	ak_free(j);
	ak_sched_unhold();
}

// A cancelled spawn: one not started yet never starts, one running runs
// to its end and its answer is dropped; the future fails now.
static void cancel_remote(ak_future *f) {
	remote *j = f->ctx ? take_remote(((remote *)f->ctx)->id) : NULL;
	if (!j) return;
	js_spawn_cancel((double)j->id);
	ak_throw_cancel();
	ak_future_reject(f, ak_error_take());
	drop_remote(j);
}

static bool spawn_on_worker(ak_closure *c, ak_kind k, ak_future *f) {
	if (!workers || c->invoke != ak_vm_closure_invoke) return false;
	const ak_func *fn = c->fn;
	ak_buf b = {0};
	ak_buf_u32(&b, fn->index);
	ak_buf_u8(&b, (uint8_t)k);
	ak_buf_u32(&b, c->ncap);
	for (uint16_t i = 0; i < c->ncap; i++) {
		if (!ak_keep_encode(&b, c->caps[i], c->capkinds ? (ak_kind)c->capkinds[i] : AK_K_INT)) {
			ak_buf_free(&b);
			return false;  // not a value (the checker keeps those out): run here
		}
	}
	remote *j = ak_alloc(sizeof *j);
	j->id = ++last_job;
	j->fut = ak_retain(f);
	j->mod = fn->mod;
	ak_module_retain(j->mod);
	j->next = remotes;
	remotes = j;
	f->ctx = j;
	f->cancel = cancel_remote;
	ak_sched_hold();  // until it's answered or cancelled
	js_spawn((double)j->id, b.data, (uint32_t)b.len);
	ak_buf_free(&b);
	return true;
}

// asuka_web_workers says the page has workers for spawn (0: it runs in
// place, on the page's thread).
EXPORT("asuka_web_workers") void asuka_web_workers(int on) {
	workers = on != 0;
	ak_spawn_elsewhere = workers ? spawn_on_worker : NULL;
}

// asuka_spawn_done settles job id with a worker's answer (status: an
// ANSWER_*), then runs what waited for it. A cancelled job's is dropped.
EXPORT("asuka_spawn_done") void asuka_spawn_done(double id, int status, const uint8_t *p, size_t n) {
	remote *j = take_remote((uint64_t)id);
	if (!j) return;
	ak_future *f = j->fut;
	ak_kind k = (ak_kind)f->kind;
	ak_reader r = {p, n, 0, false};
	ak_val v = {0};
	f->cancel = NULL;
	if (status == ANSWER_OK && (k == AK_K_VOID || ak_keep_decode(&r, k, find_shape, j->mod, &v))) {
		ak_future_resolve(f, k == AK_K_VOID ? (ak_val){0} : v);
	} else if (status == ANSWER_THREW && ak_keep_decode(&r, AK_K_REF, find_shape, j->mod, &v) && v.p) {
		ak_future_reject(f, v.p);
	} else {
		if (status == ANSWER_FAILED) ak_throw_msg("spawn: %.*s", (int)(n < 400 ? n : 400), (const char *)p);
		else ak_throw_msg("spawn: the worker's answer doesn't fit the app");
		ak_future_reject(f, ak_error_take());
	}
	drop_remote(j);
	ak_ui_run_posted();
}

// The worker's side: a core without a UI. What the module's init does
// for one (an accent, a language) goes nowhere.
static void worker_apply(const uint8_t *buf, size_t len) { AK_UNUSED(buf), AK_UNUSED(len); }

static void worker_measure(uint32_t id, float w, float h, float aw, float ah, float *ow, float *oh) {
	AK_UNUSED(id), AK_UNUSED(w), AK_UNUSED(h), AK_UNUSED(aw), AK_UNUSED(ah);
	*ow = 0;
	*oh = 0;
}

static asuka_host_callbacks worker_callbacks = {
	.apply = worker_apply,
	.measure = worker_measure,
	.post = post,
};

static ak_buf answer;

// answered points io at the answer (u32 0, 1) and returns status.
static int answered(int status) {
	io.u32[0] = (uint32_t)(uintptr_t)answer.data;
	io.u32[1] = (uint32_t)answer.len;
	return status;
}

static int worker_failed(const char *msg) {
	answer.len = 0;
	ak_buf_put(&answer, msg, strlen(msg));
	return answered(ANSWER_FAILED);
}

// asuka_worker_start makes this instance a worker's: the core without a
// UI, and the app's module (bytecode, or with none the app compiled into
// the build) with its init run. 0, or -1 with a message in io.
extern bool ak_in_spawn_worker;

EXPORT("asuka_worker_start") int asuka_worker_start(const uint8_t *module, size_t n) {
	ak_in_spawn_worker = true;
	ak_sched_set_timer(timer_schedule, timer_cancel);
	ak_zone_offset_hook = zone_offset;
	asuka_start(&worker_callbacks);
	ak_module *m = NULL;
	char msg[512];
	if (n) {
		m = ak_module_load(module, n, msg, sizeof msg);
		if (!m) {
			worker_failed(msg);
			return -1;
		}
	}
	char *err = NULL;
	if (ak_abi_init_only(m, &err) != 0) {
		snprintf(msg, sizeof msg, "%s", err ? err : "asuka: the module's init failed");
		free(err);
		worker_failed(msg);
		return -1;
	}
	ak_ui_run_posted();
	return 0;
}

// asuka_worker_call runs a job: its answer is in io (address, length)
// until the next call; the result an ANSWER_*.
EXPORT("asuka_worker_call") int asuka_worker_call(const uint8_t *p, size_t n) {
	answer.len = 0;
	ak_module *m = ak_abi_module();
	ak_reader r = {p, n, 0, false};
	uint32_t fi = ak_read_u32(&r);
	ak_kind k = (ak_kind)ak_read_u8(&r);
	uint32_t ncap = ak_read_u32(&r);
	if (r.err || !m || fi >= m->nfuncs || m->funcs[fi].ncap != ncap) return worker_failed("the job isn't one of this app's (a module of another version?)");
	const ak_func *fn = &m->funcs[fi];
	ak_closure *c = ak_closure_new(ak_vm_closure_invoke, fn, fn->ncap, fn->capkinds);
	ak_module_retain(fn->mod);
	c->drop = ak_vm_closure_drop;
	for (uint32_t i = 0; i < ncap; i++) {
		if (!ak_keep_decode(&r, (ak_kind)fn->capkinds[i], find_shape, m, &c->caps[i])) {
			ak_release(c);
			return worker_failed("its captures don't fit the app (a module of another version?)");
		}
	}
	ak_val v = ak_call(c, NULL, 0);
	int status = ANSWER_OK;
	if (ak_error_pending()) {
		void *e = ak_error_take();
		status = ANSWER_THREW;
		if (!ak_keep_encode(&answer, (ak_val){.p = e}, AK_K_REF)) {
			// An error that isn't a value: its message, as an Error.
			answer.len = 0;
			ak_throw_msg("%s", ak_error_message(e)->data);
			void *plain = ak_error_take();
			ak_keep_encode(&answer, (ak_val){.p = plain}, AK_K_REF);
			ak_release(plain);
		}
		ak_release(e);
	} else {
		if (k != AK_K_VOID && !ak_keep_encode(&answer, v, k)) {
			ak_release_val(v, k);
			ak_release(c);
			return worker_failed("its result isn't a value");
		}
		ak_release_val(v, k);
	}
	ak_release(c);
	ak_ui_run_posted();
	return answered(status);
}

// asuka_worker_answered lets a large answer's memory go once the page
// has it.
EXPORT("asuka_worker_answered") void asuka_worker_answered(void) {
	if (answer.cap > (1u << 22)) ak_buf_free(&answer);
}

#endif
