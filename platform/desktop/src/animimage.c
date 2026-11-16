// animimage.c: animated images where the platform has no player of its
// own (Windows' WIC and Linux's GDK decode a still, not an animation), as
// internal/animimage played them for the Go hosts: GIF (decoded here),
// APNG and animated WebP, their frames read here and each frame's picture
// decoded by the host's still decoder (WIC's, GDK's). A player composites
// frame after frame onto a canvas as the formats say (disposal, blending),
// so only the canvas and the frames' own rectangles are kept.
#include "desktop.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }
static uint16_t be16(const uint8_t *b) { return (uint16_t)(b[0] << 8 | b[1]); }
static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static int u24(const uint8_t *b) { return (int)b[0] | (int)b[1] << 8 | (int)b[2] << 16; }

static const uint8_t png_signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

// delay is a frame's time, as browsers show it: 10 ms or less is 100 ms.
static int delay(int ms) { return ms <= 10 ? 100 : ms; }

typedef struct {
	dsk_anim_frame *v;
	size_t n, cap;
} frames;

static dsk_anim_frame *add_frame(frames *f) {
	if (f->n == f->cap) {
		size_t cap = f->cap ? f->cap * 2 : 8;
		dsk_anim_frame *v = realloc(f->v, cap * sizeof *v);
		if (!v) return NULL;
		f->v = v, f->cap = cap;
	}
	dsk_anim_frame *fr = &f->v[f->n++];
	memset(fr, 0, sizeof *fr);
	return fr;
}

static void free_frames(frames *f) {
	for (size_t i = 0; i < f->n; i++) free(f->v[i].rgba);
	free(f->v);
}

static dsk_anim *finish(frames *f, int w, int h, int loops) {
	if (f->n < 2 || w <= 0 || h <= 0) {
		free_frames(f);
		return NULL;
	}
	dsk_anim *a = calloc(1, sizeof *a);
	if (!a) {
		free_frames(f);
		return NULL;
	}
	a->w = w, a->h = h, a->loops = loops, a->frames = f->v, a->n = f->n;
	return a;
}

void dsk_anim_free(dsk_anim *a) {
	if (!a) return;
	for (size_t i = 0; i < a->n; i++) free(a->frames[i].rgba);
	free(a->frames);
	free(a);
}

bool dsk_anim_is_animated(const uint8_t *d, size_t n) {
	if (n >= 4 && memcmp(d, "GIF8", 4) == 0) {
		// More than one graphic control extension is likelier than not an
		// animation; decoding says.
		int count = 0;
		for (size_t i = 0; i + 3 <= n; i++)
			if (d[i] == 0x21 && d[i + 1] == 0xF9 && d[i + 2] == 0x04 && ++count > 1) return true;
		return false;
	}
	if (n >= 8 && memcmp(d, png_signature, 8) == 0) {
		for (size_t p = 8; p + 12 <= n;) {
			uint32_t len = be32(d + p);
			if (len > n - p - 12) return false;
			if (memcmp(d + p + 4, "acTL", 4) == 0) return len >= 4 && be32(d + p + 8) > 1;
			if (memcmp(d + p + 4, "IDAT", 4) == 0) return false;
			p += 12 + len;
		}
		return false;
	}
	if (n >= 21 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBPVP8X", 8) == 0) return (d[20] & 2) != 0;
	return false;
}

// ---- GIF ----

typedef struct {
	const uint8_t *d;
	size_t n, p;
	// The image data's sub-blocks, read as one stream of bits.
	size_t left;  // bytes left in the sub-block
	bool ended;
	uint32_t bits;
	int nbits;
} gif_reader;

static int gif_byte(gif_reader *r) {
	while (!r->left) {
		if (r->ended || r->p >= r->n) {
			r->ended = true;
			return -1;
		}
		r->left = r->d[r->p++];
		if (!r->left) {
			r->ended = true;
			return -1;
		}
	}
	if (r->p >= r->n) {
		r->ended = true;
		return -1;
	}
	r->left--;
	return r->d[r->p++];
}

static int gif_code(gif_reader *r, int size) {
	while (r->nbits < size) {
		int b = gif_byte(r);
		if (b < 0) return -1;
		r->bits |= (uint32_t)b << r->nbits;
		r->nbits += 8;
	}
	int c = (int)(r->bits & ((1u << size) - 1));
	r->bits >>= size;
	r->nbits -= size;
	return c;
}

// gif_lzw decodes an image's LZW data into out (count indices); false if
// there's too little of it.
static bool gif_lzw(gif_reader *r, int min_size, uint8_t *out, size_t count) {
	if (min_size < 2 || min_size > 8) return false;
	enum { MAX = 4096 };
	uint16_t prefix[MAX];
	uint8_t suffix[MAX], stack[MAX + 1];
	int clear = 1 << min_size, eoi = clear + 1;
	int size = min_size + 1, next = clear + 2, prev = -1;
	uint8_t first = 0;
	size_t at = 0;
	for (int i = 0; i < clear; i++) prefix[i] = 0, suffix[i] = (uint8_t)i;
	while (at < count) {
		int c = gif_code(r, size);
		if (c < 0) return false;
		if (c == clear) {
			size = min_size + 1, next = clear + 2, prev = -1;
			continue;
		}
		if (c == eoi) break;
		int sp = 0, code = c;
		if (prev < 0) {
			if (c >= clear) return false;
			first = (uint8_t)c;
			out[at++] = first;
			prev = c;
			continue;
		}
		if (c > next || (c == next && next >= MAX)) return false;
		if (c == next) {
			// KwKwK: the code being made, prev's string and its first byte.
			stack[sp++] = first;
			code = prev;
		}
		while (code >= clear) {
			if (sp >= MAX) return false;
			stack[sp++] = suffix[code];
			code = prefix[code];
		}
		stack[sp++] = (uint8_t)code;
		first = (uint8_t)code;
		while (sp > 0 && at < count) out[at++] = stack[--sp];
		if (next < MAX) {
			prefix[next] = (uint16_t)prev;
			suffix[next] = first;
			next++;
			if (next == 1 << size && size < 12) size++;
		}
		prev = c;
	}
	return at == count;
}

// gif_skip_blocks skips sub-blocks up to and past their terminator.
static bool gif_skip_blocks(const uint8_t *d, size_t n, size_t *p) {
	while (*p < n) {
		size_t len = d[(*p)++];
		if (!len) return true;
		if (len > n - *p) return false;
		*p += len;
	}
	return false;
}

static dsk_anim *decode_gif(const uint8_t *d, size_t n) {
	if (n < 13) return NULL;
	int sw = le16(d + 6), sh = le16(d + 8);
	uint8_t flags = d[10];
	size_t p = 13;
	const uint8_t *global = NULL;
	int nglobal = 0;
	if (flags & 0x80) {
		nglobal = 1 << ((flags & 7) + 1);
		if ((size_t)nglobal * 3 > n - p) return NULL;
		global = d + p;
		p += (size_t)nglobal * 3;
	}
	frames fs = {0};
	int loop_count = -1;  // none: plays once
	int delay_cs = 0, disposal = 0, transparent = -1;
	int maxw = 0, maxh = 0;
	while (p < n) {
		uint8_t b = d[p++];
		if (b == 0x3B) break;  // the trailer
		if (b == 0x21) {
			if (p >= n) goto bad;
			uint8_t label = d[p++];
			if (label == 0xF9 && p + 6 <= n && d[p] == 4) {
				uint8_t f = d[p + 1];
				disposal = (f >> 2) & 7;
				delay_cs = le16(d + p + 2);
				transparent = f & 1 ? d[p + 4] : -1;
				p += 5;
				if (!gif_skip_blocks(d, n, &p)) goto bad;
			} else if (label == 0xFF && p + 12 <= n && d[p] == 11 && memcmp(d + p + 1, "NETSCAPE2.0", 11) == 0) {
				p += 12;
				while (p < n && d[p]) {
					size_t len = d[p];
					if (len >= 3 && p + 1 + len <= n && d[p + 1] == 1) loop_count = le16(d + p + 2);
					p += 1 + len;
					if (p > n) goto bad;
				}
				p++;
			} else if (!gif_skip_blocks(d, n, &p)) {
				goto bad;
			}
			continue;
		}
		if (b != 0x2C) goto bad;
		if (p + 9 > n) goto bad;
		int x = le16(d + p), y = le16(d + p + 2), w = le16(d + p + 4), h = le16(d + p + 6);
		uint8_t iflags = d[p + 8];
		p += 9;
		const uint8_t *pal = global;
		int npal = nglobal;
		if (iflags & 0x80) {
			npal = 1 << ((iflags & 7) + 1);
			if ((size_t)npal * 3 > n - p) goto bad;
			pal = d + p;
			p += (size_t)npal * 3;
		}
		if (!pal || p >= n) goto bad;
		int min_size = d[p++];
		size_t count = (size_t)w * (size_t)h;
		uint8_t *idx = malloc(count ? count : 1);
		if (!idx) goto bad;
		gif_reader r = {d, n, p, 0, false, 0, 0};
		bool ok = !count || gif_lzw(&r, min_size, idx, count);
		// Past the image's sub-blocks, what the decoder left of them.
		p = r.p;
		if (!r.ended) {
			if (r.left > n - p) {
				free(idx);
				goto bad;
			}
			p += r.left;
			if (!gif_skip_blocks(d, n, &p)) ok = false;
		}
		dsk_anim_frame *fr = ok ? add_frame(&fs) : NULL;
		if (!fr) {
			free(idx);
			goto bad;
		}
		fr->x = x, fr->y = y, fr->w = w, fr->h = h;
		fr->rgba = malloc(count ? count * 4 : 4);
		if (!fr->rgba) {
			free(idx);
			goto bad;
		}
		// Interlaced rows come in four passes.
		static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
		int row = 0, pass = 0;
		bool interlaced = iflags & 0x40;
		for (int sy = 0; sy < h; sy++) {
			int dy = sy;
			if (interlaced) {
				while (pass < 4 && start[pass] + row * step[pass] >= h) pass++, row = 0;
				if (pass >= 4) break;
				dy = start[pass] + row * step[pass];
				row++;
			}
			for (int sx = 0; sx < w; sx++) {
				uint8_t c = idx[(size_t)sy * (size_t)w + (size_t)sx];
				uint8_t *o = fr->rgba + ((size_t)dy * (size_t)w + (size_t)sx) * 4;
				if (c >= npal) {
					free(idx);
					goto bad;
				}
				if (c == transparent) {
					o[0] = o[1] = o[2] = o[3] = 0;
				} else {
					o[0] = pal[c * 3], o[1] = pal[c * 3 + 1], o[2] = pal[c * 3 + 2], o[3] = 255;
				}
			}
		}
		free(idx);
		fr->delay_ms = delay(delay_cs * 10);
		fr->disposal = disposal == 2 ? DSK_DISPOSE_BACKGROUND : disposal == 3 ? DSK_DISPOSE_PREVIOUS : DSK_DISPOSE_NONE;
		fr->blend = true;
		if (x + w > maxw) maxw = x + w;
		if (y + h > maxh) maxh = y + h;
		delay_cs = 0, disposal = 0, transparent = -1;
	}
	if (sw == 0 || sh == 0) sw = maxw, sh = maxh;
	return finish(&fs, sw, sh, loop_count == 0 ? 0 : loop_count < 0 ? 1 : loop_count + 1);
bad:
	free_frames(&fs);
	return NULL;
}

// ---- APNG ----

static uint32_t crc_table[256];

static uint32_t crc32_of(const uint8_t *b, size_t n) {
	if (!crc_table[1]) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			crc_table[i] = c;
		}
	}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

typedef struct {
	uint8_t *b;
	size_t n, cap;
	bool failed;
} bytes;

static void put(bytes *o, const void *p, size_t n) {
	if (o->failed) return;
	if (o->n + n > o->cap) {
		size_t cap = o->cap ? o->cap : 256;
		while (cap < o->n + n) cap *= 2;
		uint8_t *b = realloc(o->b, cap);
		if (!b) {
			o->failed = true;
			return;
		}
		o->b = b, o->cap = cap;
	}
	memcpy(o->b + o->n, p, n);
	o->n += n;
}

static void put_be32(bytes *o, uint32_t v) {
	uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
	put(o, b, 4);
}

static void put_le32(bytes *o, uint32_t v) {
	uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
	put(o, b, 4);
}

static void put_chunk(bytes *o, const char *type, const uint8_t *data, size_t n) {
	put_be32(o, (uint32_t)n);
	size_t start = o->n;
	put(o, type, 4);
	if (n) put(o, data, n);
	if (!o->failed) put_be32(o, crc32_of(o->b + start, o->n - start));
}

typedef struct {
	const uint8_t *fctl;
	size_t first, last;  // its image data's chunks: offsets of the first and past the last
	bool has;
} apng_frame;

// frame_of makes a decoded still of w by h a frame, its top left at x, y.
static bool frame_of(dsk_anim_frame *fr, uint8_t *rgba, int w, int h, int x, int y) {
	fr->rgba = rgba, fr->w = w, fr->h = h, fr->x = x, fr->y = y;
	return true;
}

static dsk_anim *decode_apng(const uint8_t *d, size_t n, dsk_still_decoder still, void *ctx) {
	const uint8_t *ihdr = NULL;
	int plays = 0, num_frames = 0;
	bool seen_idat = false, idat_is_frame = false;
	// Chunks before the image data (PLTE, tRNS, gAMA, iCCP...), every
	// frame's; the frames' fcTLs and data.
	size_t nshared = 0, nframes = 0, cap = 0;
	size_t shared[64];
	apng_frame *fl = NULL;
	struct span {
		size_t at, len;
	} *chunks = NULL;  // each frame's data chunks, in order (offset, length)
	size_t nchunks = 0, chunks_cap = 0;
	size_t *chunk_frame = NULL;
	for (size_t p = 8; p + 12 <= n;) {
		uint32_t len = be32(d + p);
		if (len > n - p - 12) break;
		const uint8_t *type = d + p + 4, *data = d + p + 8;
		if (!memcmp(type, "IHDR", 4)) {
			if (len >= 13) ihdr = data;
		} else if (!memcmp(type, "acTL", 4)) {
			if (len >= 8) num_frames = (int)be32(data), plays = (int)be32(data + 4);
		} else if (!memcmp(type, "fcTL", 4)) {
			if (len < 26) goto bad;
			if (nframes == cap) {
				cap = cap ? cap * 2 : 16;
				apng_frame *v = realloc(fl, cap * sizeof *v);
				if (!v) goto bad;
				fl = v;
			}
			fl[nframes++] = (apng_frame){data, 0, 0, false};
			if (!seen_idat) idat_is_frame = true;
		} else if (!memcmp(type, "IDAT", 4) || !memcmp(type, "fdAT", 4)) {
			bool idat = type[1] == 'D';
			if (idat) seen_idat = true;
			if (nframes && (!idat || idat_is_frame) && (idat || len >= 4)) {
				if (nchunks == chunks_cap) {
					chunks_cap = chunks_cap ? chunks_cap * 2 : 32;
					struct span *v = realloc(chunks, chunks_cap * sizeof *v);
					size_t *f = realloc(chunk_frame, chunks_cap * sizeof *f);
					if (v) chunks = v;
					if (f) chunk_frame = f;
					if (!v || !f) goto bad;
				}
				chunks[nchunks] = idat ? (struct span){p + 8, len} : (struct span){p + 12, len - 4};
				chunk_frame[nchunks++] = nframes - 1;
				fl[nframes - 1].has = true;
			}
		} else if (memcmp(type, "IEND", 4) != 0 && !seen_idat && nshared < 64) {
			shared[nshared++] = p;
		}
		p += 12 + len;
	}
	if (!ihdr || num_frames < 2 || nframes < 2) goto bad;
	frames fs = {0};
	for (size_t i = 0; i < nframes; i++) {
		if (!fl[i].has) continue;
		const uint8_t *fc = fl[i].fctl;
		uint32_t w = be32(fc + 4), h = be32(fc + 8);
		int x = (int)be32(fc + 12), y = (int)be32(fc + 16);
		int num = be16(fc + 20), den = be16(fc + 22);
		if (!den) den = 100;
		bytes o = {0};
		put(&o, png_signature, 8);
		uint8_t hdr[13];
		memcpy(hdr, ihdr, 13);
		hdr[0] = (uint8_t)(w >> 24), hdr[1] = (uint8_t)(w >> 16), hdr[2] = (uint8_t)(w >> 8), hdr[3] = (uint8_t)w;
		hdr[4] = (uint8_t)(h >> 24), hdr[5] = (uint8_t)(h >> 16), hdr[6] = (uint8_t)(h >> 8), hdr[7] = (uint8_t)h;
		put_chunk(&o, "IHDR", hdr, 13);
		for (size_t s = 0; s < nshared; s++) {
			size_t at = shared[s];
			char type[5] = {0};
			memcpy(type, d + at + 4, 4);
			put_chunk(&o, type, d + at + 8, be32(d + at));
		}
		for (size_t c = 0; c < nchunks; c++)
			if (chunk_frame[c] == i) put_chunk(&o, "IDAT", d + chunks[c].at, chunks[c].len);
		put_chunk(&o, "IEND", NULL, 0);
		uint8_t *rgba = NULL;
		int iw = 0, ih = 0;
		bool ok = !o.failed && still && still(ctx, o.b, o.n, &rgba, &iw, &ih);
		free(o.b);
		dsk_anim_frame *fr = ok ? add_frame(&fs) : NULL;
		if (!fr) {
			free(rgba);
			free_frames(&fs);
			goto bad;
		}
		frame_of(fr, rgba, iw, ih, x, y);
		fr->delay_ms = delay((int)((int64_t)num * 1000 / den));
		fr->blend = fc[25] == 1;
		fr->disposal = fc[24] == 1 ? DSK_DISPOSE_BACKGROUND : fc[24] == 2 ? DSK_DISPOSE_PREVIOUS : DSK_DISPOSE_NONE;
	}
	free(fl);
	free(chunks);
	free(chunk_frame);
	return finish(&fs, (int)be32(ihdr), (int)be32(ihdr + 4), plays);
bad:
	free(fl);
	free(chunks);
	free(chunk_frame);
	return NULL;
}

// ---- WebP ----

// still_webp is a frame's chunks as a WebP file: with an ALPH chunk, an
// extended one (VP8X with the alpha flag) of its size.
static void still_webp(bytes *o, const uint8_t *chunks, size_t n, int w, int h) {
	bool alpha = n >= 4 && !memcmp(chunks, "ALPH", 4);
	put(o, "RIFF", 4);
	put_le32(o, (uint32_t)(4 + (alpha ? 18 : 0) + n));
	put(o, "WEBP", 4);
	if (alpha) {
		uint8_t x[10] = {0x10, 0, 0, 0, (uint8_t)(w - 1), (uint8_t)((w - 1) >> 8), (uint8_t)((w - 1) >> 16),
			(uint8_t)(h - 1), (uint8_t)((h - 1) >> 8), (uint8_t)((h - 1) >> 16)};
		put(o, "VP8X", 4);
		put_le32(o, 10);
		put(o, x, 10);
	}
	put(o, chunks, n);
}

static dsk_anim *decode_webp(const uint8_t *d, size_t n, dsk_still_decoder still, void *ctx) {
	if (n < 30 || memcmp(d + 12, "VP8X", 4) != 0 || !(d[20] & 2) || !still) return NULL;
	int w = u24(d + 24) + 1, h = u24(d + 27) + 1, loops = 0;
	frames fs = {0};
	for (size_t p = 12; p + 8 <= n;) {
		uint32_t len = le32(d + p + 4);
		if (len > n - p - 8) break;
		const uint8_t *body = d + p + 8;
		if (!memcmp(d + p, "ANIM", 4)) {
			if (len >= 6) loops = le16(body + 4);
		} else if (!memcmp(d + p, "ANMF", 4)) {
			if (len < 16) goto bad;
			int x = u24(body) * 2, y = u24(body + 3) * 2;
			int fw = u24(body + 6) + 1, fh = u24(body + 9) + 1;
			int ms = u24(body + 12);
			uint8_t flags = body[15];
			bytes o = {0};
			still_webp(&o, body + 16, len - 16, fw, fh);
			uint8_t *rgba = NULL;
			int iw = 0, ih = 0;
			bool ok = !o.failed && still(ctx, o.b, o.n, &rgba, &iw, &ih);
			free(o.b);
			dsk_anim_frame *fr = ok ? add_frame(&fs) : NULL;
			if (!fr) {
				free(rgba);
				goto bad;
			}
			frame_of(fr, rgba, iw, ih, x, y);
			fr->delay_ms = delay(ms);
			fr->blend = !(flags & 2);
			fr->disposal = flags & 1 ? DSK_DISPOSE_BACKGROUND : DSK_DISPOSE_NONE;
		}
		p += 8 + len + (len & 1);
	}
	return finish(&fs, w, h, loops);
bad:
	free_frames(&fs);
	return NULL;
}

dsk_anim *dsk_anim_decode(const uint8_t *d, size_t n, dsk_still_decoder still, void *ctx) {
	if (n >= 4 && !memcmp(d, "GIF8", 4)) return decode_gif(d, n);
	if (n >= 8 && !memcmp(d, png_signature, 8)) return decode_apng(d, n, still, ctx);
	if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return decode_webp(d, n, still, ctx);
	return NULL;
}

// ---- Playing ----

bool dsk_anim_player_init(dsk_anim_player *p, const dsk_anim *a) {
	memset(p, 0, sizeof *p);
	p->a = a;
	p->last = -1;
	p->canvas = calloc((size_t)a->w * (size_t)a->h, 4);
	return p->canvas != NULL;
}

void dsk_anim_player_free(dsk_anim_player *p) {
	free(p->canvas);
	free(p->saved);
	memset(p, 0, sizeof *p);
}

// The part of frame f on the canvas.
static bool frame_rect(const dsk_anim *a, const dsk_anim_frame *f, int *x0, int *y0, int *x1, int *y1) {
	*x0 = f->x < 0 ? 0 : f->x, *y0 = f->y < 0 ? 0 : f->y;
	*x1 = f->x + f->w > a->w ? a->w : f->x + f->w;
	*y1 = f->y + f->h > a->h ? a->h : f->y + f->h;
	return *x1 > *x0 && *y1 > *y0;
}

static void dispose(dsk_anim_player *p, const dsk_anim_frame *f) {
	int x0, y0, x1, y1;
	if (!frame_rect(p->a, f, &x0, &y0, &x1, &y1)) return;
	size_t row = (size_t)(x1 - x0) * 4;
	if (f->disposal == DSK_DISPOSE_BACKGROUND) {
		for (int y = y0; y < y1; y++) memset(p->canvas + ((size_t)y * (size_t)p->a->w + (size_t)x0) * 4, 0, row);
	} else if (f->disposal == DSK_DISPOSE_PREVIOUS && p->saved) {
		for (int y = y0; y < y1; y++) memcpy(p->canvas + ((size_t)y * (size_t)p->a->w + (size_t)x0) * 4, p->saved + (size_t)(y - y0) * row, row);
		free(p->saved);
		p->saved = NULL;
	}
}

// over draws s over d, both RGBA not premultiplied.
static void over(uint8_t *d, const uint8_t *s) {
	uint32_t sa = s[3];
	if (sa == 255) {
		memcpy(d, s, 4);
		return;
	}
	if (!sa) return;
	uint32_t da = d[3] * (255 - sa) / 255, oa = sa + da;
	for (int c = 0; c < 3; c++) d[c] = (uint8_t)((s[c] * sa + d[c] * da + oa / 2) / oa);
	d[3] = (uint8_t)oa;
}

bool dsk_anim_next(dsk_anim_player *p, int *delay_ms) {
	const dsk_anim *a = p->a;
	if (p->next >= a->n) {
		p->loop++;
		if (a->loops > 0 && p->loop >= a->loops) return false;
		p->next = 0;
	}
	if (p->last >= 0) dispose(p, &a->frames[p->last]);
	if (p->next == 0) memset(p->canvas, 0, (size_t)a->w * (size_t)a->h * 4);  // a loop starts on a clear canvas
	const dsk_anim_frame *f = &a->frames[p->next];
	int x0, y0, x1, y1;
	if (frame_rect(a, f, &x0, &y0, &x1, &y1)) {
		size_t row = (size_t)(x1 - x0) * 4;
		if (f->disposal == DSK_DISPOSE_PREVIOUS) {
			free(p->saved);
			p->saved = malloc(row * (size_t)(y1 - y0));
			if (p->saved)
				for (int y = y0; y < y1; y++) memcpy(p->saved + (size_t)(y - y0) * row, p->canvas + ((size_t)y * (size_t)a->w + (size_t)x0) * 4, row);
		}
		for (int y = y0; y < y1; y++) {
			uint8_t *d = p->canvas + ((size_t)y * (size_t)a->w + (size_t)x0) * 4;
			const uint8_t *s = f->rgba + ((size_t)(y - f->y) * (size_t)f->w + (size_t)(x0 - f->x)) * 4;
			if (f->blend)
				for (int x = x0; x < x1; x++, d += 4, s += 4) over(d, s);
			else
				memcpy(d, s, row);
		}
	}
	p->last = (int)p->next;
	p->next++;
	if (delay_ms) *delay_ms = f->delay_ms;
	return true;
}
