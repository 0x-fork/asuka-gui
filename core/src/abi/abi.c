// abi.c: the C ABI of core/include/asuka.h: what hosts call.
//
// asuka_start takes the host's vtable: apply and measure become the UI
// runtime's host (ak_ui_init), post the scheduler's wake (any thread asks
// the host to call asuka_run_posted soon). Events, list calls and posted
// work then come in on the main thread, each flushed before it returns.
//
// Native modules (the vtable's native_*, asuka_native_done/invoke) and
// the crash record aren't available in the C core yet: they land with
// native packages (milestone N1); until then the calls do nothing and
// asuka_crash_start returns "".
#include "asuka.h"
#include "ui/ui.h"
#include "vm/vm.h"
#include "rt/sched.h"
#include "signal/signal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static asuka_host_callbacks host;
static bool started;
static ak_module *module;
static ak_owner *module_owner;  // what the module's init made (its state)

static void host_apply(void *ctx, const uint8_t *buf, size_t len) {
	AK_UNUSED(ctx);
	host.apply(buf, len);
}

static void host_measure(void *ctx, uint32_t id, float w, float h, float aw, float ah, float *ow, float *oh) {
	AK_UNUSED(ctx);
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
	ak_ui_init(&(ak_host){NULL, host_apply, host_measure});
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
	int32_t app = ak_module_app(m);
	if (app < 0) {
		ak_module_release(m);
		fail(err, "asuka: the module has no app");
		return -1;
	}
	// The old app goes first: its state, its views.
	ak_ui_unmount();
	if (module_owner) ak_owner_dispose(module_owner);
	module_owner = NULL;
	if (module) ak_module_release(module);
	module = m;
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

int asuka_run(void (*app_main)(void)) {
	if (!started || !app_main) return -1;
	app_main();
	return 0;
}

void asuka_free_string(char *s) { free(s); }

// asuka_attach tells the host again which view is the root, and resends
// every frame (an Android activity recreated took over the views).
void asuka_attach(void) {
	if (started) ak_ui_reattach();
}

void asuka_event(const uint8_t *buf, size_t len) {
	if (started) ak_ui_dispatch(buf, len);
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

void asuka_native_done(uint64_t call, const uint8_t *p, size_t len) {
	AK_UNUSED(call), AK_UNUSED(p), AK_UNUSED(len);
}

size_t asuka_native_invoke(uint64_t id, const uint8_t *p, size_t len, uint8_t **out) {
	AK_UNUSED(id), AK_UNUSED(p), AK_UNUSED(len);
	if (out) *out = NULL;
	return 0;
}
