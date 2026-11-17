// platform/imageedit on Windows and Linux: the platform's codecs decode
// (imageedit_windows.c, imageedit_linux.c), the actions are this file's,
// on premultiplied RGBA, and the result is written to the app's caches
// as JPEG or PNG. Each call's work is a worker's (asuka_desktop_spawn);
// macOS runs the package's Swift.
#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include "imageedit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "asuka_windows.h"
#include <shlobj.h>
#define SEP "\\"
#else
#include <errno.h>
#include <sys/random.h>
#include <sys/stat.h>
#define SEP "/"
#endif

void ie_no_image(asuka_error *err) { asuka_error_declared(err, imageedit_EditError_noImage); }

static bool remote(const char *s) { return !strncmp(s, "http://", 7) || !strncmp(s, "https://", 8); }

// local is the source's file: an asset's, a path's, a file: URL's, an
// http(s) URL's download. NULL with err set: noImage when there's no file.
static char *local(const char *source, asuka_error *err) {
	asuka_error e = {0};
	char *p = asuka_desktop_local_file(source, "", &e);
	if (p) return p;
	if (remote(source)) {
		*err = e;
		return NULL;
	}
	asuka_error_free(&e);
	ie_no_image(err);
	return NULL;
}

static ie_image make(int w, int h) { return (ie_image){w, h, calloc((size_t)w * h, 4)}; }

static uint8_t *at(const ie_image *img, int x, int y) { return img->px + ((size_t)y * img->w + x) * 4; }

// A rectangle within the image, and whether any of it is.
typedef struct {
	int x0, y0, x1, y1;
} rect;

static bool intersect(const imageedit_Step *s, const ie_image *img, rect *r) {
	if (s->width <= 0 || s->height <= 0) return false;
	int64_t x0 = s->x > 0 ? s->x : 0, y0 = s->y > 0 ? s->y : 0;
	int64_t x1 = s->x + s->width < img->w ? s->x + s->width : img->w;
	int64_t y1 = s->y + s->height < img->h ? s->y + s->height : img->h;
	if (x0 >= x1 || y0 >= y1) return false;
	*r = (rect){(int)x0, (int)y0, (int)x1, (int)y1};
	return true;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

// catmull_rom is the resampling kernel (support 2).
static double catmull_rom(double x) {
	x = fabs(x);
	if (x < 1) return 1.5 * x * x * x - 2.5 * x * x + 1;
	if (x < 2) return -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2;
	return 0;
}

// Each output position's source pixels and weights, the kernel stretched
// as the image shrinks (so every source pixel counts).
typedef struct {
	int *start, *count;
	float *w;  // count[i] weights from start[i], at i*taps
	int taps;
} weights;

static weights make_weights(int src, int dst) {
	double scale = (double)src / dst, s = scale > 1 ? scale : 1;
	double support = 2 * s;
	weights ws = {malloc(sizeof(int) * dst), malloc(sizeof(int) * dst), NULL, (int)ceil(support) * 2 + 2};
	ws.w = calloc((size_t)dst * ws.taps, sizeof(float));
	for (int i = 0; i < dst; i++) {
		double center = (i + 0.5) * scale - 0.5;
		int lo = imax((int)ceil(center - support), 0), hi = imin((int)floor(center + support), src - 1);
		if (hi - lo + 1 > ws.taps) hi = lo + ws.taps - 1;
		double sum = 0;
		float *w = ws.w + (size_t)i * ws.taps;
		for (int j = lo; j <= hi; j++) {
			double wt = catmull_rom((j - center) / s);
			w[j - lo] = (float)wt;
			sum += wt;
		}
		if (sum != 0)
			for (int k = 0; k <= hi - lo; k++) w[k] /= (float)sum;
		ws.start[i] = lo;
		ws.count[i] = hi >= lo ? hi - lo + 1 : 0;
	}
	return ws;
}

static void free_weights(weights *w) { free(w->start), free(w->count), free(w->w); }

static uint8_t clamp_byte(float f) { return f <= 0 ? 0 : f >= 255 ? 255 : (uint8_t)(f + 0.5f); }

// resize resamples img to w×h, rows then columns.
static ie_image resize(const ie_image *img, int w, int h) {
	int sw = img->w, sh = img->h;
	weights xw = make_weights(sw, w), yw = make_weights(sh, h);
	float *tmp = malloc(sizeof(float) * 4 * (size_t)w * sh);
	for (int y = 0; y < sh; y++) {
		for (int x = 0; x < w; x++) {
			float acc[4] = {0};
			const float *wt = xw.w + (size_t)x * xw.taps;
			for (int k = 0; k < xw.count[x]; k++) {
				const uint8_t *p = at(img, xw.start[x] + k, y);
				for (int c = 0; c < 4; c++) acc[c] += wt[k] * p[c];
			}
			memcpy(tmp + ((size_t)y * w + x) * 4, acc, sizeof acc);
		}
	}
	ie_image out = make(w, h);
	for (int y = 0; y < h; y++) {
		const float *wt = yw.w + (size_t)y * yw.taps;
		for (int x = 0; x < w; x++) {
			float acc[4] = {0};
			for (int k = 0; k < yw.count[y]; k++) {
				const float *p = tmp + ((size_t)(yw.start[y] + k) * w + x) * 4;
				for (int c = 0; c < 4; c++) acc[c] += wt[k] * p[c];
			}
			uint8_t a = clamp_byte(acc[3]), *o = at(&out, x, y);
			// Premultiplied: no channel above its alpha.
			for (int c = 0; c < 3; c++) {
				uint8_t v = clamp_byte(acc[c]);
				o[c] = v < a ? v : a;
			}
			o[3] = a;
		}
	}
	free(tmp);
	free_weights(&xw), free_weights(&yw);
	return out;
}

// rotate turns img clockwise by degrees: exactly by quarters, else
// sampled bilinearly into the turned image's bounds, its corners clear.
static ie_image rotate(const ie_image *img, double degrees, bool *same) {
	double deg = fmod(degrees, 360);
	if (deg < 0) deg += 360;
	*same = deg == 0;
	if (*same) return *img;
	int w = img->w, h = img->h;
	if (fmod(deg, 90) == 0) {
		int q = (int)(deg / 90);
		ie_image out = q % 2 ? make(h, w) : make(w, h);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				int nx = 0, ny = 0;
				if (q == 1) nx = h - 1 - y, ny = x;
				else if (q == 2) nx = w - 1 - x, ny = h - 1 - y;
				else nx = y, ny = w - 1 - x;
				memcpy(at(&out, nx, ny), at(img, x, y), 4);
			}
		return out;
	}
	double t = deg * 3.14159265358979323846 / 180, cs = cos(t), sn = sin(t);
	int ow = (int)ceil(w * fabs(cs) + h * fabs(sn)), oh = (int)ceil(w * fabs(sn) + h * fabs(cs));
	ie_image out = make(ow, oh);
	double cx = w / 2.0, cy = h / 2.0, ocx = ow / 2.0, ocy = oh / 2.0;
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			// The output pixel's center, turned back into the source.
			double dx = x + 0.5 - ocx, dy = y + 0.5 - ocy;
			double sx = dx * cs + dy * sn + cx - 0.5, sy = -dx * sn + dy * cs + cy - 0.5;
			int x0 = (int)floor(sx), y0 = (int)floor(sy);
			double fx = sx - x0, fy = sy - y0;
			if (x0 < -1 || y0 < -1 || x0 >= w || y0 >= h) continue;
			const int xs[4] = {x0, x0 + 1, x0, x0 + 1}, ys[4] = {y0, y0, y0 + 1, y0 + 1};
			const double ws[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
			double v[4] = {0};
			for (int k = 0; k < 4; k++) {
				if (xs[k] < 0 || ys[k] < 0 || xs[k] >= w || ys[k] >= h) continue;
				const uint8_t *p = at(img, xs[k], ys[k]);
				for (int c = 0; c < 4; c++) v[c] += p[c] * ws[k];
			}
			uint8_t *o = at(&out, x, y);
			for (int c = 0; c < 4; c++) o[c] = (uint8_t)lround(v[c]);
		}
	return out;
}

// pixelate makes squares of block pixels, aligned to the image, each the
// average of all it covers; only those within r change.
static void pixelate(ie_image *img, rect r, int block) {
	for (int by = r.y0 / block * block; by < r.y1; by += block) {
		int by1 = imin(by + block, img->h);
		for (int bx = r.x0 / block * block; bx < r.x1; bx += block) {
			int bx1 = imin(bx + block, img->w);
			uint64_t sum[4] = {0};
			for (int y = by; y < by1; y++)
				for (int x = bx; x < bx1; x++)
					for (int c = 0; c < 4; c++) sum[c] += at(img, x, y)[c];
			uint64_t n = (uint64_t)(by1 - by) * (bx1 - bx);
			for (int y = imax(by, r.y0); y < imin(by1, r.y1); y++)
				for (int x = imax(bx, r.x0); x < imin(bx1, r.x1); x++)
					for (int c = 0; c < 4; c++) at(img, x, y)[c] = (uint8_t)(sum[c] / n);
		}
	}
}

// apply does the steps on *img in order.
static bool apply(imageedit_list_Step steps, ie_image *img, asuka_error *err) {
	for (size_t i = 0; i < steps.len; i++) {
		const imageedit_Step *s = &steps.data[i];
		int w = img->w, h = img->h;
		ie_image out = {0};
		rect r;
		switch (s->op) {
		case imageedit_Op_resize: {
			int64_t tw = s->width, th = s->height;
			if (tw <= 0 && th <= 0) continue;
			if (tw <= 0) tw = imax(1, (int)llround((double)w * th / h));
			if (th <= 0) th = imax(1, (int)llround((double)h * tw / w));
			if (tw == w && th == h) continue;
			if (tw * th > 400000000) {
				asuka_error_failed(err, "imageedit", 0, "can't make a %lld×%lld image", (long long)tw, (long long)th);
				return false;
			}
			out = resize(img, (int)tw, (int)th);
			break;
		}
		case imageedit_Op_crop:
			if (!intersect(s, img, &r)) {
				asuka_error_failed(err, "imageedit", 0, "the crop is outside the %d×%d image", w, h);
				return false;
			}
			out = make(r.x1 - r.x0, r.y1 - r.y0);
			for (int y = 0; y < out.h; y++) memcpy(at(&out, 0, y), at(img, r.x0, r.y0 + y), (size_t)out.w * 4);
			break;
		case imageedit_Op_rotate: {
			bool same;
			out = rotate(img, s->degrees, &same);
			if (same) continue;
			break;
		}
		case imageedit_Op_flipHorizontal:
			out = make(w, h);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) memcpy(at(&out, x, y), at(img, w - 1 - x, y), 4);
			break;
		case imageedit_Op_flipVertical:
			out = make(w, h);
			for (int y = 0; y < h; y++) memcpy(at(&out, 0, y), at(img, 0, h - 1 - y), (size_t)w * 4);
			break;
		case imageedit_Op_fill:
			if (intersect(s, img, &r)) {
				uint8_t c[4] = {(uint8_t)(s->rgb >> 16), (uint8_t)(s->rgb >> 8), (uint8_t)s->rgb, 255};
				for (int y = r.y0; y < r.y1; y++)
					for (int x = r.x0; x < r.x1; x++) memcpy(at(img, x, y), c, 4);
			}
			continue;
		case imageedit_Op_pixelate:
			if (intersect(s, img, &r)) pixelate(img, r, s->block > 1 ? (int)s->block : 1);
			continue;
		default:
			asuka_error_failed(err, "imageedit", 0, "unknown action %lld", (long long)s->op);
			return false;
		}
		free(img->px);
		*img = out;
	}
	return true;
}

static bool mkdirs(const char *dir) {
#ifdef _WIN32
	wchar_t *w = asuka_wide(dir);
	int r = SHCreateDirectoryExW(NULL, w, NULL);
	free(w);
	return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
#else
	char *p = asuka_strdup(dir);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') {
			*s = 0;
			mkdir(p, 0755);
			*s = '/';
		}
	bool ok = mkdir(p, 0755) == 0 || errno == EEXIST;
	free(p);
	return ok;
#endif
}

static void random_name(char out[25]) {
	uint8_t b[12];
#ifdef _WIN32
	for (int i = 0; i < 12; i += 4) {
		unsigned v = 0;
		rand_s(&v);
		memcpy(b + i, &v, 4);
	}
#else
	if (getrandom(b, sizeof b, 0) != (ssize_t)sizeof b)
		for (int i = 0; i < 12; i++) b[i] = (uint8_t)rand();
#endif
	for (int i = 0; i < 12; i++) snprintf(out + i * 2, 3, "%02x", b[i]);
}

// write writes img to the caches: PNG, or JPEG on white (it has no
// transparency).
static bool write_image(ie_image *img, imageedit_Format format, double quality, imageedit_Result *res, asuka_error *err) {
	bool png = format == imageedit_Format_png;
	if (!png) {
		size_t n = (size_t)img->w * img->h * 4;
		for (size_t i = 0; i < n; i += 4) {
			unsigned a = 255 - img->px[i + 3];
			for (int c = 0; c < 3; c++) img->px[i + c] = (uint8_t)(img->px[i + c] + a);
			img->px[i + 3] = 255;
		}
	}
	asuka_bytes b = {0};
	if (!ie_encode(img, png, quality, &b, err)) return false;
	char *dir = asuka_sprintf("%s" SEP "asuka-imageedit", asuka_desktop_cache_dir());
	char name[25];
	random_name(name);
	char *path = asuka_sprintf("%s" SEP "%s%s", dir, name, png ? ".png" : ".jpg");
	bool ok = mkdirs(dir);
	if (ok) {
#ifdef _WIN32
		wchar_t *w = asuka_wide(path);
		FILE *f = _wfopen(w, L"wb");
		free(w);
#else
		FILE *f = fopen(path, "wb");
#endif
		ok = f && fwrite(b.data, 1, b.len, f) == b.len;
		if (f && fclose(f) != 0) ok = false;
	}
	free(b.data);
	free(dir);
	if (!ok) {
		asuka_error_failed(err, "imageedit", 0, "can't write %s", path);
		free(path);
		return false;
	}
	*res = (imageedit_Result){path, img->w, img->h};
	return true;
}

typedef struct {
	asuka_call *call;
	const char *source;
	imageedit_list_Step steps;
	imageedit_Format format;
	double quality;
	int64_t at;  // ns: a video's frame; -1 an image's; -2 its size
} job;

static void run(void *p) {
	job *j = p;
	asuka_error err = {0};
	imageedit_Result res = {0};
	ie_image img = {0};
	bool ok = false;
	char *path = NULL;
	if (j->at == -2) {
		int w = 0, h = 0;
		if ((path = local(j->source, &err)) && ie_size(path, &w, &h, &err)) {
			res = (imageedit_Result){asuka_strdup(""), w, h};
			ok = true;
		}
	} else {
		if (j->at == -1) ok = (path = local(j->source, &err)) && ie_decode(path, &img, &err);
		else if (remote(j->source)) ok = ie_video_frame(j->source, false, j->at / 1e9, &img, &err);
		else ok = (path = local(j->source, &err)) && ie_video_frame(path, true, j->at / 1e9, &img, &err);
		ok = ok && apply(j->steps, &img, &err) && write_image(&img, j->format, j->quality, &res, &err);
	}
	free(img.px);
	free(path);
	asuka_call *call = j->call;
	int64_t kind = j->at;
	free(j);
	if (!ok) asuka_call_fail(call, &err);
	else if (kind == -2) imageedit_Editor_size_done(call, res);
	else if (kind == -1) imageedit_Editor_edit_done(call, res);
	else imageedit_Editor_videoFrame_done(call, res);
}

static void start(job *j) {
	if (asuka_desktop_spawn(run, j)) return;
	asuka_error e = {0};
	asuka_error_failed(&e, "imageedit", 0, "no thread");
	asuka_call_fail(j->call, &e);
	free(j);
}

static void e_edit(imageedit_Editor *self, const char *source, imageedit_list_Step steps, imageedit_Format format, double quality,
                   asuka_call *call) {
	job *j = malloc(sizeof *j);
	*j = (job){call, source, steps, format, quality, -1};
	start(j);
}

static void e_size(imageedit_Editor *self, const char *source, asuka_call *call) {
	job *j = malloc(sizeof *j);
	*j = (job){call, source, {0}, imageedit_Format_jpeg, 0.9, -2};
	start(j);
}

static void e_video_frame(imageedit_Editor *self, const char *source, int64_t at, imageedit_list_Step steps, imageedit_Format format,
                          double quality, asuka_call *call) {
	job *j = malloc(sizeof *j);
	*j = (job){call, source, steps, format, quality, at > 0 ? at : 0};
	start(j);
}

static void e_release(imageedit_Editor *self) {}

static const imageedit_Editor_vtable vt = {.release = e_release, .edit = e_edit, .size = e_size, .videoFrame = e_video_frame};

imageedit_Editor *imageedit_make_Editor(asuka_error *err) {
	static imageedit_Editor editor = {&vt};
	return &editor;
}
