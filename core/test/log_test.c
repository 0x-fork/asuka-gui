// std/log's builtins (core/src/std/log.c), called as the VM calls them:
// a level that's off costs no allocation; lines as text and JSON, their
// fields merged and quoted; ASUKA_LOG. A test's process (ak_os_testing)
// writes them to stderr, read back here from a file it's pointed at.
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L  // dup, fileno
#endif
#include "t.h"
#include "std/std.h"
#include "rt/sched.h"

#if defined(_WIN32)
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#else
#include <unistd.h>
#endif

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

// fields is a [string: string] of the pairs in kv, NULL-ended.
static ak_map *fields(const char *const *kv) {
	ak_map *m = ak_map_new(AK_K_REF, true, AK_K_REF, NULL);
	for (; kv && kv[0]; kv += 2) ak_map_set(&m, (ak_val){.p = ak_str_c(kv[0])}, (ak_val){.p = ak_str_c(kv[1])});
	return m;
}

static void write_line(int level, const char *msg, ak_map *bound, ak_map *call) {
	ak_val args[4] = {{.i = level}, {.p = ak_str_c(msg)}, {.p = bound}, {.p = call}};
	fn("log.write")(args, 4);
	ak_release(args[1].p);
}

static void set_level(int level) { fn("log.setLeast")((ak_val[]){{.i = level}}, 1); }
static void set_format(int format, bool time) { fn("log.setOutput")((ak_val[]){{.i = format}, {.i = time}}, 2); }
static bool enabled(int level) { return fn("log.isEnabled")((ak_val[]){{.i = level}}, 1).i != 0; }

// The test's stderr, while it's captured.
static FILE *captured;
static int saved_stderr = -1;

static void capture(void) {
	fflush(stderr);
	captured = tmpfile();
	saved_stderr = dup(2);
	dup2(fileno(captured), 2);
}

// written is what was written since capture (free it), stderr back.
static char *written(void) {
	fflush(stderr);
	dup2(saved_stderr, 2);
	close(saved_stderr);
	long n = ftell(captured);
	char *s = malloc((size_t)n + 1);
	rewind(captured);
	size_t got = fread(s, 1, (size_t)n, captured);
	s[got] = 0;
	fclose(captured);
	return s;
}

// A test's process: its env os.fake's (none of the shell's ASUKA_LOG).
static void begin(const char *const *env) {
	ak_os_testing(true);
	ak_sched_manual_clock(true);
	ak_arr *e = ak_str_arr(0);
	for (; env && env[0]; env++) ak_str_arr_push(&e, env[0], strlen(env[0]));
	ak_arr *none = ak_str_arr(0);
	ak_bytes *in = ak_bytes_new(NULL, 0);
	fn("os.setFake")((ak_val[]){{.p = none}, {.p = e}, {.i = 1}, {.p = in}}, 4);
	ak_release(none);
	ak_release(e);
	ak_release(in);
}

static void end(void) {
	ak_os_reset();
	ak_os_testing(false);
	ak_sched_manual_clock(false);
}

TEST(off_costs_nothing) {
	begin(NULL);
	ak_map *bound = fields((const char *[]){"request", "7", NULL});
	ak_map *call = fields((const char *[]){"key", "a b", NULL});
	ak_str *msg = ak_str_c("cache miss");
	ak_val args[4] = {{.i = 0}, {.p = msg}, {.p = bound}, {.p = call}};
	ak_lib_fn write = fn("log.write"), on = fn("log.isEnabled");
	write(args, 4);  // ASUKA_LOG read: info, debug off
	uint64_t before = ak_alloc_count;
	for (int i = 0; i < 1000; i++) {
		write(args, 4);
		on((ak_val[]){{.i = 0}}, 1);
	}
	CHECK_EQ_INT(ak_alloc_count - before, 0);
	ak_release(msg);
	ak_release(bound);
	ak_release(call);
	end();
}

TEST(levels) {
	begin(NULL);
	CHECK(!enabled(0));
	CHECK(enabled(1) && enabled(3));
	set_level(2);
	CHECK(!enabled(1) && enabled(2));
	set_level(4);  // off
	CHECK(!enabled(3));
	CHECK(!enabled(-1) && !enabled(9));
	end();
	begin((const char *[]){"ASUKA_LOG", "Debug", NULL});
	CHECK(enabled(0));
	end();
	begin((const char *[]){"ASUKA_LOG", "warning", NULL});
	CHECK(!enabled(1) && enabled(2));
	end();
	begin((const char *[]){"ASUKA_LOG", "off", NULL});
	CHECK(!enabled(3));
	set_level(0);  // the program's counts over it
	CHECK(enabled(0));
	end();
}

TEST(text) {
	begin(NULL);
	ak_map *bound = fields((const char *[]){"request", "7", "user", "ann", NULL});
	ak_map *call = fields((const char *[]){"user", "bob", "path", "/a b", "empty", "", "q", "\"x\"", "ctl", "a\x01", NULL});
	capture();
	write_line(1, "started", NULL, NULL);
	write_line(0, "hidden", NULL, NULL);
	write_line(2, "slow", bound, call);
	set_format(0, false);
	ak_map *kv = fields((const char *[]){"k", "v", NULL});
	write_line(3, "héllo", NULL, kv);
	ak_release(kv);
	char *s = written();
	CHECK_EQ_STR(s,
		"2026-01-01 00:00:00.000 INFO  started\n"
		"2026-01-01 00:00:00.000 WARN  slow                                     request=7 user=bob path=\"/a b\" empty=\"\" q=\"\\\"x\\\"\" ctl=\"a\\u0001\"\n"
		"ERROR héllo                                    k=v\n");
	free(s);
	ak_release(bound);
	ak_release(call);
	end();
}

TEST(json) {
	begin(NULL);
	set_format(1, true);
	ak_map *bound = fields((const char *[]){"request", "7", NULL});
	ak_map *call = fields((const char *[]){"msg", "a\nb", "tab", "\t\\", NULL});
	capture();
	write_line(1, "saved \"a\"", bound, call);
	set_format(1, false);
	write_line(3, "failed", NULL, NULL);
	char *s = written();
	CHECK_EQ_STR(s,
		"{\"time\":\"2026-01-01T00:00:00.000Z\",\"level\":\"INFO\",\"msg\":\"saved \\\"a\\\"\",\"request\":\"7\",\"msg\":\"a\\nb\",\"tab\":\"\\t\\\\\"}\n"
		"{\"level\":\"ERROR\",\"msg\":\"failed\"}\n");
	free(s);
	ak_release(bound);
	ak_release(call);
	end();
}

TEST(colors) {
	begin((const char *[]){"FORCE_COLOR", "1", NULL});
	capture();
	ak_map *kv = fields((const char *[]){"k", "v", NULL});
	write_line(3, "failed", NULL, kv);
	ak_release(kv);
	char *s = written();
	CHECK_EQ_STR(s, "\x1b[2m2026-01-01 00:00:00.000\x1b[22m \x1b[31mERROR\x1b[39m failed                                   \x1b[2mk=\x1b[22mv\n");
	free(s);
	end();
}

T_MAIN()
