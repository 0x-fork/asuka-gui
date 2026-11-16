// anim.c: anim.h's decoders and player (internal/animimage's port).
#include "anim.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int u24(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16; }

static void *xmalloc(size_t n) {
	void *p = calloc(1, n ? n : 1);
	if (!p) abort();
	return p;
}

// delay is a frame's time, as browsers show it: 10 ms or less is 100 ms.
static int delay(int ms) { return ms <= 10 ? 100 : ms; }

static void push(dsk_anim *a, dsk_frame f) {
	if ((a->count & (a->count - 1)) == 0) {
		size_t cap = a->count ? a->count * 2 : 8;
		dsk_frame *p = realloc(a->frames, cap * sizeof *p);
		if (!p) abort();
		a->frames = p;
	}
	a->frames[a->count++] = f;
}

void dsk_anim_free(dsk_anim *a) {
	if (!a) return;
	for (size_t i = 0; i < a->count; i++) free(a->frames[i].rgba);
	free(a->frames);
	free(a);
}

static const uint8_t png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

bool dsk_anim_is_animated(const uint8_t *d, size_t n) {
	if (n >= 4 && !memcmp(d, "GIF8", 4)) {
		// More than one graphic control extension is likelier than not.
		int count = 0;
		for (size_t i = 0; i + 2 < n && count < 2; i++)
			if (d[i] == 0x21 && d[i + 1] == 0xf9 && d[i + 2] == 0x04) count++;
		return count > 1;
	}
	if (n >= 8 && !memcmp(d, png_sig, 8)) {
		for (size_t p = 8; p + 12 <= n;) {
			uint32_t len = be32(d + p);
			if (p + 12 + len > n) break;
			const uint8_t *t = d + p + 4;
			if (!memcmp(t, "acTL", 4)) return len >= 4 && be32(d + p + 8) > 1;
			if (!memcmp(t, "IDAT", 4)) return false;
			p += 12 + len;
		}
		return false;
	}
	return n >= 21 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBPVP8X", 8) && (d[20] & 2);
}

// ---- GIF ----

typedef struct {
	const uint8_t *p, *end;
} rd;

static int rd_u8(rd *r) { return r->p < r->end ? *r->p++ : -1; }

// skip_blocks skips data sub-blocks to their terminator.
static bool skip_blocks(rd *r) {
	for (;;) {
		int n = rd_u8(r);
		if (n < 0) return false;
		if (n == 0) return true;
		if (r->end - r->p < n) return false;
		r->p += n;
	}
}

// lzw decodes a GIF image's LZW data (its sub-blocks) into out (npix
// color indices); false if it's broken.
static bool lzw(rd *r, int min_code, uint8_t *out, size_t npix) {
	if (min_code < 2 || min_code > 11) return false;
	enum { MAX = 4096 };
	static __thread uint16_t prefix[MAX];
	static __thread uint8_t suffix[MAX], stack[MAX + 1];
	int clear = 1 << min_code, eoi = clear + 1;
	int size = min_code + 1, next = eoi + 1, old = -1;
	uint8_t first = 0;
	uint32_t bits = 0;
	int nbits = 0;
	size_t o = 0;
	int block = 0;
	for (int i = 0; i < clear; i++) {
		prefix[i] = 0xffff;
		suffix[i] = (uint8_t)i;
	}
	for (;;) {
		while (nbits < size) {
			if (block == 0) {
				block = rd_u8(r);
				if (block <= 0) goto done;
			}
			int b = rd_u8(r);
			if (b < 0) return false;
			block--;
			bits |= (uint32_t)b << nbits;
			nbits += 8;
		}
		int code = (int)(bits & ((1u << size) - 1));
		bits >>= size;
		nbits -= size;
		if (code == clear) {
			size = min_code + 1;
			next = eoi + 1;
			old = -1;
			continue;
		}
		if (code == eoi) break;
		int in = code, sp = 0;
		if (old < 0) {
			if (code >= clear) return false;
			first = (uint8_t)code;
			if (o < npix) out[o++] = first;
			old = code;
			continue;
		}
		if (code >= next) {
			if (code != next) return false;
			stack[sp++] = first;
			code = old;
		}
		while (code >= clear) {
			if (sp >= MAX || code >= MAX) return false;
			stack[sp++] = suffix[code];
			code = prefix[code];
		}
		first = suffix[code];
		stack[sp++] = first;
		while (sp > 0 && o < npix) out[o++] = stack[--sp];
		if (next < MAX) {
			prefix[next] = (uint16_t)old;
			suffix[next] = first;
			next++;
			if (next == 1 << size && size < 12) size++;
		}
		old = in;
	}
	// The rest of the image's sub-blocks.
	while (block > 0 && r->p < r->end) {
		r->p++;
		block--;
	}
	skip_blocks(r);
	return true;
done:
	return true;
}

static dsk_anim *decode_gif(const uint8_t *d, size_t n) {
	if (n < 13) return NULL;
	rd r = {d + 6, d + n};
	int w = r.p[0] | r.p[1] << 8, h = r.p[2] | r.p[3] << 8;
	int packed = r.p[4], bg = r.p[5];
	(void)bg;
	r.p += 7;
	uint8_t gct[768];
	int gct_n = 0;
	if (packed & 0x80) {
		gct_n = 2 << (packed & 7);
		if (r.end - r.p < gct_n * 3) return NULL;
		memcpy(gct, r.p, (size_t)gct_n * 3);
		r.p += gct_n * 3;
	}
	dsk_anim *a = xmalloc(sizeof *a);
	a->width = w;
	a->height = h;
	a->loops = 1;  // no NETSCAPE extension: once
	int g_delay = 0, g_disposal = 0, g_trans = -1;
	for (;;) {
		int b = rd_u8(&r);
		if (b < 0 || b == 0x3b) break;
		if (b == 0x21) {
			int label = rd_u8(&r);
			if (label == 0xf9) {
				int len = rd_u8(&r);
				if (len >= 4 && r.end - r.p >= len) {
					int f = r.p[0];
					g_delay = (r.p[1] | r.p[2] << 8) * 10;
					g_disposal = (f >> 2) & 7;
					g_trans = (f & 1) ? r.p[3] : -1;
					r.p += len;
				}
				if (!skip_blocks(&r)) break;
			} else if (label == 0xff) {
				int len = rd_u8(&r);
				if (len == 11 && r.end - r.p >= 11 && (!memcmp(r.p, "NETSCAPE2.0", 11) || !memcmp(r.p, "ANIMEXTS1.0", 11))) {
					r.p += 11;
					int sub = rd_u8(&r);
					if (sub >= 3 && r.end - r.p >= sub && r.p[0] == 1) {
						int count = r.p[1] | r.p[2] << 8;
						a->loops = count == 0 ? 0 : count + 1;  // Go's LoopCount semantics
					}
					if (sub > 0) r.p += sub < r.end - r.p ? sub : r.end - r.p;
				} else if (len > 0) {
					r.p += len < r.end - r.p ? len : r.end - r.p;
				}
				if (!skip_blocks(&r)) break;
			} else {
				if (!skip_blocks(&r)) break;
			}
			continue;
		}
		if (b != 0x2c || r.end - r.p < 9) break;
		int fx = r.p[0] | r.p[1] << 8, fy = r.p[2] | r.p[3] << 8;
		int fw = r.p[4] | r.p[5] << 8, fh = r.p[6] | r.p[7] << 8;
		int fp = r.p[8];
		r.p += 9;
		const uint8_t *ct = gct;
		int ct_n = gct_n;
		uint8_t lct[768];
		if (fp & 0x80) {
			ct_n = 2 << (fp & 7);
			if (r.end - r.p < ct_n * 3) break;
			memcpy(lct, r.p, (size_t)ct_n * 3);
			r.p += ct_n * 3;
			ct = lct;
		}
		int min_code = rd_u8(&r);
		if (fw <= 0 || fh <= 0 || (size_t)fw * (size_t)fh > 64u * 1024 * 1024) break;
		size_t npix = (size_t)fw * (size_t)fh;
		uint8_t *idx = xmalloc(npix);
		if (!lzw(&r, min_code, idx, npix)) {
			free(idx);
			break;
		}
		uint8_t *rgba = xmalloc(npix * 4);
		bool interlaced = fp & 0x40;
		static const int start[] = {0, 4, 2, 1}, step[] = {8, 8, 4, 2};
		int row = 0, pass = 0;
		for (int y = 0; y < fh; y++) {
			int dy = y;
			if (interlaced) {
				while (pass < 4 && row >= fh) {
					pass++;
					if (pass < 4) row = start[pass];
				}
				dy = row;
				row += step[pass < 4 ? pass : 3];
			}
			if (dy >= fh) continue;
			for (int x = 0; x < fw; x++) {
				int c = idx[(size_t)y * fw + x];
				uint8_t *px = rgba + ((size_t)dy * fw + x) * 4;
				if (c == g_trans || c >= ct_n) {
					px[0] = px[1] = px[2] = px[3] = 0;
				} else {
					px[0] = ct[c * 3];
					px[1] = ct[c * 3 + 1];
					px[2] = ct[c * 3 + 2];
					px[3] = 255;
				}
			}
		}
		free(idx);
		dsk_frame f = {rgba, fx, fy, fw, fh, delay(g_delay), DSK_DISPOSE_NONE, true};
		if (g_disposal == 2) f.disposal = DSK_DISPOSE_BACKGROUND;
		if (g_disposal == 3) f.disposal = DSK_DISPOSE_PREVIOUS;
		if (a->width == 0 || a->height == 0) {
			if (fx + fw > a->width) a->width = fx + fw;
			if (fy + fh > a->height) a->height = fy + fh;
		}
		push(a, f);
		g_delay = 0;
		g_disposal = 0;
		g_trans = -1;
	}
	if (a->count < 2) {
		dsk_anim_free(a);
		return NULL;
	}
	return a;
}

// ---- APNG ----

static uint32_t crc_table[256];

static uint32_t crc32(const uint8_t *p, size_t n) {
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			crc_table[i] = c;
		}
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

typedef struct {
	uint8_t *p;
	size_t len, cap;
} bytes;

static void put(bytes *b, const void *p, size_t n) {
	if (b->len + n > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 1024;
		while (cap < b->len + n) cap *= 2;
		uint8_t *q = realloc(b->p, cap);
		if (!q) abort();
		b->p = q;
		b->cap = cap;
	}
	memcpy(b->p + b->len, p, n);
	b->len += n;
}

static void put_be32(bytes *b, uint32_t v) {
	uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
	put(b, x, 4);
}

static void put_le32(bytes *b, uint32_t v) {
	uint8_t x[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
	put(b, x, 4);
}

static void chunk(bytes *b, const char *type, const uint8_t *data, size_t n) {
	put_be32(b, (uint32_t)n);
	size_t start = b->len;
	put(b, type, 4);
	if (n) put(b, data, n);
	put_be32(b, crc32(b->p + start, b->len - start));
}

// moved puts a frame's decoded pixels at x, y on a canvas of the
// animation's size (cut to it).
static void place(dsk_frame *f, uint8_t *rgba, int w, int h, int x, int y) {
	f->rgba = rgba;
	f->x = x;
	f->y = y;
	f->w = w;
	f->h = h;
}

static dsk_anim *decode_apng(const uint8_t *d, size_t n, dsk_still still, void *ctx) {
	const uint8_t *ihdr = NULL;
	typedef struct {
		const uint8_t *type, *data;
		uint32_t len;
	} ck;
	ck shared[64];
	int nshared = 0;
	typedef struct {
		const uint8_t *fctl;
		ck idat[256];
		int nidat;
	} raw;
	int cap = 16, count = 0;
	raw *frames = xmalloc((size_t)cap * sizeof *frames);
	raw *cur = NULL;
	int plays = 0, num_frames = 0;
	bool seen_idat = false, idat_is_frame = false;
	for (size_t p = 8; p + 12 <= n;) {
		uint32_t len = be32(d + p);
		if (p + 12 + len > n) break;
		const uint8_t *t = d + p + 4, *data = d + p + 8;
		if (!memcmp(t, "IHDR", 4)) {
			if (len >= 13) ihdr = data;
		} else if (!memcmp(t, "acTL", 4)) {
			if (len >= 8) {
				num_frames = (int)be32(data);
				plays = (int)be32(data + 4);
			}
		} else if (!memcmp(t, "fcTL", 4)) {
			if (len < 26) {
				free(frames);
				return NULL;
			}
			if (count == cap) {
				cap *= 2;
				raw *q = realloc(frames, (size_t)cap * sizeof *frames);
				if (!q) abort();
				frames = q;
			}
			cur = &frames[count++];
			cur->fctl = data;
			cur->nidat = 0;
			if (!seen_idat) idat_is_frame = true;
		} else if (!memcmp(t, "IDAT", 4)) {
			seen_idat = true;
			if (idat_is_frame && cur && cur->nidat < 256) cur->idat[cur->nidat++] = (ck){t, data, len};
		} else if (!memcmp(t, "fdAT", 4)) {
			if (cur && len >= 4 && cur->nidat < 256) cur->idat[cur->nidat++] = (ck){t, data + 4, len - 4};
		} else if (memcmp(t, "IEND", 4) && !seen_idat && nshared < 64) {
			// PLTE, tRNS, gAMA, iCCP...: what every frame's pixels need.
			shared[nshared++] = (ck){t, data, len};
		}
		p += 12 + len;
	}
	if (!ihdr || num_frames < 2 || count < 2 || !still) {
		free(frames);
		return NULL;
	}
	dsk_anim *a = xmalloc(sizeof *a);
	a->width = (int)be32(ihdr);
	a->height = (int)be32(ihdr + 4);
	a->loops = plays;
	for (int i = 0; i < count; i++) {
		raw *f = &frames[i];
		if (!f->nidat) continue;
		const uint8_t *fc = f->fctl;
		uint32_t w = be32(fc + 4), h = be32(fc + 8);
		int x = (int)be32(fc + 12), y = (int)be32(fc + 16);
		uint16_t num = be16(fc + 20), den = be16(fc + 22);
		if (!den) den = 100;
		uint8_t hdr[13];
		memcpy(hdr, ihdr, 13);
		hdr[0] = (uint8_t)(w >> 24), hdr[1] = (uint8_t)(w >> 16), hdr[2] = (uint8_t)(w >> 8), hdr[3] = (uint8_t)w;
		hdr[4] = (uint8_t)(h >> 24), hdr[5] = (uint8_t)(h >> 16), hdr[6] = (uint8_t)(h >> 8), hdr[7] = (uint8_t)h;
		bytes b = {0};
		put(&b, png_sig, 8);
		chunk(&b, "IHDR", hdr, 13);
		for (int k = 0; k < nshared; k++) chunk(&b, (const char *)shared[k].type, shared[k].data, shared[k].len);
		for (int k = 0; k < f->nidat; k++) chunk(&b, "IDAT", f->idat[k].data, f->idat[k].len);
		chunk(&b, "IEND", NULL, 0);
		int dw = 0, dh = 0;
		uint8_t *rgba = still(ctx, b.p, b.len, &dw, &dh);
		free(b.p);
		if (!rgba) {
			free(frames);
			dsk_anim_free(a);
			return NULL;
		}
		dsk_frame fr = {0};
		place(&fr, rgba, dw, dh, x, y);
		fr.delay_ms = delay((int)((uint32_t)num * 1000 / den));
		fr.blend = fc[25] == 1;
		if (fc[24] == 1) fr.disposal = DSK_DISPOSE_BACKGROUND;
		if (fc[24] == 2) fr.disposal = DSK_DISPOSE_PREVIOUS;
		push(a, fr);
	}
	free(frames);
	if (a->count < 2) {
		dsk_anim_free(a);
		return NULL;
	}
	return a;
}

// ---- WebP ----

// still_webp is a frame's chunks as a WebP file: with an ALPH chunk, an
// extended one (VP8X with the alpha flag) of its size.
static void still_webp(bytes *out, const uint8_t *chunks, size_t n, int w, int h) {
	bool alpha = n >= 4 && !memcmp(chunks, "ALPH", 4);
	bytes body = {0};
	put(&body, "WEBP", 4);
	if (alpha) {
		uint8_t x[10] = {0x10, 0, 0, 0, (uint8_t)(w - 1), (uint8_t)((w - 1) >> 8), (uint8_t)((w - 1) >> 16), (uint8_t)(h - 1), (uint8_t)((h - 1) >> 8), (uint8_t)((h - 1) >> 16)};
		put(&body, "VP8X", 4);
		put_le32(&body, 10);
		put(&body, x, 10);
	}
	put(&body, chunks, n);
	put(out, "RIFF", 4);
	put_le32(out, (uint32_t)body.len);
	put(out, body.p, body.len);
	free(body.p);
}

static dsk_anim *decode_webp(const uint8_t *d, size_t n, dsk_still still, void *ctx) {
	if (!still || n < 30 || memcmp(d + 12, "VP8X", 4) || !(d[20] & 2)) return NULL;
	dsk_anim *a = xmalloc(sizeof *a);
	a->width = u24(d + 24) + 1;
	a->height = u24(d + 27) + 1;
	for (size_t p = 12; p + 8 <= n;) {
		uint32_t len = le32(d + p + 4);
		if (p + 8 + len > n) break;
		const uint8_t *body = d + p + 8;
		if (!memcmp(d + p, "ANIM", 4)) {
			if (len >= 6) a->loops = body[4] | body[5] << 8;
		} else if (!memcmp(d + p, "ANMF", 4)) {
			if (len < 16) {
				dsk_anim_free(a);
				return NULL;
			}
			int x = u24(body) * 2, y = u24(body + 3) * 2;
			int w = u24(body + 6) + 1, h = u24(body + 9) + 1;
			int ms = u24(body + 12);
			uint8_t flags = body[15];
			bytes file = {0};
			still_webp(&file, body + 16, len - 16, w, h);
			int dw = 0, dh = 0;
			uint8_t *rgba = still(ctx, file.p, file.len, &dw, &dh);
			free(file.p);
			if (!rgba) {
				dsk_anim_free(a);
				return NULL;
			}
			dsk_frame f = {0};
			place(&f, rgba, dw, dh, x, y);
			f.delay_ms = delay(ms);
			f.blend = !(flags & 2);
			if (flags & 1) f.disposal = DSK_DISPOSE_BACKGROUND;
			push(a, f);
		}
		p += 8 + len + (len & 1);
	}
	if (a->count < 2) {
		dsk_anim_free(a);
		return NULL;
	}
	return a;
}

dsk_anim *dsk_anim_decode(const uint8_t *d, size_t n, dsk_still still, void *ctx) {
	if (n >= 4 && !memcmp(d, "GIF8", 4)) return decode_gif(d, n);
	if (n >= 8 && !memcmp(d, png_sig, 8)) return decode_apng(d, n, still, ctx);
	if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return decode_webp(d, n, still, ctx);
	return NULL;
}

// ---- The player ----

void dsk_player_init(dsk_player *p, const dsk_anim *a) {
	memset(p, 0, sizeof *p);
	p->a = a;
	p->canvas = xmalloc((size_t)a->width * (size_t)a->height * 4);
	p->last = -1;
}

void dsk_player_free(dsk_player *p) {
	free(p->canvas);
	free(p->saved);
	p->canvas = p->saved = NULL;
}

// clip is a frame's rectangle cut to the canvas.
static bool clip(const dsk_player *p, const dsk_frame *f, int *x0, int *y0, int *x1, int *y1) {
	*x0 = f->x > 0 ? f->x : 0;
	*y0 = f->y > 0 ? f->y : 0;
	*x1 = f->x + f->w < p->a->width ? f->x + f->w : p->a->width;
	*y1 = f->y + f->h < p->a->height ? f->y + f->h : p->a->height;
	return *x0 < *x1 && *y0 < *y1;
}

static void dispose(dsk_player *p, const dsk_frame *f) {
	int x0, y0, x1, y1;
	if (!clip(p, f, &x0, &y0, &x1, &y1)) return;
	size_t stride = (size_t)p->a->width * 4, row = (size_t)(x1 - x0) * 4;
	if (f->disposal == DSK_DISPOSE_BACKGROUND) {
		for (int y = y0; y < y1; y++) memset(p->canvas + y * stride + (size_t)x0 * 4, 0, row);
	} else if (f->disposal == DSK_DISPOSE_PREVIOUS && p->saved) {
		for (int y = y0; y < y1; y++) memcpy(p->canvas + y * stride + (size_t)x0 * 4, p->saved + (size_t)(y - y0) * row, row);
		free(p->saved);
		p->saved = NULL;
	}
}

int dsk_player_next(dsk_player *p) {
	const dsk_anim *a = p->a;
	if (p->next >= a->count) {
		p->loop++;
		if (a->loops > 0 && p->loop >= a->loops) return -1;
		p->next = 0;
	}
	if (p->last >= 0) dispose(p, &a->frames[p->last]);
	size_t stride = (size_t)a->width * 4;
	if (p->next == 0) memset(p->canvas, 0, stride * (size_t)a->height);  // a loop starts on a clear canvas
	const dsk_frame *f = &a->frames[p->next];
	int x0, y0, x1, y1;
	if (clip(p, f, &x0, &y0, &x1, &y1)) {
		size_t row = (size_t)(x1 - x0) * 4;
		if (f->disposal == DSK_DISPOSE_PREVIOUS) {
			free(p->saved);
			p->saved = xmalloc(row * (size_t)(y1 - y0));
			for (int y = y0; y < y1; y++) memcpy(p->saved + (size_t)(y - y0) * row, p->canvas + y * stride + (size_t)x0 * 4, row);
		}
		for (int y = y0; y < y1; y++) {
			uint8_t *dst = p->canvas + y * stride + (size_t)x0 * 4;
			const uint8_t *src = f->rgba + ((size_t)(y - f->y) * f->w + (size_t)(x0 - f->x)) * 4;
			if (!f->blend) {
				memcpy(dst, src, row);
				continue;
			}
			for (int x = 0; x < x1 - x0; x++, dst += 4, src += 4) {
				// Source over, not premultiplied.
				uint32_t sa = src[3], da = dst[3];
				if (sa == 255) {
					memcpy(dst, src, 4);
					continue;
				}
				if (sa == 0) continue;
				uint32_t oa = sa * 255 + da * (255 - sa);  // ×255
				for (int c = 0; c < 3; c++) dst[c] = (uint8_t)((src[c] * sa * 255 + dst[c] * da * (255 - sa)) / oa);
				dst[3] = (uint8_t)((oa + 127) / 255);
			}
		}
	}
	p->last = (int)p->next;
	p->next++;
	return f->delay_ms;
}
