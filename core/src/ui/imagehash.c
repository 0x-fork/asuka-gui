// imagehash.c: the compact placeholders apps send ahead of an image,
// BlurHash (blurha.sh) and ThumbHash (evanw.github.io/thumbhash), decoded
// into small pictures the hosts show until the image loads, following the
// reference implementations (blurhash's TypeScript, thumbhash's
// JavaScript) step for step, so a hash decodes here to the pixels it
// decodes to there (core/test/testdata/imagehash: their outputs).
#include "base/base.h"

#include <math.h>

#define PI 3.14159265358979323846

static const char digits83[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#$%*+,-.:;=?@[]^_{|}~";

static bool decode83(const char *s, size_t n, int *out) {
	int v = 0;
	for (size_t i = 0; i < n; i++) {
		const char *d = s[i] ? strchr(digits83, s[i]) : NULL;
		if (!d) return false;
		v = v * 83 + (int)(d - digits83);
	}
	*out = v;
	return true;
}

static double srgb_to_linear(int v) {
	double f = (double)v / 255;
	if (f <= 0.04045) return f / 12.92;
	return pow((f + 0.055) / 1.055, 2.4);
}

static uint8_t linear_to_srgb(double v) {
	double f = fmax(0, fmin(1, v));
	if (f <= 0.0031308) return (uint8_t)trunc(f * 12.92 * 255 + 0.5);
	return (uint8_t)trunc((1.055 * pow(f, 1 / 2.4) - 0.055) * 255 + 0.5);
}

static double sign_pow(double v, double e) { return (v < 0 ? -1.0 : 1.0) * pow(fabs(v), e); }

// ak_blurhash_size reports a BlurHash's components across and down, if
// it's well formed.
bool ak_blurhash_size(const char *hash, size_t n, int *nx, int *ny);

bool ak_blurhash_size(const char *hash, size_t n, int *nx, int *ny) {
	int flag;
	if (n < 6 || !decode83(hash, 1, &flag)) return false;
	*ny = flag / 9 + 1;
	*nx = flag % 9 + 1;
	return n == (size_t)(4 + 2 * *nx * *ny);
}

// ak_blurhash decodes hash into w×h pixels at out: RGBA, 8 bits each,
// opaque. punch above 1 makes its colors stronger.
bool ak_blurhash(const char *hash, size_t n, int w, int h, double punch, uint8_t *out);

bool ak_blurhash(const char *hash, size_t n, int w, int h, double punch, uint8_t *out) {
	int nx, ny, q;
	if (!ak_blurhash_size(hash, n, &nx, &ny) || w <= 0 || h <= 0) return false;
	if (punch < 1) punch = 1;
	if (!decode83(hash + 1, 1, &q)) return false;
	double maxv = (double)(q + 1) / 166;
	double (*colors)[3] = ak_alloc((size_t)nx * ny * sizeof *colors);
	bool ok = true;
	for (int i = 0; i < nx * ny && ok; i++) {
		int v = 0;
		if (i == 0) {
			if (!(ok = decode83(hash + 2, 4, &v))) break;
			colors[0][0] = srgb_to_linear(v >> 16);
			colors[0][1] = srgb_to_linear(v >> 8 & 255);
			colors[0][2] = srgb_to_linear(v & 255);
			continue;
		}
		ok = decode83(hash + 4 + i * 2, 2, &v);
		double m = maxv * punch;
		colors[i][0] = sign_pow((double)(v / 361 - 9) / 9, 2) * m;
		colors[i][1] = sign_pow((double)(v / 19 % 19 - 9) / 9, 2) * m;
		colors[i][2] = sign_pow((double)(v % 19 - 9) / 9, 2) * m;
	}
	if (!ok) {
		ak_free(colors);
		return false;
	}
	// The cosines along each axis, computed once (the reference computes
	// them per pixel: the same products).
	double *cx = ak_alloc((size_t)w * nx * sizeof(double)), *cy = ak_alloc((size_t)h * ny * sizeof(double));
	for (int x = 0; x < w; x++)
		for (int i = 0; i < nx; i++) cx[x * nx + i] = cos(PI * (double)x * (double)i / (double)w);
	for (int y = 0; y < h; y++)
		for (int j = 0; j < ny; j++) cy[y * ny + j] = cos(PI * (double)y * (double)j / (double)h);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			double r = 0, g = 0, b = 0;
			for (int j = 0; j < ny; j++) {
				for (int i = 0; i < nx; i++) {
					double basis = cx[x * nx + i] * cy[y * ny + j];
					const double *c = colors[i + j * nx];
					r += c[0] * basis;
					g += c[1] * basis;
					b += c[2] * basis;
				}
			}
			uint8_t *o = out + (size_t)(y * w + x) * 4;
			o[0] = linear_to_srgb(r), o[1] = linear_to_srgb(g), o[2] = linear_to_srgb(b), o[3] = 255;
		}
	}
	ak_free(cx);
	ak_free(cy);
	ak_free(colors);
	return true;
}

// ak_thumbhash_parse reads a ThumbHash as apps keep it: base64 (standard
// or URL-safe, padded or not), into *out (ak_alloc'd).
bool ak_thumbhash_parse(const char *s, size_t n, uint8_t **out, size_t *len);

static int b64(char c, bool url) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == (url ? '-' : '+')) return 62;
	if (c == (url ? '_' : '/')) return 63;
	return -1;
}

static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

bool ak_thumbhash_parse(const char *s, size_t n, uint8_t **out, size_t *len) {
	while (n && space(*s)) s++, n--;
	while (n && space(s[n - 1])) n--;
	while (n && s[n - 1] == '=') n--;
	bool url = false;
	for (size_t i = 0; i < n; i++) url |= s[i] == '-' || s[i] == '_';
	if (n % 4 == 1) return false;
	uint8_t *b = ak_alloc(n * 3 / 4 + 3);
	size_t m = 0;
	uint32_t acc = 0;
	int bits = 0;
	for (size_t i = 0; i < n; i++) {
		int v = b64(s[i], url);
		if (v < 0) {
			ak_free(b);
			return false;
		}
		acc = acc << 6 | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			b[m++] = (uint8_t)(acc >> bits);
		}
	}
	// Raw (unpadded) base64 requires the leftover bits to be zero.
	if (bits && (acc & ((1u << bits) - 1))) {
		ak_free(b);
		return false;
	}
	if (m < 5) {
		ak_free(b);
		return false;
	}
	*out = b;
	*len = m;
	return true;
}

static double thumbhash_ratio(const uint8_t *hash, size_t n) {
	if (n < 5) return 1;
	int header = hash[3];
	bool alpha = (hash[2] & 0x80) != 0, landscape = (hash[4] & 0x80) != 0;
	int lx, ly;
	if (landscape && alpha) lx = 5, ly = header & 7;
	else if (landscape) lx = 7, ly = header & 7;
	else if (alpha) lx = header & 7, ly = 5;
	else lx = header & 7, ly = 7;
	if (ly == 0) return 1;
	return (double)lx / (double)ly;
}

typedef struct {
	const uint8_t *hash;
	size_t n;
	int start, index;
} th_reader;

static int at(const th_reader *r, int i) { return i >= 0 && (size_t)i < r->n ? r->hash[i] : 0; }

static double *channel(th_reader *r, int nx, int ny, double scale, int *count) {
	int cap = nx * ny;
	double *ac = ak_alloc((size_t)(cap ? cap : 1) * sizeof(double));
	int k = 0;
	for (int cy = 0; cy < ny; cy++) {
		for (int cx = cy == 0 ? 1 : 0; cx * ny < nx * (ny - cy); cx++) {
			int v = at(r, r->start + (r->index >> 1)) >> ((r->index & 1) << 2) & 15;
			r->index++;
			ac[k++] = ((double)v / 7.5 - 1) * scale;
		}
	}
	*count = k;
	return ac;
}

static uint8_t clamp255(double v) { return (uint8_t)fmax(0, 255 * fmin(1, v)); }

static int imax(int a, int b) { return a > b ? a : b; }

// ak_thumbhash decodes hash into its picture: at most 32 pixels on its
// longer side, RGBA (ak_alloc'd), 8 bits each, not premultiplied.
bool ak_thumbhash(const uint8_t *hash, size_t n, int *w, int *h, uint8_t **rgba);

bool ak_thumbhash(const uint8_t *hash, size_t n, int *ow, int *oh, uint8_t **rgba) {
	if (n < 5) return false;
	th_reader r = {hash, n, 0, 0};
	int header24 = at(&r, 0) | at(&r, 1) << 8 | at(&r, 2) << 16;
	int header16 = at(&r, 3) | at(&r, 4) << 8;
	double l_dc = (double)(header24 & 63) / 63;
	double p_dc = (double)(header24 >> 6 & 63) / 31.5 - 1;
	double q_dc = (double)(header24 >> 12 & 63) / 31.5 - 1;
	double l_scale = (double)(header24 >> 18 & 31) / 31;
	bool has_alpha = header24 >> 23 != 0;
	double p_scale = (double)(header16 >> 3 & 63) / 63;
	double q_scale = (double)(header16 >> 9 & 63) / 63;
	bool landscape = header16 >> 15 != 0;
	int lx = header16 & 7, ly = has_alpha ? 5 : 7;
	if (landscape) {
		lx = has_alpha ? 5 : 7;
		ly = header16 & 7;
	}
	lx = imax(3, lx), ly = imax(3, ly);
	double a_dc = 1.0, a_scale = (double)(at(&r, 5) >> 4) / 15;
	if (has_alpha) a_dc = (double)(at(&r, 5) & 15) / 15;
	r.start = has_alpha ? 6 : 5;
	int nl, np, nq, na = 0;
	double *l_ac = channel(&r, lx, ly, l_scale, &nl);
	double *p_ac = channel(&r, 3, 3, p_scale * 1.25, &np);
	double *q_ac = channel(&r, 3, 3, q_scale * 1.25, &nq);
	double *a_ac = has_alpha ? channel(&r, 5, 5, a_scale, &na) : NULL;
	double ratio = thumbhash_ratio(hash, n);
	int w, h;
	if (ratio > 1) w = 32, h = (int)round(32 / ratio);
	else w = (int)round(32 * ratio), h = 32;
	bool ok = w > 0 && h > 0;
	if (ok) {
		int nfx = imax(lx, 3), nfy = imax(ly, 3);
		if (has_alpha) nfx = imax(lx, 5), nfy = imax(ly, 5);
		double *fx = ak_alloc((size_t)nfx * sizeof(double)), *fy = ak_alloc((size_t)nfy * sizeof(double));
		uint8_t *px = ak_alloc((size_t)w * h * 4);
		for (int y = 0, i = 0; y < h; y++) {
			for (int x = 0; x < w; x++, i += 4) {
				double l = l_dc, p = p_dc, q = q_dc, a = a_dc;
				for (int cx = 0; cx < nfx; cx++) fx[cx] = cos(PI / (double)w * ((double)x + 0.5) * (double)cx);
				for (int cy = 0; cy < nfy; cy++) fy[cy] = cos(PI / (double)h * ((double)y + 0.5) * (double)cy);
				for (int cy = 0, j = 0; cy < ly; cy++) {
					double fy2 = fy[cy] * 2;
					for (int cx = cy == 0 ? 1 : 0; cx * ly < lx * (ly - cy); cx++, j++) l += l_ac[j] * fx[cx] * fy2;
				}
				for (int cy = 0, j = 0; cy < 3; cy++) {
					double fy2 = fy[cy] * 2;
					for (int cx = cy == 0 ? 1 : 0; cx < 3 - cy; cx++, j++) {
						double f = fx[cx] * fy2;
						p += p_ac[j] * f;
						q += q_ac[j] * f;
					}
				}
				if (has_alpha) {
					for (int cy = 0, j = 0; cy < 5; cy++) {
						double fy2 = fy[cy] * 2;
						for (int cx = cy == 0 ? 1 : 0; cx < 5 - cy; cx++, j++) a += a_ac[j] * fx[cx] * fy2;
					}
				}
				double b = l - 2.0 / 3 * p;
				double rr = (3 * l - b + q) / 2;
				double g = rr - q;
				px[i] = clamp255(rr), px[i + 1] = clamp255(g), px[i + 2] = clamp255(b), px[i + 3] = clamp255(a);
			}
		}
		ak_free(fx);
		ak_free(fy);
		*ow = w, *oh = h, *rgba = px;
	}
	AK_UNUSED(nl), AK_UNUSED(np), AK_UNUSED(nq), AK_UNUSED(na);
	ak_free(l_ac);
	ak_free(p_ac);
	ak_free(q_ac);
	ak_free(a_ac);
	return ok;
}
