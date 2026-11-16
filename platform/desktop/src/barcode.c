// barcode.c (platform/desktop/src): asuka_barcode.h, barcodes and QR
// codes read in images in C alone, a port of the Go host's package
// barcode (internal/barcode), which the Go tests check this against:
// QR codes, Data Matrix, EAN-13, EAN-8, UPC-A, UPC-E, Code 128 and
// Code 39, upright or turned by any angle. Built on every platform.
#include "asuka_barcode.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

extern const uint16_t asuka_barcode_jis[94 * 94];

// ---- Helpers ----

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static int iabs(int v) { return v < 0 ? -v : v; }

// ifloor and itrunc are a double's floor and its truncation as an int,
// as Go's int() of one: a value out of range (or NaN) is far outside
// any image.
static int itrunc(double v) {
	if (!(v > -1e9 && v < 1e9)) return -1000000000;
	return (int)v;
}
static int ifloor(double v) { return itrunc(floor(v)); }

// A growing array of malloc's.
#define VEC(T) \
	struct {   \
		T *v;  \
		size_t n, cap; \
	}
#define VPUSH(a, x)                                                          \
	do {                                                                 \
		if ((a).n == (a).cap) {                                      \
			(a).cap = (a).cap ? (a).cap * 2 : 16;                \
			(a).v = realloc((a).v, (a).cap * sizeof *(a).v);    \
		}                                                            \
		(a).v[(a).n++] = (x);                                        \
	} while (0)
#define VFREE(a) (free((a).v), (a).v = NULL, (a).n = (a).cap = 0)

// A byte buffer.
typedef VEC(uint8_t) bytes;

static void bput(bytes *b, uint8_t c) { VPUSH(*b, c); }
static void bputs(bytes *b, const char *s, size_t n) {
	for (size_t i = 0; i < n; i++) bput(b, (uint8_t)s[i]);
}
static void bput_rune(bytes *b, uint32_t r) {
	if (r > 0x10FFFF || (r >= 0xD800 && r <= 0xDFFF)) r = 0xFFFD;
	if (r < 0x80) {
		bput(b, (uint8_t)r);
	} else if (r < 0x800) {
		bput(b, (uint8_t)(0xC0 | r >> 6));
		bput(b, (uint8_t)(0x80 | (r & 0x3F)));
	} else if (r < 0x10000) {
		bput(b, (uint8_t)(0xE0 | r >> 12));
		bput(b, (uint8_t)(0x80 | ((r >> 6) & 0x3F)));
		bput(b, (uint8_t)(0x80 | (r & 0x3F)));
	} else {
		bput(b, (uint8_t)(0xF0 | r >> 18));
		bput(b, (uint8_t)(0x80 | ((r >> 12) & 0x3F)));
		bput(b, (uint8_t)(0x80 | ((r >> 6) & 0x3F)));
		bput(b, (uint8_t)(0x80 | (r & 0x3F)));
	}
}
static char *bstr(bytes *b) {
	char *s = malloc(b->n + 1);
	if (b->n) memcpy(s, b->v, b->n);
	s[b->n] = 0;
	return s;
}

// A point in an image's pixels.
typedef struct {
	double x, y;
} point;

static double dist(point a, point b) { return hypot(a.x - b.x, a.y - b.y); }

// A code found, with its kind's bit and its box as floats until the end.
typedef struct {
	unsigned kind;
	char *text;
	int x0, y0, x1, y1;
} code;

typedef VEC(code) codes;

static void codes_free(codes *cs) {
	for (size_t i = 0; i < cs->n; i++) free(cs->v[i].text);
	VFREE(*cs);
}

// ---- Kinds ----

static const struct {
	const char *name;
	unsigned bit;
} kinds[] = {
    {"qr", ASUKA_BARCODE_QR},       {"datamatrix", ASUKA_BARCODE_DATAMATRIX}, {"ean13", ASUKA_BARCODE_EAN13},     {"ean8", ASUKA_BARCODE_EAN8},
    {"upca", ASUKA_BARCODE_UPCA},   {"upce", ASUKA_BARCODE_UPCE},             {"code128", ASUKA_BARCODE_CODE128}, {"code39", ASUKA_BARCODE_CODE39},
};

unsigned asuka_barcode_kind(const char *name) {
	for (size_t i = 0; name && i < sizeof kinds / sizeof *kinds; i++)
		if (!strcmp(kinds[i].name, name)) return kinds[i].bit;
	return 0;
}

const char *asuka_barcode_name(unsigned kind) {
	for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++)
		if (kinds[i].bit == kind) return kinds[i].name;
	return "";
}

#define K1D (ASUKA_BARCODE_EAN13 | ASUKA_BARCODE_EAN8 | ASUKA_BARCODE_UPCA | ASUKA_BARCODE_UPCE | ASUKA_BARCODE_CODE128 | ASUKA_BARCODE_CODE39)

// ---- Images ----

// luminance is an image's gray levels, one byte a pixel, row by row.
typedef struct {
	int w, h;
	uint8_t *pix;
} luminance;

// bitImage is a black and white image: one byte a pixel, 1 for dark.
typedef struct {
	int w, h;
	uint8_t *pix;
} bitImage;

// at reports whether (x, y) is dark; outside the image is light.
static bool at(const bitImage *b, int x, int y) {
	if (x < 0 || y < 0 || x >= b->w || y >= b->h) return false;
	return b->pix[y * b->w + x] != 0;
}

enum { blockPower = 3, blockSize = 1 << blockPower, minDynRange = 24, minLocalSize = 5 * blockSize };

static int clampBlock(int v, int hi) {
	if (v < 2) return 2;
	return MIN(v, hi);
}

// globalThreshold thresholds a small image at the valley between its
// histogram's two peaks.
static void globalThreshold(const luminance *l, bitImage *b) {
	int hist[32] = {0};
	size_t n = (size_t)l->w * l->h;
	for (size_t i = 0; i < n; i++) hist[l->pix[i] >> 3]++;
	int first = 0, firstN = 0;
	for (int i = 0; i < 32; i++)
		if (hist[i] > firstN) first = i, firstN = hist[i];
	int second = 0, best = 0;
	for (int i = 0; i < 32; i++) {
		int d = i - first;
		int s = d * d * hist[i];
		if (s > best) second = i, best = s;
	}
	if (first > second) {
		int t = first;
		first = second;
		second = t;
	}
	int t = (first + second) / 2;
	if (second - first > 1) {
		// The emptiest bucket between the peaks, nearer the light one.
		int bestV = -1;
		for (int i = second - 1; i > first; i--) {
			int d = i - first;
			int s = d * d * (second - i) * (firstN - hist[i]);
			if (s > bestV) t = i, bestV = s;
		}
	}
	uint8_t th = (uint8_t)(t << 3);
	for (size_t i = 0; i < n; i++) b->pix[i] = l->pix[i] < th ? 1 : 0;
}

// binarize thresholds l locally, as ZXing's HybridBinarizer does: each 8
// by 8 block against the mean black point of the 5 by 5 blocks around
// it, so uneven light and shadows don't matter.
static bitImage binarize(const luminance *l) {
	int w = l->w, h = l->h;
	bitImage b = {w, h, malloc((size_t)w * h)};
	if (w < minLocalSize || h < minLocalSize) {
		globalThreshold(l, &b);
		return b;
	}
	int subW = (w + blockSize - 1) >> blockPower, subH = (h + blockSize - 1) >> blockPower;
	int32_t *black = malloc((size_t)subW * subH * sizeof *black);
	int maxX = w - blockSize, maxY = h - blockSize;
	for (int y = 0; y < subH; y++) {
		int yo = MIN(y << blockPower, maxY);
		for (int x = 0; x < subW; x++) {
			int xo = MIN(x << blockPower, maxX);
			int sum = 0, lo = 255, hi = 0;
			for (int yy = 0; yy < blockSize; yy++) {
				const uint8_t *row = l->pix + (size_t)(yo + yy) * w + xo;
				for (int i = 0; i < blockSize; i++) {
					int v = row[i];
					sum += v;
					lo = MIN(lo, v);
					hi = MAX(hi, v);
				}
			}
			int avg = sum >> (2 * blockPower);
			if (hi - lo <= minDynRange) {
				// A flat block: it's light unless its neighbors say its
				// level is dark.
				avg = lo / 2;
				if (y > 0 && x > 0) {
					int n = (black[(y - 1) * subW + x] + 2 * black[y * subW + x - 1] + black[(y - 1) * subW + x - 1]) / 4;
					if (lo < n) avg = n;
				}
			}
			black[y * subW + x] = avg;
		}
	}
	for (int y = 0; y < subH; y++) {
		int yo = MIN(y << blockPower, maxY);
		int top = clampBlock(y, subH - 3);
		for (int x = 0; x < subW; x++) {
			int xo = MIN(x << blockPower, maxX);
			int left = clampBlock(x, subW - 3);
			int sum = 0;
			for (int z = -2; z <= 2; z++) {
				const int32_t *r = black + (top + z) * subW + left - 2;
				sum += r[0] + r[1] + r[2] + r[3] + r[4];
			}
			uint8_t t = (uint8_t)MIN(sum / 25, 255);
			for (int yy = 0; yy < blockSize; yy++) {
				const uint8_t *src = l->pix + (size_t)(yo + yy) * w + xo;
				uint8_t *dst = b.pix + (size_t)(yo + yy) * w + xo;
				for (int i = 0; i < blockSize; i++) dst[i] = src[i] <= t ? 1 : 0;
			}
		}
	}
	free(black);
	return b;
}

// ---- Perspective ----

// perspective maps a plane to another: (x, y) to
// ((a11 x + a21 y + a31) / d, (a12 x + a22 y + a32) / d), with
// d = a13 x + a23 y + a33.
typedef struct {
	double a11, a12, a13, a21, a22, a23, a31, a32, a33;
} perspective;

static void apply(const perspective *t, double x, double y, double *ox, double *oy) {
	double d = t->a13 * x + t->a23 * y + t->a33;
	*ox = (t->a11 * x + t->a21 * y + t->a31) / d;
	*oy = (t->a12 * x + t->a22 * y + t->a32) / d;
}

// squareToQuad maps the unit square's (0,0), (1,0), (1,1), (0,1) to the
// corners given.
static perspective squareToQuad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3) {
	double dx3 = x0 - x1 + x2 - x3, dy3 = y0 - y1 + y2 - y3;
	if (dx3 == 0 && dy3 == 0) return (perspective){x1 - x0, y1 - y0, 0, x2 - x1, y2 - y1, 0, x0, y0, 1};
	double dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
	double den = dx1 * dy2 - dx2 * dy1;
	double a13 = (dx3 * dy2 - dx2 * dy3) / den, a23 = (dx1 * dy3 - dx3 * dy1) / den;
	return (perspective){x1 - x0 + a13 * x1, y1 - y0 + a13 * y1, a13, x3 - x0 + a23 * x3, y3 - y0 + a23 * y3, a23, x0, y0, 1};
}

static perspective adjoint(perspective t) {
	perspective r;
	r.a11 = t.a22 * t.a33 - t.a23 * t.a32;
	r.a21 = t.a23 * t.a31 - t.a21 * t.a33;
	r.a31 = t.a21 * t.a32 - t.a22 * t.a31;
	r.a12 = t.a13 * t.a32 - t.a12 * t.a33;
	r.a22 = t.a11 * t.a33 - t.a13 * t.a31;
	r.a32 = t.a12 * t.a31 - t.a11 * t.a32;
	r.a13 = t.a12 * t.a23 - t.a13 * t.a22;
	r.a23 = t.a13 * t.a21 - t.a11 * t.a23;
	r.a33 = t.a11 * t.a22 - t.a12 * t.a21;
	return r;
}

// times is t after o.
static perspective times(const perspective *t, const perspective *o) {
	perspective r;
	r.a11 = t->a11 * o->a11 + t->a21 * o->a12 + t->a31 * o->a13;
	r.a21 = t->a11 * o->a21 + t->a21 * o->a22 + t->a31 * o->a23;
	r.a31 = t->a11 * o->a31 + t->a21 * o->a32 + t->a31 * o->a33;
	r.a12 = t->a12 * o->a11 + t->a22 * o->a12 + t->a32 * o->a13;
	r.a22 = t->a12 * o->a21 + t->a22 * o->a22 + t->a32 * o->a23;
	r.a32 = t->a12 * o->a31 + t->a22 * o->a32 + t->a32 * o->a33;
	r.a13 = t->a13 * o->a11 + t->a23 * o->a12 + t->a33 * o->a13;
	r.a23 = t->a13 * o->a21 + t->a23 * o->a22 + t->a33 * o->a23;
	r.a33 = t->a13 * o->a31 + t->a23 * o->a32 + t->a33 * o->a33;
	return r;
}

// quadToQuad maps the quadrilateral of the first four points to that of
// the next four, corner to corner.
static perspective quadToQuad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, double u0, double v0, double u1,
                              double v1, double u2, double v2, double u3, double v3) {
	perspective q = adjoint(squareToQuad(x0, y0, x1, y1, x2, y2, x3, y3));
	perspective s = squareToQuad(u0, v0, u1, v1, u2, v2, u3, v3);
	return times(&s, &q);
}

// grid is a code's modules, sampled: dark is 1.
typedef struct {
	int w, h;
	uint8_t *bits;
} grid;

static grid newGrid(int w, int h) { return (grid){w, h, calloc((size_t)w * h, 1)}; }

static bool gat(const grid *g, int x, int y) { return g->bits[y * g->w + x] != 0; }

// sampleGrid reads w by h modules of b, module (x, y)'s center being
// t(x+0.5, y+0.5). It fails if too many fall outside the image.
static bool sampleGrid(const bitImage *b, const perspective *t, int w, int h, grid *out) {
	grid g = newGrid(w, h);
	int outside = 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			double px, py;
			apply(t, x + 0.5, y + 0.5, &px, &py);
			if (isnan(px) || isnan(py)) {
				free(g.bits);
				return false;
			}
			int ix = ifloor(px), iy = ifloor(py);
			if (ix < -1 || iy < -1 || ix > b->w || iy > b->h) {
				free(g.bits);
				return false;
			}
			if (ix < 0 || iy < 0 || ix >= b->w || iy >= b->h) {
				// A pixel or so out: nudged in, as ZXing does.
				outside++;
				ix = MAX(0, MIN(ix, b->w - 1));
				iy = MAX(0, MIN(iy, b->h - 1));
			}
			g.bits[y * w + x] = b->pix[iy * b->w + ix];
		}
	if (outside > w + h) {
		free(g.bits);
		return false;
	}
	*out = g;
	return true;
}

// boundsOf is the box around the corners given, in whole pixels.
static void boundsOf(const point *ps, int n, int *x0, int *y0, int *x1, int *y1) {
	double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
	for (int i = 0; i < n; i++) {
		minX = fmin(minX, ps[i].x), maxX = fmax(maxX, ps[i].x);
		minY = fmin(minY, ps[i].y), maxY = fmax(maxY, ps[i].y);
	}
	*x0 = ifloor(minX), *y0 = ifloor(minY), *x1 = itrunc(ceil(maxX)), *y1 = itrunc(ceil(maxY));
}

// ---- Reed-Solomon ----

// gf256 is a Galois field of 256 elements, with a code's generator base:
// QR codes use the primitive polynomial 0x11D and roots from α^0, Data
// Matrix 0x12D and roots from α^1.
typedef struct {
	uint8_t exp[512], log[256];
	int base;
} gf256;

static void gfInit(gf256 *f, int poly, int base) {
	f->base = base;
	int x = 1;
	for (int i = 0; i < 255; i++) {
		f->exp[i] = (uint8_t)x;
		f->log[x] = (uint8_t)i;
		x <<= 1;
		if (x >= 256) x ^= poly;
	}
	for (int i = 255; i < 512; i++) f->exp[i] = f->exp[i - 255];
}

static uint8_t gmul(const gf256 *f, uint8_t a, uint8_t b) {
	if (!a || !b) return 0;
	return f->exp[f->log[a] + f->log[b]];
}

static uint8_t gdiv(const gf256 *f, uint8_t a, uint8_t b) {
	if (!a) return 0;
	return f->exp[f->log[a] + 255 - f->log[b]];
}

// gpow is α^e.
static uint8_t gpow(const gf256 *f, int e) {
	e %= 255;
	if (e < 0) e += 255;
	return f->exp[e];
}

static const gf256 *qrField(void) {
	static gf256 f;
	static bool made;
	if (!made) gfInit(&f, 0x11D, 0), made = true;  // idempotent: a race writes the same
	return &f;
}

static const gf256 *dmField(void) {
	static gf256 f;
	static bool made;
	if (!made) gfInit(&f, 0x12D, 1), made = true;
	return &f;
}

// rsDecode corrects up to nCheck/2 wrong codewords of block (data then
// nCheck check codewords) in place, and reports whether it could.
static bool rsDecode(const gf256 *f, uint8_t *block, int n, int nCheck) {
	if (nCheck > 255) return false;
	uint8_t syn[256];
	bool errs = false;
	for (int j = 0; j < nCheck; j++) {
		uint8_t x = gpow(f, j + f->base), s = 0;
		for (int i = 0; i < n; i++) s = gmul(f, s, x) ^ block[i];
		syn[j] = s;
		if (s) errs = true;
	}
	if (!errs) return true;
	// Berlekamp–Massey: the error locator Λ, lowest coefficient first.
	uint8_t lam[257] = {0}, prev[257] = {0}, tmp[257];
	lam[0] = prev[0] = 1;
	int l = 0, m = 1;
	uint8_t b = 1;
	for (int k = 0; k < nCheck; k++) {
		uint8_t d = syn[k];
		for (int i = 1; i <= l; i++) d ^= gmul(f, lam[i], syn[k - i]);
		if (!d) {
			m++;
			continue;
		}
		uint8_t coef = gdiv(f, d, b);
		if (2 * l <= k) {
			memcpy(tmp, lam, (size_t)nCheck + 1);
			for (int i = 0; i + m <= nCheck; i++) lam[i + m] ^= gmul(f, coef, prev[i]);
			memcpy(prev, tmp, (size_t)nCheck + 1);
			l = k + 1 - l;
			b = d;
			m = 1;
		} else {
			for (int i = 0; i + m <= nCheck; i++) lam[i + m] ^= gmul(f, coef, prev[i]);
			m++;
		}
	}
	if (l == 0 || 2 * l > nCheck) return false;
	// Ω = S·Λ mod x^nCheck.
	uint8_t om[256];
	for (int i = 0; i < nCheck; i++) {
		uint8_t v = 0;
		for (int j = 0; j <= i && j <= l; j++) v ^= gmul(f, lam[j], syn[i - j]);
		om[i] = v;
	}
	// Chien search over the block's positions, then Forney.
	int found = 0;
	for (int pos = 0; pos < n; pos++) {
		// The locator of position pos is X = α^(n-1-pos); test Λ(X⁻¹).
		int e = n - 1 - pos;
		uint8_t xinv = gpow(f, -e), v = 0, xp = 1;
		for (int i = 0; i <= l; i++) {
			v ^= gmul(f, lam[i], xp);
			xp = gmul(f, xp, xinv);
		}
		if (v) continue;
		uint8_t num = 0, den = 0;
		xp = 1;
		for (int i = 0; i < nCheck; i++) {
			num ^= gmul(f, om[i], xp);
			xp = gmul(f, xp, xinv);
		}
		// Λ' keeps the odd terms: Σ Λ_i x^(i-1), i odd.
		uint8_t xinv2 = gmul(f, xinv, xinv);
		xp = 1;
		for (int i = 1; i <= l; i += 2) {
			den ^= gmul(f, lam[i], xp);
			xp = gmul(f, xp, xinv2);
		}
		if (!den) return false;
		uint8_t mag = gdiv(f, num, den);
		// Times X^(1-base).
		mag = gmul(f, mag, gpow(f, e * (1 - f->base)));
		block[pos] ^= mag;
		found++;
	}
	if (found != l) return false;
	// Check: every syndrome is zero now.
	for (int j = 0; j < nCheck; j++) {
		uint8_t x = gpow(f, j + f->base), s = 0;
		for (int i = 0; i < n; i++) s = gmul(f, s, x) ^ block[i];
		if (s) return false;
	}
	return true;
}

// ---- Text ----

// ECI charsets the readers honor.
enum { eciNone = -1, eciLatin1 = 3, eciShiftJIS = 20, eciUTF8 = 26 };

static void latin1(bytes *out, const uint8_t *b, size_t n) {
	for (size_t i = 0; i < n; i++) bput_rune(out, b[i]);
}

// utf8_len is the length of the valid UTF-8 sequence at b (n bytes
// left), or 0.
static int utf8_len(const uint8_t *b, size_t n) {
	uint8_t c = b[0];
	if (c < 0x80) return 1;
	int len;
	uint32_t r;
	if (c >= 0xC2 && c <= 0xDF) len = 2, r = c & 0x1F;
	else if (c >= 0xE0 && c <= 0xEF) len = 3, r = c & 0x0F;
	else if (c >= 0xF0 && c <= 0xF4) len = 4, r = c & 0x07;
	else return 0;
	if ((size_t)len > n) return 0;
	for (int i = 1; i < len; i++) {
		if ((b[i] & 0xC0) != 0x80) return 0;
		r = r << 6 | (b[i] & 0x3F);
	}
	if ((len == 3 && r < 0x800) || (len == 4 && (r < 0x10000 || r > 0x10FFFF)) || (r >= 0xD800 && r <= 0xDFFF)) return 0;
	return len;
}

static bool utf8_valid(const uint8_t *b, size_t n) {
	for (size_t i = 0; i < n;) {
		int k = utf8_len(b + i, n - i);
		if (!k) return false;
		i += (size_t)k;
	}
	return true;
}

// jis is the character at JIS X 0208's row and cell (from 0), or 0.
static uint32_t jis(int row, int cell) {
	if (row < 0 || row >= 94 || cell < 0 || cell >= 94) return 0;
	return asuka_barcode_jis[row * 94 + cell];
}

// sjisRune is the character of Shift JIS's two bytes, or 0.
static uint32_t sjisRune(uint8_t s1, uint8_t s2) {
	int row, cell;
	if (s1 >= 0x81 && s1 <= 0x9F) row = (s1 - 0x81) * 2;
	else if (s1 >= 0xE0 && s1 <= 0xEF) row = (s1 - 0xC1) * 2;
	else return 0;
	if (s2 >= 0x40 && s2 <= 0x7E) cell = s2 - 0x40;
	else if (s2 >= 0x80 && s2 <= 0x9E) cell = s2 - 0x41;
	else if (s2 >= 0x9F && s2 <= 0xFC) row++, cell = s2 - 0x9F;
	else return 0;
	return jis(row, cell);
}

// shiftJIS is b, Shift JIS, as text.
static void shiftJIS(bytes *out, const uint8_t *b, size_t n) {
	for (size_t i = 0; i < n; i++) {
		uint8_t c = b[i];
		if (c < 0x80) {
			bput(out, c);
		} else if (c >= 0xA1 && c <= 0xDF) {
			bput_rune(out, 0xFF61 + (uint32_t)(c - 0xA1));  // half-width katakana
		} else if (i + 1 < n) {
			uint32_t r = sjisRune(c, b[i + 1]);
			bput_rune(out, r ? r : 0xFFFD);
			i++;
		} else {
			bput_rune(out, 0xFFFD);
		}
	}
}

// bytesText writes b as text in the charset ECI eci names; with none,
// UTF-8 if b is valid UTF-8, else ISO-8859-1.
static void bytesText(bytes *out, const uint8_t *b, size_t n, int eci) {
	switch (eci) {
	case 1:
	case eciLatin1:
		latin1(out, b, n);
		return;
	case eciShiftJIS:
		shiftJIS(out, b, n);
		return;
	case eciUTF8:
	case 27:
	case 170:
		// Valid UTF-8, each invalid run one U+FFFD.
		for (size_t i = 0; i < n;) {
			int k = utf8_len(b + i, n - i);
			if (k) {
				bputs(out, (const char *)b + i, (size_t)k);
				i += (size_t)k;
				continue;
			}
			bput_rune(out, 0xFFFD);
			while (i < n && !utf8_len(b + i, n - i)) i++;
		}
		return;
	}
	if (utf8_valid(b, n)) bputs(out, (const char *)b, n);
	else latin1(out, b, n);
}

// ---- 1D codes: matching ----

// matchVariance is how far counters are from pattern, scaled to the same
// width: the mean error per unit, or +Inf if one element is off by more
// than maxIndividual units.
static float matchVariance(const float *counters, const uint8_t *pattern, int n, float maxIndividual) {
	float total = 0;
	int plen = 0;
	for (int i = 0; i < n; i++) {
		total += counters[i];
		plen += pattern[i];
	}
	if (total < (float)plen * 0.8f) return INFINITY;
	float unit = total / (float)plen;
	maxIndividual *= unit;
	float v = 0;
	for (int i = 0; i < n; i++) {
		float d = counters[i] - (float)pattern[i] * unit;
		if (d < 0) d = -d;
		if (d > maxIndividual) return INFINITY;
		v += d;
	}
	return v / total;
}

static float sum32(const float *r, int n) {
	float t = 0;
	for (int i = 0; i < n; i++) t += r[i];
	return t;
}

// ---- EAN and UPC ----
//
// Digits of seven modules in two bars and two spaces, between guards;
// left-hand digits in odd (L) or even (G) parity, whose pattern says
// EAN-13's first digit (UPC-E's number system and check digit).

#define upcMaxAvgVariance 0.48f
#define upcMaxIndividualVariance 0.7f

// upcL is each digit's L widths, space first; G is them reversed, and R
// is L's widths, bar first.
static const uint8_t upcL[10][4] = {
    {3, 2, 1, 1}, {2, 2, 2, 1}, {2, 1, 2, 2}, {1, 4, 1, 1}, {1, 1, 3, 2}, {1, 2, 3, 1}, {1, 1, 1, 4}, {1, 3, 1, 2}, {1, 2, 1, 3}, {3, 1, 1, 2},
};

// eanFirstDigit is, for EAN-13's first digit, which of the six left
// digits are G (bit 5 the first).
static const uint8_t eanFirstDigit[10] = {0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A};

// upceParity is UPC-E's parity patterns by number system and check
// digit.
static const uint8_t upceParity[2][10] = {
    {0x38, 0x34, 0x32, 0x31, 0x2C, 0x26, 0x23, 0x2A, 0x29, 0x25},
    {0x07, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A},
};

static const uint8_t guard3[3] = {1, 1, 1}, guard5[5] = {1, 1, 1, 1, 1}, guard6[6] = {1, 1, 1, 1, 1, 1};

// upcDigit matches four runs to a digit: 0–9 for L (or R), 10–19 for G.
static int upcDigit(const float *r, bool g) {
	int best = -1;
	float bestV = upcMaxAvgVariance;
	for (int d = 0; d < 10; d++) {
		float v = matchVariance(r, upcL[d], 4, upcMaxIndividualVariance);
		if (v < bestV) best = d, bestV = v;
		if (g) {
			uint8_t rev[4] = {upcL[d][3], upcL[d][2], upcL[d][1], upcL[d][0]};
			v = matchVariance(r, rev, 4, upcMaxIndividualVariance);
			if (v < bestV) best = d + 10, bestV = v;
		}
	}
	return best;
}

static bool guardOK(const float *r, const uint8_t *p, int n) { return matchVariance(r, p, n, upcMaxIndividualVariance) < upcMaxAvgVariance; }

// eanCheck reports whether digits' last is their check digit.
static bool eanCheck(const char *digits, int n) {
	int sum = 0;
	for (int i = n - 2; i >= 0; i--) {
		int d = digits[i] - '0';
		if ((n - 2 - i) % 2 == 0) d *= 3;
		sum += d;
	}
	return (10 - sum % 10) % 10 == digits[n - 1] - '0';
}

// quietModules is the least quiet zone, in modules, each side: more
// than any space inside a code, so the middle of one isn't the end of
// another.
#define quietModules 5

// quietAfter reports whether runs[k] is light and wide enough to end a
// code of module m.
static bool quietAfter(const float *runs, int n, int k, float m) { return k < n && runs[k] >= quietModules * m; }

// evenModules reports whether a code's guards and digits (left digits,
// then right ones after a middle guard, if any) are all about as wide
// as their modules say: one module size throughout, give or take what
// perspective does.
static bool evenGroup(const float *runs, int *k, int n, int modules, float u) {
	float w = sum32(runs + *k, n) / (float)modules;
	*k += n;
	return w > 0.7f * u && w < 1.4f * u;
}

static bool evenModules(const float *runs, int nruns, int left, int right) {
	int total = 3 + 7 * left + 7 * right + 3;
	if (right > 0) total += 5;
	else total += 3;  // UPC-E's end guard is six modules
	float u = sum32(runs, nruns) / (float)total;
	int k = 0;
	if (!evenGroup(runs, &k, 3, 3, u)) return false;
	for (int d = 0; d < left; d++)
		if (!evenGroup(runs, &k, 4, 7, u)) return false;
	if (right == 0) return evenGroup(runs, &k, 6, 6, u);
	if (!evenGroup(runs, &k, 5, 5, u)) return false;
	for (int d = 0; d < right; d++)
		if (!evenGroup(runs, &k, 4, 7, u)) return false;
	return evenGroup(runs, &k, 3, 3, u);
}

static bool decodeEAN13(const float *runs, int nruns, int k, float m, char out[14], int *end) {
	if (k + 56 > nruns) return false;
	char digits[13];
	int parity = 0;
	for (int d = 0; d < 6; d++) {
		int v = upcDigit(runs + k, true);
		if (v < 0) return false;
		if (v >= 10) {
			parity |= 1 << (5 - d);
			v -= 10;
		}
		digits[1 + d] = (char)('0' + v);
		k += 4;
	}
	int first = -1;
	for (int d = 0; d < 10; d++)
		if (eanFirstDigit[d] == parity) first = d;
	if (first < 0 || !guardOK(runs + k, guard5, 5)) return false;
	digits[0] = (char)('0' + first);
	k += 5;
	for (int d = 0; d < 6; d++) {
		int v = upcDigit(runs + k, false);
		if (v < 0) return false;
		digits[7 + d] = (char)('0' + v);
		k += 4;
	}
	if (!guardOK(runs + k, guard3, 3) || !quietAfter(runs, nruns, k + 3, m) || !eanCheck(digits, 13)) return false;
	memcpy(out, digits, 13);
	out[13] = 0;
	*end = k + 3;
	return true;
}

static bool decodeEAN8(const float *runs, int nruns, int k, float m, char out[14], int *end) {
	if (k + 40 > nruns) return false;
	char digits[8];
	for (int d = 0; d < 8; d++) {
		if (d == 4) {
			if (!guardOK(runs + k, guard5, 5)) return false;
			k += 5;
		}
		int v = upcDigit(runs + k, false);
		if (v < 0) return false;
		digits[d] = (char)('0' + v);
		k += 4;
	}
	if (!guardOK(runs + k, guard3, 3) || !quietAfter(runs, nruns, k + 3, m) || !eanCheck(digits, 8)) return false;
	memcpy(out, digits, 8);
	out[8] = 0;
	*end = k + 3;
	return true;
}

// upceToUPCA expands a UPC-E's eight digits to the UPC-A's twelve.
static void upceToUPCA(const char *e, char out[13]) {
	const char *d = e + 1;
	char mid[11];
	char last = d[5];
	if (last == '0' || last == '1' || last == '2') {
		memcpy(mid, d, 2);
		mid[2] = last;
		memcpy(mid + 3, "0000", 4);
		memcpy(mid + 7, d + 2, 3);
	} else if (last == '3') {
		memcpy(mid, d, 3);
		memcpy(mid + 3, "00000", 5);
		memcpy(mid + 8, d + 3, 2);
	} else if (last == '4') {
		memcpy(mid, d, 4);
		memcpy(mid + 4, "00000", 5);
		mid[9] = d[4];
	} else {
		memcpy(mid, d, 5);
		memcpy(mid + 5, "0000", 4);
		mid[9] = last;
	}
	out[0] = e[0];
	memcpy(out + 1, mid, 10);
	out[11] = e[7];
	out[12] = 0;
}

static bool decodeUPCE(const float *runs, int nruns, int k, float m, char out[14], int *end) {
	if (k + 30 > nruns) return false;
	char digits[9];
	int parity = 0;
	for (int d = 0; d < 6; d++) {
		int v = upcDigit(runs + k, true);
		if (v < 0) return false;
		if (v >= 10) {
			parity |= 1 << (5 - d);
			v -= 10;
		}
		digits[1 + d] = (char)('0' + v);
		k += 4;
	}
	if (!guardOK(runs + k, guard6, 6) || !quietAfter(runs, nruns, k + 6, m)) return false;
	bool ok = false;
	for (int sys = 0; sys < 2 && !ok; sys++)
		for (int c = 0; c < 10; c++)
			if (upceParity[sys][c] == parity) {
				digits[0] = (char)('0' + sys), digits[7] = (char)('0' + c);
				ok = true;
				break;
			}
	digits[8] = 0;
	char a[13];
	if (ok) upceToUPCA(digits, a);
	if (!ok || !eanCheck(a, 12)) return false;
	memcpy(out, digits, 9);
	*end = k + 6;
	return true;
}

// decodeUPCEAN reads an EAN-13 (UPC-A among them, as EAN-13), EAN-8 or
// UPC-E whose start guard is at runs[i]: its kind, text and the index of
// the quiet zone after it.
static unsigned decodeUPCEAN(const float *runs, int nruns, int i, unsigned want, char text[14], int *end) {
	if (i + 3 > nruns || !guardOK(runs + i, guard3, 3)) return 0;
	float m = sum32(runs + i, 3) / 3;
	if (runs[i - 1] < quietModules * m) return 0;
	int j;
	if (want & (ASUKA_BARCODE_EAN13 | ASUKA_BARCODE_UPCA))
		if (decodeEAN13(runs, nruns, i + 3, m, text, &j) && evenModules(runs + i, j - i, 6, 6)) return *end = j, ASUKA_BARCODE_EAN13;
	if (want & ASUKA_BARCODE_EAN8)
		if (decodeEAN8(runs, nruns, i + 3, m, text, &j) && evenModules(runs + i, j - i, 4, 4)) return *end = j, ASUKA_BARCODE_EAN8;
	if (want & ASUKA_BARCODE_UPCE)
		if (decodeUPCE(runs, nruns, i + 3, m, text, &j) && evenModules(runs + i, j - i, 6, 0)) return *end = j, ASUKA_BARCODE_UPCE;
	return 0;
}

// ---- Code 128 ----
//
// Symbols of three bars and three spaces, eleven modules; the stop
// symbol has a last bar of two more. Code sets A, B and C, a weighted
// check symbol modulo 103.

#define c128MaxAvgVariance 0.25f
#define c128MaxIndividualVariance 0.7f
enum { c128FNC1 = 102, c128StartA = 103, c128StartB = 104, c128StartC = 105, c128Stop = 106 };

static const uint8_t c128Patterns[107][6] = {
    {2, 1, 2, 2, 2, 2}, {2, 2, 2, 1, 2, 2}, {2, 2, 2, 2, 2, 1}, {1, 2, 1, 2, 2, 3}, {1, 2, 1, 3, 2, 2}, {1, 3, 1, 2, 2, 2}, {1, 2, 2, 2, 1, 3},
    {1, 2, 2, 3, 1, 2}, {1, 3, 2, 2, 1, 2}, {2, 2, 1, 2, 1, 3}, {2, 2, 1, 3, 1, 2}, {2, 3, 1, 2, 1, 2}, {1, 1, 2, 2, 3, 2}, {1, 2, 2, 1, 3, 2},
    {1, 2, 2, 2, 3, 1}, {1, 1, 3, 2, 2, 2}, {1, 2, 3, 1, 2, 2}, {1, 2, 3, 2, 2, 1}, {2, 2, 3, 2, 1, 1}, {2, 2, 1, 1, 3, 2}, {2, 2, 1, 2, 3, 1},
    {2, 1, 3, 2, 1, 2}, {2, 2, 3, 1, 1, 2}, {3, 1, 2, 1, 3, 1}, {3, 1, 1, 2, 2, 2}, {3, 2, 1, 1, 2, 2}, {3, 2, 1, 2, 2, 1}, {3, 1, 2, 2, 1, 2},
    {3, 2, 2, 1, 1, 2}, {3, 2, 2, 2, 1, 1}, {2, 1, 2, 1, 2, 3}, {2, 1, 2, 3, 2, 1}, {2, 3, 2, 1, 2, 1}, {1, 1, 1, 3, 2, 3}, {1, 3, 1, 1, 2, 3},
    {1, 3, 1, 3, 2, 1}, {1, 1, 2, 3, 1, 3}, {1, 3, 2, 1, 1, 3}, {1, 3, 2, 3, 1, 1}, {2, 1, 1, 3, 1, 3}, {2, 3, 1, 1, 1, 3}, {2, 3, 1, 3, 1, 1},
    {1, 1, 2, 1, 3, 3}, {1, 1, 2, 3, 3, 1}, {1, 3, 2, 1, 3, 1}, {1, 1, 3, 1, 2, 3}, {1, 1, 3, 3, 2, 1}, {1, 3, 3, 1, 2, 1}, {3, 1, 3, 1, 2, 1},
    {2, 1, 1, 3, 3, 1}, {2, 3, 1, 1, 3, 1}, {2, 1, 3, 1, 1, 3}, {2, 1, 3, 3, 1, 1}, {2, 1, 3, 1, 3, 1}, {3, 1, 1, 1, 2, 3}, {3, 1, 1, 3, 2, 1},
    {3, 3, 1, 1, 2, 1}, {3, 1, 2, 1, 1, 3}, {3, 1, 2, 3, 1, 1}, {3, 3, 2, 1, 1, 1}, {3, 1, 4, 1, 1, 1}, {2, 2, 1, 4, 1, 1}, {4, 3, 1, 1, 1, 1},
    {1, 1, 1, 2, 2, 4}, {1, 1, 1, 4, 2, 2}, {1, 2, 1, 1, 2, 4}, {1, 2, 1, 4, 2, 1}, {1, 4, 1, 1, 2, 2}, {1, 4, 1, 2, 2, 1}, {1, 1, 2, 2, 1, 4},
    {1, 1, 2, 4, 1, 2}, {1, 2, 2, 1, 1, 4}, {1, 2, 2, 4, 1, 1}, {1, 4, 2, 1, 1, 2}, {1, 4, 2, 2, 1, 1}, {2, 4, 1, 2, 1, 1}, {2, 2, 1, 1, 1, 4},
    {4, 1, 3, 1, 1, 1}, {2, 4, 1, 1, 1, 2}, {1, 3, 4, 1, 1, 1}, {1, 1, 1, 2, 4, 2}, {1, 2, 1, 1, 4, 2}, {1, 2, 1, 2, 4, 1}, {1, 1, 4, 2, 1, 2},
    {1, 2, 4, 1, 1, 2}, {1, 2, 4, 2, 1, 1}, {4, 1, 1, 2, 1, 2}, {4, 2, 1, 1, 1, 2}, {4, 2, 1, 2, 1, 1}, {2, 1, 2, 1, 4, 1}, {2, 1, 4, 1, 2, 1},
    {4, 1, 2, 1, 2, 1}, {1, 1, 1, 1, 4, 3}, {1, 1, 1, 3, 4, 1}, {1, 3, 1, 1, 4, 1}, {1, 1, 4, 1, 1, 3}, {1, 1, 4, 3, 1, 1}, {4, 1, 1, 1, 1, 3},
    {4, 1, 1, 3, 1, 1}, {1, 1, 3, 1, 4, 1}, {1, 1, 4, 1, 3, 1}, {3, 1, 1, 1, 4, 1}, {4, 1, 1, 1, 3, 1}, {2, 1, 1, 4, 1, 2}, {2, 1, 1, 2, 1, 4},
    {2, 1, 1, 2, 3, 2}, {2, 3, 3, 1, 1, 1},
};

// c128Symbol matches six runs to a symbol, or -1.
static int c128Symbol(const float *r) {
	int best = -1;
	float bestV = c128MaxAvgVariance;
	for (int c = 0; c < 107; c++) {
		float v = matchVariance(r, c128Patterns[c], 6, c128MaxIndividualVariance);
		if (v < bestV) best = c, bestV = v;
	}
	return best;
}

// fnc4 is FNC4: once, the next character is extended (+128); twice,
// every one is until it's done twice again.
static void fnc4(bool *upper, bool *shiftUpper) {
	if (!*upper && *shiftUpper) *upper = true, *shiftUpper = false;
	else if (*upper && *shiftUpper) *upper = false, *shiftUpper = false;
	else *shiftUpper = true;
}

// c128Text interprets the symbols after the start one.
static bool c128Text(int start, const int *cs, int n, bytes *out) {
	enum { setA, setB, setC };
	int set = start - c128StartA;
	bool upper = false, shiftUpper = false;
#define PUT(c)                                                     \
	do {                                                       \
		int c_ = (c);                                      \
		if (upper != shiftUpper) c_ += 128;                \
		shiftUpper = false;                                \
		bput_rune(out, (uint32_t)c_);                      \
	} while (0)
	for (int k = 0; k < n; k++) {
		int c = cs[k];
		int cur = set;
		for (;;) {
			if (cur == setA || cur == setB) {
				if (cur == setA && c < 64) {
					PUT(' ' + c);
				} else if (cur == setA && c < 96) {
					PUT(c - 64);
				} else if (cur == setB && c < 96) {
					PUT(' ' + c);
				} else if (c == c128FNC1) {
					if (k > 0) bput(out, 0x1D);
				} else if (c == 96 || c == 97) {  // FNC3, FNC2
				} else if (c == 98) {  // shift: the next symbol in the other set
					if (k + 1 >= n) return false;
					c = cs[++k];
					cur = cur == setA ? setB : setA;
					continue;
				} else if (c == 99) {
					set = setC;
				} else if (c == 100) {
					if (cur == setA) set = setB;
					else fnc4(&upper, &shiftUpper);
				} else if (c == 101) {
					if (cur == setB) set = setA;
					else fnc4(&upper, &shiftUpper);
				}
			} else {
				if (c < 100) {
					bput(out, (uint8_t)('0' + c / 10));
					bput(out, (uint8_t)('0' + c % 10));
				} else if (c == c128FNC1) {
					if (k > 0) bput(out, 0x1D);
				} else if (c == 100) {
					set = setB;
				} else if (c == 101) {
					set = setA;
				}
			}
			break;
		}
	}
#undef PUT
	return true;
}

// decodeCode128 reads a Code 128 whose start symbol is at runs[i]: its
// text and the index of the quiet zone after it.
static char *decodeCode128(const float *runs, int nruns, int i, int *end) {
	if (i + 6 > nruns) return NULL;
	int start = -1;
	float bestV = c128MaxAvgVariance;
	for (int c = c128StartA; c <= c128StartC; c++) {
		float v = matchVariance(runs + i, c128Patterns[c], 6, c128MaxIndividualVariance);
		if (v < bestV) start = c, bestV = v;
	}
	if (start < 0) return NULL;
	float width = sum32(runs + i, 6);
	if (runs[i - 1] < width / 2) return NULL;
	VEC(int) cs = {0};
	int k = i + 6;
	for (;;) {
		if (k + 6 > nruns) goto fail;
		int c = c128Symbol(runs + k);
		if (c < 0 || (c >= c128StartA && c <= c128StartC)) goto fail;
		float w = sum32(runs + k, 6);
		// Symbols keep their width.
		if (w < 0.7f * width || w > 1.4f * width) goto fail;
		if (c == c128Stop) {
			float m = w / 11;
			if (k + 7 >= nruns) goto fail;
			float last = runs[k + 6];
			if (last < 1.3f * m || last > 2.7f * m || runs[k + 7] < w / 2) goto fail;
			k += 7;
			break;
		}
		VPUSH(cs, c);
		k += 6;
	}
	if (cs.n < 2) goto fail;
	int sum = start;
	for (size_t n = 0; n + 1 < cs.n; n++) sum += (int)(n + 1) * cs.v[n];
	if (sum % 103 != cs.v[cs.n - 1]) goto fail;
	bytes text = {0};
	if (!c128Text(start, cs.v, (int)cs.n - 1, &text) || !text.n) {
		VFREE(text);
		goto fail;
	}
	VFREE(cs);
	*end = k;
	char *s = bstr(&text);
	VFREE(text);
	return s;
fail:
	VFREE(cs);
	return NULL;
}

// ---- Code 39 ----
//
// Characters of five bars and four spaces, three of them wide, apart by
// a narrow space, between * start and stop characters. No check
// character is read.

static const char code39Alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";

// code39Patterns is each character's wide elements, bit 8 the first.
static const uint16_t code39Patterns[43] = {
    0x034, 0x121, 0x061, 0x160, 0x031, 0x130, 0x070, 0x025, 0x124, 0x064, 0x109, 0x049, 0x148, 0x019, 0x118,
    0x058, 0x00D, 0x10C, 0x04C, 0x01C, 0x103, 0x043, 0x142, 0x013, 0x112, 0x052, 0x007, 0x106, 0x046, 0x016,
    0x181, 0x0C1, 0x1C0, 0x091, 0x190, 0x0D0, 0x085, 0x184, 0x0C4, 0x0A8, 0x0A2, 0x08A, 0x02A,
};

enum { code39Asterisk = 0x094 };

// code39Wide sorts nine runs into narrow and wide: the pattern of the
// three wide ones, or -1 if they don't split cleanly.
static int code39Wide(const float *r) {
	// The three widest must be clearly wider than the six others.
	int idx[9];
	for (int i = 0; i < 9; i++) idx[i] = i;
	for (int a = 0; a < 9; a++)
		for (int b = a + 1; b < 9; b++)
			if (r[idx[b]] > r[idx[a]]) {
				int t = idx[a];
				idx[a] = idx[b];
				idx[b] = t;
			}
	float narrowMax = r[idx[3]], wideMin = r[idx[2]], nsum = 0, wsum = 0;
	for (int k = 3; k < 9; k++) nsum += r[idx[k]];
	for (int k = 0; k < 3; k++) wsum += r[idx[k]];
	float n = nsum / 6, w = wsum / 3;
	if (w < 1.45f * n || w > 4 * n || wideMin < 1.15f * narrowMax || narrowMax > 1.6f * n || r[idx[0]] > 1.6f * w) return -1;
	int p = 0;
	for (int k = 0; k < 3; k++) p |= 1 << (8 - idx[k]);
	return p;
}

static char code39Char(int p) {
	for (int i = 0; i < 43; i++)
		if (code39Patterns[i] == p) return code39Alphabet[i];
	return 0;
}

// decodeCode39 reads a Code 39 whose start character is at runs[i]: its
// text and the index of the quiet zone after it.
static char *decodeCode39(const float *runs, int nruns, int i, int *end) {
	// The start character: narrow, wide, narrow, narrow, wide (bars and
	// spaces from the first).
	if (i + 10 > nruns || runs[i + 1] < 1.3f * runs[i] || runs[i + 4] < 1.3f * runs[i + 3] || code39Wide(runs + i) != code39Asterisk) return NULL;
	float width = sum32(runs + i, 9);
	if (runs[i - 1] < width / 2) return NULL;
	float narrow = width / 15;
	bytes text = {0};
	int k = i + 9;
	for (;;) {
		// The gap between characters is narrow.
		if (k + 10 > nruns || runs[k] > 3 * narrow) break;
		k++;
		float w = sum32(runs + k, 9);
		if (w < 0.75f * width || w > 1.33f * width) break;
		int p = code39Wide(runs + k);
		if (p < 0) break;
		if (p == code39Asterisk) {
			if (k + 9 >= nruns || runs[k + 9] < w / 2 || !text.n) break;
			*end = k + 9;
			char *s = bstr(&text);
			VFREE(text);
			return s;
		}
		char c = code39Char(p);
		if (!c) break;
		bput(&text, (uint8_t)c);
		width = (width + w) / 2;
		k += 9;
	}
	VFREE(text);
	return NULL;
}

// ---- 1D codes: scanning ----
//
// Barcodes are read along scan lines across the image in several
// directions, each line both ways: its gray levels become the widths of
// its dark and light runs, between edges where the level swings, and
// readers match those against each symbology's patterns, as ZXing's
// OneDReader does. A code counts once two lines agree on it.

enum { scanDirections = 12, minAgree = 2 };
#define scanSpacing 6.0
#define minSwing 14.0f
#define swingShare 0.18f

// read1D is a code read on one line: its kind, text and ends.
typedef struct {
	unsigned kind;
	char *text;
	point a, b;
} read1D;

typedef struct {
	int pos;
	float v;
} extreme;

// scanner holds a line's buffers, reused line after line.
typedef struct {
	const luminance *lum;
	unsigned want;
	VEC(float) sig;
	VEC(float) thr;
	VEC(float) blo;
	VEC(extreme) ext;
	VEC(float) runs;
	VEC(float) pos;
	VEC(float) rruns;
	VEC(float) rpos;
	VEC(read1D) reads;
} scanner;

static void reserve(void **p, size_t *cap, size_t n, size_t size) {
	if (*cap < n) {
		*p = realloc(*p, n * size);
		*cap = n;
	}
}
#define RESERVE(a, k) reserve((void **)&(a).v, &(a).cap, (k), sizeof *(a).v)

// edges splits sig into runs at the edges between its swings, and
// reports whether the first run is dark. A swing counts when it's a
// good part of the contrast around it, so noise in flat parts and uneven
// light don't make edges.
static bool edges(scanner *s, const float *sig, int n) {
	enum { blk = 16 };
	int nb = (n + blk - 1) / blk;
	RESERVE(s->thr, (size_t)nb);
	float *thr = s->thr.v;
	// Each block's range, then the range of the five blocks around it.
	RESERVE(s->blo, (size_t)2 * nb);
	float *blo = s->blo.v, *bhi = s->blo.v + nb;
	for (int b = 0; b < nb; b++) {
		int lo = b * blk, hi = MIN((b + 1) * blk, n);
		float mn = sig[lo], mx = sig[lo];
		for (int k = lo; k < hi; k++) mn = fminf(mn, sig[k]), mx = fmaxf(mx, sig[k]);
		blo[b] = mn, bhi[b] = mx;
	}
	for (int b = 0; b < nb; b++) {
		float mn = blo[b], mx = bhi[b];
		for (int j = MAX(0, b - 2); j <= MIN(nb - 1, b + 2); j++) mn = fminf(mn, blo[j]), mx = fmaxf(mx, bhi[j]);
		thr[b] = fmaxf(minSwing, swingShare * (mx - mn));
	}
	// Alternate extremes, each swing at least the threshold.
	s->ext.n = 0;
	float hiV = sig[0], loV = sig[0];
	int hiP = 0, loP = 0, dir = 0;
	for (int k = 1; k < n; k++) {
		float v = sig[k], t = thr[k / blk];
		if (dir == 0) {
			if (v > hiV) hiV = v, hiP = k;
			if (v < loV) loV = v, loP = k;
			if (hiV - loV >= t) {
				if (hiP > loP) {
					VPUSH(s->ext, ((extreme){loP, loV}));
					dir = 1;
				} else {
					VPUSH(s->ext, ((extreme){hiP, hiV}));
					dir = -1;
					loV = v, loP = k;
				}
			}
		} else if (dir == 1) {
			if (v > hiV) {
				hiV = v, hiP = k;
			} else if (hiV - v >= t) {
				VPUSH(s->ext, ((extreme){hiP, hiV}));
				dir = -1;
				loV = v, loP = k;
			}
		} else {
			if (v < loV) {
				loV = v, loP = k;
			} else if (v - loV >= t) {
				VPUSH(s->ext, ((extreme){loP, loV}));
				dir = 1;
				hiV = v, hiP = k;
			}
		}
	}
	// The last extreme, if the level got there.
	if (s->ext.n > 0) {
		extreme last = s->ext.v[s->ext.n - 1];
		float t = thr[(n - 1) / blk];
		if (dir == 1 && hiV - last.v >= t) VPUSH(s->ext, ((extreme){hiP, hiV}));
		else if (dir == -1 && last.v - loV >= t) VPUSH(s->ext, ((extreme){loP, loV}));
	}
	s->runs.n = 0;
	s->pos.n = 0;
	if (s->ext.n < 2) return false;
	// An edge where the level crosses the middle of two extremes.
	float prev = 0;
	VPUSH(s->pos, 0.0f);
	for (size_t i = 0; i + 1 < s->ext.n; i++) {
		extreme a = s->ext.v[i], b = s->ext.v[i + 1];
		float mid = (a.v + b.v) / 2, e = (float)a.pos;
		for (int k = a.pos; k < b.pos; k++) {
			float v0 = sig[k] - mid, v1 = sig[k + 1] - mid;
			if ((v0 <= 0) != (v1 <= 0)) {
				e = (float)k + v0 / (v0 - v1);
				break;
			}
		}
		VPUSH(s->runs, e - prev);
		VPUSH(s->pos, e);
		prev = e;
	}
	VPUSH(s->runs, (float)(n - 1) - prev);
	// The first run is dark if its extreme is a trough.
	return s->ext.v[0].v < s->ext.v[1].v;
}

static void addRead(scanner *s, unsigned kind, char *text, double x0, double y0, double dx, double dy, float ta, float tb) {
	read1D r = {kind, text, {x0 + (double)ta * dx, y0 + (double)ta * dy}, {x0 + (double)tb * dx, y0 + (double)tb * dy}};
	VPUSH(s->reads, r);
}

// decodeRuns tries every reader wanted at every dark run after a light
// one; a code found is read from the run indexes of its first bar and of
// the quiet zone after it.
static void decodeRuns(scanner *s, const float *runs, const float *pos, int nruns, bool firstDark, double x0, double y0, double dx, double dy) {
	int start = firstDark ? 2 : 1;
	for (int i = start; i + 8 < nruns; i += 2) {
		if (runs[i - 1] < 2 * runs[i]) continue;
		int j;
		if (s->want & (ASUKA_BARCODE_EAN13 | ASUKA_BARCODE_UPCA | ASUKA_BARCODE_EAN8 | ASUKA_BARCODE_UPCE)) {
			char text[14];
			unsigned kind = decodeUPCEAN(runs, nruns, i, s->want, text, &j);
			if (kind) {
				size_t len = strlen(text);
				char *t = malloc(len + 1);
				memcpy(t, text, len + 1);
				addRead(s, kind, t, x0, y0, dx, dy, pos[i], pos[j]);
				i = j - 1;
				continue;
			}
		}
		if (s->want & ASUKA_BARCODE_CODE128) {
			char *t = decodeCode128(runs, nruns, i, &j);
			if (t) {
				addRead(s, ASUKA_BARCODE_CODE128, t, x0, y0, dx, dy, pos[i], pos[j]);
				i = j - 1;
				continue;
			}
		}
		if (s->want & ASUKA_BARCODE_CODE39) {
			char *t = decodeCode39(runs, nruns, i, &j);
			if (t) {
				addRead(s, ASUKA_BARCODE_CODE39, t, x0, y0, dx, dy, pos[i], pos[j]);
				i = j - 1;
				continue;
			}
		}
	}
}

// scanLine reads the line through (px, py) in direction (dx, dy).
static void scanLine(scanner *s, double px, double py, double dx, double dy) {
	const luminance *l = s->lum;
	double w = l->w - 1, h = l->h - 1;
	double t0 = -INFINITY, t1 = INFINITY;
	double ps[2] = {px, py}, ds[2] = {dx, dy}, maxs[2] = {w, h};
	for (int k = 0; k < 2; k++) {
		if (ds[k] == 0) {
			if (!(ps[k] >= 0 && ps[k] <= maxs[k])) return;
			continue;
		}
		double a = -ps[k] / ds[k], b = (maxs[k] - ps[k]) / ds[k];
		if (a > b) {
			double t = a;
			a = b;
			b = t;
		}
		t0 = fmax(t0, a), t1 = fmin(t1, b);
	}
	t0 = ceil(t0 - 1e-9);
	if (!(t1 - t0 < 1e9)) return;
	int n = itrunc(t1 - t0) + 1;
	if (n < 40) return;
	RESERVE(s->sig, (size_t)n);
	float *sig = s->sig.v;
	double x0 = px + t0 * dx, y0 = py + t0 * dy;
	if (dy == 0) {
		const uint8_t *row = l->pix + (size_t)itrunc(round(y0)) * l->w;
		int xs = itrunc(round(x0));
		for (int i = 0; i < n; i++) sig[i] = row[xs + i];
	} else if (dx == 0) {
		int xs = itrunc(round(x0)), ys = itrunc(round(y0));
		for (int i = 0; i < n; i++) sig[i] = l->pix[(size_t)(ys + i) * l->w + xs];
	} else {
		float fdx = (float)dx, fdy = (float)dy, fx = (float)x0, fy = (float)y0;
		for (int i = 0; i < n; i++) {
			float x = fx + (float)i * fdx, y = fy + (float)i * fdy;
			int ix = (int)x, iy = (int)y;
			if (ix >= l->w - 1) ix = l->w - 2;
			if (iy >= l->h - 1) iy = l->h - 2;
			if (ix < 0) ix = 0;
			if (iy < 0) iy = 0;
			float ax = x - (float)ix, ay = y - (float)iy;
			const uint8_t *p = l->pix + (size_t)iy * l->w + ix;
			float top = (float)p[0] + ax * ((float)p[1] - (float)p[0]);
			float bot = (float)p[l->w] + ax * ((float)p[l->w + 1] - (float)p[l->w]);
			sig[i] = top + ay * (bot - top);
		}
	}
	bool firstDark = edges(s, sig, n);
	if (s->runs.n < 20) return;
	int m = (int)s->runs.n;
	decodeRuns(s, s->runs.v, s->pos.v, m, firstDark, x0, y0, dx, dy);
	// And the other way.
	s->rruns.n = 0;
	s->rpos.n = 0;
	for (int k = m - 1; k >= 0; k--) {
		VPUSH(s->rruns, s->runs.v[k]);
		VPUSH(s->rpos, s->pos.v[k] + s->runs.v[k]);
	}
	bool lastDark = firstDark == (m % 2 == 1);
	decodeRuns(s, s->rruns.v, s->rpos.v, m, lastDark, x0, y0, dx, dy);
}

typedef struct {
	unsigned kind;
	const char *text;
	double cx, cy, length;
	int n;
	double x0, y0, x1, y1;
} group;

// agree groups the reads by code and place, and is the codes read on
// enough lines.
static void agree(scanner *s, codes *out) {
	VEC(group) gs = {0};
	for (size_t ri = 0; ri < s->reads.n; ri++) {
		read1D *r = &s->reads.v[ri];
		double mx = (r->a.x + r->b.x) / 2, my = (r->a.y + r->b.y) / 2, l = dist(r->a, r->b);
		size_t k = (size_t)-1;
		for (size_t i = 0; i < gs.n; i++) {
			group *g = &gs.v[i];
			if (g->kind != r->kind || strcmp(g->text, r->text)) continue;
			if (hypot(g->cx - mx, g->cy - my) < 0.6 * fmax(l, g->length)) {
				k = i;
				break;
			}
		}
		if (k == (size_t)-1) {
			group g = {r->kind, r->text, 0, 0, 0, 0, INFINITY, INFINITY, -INFINITY, -INFINITY};
			VPUSH(gs, g);
			k = gs.n - 1;
		}
		group *g = &gs.v[k];
		double f = g->n;
		g->cx = (g->cx * f + mx) / (f + 1);
		g->cy = (g->cy * f + my) / (f + 1);
		g->length = (g->length * f + l) / (f + 1);
		g->n++;
		point ends[2] = {r->a, r->b};
		for (int e = 0; e < 2; e++) {
			g->x0 = fmin(g->x0, ends[e].x), g->y0 = fmin(g->y0, ends[e].y);
			g->x1 = fmax(g->x1, ends[e].x), g->y1 = fmax(g->y1, ends[e].y);
		}
	}
	// One place, one code: where reads of different codes overlap, the
	// code most lines read wins. (Stable: an insertion sort.)
	for (size_t i = 1; i < gs.n; i++) {
		group g = gs.v[i];
		size_t j = i;
		while (j > 0 && gs.v[j - 1].n < g.n) {
			gs.v[j] = gs.v[j - 1];
			j--;
		}
		gs.v[j] = g;
	}
	VEC(size_t) kept = {0};
	for (size_t gi = 0; gi < gs.n; gi++) {
		group *g = &gs.v[gi];
		if (g->n < minAgree) continue;
		bool clash = false;
		for (size_t k = 0; k < kept.n && !clash; k++) {
			group *o = &gs.v[kept.v[k]];
			clash = g->cx > o->x0 && g->cx < o->x1 && g->cy > o->y0 && g->cy < o->y1;
		}
		if (clash) continue;
		VPUSH(kept, gi);
		unsigned kind = g->kind;
		const char *text = g->text;
		if (kind == ASUKA_BARCODE_EAN13) {
			// A UPC-A is an EAN-13 that starts with 0.
			if (text[0] == '0' && (s->want & ASUKA_BARCODE_UPCA)) kind = ASUKA_BARCODE_UPCA, text++;
			else if (!(s->want & ASUKA_BARCODE_EAN13)) continue;
		}
		size_t len = strlen(text);
		char *t = malloc(len + 1);
		memcpy(t, text, len + 1);
		code c = {kind, t, ifloor(g->x0), ifloor(g->y0), itrunc(ceil(g->x1)) + 1, itrunc(ceil(g->y1)) + 1};
		VPUSH(*out, c);
	}
	VFREE(kept);
	VFREE(gs);
}

static void decode1D(const luminance *l, unsigned want, codes *out) {
	scanner s = {0};
	s.lum = l;
	s.want = want;
	double w = l->w, h = l->h;
	for (int k = 0; k < scanDirections; k++) {
		double th = (double)k * 3.14159265358979323846 / scanDirections;
		double dx = cos(th), dy = sin(th);
		if (fabs(dx) < 1e-9) dx = 0;
		if (fabs(dy) < 1e-9) dy = 0;
		double nx = -dy, ny = dx;
		double lo = INFINITY, hi = -INFINITY;
		point cs[4] = {{0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}};
		for (int c = 0; c < 4; c++) {
			double o = cs[c].x * nx + cs[c].y * ny;
			lo = fmin(lo, o), hi = fmax(hi, o);
		}
		for (double o = lo + scanSpacing / 2; o <= hi; o += scanSpacing) scanLine(&s, o * nx, o * ny, dx, dy);
	}
	agree(&s, out);
	for (size_t i = 0; i < s.reads.n; i++) free(s.reads.v[i].text);
	VFREE(s.reads);
	VFREE(s.sig);
	VFREE(s.thr);
	VFREE(s.blo);
	VFREE(s.ext);
	VFREE(s.runs);
	VFREE(s.pos);
	VFREE(s.rruns);
	VFREE(s.rpos);
}

// ---- QR codes: finding ----

// finder is a QR code's finder pattern found: its center, module size,
// and how many scans confirmed it.
typedef struct {
	double x, y, size;
	int count;
} finder;

typedef VEC(finder) finders;

static bool near_(const finder *f, double size, double y, double x) {
	if (fabs(y - f->y) <= size && fabs(x - f->x) <= size) {
		double d = fabs(size - f->size);
		return d <= 1 || d <= f->size;
	}
	return false;
}

static void combine(finder *f, double y, double x, double size) {
	double n = f->count;
	f->x = (n * f->x + x) / (n + 1);
	f->y = (n * f->y + y) / (n + 1);
	f->size = (n * f->size + size) / (n + 1);
	f->count++;
}

static bool foundCross(const int c[5]) {
	int total = 0;
	for (int i = 0; i < 5; i++) {
		if (!c[i]) return false;
		total += c[i];
	}
	if (total < 7) return false;
	double m = total / 7.0, v = m / 2;
	return fabs(m - c[0]) < v && fabs(m - c[1]) < v && fabs(3 * m - c[2]) < 3 * v && fabs(m - c[3]) < v && fabs(m - c[4]) < v;
}

static bool foundDiagonal(const int c[5]) {
	int total = 0;
	for (int i = 0; i < 5; i++) {
		if (!c[i]) return false;
		total += c[i];
	}
	if (total < 7) return false;
	double m = total / 7.0, v = m / 1.333;
	return fabs(m - c[0]) < v && fabs(m - c[1]) < v && fabs(3 * m - c[2]) < 3 * v && fabs(m - c[3]) < v && fabs(m - c[4]) < v;
}

static double centerFromEnd(const int c[5], int end) { return (double)(end - c[4] - c[3]) - c[2] / 2.0; }

// qrFinder finds finder patterns: dark, light, dark, light, dark runs in
// the ratio 1:1:3:1:1 across rows, confirmed down the column, across the
// row again and along a diagonal, as ZXing's FinderPatternFinder does.
typedef struct {
	const bitImage *b;
	finders centers;
} qrFinder;

// crossCheck counts the runs through (x, y) down the column (or along
// the row) and is the pattern's center on that line, or NaN.
static double crossCheck(qrFinder *f, int x, int y, int maxCount, int origTotal, bool vertical) {
	const bitImage *b = f->b;
#define AT(i) (vertical ? at(b, x, (i)) : at(b, (i), y))
	int start = vertical ? y : x, limit = vertical ? b->h : b->w;
	int c[5] = {0};
	int i = start;
	while (i >= 0 && AT(i)) c[2]++, i--;
	if (i < 0) return NAN;
	while (i >= 0 && !AT(i) && c[1] <= maxCount) c[1]++, i--;
	if (i < 0 || c[1] > maxCount) return NAN;
	while (i >= 0 && AT(i) && c[0] <= maxCount) c[0]++, i--;
	if (c[0] > maxCount) return NAN;
	i = start + 1;
	while (i < limit && AT(i)) c[2]++, i++;
	if (i == limit) return NAN;
	while (i < limit && !AT(i) && c[3] < maxCount) c[3]++, i++;
	if (i == limit || c[3] >= maxCount) return NAN;
	while (i < limit && AT(i) && c[4] < maxCount) c[4]++, i++;
	if (c[4] >= maxCount) return NAN;
#undef AT
	int total = c[0] + c[1] + c[2] + c[3] + c[4];
	if (5 * iabs(total - origTotal) >= 2 * origTotal) return NAN;
	if (!foundCross(c)) return NAN;
	return centerFromEnd(c, i);
}

static bool crossDiagonal(qrFinder *f, int x, int y) {
	const bitImage *b = f->b;
	int c[5] = {0};
	int i = 0;
	while (y >= i && x >= i && at(b, x - i, y - i)) c[2]++, i++;
	if (!c[2]) return false;
	while (y >= i && x >= i && !at(b, x - i, y - i)) c[1]++, i++;
	if (!c[1]) return false;
	while (y >= i && x >= i && at(b, x - i, y - i)) c[0]++, i++;
	if (!c[0]) return false;
	i = 1;
	while (y + i < b->h && x + i < b->w && at(b, x + i, y + i)) c[2]++, i++;
	while (y + i < b->h && x + i < b->w && !at(b, x + i, y + i)) c[3]++, i++;
	if (!c[3]) return false;
	while (y + i < b->h && x + i < b->w && at(b, x + i, y + i)) c[4]++, i++;
	if (!c[4]) return false;
	return foundDiagonal(c);
}

static bool handle(qrFinder *f, const int c[5], int y, int x) {
	int total = c[0] + c[1] + c[2] + c[3] + c[4];
	double cx = centerFromEnd(c, x);
	double cy = crossCheck(f, itrunc(cx), y, c[2], total, true);
	if (isnan(cy)) return false;
	cx = crossCheck(f, itrunc(cx), itrunc(cy), c[2], total, false);
	if (isnan(cx) || !crossDiagonal(f, itrunc(cx), itrunc(cy))) return false;
	double size = total / 7.0;
	for (size_t i = 0; i < f->centers.n; i++)
		if (near_(&f->centers.v[i], size, cy, cx)) {
			combine(&f->centers.v[i], cy, cx, size);
			return true;
		}
	finder n = {cx, cy, size, 1};
	VPUSH(f->centers, n);
	return true;
}

static void find(qrFinder *f) {
	const bitImage *b = f->b;
	int skip = b->h < 100 ? 1 : 2;
	for (int y = skip - 1; y < b->h; y += skip) {
		int c[5] = {0};
		int state = 0;
		const uint8_t *row = b->pix + (size_t)y * b->w;
		for (int x = 0; x < b->w; x++) {
			if (row[x]) {
				if ((state & 1) == 1) state++;
				c[state]++;
				continue;
			}
			if ((state & 1) == 1) {
				c[state]++;
				continue;
			}
			if (state != 4) {
				state++;
				c[state]++;
				continue;
			}
			if (foundCross(c) && handle(f, c, y, x)) {
				memset(c, 0, sizeof c);
				state = 0;
				continue;
			}
			int n[5] = {c[2], c[3], c[4], 1, 0};
			memcpy(c, n, sizeof c);
			state = 3;
		}
		if (foundCross(c)) handle(f, c, y, b->w);
	}
}

// orderFinders is three finders as bottom left, top left and top right.
static void orderFinders(finder *p[3], finder **bl, finder **tl, finder **tr) {
#define D(a, b) hypot((a)->x - (b)->x, (a)->y - (b)->y)
	double d01 = D(p[0], p[1]), d12 = D(p[1], p[2]), d02 = D(p[0], p[2]);
#undef D
	finder *a, *b, *c;
	if (d12 >= d01 && d12 >= d02) b = p[0], a = p[1], c = p[2];
	else if (d02 >= d12 && d02 >= d01) b = p[1], a = p[0], c = p[2];
	else b = p[2], a = p[0], c = p[1];
	if ((c->x - b->x) * (a->y - b->y) - (c->y - b->y) * (a->x - b->x) < 0) {
		finder *t = a;
		a = c;
		c = t;
	}
	*bl = a, *tl = b, *tr = c;
}

typedef struct {
	int t[3];
	double score;
} triple;

typedef VEC(triple) triples;

static bool compatible(double a, double b) { return fabs(a - b) <= 0.5 || fabs(a - b) / fmin(a, b) < 0.4; }

// finderTriples is the sets of three finders that could be one code's,
// best first.
static void finderTriples(finders *cs, triples *out) {
	VEC(int) idx = {0};
	for (size_t i = 0; i < cs->n; i++)
		if (cs->v[i].count >= 2) VPUSH(idx, (int)i);
	if (idx.n < 3) {
		// A small code may be confirmed once per finder.
		idx.n = 0;
		for (size_t i = 0; i < cs->n; i++) VPUSH(idx, (int)i);
	}
	// By size (Go's sort isn't stable here; an insertion sort is a
	// stable order of the same).
	for (size_t i = 1; i < idx.n; i++) {
		int v = idx.v[i];
		size_t j = i;
		while (j > 0 && cs->v[idx.v[j - 1]].size > cs->v[v].size) {
			idx.v[j] = idx.v[j - 1];
			j--;
		}
		idx.v[j] = v;
	}
	for (size_t i = 0; i < idx.n; i++) {
		finder *p = &cs->v[idx.v[i]];
		for (size_t j = i + 1; j < idx.n; j++) {
			finder *q = &cs->v[idx.v[j]];
			if (!compatible(p->size, q->size)) break;
			for (size_t k = j + 1; k < idx.n; k++) {
				finder *r = &cs->v[idx.v[k]];
				if (!compatible(q->size, r->size)) break;
				finder *three[3] = {p, q, r}, *bl, *tl, *tr;
				orderFinders(three, &bl, &tl, &tr);
				double dA = hypot(bl->x - tl->x, bl->y - tl->y);
				double dB = hypot(tr->x - tl->x, tr->y - tl->y);
				double dC = hypot(tr->x - bl->x, tr->y - bl->y);
				double size = (p->size + q->size + r->size) / 3;
				double modules = (dA + dB) / (2 * size);
				if (modules > 180 || modules < 9) continue;
				double vAB = fabs(dA - dB) / fmin(dA, dB);
				if (vAB >= 0.5) continue;
				double dCpy = hypot(dA, dB);
				double vC = fabs(dC - dCpy) / fmin(dC, dCpy);
				if (vC >= 0.25) continue;
				triple t = {{idx.v[i], idx.v[j], idx.v[k]}, vAB + vC};
				VPUSH(*out, t);
			}
		}
	}
	for (size_t i = 1; i < out->n; i++) {
		triple t = out->v[i];
		size_t j = i;
		while (j > 0 && out->v[j - 1].score > t.score) {
			out->v[j] = out->v[j - 1];
			j--;
		}
		out->v[j] = t;
	}
	VFREE(idx);
}

// bwbRun is the length of the dark, light, dark runs from (fx, fy)
// toward (tx, ty).
static double bwbRun(const bitImage *img, int fx, int fy, int tx, int ty) {
	bool steep = iabs(ty - fy) > iabs(tx - fx);
	if (steep) {
		int t = fx;
		fx = fy, fy = t;
		t = tx;
		tx = ty, ty = t;
	}
	int dx = iabs(tx - fx), dy = iabs(ty - fy);
	int e = -dx / 2;
	int xs = fx > tx ? -1 : 1, ys = fy > ty ? -1 : 1;
	int state = 0;
	int xl = tx + xs;
	for (int x = fx, y = fy; x != xl; x += xs) {
		int rx = steep ? y : x, ry = steep ? x : y;
		if ((state == 1) == at(img, rx, ry)) {
			if (state == 2) return hypot(x - fx, y - fy);
			state++;
		}
		e += dy;
		if (e > 0) {
			if (y == ty) break;
			y += ys;
			e -= dx;
		}
	}
	if (state == 2) return hypot(tx + xs - fx, ty - fy);
	return NAN;
}

static double runBothWays(const bitImage *img, int fx, int fy, int tx, int ty) {
	double r = bwbRun(img, fx, fy, tx, ty);
	double scale = 1.0;
	int ox = fx - (tx - fx);
	if (ox < 0) {
		scale = (double)fx / (fx - ox);
		ox = 0;
	} else if (ox >= img->w) {
		scale = (double)(img->w - 1 - fx) / (ox - fx);
		ox = img->w - 1;
	}
	int oy = itrunc(fy - (double)(ty - fy) * scale);
	scale = 1;
	if (oy < 0) {
		scale = (double)fy / (fy - oy);
		oy = 0;
	} else if (oy >= img->h) {
		scale = (double)(img->h - 1 - fy) / (oy - fy);
		oy = img->h - 1;
	}
	ox = itrunc(fx + (double)(ox - fx) * scale);
	r += bwbRun(img, fx, fy, ox, oy);
	return r - 1;
}

// moduleSizeOneWay estimates the module size from the finder a's run
// toward b and back.
static double moduleSizeOneWay(const bitImage *img, const finder *a, const finder *b) {
	double x = runBothWays(img, itrunc(a->x), itrunc(a->y), itrunc(b->x), itrunc(b->y));
	double y = runBothWays(img, itrunc(b->x), itrunc(b->y), itrunc(a->x), itrunc(a->y));
	if (isnan(x)) return y / 7;
	if (isnan(y)) return x / 7;
	return (x + y) / 14;
}

typedef struct {
	const bitImage *b;
	double size;
	point axes[2];
	finders cands;
} aligner;

static bool alignCross(const aligner *a, const int c[3]) {
	for (int i = 0; i < 3; i++)
		if (fabs(a->size - c[i]) >= 0.6 * a->size) return false;
	return true;
}

// ring reports whether (x, y) is the middle of the pattern: dark, in
// light, in dark, along the code's axes.
static bool ring(const aligner *a, double x, double y) {
	int ok = 0;
	for (int i = -2; i <= 2; i++)
		for (int j = -2; j <= 2; j++) {
			if ((i == 0 && j == 0) || ((iabs(i) == 2 || iabs(j) == 2) && (i + j) % 2 != 0)) {
				// The center's checked by the runs; on the outer ring,
				// the corners and middles are enough.
				continue;
			}
			double px = x + i * a->axes[0].x + j * a->axes[1].x;
			double py = y + i * a->axes[0].y + j * a->axes[1].y;
			bool dark = MAX(iabs(i), iabs(j)) == 2;
			if (at(a->b, ifloor(px), ifloor(py)) == dark) ok++;
		}
	return ok >= 15;  // of 16
}

static double alignCrossCheck(const aligner *a, int startY, int x, int maxCount, int origTotal) {
	const bitImage *b = a->b;
	int c[3] = {0};
	int y = startY;
	while (y >= 0 && at(b, x, y) && c[1] <= maxCount) c[1]++, y--;
	if (y < 0 || c[1] > maxCount) return NAN;
	while (y >= 0 && !at(b, x, y) && c[0] <= maxCount) c[0]++, y--;
	if (c[0] > maxCount) return NAN;
	y = startY + 1;
	while (y < b->h && at(b, x, y) && c[1] <= maxCount) c[1]++, y++;
	if (y == b->h || c[1] > maxCount) return NAN;
	while (y < b->h && !at(b, x, y) && c[2] <= maxCount) c[2]++, y++;
	if (c[2] > maxCount) return NAN;
	int total = c[0] + c[1] + c[2];
	if (5 * iabs(total - origTotal) >= 2 * origTotal) return NAN;
	if (!alignCross(a, c)) return NAN;
	return (double)(y - c[2]) - c[1] / 2.0;
}

static void alignCheck(aligner *a, const int c[3], int y, int end) {
	int total = c[0] + c[1] + c[2];
	double cx = (double)(end - c[2]) - c[1] / 2.0;
	double cy = alignCrossCheck(a, y, itrunc(cx), 2 * c[1], total);
	if (isnan(cy) || !ring(a, cx, cy)) return;
	double s = total / 3.0;
	for (size_t i = 0; i < a->cands.n; i++)
		if (near_(&a->cands.v[i], s, cy, cx)) {
			combine(&a->cands.v[i], cy, cx, s);
			return;
		}
	finder n = {cx, cy, s, 1};
	VPUSH(a->cands, n);
}

// findAlignment looks for an alignment pattern (light, dark, light runs
// 1:1:1, inside a dark ring) within allow modules of (ex, ey): the one
// nearest it.
static bool findAlignment(const bitImage *b, double size, double ex, double ey, double allow, const point axes[2], double *ox, double *oy) {
	int al = itrunc(allow * size);
	int left = MAX(0, itrunc(ex) - al), right = MIN(b->w - 1, itrunc(ex) + al);
	if ((double)(right - left) < size * 3) return false;
	int top = MAX(0, itrunc(ey) - al), bottom = MIN(b->h - 1, itrunc(ey) + al);
	if ((double)(bottom - top) < size * 3) return false;
	aligner a = {b, size, {axes[0], axes[1]}, {0}};
	for (int y = top; y <= bottom; y++) {
		int c[3] = {0};
		int x = left;
		while (x < right && !at(b, x, y)) x++;
		int state = 0;
		for (; x < right; x++) {
			if (at(b, x, y)) {
				if (state == 1) {
					c[1]++;
				} else if (state == 2) {
					if (alignCross(&a, c)) alignCheck(&a, c, y, x);
					int n[3] = {c[2], 1, 0};
					memcpy(c, n, sizeof c);
					state = 1;
				} else {
					state++;
					c[state]++;
				}
			} else {
				if (state == 1) state++;
				c[state]++;
			}
		}
		if (alignCross(&a, c)) alignCheck(&a, c, y, right);
	}
	int best = -1;
	double bestD = INFINITY;
	for (size_t i = 0; i < a.cands.n; i++) {
		double d = hypot(a.cands.v[i].x - ex, a.cands.v[i].y - ey);
		if (d < bestD) best = (int)i, bestD = d;
	}
	if (best >= 0) *ox = a.cands.v[best].x, *oy = a.cands.v[best].y;
	VFREE(a.cands);
	return best >= 0;
}

// ---- QR codes: reading ----

// QR code error correction levels, as their format bits name them.
enum { ecM = 0, ecL = 1, ecH = 2, ecQ = 3 };

// qrECCPerBlock and qrBlocks are, by level (L, M, Q, H) and version, the
// check codewords in each block and the number of blocks.
static const int8_t qrECCPerBlock[4][41] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};
static const int8_t qrBlocks[4][41] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

// levelIndex is a level's row in the tables above.
static int levelIndex(int ec) {
	static const int rows[4] = {1, 0, 3, 2};
	return rows[ec];
}

// qrSize is the modules on a side of version v.
static int qrSize(int v) { return 17 + 4 * v; }

// qrRawModules is the modules that carry codewords in version v.
static int qrRawModules(int v) {
	int n = (16 * v + 128) * v + 64;
	if (v >= 2) {
		int a = v / 7 + 2;
		n -= (25 * a - 10) * a - 55;
		if (v >= 7) n -= 36;
	}
	return n;
}

// qrAlignment is the rows (and columns) of version v's alignment pattern
// centers: their count.
static int qrAlignment(int v, int out[7]) {
	if (v == 1) return 0;
	int n = v / 7 + 2;
	int step = (v * 8 + n * 3 + 5) / (n * 4 - 4) * 2;
	out[0] = 6;
	int pos = qrSize(v) - 7;
	for (int i = n - 1; i >= 1; i--) {
		out[i] = pos;
		pos -= step;
	}
	return n;
}

// qrBlockLayout is version v's blocks at level ec: how many, their check
// codewords, and the data codewords of the short ones (the long ones
// have one more), with how many are short.
static void qrBlockLayout(int v, int ec, int *blocks, int *check, int *shortData, int *shortCount) {
	int li = levelIndex(ec);
	*blocks = qrBlocks[li][v];
	*check = qrECCPerBlock[li][v];
	int raw = qrRawModules(v) / 8;
	*shortCount = *blocks - raw % *blocks;
	*shortData = raw / *blocks - *check;
}

// qrFunction marks version v's function modules (finders, timing,
// alignment, format and version information).
static grid qrFunction(int v) {
	int n = qrSize(v);
	grid g = newGrid(n, n);
#define REGION(x_, y_, w_, h_)                                                  \
	for (int ry_ = (y_); ry_ < (y_) + (h_); ry_++)                          \
		for (int rx_ = (x_); rx_ < (x_) + (w_); rx_++) g.bits[ry_ * n + rx_] = 1;
	REGION(0, 0, 9, 9);
	REGION(n - 8, 0, 8, 9);
	REGION(0, n - 8, 9, 8);
	int al[7];
	int na = qrAlignment(v, al), last = na - 1;
	for (int i = 0; i < na; i++)
		for (int j = 0; j < na; j++) {
			if ((i == 0 && j == 0) || (i == 0 && j == last) || (i == last && j == 0)) continue;
			REGION(al[i] - 2, al[j] - 2, 5, 5);
		}
	REGION(6, 9, 1, n - 17);
	REGION(9, 6, n - 17, 1);
	if (v >= 7) {
		REGION(n - 11, 0, 3, 6);
		REGION(0, n - 11, 6, 3);
	}
#undef REGION
	return g;
}

// qrMasked reports whether mask m flips the module at (x, y).
static bool qrMasked(int m, int x, int y) {
	switch (m) {
	case 0:
		return (x + y) % 2 == 0;
	case 1:
		return y % 2 == 0;
	case 2:
		return x % 3 == 0;
	case 3:
		return (x + y) % 3 == 0;
	case 4:
		return (y / 2 + x / 3) % 2 == 0;
	case 5:
		return x * y % 2 + x * y % 3 == 0;
	case 6:
		return (x * y % 2 + x * y % 3) % 2 == 0;
	default:
		return ((x + y) % 2 + x * y % 3) % 2 == 0;
	}
}

// qrFormatBits is the 15 masked format bits of 5 data bits.
static int qrFormatBits(int data) {
	int rem = data;
	for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	return ((data << 10) | rem) ^ 0x5412;
}

// qrVersionBits is the 18 version bits of version v.
static int qrVersionBits(int v) {
	int rem = v;
	for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	return (v << 12) | rem;
}

static int popcount(unsigned v) {
	int n = 0;
	for (; v; v &= v - 1) n++;
	return n;
}

// decodeFormat finds the format (level and mask) nearest either copy of
// its bits, if within 3 bits.
static bool decodeFormat(int a, int b, int *ec, int *mask) {
	int best = 0, bestD = 99;
	for (int d = 0; d < 32; d++) {
		int f = qrFormatBits(d);
		int xs[2] = {a, b};
		for (int k = 0; k < 2; k++) {
			int n = popcount((unsigned)(f ^ xs[k]));
			if (n < bestD) best = d, bestD = n;
		}
	}
	if (bestD > 3) return false;
	*ec = best >> 3, *mask = best & 7;
	return true;
}

// decodeVersion finds the version nearest version bits, if within 3.
static int decodeVersion(int a) {
	int best = 0, bestD = 99;
	for (int v = 7; v <= 40; v++) {
		int n = popcount((unsigned)(qrVersionBits(v) ^ a));
		if (n < bestD) best = v, bestD = n;
	}
	return bestD <= 3 ? best : 0;
}

// qrReadFormat reads a sampled code's format information.
static bool qrReadFormat(const grid *g, int *ec, int *mask) {
	int n = g->w;
#define BIT(acc, x, y) ((acc) = ((acc) << 1) | (gat(g, (x), (y)) ? 1 : 0))
	int a = 0;
	for (int x = 0; x <= 5; x++) BIT(a, x, 8);
	BIT(a, 7, 8);
	BIT(a, 8, 8);
	BIT(a, 8, 7);
	for (int y = 5; y >= 0; y--) BIT(a, 8, y);
	int b = 0;
	for (int y = n - 1; y >= n - 7; y--) BIT(b, 8, y);
	for (int x = n - 8; x < n; x++) BIT(b, x, 8);
#undef BIT
	return decodeFormat(a, b, ec, mask);
}

// qrReadVersion reads a sampled code's version information (versions 7
// and up): 0 if neither copy is readable.
static int qrReadVersion(const grid *g) {
	int n = g->w, a = 0, b = 0;
	for (int j = 5; j >= 0; j--)
		for (int i = n - 9; i >= n - 11; i--) {
			a = (a << 1) | (gat(g, i, j) ? 1 : 0);
			b = (b << 1) | (gat(g, j, i) ? 1 : 0);
		}
	int v = decodeVersion(a);
	if (v) return v;
	return decodeVersion(b);
}

// qrCorrect deinterleaves raw into blocks, corrects each, and is the
// data codewords (NULL if it can't).
static uint8_t *qrCorrect(const uint8_t *raw, int v, int ec, int *ndata) {
	int blocks, check, shortData, shortCount;
	qrBlockLayout(v, ec, &blocks, &check, &shortData, &shortCount);
	int *lens = malloc((size_t)blocks * sizeof *lens);
	uint8_t **bs = malloc((size_t)blocks * sizeof *bs);
	int total = 0;
	for (int i = 0; i < blocks; i++) {
		lens[i] = shortData + (i >= shortCount ? 1 : 0);
		total += lens[i];
		bs[i] = malloc((size_t)lens[i] + check);
	}
	int k = 0;
	for (int i = 0; i <= shortData; i++)
		for (int b = 0; b < blocks; b++)
			if (i < lens[b]) bs[b][i] = raw[k++];
	for (int i = 0; i < check; i++)
		for (int b = 0; b < blocks; b++) bs[b][lens[b] + i] = raw[k++];
	uint8_t *out = malloc((size_t)total + 1);
	int n = 0;
	bool ok = true;
	for (int b = 0; b < blocks && ok; b++) {
		if (!rsDecode(qrField(), bs[b], lens[b] + check, check)) {
			ok = false;
			break;
		}
		memcpy(out + n, bs[b], (size_t)lens[b]);
		n += lens[b];
	}
	for (int b = 0; b < blocks; b++) free(bs[b]);
	free(bs);
	free(lens);
	if (!ok) {
		free(out);
		return NULL;
	}
	*ndata = n;
	return out;
}

// bitReader reads bits, most significant first.
typedef struct {
	const uint8_t *b;
	int n, pos;
} bitReader;

static int left(const bitReader *r) { return 8 * r->n - r->pos; }

static int readBits(bitReader *r, int n) {
	int v = 0;
	for (int i = 0; i < n; i++) {
		v <<= 1;
		if (r->pos < 8 * r->n && (r->b[r->pos >> 3] & (0x80 >> (r->pos & 7))) != 0) v |= 1;
		r->pos++;
	}
	return v;
}

static const char qrAlnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// qrDecodeData reads a QR code's segments: its text (NULL if it can't).
static char *qrDecodeData(const uint8_t *data, int ndata, int v) {
	bitReader r = {data, ndata, 0};
	bytes out = {0}, pending = {0};  // pending: bytes of the current ECI's charset
	int eci = eciNone;
	bool fnc1 = false;
#define FLUSH()                                                        \
	do {                                                           \
		if (pending.n) bytesText(&out, pending.v, pending.n, eci); \
		pending.n = 0;                                         \
	} while (0)
#define COUNT(s, m, l) (v <= 9 ? (s) : v <= 26 ? (m) : (l))
	while (left(&r) >= 4) {
		int mode = readBits(&r, 4);
		switch (mode) {
		case 0:
			FLUSH();
			goto done;
		case 1: {  // numeric
			int n = readBits(&r, COUNT(10, 12, 14));
			for (; n >= 3; n -= 3) {
				if (left(&r) < 10) goto fail;
				int d = readBits(&r, 10);
				if (d >= 1000) goto fail;
				bput(&pending, (uint8_t)('0' + d / 100));
				bput(&pending, (uint8_t)('0' + d / 10 % 10));
				bput(&pending, (uint8_t)('0' + d % 10));
			}
			if (n == 2) {
				if (left(&r) < 7) goto fail;
				int d = readBits(&r, 7);
				if (d >= 100) goto fail;
				bput(&pending, (uint8_t)('0' + d / 10));
				bput(&pending, (uint8_t)('0' + d % 10));
			} else if (n == 1) {
				if (left(&r) < 4) goto fail;
				int d = readBits(&r, 4);
				if (d >= 10) goto fail;
				bput(&pending, (uint8_t)('0' + d));
			}
			break;
		}
		case 2: {  // alphanumeric
			int n = readBits(&r, COUNT(9, 11, 13));
			size_t start = pending.n;
			for (; n >= 2; n -= 2) {
				if (left(&r) < 11) goto fail;
				int d = readBits(&r, 11);
				if (d >= 45 * 45) goto fail;
				bput(&pending, (uint8_t)qrAlnum[d / 45]);
				bput(&pending, (uint8_t)qrAlnum[d % 45]);
			}
			if (n == 1) {
				if (left(&r) < 6) goto fail;
				int d = readBits(&r, 6);
				if (d >= 45) goto fail;
				bput(&pending, (uint8_t)qrAlnum[d]);
			}
			if (fnc1) {
				// In GS1 mode, % is the separator and %% a percent sign.
				size_t o = start;
				for (size_t i = start; i < pending.n; i++) {
					if (pending.v[i] == '%') {
						if (i + 1 < pending.n && pending.v[i + 1] == '%') {
							pending.v[o++] = '%';
							i++;
						} else {
							pending.v[o++] = 0x1D;
						}
					} else {
						pending.v[o++] = pending.v[i];
					}
				}
				pending.n = o;
			}
			break;
		}
		case 4: {  // bytes
			int n = readBits(&r, COUNT(8, 16, 16));
			if (left(&r) < 8 * n) goto fail;
			for (int i = 0; i < n; i++) bput(&pending, (uint8_t)readBits(&r, 8));
			break;
		}
		case 8: {  // kanji
			int n = readBits(&r, COUNT(8, 10, 12));
			if (left(&r) < 13 * n) goto fail;
			FLUSH();
			for (int i = 0; i < n; i++) {
				int d = readBits(&r, 13);
				int c = ((d / 0xC0) << 8) | (d % 0xC0);
				c += c < 0x1F00 ? 0x8140 : 0xC140;
				uint32_t ru = sjisRune((uint8_t)(c >> 8), (uint8_t)c);
				if (!ru) goto fail;
				bput_rune(&out, ru);
			}
			break;
		}
		case 7: {  // ECI
			FLUSH();
			int first = readBits(&r, 8);
			if ((first & 0x80) == 0) eci = first & 0x7F;
			else if ((first & 0xC0) == 0x80) eci = ((first & 0x3F) << 8) | readBits(&r, 8);
			else if ((first & 0xE0) == 0xC0) eci = ((first & 0x1F) << 16) | readBits(&r, 16);
			else goto fail;
			break;
		}
		case 3:  // structured append: its position and parity, ignored
			readBits(&r, 16);
			break;
		case 5:  // FNC1, first position
			fnc1 = true;
			break;
		case 9:  // FNC1, second position: an application indicator
			readBits(&r, 8);
			fnc1 = true;
			break;
		default:
			goto fail;
		}
	}
	FLUSH();
done:;
	char *s = bstr(&out);
	VFREE(out);
	VFREE(pending);
	return s;
fail:
	VFREE(out);
	VFREE(pending);
	return NULL;
#undef FLUSH
#undef COUNT
}

// qrDecodeGrid reads a sampled code of version v: its text (NULL if it
// can't).
static char *qrDecodeGrid(const grid *g, int v) {
	int ec, mask;
	if (!qrReadFormat(g, &ec, &mask)) return NULL;
	int n = qrSize(v);
	grid fn = qrFunction(v);
	int nraw = qrRawModules(v) / 8;
	uint8_t *raw = calloc((size_t)nraw, 1);
	int k = 0, nbits = 0;
	uint8_t cur = 0;
	bool up = true;
	for (int right = n - 1; right >= 1; right -= 2) {
		if (right == 6) right = 5;
		for (int c = 0; c < n; c++) {
			int y = up ? n - 1 - c : c;
			for (int j = 0; j < 2; j++) {
				int x = right - j;
				if (fn.bits[y * n + x]) continue;
				bool d = g->bits[y * n + x] != 0;
				if (qrMasked(mask, x, y)) d = !d;
				cur = (uint8_t)(cur << 1 | (d ? 1 : 0));
				if (++nbits == 8) {
					if (k < nraw) raw[k] = cur;
					k++;
					cur = 0, nbits = 0;
				}
			}
		}
		up = !up;
	}
	free(fn.bits);
	int ndata;
	uint8_t *data = qrCorrect(raw, v, ec, &ndata);
	free(raw);
	if (!data) return NULL;
	char *s = qrDecodeData(data, ndata, v);
	free(data);
	return s;
}

static grid transpose(const grid *g) {
	grid t = newGrid(g->h, g->w);
	for (int y = 0; y < g->h; y++)
		for (int x = 0; x < g->w; x++) t.bits[x * t.w + y] = g->bits[y * g->w + x];
	return t;
}

// qrTransforms maps version v's modules to the image, through its
// finders and its bottom right alignment pattern if one is found (then
// without it, in case it was a look-alike): their count.
static int qrTransforms(const bitImage *b, const finder *bl, const finder *tl, const finder *tr, int v, double size, perspective out[2]) {
	double dim = qrSize(v);
	double brx = tr->x - tl->x + bl->x, bry = tr->y - tl->y + bl->y;
	double m = dim - 3.5;
	perspective plain = quadToQuad(3.5, 3.5, m, 3.5, m, m, 3.5, m, tl->x, tl->y, tr->x, tr->y, brx, bry, bl->x, bl->y);
	if (v < 2) {
		out[0] = plain;
		return 1;
	}
	// Where the alignment pattern would be, and the module's axes there.
	double ex, ey, x0, y0, x1, y1, x2, y2;
	apply(&plain, dim - 6.5, dim - 6.5, &ex, &ey);
	apply(&plain, dim - 6.5, dim - 6.5, &x0, &y0);
	apply(&plain, dim - 5.5, dim - 6.5, &x1, &y1);
	apply(&plain, dim - 6.5, dim - 5.5, &x2, &y2);
	point axes[2] = {{x1 - x0, y1 - y0}, {x2 - x0, y2 - y0}};
	for (int allow = 4; allow <= 16; allow <<= 1) {
		double ax, ay;
		if (findAlignment(b, size, ex, ey, allow, axes, &ax, &ay)) {
			out[0] = quadToQuad(3.5, 3.5, m, 3.5, dim - 6.5, dim - 6.5, 3.5, m, tl->x, tl->y, tr->x, tr->y, ax, ay, bl->x, bl->y);
			out[1] = plain;
			return 2;
		}
	}
	out[0] = plain;
	return 1;
}

// qrDetectDecode reads the code whose finders are given: its text and
// corners (NULL if it can't).
static char *qrDetectDecode(const bitImage *b, const finder *bl, const finder *tl, const finder *tr, point corners[4]) {
	double size = (moduleSizeOneWay(b, tl, tr) + moduleSizeOneWay(b, tl, bl)) / 2;
	if (size < 1 || isnan(size)) return NULL;
	double dA = hypot(tl->x - tr->x, tl->y - tr->y) / size;
	double dB = hypot(tl->x - bl->x, tl->y - bl->y) / size;
	if (!(dA < 1e6 && dB < 1e6)) return NULL;
	int dim = itrunc(round(dA) + round(dB)) / 2 + 7;
	switch (dim & 3) {
	case 0:
		dim++;
		break;
	case 2:
		dim--;
		break;
	case 3:
		dim -= 2;
		break;
	}
	bool tried[178] = {false};
	int try[16], ntry = 0;
	try[ntry++] = dim, try[ntry++] = dim + 4, try[ntry++] = dim - 4;
	while (ntry > 0) {
		int d = try[0];
		memmove(try, try + 1, (size_t)--ntry * sizeof *try);
		if (d < 21 || d > 177 || tried[d]) continue;
		tried[d] = true;
		int v = (d - 17) / 4;
		perspective ts[2];
		int nt = qrTransforms(b, bl, tl, tr, v, size, ts);
		for (int i = 0; i < nt; i++) {
			grid g;
			if (!sampleGrid(b, &ts[i], d, d, &g)) continue;
			if (v >= 7) {
				int rv = qrReadVersion(&g);
				if (rv && rv != v) {
					// The version information knows better.
					if (ntry < 15) {
						memmove(try + 1, try, (size_t)ntry * sizeof *try);
						try[0] = qrSize(rv);
						ntry++;
					}
					free(g.bits);
					break;
				}
			}
			char *text = qrDecodeGrid(&g, v);
			if (!text) {
				// A mirrored code reads transposed.
				grid t = transpose(&g);
				text = qrDecodeGrid(&t, v);
				free(t.bits);
			}
			free(g.bits);
			if (text) {
				double n = d;
				double cs[4][2] = {{0, 0}, {n, 0}, {n, n}, {0, n}};
				for (int k = 0; k < 4; k++) apply(&ts[i], cs[k][0], cs[k][1], &corners[k].x, &corners[k].y);
				return text;
			}
		}
	}
	return NULL;
}

// decodeQRCodes finds and reads the QR codes in b.
static void decodeQRCodes(const bitImage *b, codes *out) {
	qrFinder f = {b, {0}};
	find(&f);
	if (f.centers.n < 3) {
		VFREE(f.centers);
		return;
	}
	bool *used = calloc(f.centers.n, 1);
	triples ts = {0};
	finderTriples(&f.centers, &ts);
	for (size_t k = 0; k < ts.n; k++) {
		int *t = ts.v[k].t;
		if (used[t[0]] || used[t[1]] || used[t[2]]) continue;
		finder *three[3] = {&f.centers.v[t[0]], &f.centers.v[t[1]], &f.centers.v[t[2]]}, *bl, *tl, *tr;
		orderFinders(three, &bl, &tl, &tr);
		point corners[4];
		char *text = qrDetectDecode(b, bl, tl, tr, corners);
		if (!text) continue;
		used[t[0]] = used[t[1]] = used[t[2]] = true;
		code c = {ASUKA_BARCODE_QR, text, 0, 0, 0, 0};
		boundsOf(corners, 4, &c.x0, &c.y0, &c.x1, &c.y1);
		VPUSH(*out, c);
	}
	VFREE(ts);
	free(used);
	VFREE(f.centers);
}

// ---- Data Matrix: reading ----

// dmSize is a Data Matrix (ECC 200) symbol size: its modules, its data
// regions' modules, its data codewords, and its blocks' check codewords
// and number.
typedef struct {
	int rows, cols, regRows, regCols, data, check, blocks;
} dmSize;

static const dmSize dmSizes[] = {
    {10, 10, 8, 8, 3, 5, 1},         {12, 12, 10, 10, 5, 7, 1},       {14, 14, 12, 12, 8, 10, 1},     {16, 16, 14, 14, 12, 12, 1},
    {18, 18, 16, 16, 18, 14, 1},     {20, 20, 18, 18, 22, 18, 1},     {22, 22, 20, 20, 30, 20, 1},    {24, 24, 22, 22, 36, 24, 1},
    {26, 26, 24, 24, 44, 28, 1},     {32, 32, 14, 14, 62, 36, 1},     {36, 36, 16, 16, 86, 42, 1},    {40, 40, 18, 18, 114, 48, 1},
    {44, 44, 20, 20, 144, 56, 1},    {48, 48, 22, 22, 174, 68, 1},    {52, 52, 24, 24, 204, 42, 2},   {64, 64, 14, 14, 280, 56, 2},
    {72, 72, 16, 16, 368, 36, 4},    {80, 80, 18, 18, 456, 48, 4},    {88, 88, 20, 20, 576, 56, 4},   {96, 96, 22, 22, 696, 68, 4},
    {104, 104, 24, 24, 816, 56, 6},  {120, 120, 18, 18, 1050, 68, 6}, {132, 132, 20, 20, 1304, 62, 8}, {144, 144, 22, 22, 1558, 62, 10},
    {8, 18, 6, 16, 5, 7, 1},         {8, 32, 6, 14, 10, 11, 1},       {12, 26, 10, 24, 16, 14, 1},    {12, 36, 10, 16, 22, 18, 1},
    {16, 36, 14, 16, 32, 24, 1},     {16, 48, 14, 22, 49, 28, 1},
};
enum { nDmSizes = sizeof dmSizes / sizeof *dmSizes };

// mapRows and mapCols are the size of the symbol's data, its regions
// put together.
static int mapRows(const dmSize *s) { return s->rows / (s->regRows + 2) * s->regRows; }
static int mapCols(const dmSize *s) { return s->cols / (s->regCols + 2) * s->regCols; }
static int dmTotal(const dmSize *s) { return s->data + s->check * s->blocks; }

typedef VEC(int32_t) placement;

static void placeModule(placement *out, bool *used, int nrow, int ncol, int row, int col) {
	if (row < 0) {
		row += nrow;
		col += 4 - (nrow + 4) % 8;
	}
	if (col < 0) {
		col += ncol;
		row += 4 - (ncol + 4) % 8;
	}
	used[row * ncol + col] = true;
	VPUSH(*out, (int32_t)(row * ncol + col));
}

static void utah(placement *out, bool *used, int nrow, int ncol, int row, int col) {
	static const int d[8][2] = {{-2, -2}, {-2, -1}, {-1, -2}, {-1, -1}, {-1, 0}, {0, -2}, {0, -1}, {0, 0}};
	for (int i = 0; i < 8; i++) placeModule(out, used, nrow, ncol, row + d[i][0], col + d[i][1]);
}

static void cornerPlace(placement *out, bool *used, int nrow, int ncol, const int ps[8][2]) {
	for (int i = 0; i < 8; i++) placeModule(out, used, nrow, ncol, ps[i][0], ps[i][1]);
}

// dmPlacement is where each bit of each codeword goes in the data
// modules (ISO/IEC 16022 Annex F): for codeword i, bit b (0 the most
// significant), entry 8i+b is row*cols+col.
static placement dmPlacement(int nrow, int ncol) {
	placement out = {0};
	bool *used = calloc((size_t)nrow * ncol, 1);
	int row = 4, col = 0;
	for (;;) {
		if (row == nrow && col == 0) {
			const int ps[8][2] = {{nrow - 1, 0}, {nrow - 1, 1}, {nrow - 1, 2}, {0, ncol - 2}, {0, ncol - 1}, {1, ncol - 1}, {2, ncol - 1}, {3, ncol - 1}};
			cornerPlace(&out, used, nrow, ncol, ps);
		}
		if (row == nrow - 2 && col == 0 && ncol % 4 != 0) {
			const int ps[8][2] = {{nrow - 3, 0}, {nrow - 2, 0}, {nrow - 1, 0}, {0, ncol - 4}, {0, ncol - 3}, {0, ncol - 2}, {0, ncol - 1}, {1, ncol - 1}};
			cornerPlace(&out, used, nrow, ncol, ps);
		}
		if (row == nrow - 2 && col == 0 && ncol % 8 == 4) {
			const int ps[8][2] = {{nrow - 3, 0}, {nrow - 2, 0}, {nrow - 1, 0}, {0, ncol - 2}, {0, ncol - 1}, {1, ncol - 1}, {2, ncol - 1}, {3, ncol - 1}};
			cornerPlace(&out, used, nrow, ncol, ps);
		}
		if (row == nrow + 4 && col == 2 && ncol % 8 == 0) {
			const int ps[8][2] = {{nrow - 1, 0}, {nrow - 1, ncol - 1}, {0, ncol - 3}, {0, ncol - 2}, {0, ncol - 1}, {1, ncol - 3}, {1, ncol - 2}, {1, ncol - 1}};
			cornerPlace(&out, used, nrow, ncol, ps);
		}
		for (;;) {
			if (row < nrow && col >= 0 && !used[row * ncol + col]) utah(&out, used, nrow, ncol, row, col);
			row -= 2;
			col += 2;
			if (row < 0 || col >= ncol) break;
		}
		row++;
		col += 3;
		for (;;) {
			if (row >= 0 && col < ncol && !used[row * ncol + col]) utah(&out, used, nrow, ncol, row, col);
			row += 2;
			col -= 2;
			if (row >= nrow || col < 0) break;
		}
		row += 3;
		col++;
		if (row >= nrow && col >= ncol) break;
	}
	free(used);
	return out;
}

// dmCorrect deinterleaves raw (codeword i belongs to block i mod
// blocks), corrects each block, and is the data codewords.
static bool dmCorrect(const uint8_t *raw, int nraw, const dmSize *s, uint8_t *data) {
	int nb = s->blocks;
	uint8_t *block = malloc((size_t)(s->data / nb + 1 + s->check));
	bool ok = true;
	for (int b = 0; b < nb && ok; b++) {
		int n = 0;
		for (int i = b; i < s->data; i += nb) block[n++] = raw[i];
		int nd = n;
		for (int i = s->data + (b - s->data % nb + nb) % nb; i < nraw; i += nb) {
			if (n >= s->data / nb + 1 + s->check) {
				ok = false;
				break;
			}
			block[n++] = raw[i];
		}
		if (!ok || n - nd != s->check || !rsDecode(dmField(), block, n, s->check)) {
			ok = false;
			break;
		}
		for (int i = b, j = 0; i < s->data; i += nb, j++) data[i] = block[j];
	}
	free(block);
	return ok;
}

// Data Matrix encodations.
enum { dmASCII, dmC40, dmText, dmX12, dmEDIFACT, dmBase256 };

static const char dmShift2[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_";

// dmDecodeC40 reads C40 (or Text) from cw[i] until it unlatches: where
// it ends, or -1.
static int dmDecodeC40(const uint8_t *cw, int n, int i, bool text, bytes *out, bool *upper) {
	int shift = 0;
#define EMIT(c)                                       \
	do {                                          \
		int c_ = (c);                         \
		if (*upper) c_ += 128, *upper = false; \
		bput(out, (uint8_t)c_);               \
	} while (0)
	while (i < n) {
		if (cw[i] == 254) return i + 1;
		if (i + 1 >= n) return i;  // a last lone codeword is ASCII
		int v = ((int)cw[i] << 8 | (int)cw[i + 1]) - 1;
		i += 2;
		int vals[3] = {v / 1600, v / 40 % 40, v % 40};
		for (int k = 0; k < 3; k++) {
			int c = vals[k];
			switch (shift) {
			case 0:
				if (c < 3) shift = c + 1;
				else if (c == 3) EMIT(' ');
				else if (c < 14) EMIT('0' + c - 4);
				else if (c < 40) EMIT(text ? 'a' + c - 14 : 'A' + c - 14);
				else return -1;
				break;
			case 1:
				EMIT(c);
				shift = 0;
				break;
			case 2:
				if (c < 27) EMIT(dmShift2[c]);
				else if (c == 27) bput(out, 0x1D);
				else if (c == 30) *upper = true;
				else return -1;
				shift = 0;
				break;
			case 3:
				if (!text) EMIT(c + 96);
				else if (c == 0) EMIT('`');
				else if (c < 27) EMIT('A' + c - 1);
				else EMIT("{|}~\x7f"[c - 27]);
				shift = 0;
				break;
			}
		}
	}
#undef EMIT
	return i;
}

// dmDecodeX12 reads ANSI X12 from cw[i] until it unlatches.
static int dmDecodeX12(const uint8_t *cw, int n, int i, bytes *out) {
	while (i < n) {
		if (cw[i] == 254) return i + 1;
		if (i + 1 >= n) return i;
		int v = ((int)cw[i] << 8 | (int)cw[i + 1]) - 1;
		i += 2;
		int vals[3] = {v / 1600, v / 40 % 40, v % 40};
		for (int k = 0; k < 3; k++) {
			int c = vals[k];
			if (c == 0) bput(out, '\r');
			else if (c == 1) bput(out, '*');
			else if (c == 2) bput(out, '>');
			else if (c == 3) bput(out, ' ');
			else if (c < 14) bput(out, (uint8_t)('0' + c - 4));
			else if (c < 40) bput(out, (uint8_t)('A' + c - 14));
			else return -1;
		}
	}
	return i;
}

// dmDecodeEDIFACT reads EDIFACT from cw[i] until it unlatches.
static int dmDecodeEDIFACT(const uint8_t *cw, int n, int i, bytes *out) {
	while (i < n) {
		if (n - i < 3) return i;  // the last one or two codewords are ASCII
		int v = (int)cw[i] << 16 | (int)cw[i + 1] << 8 | (int)cw[i + 2];
		for (int k = 0; k < 4; k++) {
			int c = (v >> (18 - 6 * k)) & 0x3F;
			if (c == 0x1F) return i + (6 * k + 6 + 7) / 8;  // unlatch: the rest of this codeword's bits are padding
			if ((c & 0x20) == 0) c |= 0x40;
			bput(out, (uint8_t)c);
		}
		i += 3;
	}
	return i;
}

static int unrand(uint8_t c, int pos) {
	int r = (int)c - (149 * pos % 255 + 1);
	if (r < 0) r += 256;
	return r;
}

// dmDecodeBase256 reads Base 256 bytes from cw[i]: where it ends, or -1.
static int dmDecodeBase256(const uint8_t *cw, int ncw, int i, bytes *out) {
	if (i >= ncw) return -1;
	int d1 = unrand(cw[i], i + 1);
	i++;
	int n;
	if (d1 == 0) {
		n = ncw - i;
	} else if (d1 < 250) {
		n = d1;
	} else {
		if (i >= ncw) return -1;
		n = 250 * (d1 - 249) + unrand(cw[i], i + 1);
		i++;
	}
	if (n < 0 || i + n > ncw) return -1;
	for (int k = 0; k < n; k++, i++) bput(out, (uint8_t)unrand(cw[i], i + 1));
	return i;
}

// dmDecodeData reads a symbol's data codewords: its text (NULL if it
// can't).
static char *dmDecodeData(const uint8_t *cw, int n) {
	bytes out = {0}, pending = {0}, suffix = {0};
	int eci = eciNone;
#define FLUSH()                                                        \
	do {                                                           \
		if (pending.n) bytesText(&out, pending.v, pending.n, eci); \
		pending.n = 0;                                         \
	} while (0)
	int mode = dmASCII;
	bool upper = false;
	int i = 0;
	while (i < n) {
		switch (mode) {
		case dmASCII: {
			int c = cw[i++];
			if (c == 0) {
				goto fail;
			} else if (c <= 128) {
				if (upper) c += 128, upper = false;
				bput(&pending, (uint8_t)(c - 1));
			} else if (c == 129) {  // pad: the end
				i = n;
			} else if (c <= 229) {
				int d = c - 130;
				bput(&pending, (uint8_t)('0' + d / 10));
				bput(&pending, (uint8_t)('0' + d % 10));
			} else if (c == 230) {
				mode = dmC40;
			} else if (c == 231) {
				mode = dmBase256;
			} else if (c == 232) {  // FNC1: GS1's leading one dropped
				if (i > 1) bput(&pending, 0x1D);
			} else if (c == 233) {  // structured append: position and file id
				i += 3;
			} else if (c == 234) {  // reader programming
			} else if (c == 235) {
				upper = true;
			} else if (c == 236 || c == 237) {
				bputs(&pending, c == 236 ? "[)>\x1e" "05\x1d" : "[)>\x1e" "06\x1d", 7);
				bytes s = {0};
				bputs(&s, "\x1e\x04", 2);
				for (size_t k = 0; k < suffix.n; k++) bput(&s, suffix.v[k]);
				VFREE(suffix);
				suffix = s;
			} else if (c == 238) {
				mode = dmX12;
			} else if (c == 239) {
				mode = dmText;
			} else if (c == 240) {
				mode = dmEDIFACT;
			} else if (c == 241) {
				FLUSH();
				if (i >= n) goto fail;
				int c1 = cw[i++];
				if (c1 <= 127) {
					eci = c1 - 1;
				} else if (c1 <= 191) {
					if (i >= n) goto fail;
					eci = (c1 - 128) * 254 + (int)cw[i] - 1 + 127;
					i++;
				} else {
					if (i + 1 >= n) goto fail;
					eci = (c1 - 192) * 64516 + ((int)cw[i] - 1) * 254 + (int)cw[i + 1] - 1 + 16383;
					i += 2;
				}
			} else if (c == 254) {  // unlatch, harmless in ASCII
			} else {
				goto fail;
			}
			break;
		}
		case dmC40:
		case dmText:
			i = dmDecodeC40(cw, n, i, mode == dmText, &pending, &upper);
			if (i < 0) goto fail;
			mode = dmASCII;
			break;
		case dmX12:
			i = dmDecodeX12(cw, n, i, &pending);
			if (i < 0) goto fail;
			mode = dmASCII;
			break;
		case dmEDIFACT:
			i = dmDecodeEDIFACT(cw, n, i, &pending);
			mode = dmASCII;
			break;
		case dmBase256:
			i = dmDecodeBase256(cw, n, i, &pending);
			if (i < 0) goto fail;
			mode = dmASCII;
			break;
		}
	}
	FLUSH();
	for (size_t k = 0; k < suffix.n; k++) bput(&out, suffix.v[k]);
	char *s = bstr(&out);
	VFREE(out);
	VFREE(pending);
	VFREE(suffix);
	return s;
fail:
	VFREE(out);
	VFREE(pending);
	VFREE(suffix);
	return NULL;
#undef FLUSH
}

// dmDecodeGrid reads a sampled symbol of size s: its text (NULL if it
// can't).
static char *dmDecodeGrid(const grid *g, const dmSize *s) {
	int nr = mapRows(s), nc = mapCols(s);
	placement place = dmPlacement(nr, nc);
	int n = dmTotal(s);
	if ((int)place.n < 8 * n) {
		VFREE(place);
		return NULL;
	}
	uint8_t *raw = calloc((size_t)n, 1);
	for (int i = 0; i < 8 * n; i++) {
		int p = place.v[i];
		int r = p / nc, c = p % nc;
		int sr = r / s->regRows * (s->regRows + 2) + 1 + r % s->regRows;
		int sc = c / s->regCols * (s->regCols + 2) + 1 + c % s->regCols;
		if (g->bits[sr * g->w + sc]) raw[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
	}
	VFREE(place);
	uint8_t *data = malloc((size_t)s->data);
	char *text = NULL;
	if (dmCorrect(raw, n, s, data)) text = dmDecodeData(data, s->data);
	free(data);
	free(raw);
	return text;
}

// ---- Data Matrix: finding ----
//
// A symbol is found by its finder: the solid L of its left column and
// bottom row, whose two outer edges are long straight sides of the
// convex hull of the dark region they're part of. The opposite edges
// alternate (the timing pattern); which sizes fit, and where the fourth
// corner is, are what makes them alternate best.

// run is a horizontal run of dark pixels, [x0, x1) on row y.
typedef struct {
	int32_t x0, x1, y, parent;
} run;

// side is a straight stretch of a hull: consecutive edges in nearly the
// same direction.
typedef struct {
	point a, b;
	double len;
} side;

// lCorner is a candidate finder: its corner and the far ends of the
// bottom (p1) and left (p2) legs, and the hull's farthest point from
// the corner (p3), likely the symbol's fourth corner.
typedef struct {
	point p0, p1, p2, p3;
} lCorner;

// regions holds the buffers labeling uses.
typedef struct {
	VEC(run) runs;
	int32_t *rowStart;
	int32_t *count;
	VEC(run) grouped;
	VEC(int32_t) spans;  // pairs: start, end
	VEC(point) pts;
	VEC(point) hull;
	VEC(side) sides;
} regions;

static int32_t findRoot(run *runs, int32_t i) {
	while (runs[i].parent != i) {
		runs[i].parent = runs[runs[i].parent].parent;
		i = runs[i].parent;
	}
	return i;
}

// label groups b's dark regions (8-connected) of four rows or more:
// each span of r->grouped is one, its runs in row order.
static void label(regions *r, const bitImage *b) {
	r->runs.n = 0;
	r->rowStart = realloc(r->rowStart, ((size_t)b->h + 1) * sizeof *r->rowStart);
	for (int y = 0; y < b->h; y++) {
		r->rowStart[y] = (int32_t)r->runs.n;
		const uint8_t *row = b->pix + (size_t)y * b->w;
		for (int x = 0; x < b->w;) {
			if (!row[x]) {
				x++;
				continue;
			}
			int s = x;
			while (x < b->w && row[x]) x++;
			run rn = {s, x, y, (int32_t)r->runs.n};
			VPUSH(r->runs, rn);
		}
	}
	r->rowStart[b->h] = (int32_t)r->runs.n;
	run *runs = r->runs.v;
	for (int y = 1; y < b->h; y++) {
		int32_t p = r->rowStart[y - 1], pe = r->rowStart[y], c = r->rowStart[y], ce = r->rowStart[y + 1];
		while (c < ce && p < pe) {
			if (runs[p].x0 <= runs[c].x1 && runs[c].x0 <= runs[p].x1) {
				int32_t a = findRoot(runs, p), bb = findRoot(runs, c);
				// The root is the region's first run.
				if (a != bb) {
					if (a < bb) runs[bb].parent = a;
					else runs[a].parent = bb;
				}
			}
			if (runs[p].x1 < runs[c].x1) p++;
			else c++;
		}
	}
	// Count each region's runs, give each region its span, then place
	// the runs in order.
	size_t n = r->runs.n;
	r->count = realloc(r->count, (n ? n : 1) * sizeof *r->count);
	memset(r->count, 0, n * sizeof *r->count);
	for (size_t i = 0; i < n; i++) {
		int32_t root = findRoot(runs, (int32_t)i);
		runs[i].parent = root;
		r->count[root]++;
	}
	r->spans.n = 0;
	int32_t total = 0;
	for (size_t i = 0; i < n; i++) {
		if (runs[i].parent == (int32_t)i && r->count[i] >= 4) {
			VPUSH(r->spans, total);
			VPUSH(r->spans, total);
			int32_t c = r->count[i];
			// From here on, count[root] is the region's index.
			r->count[i] = (int32_t)(r->spans.n / 2 - 1);
			total += c;
		} else if (runs[i].parent == (int32_t)i) {
			r->count[i] = -1;
		}
	}
	RESERVE(r->grouped, (size_t)(total ? total : 1));
	r->grouped.n = (size_t)total;
	for (size_t i = 0; i < n; i++) {
		int32_t k = r->count[runs[i].parent];
		if (k < 0) continue;
		int32_t *sp = &r->spans.v[2 * k];
		r->grouped.v[sp[1]] = runs[i];
		sp[1]++;
	}
}

static double crossP(point o, point a, point b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

// convexHull is the convex hull of a region's pixels (their corners):
// its count, the points in r->hull.
static size_t convexHull(regions *r, const run *rs, size_t nrs) {
	// At each row boundary, the leftmost and rightmost pixel corners of
	// the rows either side: sorted by y, then x.
	r->pts.n = 0;
	size_t i = 0;
	int32_t prevLo = INT32_MAX, prevHi = INT32_MIN;
	while (i < nrs) {
		int32_t y = rs[i].y, lo = rs[i].x0, hi = rs[i].x1;
		while (i < nrs && rs[i].y == y) {
			lo = MIN(lo, rs[i].x0);
			hi = MAX(hi, rs[i].x1);
			i++;
		}
		double fy = y;
		point a = {(double)MIN(lo, prevLo), fy}, b = {(double)MAX(hi, prevHi), fy};
		VPUSH(r->pts, a);
		VPUSH(r->pts, b);
		prevLo = lo, prevHi = hi;
	}
	double last = rs[nrs - 1].y + 1;
	point a = {(double)prevLo, last}, b = {(double)prevHi, last};
	VPUSH(r->pts, a);
	VPUSH(r->pts, b);
	r->hull.n = 0;
	for (size_t k = 0; k < r->pts.n; k++) {
		point p = r->pts.v[k];
		while (r->hull.n >= 2 && crossP(r->hull.v[r->hull.n - 2], r->hull.v[r->hull.n - 1], p) >= 0) r->hull.n--;
		VPUSH(r->hull, p);
	}
	size_t lower = r->hull.n + 1;
	for (size_t k = r->pts.n - 1; k-- > 0;) {
		point p = r->pts.v[k];
		while (r->hull.n >= lower && crossP(r->hull.v[r->hull.n - 2], r->hull.v[r->hull.n - 1], p) >= 0) r->hull.n--;
		VPUSH(r->hull, p);
	}
	return r->hull.n - 1;
}

// hullSides splits the hull into sides: their count, in r->sides.
static size_t hullSides(regions *r, const point *h, size_t n) {
	r->sides.n = 0;
	if (n < 3) return 0;
	// Start after the sharpest turn, so no side is split at the seam.
	size_t start = 0;
	double sharpest = 2.0;
	for (size_t i = 0; i < n; i++) {
		point p = h[(i + n - 1) % n], q = h[i], rr = h[(i + 1) % n];
		double d1x = q.x - p.x, d1y = q.y - p.y, d2x = rr.x - q.x, d2y = rr.y - q.y;
		double c = (d1x * d2x + d1y * d2y) / (hypot(d1x, d1y) * hypot(d2x, d2y));
		if (c < sharpest) start = i, sharpest = c;
	}
	const double maxTurn = 0.985;  // cos 10°
	side cur = {h[start], h[(start + 1) % n], 0};
	for (size_t k = 1; k < n; k++) {
		point q = h[(start + k + 1) % n];
		double dx = cur.b.x - cur.a.x, dy = cur.b.y - cur.a.y, ex = q.x - cur.b.x, ey = q.y - cur.b.y;
		double c = (dx * ex + dy * ey) / (hypot(dx, dy) * hypot(ex, ey));
		if (c >= maxTurn) {
			cur.b = q;
			continue;
		}
		cur.len = dist(cur.a, cur.b);
		VPUSH(r->sides, cur);
		cur = (side){cur.b, q, 0};
	}
	cur.len = dist(cur.a, cur.b);
	VPUSH(r->sides, cur);
	return r->sides.n;
}

// intersect is where the lines through a and b cross.
static bool intersect(side a, side b, point *p) {
	double d1x = a.b.x - a.a.x, d1y = a.b.y - a.a.y, d2x = b.b.x - b.a.x, d2y = b.b.y - b.a.y;
	double den = d1x * d2y - d1y * d2x;
	if (fabs(den) < 1e-9) return false;
	double t = ((b.a.x - a.a.x) * d2y - (b.a.y - a.a.y) * d2x) / den;
	*p = (point){a.a.x + t * d1x, a.a.y + t * d1y};
	return true;
}

typedef VEC(lCorner) lcorners;

static void lCorners(regions *r, const point *h, size_t nh, lcorners *out) {
	size_t n = hullSides(r, h, nh);
	side *ss = r->sides.v;
	out->n = 0;
	const double minLeg = 12;
	for (size_t i = 0; i < n; i++) {
		side a = ss[i];
		if (a.len < minLeg) continue;
		// The next long side; a blurred corner may be cut by short ones.
		double skipped = 0;
		for (size_t skip = 1; skip <= 4 && skip < n; skip++) {
			side b = ss[(i + skip) % n];
			if (b.len < minLeg) {
				skipped += b.len;
				if (skipped > 0.25 * a.len) break;
				continue;
			}
			if (skipped > 0.25 * b.len) break;
			double ratio = a.len / b.len;
			if (ratio > 6 || ratio < 1.0 / 6) break;
			double dax = (a.b.x - a.a.x) / a.len, day = (a.b.y - a.a.y) / a.len;
			double dbx = (b.b.x - b.a.x) / b.len, dby = (b.b.y - b.a.y) / b.len;
			if (fabs(dax * dbx + day * dby) > 0.5) break;
			point p0;
			if (!intersect(a, b, &p0)) break;
			point endA = a.a, endB = b.b;
			double ux = endA.x - p0.x, uy = endA.y - p0.y, vx = endB.x - p0.x, vy = endB.y - p0.y;
			lCorner c = {p0, endA, endB, {0, 0}};
			if (ux * vy - uy * vx >= 0) c.p1 = endB, c.p2 = endA;
			// Along the diagonal, beyond both legs' far ends.
			double dx = c.p1.x + c.p2.x - 2 * p0.x, dy = c.p1.y + c.p2.y - 2 * p0.y;
			double best = -INFINITY;
			for (size_t k = 0; k < nh; k++) {
				double v = (h[k].x - p0.x) * dx + (h[k].y - p0.y) * dy;
				if (v > best) best = v, c.p3 = h[k];
			}
			VPUSH(*out, c);
			break;
		}
	}
}

// legThickness is the module size the L's leg from p0 to p1 shows: how
// far its dark goes inward (toward in), at its thinner places.
static double legThickness(const bitImage *b, point p0, point p1, point in) {
	double ts[16];
	int nts = 0;
	double l = dist(p0, p1);
	for (int k = 1; k < 16; k++) {
		double f = k / 16.0;
		double x = p0.x + f * (p1.x - p0.x), y = p0.y + f * (p1.y - p0.y);
#define DARK(t) at(b, ifloor(x + (t) * in.x), ifloor(y + (t) * in.y))
		// The hull's line touches the outermost pixels; the edge may be
		// a pixel or two in.
		double t = 0.125;
		while (t < 3 && !DARK(t)) t += 0.25;
		if (t >= 3) continue;
		double t0 = t;
		while (t < l / 4 && DARK(t)) t += 0.25;
#undef DARK
		ts[nts++] = t - t0;
	}
	if (nts < 6) return 0;
	for (int i = 1; i < nts; i++) {
		double v = ts[i];
		int j = i;
		while (j > 0 && ts[j - 1] > v) ts[j] = ts[j - 1], j--;
		ts[j] = v;
	}
	return ts[nts / 5];
}

// lineStats samples b on the segment from a to c: the share of dark and
// the number of changes.
static double lineStats(const bitImage *b, point a, point c, int *changes) {
	double l = dist(a, c);
	int n = itrunc(l * 2) + 1;
	if (n < 1) n = 1;
	bool prev = false;
	int nd = 0, ch = 0;
	for (int i = 0; i <= n; i++) {
		double f = (double)i / n;
		bool v = at(b, ifloor(a.x + f * (c.x - a.x)), ifloor(a.y + f * (c.y - a.y)));
		if (v) nd++;
		if (i > 0 && v != prev) ch++;
		prev = v;
	}
	if (changes) *changes = ch;
	return (double)nd / (n + 1);
}

// dmModule is a finder or timing module: where, and whether it's dark.
typedef struct {
	int16_t x, y;
	bool dark;
} dmModule;

typedef VEC(dmModule) dmModules;

// dmFixed is the finder and timing modules of each data region of s, and
// the quiet zone's around the symbol.
static dmModules dmFixed(const dmSize *s) {
	dmModules out = {0};
	int rh = s->regRows + 2, rw = s->regCols + 2;
	for (int y = 0; y < s->rows; y++)
		for (int x = 0; x < s->cols; x++) {
			int ry = y % rh, rx = x % rw;
			dmModule m = {(int16_t)x, (int16_t)y, false};
			if (ry == rh - 1 || rx == 0) m.dark = true;
			else if (ry == 0) m.dark = rx % 2 == 0;
			else if (rx == rw - 1) m.dark = (rh - 1 - ry) % 2 == 0;
			else continue;
			VPUSH(out, m);
		}
	// And the quiet zone around: light.
	for (int x = -1; x <= s->cols; x++) {
		dmModule a = {(int16_t)x, -1, false}, b = {(int16_t)x, (int16_t)s->rows, false};
		VPUSH(out, a);
		VPUSH(out, b);
	}
	for (int y = 0; y < s->rows; y++) {
		dmModule a = {-1, (int16_t)y, false}, b = {(int16_t)s->cols, (int16_t)y, false};
		VPUSH(out, a);
		VPUSH(out, b);
	}
	return out;
}

static perspective dmTransform(const dmSize *s, const point q[4]) {
	double W = s->cols, H = s->rows;
	return quadToQuad(0, 0, W, 0, W, H, 0, H, q[0].x, q[0].y, q[1].x, q[1].y, q[2].x, q[2].y, q[3].x, q[3].y);
}

// dmCenters is how many fixed modules read right at their centers.
static int dmCenters(const bitImage *b, const dmSize *s, const dmModules *fixed, const point q[4]) {
	perspective t = dmTransform(s, q);
	int n = 0;
	for (size_t i = 0; i < fixed->n; i++) {
		double px, py;
		apply(&t, fixed->v[i].x + 0.5, fixed->v[i].y + 0.5, &px, &py);
		if (at(b, ifloor(px), ifloor(py)) == fixed->v[i].dark) n++;
	}
	return n;
}

// dmScore is what the search climbs: also near each center's corners,
// so the modules end up in the middle of where they read right.
static int dmScore(const bitImage *b, const dmSize *s, const dmModules *fixed, const point q[4]) {
	static const double os[5][3] = {{0, 0, 4}, {-0.3, -0.3, 1}, {0.3, -0.3, 1}, {-0.3, 0.3, 1}, {0.3, 0.3, 1}};
	perspective t = dmTransform(s, q);
	int n = 0;
	for (size_t i = 0; i < fixed->n; i++) {
		double fx = fixed->v[i].x + 0.5, fy = fixed->v[i].y + 0.5;
		for (int k = 0; k < 5; k++) {
			double px, py;
			apply(&t, fx + os[k][0], fy + os[k][1], &px, &py);
			if (at(b, ifloor(px), ifloor(py)) == fixed->v[i].dark) n += (int)os[k][2];
		}
	}
	return n;
}

// dmEdge is how well the outer timing pattern (the top row and right
// column alone) reads.
static int dmEdge(const bitImage *b, const dmSize *s, const point q[4]) {
	perspective t = dmTransform(s, q);
	double W = s->cols;
	int n = 0;
	for (int x = 0; x < s->cols; x++) {
		double px, py;
		apply(&t, x + 0.5, 0.5, &px, &py);
		if (at(b, ifloor(px), ifloor(py)) == (x % 2 == 0)) n++;
	}
	for (int y = 0; y < s->rows; y++) {
		double px, py;
		apply(&t, W - 0.5, y + 0.5, &px, &py);
		if (at(b, ifloor(px), ifloor(py)) == ((s->rows - 1 - y) % 2 == 0)) n++;
	}
	return n;
}

// dmFit places the fourth corner (and nudges the others) where the
// timing pattern of size s reads best: the transform from modules to the
// image, the corners, and how many finder modules read right.
static int dmFit(const bitImage *b, const lCorner *l, point p3, const dmSize *s, const dmModules *fixed, perspective *t, point quad[4]) {
	double W = s->cols, H = s->rows;
	point a = {(l->p1.x - l->p0.x) / W, (l->p1.y - l->p0.y) / W};
	point c = {(l->p2.x - l->p0.x) / H, (l->p2.y - l->p0.y) / H};
	point corners[4] = {l->p2, p3, l->p1, l->p0};  // top left, top right, bottom right, bottom left
	// The fourth corner first, where the outer timing pattern reads best:
	// within a fifth of the symbol (up to seven modules) of the guess,
	// half a module at a time.
	point bestQ[4];
	memcpy(bestQ, corners, sizeof bestQ);
	int bestE = dmEdge(b, s, corners);
	int r = MAX(4, MIN(14, itrunc(0.4 * fmax(W, H))));
	for (int i = -r; i <= r; i++)
		for (int j = -r; j <= r; j++) {
			point q[4];
			memcpy(q, corners, sizeof q);
			q[1] = (point){p3.x + 0.5 * (i * a.x + j * c.x), p3.y + 0.5 * (i * a.y + j * c.y)};
			int v = dmEdge(b, s, q);
			if (v > bestE) bestE = v, memcpy(bestQ, q, sizeof q);
		}
	int best = dmScore(b, s, fixed, bestQ);
	// Then each corner in turn, a quarter module at a time, then an
	// eighth.
	static const double steps[2] = {0.25, 0.125};
	static const double ds[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
	for (int si = 0; si < 2; si++)
		for (int pass = 0; pass < 8; pass++) {
			bool improved = false;
			for (int k = 0; k < 4; k++)
				for (int d = 0; d < 8; d++) {
					point q[4];
					memcpy(q, bestQ, sizeof q);
					q[k].x += steps[si] * (ds[d][0] * a.x + ds[d][1] * c.x);
					q[k].y += steps[si] * (ds[d][0] * a.y + ds[d][1] * c.y);
					int v = dmScore(b, s, fixed, q);
					if (v > best) {
						best = v;
						memcpy(bestQ, q, sizeof q);
						improved = true;
					}
				}
			if (!improved) break;
		}
	*t = dmTransform(s, bestQ);
	memcpy(quad, bestQ, sizeof bestQ);
	return dmCenters(b, s, fixed, bestQ);
}

typedef struct {
	const dmSize *s;
	double score;
} dmCand;

typedef struct {
	perspective t;
	point quad[4];
	int score;
} dmFitted;

// dmEdgeRead reads a timing edge from its own end, shifted a little
// either way: the ends are only so exact.
static int dmEdgeRead(const bitImage *b, point o, point along, point in, int n) {
	static const double shs[5] = {0, -0.25, 0.25, -0.5, 0.5};
	int best = 0;
	for (int si = 0; si < 5; si++) {
		int ok = 0;
		for (int k = 0; k < n; k++) {
			double f = k + 0.5 + shs[si];
			double x = o.x + f * along.x + 0.5 * in.x, y = o.y + f * along.y + 0.5 * in.y;
			if (at(b, ifloor(x), ifloor(y)) == (k % 2 == 0)) ok++;
		}
		best = MAX(best, ok);
	}
	return best;
}

static void sortCands(dmCand *cs, size_t n) {  // by score, highest first; stable
	for (size_t i = 1; i < n; i++) {
		dmCand c = cs[i];
		size_t j = i;
		while (j > 0 && cs[j - 1].score < c.score) cs[j] = cs[j - 1], j--;
		cs[j] = c;
	}
}

// dmRead tries the candidate finder l: the symbol's text and corners
// (NULL if it can't).
static char *dmRead(const bitImage *b, const lCorner *l, point quad[4]) {
	double lenA = dist(l->p0, l->p1), lenB = dist(l->p0, l->p2);
	point ua = {(l->p1.x - l->p0.x) / lenA, (l->p1.y - l->p0.y) / lenA};
	point ub = {(l->p2.x - l->p0.x) / lenB, (l->p2.y - l->p0.y) / lenB};
	double ma = legThickness(b, l->p0, l->p1, ub), mb = legThickness(b, l->p0, l->p2, ua);
	if (ma < 1 || mb < 1) return NULL;
	double m = (ma + mb) / 2;
	if (ma > 1.6 * mb || mb > 1.6 * ma) m = fmin(ma, mb);
	// The legs are solid half a module in.
#define IN(p, u, k) ((point){(p).x + (k) * (u).x, (p).y + (k) * (u).y})
	if (lineStats(b, IN(l->p0, ub, m / 2), IN(l->p1, ub, m / 2), NULL) < 0.85) return NULL;
	if (lineStats(b, IN(l->p0, ua, m / 2), IN(l->p2, ua, m / 2), NULL) < 0.85) return NULL;
#undef IN
	// The fourth corner: the parallelogram's, or the hull's if the symbol
	// is seen in perspective; the opposite edges alternate.
	point p3s[2];
	int np3 = 0, ct = 0, cr = 0;
	point guesses[2] = {{l->p1.x + l->p2.x - l->p0.x, l->p1.y + l->p2.y - l->p0.y}, l->p3};
	for (int g = 0; g < 2; g++) {
		point p3 = guesses[g];
		point ta = {l->p2.x - ub.x * m / 2, l->p2.y - ub.y * m / 2}, tc = {p3.x - ub.x * m / 2, p3.y - ub.y * m / 2};
		point ra = {l->p1.x - ua.x * m / 2, l->p1.y - ua.y * m / 2}, rc = {p3.x - ua.x * m / 2, p3.y - ua.y * m / 2};
		// Changes along the near half of each edge, where the guess of the
		// corner matters least.
		int t, r;
		lineStats(b, ta, (point){ta.x + 0.5 * (tc.x - ta.x), ta.y + 0.5 * (tc.y - ta.y)}, &t);
		lineStats(b, ra, (point){ra.x + 0.5 * (rc.x - ra.x), ra.y + 0.5 * (rc.y - ra.y)}, &r);
		t *= 2, r *= 2;
		if (dist(p3, l->p0) > 0.5 * (lenA + lenB)) {
			p3s[np3++] = p3;
			ct = MAX(ct, t), cr = MAX(cr, r);
		}
	}
	if (ct < 4 || cr < 4) return NULL;
	// Which sizes fit: their timing pattern read along the near part of
	// each timing edge, where the guess of the fourth corner matters
	// least.
	dmCand cands[nDmSizes + 3];
	size_t nc = 0;
	for (int i = 0; i < nDmSizes; i++) {
		const dmSize *s = &dmSizes[i];
		double w = s->cols, h = s->rows;
		double ratio = (lenA / lenB) / (w / h);
		if (ratio < 0.7 || ratio > 1.43) continue;
		double mw = lenA / w;
		if (mw < 1.2 || mw > 3 * m || mw < m / 3) continue;
		point ax = {(l->p1.x - l->p0.x) / w, (l->p1.y - l->p0.y) / w};
		point cy = {(l->p2.x - l->p0.x) / h, (l->p2.y - l->p0.y) / h};
		// A big symbol's ends are a module or so off: its near quarter
		// still reads.
		int nt = MIN(s->cols / 2 + 2, MAX(12, s->cols / 4 + 2));
		int nr = MIN(s->rows / 2 + 2, MAX(12, s->rows / 4 + 2));
		int ok = dmEdgeRead(b, l->p2, ax, (point){-cy.x, -cy.y}, nt) + dmEdgeRead(b, l->p1, cy, (point){-ax.x, -ax.y}, nr);
		double sc = (double)ok / (nt + nr);
		if (sc >= 0.7) cands[nc++] = (dmCand){s, sc};
	}
	sortCands(cands, nc);
	if (nc > 4) nc = 4;
	// And the sizes nearest what the legs' thickness and the changes
	// along the timing edges say, keeping the legs' ratio.
	double ests[4][2] = {
	    {lenA / m, lenB / m},
	    {(double)(ct + 1), (double)(cr + 1)},
	    {(double)(ct + 1), (double)(ct + 1) * lenB / lenA},
	    {(double)(cr + 1) * lenA / lenB, (double)(cr + 1)},
	};
	dmCand near[nDmSizes];
	size_t nn = 0;
	for (int i = 0; i < nDmSizes; i++) {
		const dmSize *s = &dmSizes[i];
		double w = s->cols, h = s->rows, d = INFINITY;
		for (int e = 0; e < 4; e++) d = fmin(d, fabs(w - ests[e][0]) / w + fabs(h - ests[e][1]) / h);
		bool have = false;
		for (size_t k = 0; k < nc; k++) have = have || cands[k].s == s;
		if (d <= 0.4 && !have) near[nn++] = (dmCand){s, -d};
	}
	sortCands(near, nn);
	for (size_t k = 0; k < nn && k < 3; k++) cands[nc++] = near[k];
	for (size_t ci = 0; ci < nc; ci++) {
		const dmSize *s = cands[ci].s;
		dmModules fixed = dmFixed(s);
		// Each guess of the fourth corner may settle elsewhere: the best
		// first, and each one that reads well enough.
		dmFitted fits[2];
		for (int i = 0; i < np3; i++) fits[i].score = dmFit(b, l, p3s[i], s, &fixed, &fits[i].t, fits[i].quad);
		if (np3 == 2 && fits[1].score > fits[0].score) {
			dmFitted t = fits[0];
			fits[0] = fits[1];
			fits[1] = t;
		}
		for (int i = 0; i < np3; i++) {
			if (fits[i].score < 0.8 * (double)fixed.n || (i == 1 && dist(fits[i].quad[1], fits[0].quad[1]) < 0.5)) continue;
			grid g;
			if (!sampleGrid(b, &fits[i].t, s->cols, s->rows, &g)) continue;
			char *text = dmDecodeGrid(&g, s);
			free(g.bits);
			if (text) {
				memcpy(quad, fits[i].quad, 4 * sizeof *quad);
				VFREE(fixed);
				return text;
			}
		}
		VFREE(fixed);
	}
	return NULL;
}

static bool insideAny(const point (*qs)[4], size_t n, point p) {
	for (size_t k = 0; k < n; k++) {
		const point *q = qs[k];
		for (int wind = 0; wind < 2; wind++) {  // either winding
			bool in = true;
			for (int i = 0; i < 4 && in; i++) {
				point a = q[i], b = q[(i + 1) % 4];
				double c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
				in = wind == 0 ? !(c > 0) : !(c < 0);
			}
			if (in) return true;
		}
	}
	return false;
}

// decodeDataMatrixCodes finds and reads the Data Matrix symbols in b.
static void decodeDataMatrixCodes(const bitImage *b, codes *out) {
	regions r = {0};
	label(&r, b);
	typedef point quad4[4];
	VEC(quad4) found = {0};
	lcorners ls = {0};
	for (size_t si = 0; si < r.spans.n; si += 2) {
		const run *comp = r.grouped.v + r.spans.v[si];
		size_t ncomp = (size_t)(r.spans.v[si + 1] - r.spans.v[si]);
		int32_t x0 = comp[0].x0, y0 = comp[0].y, x1 = comp[0].x1, y1 = comp[ncomp - 1].y + 1;
		for (size_t k = 0; k < ncomp; k++) x0 = MIN(x0, comp[k].x0), x1 = MAX(x1, comp[k].x1);
		if (MAX(x1 - x0, y1 - y0) < 12) continue;
		point center = {(x0 + x1) / 2.0, (y0 + y1) / 2.0};
		if (insideAny((const point(*)[4])found.v, found.n, center)) continue;
		size_t nh = convexHull(&r, comp, ncomp);
		// r.hull is reused by lCorners' sides only; copy the hull.
		point *hull = malloc(nh * sizeof *hull);
		memcpy(hull, r.hull.v, nh * sizeof *hull);
		lCorners(&r, hull, nh, &ls);
		for (size_t k = 0; k < ls.n; k++) {
			point quad[4];
			char *text = dmRead(b, &ls.v[k], quad);
			if (text) {
				if (found.n == found.cap) {
					found.cap = found.cap ? found.cap * 2 : 4;
					found.v = realloc(found.v, found.cap * sizeof *found.v);
				}
				memcpy(found.v[found.n++], quad, sizeof quad);
				code c = {ASUKA_BARCODE_DATAMATRIX, text, 0, 0, 0, 0};
				boundsOf(quad, 4, &c.x0, &c.y0, &c.x1, &c.y1);
				VPUSH(*out, c);
				break;
			}
		}
		free(hull);
	}
	VFREE(ls);
	VFREE(found);
	VFREE(r.runs);
	free(r.rowStart);
	free(r.count);
	VFREE(r.grouped);
	VFREE(r.spans);
	VFREE(r.pts);
	VFREE(r.hull);
	VFREE(r.sides);
}

// ---- Decoding ----

// overlaps reports whether a and b share at least half the smaller's
// area.
static bool overlaps(const code *a, const code *b) {
	int ix0 = MAX(a->x0, b->x0), iy0 = MAX(a->y0, b->y0), ix1 = MIN(a->x1, b->x1), iy1 = MIN(a->y1, b->y1);
	if (ix0 >= ix1 || iy0 >= iy1) return false;
	long aa = (long)(a->x1 - a->x0) * (a->y1 - a->y0), ab = (long)(b->x1 - b->x0) * (b->y1 - b->y0);
	long m = MIN(aa, ab);
	return 2L * (ix1 - ix0) * (iy1 - iy0) >= m;
}

static bool rect_empty(const code *c) { return c->x0 >= c->x1 || c->y0 >= c->y1; }

size_t asuka_barcode_decode(const uint8_t *gray, int width, int height, int stride, unsigned want, asuka_barcode **out) {
	*out = NULL;
	want &= ASUKA_BARCODE_ALL;
	if (!want || width <= 0 || height <= 0 || !gray) return 0;
	luminance lum = {width, height, malloc((size_t)width * height)};
	for (int y = 0; y < height; y++) memcpy(lum.pix + (size_t)y * width, gray + (size_t)y * stride, (size_t)width);
	codes cs = {0};
	if (want & (ASUKA_BARCODE_QR | ASUKA_BARCODE_DATAMATRIX)) {
		bitImage bin = binarize(&lum);
		if (want & ASUKA_BARCODE_QR) decodeQRCodes(&bin, &cs);
		if (want & ASUKA_BARCODE_DATAMATRIX) decodeDataMatrixCodes(&bin, &cs);
		free(bin.pix);
	}
	if (want & K1D) decode1D(&lum, want, &cs);
	free(lum.pix);
	// dedupe drops a code found again in the same place.
	size_t n = 0;
	for (size_t i = 0; i < cs.n; i++) {
		code c = cs.v[i];
		bool dup = false;
		for (size_t k = 0; k < n && !dup; k++) {
			code *o = &cs.v[k];
			if (o->kind == c.kind && !strcmp(o->text, c.text) && overlaps(o, &c)) {
				// The union of the two (Go's: an empty one is the other).
				if (rect_empty(o)) o->x0 = c.x0, o->y0 = c.y0, o->x1 = c.x1, o->y1 = c.y1;
				else if (!rect_empty(&c)) o->x0 = MIN(o->x0, c.x0), o->y0 = MIN(o->y0, c.y0), o->x1 = MAX(o->x1, c.x1), o->y1 = MAX(o->y1, c.y1);
				dup = true;
			}
		}
		if (dup) free(c.text);
		else cs.v[n++] = c;
	}
	cs.n = n;
	// Top to bottom, then left to right (stable).
	for (size_t i = 1; i < cs.n; i++) {
		code c = cs.v[i];
		size_t j = i;
		while (j > 0 && (cs.v[j - 1].y0 > c.y0 || (cs.v[j - 1].y0 == c.y0 && cs.v[j - 1].x0 > c.x0))) cs.v[j] = cs.v[j - 1], j--;
		cs.v[j] = c;
	}
	if (!cs.n) {
		codes_free(&cs);
		return 0;
	}
	asuka_barcode *r = malloc(cs.n * sizeof *r);
	for (size_t i = 0; i < cs.n; i++) r[i] = (asuka_barcode){cs.v[i].kind, cs.v[i].text, cs.v[i].x0, cs.v[i].y0, cs.v[i].x1, cs.v[i].y1};
	n = cs.n;
	VFREE(cs);
	*out = r;
	return n;
}

void asuka_barcode_free(asuka_barcode *codes_, size_t n) {
	for (size_t i = 0; i < n; i++) free(codes_[i].text);
	free(codes_);
}
