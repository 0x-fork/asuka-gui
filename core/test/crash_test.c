// platform/crash's record in the core (core/src/std/crash.c): a run begun
// as a host begins it, its breadcrumbs and values; earlier runs' files and
// what hosts wrote in the inbox (an exception, a MetricKit payload, an
// Android ANR and a native crash with its tombstone) collected into
// reports, each once, with the context of the run it was in; a recorded
// error; deleting.
#define _POSIX_C_SOURCE 200809L
#include "t.h"
#include "std/std.h"
#include "rt/sched.h"
#include "asuka.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *e = ak_std_crash; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

static ak_val S(const char *s) { return (ak_val){.p = ak_str_c(s)}; }

static void call1(const char *name, const char *a) {
	ak_val args[] = {S(a)};
	fn(name)(args, 1);
	ak_release(args[0].p);
}

static void call2(const char *name, const char *a, const char *b) {
	ak_val args[] = {S(a), S(b)};
	fn(name)(args, 2);
	ak_release(args[0].p);
	ak_release(args[1].p);
}

static char *reports(void) {
	ak_val r = fn("crash.coreReports")(NULL, 0);
	ak_str *s = r.p;
	char *out = strdup(s ? s->data : "");
	ak_release(s);
	return out;
}

static void put(const char *dir, const char *name, const void *p, size_t n) {
	char path[2048];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "wb");
	if (!f) {
		T_FAIL("can't write %s", path);
		return;
	}
	fwrite(p, 1, n, f);
	fclose(f);
}

static void puts_(const char *dir, const char *name, const char *text) { put(dir, name, text, strlen(text)); }

static bool there(const char *dir, const char *name) {
	char path[2048];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	struct stat st;
	return stat(path, &st) == 0;
}

static char *slurp(const char *dir, const char *name) {
	char path[2048];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f) return strdup("");
	static char buf[1 << 16];
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	return strdup(buf);
}

#define HAS(hay, needle) do { if (!strstr((hay), (needle))) T_FAIL("no %s in:\n%s", (needle), (hay)); } while (0)
#define HASNT(hay, needle) do { if (strstr((hay), (needle))) T_FAIL("%s in:\n%s", (needle), (hay)); } while (0)

// A protocol buffer, written: varints and length-delimited fields.
typedef struct {
	uint8_t b[1024];
	size_t n;
} pbw;

static void pb_varint(pbw *w, uint64_t v) {
	do {
		uint8_t c = v & 0x7f;
		v >>= 7;
		w->b[w->n++] = c | (v ? 0x80 : 0);
	} while (v);
}

static void pb_int(pbw *w, int field, uint64_t v) {
	pb_varint(w, (uint64_t)field << 3);
	pb_varint(w, v);
}

static void pb_bytes(pbw *w, int field, const void *p, size_t n) {
	pb_varint(w, (uint64_t)field << 3 | 2);
	pb_varint(w, n);
	memcpy(w->b + w->n, p, n);
	w->n += n;
}

static void pb_str(pbw *w, int field, const char *s) { pb_bytes(w, field, s, strlen(s)); }

TEST(crash_record_and_reports) {
	char *app = ak_fs_temp_dir();
	REQUIRE(app);
	ak_fs_set_app_dir(app);
	char *dir = asuka_crash_start("{\"model\":\"Pixel 9\",\"system\":\"Android\",\"systemVersion\":\"16\",\"appVersion\":\"1.2\",\"build\":\"7\",\"foreground\":true}");
	REQUIRE(dir && *dir);
	char want[1100];
	snprintf(want, sizeof want, "%s/crashes", app);
	CHECK_EQ_STR(dir, want);
	CHECK_EQ_INT(fn("crash.coreStarted")(NULL, 0).i, 1);
	char runs[1100], inbox[1100];
	snprintf(runs, sizeof runs, "%s/runs", dir);
	snprintf(inbox, sizeof inbox, "%s/inbox", dir);

	call1("crash.coreBreadcrumb", "opened the cart");
	call2("crash.coreSet", "user", "u1");
	call2("crash.coreSet", "screen", "cart");
	call2("crash.coreSet", "screen", "");
	ak_crash_lifecycle(1);

	// An earlier run that ended with the core's panic.
	puts_(runs, "1000-42.ctx",
	      "{\"run\":{\"pid\":42,\"start\":\"1970-01-01T00:00:01Z\",\"process\":\"app\",\"arch\":\"arm64\"}}\n"
	      "{\"info\":{\"model\":\"Pixel 8\",\"system\":\"Android\",\"systemVersion\":\"15\",\"appVersion\":\"1.1\"}}\n"
	      "{\"fg\":true}\n"
	      "{\"b\":\"tapped pay\",\"t\":1500}\n"
	      "{\"k\":\"user\",\"v\":\"u0\"}\n"
	      "{\"b\":\"cut sho");
	puts_(runs, "1000-42.crash", "panic: index out of range\n\npay (cart.asuka:12)\nmain (app.asuka:3)\n");
	{
		// It crashed at 1.2s: the file's last write.
		char p[1200];
		snprintf(p, sizeof p, "%s/1000-42.crash", runs);
		struct timespec ts[2] = {{1, 200000000}, {1, 200000000}};
		utimensat(AT_FDCWD, p, ts, 0);
	}
	// One that ended without crashing: nothing to say.
	puts_(runs, "2000-43.ctx", "{\"run\":{\"pid\":43,\"start\":\"1970-01-01T00:00:02Z\",\"process\":\"app\",\"arch\":\"arm64\"}}\n");
	puts_(runs, "2000-43.crash", "");

	// What hosts wrote: an uncaught JVM exception in run 43, ...
	puts_(inbox, "exception-a.json",
	      "{\"time\":2500,\"pid\":43,\"thread\":\"main\",\"name\":\"java.lang.IllegalStateException\",\"reason\":\"no cart\","
	      "\"stack\":\"java.lang.IllegalStateException: no cart\\n\\tat a.Cart.pay(Cart.kt:12)\\n\\tat a.Main.run(Main.kt:3)\\nCaused by: x\\n\\tat b.C.d(C.kt:1)\","
	      "\"threads\":[{\"name\":\"worker\",\"stack\":\"\\tat java.lang.Thread.sleep(Native Method)\"}]}");
	// ... MetricKit's payload: a crash (a signal) and a hang, ...
	puts_(inbox, "metrickit-b.json",
	      "{\"timeStampBegin\":\"2026-03-01 10:00:00\",\"timeStampEnd\":\"2026-03-02 10:00:00\","
	      "\"crashDiagnostics\":[{\"diagnosticMetaData\":{\"appVersion\":\"1.0\",\"appBuildVersion\":\"5\",\"osVersion\":\"iPhone OS 26.0 (23A341)\","
	      "\"deviceType\":\"iPhone17,1\",\"exceptionType\":1,\"signal\":11,\"exceptionCode\":0},"
	      "\"callStackTree\":{\"callStackPerThread\":true,\"callStacks\":[{\"threadAttributed\":false,\"callStackRootFrames\":[]},"
	      "{\"threadAttributed\":true,\"callStackRootFrames\":[{\"binaryUUID\":\"U1\",\"offsetIntoBinaryTextSegment\":100,\"sampleCount\":1,\"binaryName\":\"App\",\"address\":4096,"
	      "\"subFrames\":[{\"binaryUUID\":\"U2\",\"offsetIntoBinaryTextSegment\":7,\"sampleCount\":1,\"binaryName\":\"libsystem\",\"address\":8192}]}]}]}}],"
	      "\"hangDiagnostics\":[{\"diagnosticMetaData\":{\"hangDuration\":\"3 sec\"},\"callStackTree\":{\"callStackPerThread\":false,\"callStacks\":[]}}]}");
	// ... an ANR of run 43, with its traces, ...
	puts_(inbox, "exit-2600-43.trace",
	      "----- pid 43 -----\n\"main\" prio=5 tid=1 Native\n  at a.B.c(B.kt:9)\n  native: #00 pc 000000000004a1c0  /system/lib64/libc.so (syscall+28) (BuildId: 1a2b)\n\n"
	      "\"other\" prio=5 tid=2\n  at x.Y.z(Y.kt:1)\n----- end 43 -----\n");
	puts_(inbox, "exit-2600-43.json", "{\"time\":2600,\"pid\":43,\"reason\":6,\"status\":0,\"importance\":100,\"description\":\"Input dispatching timed out\",\"pss\":0,\"rss\":0,\"process\":\"a\"}");
	// ... and a native crash, with its tombstone.
	pbw frame = {0}, thread = {0}, entry = {0}, sig = {0}, ts = {0};
	pb_int(&frame, 1, 0x1234);
	pb_int(&frame, 2, 0x7f001234);
	pb_str(&frame, 4, "ak_view_frame");
	pb_int(&frame, 5, 16);
	pb_str(&frame, 6, "/data/app/x/lib/arm64/libasuka.so");
	pb_str(&frame, 8, "beef");
	pb_int(&thread, 1, 77);
	pb_str(&thread, 2, "main");
	pb_bytes(&thread, 4, frame.b, frame.n);
	pb_int(&entry, 1, 77);
	pb_bytes(&entry, 2, thread.b, thread.n);
	pb_int(&sig, 1, 11);
	pb_str(&sig, 2, "SIGSEGV");
	pb_int(&sig, 3, 1);
	pb_str(&sig, 4, "SEGV_MAPERR");
	pb_int(&sig, 8, 1);
	pb_int(&sig, 9, 0x10);
	pb_str(&ts, 2, "google/x:16");
	pb_int(&ts, 5, 44);
	pb_int(&ts, 6, 77);
	pb_str(&ts, 9, "dev.asuka.app");
	pb_bytes(&ts, 10, sig.b, sig.n);
	pb_bytes(&ts, 16, entry.b, entry.n);
	put(inbox, "exit-3000-44.trace", ts.b, ts.n);
	puts_(inbox, "exit-3000-44.json", "{\"time\":3000,\"pid\":44,\"reason\":5,\"status\":11,\"importance\":400,\"description\":\"crash\",\"pss\":0,\"rss\":0,\"process\":\"a\"}");
	// Not one of these: read, nothing made of it.
	puts_(inbox, "other.json", "{}");

	char *r = reports();
	// The core's panic, with run 42's context: its last breadcrumbs (the
	// line cut short left out), its values, the device it said.
	HAS(r, "\"id\":\"panic-1000-42\",\"kind\":\"panic\",\"fatal\":true");
	HAS(r, "\"message\":\"panic: index out of range\"");
	HAS(r, "\"name\":\"main\",\"crashed\":true,\"frames\":[{\"function\":\"pay\",\"file\":\"cart.asuka\",\"line\":12");
	HAS(r, "\"appVersion\":\"1.1\",\"build\":\"\",\"model\":\"Pixel 8\",\"system\":\"Android\",\"systemVersion\":\"15\",\"arch\":\"arm64\",\"process\":\"app\"");
	HAS(r, "\"pid\":42,\"started\":1000,\"uptime\":200000000,\"foreground\":true,\"breadcrumbs\":[{\"time\":1500,\"message\":\"tapped pay\"}],\"values\":{\"user\":\"u0\"}");
	// The exception, in run 43.
	HAS(r, "\"id\":\"exception-a\",\"kind\":\"exception\",\"fatal\":true,\"time\":2500,\"message\":\"java.lang.IllegalStateException: no cart\"");
	HAS(r, "{\"function\":\"a.Cart.pay\",\"file\":\"Cart.kt\",\"line\":12");
	HASNT(r, "\"function\":\"b.C.d\"");
	HAS(r, "\"name\":\"worker\",\"crashed\":false,\"frames\":[{\"function\":\"java.lang.Thread.sleep\",\"file\":\"\"");
	HAS(r, "\"pid\":43,\"started\":2000,\"uptime\":500000000");
	// MetricKit's: the crash (EXC_BAD_ACCESS, SIGSEGV), its attributed
	// thread first; the hang, not fatal.
	HAS(r, "\"id\":\"metrickit-b-0\",\"kind\":\"native\",\"fatal\":true");
	HAS(r, "\"message\":\"EXC_BAD_ACCESS (SIGSEGV)\"");
	HAS(r, "\"threads\":[{\"name\":\"Thread 1\",\"crashed\":true,\"frames\":[{\"function\":\"\",\"file\":\"\",\"line\":0,\"binary\":\"App\",\"binaryId\":\"U1\",\"address\":4096,\"offset\":100}");
	HAS(r, "Binary Images:\\nU1 App\\nU2 libsystem");
	HAS(r, "\"model\":\"iPhone17,1\",\"system\":\"iOS\",\"systemVersion\":\"26.0\"");
	HAS(r, "\"id\":\"metrickit-b-1\",\"kind\":\"hang\",\"fatal\":false");
	HAS(r, "Hang: the main thread was busy for 3 sec");
	// The ANR: main first, its native frame read.
	HAS(r, "\"id\":\"exit-2600-43\",\"kind\":\"hang\"");
	HAS(r, "\"message\":\"ANR: Input dispatching timed out\"");
	HAS(r, "{\"function\":\"syscall\",\"file\":\"\",\"line\":0,\"binary\":\"/system/lib64/libc.so\",\"binaryId\":\"1a2b\",\"address\":0,\"offset\":303552}");
	// The native crash, from its tombstone.
	HAS(r, "\"id\":\"exit-3000-44\",\"kind\":\"native\"");
	HAS(r, "\"message\":\"SIGSEGV (SEGV_MAPERR)\"");
	HAS(r, "{\"function\":\"ak_view_frame\",\"file\":\"\",\"line\":0,\"binary\":\"/data/app/x/lib/arm64/libasuka.so\",\"binaryId\":\"beef\",\"address\":2130711092,\"offset\":4660}");
	HAS(r, "signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x10");
	HAS(r, "\"foreground\":false");
	// The oldest first.
	CHECK(strstr(r, "panic-1000-42") < strstr(r, "exception-a"));
	free(r);

	// Read: the inbox's files and the panic's .crash are gone, and run
	// 42's context says it was reported.
	CHECK(!there(inbox, "exception-a.json"));
	CHECK(!there(inbox, "exit-3000-44.trace"));
	CHECK(!there(inbox, "other.json"));
	CHECK(!there(runs, "1000-42.crash"));
	char *ctx = slurp(runs, "1000-42.ctx");
	HAS(ctx, "{\"reported\":\"panic\"}");
	free(ctx);

	// A recorded error: a report at once, with this run's context.
	int64_t v = fn("crash.coreVersion")(NULL, 0).i;
	call1("crash.coreRecord", "checkout: no network");
	CHECK(fn("crash.coreVersion")(NULL, 0).i > v);
	r = reports();
	HAS(r, "\"kind\":\"nonfatal\",\"fatal\":false");
	HAS(r, "\"message\":\"checkout: no network\"");
	HAS(r, "\"appVersion\":\"1.2\",\"build\":\"7\",\"model\":\"Pixel 9\",\"system\":\"Android\",\"systemVersion\":\"16\"");
	HAS(r, "\"breadcrumbs\":[{\"time\":");
	HAS(r, "\"message\":\"opened the cart\"},{\"time\":");
	HAS(r, "\"message\":\"app: background\"}],\"values\":{\"user\":\"u1\"}");
	// Each once: the same reports again, no more.
	size_t once = 0;
	for (const char *p = r; (p = strstr(p, "\"id\":\"exception-a\"")); p++) once++;
	CHECK_EQ_INT(once, 1);
	free(r);

	// Deleted: gone from the reports.
	call1("crash.coreDelete", "exception-a");
	call1("crash.coreDelete", "../nowhere");
	r = reports();
	HASNT(r, "exception-a");
	HAS(r, "panic-1000-42");
	free(r);

	free(dir);
	ak_fs_remove_tree(app);
	ak_free(app);
}

T_MAIN()
