// crash.c: the run's crash record (asuka/crash; host/windows' crash.go):
// the core keeps it (asuka_crash_start, core/src/std/crash.c), with the
// PC and the app; the host writes what the core can't see, an exception
// nothing handled (an access violation in the core or in a package's C),
// as a report in the record's inbox (inbox/exception-<time>.json, the
// format the core reads of every host's: name, reason, the stack as text
// and as frames, fatal, time, pid), from an unhandled exception filter.
// The filter returns EXCEPTION_CONTINUE_SEARCH: Windows Error Reporting
// keeps its record as ever. The core reads the report at the app's next
// launch.
#include "modules.h"

#include <dbghelp.h>
#include <stdarg.h>

static wchar_t inbox[MAX_PATH * 2];
static char report[64 * 1024];  // written in place: the heap may be what failed

static const char *exception_name(DWORD code) {
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
	case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
	case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
	case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
	}
	return "EXCEPTION";
}

// The report's text, appended in place.
static size_t at;

static void put(const char *s) {
	size_t n = strlen(s);
	if (at + n >= sizeof report) n = sizeof report - 1 - at;
	memcpy(report + at, s, n);
	at += n;
	report[at] = 0;
}

static void putf(const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	put(buf);
}

// put_json puts s as a JSON string.
static void put_json(const char *s) {
	put("\"");
	char c[8];
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		if (ch == '"' || ch == '\\') c[0] = '\\', c[1] = (char)ch, c[2] = 0;
		else if (ch == '\n') strcpy(c, "\\n");
		else if (ch < 0x20) snprintf(c, sizeof c, "\\u%04x", ch);
		else c[0] = (char)ch, c[1] = 0;
		put(c);
	}
	put("\"");
}

typedef struct {
	char function[256], file[MAX_PATH], binary[MAX_PATH];
	DWORD line;
	uint64_t address, offset;
} frame;

static frame frames[64];

static void describe(HANDLE proc, frame *f) {
	f->function[0] = f->file[0] = f->binary[0] = 0, f->line = 0, f->offset = 0;
	HMODULE m = NULL;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(uintptr_t)f->address, &m) && m) {
		wchar_t p[MAX_PATH];
		DWORD n = GetModuleFileNameW(m, p, WH_LEN(p));
		const wchar_t *base = p;
		for (DWORD i = 0; i < n; i++)
			if (p[i] == '\\') base = p + i + 1;
		WideCharToMultiByte(CP_UTF8, 0, base, -1, f->binary, sizeof f->binary, NULL, NULL);
		f->offset = f->address - (uint64_t)(uintptr_t)m;
	}
	union {
		SYMBOL_INFO info;
		char buf[sizeof(SYMBOL_INFO) + 256];
	} sym;
	memset(&sym, 0, sizeof sym);
	sym.info.SizeOfStruct = sizeof sym.info;
	sym.info.MaxNameLen = 255;
	DWORD64 disp = 0;
	if (SymFromAddr(proc, f->address, &disp, &sym.info)) snprintf(f->function, sizeof f->function, "%s", sym.info.Name);
	IMAGEHLP_LINE64 line = {sizeof line};
	DWORD ld = 0;
	if (SymGetLineFromAddr64(proc, f->address, &ld, &line)) {
		snprintf(f->file, sizeof f->file, "%s", line.FileName);
		f->line = line.LineNumber;
	}
}

// walk fills frames from the exception's context: the faulting thread's
// stack.
static int walk(CONTEXT *ctx) {
	HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	SymInitialize(proc, NULL, TRUE);
	CONTEXT c = *ctx;
	STACKFRAME64 sf = {0};
	DWORD machine;
#if defined(_M_ARM64) || defined(__aarch64__)
	machine = IMAGE_FILE_MACHINE_ARM64;
	sf.AddrPC.Offset = c.Pc, sf.AddrFrame.Offset = c.Fp, sf.AddrStack.Offset = c.Sp;
#else
	machine = IMAGE_FILE_MACHINE_AMD64;
	sf.AddrPC.Offset = c.Rip, sf.AddrFrame.Offset = c.Rbp, sf.AddrStack.Offset = c.Rsp;
#endif
	sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
	int n = 0;
	while (n < (int)WH_LEN(frames) && StackWalk64(machine, proc, thread, &sf, &c, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
		if (!sf.AddrPC.Offset) break;
		frames[n].address = sf.AddrPC.Offset;
		describe(proc, &frames[n]);
		n++;
	}
	return n;
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *e) {
	static volatile LONG once;
	if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
	DWORD code = e->ExceptionRecord->ExceptionCode;
	char reason[256];
	if (code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) {
		ULONG_PTR kind = e->ExceptionRecord->ExceptionInformation[0];
		snprintf(reason, sizeof reason, "%s of address 0x%llx at 0x%llx", kind == 1 ? "write" : kind == 8 ? "execution" : "read",
			(unsigned long long)e->ExceptionRecord->ExceptionInformation[1], (unsigned long long)(uintptr_t)e->ExceptionRecord->ExceptionAddress);
	} else {
		snprintf(reason, sizeof reason, "code 0x%08lx at 0x%llx", (unsigned long)code, (unsigned long long)(uintptr_t)e->ExceptionRecord->ExceptionAddress);
	}
	int n = walk(e->ContextRecord);
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	int64_t ms = (int64_t)((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000) - 11644473600000LL;
	at = 0;
	report[0] = 0;
	// The stack as text, then the report.
	static char stack[32 * 1024];
	size_t sl = 0;
	for (int i = 0; i < n && sl < sizeof stack - 600; i++) {
		frame *f = &frames[i];
		sl += (size_t)snprintf(stack + sl, sizeof stack - sl, "%s!%s+0x%llx", *f->binary ? f->binary : "?", *f->function ? f->function : "?",
			(unsigned long long)f->offset);
		if (*f->file) sl += (size_t)snprintf(stack + sl, sizeof stack - sl, " (%s:%lu)", f->file, f->line);
		stack[sl++] = '\n';
		stack[sl] = 0;
	}
	char thread[32];
	snprintf(thread, sizeof thread, "thread %lu", GetCurrentThreadId());
	put("{\"name\":"), put_json(exception_name(code));
	put(",\"reason\":"), put_json(reason);
	put(",\"stack\":"), put_json(stack);
	put(",\"thread\":"), put_json(thread);
	put(",\"frames\":[");
	for (int i = 0; i < n; i++) {
		frame *f = &frames[i];
		if (i) put(",");
		put("{\"function\":"), put_json(f->function);
		put(",\"file\":"), put_json(f->file);
		putf(",\"line\":%lu,\"binary\":", f->line), put_json(f->binary);
		putf(",\"binaryId\":\"\",\"address\":%llu,\"offset\":%llu}", (unsigned long long)f->address, (unsigned long long)f->offset);
	}
	putf("],\"fatal\":true,\"time\":%lld,\"pid\":%lu}", (long long)ms, GetCurrentProcessId());
	// Written whole under a name of its own, then renamed: the core never
	// reads half a report.
	wchar_t tmp[MAX_PATH * 2 + 64], path[MAX_PATH * 2 + 64];
	swprintf(tmp, WH_LEN(tmp), L"%ls\\.exception-%lld-%lu.tmp", inbox, (long long)ms, GetCurrentProcessId());
	swprintf(path, WH_LEN(path), L"%ls\\exception-%lld-%lu.json", inbox, (long long)ms, GetCurrentProcessId());
	HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f != INVALID_HANDLE_VALUE) {
		DWORD wrote;
		WriteFile(f, report, (DWORD)at, &wrote, NULL);
		FlushFileBuffers(f);
		CloseHandle(f);
		MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

// model is the PC's model: its firmware's product name, "" if there's
// none.
static char *model(void) {
	wchar_t *v = wh_reg_string(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName");
	char *s = v ? wh_utf8(v, -1, NULL) : wh_strdup("");
	free(v);
	return s;
}

// wh_crash_start begins the run's crash record, with the PC and the app,
// and, when the app reads its reports, watches for an exception nothing
// handles.
void wh_crash_start(void) {
	char *m = model();
	char ver[32];
	snprintf(ver, sizeof ver, "10.0.%lu", (unsigned long)wh_build());
	dsk_jw w = {0};
	dsk_jw_obj(&w);
	dsk_jw_key(&w, "model"), dsk_jw_str(&w, m);
	dsk_jw_key(&w, "system"), dsk_jw_str(&w, "Windows");
	dsk_jw_key(&w, "systemVersion"), dsk_jw_str(&w, ver);
	dsk_jw_key(&w, "appVersion"), dsk_jw_str(&w, H.config.version ? H.config.version : "");
	dsk_jw_key(&w, "build"), dsk_jw_str(&w, H.config.build ? H.config.build : "");
	if (!H.config.background) dsk_jw_key(&w, "foreground"), dsk_jw_bool(&w, true);
	dsk_jw_end(&w);
	free(m);
	char *info = dsk_jw_take(&w, NULL);
	asuka_set_app_dir(H.dirs.data);  // the record's in the app's directory
	char *dir = asuka_crash_start(info);
	free(info);
	if (dir && *dir) {
		wchar_t *wd = wh_wide(dir, -1);
		wcsncpy(inbox, wd, WH_LEN(inbox) - 1);
		free(wd);
		CreateDirectoryW(inbox, NULL);
		SetUnhandledExceptionFilter(unhandled);
	}
	free(dir);
}
