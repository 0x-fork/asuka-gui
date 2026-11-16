// inflate.c: DEFLATE (RFC 1951) and gzip (RFC 1952) decompression, read
// as a stream: ak_inflate_read gives the next bytes of what a section of
// a file decompresses to, so that several streams can be read at once
// (a bsdiff patch's three) and nothing is held whole in memory. For the
// updater's archives and deltas (update.c), which Go's compress/flate and
// compress/gzip write.
//
// Decoding follows zlib's puff.c (Mark Adler's reference decoder): codes
// are canonical Huffman codes, decoded a bit at a time from the counts
// of each length. A stream suspends only between output bytes, so the
// state kept across reads is the block being decoded (a stored block's
// bytes left, or its Huffman tables) and a match being copied.
#include "std/std.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

enum { MAXBITS = 15, MAXLCODES = 286, MAXDCODES = 30, FIXLCODES = 288, WSIZE = 32768, INSIZE = 16384 };

typedef struct {
	short count[MAXBITS + 1];
	short symbol[FIXLCODES];
} huffman;

enum { ST_HEADER, ST_STORED, ST_CODES, ST_DONE };

struct ak_inflate {
	FILE *f;
	int64_t left;  // bytes of the section not read yet (-1: to the end)
	uint8_t in[INSIZE];
	size_t in_pos, in_len;
	uint32_t bitbuf;
	int bitcnt;
	int state;
	bool last;          // the block being decoded is the last
	uint32_t stored;    // a stored block's bytes left
	uint32_t copy_len;  // a match's bytes left to copy, from copy_dist back
	uint32_t copy_dist;
	huffman lencode, distcode;
	uint8_t window[WSIZE];
	uint32_t wpos;
	uint64_t total;  // bytes made
	bool gzip;
	uint32_t crc;
	char err[160];
};

FILE *ak_update_fopen(const char *path, const char *mode) {
#if defined(_WIN32)
	int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
	int m = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
	if (n <= 0 || m <= 0) return NULL;
	wchar_t *w = ak_alloc(sizeof(wchar_t) * (size_t)n), wm[8];
	MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
	MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, m < 8 ? m : 8);
	wm[7] = 0;
	FILE *f = _wfopen(w, wm);
	ak_free(w);
	return f;
#else
	return fopen(path, mode);
#endif
}

static void fail(ak_inflate *z, const char *msg) {
	if (!z->err[0]) snprintf(z->err, sizeof z->err, "%s", msg);
	z->state = ST_DONE;
}

// byte_in reads the next byte of the section; -1 at its end.
static int byte_in(ak_inflate *z) {
	if (z->in_pos == z->in_len) {
		if (z->left == 0 || !z->f) return -1;
		size_t want = INSIZE;
		if (z->left > 0 && (int64_t)want > z->left) want = (size_t)z->left;
		size_t got = fread(z->in, 1, want, z->f);
		if (got == 0) return -1;
		if (z->left > 0) z->left -= (int64_t)got;
		z->in_pos = 0, z->in_len = got;
	}
	return z->in[z->in_pos++];
}

// bits reads need bits (up to 16), the first in the low bit; a cut-short
// input fails the stream, and gives 0.
static uint32_t bits(ak_inflate *z, int need) {
	uint32_t val = z->bitbuf;
	while (z->bitcnt < need) {
		int b = byte_in(z);
		if (b < 0) {
			fail(z, "the compressed data is cut short");
			return 0;
		}
		val |= (uint32_t)b << z->bitcnt;
		z->bitcnt += 8;
	}
	z->bitbuf = val >> need;
	z->bitcnt -= need;
	return val & ((1u << need) - 1);
}

// decode reads a symbol of h; -1 when the stream has failed or the code
// is none of h's.
static int decode(ak_inflate *z, const huffman *h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= MAXBITS; len++) {
		code |= (int)bits(z, 1);
		if (z->err[0]) return -1;
		int count = h->count[len];
		if (code - count < first) return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	fail(z, "the compressed data is damaged (a code of no symbol)");
	return -1;
}

// construct makes h of the code lengths of n symbols: 0 for a complete
// code, more than 0 for an incomplete one, less for one that's
// over-subscribed.
static int construct(huffman *h, const short *length, int n) {
	for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
	for (int s = 0; s < n; s++) h->count[length[s]]++;
	if (h->count[0] == n) return 0;  // no codes: complete, and decode fails
	int left = 1;
	for (int len = 1; len <= MAXBITS; len++) {
		left <<= 1;
		left -= h->count[len];
		if (left < 0) return left;
	}
	short offs[MAXBITS + 1];
	offs[1] = 0;
	for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
	for (int s = 0; s < n; s++)
		if (length[s] != 0) h->symbol[offs[length[s]]++] = (short)s;
	return left;
}

static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void fixed(ak_inflate *z) {
	short lengths[FIXLCODES];
	int s = 0;
	for (; s < 144; s++) lengths[s] = 8;
	for (; s < 256; s++) lengths[s] = 9;
	for (; s < 280; s++) lengths[s] = 7;
	for (; s < FIXLCODES; s++) lengths[s] = 8;
	construct(&z->lencode, lengths, FIXLCODES);
	for (s = 0; s < MAXDCODES; s++) lengths[s] = 5;
	construct(&z->distcode, lengths, MAXDCODES);
}

static void dynamic(ak_inflate *z) {
	static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	short lengths[MAXLCODES + MAXDCODES];
	int nlen = (int)bits(z, 5) + 257, ndist = (int)bits(z, 5) + 1, ncode = (int)bits(z, 4) + 4;
	if (z->err[0]) return;
	if (nlen > MAXLCODES || ndist > MAXDCODES) {
		fail(z, "the compressed data is damaged (too many codes)");
		return;
	}
	int index = 0;
	for (; index < ncode; index++) lengths[order[index]] = (short)bits(z, 3);
	for (; index < 19; index++) lengths[order[index]] = 0;
	if (z->err[0]) return;
	if (construct(&z->lencode, lengths, 19) != 0) {
		fail(z, "the compressed data is damaged (an incomplete code of code lengths)");
		return;
	}
	index = 0;
	while (index < nlen + ndist) {
		int symbol = decode(z, &z->lencode);
		if (symbol < 0) return;
		if (symbol < 16) {
			lengths[index++] = (short)symbol;
			continue;
		}
		short len = 0;
		int rep;
		if (symbol == 16) {
			if (index == 0) {
				fail(z, "the compressed data is damaged (a repeat with no length before)");
				return;
			}
			len = lengths[index - 1];
			rep = 3 + (int)bits(z, 2);
		} else if (symbol == 17) {
			rep = 3 + (int)bits(z, 3);
		} else {
			rep = 11 + (int)bits(z, 7);
		}
		if (z->err[0]) return;
		if (index + rep > nlen + ndist) {
			fail(z, "the compressed data is damaged (too many lengths)");
			return;
		}
		while (rep--) lengths[index++] = len;
	}
	if (lengths[256] == 0) {
		fail(z, "the compressed data is damaged (no end of block)");
		return;
	}
	int err = construct(&z->lencode, lengths, nlen);
	if (err && (err < 0 || nlen != z->lencode.count[0] + z->lencode.count[1])) {
		fail(z, "the compressed data is damaged (an incomplete code of lengths)");
		return;
	}
	err = construct(&z->distcode, lengths + nlen, ndist);
	if (err && (err < 0 || ndist != z->distcode.count[0] + z->distcode.count[1])) fail(z, "the compressed data is damaged (an incomplete code of distances)");
}

// ---- CRC-32 (gzip's), by nibbles ----

static const uint32_t crc_nibble[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
                                        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};

// crc_byte adds c to crc, kept inverted (it starts at 0xffffffff).
static uint32_t crc_byte(uint32_t crc, uint8_t c) {
	crc ^= c;
	crc = (crc >> 4) ^ crc_nibble[crc & 15];
	return (crc >> 4) ^ crc_nibble[crc & 15];
}

// gzip_header reads a gzip member's header (RFC 1952, 2.3).
static void gzip_header(ak_inflate *z) {
	int id1 = byte_in(z), id2 = byte_in(z), cm = byte_in(z), flg = byte_in(z);
	if (id1 != 0x1f || id2 != 0x8b || cm != 8 || flg < 0 || (flg & 0xe0)) {
		fail(z, "not a gzip file");
		return;
	}
	for (int i = 0; i < 6; i++) byte_in(z);  // MTIME, XFL, OS
	if (flg & 4) {                            // FEXTRA
		int lo = byte_in(z), hi = byte_in(z);
		if (hi < 0) goto short_;
		for (int n = lo | hi << 8; n > 0; n--)
			if (byte_in(z) < 0) goto short_;
	}
	for (int f = 8; f <= 16; f <<= 1) {  // FNAME, FCOMMENT: zero-terminated
		if (!(flg & f)) continue;
		int c;
		while ((c = byte_in(z)) > 0) {}
		if (c < 0) goto short_;
	}
	if (flg & 2) {  // FHCRC
		byte_in(z);
		if (byte_in(z) < 0) goto short_;
	}
	return;
short_:
	fail(z, "the gzip file is cut short");
}

// gzip_trailer checks the member's CRC-32 and size, after its last block.
static void gzip_trailer(ak_inflate *z) {
	z->bitbuf = 0, z->bitcnt = 0;  // the trailer starts at the next byte
	uint8_t t[8];
	for (int i = 0; i < 8; i++) {
		int b = byte_in(z);
		if (b < 0) {
			fail(z, "the gzip file is cut short");
			return;
		}
		t[i] = (uint8_t)b;
	}
	uint32_t crc = (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16 | (uint32_t)t[3] << 24;
	uint32_t size = (uint32_t)t[4] | (uint32_t)t[5] << 8 | (uint32_t)t[6] << 16 | (uint32_t)t[7] << 24;
	if (crc != ~z->crc || size != (uint32_t)z->total) fail(z, "the gzip file is damaged (its checksum differs)");
}

static ak_inflate *open_(const char *path, int64_t offset, int64_t len, bool gzip) {
	ak_inflate *z = ak_alloc(sizeof *z);
	memset(z, 0, sizeof *z);
	z->left = len;
	z->crc = 0xffffffff;
	z->gzip = gzip;
	z->f = ak_update_fopen(path, "rb");
	if (!z->f) {
		snprintf(z->err, sizeof z->err, "%s: can't be opened", path);
		z->state = ST_DONE;
		return z;
	}
	if (offset > 0 && (offset > 0x7fffffff || fseek(z->f, (long)offset, SEEK_SET) != 0)) fail(z, "the compressed data is cut short");
	if (gzip && !z->err[0]) gzip_header(z);
	return z;
}

ak_inflate *ak_inflate_open(const char *path, int64_t offset, int64_t len) { return open_(path, offset, len, false); }
ak_inflate *ak_gunzip_open(const char *path) { return open_(path, 0, -1, true); }

void ak_inflate_close(ak_inflate *z) {
	if (!z) return;
	if (z->f) fclose(z->f);
	ak_free(z);
}

const char *ak_inflate_error(const ak_inflate *z) { return z->err[0] ? z->err : NULL; }
bool ak_inflate_done(const ak_inflate *z) { return z->state == ST_DONE && !z->err[0]; }
uint64_t ak_inflate_total(const ak_inflate *z) { return z->total; }

static void put(ak_inflate *z, uint8_t c, uint8_t *out, size_t *k) {
	out[(*k)++] = c;
	z->window[z->wpos] = c;
	if (z->gzip) z->crc = crc_byte(z->crc, c);
	z->wpos = (z->wpos + 1) & (WSIZE - 1);
	z->total++;
}

size_t ak_inflate_read(ak_inflate *z, uint8_t *out, size_t n) {
	size_t k = 0;
	while (k < n && z->state != ST_DONE) {
		if (z->copy_len) {
			while (z->copy_len && k < n) {
				put(z, z->window[(z->wpos - z->copy_dist) & (WSIZE - 1)], out, &k);
				z->copy_len--;
			}
			continue;
		}
		switch (z->state) {
		case ST_HEADER: {
			if (z->last) {
				if (z->gzip) gzip_trailer(z);
				z->state = ST_DONE;
				break;
			}
			z->last = bits(z, 1);
			uint32_t type = bits(z, 2);
			if (z->err[0]) break;
			if (type == 0) {
				z->bitbuf = 0, z->bitcnt = 0;  // a stored block starts at a byte
				int b[4];
				for (int i = 0; i < 4; i++) b[i] = byte_in(z);
				if (b[3] < 0) {
					fail(z, "the compressed data is cut short");
					break;
				}
				uint32_t len = (uint32_t)(b[0] | b[1] << 8), nlen = (uint32_t)(b[2] | b[3] << 8);
				if (len != (~nlen & 0xffff)) {
					fail(z, "the compressed data is damaged (a stored block's length)");
					break;
				}
				z->stored = len;
				z->state = ST_STORED;
			} else if (type == 1) {
				fixed(z);
				z->state = ST_CODES;
			} else if (type == 2) {
				dynamic(z);
				if (!z->err[0]) z->state = ST_CODES;
			} else {
				fail(z, "the compressed data is damaged (a block of no type)");
			}
			break;
		}
		case ST_STORED:
			while (z->stored && k < n) {
				int b = byte_in(z);
				if (b < 0) {
					fail(z, "the compressed data is cut short");
					break;
				}
				put(z, (uint8_t)b, out, &k);
				z->stored--;
			}
			if (!z->stored && z->state == ST_STORED) z->state = ST_HEADER;
			break;
		case ST_CODES:
			while (k < n && !z->copy_len) {
				int symbol = decode(z, &z->lencode);
				if (symbol < 0) break;
				if (symbol < 256) {
					put(z, (uint8_t)symbol, out, &k);
					continue;
				}
				if (symbol == 256) {
					z->state = ST_HEADER;
					break;
				}
				symbol -= 257;
				if (symbol >= 29) {
					fail(z, "the compressed data is damaged (a length of no code)");
					break;
				}
				uint32_t len = (uint32_t)lbase[symbol] + bits(z, lext[symbol]);
				int ds = decode(z, &z->distcode);
				if (ds < 0) break;
				if (ds >= 30) {
					fail(z, "the compressed data is damaged (a distance of no code)");
					break;
				}
				uint32_t dist = (uint32_t)dbase[ds] + bits(z, dext[ds]);
				if (z->err[0]) break;
				if (dist > z->total) {
					fail(z, "the compressed data is damaged (a distance too far back)");
					break;
				}
				z->copy_len = len, z->copy_dist = dist;
			}
			break;
		}
	}
	return k;
}
