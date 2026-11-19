// std/compress's and std/archive's C (core/src/std/compress.c,
// archive.c): what the Asuka tests (std/compress, std/archive) reach less
// easily: streams read a byte at a time, gzip's optional header fields,
// links already in a destination, the permissions extracting keeps.
#define _POSIX_C_SOURCE 200809L
#include "t.h"
#include "std/compress.h"
#include "std/flate.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

static ak_val S(const char *s) { return (ak_val){.p = ak_str_c(s)}; }
static ak_val I(int64_t i) { return (ak_val){.i = i}; }
static ak_val B(const void *p, size_t n) { return (ak_val){.p = ak_bytes_new(p, n)}; }

// call calls a builtin with args, releasing the references among them
// (refs: a bit per argument).
static ak_val call(const char *name, ak_val *args, uint32_t n, uint32_t refs) {
	ak_val r = fn(name)(args, n);
	for (uint32_t i = 0; i < n; i++)
		if (refs >> i & 1) ak_release(args[i].p);
	return r;
}

static uint8_t *sample(size_t n) {
	uint8_t *p = malloc(n);
	uint32_t x = 1;
	for (size_t i = 0; i < n; i++) {
		x = x * 1103515245 + 12345;
		p[i] = i % 3000 < 2000 ? (uint8_t)('a' + i % 7) : (uint8_t)(x >> 16);
	}
	return p;
}

// Each format, read back a byte at a time and in odd pieces: what's
// pending in the window survives reads of any size.
TEST(inflate_in_pieces) {
	size_t n = 200000;
	uint8_t *data = sample(n);
	for (int format = AK_CZ_GZIP; format <= AK_CZ_RAW; format++) {
		ak_bytes *packed = call("compress.encodeBytes", (ak_val[]){B(data, n), I(format), I(6)}, 3, 1).p;
		REQUIRE(packed);
		for (size_t piece = 1; piece < 70000; piece = piece * 7 + 3) {
			ak_cz_reset();
			ak_cz_src src = {.mem = packed->data, .len = packed->len};
			ak_cz_inflate *z = ak_cz_inflate_new(&src, format, -1);
			uint8_t *out = malloc(n + piece);
			size_t k = 0, got;
			while ((got = ak_cz_inflate_read(z, out + k, piece)) > 0) k += got;
			CHECK(ak_cz_inflate_done(z));
			CHECK_EQ_INT(k, n);
			CHECK(memcmp(out, data, n) == 0);
			CHECK_EQ_INT(ak_cz_inflate_total(z), n);
			ak_cz_inflate_free(z);
			free(out);
		}
		ak_release(packed);
	}
	free(data);
}

// ---- DEFLATE (flate.c) ----

// lcg: the next of a sequence of pseudo-random numbers.
static uint32_t lcg(uint32_t *x) { return (*x = *x * 1103515245 + 12345) >> 16; }

// inputs fills in[k] (n bytes) with kind k: text from a few words, bytes
// that don't compress, one byte over and over, runs longer than a match
// (258) and further apart than the window (32 KB), and a mix of them all.
enum { KINDS = 5 };
static uint8_t *input(int kind, size_t n) {
	static const char *const words[] = {"ka", "to", "ri", "su", "me", "no", "ha", "yu", "chi", "ra", " ", "\n"};
	uint8_t *p = malloc(n + 1);
	uint32_t x = (uint32_t)kind + 7;
	for (size_t i = 0; i < n;) {
		int k = kind == 4 ? (int)(i / 50000 % 4) : kind;
		if (k == 0) {
			const char *w = words[lcg(&x) % 12];
			for (; *w && i < n; w++) p[i++] = (uint8_t)*w;
		} else if (k == 1) {
			p[i++] = (uint8_t)lcg(&x);
		} else if (k == 2) {
			p[i++] = 'z';
		} else {
			// A run of 300 to 2000 copies of a byte, then 40 KB before it again.
			size_t run = 300 + lcg(&x) % 1700;
			uint8_t c = (uint8_t)(i / 40000);
			for (size_t j = 0; j < run && i < n; j++) p[i++] = c;
			for (size_t j = 0; j < 100 && i < n; j++) p[i++] = (uint8_t)lcg(&x);
		}
	}
	return p;
}

typedef struct {
	uint8_t *p;
	size_t n;
} sink;

static bool sink_put(void *user, const uint8_t *p, size_t n) {
	sink *s = user;
	s->p = realloc(s->p, s->n + n + 1);
	memcpy(s->p + s->n, p, n);
	s->n += n;
	return true;
}

// deflate_pieces compresses p at level, written piece bytes at a time.
static sink deflate_pieces(const uint8_t *p, size_t n, int level, size_t piece) {
	sink s = {0};
	ak_deflater *d = ak_deflater_new(level, sink_put, &s);
	for (size_t at = 0; at < n; at += piece) ak_deflater_write(d, p + at, n - at < piece ? n - at : piece);
	ak_deflater_finish(d);
	ak_deflater_free(d);
	return s;
}

// inflate_whole decodes s; SIZE_MAX if it's broken.
static size_t inflate_whole(const uint8_t *s, size_t n, uint8_t *out, size_t cap) { return ak_inflate_mem(out, cap, s, n); }

// Every level round-trips every kind of input, at sizes around the
// block's and the window's, written in pieces of any size; a level
// compresses text no worse than the one below it does by much, and 9 no
// worse than 1.
TEST(deflate_levels_round_trip) {
	static const size_t sizes[] = {0, 1, 3, 12, 13, 300, 65535, 65536, 70000, 300000, 700000};
	for (int kind = 0; kind < KINDS; kind++) {
		for (size_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
			size_t n = sizes[si];
			if (n > 70000 && kind == 1 && si % 2) continue;
			uint8_t *p = input(kind, n);
			uint8_t *out = malloc(n + AK_FL_OUT_SLACK);
			size_t first = 0, size1 = 0;
			for (int level = 0; level <= 9; level++) {
				size_t piece = level % 3 == 0 ? n + 1 : level % 3 == 1 ? 4093 : 65536 + 7;
				if (piece == 0) piece = 1;
				sink s = deflate_pieces(p, n, level, piece);
				size_t got = inflate_whole(s.p, s.n, out, n + AK_FL_OUT_SLACK);
				if (got != n || (n && memcmp(out, p, n) != 0)) T_FAIL("kind %d, %zu bytes, level %d: round trip broke (%zu)", kind, n, level, got);
				if (level == 0) first = s.n;
				if (level == 1) size1 = s.n;
				if (level == 9 && kind == 0 && n >= 300) CHECK(s.n <= size1);
				if (level == 0) CHECK(s.n >= n);
				if (level > 0 && kind != 1) CHECK(s.n <= first);
				free(s.p);
			}
			free(out);
			free(p);
		}
	}
}

// Decoded in pieces, from a file-like source a byte at a time, a stream
// gives what it does whole: the window's history survives every read.
TEST(inflate_streams_in_pieces) {
	size_t n = 600000;
	uint8_t *p = input(4, n);
	for (int level = 1; level <= 9; level += 4) {
		sink s = deflate_pieces(p, n, level, n);
		ak_inflater *d = ak_inflater_new();
		uint8_t *win = malloc(70000);
		size_t k = 0, in_len = 0, in_pos = 0, start = 0, pos = 0;
		uint8_t *in = malloc(s.n + 8);
		bool ok = true;
		for (int guard = 0; guard < 10000000; guard++) {
			ak_fl_io io = {.in = in, .in_len = in_len, .in_pos = in_pos, .eof = in_len == s.n, .out = win, .start = start, .pos = pos, .cap = 70000};
			int r = ak_inflater_run(d, &io);
			in_pos = io.in_pos;
			if (memcmp(win + pos, p + k, io.pos - pos) != 0) ok = false;
			k += io.pos - pos;
			pos = io.pos;
			if (r == AK_FL_DONE || r == AK_FL_CORRUPT) {
				ok = ok && r == AK_FL_DONE;
				break;
			}
			if (r == AK_FL_NEED_INPUT) {
				// What's unread stays; one more byte comes.
				memcpy(in + in_len, s.p + in_len, 1);
				in_len++;
			} else {
				size_t keep = pos < 32768 ? pos : 32768;
				memmove(win, win + pos - keep, keep);
				pos = keep;
			}
		}
		CHECK(ok);
		CHECK_EQ_INT(k, n);
		CHECK_EQ_INT(in_pos, s.n);
		free(in);
		free(win);
		ak_inflater_free(d);
		free(s.p);
	}
	free(p);
}

// zlib's own streams (Python's zlib, level 9): fixed codes, its own
// dynamic codes, and gzip members one after another.
static const uint8_t zlib9[] = {0x78, 0xda, 0x73, 0x2c, 0x2e, 0xcd, 0x4e, 0x54, 0x28, 0x4a, 0xcd, 0x4b, 0x49, 0x2d, 0x2a, 0x56, 0x28, 0xcf, 0x2c, 0xc9, 0x50, 0x48, 0x4d, 0x4c, 0xce, 0x50, 0x28, 0xc8, 0x49, 0x2c, 0x49, 0xcb, 0x2f, 0xca, 0x55, 0x2f, 0x56, 0xc8, 0x2f, 0xcf, 0x03, 0x4a, 0xa4, 0xa4, 0xa7, 0x96, 0x14, 0xeb, 0x29, 0x38, 0xd2, 0x58, 0x3d, 0x03, 0x23, 0x13, 0x33, 0x0b, 0x2b, 0x1b, 0x3b, 0x07, 0x27, 0x17, 0x37, 0x0f, 0x2f, 0x1f, 0xbf, 0x80, 0xa0, 0x90, 0xb0, 0x88, 0xa8, 0x98, 0xb8, 0x84, 0xa4, 0x94, 0xb4, 0x8c, 0xac, 0x9c, 0xbc, 0x82, 0xa2, 0x92, 0xb2, 0x8a, 0xaa, 0x9a, 0xba, 0x86, 0xa6, 0x96, 0xb6, 0x8e, 0xae, 0x9e, 0xbe, 0x81, 0xa1, 0x91, 0xb1, 0x89, 0xa9, 0x99, 0xb9, 0x85, 0xa5, 0x95, 0xb5, 0x8d, 0xad, 0x9d, 0xbd, 0x83, 0xa3, 0x93, 0xb3, 0x8b, 0xab, 0x9b, 0xbb, 0x87, 0xa7, 0x97, 0xb7, 0x8f, 0xaf, 0x9f, 0x7f, 0x40, 0x60, 0x50, 0x70, 0x48, 0x68, 0x58, 0x78, 0x44, 0x64, 0x54, 0x74, 0x4c, 0x6c, 0x5c, 0x7c, 0x42, 0x62, 0x52, 0x72, 0x4a, 0x6a, 0x5a, 0x7a, 0x46, 0x66, 0x56, 0x76, 0x4e, 0x6e, 0x5e, 0x7e, 0x41, 0x61, 0x51, 0x71, 0x49, 0x69, 0x59, 0x79, 0x45, 0x65, 0x55, 0x75, 0x4d, 0x6d, 0x5d, 0x7d, 0x43, 0x63, 0x53, 0x73, 0x4b, 0x6b, 0x5b, 0x7b, 0x47, 0x67, 0x57, 0x77, 0x4f, 0x6f, 0x5f, 0xff, 0x84, 0x89, 0x93, 0x26, 0x4f, 0x99, 0x3a, 0x6d, 0xfa, 0x8c, 0x99, 0xb3, 0x66, 0xcf, 0x99, 0x3b, 0x6f, 0xfe, 0x82, 0x85, 0x8b, 0x16, 0x2f, 0x59, 0xba, 0x6c, 0xf9, 0x8a, 0x95, 0xab, 0x56, 0xaf, 0x59, 0xbb, 0x6e, 0xfd, 0x86, 0x8d, 0x9b, 0x36, 0x6f, 0xd9, 0xba, 0x6d, 0xfb, 0x8e, 0x9d, 0xbb, 0x76, 0xef, 0xd9, 0xbb, 0x6f, 0xff, 0x81, 0x83, 0x87, 0x0e, 0x1f, 0x39, 0x7a, 0xec, 0xf8, 0x89, 0x93, 0xa7, 0x4e, 0x9f, 0x39, 0x7b, 0xee, 0xfc, 0x85, 0x8b, 0x97, 0x2e, 0x5f, 0xb9, 0x7a, 0xed, 0xfa, 0x8d, 0x9b, 0xb7, 0x6e, 0xdf, 0xb9, 0x7b, 0xef, 0xfe, 0x83, 0x87, 0x8f, 0x1e, 0x3f, 0x79, 0xfa, 0xec, 0xf9, 0x8b, 0x97, 0xaf, 0x5e, 0xbf, 0x79, 0xfb, 0xee, 0xfd, 0x87, 0x8f, 0x9f, 0x3e, 0x7f, 0xf9, 0xfa, 0xed, 0xfb, 0x8f, 0x9f, 0xbf, 0x7e, 0xff, 0xf9, 0xfb, 0xef, 0x3f, 0x00, 0x56, 0xfd, 0xb4, 0x28};
static const uint8_t zlib_dynamic[] = {0x25, 0x94, 0x89, 0x11, 0xc2, 0x40, 0x0c, 0x03, 0x6b, 0xf5, 0xdf, 0x7f, 0x07, 0xd9, 0xbd, 0x30, 0x30, 0x90, 0x7b, 0x64, 0x49, 0x96, 0xe9, 0xdd, 0x8a, 0xa8, 0xc8, 0xdd, 0xdb, 0x98, 0xaa, 0xca, 0xee, 0xba, 0xb8, 0x66, 0x95, 0x3d, 0x1e, 0x82, 0x87, 0x8b, 0x9d, 0xab, 0xa9, 0xf3, 0x50, 0xf4, 0xb0, 0x71, 0xd1, 0xb5, 0x9d, 0x57, 0xc9, 0xcd, 0x89, 0x88, 0xe5, 0xb1, 0x59, 0x98, 0xde, 0xdc, 0xbc, 0x05, 0x21, 0x59, 0xef, 0x4b, 0x20, 0x6f, 0x32, 0x13, 0x58, 0x77, 0x1b, 0xb0, 0xa0, 0x0e, 0x65, 0xa6, 0x38, 0x15, 0xd5, 0xde, 0x99, 0x29, 0xde, 0xcb, 0xb9, 0x9c, 0xca, 0x38, 0x4a, 0xe4, 0xf8, 0xfd, 0x28, 0xc6, 0x52, 0x4c, 0x1e, 0x60, 0x6d, 0x34, 0xeb, 0x0b, 0x66, 0x64, 0x01, 0x53, 0x39, 0x7b, 0x4d, 0x31, 0x96, 0x40, 0xac, 0xa2, 0xec, 0x5e, 0xc4, 0x4c, 0x0f, 0x25, 0xb8, 0xc9, 0x89, 0xba, 0xe6, 0x53, 0x93, 0x56, 0x46, 0x44, 0x76, 0xd6, 0x2c, 0xe2, 0xbc, 0xc9, 0x69, 0xd9, 0x83, 0x7a, 0x91, 0x8f, 0xfd, 0x22, 0xa2, 0x8e, 0x77, 0x0e, 0xf2, 0xb3, 0xa3, 0xb8, 0x94, 0x14, 0x46, 0xcd, 0xbc, 0x93, 0xdd, 0x33, 0xeb, 0x01, 0x30, 0x81, 0xec, 0x9d, 0x64, 0x83, 0xc5, 0x07, 0x77, 0xb3, 0x33, 0x30, 0x44, 0x5f, 0x83, 0x8a, 0x56, 0x29, 0x46, 0xfa, 0xc8, 0x65, 0x60, 0x10, 0x05, 0x07, 0xd0, 0xa8, 0x04, 0x34, 0x7a, 0xf1, 0x8b, 0xeb, 0xf7, 0x4c, 0xa7, 0x1a, 0x26, 0xb9, 0x07, 0x47, 0xbe, 0x8e, 0x1e, 0x3d, 0x9d, 0x5b, 0xcf, 0x2c, 0x84, 0xb3, 0x46, 0x71, 0xb9, 0x2d, 0xec, 0x46, 0xdb, 0x42, 0x3b, 0x50, 0xfc, 0x4c, 0xa6, 0x8e, 0x4d, 0x85, 0x33, 0x06, 0xe0, 0xa6, 0x76, 0xe3, 0x2c, 0x6d, 0x00, 0x80, 0xdb, 0x1c, 0xa2, 0x1d, 0x14, 0xe0, 0x45, 0xdd, 0x4a, 0x0f, 0x60, 0x9b, 0xad, 0xe8, 0x7d, 0x44, 0xef, 0xac, 0x80, 0x22, 0x0c, 0xc1, 0x28, 0x40, 0xc8, 0x01, 0x6b, 0xd1, 0xe0, 0xa7, 0xc0, 0x24, 0x67, 0xfa, 0xa9, 0x1b, 0xa3, 0x31, 0xe6, 0x48, 0x37, 0x61, 0x04, 0x7c, 0xd8, 0x72, 0x3c, 0xa1, 0xc7, 0x43, 0xc7, 0xa0, 0x11, 0xf6, 0x9b, 0x35, 0x9c, 0x83, 0xf0, 0xd8, 0x75, 0xb8, 0x17, 0x6d, 0x36, 0x40, 0xa8, 0xa0, 0x4b, 0xf5, 0xcc, 0x1f, 0x3f, 0xd8, 0x2f, 0xbb, 0xd4, 0xe4, 0x51, 0x41, 0x98, 0x3e, 0x04, 0x48, 0xe0, 0x4c, 0x12, 0x89, 0x35, 0x22, 0xc8, 0x53, 0x4c, 0xa9, 0x96, 0x4b, 0xf4, 0x6b, 0x8b, 0x96, 0x53, 0x87, 0x3b, 0xec, 0x28, 0x9f, 0x45, 0x6c, 0x5b, 0xfb, 0x19, 0x0f, 0x80, 0x4a, 0xa5, 0x2e, 0x93, 0xc4, 0x7a, 0xc6, 0x68, 0x2a, 0x3b, 0x24, 0xd2, 0x6c, 0xd3, 0xa6, 0x31, 0xe6, 0x70, 0x53, 0x2e, 0x20, 0xb4, 0x50, 0xdc, 0x73, 0x18, 0x48, 0x8a, 0x26, 0x3d, 0xf7, 0x4b, 0x05, 0x94, 0x20, 0x61, 0x98, 0xc4, 0x67, 0x3c, 0xe2, 0xb9, 0x7b, 0x42, 0xf5, 0x12, 0x1a, 0x32, 0x06, 0x15, 0x08, 0xfa, 0x86, 0x69, 0x0e, 0x8f, 0x05, 0xc8, 0xca, 0xe0, 0x16, 0xbd, 0xd0, 0xa6, 0x31, 0xe6, 0xaf, 0x79, 0x64, 0x95, 0x91, 0x6b, 0x9b, 0x82, 0xb7, 0xce, 0x9a, 0x21, 0x76, 0x1e, 0x0c, 0x35, 0x54, 0x99, 0x17, 0x43, 0x86, 0x52, 0xa8, 0xbd, 0x11, 0x72, 0x9a, 0x0b, 0xd1, 0xe4, 0x2f, 0x31, 0xc2, 0x19, 0x04, 0xa0, 0xdf, 0x44, 0x80, 0xac, 0xdd, 0xc8, 0x76, 0x0c, 0xdf, 0x88, 0x9e, 0x79, 0x41, 0x8c, 0x99, 0x86, 0x11, 0x3f, 0x29, 0x0b, 0x37, 0x36, 0x47, 0x0e, 0xc0, 0x53, 0xd5, 0xe0, 0x97, 0x6a, 0x4f, 0x1f, 0x48, 0x16, 0xed, 0xb6, 0xdb, 0x34, 0x10, 0xce, 0xf5, 0x4f, 0x8b, 0xb5, 0xcd, 0xdb, 0xcb, 0xb6, 0x7f, 0x26, 0x6f, 0x3e, 0x1c, 0x67, 0xe3, 0x45, 0x08, 0x0c, 0xf8, 0x38, 0x69, 0x1c, 0xe0, 0x9e, 0xb3, 0x0f, 0x18, 0x9a, 0xd3, 0x80, 0x19, 0x18, 0x9b, 0xff, 0xff, 0x5f, 0xd0, 0x17, 0x7f, 0x21, 0xd1, 0x9c, 0x65, 0xd9, 0x1d, 0x84, 0x7c};
static const uint8_t two_members[] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x4b, 0xcb, 0x2c, 0x2a, 0x2e, 0x51, 0xc8, 0x4d, 0xcd, 0x4d, 0x4a, 0x2d, 0xd2, 0x51, 0x00, 0x00, 0xde, 0xff, 0x0a, 0x62, 0x0e, 0x00, 0x00, 0x00, 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x2b, 0x4e, 0x4d, 0xce, 0xcf, 0x4b, 0x51, 0xc8, 0x4d, 0xcd, 0x4d, 0x4a, 0x2d, 0x02, 0x00, 0x24, 0x74, 0xfa, 0x9f, 0x0d, 0x00, 0x00, 0x00};

TEST(inflate_zlibs_streams) {
	uint8_t text[400];
	for (int i = 0; i < 3; i++) memcpy(text + 48 * i, "Asuka renders with each platform's own widgets. ", 48);
	for (int i = 0; i < 256; i++) text[144 + i] = (uint8_t)i;
	ak_bytes *out = call("compress.decodeBytes", (ak_val[]){B(zlib9, sizeof zlib9), I(AK_CZ_ZLIB), I(-1)}, 3, 1).p;
	REQUIRE(out);
	CHECK(out->len == 400 && memcmp(out->data, text, 400) == 0);
	ak_release(out);
	uint8_t want[1200];
	uint32_t x = 1;
	for (int i = 0; i < 1200; i++) want[i] = (uint8_t)('a' + lcg(&x) % 7);
	out = call("compress.decodeBytes", (ak_val[]){B(zlib_dynamic, sizeof zlib_dynamic), I(AK_CZ_RAW), I(-1)}, 3, 1).p;
	REQUIRE(out);
	CHECK(out->len == 1200 && memcmp(out->data, want, 1200) == 0);
	ak_release(out);
	out = call("compress.decodeBytes", (ak_val[]){B(two_members, sizeof two_members), I(AK_CZ_GZIP), I(-1)}, 3, 1).p;
	REQUIRE(out);
	CHECK(out->len == 27 && memcmp(out->data, "first member, second member", 27) == 0);
	ak_release(out);
	// zlib's header: a preset dictionary, or a check that's off.
	uint8_t bad[sizeof zlib9];
	memcpy(bad, zlib9, sizeof bad);
	bad[1] = 0xdb;
	CHECK(call("compress.decodeBytes", (ak_val[]){B(bad, sizeof bad), I(AK_CZ_ZLIB), I(-1)}, 3, 1).p == NULL);
	bad[sizeof bad - 1] ^= 1;  // Adler-32
	bad[1] = 0xda;
	CHECK(call("compress.decodeBytes", (ak_val[]){B(bad, sizeof bad), I(AK_CZ_ZLIB), I(-1)}, 3, 1).p == NULL);
	CHECK_EQ_INT(call("compress.failureKind", NULL, 0, 0).i, AK_CZ_CORRUPT);
}

// Broken streams fail, never read or write out of bounds (ASan): every
// prefix of one, its bits flipped one at a time, and hand-made ones: a
// block of type 3, a stored block whose length's complement is off,
// codes over-subscribed or incomplete, a distance before the start.
TEST(inflate_corrupt) {
	size_t n = 3000;
	uint8_t *p = input(4, n), *out = malloc(n + AK_FL_OUT_SLACK);
	sink s = deflate_pieces(p, n, 6, n);
	for (size_t k = 0; k < s.n; k++) CHECK(inflate_whole(s.p, k, out, n + AK_FL_OUT_SLACK) == SIZE_MAX);
	for (size_t bit = 0; bit < s.n * 8; bit += 3) {
		s.p[bit / 8] ^= (uint8_t)(1 << bit % 8);
		size_t got = inflate_whole(s.p, s.n, out, n + AK_FL_OUT_SLACK);
		CHECK(got == SIZE_MAX || got <= n);
		s.p[bit / 8] ^= (uint8_t)(1 << bit % 8);
	}
	// Through gzip, a flipped bit is always caught: by DEFLATE or the CRC.
	ak_bytes *gz = call("compress.encodeBytes", (ak_val[]){B(p, n), I(AK_CZ_GZIP), I(6)}, 3, 1).p;
	for (size_t bit = 10 * 8; bit < (gz->len - 8) * 8; bit += 5) {
		gz->data[bit / 8] ^= (uint8_t)(1 << bit % 8);
		ak_bytes *r = call("compress.decodeBytes", (ak_val[]){{.p = gz}, I(AK_CZ_GZIP), I(-1)}, 3, 0).p;
		if (r) {
			CHECK(r->len == n && memcmp(r->data, p, n) == 0);
			ak_release(r);
		}
		gz->data[bit / 8] ^= (uint8_t)(1 << bit % 8);
	}
	ak_release(gz);
	static const struct {
		uint8_t b[8];
		size_t n;
	} bad[] = {
		{{0x07}, 1},                                // final, type 3
		{{0x01, 0x02, 0x00, 0xfd, 0xfe, 'o', 'k'}, 7},  // stored: NLEN not LEN's complement
		{{0x01, 0x05, 0x00, 0xfa, 0xff, 'o', 'k'}, 7},  // stored: ends before its 5 bytes
		{{0x05, 0xe0, 0xff, 0xff, 0xff, 0xff}, 6},  // dynamic: code lengths' code over-subscribed
		{{0x05, 0x00, 0x00, 0x00, 0x00}, 5},        // dynamic: no code lengths at all
		{{0x03, 0x02, 0x00}, 3},                    // fixed: a match 1 back with nothing before it
		{{0x03}, 1},                                // fixed: ends with no end code
	};
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		if (inflate_whole(bad[i].b, bad[i].n, out, n + AK_FL_OUT_SLACK) != SIZE_MAX) T_FAIL("bad stream %zu decoded", i);
		ak_val r = call("compress.decodeBytes", (ak_val[]){B(bad[i].b, bad[i].n), I(AK_CZ_RAW), I(-1)}, 3, 1);
		CHECK(r.p == NULL);
	}
	// The empty stream, fixed codes: just the end code.
	CHECK_EQ_INT(inflate_whole((const uint8_t[]){0x03, 0x00}, 2, out, n + AK_FL_OUT_SLACK), 0);
	free(s.p);
	free(out);
	free(p);
}

// A stream that makes far more than its size stops at the limit, and
// what decoding it holds stays near the limit, not what it'd make.
TEST(inflate_bomb_bounded) {
	size_t n = 64 << 20;
	ak_cz_sink sk = {0};
	ak_cz_deflate *z = ak_cz_deflate_new(&sk, AK_CZ_GZIP, 9);
	uint8_t *zeros = calloc(1, 1 << 20);
	for (size_t at = 0; at < n; at += 1 << 20) ak_cz_deflate_write(z, zeros, 1 << 20);
	CHECK(ak_cz_deflate_finish(z));
	ak_cz_deflate_free(z);
	free(zeros);
	ak_bytes *packed = ak_cz_sink_bytes(&sk);
	CHECK(packed->len < n / 500);
	uint64_t before = ak_alloc_bytes;
	ak_val r = call("compress.decodeBytes", (ak_val[]){B(packed->data, packed->len), I(AK_CZ_GZIP), I(1 << 20)}, 3, 1);
	CHECK(r.p == NULL);
	CHECK_EQ_INT(call("compress.failureKind", NULL, 0, 0).i, AK_CZ_TOO_LARGE);
#ifndef NDEBUG
	CHECK(ak_alloc_bytes - before < (8u << 20));
#endif
	AK_UNUSED(before);
	// Under the limit it decodes whole.
	ak_bytes *out = call("compress.decodeBytes", (ak_val[]){B(packed->data, packed->len), I(AK_CZ_GZIP), I(n)}, 3, 1).p;
	REQUIRE(out);
	CHECK_EQ_INT(out->len, n);
	ak_release(out);
	ak_release(packed);
}

// Adler-32 at every length near its chunks' (5552), against the
// definition's two sums.
TEST(adler32) {
	static uint8_t data[12000];
	uint32_t x = 3;
	for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)lcg(&x);
	CHECK_EQ_INT(ak_adler32(1, (const uint8_t *)"Wikipedia", 9), 0x11E60398);
	for (size_t n = 0; n < sizeof data; n += n < 64 ? 1 : 997) {
		uint32_t a = 1, b = 0;
		for (size_t i = 0; i < n; i++) a = (a + data[i]) % 65521, b = (b + a) % 65521;
		CHECK_EQ_INT(ak_adler32(1, data, n), b << 16 | a);
		CHECK_EQ_INT(ak_adler32(ak_adler32(1, data, n / 3), data + n / 3, n - n / 3), b << 16 | a);
	}
}

// gzip's header with every optional field: extra, name, comment and its
// own CRC (RFC 1952 2.3.1), before a member that's stored.
TEST(gzip_header_fields) {
	uint8_t g[64] = {0x1f, 0x8b, 8, 2 | 4 | 8 | 16, 0, 0, 0, 0, 0, 3};
	size_t k = 10;
	g[k++] = 3, g[k++] = 0, g[k++] = 'x', g[k++] = 'y', g[k++] = 'z';  // FEXTRA
	memcpy(g + k, "a.txt", 6), k += 6;                                   // FNAME
	memcpy(g + k, "hi", 3), k += 3;                                      // FCOMMENT
	g[k++] = 0xab, g[k++] = 0xcd;                                        // FHCRC
	// A stored block: final, 2 bytes, "ok".
	g[k++] = 1, g[k++] = 2, g[k++] = 0, g[k++] = 0xfd, g[k++] = 0xff, g[k++] = 'o', g[k++] = 'k';
	uint32_t crc = ak_cz_crc32(0, "ok", 2);
	for (int i = 0; i < 4; i++) g[k++] = (uint8_t)(crc >> (8 * i));
	g[k++] = 2, g[k++] = 0, g[k++] = 0, g[k++] = 0;
	ak_bytes *out = call("compress.decodeBytes", (ak_val[]){B(g, k), I(AK_CZ_GZIP), I(100)}, 3, 1).p;
	REQUIRE(out);
	CHECK(out->len == 2 && memcmp(out->data, "ok", 2) == 0);
	ak_release(out);
	// Reserved flags set: corrupt.
	g[3] |= 0x20;
	CHECK(call("compress.decodeBytes", (ak_val[]){B(g, k), I(AK_CZ_GZIP), I(100)}, 3, 1).p == NULL);
	CHECK_EQ_INT(call("compress.failureKind", NULL, 0, 0).i, AK_CZ_CORRUPT);
}

// The CRC-32 of "123456789" is its check value (ISO 3309).
TEST(crc32_check) {
	CHECK_EQ_INT(ak_cz_crc32(0, "123456789", 9), 0xcbf43926);
	CHECK_EQ_INT(ak_cz_crc32(ak_cz_crc32(0, "1234", 4), "56789", 5), 0xcbf43926);
	CHECK_EQ_INT(ak_crc32c(0, "123456789", 9), 0xe3069283);
	// The CPU's instructions and the eight tables give the table's CRC a
	// byte at a time, at every length and alignment, continued or not.
	static uint8_t data[4200];
	uint32_t r = 1;
	for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)((r = r * 1103515245 + 12345) >> 16);
	for (size_t n = 0; n < 8192; n += n < 200 ? 1 : 61) {
		ak_crc_hw_off = n >= 4096;
		for (size_t at = 0; at < 8; at++) {
			uint32_t prev = (uint32_t)(n * 2654435761u);
			CHECK_EQ_INT(ak_crc32(prev, data + at, n % 4096), ak_crc32_bytewise(prev, data + at, n % 4096, false));
			CHECK_EQ_INT(ak_crc32c(prev, data + at, n % 4096), ak_crc32_bytewise(prev, data + at, n % 4096, true));
		}
	}
	ak_crc_hw_off = false;
}

#if !defined(_WIN32)

static char *path_in(const char *dir, const char *name) {
	size_t n = strlen(dir) + strlen(name) + 2;
	char *p = malloc(n);
	snprintf(p, n, "%s/%s", dir, name);
	return p;
}

static void write_text(const char *path, const char *s) {
	FILE *f = fopen(path, "wb");
	fputs(s, f);
	fclose(f);
}

static char *read_text(const char *path) {
	static char buf[256];
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	size_t n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = 0;
	return buf;
}

// encode makes an archive of (name, data, kind, mode) entries into path.
typedef struct {
	const char *name, *data;
	int kind, mode;
	const char *target;
} ent;

static bool encode_to(const char *path, int format, const ent *es, int n) {
	ak_arr *names = ak_str_arr(0), *targets = ak_str_arr(0), *datas = ak_arr_new(AK_K_REF, NULL, 0), *info = ak_arr_new(AK_K_INT, NULL, 0);
	for (int i = 0; i < n; i++) {
		ak_str_arr_push(&names, es[i].name, strlen(es[i].name));
		const char *t = es[i].target ? es[i].target : "";
		ak_str_arr_push(&targets, t, strlen(t));
		ak_arr_push(&datas, (ak_val){.p = ak_bytes_new(es[i].data, es[i].data ? strlen(es[i].data) : 0)});
		int64_t v[4] = {es[i].kind, es[i].mode, 1704209444000000000ll, 0x58220000 | (15 << 11)};
		for (int k = 0; k < 4; k++) ak_arr_push(&info, (ak_val){.i = v[k]});
	}
	ak_bytes *b = call("archive.encodeFiles", (ak_val[]){I(format), I(6), {.p = names}, {.p = targets}, {.p = datas}, {.p = info}}, 6, 0x3c).p;
	if (!b) return false;
	FILE *f = fopen(path, "wb");
	fwrite(b->data, 1, b->len, f);
	fclose(f);
	ak_release(b);
	return true;
}

static bool extract(const char *archive, const char *dir) {
	return call("archive.extractFile", (ak_val[]){S(archive), S(dir), I(1 << 20)}, 3, 3).i != 0;
}

// Extracting keeps an entry's permissions (not setuid's) and its time,
// and replaces a link that's where a file goes, never writing through
// it; a directory that's a link (made before) isn't gone into.
TEST(extract_over_links) {
	char *root = ak_fs_temp_dir();
	REQUIRE(root);
	char *out = path_in(root, "out"), *outside = path_in(root, "outside"), *dir = path_in(root, "dest");
	char *z = path_in(root, "a.zip"), *t = path_in(root, "a.tar");
	REQUIRE(ak_fs_make_dirs(out) && ak_fs_make_dirs(dir));
	char *victim = path_in(outside, "victim.txt");
	REQUIRE(ak_fs_make_dirs(outside));
	write_text(victim, "safe");
	ent es[] = {{"run.sh", "#!/bin/sh\n", 0, 04755, NULL}, {"victim.txt", "evil", 0, 0644, NULL}};
	for (int format = 0; format < 3; format++) {
		const char *archive = format == 0 ? z : t;
		REQUIRE(encode_to(archive, format, es, 2));
		// victim.txt in dest is a link to the file outside.
		char *link = path_in(dir, "victim.txt");
		unlink(link);
		REQUIRE(symlink(victim, link) == 0);
		REQUIRE(extract(archive, dir));
		CHECK_EQ_STR(read_text(victim), "safe");
		struct stat st;
		REQUIRE(lstat(link, &st) == 0);
		CHECK(S_ISREG(st.st_mode));
		CHECK_EQ_STR(read_text(link), "evil");
		char *run = path_in(dir, "run.sh");
		REQUIRE(stat(run, &st) == 0);
		mode_t mask = umask(0);
		umask(mask);
		CHECK_EQ_INT(st.st_mode & 07777, 0755 & ~mask);
		CHECK_EQ_INT(st.st_mtime, 1704209444);
		free(run);
		free(link);
	}
	// A directory of dest that's a link outside: an entry under it is refused.
	char *sub = path_in(dir, "sub");
	REQUIRE(symlink(outside, sub) == 0);
	ent under[] = {{"sub/x.txt", "x", 0, 0, NULL}};
	REQUIRE(encode_to(z, 0, under, 1));
	CHECK(!extract(z, dir));
	CHECK_EQ_INT(call("archive.failureKind", NULL, 0, 0).i, AK_CZ_UNSAFE);
	char *x = path_in(outside, "x.txt");
	CHECK(access(x, F_OK) != 0);
	// A file where a directory goes: not a directory.
	char *file = path_in(dir, "file");
	write_text(file, "f");
	free(file);
	ent in_file[] = {{"file/x.txt", "x", 0, 0, NULL}};
	REQUIRE(encode_to(z, 0, in_file, 1));
	CHECK(!extract(z, dir));
	CHECK_EQ_INT(call("archive.failureKind", NULL, 0, 0).i, AK_CZ_NOT_DIR);
	free(x);
	free(sub);
	free(victim);
	CHECK(ak_fs_remove_tree(root));
	ak_free(root);
	free(out), free(outside), free(dir), free(z), free(t);
}

// A link an entry makes stays where it points, inside: one to a sibling
// is made, and what follows through it is refused.
TEST(extract_links_made) {
	char *root = ak_fs_temp_dir();
	REQUIRE(root);
	char *dir = path_in(root, "dest"), *t = path_in(root, "l.tar");
	ent es[] = {{"d/a.txt", "a", 0, 0, NULL}, {"d/l", NULL, 2, 0, "a.txt"}, {"up", NULL, 2, 0, "d/../d"}};
	REQUIRE(encode_to(t, 1, es, 3));
	REQUIRE(extract(t, dir));
	char *l = path_in(dir, "d/l");
	char target[64] = {0};
	CHECK(readlink(l, target, sizeof target - 1) == 5 && strcmp(target, "a.txt") == 0);
	CHECK_EQ_STR(read_text(l), "a");
	free(l);
	CHECK(ak_fs_remove_tree(root));
	ak_free(root);
	free(dir), free(t);
}

// A sink writes, patches what it wrote and drops its end, in a file as in
// memory.
TEST(sink_patch_truncate) {
	char *root = ak_fs_temp_dir();
	REQUIRE(root);
	char *p = path_in(root, "s.bin");
	for (int file = 0; file < 2; file++) {
		ak_cz_reset();
		ak_cz_sink s = {.f = file ? ak_cz_create(p, 0644, NULL) : NULL};
		CHECK(ak_cz_sink_write(&s, "hello world", 11));
		CHECK(ak_cz_sink_patch(&s, 0, "J", 1));
		CHECK(ak_cz_sink_truncate(&s, 5));
		CHECK(ak_cz_sink_write(&s, "!", 1));
		CHECK_EQ_INT(s.pos, 6);
		if (file) {
			CHECK(ak_cz_close(s.f));
			CHECK_EQ_STR(read_text(p), "Jello!");
		} else {
			ak_bytes *b = ak_cz_sink_bytes(&s);
			CHECK(b->len == 6 && memcmp(b->data, "Jello!", 6) == 0);
			ak_release(b);
		}
	}
	free(p);
	CHECK(ak_fs_remove_tree(root));
	ak_free(root);
}
#endif

T_MAIN()
