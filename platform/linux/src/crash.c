// crash.c: crash records (asuka/crash). The core keeps the run's record
// (asuka_crash_start: core/src/std/crash.c) and its own fatal errors; the
// host catches what only it sees, a fatal signal in native code (a
// segmentation fault in GTK's code, an abort of GLib's, the app's C), on
// an alternate stack, and writes it to the record's inbox as
// inbox/exception-<id>.json: the signal's name and reason, the crashed
// thread's frames (their addresses, binaries and offsets, symbols where
// dladdr finds them), the time and the process, for the core to make a
// report of on the next launch. Then the signal's default action runs, so
// the system's record (systemd-coredump, apport) is kept as ever.
//
// Before GTK loads, so a crash as it does is kept. The handler writes
// with write(2) into memory set aside at start; backtrace(3) is called
// once at start so it needn't load anything when it's called again.
#include "host.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static char inbox[3072];
static char altstack[64 * 1024];
static char out[48 * 1024];
static size_t out_len;
static void *frames[64];

// The device and the app, as JSON for asuka_crash_start: the firmware's
// product name, the distribution and its version (the host's, in a
// Flatpak), asuka.toml's version.
static void info(ak_buf *b);

const char *lx_os_version(void);

static void put(const char *s) {
	size_t n = strlen(s), room = out_len < sizeof out - 1 ? sizeof out - 1 - out_len : 0;
	if (n > room) n = room;
	memcpy(out + out_len, s, n);
	out_len += n;
}

// put_quoted writes s as a JSON string, escaping only what must be.
static void put_quoted(const char *s) {
	put("\"");
	char tmp[2] = {0};
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			put("\\");
			tmp[0] = (char)c;
			put(tmp);
		} else if (c < 0x20) {
			put(" ");
		} else {
			tmp[0] = (char)c;
			put(tmp);
		}
	}
	put("\"");
}

// put_num writes v in decimal, without stdio's locks.
static void put_num(unsigned long long v) {
	char d[24];
	int i = 23;
	d[i] = 0;
	do {
		d[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v && i > 0);
	put(d + i);
}

static void put_hex(unsigned long long v) {
	static const char hex[] = "0123456789abcdef";
	char d[24];
	int i = 23;
	d[i] = 0;
	do {
		d[--i] = hex[v & 15];
		v >>= 4;
	} while (v && i > 2);
	d[--i] = 'x';
	d[--i] = '0';
	put(d + i);
}

static const char *signal_name(int sig) {
	switch (sig) {
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS: return "SIGBUS";
	case SIGILL: return "SIGILL";
	case SIGFPE: return "SIGFPE";
	case SIGABRT: return "SIGABRT";
	case SIGTRAP: return "SIGTRAP";
	}
	return "SIGNAL";
}

static const char *signal_reason(int sig) {
	switch (sig) {
	case SIGSEGV: return "Segmentation fault";
	case SIGBUS: return "Bus error";
	case SIGILL: return "Illegal instruction";
	case SIGFPE: return "Floating point exception";
	case SIGABRT: return "Aborted";
	case SIGTRAP: return "Trace/breakpoint trap";
	}
	return "Fatal signal";
}

static void on_fatal(int sig, siginfo_t *si, void *uc) {
	(void)uc;
	int n = backtrace(frames, (int)AK_LEN(frames));
	out_len = 0;
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	put("{\"time\":");
	put_num((unsigned long long)ts.tv_sec * 1000 + (unsigned long long)ts.tv_nsec / 1000000);
	put(",\"pid\":");
	put_num((unsigned long long)getpid());
	put(",\"thread\":");
	put_quoted(gettid() == getpid() ? "main" : "Thread");
	put(",\"name\":");
	put_quoted(signal_name(sig));
	put(",\"reason\":\"");
	put(signal_reason(sig));
	if (si && (sig == SIGSEGV || sig == SIGBUS)) {
		put(" at ");
		put_hex((unsigned long long)(uintptr_t)si->si_addr);
	}
	put("\",\"stack\":\"");
	for (int i = 0; i < n; i++) {
		put_hex((unsigned long long)(uintptr_t)frames[i]);
		put("\\n");
	}
	put("\",\"frames\":[");
	for (int i = 0; i < n; i++) {
		if (i) put(",");
		put("{\"address\":");
		put_num((unsigned long long)(uintptr_t)frames[i]);
		Dl_info d;
		if (dladdr(frames[i], &d) && d.dli_fbase) {
			put(",\"offset\":");
			put_num((unsigned long long)((uintptr_t)frames[i] - (uintptr_t)d.dli_fbase));
			if (d.dli_fname) {
				const char *b = strrchr(d.dli_fname, '/');
				put(",\"binary\":");
				put_quoted(b ? b + 1 : d.dli_fname);
			}
			if (d.dli_sname) {
				put(",\"function\":");
				put_quoted(d.dli_sname);
			}
		}
		put("}");
	}
	put("]}");
	// inbox/exception-<pid>-<time>.json, written whole then renamed.
	char path[sizeof inbox + 96], tmp[sizeof path + 8];
	size_t k = strlen(inbox);
	memcpy(path, inbox, k);
	memcpy(path + k, "/exception-", 11);
	k += 11;
	out[out_len] = 0;
	char num[48];
	size_t m = 0;
	unsigned long long v = (unsigned long long)getpid() * 1000000ull + (unsigned long long)(ts.tv_nsec / 1000);
	do {
		num[m++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (m) path[k++] = num[--m];
	memcpy(path + k, ".json", 6);
	memcpy(tmp, path, k + 6);
	memcpy(tmp + k + 5, ".tmp", 5);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		ssize_t w = write(fd, out, out_len);
		close(fd);
		if (w == (ssize_t)out_len) rename(tmp, path);
	}
	// The default action: the process ends as the signal ends it.
	signal(sig, SIG_DFL);
	raise(sig);
}

static void info(ak_buf *b) {
	ak_buf_put(b, "{\"model\":", 9);
	size_t n;
	char *model = dsk_read_file("/sys/class/dmi/id/product_name", &n);
	if (model) {
		while (n && (model[n - 1] == '\n' || model[n - 1] == ' ')) model[--n] = 0;
	}
	ak_fmt_quote(b, ak_cstr(model ? model : ""));
	free(model);
	ak_buf_printf(b, ",\"system\":\"Linux\",\"systemVersion\":");
	ak_fmt_quote(b, ak_cstr(lx_os_version()));
	ak_buf_printf(b, ",\"appVersion\":");
	ak_fmt_quote(b, ak_cstr(S(H.cfg.version)));
	ak_buf_printf(b, ",\"build\":");
	ak_fmt_quote(b, ak_cstr(S(H.cfg.build)));
	ak_buf_printf(b, ",\"foreground\":true}");
}

// lx_crash_start begins the run's record, if the app reads its reports,
// and the fatal signals' handler.
void lx_crash_start(void) {
	ak_buf b = {0};
	info(&b);
	char *dir = asuka_crash_start(lx_buf_cstr(&b));
	ak_buf_free(&b);
	if (!dir) return;
	if (*dir && strlen(dir) + 7 < sizeof inbox) {
		snprintf(inbox, sizeof inbox, "%s/inbox", dir);
		dsk_mkdirs(inbox);
		backtrace(frames, 1);  // loads what backtrace needs, now
		stack_t ss = {.ss_sp = altstack, .ss_size = sizeof altstack};
		sigaltstack(&ss, NULL);
		struct sigaction sa = {0};
		sa.sa_sigaction = on_fatal;
		sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
		sigemptyset(&sa.sa_mask);
		const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
		for (size_t i = 0; i < AK_LEN(sigs); i++) sigaction(sigs[i], &sa, NULL);
	}
	free(dir);
}
