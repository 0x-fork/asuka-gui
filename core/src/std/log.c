// log.c: std/log's builtins: leveled lines with fields (std/log/
// log.asuka), formatted here and written where the platform shows them.
//
//	log.write(level, message, bound, fields)   a line, when level is on:
//	                                           bound's fields (a Logger's),
//	                                           then fields (the call's, which
//	                                           replace bound's of one name)
//	log.isEnabled(level) -> bool               0 debug, 1 info, 2 warn, 3 error
//	log.setLeast(level)                        the least that's written (4: off)
//	log.setOutput(format, time)                0 text, 1 JSON; with timestamps
//
// The least level is ASUKA_LOG's (debug, info, warn, error or off, in any
// case) until the program sets one, else info. A level that's off costs
// one atomic load: nothing is formatted, nothing allocated.
//
// Where lines go: stderr for a program and a test (where FakeOS.run keeps
// them); an app's go to the system's log, where its developer looks:
// unified logging on iOS and macOS (os_log: Xcode's console, Console.app,
// `log stream`), logcat on Android, the browser's console on the web, the
// debugger's output on Windows (OutputDebugString). A desktop's process
// whose stderr goes somewhere (a terminal, a pipe, a file: asuka dev's
// player, a program run by hand) writes there instead; Linux's always does
// (the session's journal keeps it). asuka dev's player on a phone writes
// to stderr too, which it sends to the terminal.
//
// On stderr a line is text, as people read it (the time, the level, the
// message, then key=value fields, quoted when they need it, colored when
// std/term says colors show on stderr), or JSON, one object a line, as
// Go's log/slog writes it: {"time":…,"level":"INFO","msg":…,"key":"value"}.
// The system's logs keep their own time and level: they get the message
// and the fields.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE  // getprogname
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE  // getprogname (bionic)
#endif
#include "std/std.h"
#include "abi/dev.h"
#include "base/thread.h"
#include "rt/sched.h"
#include "rt/json.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if defined(__has_include)
#if __has_include(<os/log.h>)
#include <os/log.h>
#define AK_LOG_OSLOG 1  // the SDK's (a cross build's headers have none)
#endif
#endif
#endif
#if !defined(_WIN32) && !defined(__wasm__)
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__)
// liblog's (linked with every Android build).
int __android_log_write(int prio, const char *tag, const char *text);
#endif
#if defined(__wasm__)
// The page's console (platform/web/src/core.js): debug, info, warn or
// error by level, and asuka dev's terminal when it's there.
__attribute__((import_module("asuka"), import_name("log_write"))) void js_log_write(int32_t level, const void *p, uint32_t len);
#endif

enum { LV_DEBUG, LV_INFO, LV_WARN, LV_ERROR, LV_OFF };
enum { FMT_TEXT, FMT_JSON };
enum { TO_STDERR = 1, TO_SYSTEM = 2 };

#define UNREAD (-1)
#define PAD 40  // a text line's message is padded to this many characters before its fields

static _Atomic int least = UNREAD;   // what the program set; UNREAD: ASUKA_LOG's
static _Atomic int from_env = UNREAD;  // ASUKA_LOG's, once read (each time, in a test)
static _Atomic int format = FMT_TEXT;
static _Atomic bool with_time = true;
static _Atomic int desk_sink = 0;  // a desktop's sink, once decided (0: not yet)

void ak_log_reset(void) {
	atomic_store(&least, UNREAD);
	atomic_store(&from_env, UNREAD);
	atomic_store(&format, FMT_TEXT);
	atomic_store(&with_time, true);
	atomic_store(&desk_sink, 0);
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static bool same(const char *a, const char *b) {
	for (; *a && *b; a++, b++)
		if (lower((unsigned char)*a) != *b) return false;
	return !*a && !*b;
}

// env_level is ASUKA_LOG's level, info when it isn't set or isn't one.
// A test's process sees only os.fake's env:, so what the tests print
// doesn't follow the shell's.
static int env_level(void) {
	char *v = ak_os_getenv("ASUKA_LOG", false);
	int l = LV_INFO;
	if (v) {
		if (same(v, "debug") || same(v, "trace") || same(v, "all")) l = LV_DEBUG;
		else if (same(v, "info")) l = LV_INFO;
		else if (same(v, "warn") || same(v, "warning")) l = LV_WARN;
		else if (same(v, "error")) l = LV_ERROR;
		else if (same(v, "off") || same(v, "none")) l = LV_OFF;
	}
	ak_free(v);
	return l;
}

static int threshold(void) {
	int l = atomic_load_explicit(&least, memory_order_relaxed);
	if (l != UNREAD) return l;
	l = atomic_load_explicit(&from_env, memory_order_relaxed);
	if (l != UNREAD) return l;
	l = env_level();
	// A test's process is os.fake's, which may change its env: read again.
	if (!ak_os_test_process()) atomic_store(&from_env, l);
	return l;
}

static bool on(int64_t level) { return level >= LV_DEBUG && level <= LV_ERROR && level >= threshold(); }

// ---- Where lines go ----

#if (defined(__APPLE__) && !TARGET_OS_IPHONE && defined(AK_LOG_OSLOG)) || defined(_WIN32)
// stderr_read reports whether stderr goes somewhere: a terminal, a pipe
// or a file, not nothing (a GUI app's on Windows) nor /dev/null (an app
// opened from the Finder, a launchd job's without a log file).
static bool stderr_read(void) {
#if defined(_WIN32)
	HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
	if (!h || h == INVALID_HANDLE_VALUE) return false;
	DWORD type = GetFileType(h);
	if (type == FILE_TYPE_UNKNOWN) return false;
	if (type == FILE_TYPE_CHAR) {
		DWORD mode;
		return GetConsoleMode(h, &mode) != 0;  // a console, not NUL
	}
	return true;
#else
	struct stat err, null;
	if (fstat(2, &err) != 0) return false;
	if (S_ISCHR(err.st_mode) && stat("/dev/null", &null) == 0 && err.st_rdev == null.st_rdev) return false;
	return true;
#endif
}
#endif

static int sinks(void) {
	if (ak_os_test_process()) return TO_STDERR;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#if defined(__ANDROID__) || defined(AK_LOG_OSLOG)
	return TO_SYSTEM | (ak_dev_player() ? TO_STDERR : 0);
#else
	return TO_STDERR;
#endif
#elif defined(__wasm__)
	return TO_SYSTEM;
#elif (defined(__APPLE__) && defined(AK_LOG_OSLOG)) || defined(_WIN32)
	int s = atomic_load(&desk_sink);
	if (!s) {
		s = stderr_read() ? TO_STDERR : TO_SYSTEM;
		atomic_store(&desk_sink, s);
	}
	return s;
#else
	return TO_STDERR;
#endif
}

// ---- Formatting ----

static const char *const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
static const char *const colors[] = {"\x1b[34m", "\x1b[32m", "\x1b[33m", "\x1b[31m"};  // blue, green, yellow, red

static void put(ak_buf *b, const char *s) { ak_buf_put(b, s, strlen(s)); }

// put_json writes s[0:n] as a JSON string, quotes and all.
static void put_json(ak_buf *b, const char *s, size_t n) {
	ak_buf_put(b, "\"", 1);
	size_t from = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		const char *esc = NULL;
		char u[8];
		if (c == '"') esc = "\\\"";
		else if (c == '\\') esc = "\\\\";
		else if (c == '\n') esc = "\\n";
		else if (c == '\r') esc = "\\r";
		else if (c == '\t') esc = "\\t";
		else if (c < 0x20 || c == 0x7f) {
			snprintf(u, sizeof u, "\\u%04x", c);
			esc = u;
		}
		if (esc) {
			ak_buf_put(b, s + from, i - from);
			put(b, esc);
			from = i + 1;
		}
	}
	ak_buf_put(b, s + from, n - from);
	ak_buf_put(b, "\"", 1);
}

// bare reports whether a text line can show s as it is: not empty, and
// without spaces, quotes, '=' or control characters (logfmt's rule).
static bool bare(const char *s, size_t n) {
	if (n == 0) return false;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c <= ' ' || c == '"' || c == '=' || c == 0x7f) return false;
	}
	return true;
}

static void put_text(ak_buf *b, const ak_str *s) {
	if (bare(s->data, s->len)) ak_buf_put(b, s->data, s->len);
	else put_json(b, s->data, s->len);
}

// chars is the number of code points in s[0:n].
// put_value writes a field's value, its JSON (log.asuka's texts), as
// text: a string as its text (quoted as put_text says), the rest (a
// number, true, an array) as JSON writes it.
static void put_value(ak_buf *b, const ak_str *v) {
	if (v->len && v->data[0] == '"') {
		ak_val s = ak_json_decode(v->data, v->len, "s", NULL);
		if (!ak_error_pending() && s.p) {
			put_text(b, s.p);
			ak_release(s.p);
			return;
		}
		if (ak_error_pending()) ak_release(ak_error_take());
	}
	if (bare(v->data, v->len)) ak_buf_put(b, v->data, v->len);
	else put_text(b, v);
}

static size_t chars(const char *s, size_t n) {
	size_t k = 0;
	for (size_t i = 0; i < n; i++)
		if (((unsigned char)s[i] & 0xc0) != 0x80) k++;
	return k;
}

// civil is the date of a day since 1970-01-01 (Howard Hinnant's
// algorithm).
static void civil(int64_t days, int64_t *y, int *m, int *d) {
	days += 719468;
	int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	int64_t doe = days - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = yoe + era * 400 + (*m <= 2);
}

// put_time writes now in the local zone (UTC in a test, whose clock
// starts at 2026-01-01): "2026-11-19 10:04:05.123" for text, RFC 3339
// with its offset for JSON.
static void put_time(ak_buf *b, bool json) {
	int64_t ns = ak_sched_wall();
	int64_t secs = ns >= 0 ? ns / 1000000000 : -((-ns + 999999999) / 1000000000);
	int ms = (int)((ns - secs * 1000000000) / 1000000);
	int32_t off = ak_sched_manual() ? 0 : ak_zone_offset(secs, NULL);
	int64_t t = secs + off;
	int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
	int64_t in_day = t - days * 86400;
	int64_t y;
	int mo, d;
	civil(days, &y, &mo, &d);
	ak_buf_printf(b, "%04lld-%02d-%02d%c%02d:%02d:%02d.%03d", (long long)y, mo, d, json ? 'T' : ' ', (int)(in_day / 3600), (int)(in_day / 60 % 60), (int)(in_day % 60), ms);
	if (!json) return;
	if (off == 0) {
		put(b, "Z");
	} else {
		int a = off < 0 ? -off : off;
		ak_buf_printf(b, "%c%02d:%02d", off < 0 ? '-' : '+', a / 3600, a / 60 % 60);
	}
}

typedef struct {
	const ak_map *bound, *fields;
} field_set;

static bool has(const ak_map *m, ak_str *key) {
	ak_val v;
	return m && m->live && ak_map_get(m, (ak_val){.p = key}, &v);
}

// EACH runs its body with k and v, each field of the line: bound's first
// (but those the call's own replace), then the call's.
#define EACH(fs, k, v, ...)                                             \
	for (int part_ = 0; part_ < 2; part_++) {                           \
		const ak_map *m_ = part_ ? (fs).fields : (fs).bound;            \
		if (!m_) continue;                                              \
		for (uint32_t i_ = 0; i_ < m_->len; i_++) {                     \
			if (m_->dead && m_->dead[i_]) continue;                     \
			ak_str *k = m_->keys[i_].p, *v = m_->vals[i_].p;            \
			if (!part_ && has((fs).fields, k)) continue;                \
			__VA_ARGS__                                                 \
		}                                                               \
	}

static bool any_field(field_set fs) {
	return (fs.bound && fs.bound->live) || (fs.fields && fs.fields->live);
}

// text writes the human line: its time and level when stamped (a
// stream's), then the message, then the fields.
static void text(ak_buf *b, int level, const ak_str *msg, field_set fs, bool stamped, bool color) {
	if (stamped) {
		if (atomic_load(&with_time)) {
			if (color) put(b, "\x1b[2m");
			put_time(b, false);
			if (color) put(b, "\x1b[22m");
			put(b, " ");
		}
		if (color) put(b, colors[level]);
		ak_buf_printf(b, "%-5s", names[level]);
		if (color) put(b, "\x1b[39m");
		put(b, " ");
	}
	ak_buf_put(b, msg->data, msg->len);
	if (!any_field(fs)) return;
	if (stamped)
		for (size_t c = chars(msg->data, msg->len); c < PAD; c++) ak_buf_put(b, " ", 1);
	EACH(fs, k, v, {
		put(b, " ");
		if (color) put(b, "\x1b[2m");
		put_text(b, k);
		put(b, "=");
		if (color) put(b, "\x1b[22m");
		put_value(b, v);
	})
}

static void json(ak_buf *b, int level, const ak_str *msg, field_set fs) {
	put(b, "{");
	if (atomic_load(&with_time)) {
		put(b, "\"time\":\"");
		put_time(b, true);
		put(b, "\",");
	}
	ak_buf_printf(b, "\"level\":\"%s\",\"msg\":", names[level]);
	put_json(b, msg->data, msg->len);
	EACH(fs, k, v, {
		put(b, ",");
		put_json(b, k->data, k->len);
		put(b, ":");
		ak_buf_put(b, v->data, v->len);  // the value's JSON, as it is
	})
	put(b, "}");
}

// ---- The system's logs ----

#if defined(_WIN32)
static void debugger(const char *s, size_t n) {
	int k = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
	wchar_t *w = ak_alloc(sizeof(wchar_t) * ((size_t)k + 1));
	MultiByteToWideChar(CP_UTF8, 0, s, (int)n, w, k);
	w[k] = 0;
	OutputDebugStringW(w);
	ak_free(w);
}
#endif

static void to_system(int level, ak_buf *line) {
#if defined(AK_LOG_OSLOG)
	static const os_log_type_t types[] = {OS_LOG_TYPE_DEBUG, OS_LOG_TYPE_INFO, OS_LOG_TYPE_DEFAULT, OS_LOG_TYPE_ERROR};
	ak_buf_put(line, "", 1);
	os_log_with_type(OS_LOG_DEFAULT, types[level], "%{public}s", (const char *)line->data);
#elif defined(__ANDROID__)
	static const int prio[] = {3, 4, 5, 6};  // ANDROID_LOG_DEBUG … ANDROID_LOG_ERROR
	ak_buf_put(line, "", 1);
	const char *tag = getprogname();
	__android_log_write(prio[level], tag && *tag ? tag : "asuka", (const char *)line->data);
#elif defined(__wasm__)
	js_log_write(level, line->data, (uint32_t)line->len);
#elif defined(_WIN32)
	ak_buf_put(line, "\r\n", 2);
	debugger((const char *)line->data, line->len);
#else
	AK_UNUSED(level);
	AK_UNUSED(line);
#endif
}

// ---- The builtins ----

static ak_val write_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t level = args[0].i;
	if (!on(level)) return (ak_val){0};
	const ak_str *msg = args[1].p;
	field_set fs = {args[2].p, args[3].p};
	int to = sinks();
	ak_buf b = {0};
	if (to & TO_STDERR) {
		if (atomic_load(&format) == FMT_JSON) json(&b, (int)level, msg, fs);
		else text(&b, (int)level, msg, fs, true, ak_term_colors(2));
		ak_buf_put(&b, "\n", 1);
		ak_os_write(2, b.data, b.len);
	}
	if (to & TO_SYSTEM) {
		b.len = 0;
#if defined(_WIN32)
		// The debugger's output has no time or level of its own.
		text(&b, (int)level, msg, fs, true, false);
#else
		text(&b, (int)level, msg, fs, false, false);
#endif
		to_system((int)level, &b);
	}
	ak_buf_free(&b);
	return (ak_val){0};
}

static ak_val enabled_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.i = on(args[0].i)};
}

static ak_val set_level_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t l = args[0].i;
	if (l < LV_DEBUG || l > LV_OFF) l = LV_INFO;
	atomic_store(&least, (int)l);
	return (ak_val){0};
}

static ak_val set_format_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	atomic_store(&format, args[0].i == FMT_JSON ? FMT_JSON : FMT_TEXT);
	atomic_store(&with_time, args[1].i != 0);
	return (ak_val){0};
}

const ak_lib_entry ak_std_log[] = {
	{"log.write", write_fn},
	{"log.isEnabled", enabled_fn},
	{"log.setLeast", set_level_fn},
	{"log.setOutput", set_format_fn},
	{NULL, NULL},
};
