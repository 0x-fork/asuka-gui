// asuka-run: runs a compiled module (lang/bytecode) on the VM. With
// -test, it runs the module's test blocks instead of its app, with time
// that only moves when nothing else can run (a sleep in a test takes no
// time), and reports each as "ok name" or "FAIL name: message". A test
// mounts views on the fake host (test.mount: core/host/fake/uitest.h),
// and has an empty directory of its own as the app's (std/fs's
// appDir), removed after it. With -leaks, a test also fails when objects
// it made outlive it (its owner disposed, everything it started
// settled): what it made and didn't free, such as a reference cycle.
//
//	asuka-run [-leaks] [-test [pattern]] module.akb
#include "vm/vm.h"
#include "rt/sched.h"
#include "signal/signal.h"
#include "ui/ui.h"
#include "fake/uitest.h"
#include "std/std.h"
#include "abi/program.h"

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
static void show_app(ak_owner *o, const ak_func *f);

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
		if (ak_uitest_pump()) continue;  // the fake host's answers
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
	bool test = false, aot = false, app = false, leaks = false, program = false;
	const char *pattern = NULL, *path = NULL, *name = "main";
	int rest = argc;  // the program's arguments, after --
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) {
			rest = i + 1;
			break;
		} else if (strcmp(argv[i], "-main") == 0) {
			program = true;
		} else if (strcmp(argv[i], "-program") == 0 && i + 1 < argc) {
			name = argv[++i];
		} else if (strcmp(argv[i], "-trim") == 0 && i + 1 < argc) {
			ak_program_trim(argv[++i]);
		} else if (strcmp(argv[i], "-aot") == 0) {
			aot = true;
		} else if (strcmp(argv[i], "-app") == 0) {
			app = true;
		} else if (strcmp(argv[i], "-leaks") == 0) {
			leaks = true;
		} else if (strcmp(argv[i], "-platform") == 0 && i + 1 < argc) {
			static const char *names[] = {"", "ios", "android", "macos", "windows", "web", "linux"};
			const char *want = argv[++i];
			uint8_t p = 0;
			for (uint8_t k = 1; k < sizeof names / sizeof *names; k++)
				if (strcmp(names[k], want) == 0) p = k;
			if (!p) {
				fprintf(stderr, "asuka-run: -platform %s: one of ios, android, macos, windows, web, linux\n", want);
				return 2;
			}
			ak_uitest_set_platform(p);
		} else if (strcmp(argv[i], "-test") == 0) {
			test = true;
			if (i + 2 < argc && argv[i + 1][0] != '-') pattern = argv[++i];
		} else {
			path = argv[i];
		}
	}
	if (!path && !aot) {
		fprintf(stderr, "usage: asuka-run [-leaks] [-test [pattern]] [-app] [-platform name] (module.akb | -aot)\n"
		                "       asuka-run -main [-program name] (module.akb | -aot) [-- args...]\n");
		return 2;
	}
	size_t n = 0;
	uint8_t *p = NULL;
	if (!aot) {
		p = read_file(path, &n);
		if (!p) {
			fprintf(stderr, "asuka-run: can't read %s\n", path);
			return 2;
		}
	} else if (!ak_app_loader) {
		fprintf(stderr, "asuka-run: no app compiled in (-aot)\n");
		return 2;
	}
	ak_signal_bind_thread();
	ak_vm_init();
	// The UI runtime, and tests' mount, tap, expect... on the fake host.
	ak_ui_builtins_init();
	ak_uitest_builtins_init();
	ak_signal_set_error_handler(on_error, NULL);
	if (test || app) ak_sched_manual_clock(true);
	char err[512] = "";
	ak_module *m = aot ? ak_app_loader() : ak_module_load(p, n, err, sizeof err);
	ak_free(p);
	if (!m) {
		fprintf(stderr, "asuka-run: %s\n", err);
		return 2;
	}
	if (program) {
		// argv[0] the program's name, then what came after --.
		int pargc = 1 + (argc - rest);
		char **pargv = ak_alloc(sizeof(char *) * (size_t)(pargc + 1));
		pargv[0] = (char *)name;
		for (int i = rest; i < argc; i++) pargv[1 + i - rest] = argv[i];
		int code = ak_program_start(m, pargc, pargv, NULL);
		if (code < 0) code = ak_program_loop();
		ak_free(pargv);
		return code;
	}
	// Tests' processes: exit ends a test, not the runner.
	if (test) ak_os_testing(true);
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
		// mountApp mounts the app block's view.
		int32_t ai = ak_module_app(m);
		if (ai >= 0) {
			ak_closure *build = ak_vm_closure(ak_module_func(m, (uint32_t)ai));
			ak_uitest_set_app(build);
			ak_release(build);
		}
		uint32_t nt = ak_module_ntests(m);
		for (uint32_t i = 0; i < nt; i++) {
			uint32_t fi;
			const char *name = ak_module_test(m, i, &fi);
			if (pattern && !strstr(name, pattern)) continue;
			current = name;
			failure[0] = 0;
			char *dir = ak_fs_temp_dir();
			ak_fs_set_app_dir(dir);
			const ak_func *f = ak_module_func(m, fi);
			int64_t live = ak_obj_live;
			ak_owner *o = ak_owner_new();
			run_t r = {f, NULL};
			ak_with_owner(o, run_test, &r);
			ak_future *fut = r.fut;
			settle(fut);
			if (fut && fut->state == AK_FUT_FAILED && !failure[0]) snprintf(failure, sizeof failure, "%s", ak_error_message(fut->error)->data);
			else if (fut && fut->state == AK_FUT_PENDING && !failure[0]) snprintf(failure, sizeof failure, "the test never finished (it awaits something that never completes)");
			int64_t code;
			if (ak_os_test_exit(&code)) {
				failure[0] = 0;
				snprintf(failure, sizeof failure, "it called os.exit(%lld), which ends a program: a test runs main with os.fake().run(main) to see its code", (long long)code);
			}
			ak_release(fut);
			ak_owner_dispose(o);
			settle(NULL);
			ak_os_reset();
			if (dir) {
				ak_fs_remove_tree(dir);
				ak_free(dir);
			}
			ak_fs_set_app_dir(NULL);
			if (leaks && !failure[0] && ak_obj_live > live)
				snprintf(failure, sizeof failure, "%lld objects it made outlive it", (long long)(ak_obj_live - live));
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
	if (app) {
		int32_t ai = ak_module_app(m);
		if (ai < 0) {
			fprintf(stderr, "asuka-run: the module has no app\n");
			failures++;
		} else {
			ak_owner *o = ak_owner_new();
			show_app(o, ak_module_func(m, (uint32_t)ai));
			ak_owner_dispose(o);
			settle(NULL);
		}
	}
	ak_uitest_set_app(NULL);
	ak_owner_dispose(root);
	ak_ui_shutdown();
	if (!aot) ak_module_release(m);
	return failures ? 1 : 0;
}

typedef struct {
	const ak_func *f;
	ak_uitest *a;
} show_t;

static void mount_app(void *ctx) {
	show_t *s = ctx;
	ak_closure *build = ak_vm_closure(s->f);
	s->a = ak_uitest_mount(build, NULL);
	ak_release(build);
}

// show_app mounts the app's root view on the fake host, owned by o, lets
// it settle and prints what the screen shows.
static void show_app(ak_owner *o, const ak_func *f) {
	show_t s = {f, NULL};
	ak_with_owner(o, mount_app, &s);
	settle(NULL);
	ak_uitest_settle(s.a);
	ak_buf out = {0};
	ak_uitest_snapshot(s.a, &out);
	fwrite(out.data, 1, out.len, stdout);
	fflush(stdout);
	ak_buf_free(&out);
	ak_uitest_unmount(s.a, NULL);
	ak_release(s.a);
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
