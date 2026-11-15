// asuka-run: runs a compiled module (lang/bytecode) on the VM. With
// -test, it runs the module's test blocks instead of its app, with time
// that only moves when nothing else can run (a sleep in a test takes no
// time), and reports each as "ok name" or "FAIL name: message".
//
//	asuka-run [-test [pattern]] module.akb
#include "vm/vm.h"
#include "rt/sched.h"
#include "signal/signal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *p = ak_alloc((size_t)len + 1);
	*n = fread(p, 1, (size_t)len, f);
	fclose(f);
	return p;
}

typedef struct {
	const ak_func *f;
	ak_future *fut;
} run_t;

static void run_test(void *ctx);
static void run_init(void *ctx);

static int failures;
static const char *current;
static char failure[2048];

static void on_error(void *ctx, void *err) {
	AK_UNUSED(ctx);
	if (current && !failure[0]) snprintf(failure, sizeof failure, "%s", ak_error_message(err)->data);
	else if (!current) fprintf(stderr, "asuka: uncaught error: %s\n", ak_error_message(err)->data);
}

// settle runs ready work, then moves the clock to the next timer, until
// nothing is left or fut is settled.
static void settle(ak_future *fut) {
	for (int guard = 0; guard < 1000000; guard++) {
		ak_sched_run_posted();
		if (ak_sched_run()) continue;
		if (fut && fut->state != AK_FUT_PENDING) return;
		if (ak_sched_held()) {
			// Another thread works for us: wait for what it posts.
			ak_sched_wait_posted();
			continue;
		}
		int64_t at;
		if (!ak_sched_next_timer(&at)) return;
		ak_sched_advance(at - ak_sched_now());
	}
}

int main(int argc, char **argv) {
	bool test = false;
	const char *pattern = NULL, *path = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-test") == 0) {
			test = true;
			if (i + 2 < argc) pattern = argv[++i];
		} else {
			path = argv[i];
		}
	}
	if (!path) {
		fprintf(stderr, "usage: asuka-run [-test [pattern]] module.akb\n");
		return 2;
	}
	size_t n;
	uint8_t *p = read_file(path, &n);
	if (!p) {
		fprintf(stderr, "asuka-run: can't read %s\n", path);
		return 2;
	}
	ak_signal_bind_thread();
	ak_vm_init();
	ak_signal_set_error_handler(on_error, NULL);
	if (test) ak_sched_manual_clock(true);
	char err[512];
	ak_module *m = ak_module_load(p, n, err, sizeof err);
	ak_free(p);
	if (!m) {
		fprintf(stderr, "asuka-run: %s\n", err);
		return 2;
	}
	ak_owner *root = ak_root_new();
	const ak_func *init = ak_module_nfuncs(m) ? ak_module_func(m, ak_module_init(m)) : NULL;
	if (init) {
		ak_with_owner(root, run_init, (void *)init);
		if (ak_error_pending()) {
			void *e = ak_error_take();
			fprintf(stderr, "asuka-run: init: %s\n", ak_error_message(e)->data);
			ak_release(e);
			return 1;
		}
	}
	if (test) {
		uint32_t nt = ak_module_ntests(m);
		for (uint32_t i = 0; i < nt; i++) {
			uint32_t fi;
			const char *name = ak_module_test(m, i, &fi);
			if (pattern && !strstr(name, pattern)) continue;
			current = name;
			failure[0] = 0;
			const ak_func *f = ak_module_func(m, fi);
			ak_owner *o = ak_owner_new();
			run_t r = {f, NULL};
			ak_with_owner(o, run_test, &r);
			ak_future *fut = r.fut;
			settle(fut);
			if (fut && fut->state == AK_FUT_FAILED && !failure[0]) snprintf(failure, sizeof failure, "%s", ak_error_message(fut->error)->data);
			else if (fut && fut->state == AK_FUT_PENDING && !failure[0]) snprintf(failure, sizeof failure, "the test never finished (it awaits something that never completes)");
			ak_release(fut);
			ak_owner_dispose(o);
			settle(NULL);
			if (failure[0]) {
				failures++;
				printf("FAIL %s: %s\n", name, failure);
			} else {
				printf("ok %s\n", name);
			}
			fflush(stdout);
		}
		current = NULL;
	}
	ak_owner_dispose(root);
	ak_module_release(m);
	return failures ? 1 : 0;
}

// run_test starts a test: an async function, so a task whose future it
// keeps; a synchronous one finishes at once.
static void run_init(void *ctx) {
	ak_val v = ak_vm_call(ctx, NULL, 0);
	AK_UNUSED(v);
}

static void run_test(void *ctx) {
	run_t *r = ctx;
	if (ak_func_async(r->f)) {
		ak_closure *c = ak_vm_closure(r->f);
		ak_val v = ak_call(c, NULL, 0);
		ak_release(c);
		r->fut = v.p;
		return;
	}
	ak_val v = ak_vm_call(r->f, NULL, 0);
	AK_UNUSED(v);
	if (ak_error_pending()) {
		void *e = ak_error_take();
		if (!failure[0]) snprintf(failure, sizeof failure, "%s", ak_error_message(e)->data);
		ak_release(e);
	}
}
