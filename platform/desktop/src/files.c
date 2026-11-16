// files.c: files by UTF-8 path, the environment, the executable's path
// (desktop.h). Windows' file names and environment are UTF-16: they're
// converted here; Linux's are bytes, UTF-8 by convention.
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L  // strdup, readlink under -std=c17
#endif
#include "desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static wchar_t *wide(const char *s) {
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (n <= 0) return NULL;
	wchar_t *w = malloc((size_t)n * sizeof *w);
	if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
	return w;
}

static char *narrow(const wchar_t *w) {
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if (n <= 0) return NULL;
	char *s = malloc((size_t)n);
	if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
	return s;
}

static FILE *open_file(const char *path, const wchar_t *mode) {
	wchar_t *w = wide(path);
	if (!w) return NULL;
	FILE *f = _wfopen(w, mode);
	free(w);
	return f;
}

#define SEP '\\'
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static FILE *open_file(const char *path, const char *mode) { return fopen(path, mode); }
#define SEP '/'
#endif

char *dsk_read_file(const char *path, size_t *len) {
	if (len) *len = 0;
	if (!path || !*path) return NULL;
#ifdef _WIN32
	FILE *f = open_file(path, L"rb");
#else
	FILE *f = open_file(path, "rb");
#endif
	if (!f) return NULL;
	size_t cap = 4096, n = 0;
	char *b = malloc(cap + 1);
	while (b) {
		size_t got = fread(b + n, 1, cap - n, f);
		n += got;
		if (n < cap) break;
		cap *= 2;
		char *nb = realloc(b, cap + 1);
		if (!nb) free(b);
		b = nb;
	}
	bool failed = ferror(f) != 0;
	fclose(f);
	if (!b || failed) {
		free(b);
		return NULL;
	}
	b[n] = 0;
	if (len) *len = n;
	return b;
}

static bool is_sep(char c) {
#ifdef _WIN32
	return c == '\\' || c == '/';
#else
	return c == '/';
#endif
}

char *dsk_dir(const char *path) {
	size_t n = strlen(path);
	while (n > 0 && !is_sep(path[n - 1])) n--;
	while (n > 1 && is_sep(path[n - 1])) n--;
	char *d = malloc(n + 2);
	if (!d) return NULL;
	if (n == 0) {
		d[0] = '.', d[1] = 0;
		return d;
	}
	memcpy(d, path, n);
	d[n] = 0;
#ifdef _WIN32
	if (n == 2 && d[1] == ':') d[2] = SEP, d[3] = 0;  // C: alone is C:'s current directory
#endif
	return d;
}

const char *dsk_base(const char *path) {
	const char *b = path;
	for (const char *p = path; *p; p++)
		if (is_sep(*p)) b = p + 1;
	return b;
}

char *dsk_join(const char *a, const char *b) {
	size_t na = strlen(a), nb = strlen(b);
	char *s = malloc(na + nb + 2);
	if (!s) return NULL;
	memcpy(s, a, na);
	size_t n = na;
	if (n > 0 && !is_sep(s[n - 1])) s[n++] = SEP;
	memcpy(s + n, b, nb + 1);
	return s;
}

bool dsk_mkdirs(const char *path) {
	if (!path || !*path) return false;
	char *p = strdup(path);
	if (!p) return false;
	size_t n = strlen(p);
	bool ok = true;
	for (size_t i = 1; i <= n && ok; i++) {
		if (i < n && !is_sep(p[i])) continue;
		char c = p[i];
		p[i] = 0;
#ifdef _WIN32
		if (!(i == 2 && p[1] == ':')) {
			wchar_t *w = wide(p);
			if (w && !CreateDirectoryW(w, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) ok = i < n;  // a parent we may not make (a share's root)
			free(w);
		}
#else
		if (mkdir(p, 0755) != 0 && errno != EEXIST) ok = i < n;
#endif
		p[i] = c;
	}
	free(p);
	return ok;
}

bool dsk_exists(const char *path) {
	if (!path || !*path) return false;
#ifdef _WIN32
	wchar_t *w = wide(path);
	bool ok = w && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
	free(w);
	return ok;
#else
	struct stat st;
	return stat(path, &st) == 0;
#endif
}

bool dsk_write_file(const char *path, const void *data, size_t len) {
	char *dir = dsk_dir(path);
	if (dir) dsk_mkdirs(dir);
	free(dir);
	size_t n = strlen(path);
	char *tmp = malloc(n + 32);
	if (!tmp) return false;
	snprintf(tmp, n + 32, "%s.%lu.tmp", path,
#ifdef _WIN32
		(unsigned long)GetCurrentProcessId()
#else
		(unsigned long)getpid()
#endif
	);
#ifdef _WIN32
	FILE *f = open_file(tmp, L"wb");
#else
	FILE *f = open_file(tmp, "wb");
#endif
	bool ok = f && fwrite(data, 1, len, f) == len;
	if (f && fclose(f) != 0) ok = false;
	if (ok) {
#ifdef _WIN32
		wchar_t *wf = wide(tmp), *wt = wide(path);
		ok = wf && wt && MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING);
		if (!ok && wf) DeleteFileW(wf);
		free(wf);
		free(wt);
#else
		ok = rename(tmp, path) == 0;
		if (!ok) unlink(tmp);
#endif
	}
	free(tmp);
	return ok;
}

char *dsk_getenv(const char *name) {
#ifdef _WIN32
	wchar_t *w = wide(name);
	if (!w) return NULL;
	DWORD n = GetEnvironmentVariableW(w, NULL, 0);
	char *out = NULL;
	if (n > 1) {
		wchar_t *v = malloc(n * sizeof *v);
		if (v && GetEnvironmentVariableW(w, v, n) > 0) out = narrow(v);
		free(v);
	}
	free(w);
	return out;
#else
	const char *v = getenv(name);
	return v && *v ? strdup(v) : NULL;
#endif
}

char *dsk_exe_path(void) {
#ifdef _WIN32
	wchar_t buf[32768];
	DWORD n = GetModuleFileNameW(NULL, buf, 32768);
	if (n == 0 || n >= 32768) return NULL;
	return narrow(buf);
#elif defined(__APPLE__)
	char buf[4096];
	uint32_t n = sizeof buf;
	if (_NSGetExecutablePath(buf, &n) != 0) return NULL;
	char *real = realpath(buf, NULL);
	return real ? real : strdup(buf);
#else
	char buf[4096];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
	if (n <= 0) return NULL;
	buf[n] = 0;
	return strdup(buf);
#endif
}
