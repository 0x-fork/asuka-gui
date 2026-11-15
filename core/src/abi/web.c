// web.c: the core as WebAssembly, for the web host (platform/web). The
// JavaScript host is reached through imports (module "asuka"); it calls
// in through the C ABI (asuka.h), exported. Batches and measurements go
// through a small shared buffer (ak_web_io) the host reads in place, as
// the Go web host did: one crossing per batch, nothing allocated per
// measure.
//
// Only compiled into wasm builds.
#ifdef __wasm__

#include "asuka.h"
#include "rt/sched.h"

#include <stdlib.h>

#define IMPORT(name) __attribute__((import_module("asuka"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name), visibility("default")))

IMPORT("apply") void js_apply(void);
IMPORT("measure") void js_measure(void);
IMPORT("post") void js_post(void);
IMPORT("timer_set") void js_timer_set(double id, double ms);
IMPORT("timer_clear") void js_timer_clear(double id);

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

static asuka_host_callbacks callbacks = {
	.apply = apply,
	.measure = measure,
	.post = post,
};

EXPORT("asuka_web_start") void asuka_web_start(void) {
	ak_sched_set_timer(timer_schedule, timer_cancel);
	asuka_start(&callbacks);
}

EXPORT("asuka_io") uint32_t asuka_io(void) { return (uint32_t)(uintptr_t)&io; }
EXPORT("asuka_alloc") void *asuka_alloc(size_t n) { return malloc(n ? n : 1); }
EXPORT("asuka_free") void asuka_free(void *p) { free(p); }
EXPORT("asuka_timer_fire") void asuka_timer_fire(double id) { ak_timer_fire((uint64_t)id); }

#endif
