// compress.c: std/compress's builtins, and what std/archive shares with
// it (compress.h): failures, files, and DEFLATE streams (flate.c's
// compressor and decompressor) framed as gzip and zlib, read and written
// a piece at a time.
//
//	compress.encodeBytes(data, format, level) -> bytes
//	compress.decodeBytes(data, format, limit) -> bytes?
//	compress.deflateFile(from, to, format, level) -> bool
//	compress.inflateFile(from, to, format, limit) -> bool
//	compress.failureKind() -> int / failureReason() / failurePath() -> string
//
// format is std/compress's Format (0 gzip, 1 zlib, 2 deflate); limit is
// the most bytes a decoding may make (bytes: at most 4 GB). The file
// calls stream: a file of any size takes a few hundred KB.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _FILE_OFFSET_BITS 64
#endif
#include "std/compress.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "std/flate.h"

#define STR(i) ((ak_str *)args[i].p)
#define BYTES(i) ((ak_bytes *)args[i].p)

// ---- Failures ----

static AK_TLS int fail_kind;
static AK_TLS char fail_reason[256];
static AK_TLS char fail_path[1024];

void ak_cz_reset(void) {
	fail_kind = 0;
	fail_reason[0] = fail_path[0] = 0;
}

bool ak_cz_failed(void) { return fail_kind != 0; }
int ak_cz_failure_kind_now(void) { return fail_kind; }

static void vfail(int kind, const char *path, const char *fmt, va_list ap) {
	if (fail_kind) return;
	fail_kind = kind;
	vsnprintf(fail_reason, sizeof fail_reason, fmt, ap);
	snprintf(fail_path, sizeof fail_path, "%s", path ? path : "");
}

void ak_cz_fail(int kind, const char *path, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfail(kind, path, fmt, ap);
	va_end(ap);
}

#if defined(_WIN32)
void ak_cz_fail_os(const char *path) {
	DWORD e = GetLastError();
	int kind;
	switch (e) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_INVALID_NAME:
		kind = AK_CZ_NOT_FOUND;
		break;
	case ERROR_ALREADY_EXISTS:
	case ERROR_FILE_EXISTS:
		kind = AK_CZ_EXISTS;
		break;
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
	case ERROR_PRIVILEGE_NOT_HELD:
		kind = AK_CZ_DENIED;
		break;
	case ERROR_DIRECTORY:
		kind = AK_CZ_NOT_DIR;
		break;
	case ERROR_DIR_NOT_EMPTY:
		kind = AK_CZ_NOT_EMPTY;
		break;
	default:
		kind = AK_CZ_OTHER;
	}
	char msg[160] = "";
	DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, msg, sizeof msg, NULL);
	while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == '\r' || msg[n - 1] == '.')) msg[--n] = 0;
	if (n == 0) snprintf(msg, sizeof msg, "error %lu", (unsigned long)e);
	ak_cz_fail(kind, path, "%s", msg);
}
#else
void ak_cz_fail_os(const char *path) {
	int e = errno;
	int kind;
	switch (e) {
	case ENOENT: kind = AK_CZ_NOT_FOUND; break;
	case EEXIST: kind = AK_CZ_EXISTS; break;
	case EACCES:
	case EPERM: kind = AK_CZ_DENIED; break;
	case ENOTDIR: kind = AK_CZ_NOT_DIR; break;
	case EISDIR: kind = AK_CZ_IS_DIR; break;
	case ENOTEMPTY: kind = AK_CZ_NOT_EMPTY; break;
	default: kind = AK_CZ_OTHER;
	}
	ak_cz_fail(kind, path, "%s", strerror(e));
}
#endif

ak_val ak_cz_failure_kind(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.i = fail_kind ? fail_kind : AK_CZ_OTHER};
}

ak_val ak_cz_failure_reason(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.p = ak_str_c(fail_reason[0] ? fail_reason : "failed")};
}

ak_val ak_cz_failure_path(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.p = ak_str_c(fail_path)};
}

// ---- Files ----

struct ak_cz_file {
#if defined(_WIN32)
	HANDLE h;
#else
	int fd;
#endif
	char path[];
};

static ak_cz_file *file_new(const char *path) {
	size_t n = strlen(path);
	ak_cz_file *f = ak_alloc(sizeof *f + n + 1);
	memcpy(f->path, path, n + 1);
	return f;
}

const char *ak_cz_path(const ak_cz_file *f) { return f->path; }

#if defined(_WIN32)

static wchar_t *wide(const char *p) {
	int n = MultiByteToWideChar(CP_UTF8, 0, p, -1, NULL, 0);
	if (n <= 0) n = 1;
	wchar_t *w = ak_alloc(sizeof(wchar_t) * (size_t)n);
	MultiByteToWideChar(CP_UTF8, 0, p, -1, w, n);
	return w;
}

ak_cz_file *ak_cz_open(const char *path) {
	wchar_t *w = wide(path);
	HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	ak_free(w);
	if (h == INVALID_HANDLE_VALUE) {
		ak_cz_fail_os(path);
		return NULL;
	}
	ak_cz_file *f = file_new(path);
	f->h = h;
	return f;
}

static bool same_file(HANDLE a, HANDLE b) {
	BY_HANDLE_FILE_INFORMATION x, y;
	if (!GetFileInformationByHandle(a, &x) || !GetFileInformationByHandle(b, &y)) return false;
	return x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
}

ak_cz_file *ak_cz_create(const char *path, int mode, const ak_cz_file *same) {
	AK_UNUSED(mode);
	wchar_t *w = wide(path);
	HANDLE h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	ak_free(w);
	if (h == INVALID_HANDLE_VALUE) {
		ak_cz_fail_os(path);
		return NULL;
	}
	if (same && same_file(h, same->h)) {
		CloseHandle(h);
		ak_cz_fail(AK_CZ_OTHER, path, "it's the file being read");
		return NULL;
	}
	ak_cz_file *f = file_new(path);
	f->h = h;
	if (!ak_cz_truncate(f, 0)) {
		ak_cz_close(f);
		return NULL;
	}
	return f;
}

int64_t ak_cz_read(ak_cz_file *f, void *p, size_t n) {
	DWORD got;
	if (n > (1u << 30)) n = 1u << 30;
	if (!ReadFile(f->h, p, (DWORD)n, &got, NULL)) {
		ak_cz_fail_os(f->path);
		return -1;
	}
	return got;
}

bool ak_cz_write(ak_cz_file *f, const void *p, size_t n) {
	const uint8_t *b = p;
	while (n > 0) {
		DWORD put, chunk = n > (1u << 30) ? (1u << 30) : (DWORD)n;
		if (!WriteFile(f->h, b, chunk, &put, NULL)) {
			ak_cz_fail_os(f->path);
			return false;
		}
		b += put, n -= put;
	}
	return true;
}

bool ak_cz_seek(ak_cz_file *f, int64_t off) {
	LARGE_INTEGER li;
	li.QuadPart = off;
	if (!SetFilePointerEx(f->h, li, NULL, FILE_BEGIN)) {
		ak_cz_fail_os(f->path);
		return false;
	}
	return true;
}

int64_t ak_cz_size(ak_cz_file *f) {
	LARGE_INTEGER li;
	if (!GetFileSizeEx(f->h, &li)) {
		ak_cz_fail_os(f->path);
		return -1;
	}
	return li.QuadPart;
}

bool ak_cz_truncate(ak_cz_file *f, int64_t len) {
	if (!ak_cz_seek(f, len)) return false;
	if (!SetEndOfFile(f->h)) {
		ak_cz_fail_os(f->path);
		return false;
	}
	return true;
}

void ak_cz_set_modified(ak_cz_file *f, int64_t unix_ns) {
	uint64_t t = (uint64_t)(unix_ns / 100 + 116444736000000000ll);
	FILETIME ft = {.dwLowDateTime = (DWORD)t, .dwHighDateTime = (DWORD)(t >> 32)};
	SetFileTime(f->h, NULL, NULL, &ft);
}

bool ak_cz_close(ak_cz_file *f) {
	if (!f) return true;
	bool ok = CloseHandle(f->h) != 0;
	if (!ok) ak_cz_fail_os(f->path);
	ak_free(f);
	return ok;
}

void ak_cz_remove(const char *path) {
	wchar_t *w = wide(path);
	DeleteFileW(w);
	ak_free(w);
}

#else

#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

ak_cz_file *ak_cz_open(const char *path) {
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ak_cz_fail_os(path);
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
		close(fd);
		ak_cz_fail(AK_CZ_IS_DIR, path, "is a directory");
		return NULL;
	}
	ak_cz_file *f = file_new(path);
	f->fd = fd;
	return f;
}

ak_cz_file *ak_cz_create(const char *path, int mode, const ak_cz_file *same) {
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, (mode_t)(mode ? mode : 0644));
	if (fd < 0) {
		ak_cz_fail_os(path);
		return NULL;
	}
	struct stat a, b;
	if (fstat(fd, &a) == 0 && S_ISDIR(a.st_mode)) {
		close(fd);
		ak_cz_fail(AK_CZ_IS_DIR, path, "is a directory");
		return NULL;
	}
	if (same && fstat(same->fd, &b) == 0 && fstat(fd, &a) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
		close(fd);
		ak_cz_fail(AK_CZ_OTHER, path, "it's the file being read");
		return NULL;
	}
	ak_cz_file *f = file_new(path);
	f->fd = fd;
	if (!ak_cz_truncate(f, 0)) {
		ak_cz_close(f);
		return NULL;
	}
	return f;
}

int64_t ak_cz_read(ak_cz_file *f, void *p, size_t n) {
	if (n > (1u << 30)) n = 1u << 30;
	for (;;) {
		ssize_t got = read(f->fd, p, n);
		if (got >= 0) return got;
		if (errno == EINTR) continue;
		ak_cz_fail_os(f->path);
		return -1;
	}
}

bool ak_cz_write(ak_cz_file *f, const void *p, size_t n) {
	const uint8_t *b = p;
	while (n > 0) {
		ssize_t put = write(f->fd, b, n > (1u << 30) ? (1u << 30) : n);
		if (put < 0) {
			if (errno == EINTR) continue;
			ak_cz_fail_os(f->path);
			return false;
		}
		b += put, n -= (size_t)put;
	}
	return true;
}

bool ak_cz_seek(ak_cz_file *f, int64_t off) {
	if (lseek(f->fd, (off_t)off, SEEK_SET) < 0) {
		ak_cz_fail_os(f->path);
		return false;
	}
	return true;
}

int64_t ak_cz_size(ak_cz_file *f) {
	struct stat st;
	if (fstat(f->fd, &st) != 0) {
		ak_cz_fail_os(f->path);
		return -1;
	}
	return (int64_t)st.st_size;
}

bool ak_cz_truncate(ak_cz_file *f, int64_t len) {
	if (ftruncate(f->fd, (off_t)len) != 0) {
		ak_cz_fail_os(f->path);
		return false;
	}
	return ak_cz_seek(f, len);
}

void ak_cz_set_modified(ak_cz_file *f, int64_t unix_ns) {
	int64_t s = unix_ns / 1000000000, ns = unix_ns % 1000000000;
	if (ns < 0) s--, ns += 1000000000;
	struct timespec ts[2] = {{.tv_sec = 0, .tv_nsec = UTIME_OMIT}, {.tv_sec = (time_t)s, .tv_nsec = (long)ns}};
	(void)futimens(f->fd, ts);
}

bool ak_cz_close(ak_cz_file *f) {
	if (!f) return true;
	bool ok = close(f->fd) == 0;
	if (!ok) ak_cz_fail_os(f->path);
	ak_free(f);
	return ok;
}

void ak_cz_remove(const char *path) { (void)unlink(path); }

#endif

bool ak_cz_read_full(ak_cz_file *f, void *p, size_t n, const char *what) {
	uint8_t *b = p;
	while (n > 0) {
		int64_t got = ak_cz_read(f, b, n);
		if (got < 0) return false;
		if (got == 0) {
			ak_cz_fail(AK_CZ_CORRUPT, f->path, "%s ends too soon", what);
			return false;
		}
		b += got, n -= (size_t)got;
	}
	return true;
}

// ---- Sources ----

size_t ak_cz_src_read(ak_cz_src *s, uint8_t *p, size_t n) {
	if (!s->f) {
		size_t k = s->len - s->pos < n ? s->len - s->pos : n;
		memcpy(p, s->mem + s->pos, k);
		s->pos += k;
		return k;
	}
	if (s->left >= 0 && (int64_t)n > s->left) n = (size_t)s->left;
	if (n == 0) return 0;
	int64_t got = ak_cz_read(s->f, p, n);
	if (got <= 0) return 0;
	if (s->left > 0) s->left -= got;
	return (size_t)got;
}

// ---- Inflating ----
//
// The stream's framing (gzip's members, zlib's header and checksum) is
// here; DEFLATE is flate.c's. A source in memory is read where it is; a
// file's through a buffer. What it decompresses to goes into a window
// (32 KB of history and room after it) that reads empty, or, decoding
// bytes whole, into the result itself, grown as it fills.

enum { IN_SIZE = 65536, OB_SIZE = 256 * 1024, HIST = 32768 };
enum { Z_HEADER, Z_BODY, Z_TRAILER, Z_END, Z_FAILED };

struct ak_cz_inflate {
	ak_inflater *d;
	ak_cz_src *src;
	int format, state;
	int64_t limit;
	uint64_t total;   // bytes made
	uint32_t check;   // the member's CRC-32 (gzip) or the stream's Adler-32 (zlib) so far
	uint32_t member;  // gzip: the member's bytes (mod 2^32)
	const uint8_t *in;  // the input: in[in_pos..in_len) not read yet
	size_t in_len, in_pos;
	bool eof;           // the source has nothing more
	uint8_t *inbuf;     // a file's: IN_SIZE bytes
	uint8_t *ob;        // the output: ob[ob_start..ob_pos) this stream's, ob[ob_read..ob_pos) not read yet
	size_t ob_cap, ob_pos, ob_start, ob_read;
	ak_bytes *whole;    // decoding whole: ob is its data
	bool into;          // decoding into the caller's ob: it neither grows nor moves
};

static const char *where(const ak_cz_inflate *z) { return z->src->f ? z->src->f->path : ""; }

// z_fail fails z as corrupt (unless it failed already, reading); false.
static bool z_fail(ak_cz_inflate *z, const char *msg) {
	if (!ak_cz_failed()) ak_cz_fail(AK_CZ_CORRUPT, where(z), "%s", msg);
	z->state = Z_FAILED;
	return false;
}

// refill reads more of a file into the input, keeping what's unread and
// the AK_FL_IN_KEEP bytes before it.
static void refill(ak_cz_inflate *z) {
	if (z->eof) return;
	if (!z->inbuf) {
		z->eof = true;
		return;
	}
	size_t keep = z->in_pos < AK_FL_IN_KEEP ? z->in_pos : AK_FL_IN_KEEP, from = z->in_pos - keep;
	if (from) {
		memmove(z->inbuf, z->inbuf + from, z->in_len - from);
		z->in_len -= from, z->in_pos -= from;
	}
	size_t got = ak_cz_src_read(z->src, z->inbuf + z->in_len, IN_SIZE - z->in_len);
	z->in_len += got;
	if (got == 0) z->eof = true;
}

// in_byte is the input's next byte; -1 at its end.
static int in_byte(ak_cz_inflate *z) {
	if (z->in_pos == z->in_len) refill(z);
	if (z->in_pos == z->in_len) return -1;
	return z->in[z->in_pos++];
}

static const char *const too_soon = "the data ends too soon";

static void begin_body(ak_cz_inflate *z) {
	ak_inflater_reset(z->d);
	z->ob_start = z->ob_pos;
	z->state = Z_BODY;
}

// gzip_header reads a member's header (RFC 1952 2.3), its magic read;
// false (failed) when it's broken.
static bool gzip_header(ak_cz_inflate *z) {
	uint8_t h[8];
	for (int i = 0; i < 8; i++) {
		int c = in_byte(z);
		if (c < 0) return z_fail(z, too_soon);
		h[i] = (uint8_t)c;
	}
	if (h[0] != 8) return z_fail(z, "not DEFLATE: gzip's method isn't 8");
	int flg = h[1];
	if (flg & 0xe0) return z_fail(z, "a gzip header's reserved flags are set");
	if (flg & 4) {  // FEXTRA
		int a = in_byte(z), b = in_byte(z);
		if (a < 0 || b < 0) return z_fail(z, too_soon);
		for (int n = a | b << 8; n > 0; n--)
			if (in_byte(z) < 0) return z_fail(z, too_soon);
	}
	for (int bit = 8; bit <= 16; bit <<= 1) {  // FNAME, FCOMMENT
		if (!(flg & bit)) continue;
		int c;
		while ((c = in_byte(z)) > 0) {}
		if (c < 0) return z_fail(z, too_soon);
	}
	if (flg & 2)  // FHCRC
		for (int i = 0; i < 2; i++)
			if (in_byte(z) < 0) return z_fail(z, too_soon);
	z->check = 0;
	z->member = 0;
	begin_body(z);
	return true;
}

static bool magic(ak_cz_inflate *z) {
	int a = in_byte(z), b = in_byte(z);
	if (a == 0x1f && b == 0x8b) return true;
	return z_fail(z, a < 0 || b < 0 ? too_soon : "not gzip: its first bytes aren't 1f 8b");
}

// zlib_header reads zlib's two bytes (RFC 1950 2.2): DEFLATE, a window
// of at most 32 KB, no preset dictionary.
static bool zlib_header(ak_cz_inflate *z) {
	int cmf = in_byte(z), flg = in_byte(z);
	if (cmf < 0 || flg < 0) return z_fail(z, too_soon);
	if ((cmf & 15) != 8 || cmf >> 4 > 7 || (cmf << 8 | flg) % 31 != 0) return z_fail(z, "not zlib, or not DEFLATE");
	if (flg & 0x20) return z_fail(z, "zlib with a preset dictionary");
	z->check = 1;
	begin_body(z);
	return true;
}

// trailer checks a gzip member's CRC-32 and size, then reads the next
// member if one follows (zeros after the last are padding); or zlib's
// Adler-32.
static bool trailer(ak_cz_inflate *z) {
	if (z->format == AK_CZ_ZLIB) {
		uint32_t a = 0;
		for (int i = 0; i < 4; i++) {
			int c = in_byte(z);
			if (c < 0) return z_fail(z, too_soon);
			a = a << 8 | (uint32_t)c;
		}
		if (a != z->check) return z_fail(z, "zlib's checksum doesn't match");
		z->state = Z_END;
		return true;
	}
	uint8_t t[8];
	for (int i = 0; i < 8; i++) {
		int c = in_byte(z);
		if (c < 0) return z_fail(z, too_soon);
		t[i] = (uint8_t)c;
	}
	uint32_t crc = (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16 | (uint32_t)t[3] << 24;
	uint32_t size = (uint32_t)t[4] | (uint32_t)t[5] << 8 | (uint32_t)t[6] << 16 | (uint32_t)t[7] << 24;
	if (crc != z->check) return z_fail(z, "gzip's checksum doesn't match");
	if (size != z->member) return z_fail(z, "gzip's size doesn't match");
	int c = in_byte(z);
	if (c == 0x1f) {
		if (in_byte(z) != 0x8b) return z_fail(z, "data after the gzip stream");
		z->state = Z_HEADER;
		return true;
	}
	while (c == 0) c = in_byte(z);
	if (c >= 0) return z_fail(z, "data after the gzip stream");
	z->state = Z_END;
	return true;
}

ak_cz_inflate *ak_cz_inflate_new(ak_cz_src *src, int format, int64_t limit) {
	ak_cz_inflate *z = ak_alloc(sizeof *z);
	z->d = ak_inflater_new();
	z->src = src;
	z->format = format;
	z->limit = limit;
	if (src->f) {
		z->inbuf = ak_alloc(IN_SIZE);
		z->in = z->inbuf;
	} else {
		z->in = src->mem;
		z->in_len = src->len;
		z->in_pos = src->pos;
		z->eof = true;
	}
	if (format == AK_CZ_GZIP) {
		if (magic(z)) z->state = Z_HEADER;
	} else if (format == AK_CZ_ZLIB) {
		z->state = Z_HEADER;
	} else {
		begin_body(z);
	}
	return z;
}

void ak_cz_inflate_free(ak_cz_inflate *z) {
	if (!z) return;
	ak_inflater_free(z->d);
	ak_free(z->inbuf);
	if (z->whole) ak_release(z->whole);
	else if (!z->into) ak_free(z->ob);
	ak_free(z);
}

bool ak_cz_inflate_done(const ak_cz_inflate *z) { return z->state == Z_END && z->ob_read == z->ob_pos; }
uint64_t ak_cz_inflate_total(const ak_cz_inflate *z) { return z->total; }

// room makes room in the output for the decoder to go on: the window
// moved down to its last 32 KB (all of it read), or the result grown
// (up to the limit, and a little past it to see it's passed).
static bool room(ak_cz_inflate *z) {
	if (z->ob_cap - z->ob_pos >= AK_FL_OUT_SLACK) return true;
	if (z->into) {
		ak_cz_fail(AK_CZ_TOO_LARGE, where(z), "it decompresses to more than %zu bytes", z->ob_cap - AK_FL_OUT_SLACK);
		z->state = Z_FAILED;
		return false;
	}
	if (z->whole) {
		uint64_t want = z->ob_cap, left = z->limit >= 0 ? (uint64_t)z->limit - z->total + 1 : UINT64_MAX;
		if (want > left) want = left;
		if (want < AK_FL_OUT_SLACK) want = AK_FL_OUT_SLACK;
		uint64_t cap = (uint64_t)z->ob_pos + want;
		if (cap > SIZE_MAX - sizeof(ak_bytes)) return z_fail(z, "too large");
		z->whole = ak_realloc(z->whole, sizeof(ak_bytes) + (size_t)cap);
		z->ob = z->whole->data;
		z->ob_cap = (size_t)cap;
		return true;
	}
	if (!z->ob) {
		z->ob = ak_alloc(OB_SIZE);
		z->ob_cap = OB_SIZE;
		return true;
	}
	size_t shift = z->ob_pos > HIST ? z->ob_pos - HIST : 0;
	memmove(z->ob, z->ob + shift, z->ob_pos - shift);
	z->ob_pos -= shift, z->ob_read -= shift;
	z->ob_start = z->ob_start > shift ? z->ob_start - shift : 0;
	return true;
}

static void body(ak_cz_inflate *z) {
	if (!room(z)) return;
	ak_fl_io io = {
		.in = z->in, .in_len = z->in_len, .in_pos = z->in_pos, .eof = z->eof,
		.out = z->ob, .start = z->ob_start, .pos = z->ob_pos, .cap = z->ob_cap,
	};
	size_t before = z->ob_pos;
	int r = ak_inflater_run(z->d, &io);
	z->in_pos = io.in_pos;
	z->ob_pos = io.pos;
	size_t made = z->ob_pos - before;
	if (made) {
		z->total += made;
		if (z->limit >= 0 && z->total > (uint64_t)z->limit) {
			ak_cz_fail(AK_CZ_TOO_LARGE, where(z), "it decompresses to more than %lld bytes", (long long)z->limit);
			z->state = Z_FAILED;
			z->ob_pos = z->ob_read = before;
			return;
		}
		if (z->format == AK_CZ_GZIP) {
			z->check = ak_cz_crc32(z->check, z->ob + before, made);
			z->member += (uint32_t)made;
		} else if (z->format == AK_CZ_ZLIB) {
			z->check = ak_adler32(z->check, z->ob + before, made);
		}
	}
	switch (r) {
	case AK_FL_NEED_INPUT:
		refill(z);
		if (ak_cz_failed()) z_fail(z, "");
		break;
	case AK_FL_NEED_OUTPUT:
		break;
	case AK_FL_DONE:
		z->state = z->format == AK_CZ_RAW ? Z_END : Z_TRAILER;
		break;
	default:
		z_fail(z, ak_inflater_error(z->d));
	}
}

// step moves z on: a header, the body (some of it), a trailer.
static bool step(ak_cz_inflate *z) {
	switch (z->state) {
	case Z_HEADER:
		if (z->format == AK_CZ_GZIP) gzip_header(z);
		else zlib_header(z);
		return true;
	case Z_BODY: body(z); return true;
	case Z_TRAILER: trailer(z); return true;
	default: return false;
	}
}

size_t ak_cz_inflate_read(ak_cz_inflate *z, uint8_t *p, size_t n) {
	size_t k = 0;
	while (k < n) {
		if (z->ob_read < z->ob_pos) {
			size_t m = z->ob_pos - z->ob_read < n - k ? z->ob_pos - z->ob_read : n - k;
			memcpy(p + k, z->ob + z->ob_read, m);
			z->ob_read += m, k += m;
			continue;
		}
		if (!step(z)) break;
	}
	return k;
}

int64_t ak_cz_inflate_into(ak_cz_inflate *z, uint8_t *dst, size_t cap) {
	if (z->ob || cap < AK_FL_OUT_SLACK) return -1;
	z->into = true;
	z->ob = dst, z->ob_cap = cap;
	while (z->state != Z_END && z->state != Z_FAILED) {
		step(z);
		z->ob_read = z->ob_pos;
	}
	return z->state == Z_END ? (int64_t)z->ob_pos : -1;
}

// inflate_whole decodes all of z's source into bytes (its size guessed
// from gzip's trailer, or the input's); NULL when it failed.
static ak_bytes *inflate_whole(ak_cz_inflate *z, size_t in_len) {
	uint64_t guess = (uint64_t)in_len * 4;
	if (z->format == AK_CZ_GZIP && in_len >= 18) {
		const uint8_t *t = z->in + z->in_len - 4;
		// The last member's size: a hint only, at most what DEFLATE can
		// make of the input (1032 to 1).
		uint64_t isize = (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16 | (uint32_t)t[3] << 24;
		if (isize <= (uint64_t)in_len * 1032) guess = isize;
	}
	if (z->limit >= 0 && guess > (uint64_t)z->limit) guess = (uint64_t)z->limit;
	if (guess > (1u << 30)) guess = 1u << 30;
	size_t cap = (size_t)guess + AK_FL_OUT_SLACK + 1;
	z->whole = ak_bytes_new(NULL, cap);
	z->ob = z->whole->data;
	z->ob_cap = cap;
	while (z->state != Z_END && z->state != Z_FAILED) {
		step(z);
		z->ob_read = z->ob_pos;
	}
	if (z->state != Z_END) return NULL;
	ak_bytes *out = z->whole;
	z->whole = NULL, z->ob = NULL;
	out = ak_realloc(out, sizeof(ak_bytes) + (z->ob_pos ? z->ob_pos : 1));
	out->len = (uint32_t)z->ob_pos;
	return out;
}

// ---- Sinks ----

static const char *sink_path(const ak_cz_sink *s) { return s->f ? s->f->path : ""; }

void ak_cz_sink_reserve(ak_cz_sink *s, size_t n) {
	if (s->f || (s->obj && s->len + n <= s->obj->len)) return;
	size_t cap = s->obj ? (size_t)s->obj->len * 2 : 4096;
	if (cap < s->len + n) cap = s->len + n;
	if (cap > UINT32_MAX) cap = UINT32_MAX;
	if (!s->obj) {
		s->obj = ak_bytes_new(NULL, cap);
	} else {
		s->obj = ak_realloc(s->obj, sizeof(ak_bytes) + cap);
		s->obj->len = (uint32_t)cap;
	}
}

bool ak_cz_sink_write(ak_cz_sink *s, const void *p, size_t n) {
	if (s->f) {
		if (!ak_cz_write(s->f, p, n)) return false;
	} else {
		if (s->len + n > UINT32_MAX) {
			ak_cz_fail(AK_CZ_TOO_LARGE, "", "the result would be larger than 4 GB, more than bytes hold");
			return false;
		}
		ak_cz_sink_reserve(s, n);
		if (n) memcpy(s->obj->data + s->len, p, n);
		s->len += n;
	}
	s->pos += (int64_t)n;
	return true;
}

bool ak_cz_sink_patch(ak_cz_sink *s, int64_t off, const void *p, size_t n) {
	if (!s->f) {
		memcpy(s->obj->data + off, p, n);
		return true;
	}
	return ak_cz_seek(s->f, off) && ak_cz_write(s->f, p, n) && ak_cz_seek(s->f, s->pos);
}

bool ak_cz_sink_truncate(ak_cz_sink *s, int64_t len) {
	if (s->f && !ak_cz_truncate(s->f, len)) return false;
	if (!s->f) s->len = (size_t)len;
	s->pos = len;
	return true;
}

ak_bytes *ak_cz_sink_bytes(ak_cz_sink *s) {
	ak_bytes *b = s->obj;
	if (!b) return ak_bytes_new(NULL, 0);
	if (s->len + s->len / 8 + 4096 < b->len) b = ak_realloc(b, sizeof(ak_bytes) + (s->len ? s->len : 1));
	b->len = (uint32_t)s->len;
	s->obj = NULL, s->len = 0;
	return b;
}

void ak_cz_sink_free(ak_cz_sink *s) {
	if (s->obj) ak_release(s->obj);
	s->obj = NULL, s->len = 0;
}

// ---- Deflating ----

struct ak_cz_deflate {
	ak_deflater *d;
	ak_cz_sink *to;
	int format;
	uint32_t check, size;
	bool ok;
};

static bool put(void *user, const uint8_t *p, size_t len) {
	ak_cz_deflate *z = user;
	z->ok = z->ok && ak_cz_sink_write(z->to, p, len);
	return z->ok;
}

ak_cz_deflate *ak_cz_deflate_new(ak_cz_sink *to, int format, int level) {
	if (level < 0) level = 0;
	if (level > 9) level = 9;
	ak_cz_deflate *z = ak_alloc(sizeof *z);
	z->to = to;
	z->format = format;
	z->ok = true;
	z->d = ak_deflater_new(level, put, z);
	if (format == AK_CZ_GZIP) {
		// No name and no time (gzip -n's), so the same bytes make the
		// same file; XFL says the level, OS "unknown" (RFC 1952).
		uint8_t h[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, level == 9 ? 2 : level == 1 ? 4 : 0, 255};
		z->ok = ak_cz_sink_write(to, h, sizeof h);
	} else if (format == AK_CZ_ZLIB) {
		// A 32 KB window; FLEVEL as zlib's: fastest, fast, default, best.
		unsigned cmf = 0x78, flevel = level < 2 ? 0 : level < 6 ? 1 : level == 6 ? 2 : 3;
		unsigned flg = flevel << 6;
		flg += 31 - (cmf << 8 | flg) % 31;
		uint8_t h[2] = {(uint8_t)cmf, (uint8_t)flg};
		z->check = 1;
		z->ok = ak_cz_sink_write(to, h, sizeof h);
	}
	return z;
}

bool ak_cz_deflate_write(ak_cz_deflate *z, const void *p, size_t n) {
	if (!z->ok || n == 0) return z->ok;
	if (z->format == AK_CZ_GZIP) {
		z->check = ak_cz_crc32(z->check, p, n);
		z->size += (uint32_t)n;
	} else if (z->format == AK_CZ_ZLIB) {
		z->check = ak_adler32(z->check, p, n);
	}
	ak_deflater_write(z->d, p, n);
	if (!z->ok && !ak_cz_failed()) ak_cz_fail(AK_CZ_OTHER, sink_path(z->to), "compressing failed");
	return z->ok;
}

bool ak_cz_deflate_finish(ak_cz_deflate *z) {
	if (z->ok) ak_deflater_finish(z->d);
	if (z->ok && z->format == AK_CZ_GZIP) {
		uint8_t t[8] = {(uint8_t)z->check, (uint8_t)(z->check >> 8), (uint8_t)(z->check >> 16), (uint8_t)(z->check >> 24),
		                (uint8_t)z->size, (uint8_t)(z->size >> 8), (uint8_t)(z->size >> 16), (uint8_t)(z->size >> 24)};
		z->ok = ak_cz_sink_write(z->to, t, sizeof t);
	} else if (z->ok && z->format == AK_CZ_ZLIB) {
		uint8_t t[4] = {(uint8_t)(z->check >> 24), (uint8_t)(z->check >> 16), (uint8_t)(z->check >> 8), (uint8_t)z->check};
		z->ok = ak_cz_sink_write(z->to, t, sizeof t);
	}
	if (!z->ok && !ak_cz_failed()) ak_cz_fail(AK_CZ_OTHER, sink_path(z->to), "compressing failed");
	return z->ok;
}

void ak_cz_deflate_free(ak_cz_deflate *z) {
	if (!z) return;
	ak_deflater_free(z->d);
	ak_free(z);
}

uint32_t ak_cz_crc32(uint32_t crc, const void *p, size_t n) { return ak_crc32(crc, p, n); }

// ---- The builtins ----

static int format_of(int64_t f) { return f >= AK_CZ_GZIP && f <= AK_CZ_RAW ? (int)f : AK_CZ_GZIP; }

static ak_val encode_bytes(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_cz_reset();
	const ak_bytes *data = BYTES(0);
	ak_cz_sink s = {0};
	ak_cz_deflate *z = ak_cz_deflate_new(&s, format_of(args[1].i), (int)args[2].i);
	bool ok = ak_cz_deflate_write(z, data ? data->data : NULL, data ? data->len : 0) && ak_cz_deflate_finish(z);
	ak_cz_deflate_free(z);
	if (!ok) {
		ak_cz_sink_free(&s);
		ak_throw_msg("compress: %s", fail_reason);
		return (ak_val){.p = NULL};
	}
	return (ak_val){.p = ak_cz_sink_bytes(&s)};
}

static int64_t mem_limit(int64_t limit) { return limit < 0 || limit > UINT32_MAX ? UINT32_MAX : limit; }

static ak_val decode_bytes(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_cz_reset();
	const ak_bytes *data = BYTES(0);
	ak_cz_src src = {.mem = data ? data->data : NULL, .len = data ? data->len : 0};
	ak_cz_inflate *z = ak_cz_inflate_new(&src, format_of(args[1].i), mem_limit(args[2].i));
	ak_bytes *out = z->state == Z_FAILED ? NULL : inflate_whole(z, src.len);
	ak_cz_inflate_free(z);
	return (ak_val){.p = out};
}

enum { CHUNK = 1 << 16 };

static ak_val deflate_file(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_cz_reset();
	const char *from = STR(0)->data, *to = STR(1)->data;
	ak_cz_file *in = ak_cz_open(from);
	if (!in) return (ak_val){.i = 0};
	ak_cz_sink s = {.f = ak_cz_create(to, 0644, in)};
	if (!s.f) {
		ak_cz_close(in);
		return (ak_val){.i = 0};
	}
	ak_cz_deflate *z = ak_cz_deflate_new(&s, format_of(args[2].i), (int)args[3].i);
	uint8_t *buf = ak_alloc(CHUNK);
	bool ok = true;
	for (;;) {
		int64_t got = ak_cz_read(in, buf, CHUNK);
		if (got < 0) ok = false;
		if (got <= 0) break;
		if (!(ok = ak_cz_deflate_write(z, buf, (size_t)got))) break;
	}
	ok = ok && ak_cz_deflate_finish(z);
	ak_free(buf);
	ak_cz_deflate_free(z);
	ak_cz_close(in);
	ok = ak_cz_close(s.f) && ok;
	if (!ok) ak_cz_remove(to);
	return (ak_val){.i = ok};
}

static ak_val inflate_file(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_cz_reset();
	const char *from = STR(0)->data, *to = STR(1)->data;
	ak_cz_file *in = ak_cz_open(from);
	if (!in) return (ak_val){.i = 0};
	ak_cz_file *out = ak_cz_create(to, 0644, in);
	if (!out) {
		ak_cz_close(in);
		return (ak_val){.i = 0};
	}
	ak_cz_src src = {.f = in, .left = -1};
	ak_cz_inflate *z = ak_cz_inflate_new(&src, format_of(args[2].i), args[3].i < 0 ? -1 : args[3].i);
	uint8_t *buf = ak_alloc(CHUNK);
	bool ok = true;
	for (;;) {
		size_t got = ak_cz_inflate_read(z, buf, CHUNK);
		if (got == 0) break;
		if (!(ok = ak_cz_write(out, buf, got))) break;
	}
	ok = ok && ak_cz_inflate_done(z);
	ak_free(buf);
	ak_cz_inflate_free(z);
	ak_cz_close(in);
	ok = ak_cz_close(out) && ok;
	if (!ok) ak_cz_remove(to);
	return (ak_val){.i = ok};
}

const ak_lib_entry ak_std_compress[] = {
	{"compress.encodeBytes", encode_bytes},
	{"compress.decodeBytes", decode_bytes},
	{"compress.deflateFile", deflate_file},
	{"compress.inflateFile", inflate_file},
	{"compress.failureKind", ak_cz_failure_kind},
	{"compress.failureReason", ak_cz_failure_reason},
	{"compress.failurePath", ak_cz_failure_path},
	{NULL, NULL},
};
