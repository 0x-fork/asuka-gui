// program.c: a program (docs/programs.md): a module whose fn main is its
// entry, run with no UI host. Its init runs, then main, as a task when
// it's async; the program ends when main does: its other tasks are
// cancelled (their defers run), and the exit code is 0, 1 when main threw
// (the error and where it was thrown written to stderr), or what os.exit
// said at once.
//
// The main thread's loop is the core's own (ak_program_loop: what's
// posted and made ready runs; it waits for more), or a host's (macOS's
// CFRunLoop: callbacks->post asks for asuka_run_posted, and the process
// exits as main ends).
#include "asuka.h"
#include "abi/program.h"
#include "ui/ui.h"
#include "vm/vm.h"
#include "rt/sched.h"
#include "signal/signal.h"
#include "native/native.h"
#include "base/thread.h"
#include "std/std.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static struct {
	ak_mutex mu;
	ak_cond cond;
	bool woken;
	bool own_loop;  // the core's loop runs: the end returns from it, not exit()
	void (*host_post)(void);
	bool ended;
	int code;
	ak_owner *owner;  // init's and main's
	ak_future *main;
	// The last error thrown, and the VM's stack as it was: what an error
	// main throws is reported with.
	void *traced;
	char trace[4096];
	char ctrace[4096];  // compiled functions' frames, innermost first
	uint64_t thread;  // the main thread's
	const char *trim;  // what the trace's files are relative to
	asuka_host_callbacks host;  // the host's, kept
} P = {.mu = AK_MUTEX_INIT};

static void wake(void) {
	ak_mutex_lock(&P.mu);
	P.woken = true;
	ak_cond_signal(&P.cond);
	ak_mutex_unlock(&P.mu);
	if (P.host_post) P.host_post();
}

static void on_throw(void *err) {
	if (err == P.traced || ak_thread_id() != P.thread || ak_is_cancel(err)) return;
	if (P.traced) ak_release(P.traced);
	P.traced = ak_retain(err);
	ak_vm_traceback(P.trace, sizeof P.trace);
	P.ctrace[0] = 0;
}

static void on_frame(const char *fn, const char *file, uint32_t line) {
	if (ak_thread_id() != P.thread || !P.traced || !ak_error_pending()) return;
	size_t n = strlen(P.ctrace);
	if (n + 2 < sizeof P.ctrace) snprintf(P.ctrace + n, sizeof P.ctrace - n, "%s (%s:%u)\n", fn, file, line);
}

// print_frames writes a trace's lines, indented, its files relative to
// P.trim.
static void print_frames(const char *t) {
	for (const char *l = t; *l;) {
		const char *e = strchr(l, '\n');
		size_t n = e ? (size_t)(e - l) : strlen(l);
		// "name (file:line)": the file relative to the project's.
		const char *open = memchr(l, '(', n);
		size_t k = P.trim ? strlen(P.trim) : 0;
		if (open && k && (size_t)(l + n - open) > k + 1 && strncmp(open + 1, P.trim, k) == 0 && (open[1 + k] == '/' || open[1 + k] == '\\')) {
			fprintf(stderr, "    %.*s(%.*s\n", (int)(open - l), l, (int)(l + n - open - k - 2), open + k + 2);
		} else {
			fprintf(stderr, "    %.*s\n", (int)n, l);
		}
		l += n + (e ? 1 : 0);
	}
}

static void report(void *err) {
	fflush(stdout);
	ak_str *msg = ak_vm_error_text(err);
	fprintf(stderr, "error: %s\n", msg->data);
	ak_release(msg);
	if (err == P.traced) {
		print_frames(P.ctrace);
		print_frames(P.trace);
	}
	fflush(stderr);
}

static void on_error(void *ctx, void *err) {
	AK_UNUSED(ctx);
	// A task's error nothing caught (not main's: its own ends the program).
	report(err);
}

// end ends the program with code: what it left running is cancelled.
static void end(int code) {
	if (P.ended) return;
	P.ended = true;
	P.code = code;
	if (P.owner) ak_owner_dispose(P.owner);
	P.owner = NULL;
	for (int i = 0; i < 1000 && ak_sched_run(); i++) {
	}
	fflush(stdout);
	fflush(stderr);
	if (!P.own_loop) exit(code);
	wake();
}

static void main_done(void *ctx) {
	AK_UNUSED(ctx);
	ak_future *f = P.main;
	P.main = NULL;
	int code = 0;
	if (f->state == AK_FUT_FAILED && !ak_is_cancel(f->error)) {
		report(f->error);
		code = 1;
	}
	ak_release(f);
	end(code);
}

typedef struct {
	const ak_func *init, *main;
	bool failed;
} start_t;

static void start(void *ctx) {
	start_t *s = ctx;
	if (s->init) {
		ak_val v = ak_vm_call(s->init, NULL, 0);
		AK_UNUSED(v);
		if (ak_error_pending()) {
			void *e = ak_error_take();
			report(e);
			ak_release(e);
			s->failed = true;
			return;
		}
	}
	if (ak_func_async(s->main)) {
		ak_closure *c = ak_vm_closure(s->main);
		ak_val v = ak_call(c, NULL, 0);
		ak_release(c);
		P.main = v.p;
		return;
	}
	ak_val v = ak_vm_call(s->main, NULL, 0);
	AK_UNUSED(v);
	if (ak_error_pending()) {
		void *e = ak_error_take();
		if (!ak_is_cancel(e)) {
			report(e);
			s->failed = true;
		}
		ak_release(e);
	}
}

void ak_program_trim(const char *dir) { P.trim = dir; }

const char *ak_program_data;

// data_dir makes the program's data the folder ak_program_data names (an
// app's, which it ships with) in the platform's place for an app's, as
// the app's host does: macOS's Application Support, Windows' Local
// AppData, Linux's XDG data directory. ASUKA_DATA, when set, wins.
static void data_dir(void) {
	if (!ak_program_data || !*ak_program_data) return;
	const char *set = getenv("ASUKA_DATA");
	if (set && *set) return;
	char buf[4096];
	buf[0] = 0;
#if defined(_WIN32)
	const char *base = getenv("LOCALAPPDATA");
	if (!base || !*base) base = getenv("APPDATA");
	if (base && *base) snprintf(buf, sizeof buf, "%s\\%s", base, ak_program_data);
#elif defined(__APPLE__)
	const char *home = getenv("HOME");
	if (home && *home) snprintf(buf, sizeof buf, "%s/Library/Application Support/%s", home, ak_program_data);
#else
	const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (xdg && xdg[0] == '/') snprintf(buf, sizeof buf, "%s/%s", xdg, ak_program_data);
	else if (home && *home) snprintf(buf, sizeof buf, "%s/.local/share/%s", home, ak_program_data);
#endif
	if (!buf[0]) return;
	asuka_setenv("ASUKA_DATA", buf);  // the host's directories follow it
	ak_fs_set_app_dir(buf);
}

int ak_program_start(ak_module *m, int argc, char **argv, const asuka_host_callbacks *cb) {
	ak_cond_init(&P.cond);
	P.thread = ak_thread_id();
	P.host_post = cb ? cb->post : NULL;
	if (argv) ak_os_set_args(argc, argv);  // else the system's (Windows: UTF-16)
#if defined(_WIN32)
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif
	ak_signal_set_error_handler(on_error, NULL);
	ak_sched_set_wake(wake);
	if (cb) {
		P.host = *cb;
		ak_native_set_host(&P.host);
	}
	ak_throw_hook = on_throw;
	ak_frame_hook = on_frame;
	data_dir();
	// The program's own directory (std/fs's appDir) is there, as an
	// app's host makes the app's.
	char *dir = ak_fs_app_dir();
	if (dir) {
		ak_fs_make_dirs(dir);
		ak_free(dir);
	}
	int32_t mi = ak_module_main(m);
	if (mi < 0) {
		fprintf(stderr, "asuka: the module has no fn main: it isn't a program\n");
		return 2;
	}
	start_t s = {ak_module_nfuncs(m) ? ak_module_func(m, ak_module_init(m)) : NULL, ak_module_func(m, (uint32_t)mi), false};
	P.owner = ak_root_new();
	ak_with_owner(P.owner, start, &s);
	if (s.failed) {
		end(1);
	} else if (!P.main) {
		end(0);
	} else {
		ak_future_wait(P.main, main_done, NULL);
	}
	return P.ended ? P.code : -1;
}

int ak_program_loop(void) {
	while (!P.ended) {
		ak_sched_run_posted();
		while (ak_sched_run()) {
		}
		if (P.ended) break;
		ak_mutex_lock(&P.mu);
		while (!P.woken) ak_cond_wait(&P.cond, &P.mu);
		P.woken = false;
		ak_mutex_unlock(&P.mu);
	}
	if (P.traced) ak_release(P.traced);
	P.traced = NULL;
	ak_throw_hook = NULL;
	ak_frame_hook = NULL;
	return P.code;
}

// ---- The ABI ----

#if defined(AK_APP_COMPILED)
ak_module *asuka_app_module(void);
#endif

static ak_module *compiled(void) {
#if defined(AK_APP_COMPILED)
	return ak_app_loader ? ak_app_loader() : asuka_app_module();
#else
	return ak_app_loader ? ak_app_loader() : NULL;
#endif
}

static void init_core(void) {
	ak_signal_bind_thread();
	ak_vm_init();
	ak_ui_builtins_init();
}

int asuka_program_main(int argc, char **argv, asuka_host_callbacks *callbacks) {
	init_core();
	ak_module *m = compiled();
	if (!m) {
		fprintf(stderr, "asuka: no program is compiled into this build\n");
		return 2;
	}
	P.own_loop = true;
	int code = ak_program_start(m, argc, argv, callbacks);
	if (code >= 0) return code;
	return ak_program_loop();
}

int asuka_program_start(int argc, char **argv, asuka_host_callbacks *callbacks) {
	init_core();
	ak_module *m = compiled();
	if (!m) {
		fprintf(stderr, "asuka: no program is compiled into this build\n");
		return 2;
	}
	P.own_loop = false;
	return ak_program_start(m, argc, argv, callbacks);
}
