#define _POSIX_C_SOURCE 200809L  // setenv under -std=c17
// abi.c: the C ABI of core/include/asuka.h: what hosts call.
//
// asuka_start takes the host's vtable: apply and measure become the UI
// runtime's host (ak_ui_init), post the scheduler's wake (any thread asks
// the host to call asuka_run_posted soon). Events, list calls and posted
// work then come in on the main thread, each flushed before it returns.
//
// Native packages go through the vtable's native_* (core/src/native);
// asuka dev's player connects with asuka_dev_connect (core/src/abi/dev.c),
// and a reload keeps state cells by their instance paths (vm/keep.c).
#include "asuka.h"
#include "ui/ui.h"
#include "vm/vm.h"
#include "rt/sched.h"
#include "signal/signal.h"
#include "native/native.h"
#include "vm/keep.h"
#include "abi/dev.h"
#include "base/thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static asuka_host_callbacks host;
static void dispatch_early(void);
static bool started;
static ak_module *module;
static ak_owner *module_owner;  // what the module's init made (its state)

static void host_apply(void *ctx, const uint8_t *buf, size_t len) {
	AK_UNUSED(ctx);
	host.apply(buf, len);
}

static void host_measure(void *ctx, uint32_t id, float w, float h, float aw, float ah, float *ow, float *oh) {
	AK_UNUSED(ctx);
	if (host.measure_v) {
		float in[4] = {w, h, aw, ah}, out[2] = {0, 0};
		host.measure_v(id, in, out);
		*ow = out[0];
		*oh = out[1];
		return;
	}
	host.measure(id, w, h, aw, ah, ow, oh);
}

static void wake(void) {
	if (host.post) host.post();
}

static void on_error(void *ctx, void *err) {
	AK_UNUSED(ctx);
	// An error nothing caught (no Boundary around it): the app's default,
	// logged. In development the player shows it too (the dev loop's).
	fprintf(stderr, "asuka: %s\n", ak_error_message(err)->data);
}

void asuka_start(asuka_host_callbacks *callbacks) {
	host = *callbacks;
	if (started) return;
	started = true;
	ak_signal_bind_thread();
	ak_vm_init();
	ak_ui_builtins_init();
	ak_signal_set_error_handler(on_error, NULL);
	ak_sched_set_wake(wake);
	ak_native_set_host(&host);
	ak_ui_init(&(ak_host){NULL, host_apply, host_measure});
	dispatch_early();
}

static char *dup_string(const char *s) {
	size_t n = strlen(s);
	char *p = malloc(n + 1);
	if (p) memcpy(p, s, n + 1);
	return p;
}

static void fail(char **err, const char *msg) {
	if (err) *err = dup_string(msg);
}

static void run_init(void *ctx) {
	ak_val v = ak_vm_call(ctx, NULL, 0);
	AK_UNUSED(v);
}

static bool keeping;  // the dev loop keeps state across loads

static const ak_shape *shape_named(void *ctx, const char *name) { return ak_module_shape_named(ctx, name); }

// run_module makes m (owned) the running app: the old one goes (its
// state saved first when the dev loop keeps it), m's init runs and its
// app mounts.
static int run_module(ak_module *m, char **err) {
	char msg[512];
	int32_t app = ak_module_app(m);
	if (app < 0) {
		ak_module_release(m);
		fail(err, "asuka: the module has no app");
		return -1;
	}
	// The old app goes first: its state (saved, in development), its views.
	if (keeping) ak_keep_save();
	ak_ui_unmount();
	if (module_owner) ak_owner_dispose(module_owner);
	module_owner = NULL;
	if (module) ak_module_release(module);
	module = m;
	if (keeping) ak_keep_use(shape_named, m);
	if (ak_module_nfuncs(m)) {
		ak_owner *o = module_owner = ak_root_new();
		ak_with_owner(o, run_init, (void *)ak_module_func(m, ak_module_init(m)));
		if (ak_error_pending()) {
			void *e = ak_error_take();
			snprintf(msg, sizeof msg, "asuka: init: %s", ak_error_message(e)->data);
			ak_release(e);
			fail(err, msg);
			return -1;
		}
	}
	ak_closure *build = ak_vm_closure(ak_module_func(m, (uint32_t)app));
	ak_ui_mount(build);
	ak_release(build);
	return 0;
}

int asuka_load(const uint8_t *p, size_t n, char **err) {
	if (err) *err = NULL;
	if (!started) {
		fail(err, "asuka: asuka_load before asuka_start");
		return -1;
	}
	char msg[512];
	ak_module *m = ak_module_load(p, n, msg, sizeof msg);
	if (!m) {
		fail(err, msg);
		return -1;
	}
	return run_module(m, err);
}

int asuka_run(char **err) {
	if (err) *err = NULL;
	if (!started) {
		fail(err, "asuka: asuka_run before asuka_start");
		return -1;
	}
	if (!ak_app_loader) {
		fail(err, "asuka: no app is compiled into this build (a player loads one with asuka_dev_connect)");
		return -1;
	}
	ak_module *m = ak_app_loader();
	ak_module_retain(m);  // the build's own stays with it
	return run_module(m, err);
}

// ---- asuka dev ----

static void reload(const uint8_t *p, size_t n) {
	int64_t t0 = ak_now_ns();
	char *err = NULL;
	if (asuka_load(p, n, &err) != 0) {
		ak_dev_send(AK_DEV_ERROR, err, strlen(err));
		fprintf(stderr, "%s\n", err);
		free(err);
		return;
	}
	uint32_t kept = ak_keep_restored(), us = (uint32_t)((ak_now_ns() - t0) / 1000);
	uint8_t b[8] = {(uint8_t)kept, (uint8_t)(kept >> 8), (uint8_t)(kept >> 16), (uint8_t)(kept >> 24), (uint8_t)us, (uint8_t)(us >> 8), (uint8_t)(us >> 16), (uint8_t)(us >> 24)};
	ak_dev_send(AK_DEV_RELOADED, b, sizeof b);
}

static void keep_states(void) {
	if (keeping) return;
	keeping = true;
	ak_keep_start();
	ak_keep_qualify = ak_ui_path_key;
}

int asuka_dev_reload(const uint8_t *p, size_t n, uint32_t *micros, char **err) {
	if (err) *err = NULL;
	keep_states();
	int64_t t0 = ak_now_ns();
	if (asuka_load(p, n, err) != 0) return -1;
	if (micros) *micros = (uint32_t)((ak_now_ns() - t0) / 1000);
	return (int)ak_keep_restored();
}

int asuka_dev_connect(const char *host_name, int port, const char *hello, char **err) {
	if (err) *err = NULL;
	if (!started) {
		fail(err, "asuka: asuka_dev_connect before asuka_start");
		return -1;
	}
	keep_states();
	ak_dev_set_reload(reload);
	char msg[256];
	if (!ak_dev_connect(host_name, port, hello, true, msg, sizeof msg)) {
		fail(err, msg);
		return -1;
	}
	return 0;
}

void asuka_free_string(char *s) { free(s); }

void *asuka_alloc(size_t n) { return malloc(n ? n : 1); }
void asuka_free(void *p) { free(p); }

// asuka_attach tells the host again which view is the root, and resends
// every frame (an Android activity recreated took over the views).
void asuka_attach(void) {
	if (started) ak_ui_reattach();
}

// Events a host sends before it starts the core (Android reports its
// environment, the screen's size and insets, first) wait for it.
static ak_buf early;

void asuka_event(const uint8_t *buf, size_t len) {
	if (started) {
		ak_ui_dispatch(buf, len);
		return;
	}
	ak_buf_u32(&early, (uint32_t)len);
	ak_buf_put(&early, buf, len);
}

static void dispatch_early(void) {
	ak_reader r = {early.data, early.len, 0, false};
	while (r.off < r.len) {
		uint32_t n;
		const uint8_t *p = ak_read_bytes(&r, &n);
		if (r.err) break;
		ak_ui_dispatch(p, n);
	}
	ak_buf_free(&early);
}

int64_t asuka_list_count(uint32_t list) { return started ? ak_ui_list_count(list) : 0; }

void asuka_list_bind(uint32_t list, int64_t index, uint32_t cell) {
	if (started) ak_ui_list_bind(list, index, cell);
}

void asuka_list_unbind(uint32_t list, uint32_t cell) {
	if (started) ak_ui_list_unbind(list, cell);
}

void asuka_run_posted(void) {
	if (started) ak_ui_run_posted();
}

void asuka_setenv(const char *key, const char *value) {
#if defined(_WIN32)
	_putenv_s(key, value);
#else
	setenv(key, value, 1);
#endif
}

char *asuka_crash_start(const char *info) {
	AK_UNUSED(info);
	return dup_string("");
}

void asuka_crash_notify(void) {}

void asuka_native_done(uint64_t call, const uint8_t *p, size_t len) { ak_native_done(call, p, len); }

size_t asuka_native_invoke(uint64_t id, const uint8_t *p, size_t len, uint8_t **out) { return ak_native_invoke(id, p, len, out); }
