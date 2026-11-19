// flate.h: DEFLATE (RFC 1951), compressed and decompressed (flate.c):
// what std/compress (compress.c) and std/archive (archive.c) frame as
// gzip, zlib, zip entries and .tar.gz. Safe on any thread: each stream's
// state is its own.
//
// The compressor reads its input a piece at a time and hands its output
// to a callback a block at a time, so a stream of any size takes a few
// hundred KB. The decompressor decodes from an input buffer into an
// output buffer that holds what it made before (DEFLATE's matches look
// up to 32 KB back): the caller refills the one and empties (or grows)
// the other between calls.
#ifndef AK_STD_FLATE_H
#define AK_STD_FLATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---- Compressing ----

// ak_fl_put takes the next n bytes of the compressed stream; false
// stops the compressor (writing failed).
typedef bool (*ak_fl_put)(void *user, const uint8_t *p, size_t n);

typedef struct ak_deflater ak_deflater;

// ak_deflater_new compresses at level, 0 (stored blocks) to 9 (the
// longest searches), clamped; its output goes to put.
ak_deflater *ak_deflater_new(int level, ak_fl_put put, void *user);
// ak_deflater_write compresses the next n bytes; false once put failed.
bool ak_deflater_write(ak_deflater *d, const uint8_t *p, size_t n);
// ak_deflater_finish ends the stream (its last block, the final byte).
bool ak_deflater_finish(ak_deflater *d);
void ak_deflater_free(ak_deflater *d);

// ---- Decompressing ----

typedef struct ak_inflater ak_inflater;

// What ak_inflater_run stopped for.
enum {
	AK_FL_NEED_INPUT,   // in is used up (and more is coming: !eof)
	AK_FL_NEED_OUTPUT,  // out has less than AK_FL_OUT_SLACK bytes of room
	AK_FL_DONE,         // the stream's last block ended
	AK_FL_CORRUPT,      // not DEFLATE: ak_inflater_error says why
};

// The room out must have for the decompressor to go on: a match's
// longest, and the bytes its fast copies write past one's end.
enum { AK_FL_OUT_SLACK = 258 + 64 };

typedef struct {
	const uint8_t *in;  // the input: in[in_pos..in_len) not read yet
	size_t in_len, in_pos;
	bool eof;      // nothing comes after in_len (padding past it is the stream's end)
	uint8_t *out;  // the output: out[start..pos) is what the stream made so far
	size_t start;  // where this stream's output begins (matches reach no further back)
	size_t pos, cap;
} ak_fl_io;

// The bytes before in_pos a caller that refills in keeps (moving what's
// unread to the front): ak_inflater_run may give back up to these when
// the stream ends mid-buffer.
enum { AK_FL_IN_KEEP = 8 };

ak_inflater *ak_inflater_new(void);
// ak_inflater_reset starts a new stream (a gzip's next member).
void ak_inflater_reset(ak_inflater *d);
// ak_inflater_run decodes from io's input into its output, advancing
// in_pos and pos, until one of the above. At DONE, in_pos is just after
// the stream's last byte (what follows: a trailer, the next member).
int ak_inflater_run(ak_inflater *d, ak_fl_io *io);
const char *ak_inflater_error(const ak_inflater *d);
void ak_inflater_free(ak_inflater *d);

// ak_inflate_mem decodes a whole stream from src into out (cap bytes,
// AK_FL_OUT_SLACK more than it needs); how many bytes it made, or
// SIZE_MAX if it's broken or doesn't fit.
size_t ak_inflate_mem(uint8_t *out, size_t cap, const uint8_t *src, size_t len);

// Adler-32 (RFC 1950, zlib's), continued from a (1 to start).
uint32_t ak_adler32(uint32_t a, const uint8_t *p, size_t n);

#endif
