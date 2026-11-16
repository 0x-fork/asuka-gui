// asuka/svg on Windows: a child window of the package's whose WM_PAINT
// fills and strokes the layers as GDI+ paths, antialiased, fitted to it
// (its pixels: points times the window's DPI); currentColor is the
// view's color, resolved for the theme shown. A Shape's layers are
// resolved in the window's size each time it paints.
#include "asuka_windows.h"
#include "svg.h"

#include <math.h>
#include <stdlib.h>
#include <uxtheme.h>

// GDI+'s flat API (gdiplus.dll), as much of it as the package draws with.
typedef int GpStatus;
typedef struct {
	UINT32 version;
	void *debug_event;
	BOOL suppress_background_thread, suppress_external_codecs;
} gp_startup_input;
typedef struct {
	float x, y;
} gp_point;
GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const gp_startup_input *input, void *output);
GpStatus WINAPI GdipCreateFromHDC(HDC dc, void **g);
GpStatus WINAPI GdipDeleteGraphics(void *g);
GpStatus WINAPI GdipSetSmoothingMode(void *g, int mode);
GpStatus WINAPI GdipSetPixelOffsetMode(void *g, int mode);
GpStatus WINAPI GdipCreatePath(int fill_mode, void **path);
GpStatus WINAPI GdipDeletePath(void *path);
GpStatus WINAPI GdipStartPathFigure(void *path);
GpStatus WINAPI GdipClosePathFigure(void *path);
GpStatus WINAPI GdipAddPathLine(void *path, float x1, float y1, float x2, float y2);
GpStatus WINAPI GdipAddPathBezier(void *path, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4);
GpStatus WINAPI GdipAddPathRectangle(void *path, float x, float y, float w, float h);
GpStatus WINAPI GdipAddPathEllipse(void *path, float x, float y, float w, float h);
GpStatus WINAPI GdipAddPathArc(void *path, float x, float y, float w, float h, float start, float sweep);
GpStatus WINAPI GdipCreateSolidFill(DWORD argb, void **brush);
GpStatus WINAPI GdipCreateLineBrush(const gp_point *from, const gp_point *to, DWORD c1, DWORD c2, int wrap, void **brush);
GpStatus WINAPI GdipDeleteBrush(void *brush);
GpStatus WINAPI GdipFillPath(void *g, void *brush, void *path);
GpStatus WINAPI GdipCreatePen1(DWORD argb, float width, int unit, void **pen);
GpStatus WINAPI GdipSetPenLineCap197819(void *pen, int start, int end, int dash);
GpStatus WINAPI GdipSetPenLineJoin(void *pen, int join);
GpStatus WINAPI GdipDeletePen(void *pen);
GpStatus WINAPI GdipDrawPath(void *g, void *pen, void *path);

enum { FILL_WINDING = 1, SMOOTH_ANTIALIAS = 4, PIXEL_OFFSET_HALF = 4, UNIT_WORLD = 0, WRAP_TILE_FLIP_XY = 3 };

static bool gdiplus(void) {
	static bool tried, ok;
	if (!tried) {
		tried = true;
		ULONG_PTR token;
		gp_startup_input in = {.version = 1};
		ok = GdiplusStartup(&token, &in, NULL) == 0;
	}
	return ok;
}

static UINT dpi_of(HWND h) {
	static UINT(WINAPI * for_window)(HWND);
	static bool tried;
	if (!tried) {
		tried = true;
		*(FARPROC *)&for_window = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	}
	UINT d = for_window ? for_window(h) : 0;
	return d ? d : 96;
}

// argb is a paint as GDI+'s color, currentColor cur (0xRRGGBBAA): false
// when it paints nothing.
static bool argb(svg_paint p, uint32_t cur, DWORD *out) {
	switch (p.kind) {
	case 1:
		*out = (DWORD)p.a << 24 | (DWORD)p.r << 16 | (DWORD)p.g << 8 | p.b;
		return p.a > 0;
	case 2: {
		float a = (float)(cur & 255) * p.opacity;
		*out = (DWORD)(a + 0.5f) << 24 | (DWORD)(cur >> 24) << 16 | (DWORD)(cur >> 16 & 255) << 8 | (cur >> 8 & 255);
		return a >= 0.5f;
	}
	}
	return false;
}

// GDI+'s caps and joins, by ours (butt, round, square; miter, round,
// bevel).
static const int gp_cap[3] = {0, 2, 1}, gp_join[3] = {0, 2, 1};

// paint fills and strokes d's layers into dc, scaled by scale and moved
// by dx, dy; currentColor is cur.
static void paint(HDC dc, const svg_drawing *d, float scale, float dx, float dy, uint32_t cur) {
	void *g = NULL;
	if (!gdiplus() || GdipCreateFromHDC(dc, &g) != 0) return;
	GdipSetSmoothingMode(g, SMOOTH_ANTIALIAS);
	GdipSetPixelOffsetMode(g, PIXEL_OFFSET_HALF);
#define X(v) ((v) * scale + dx)
#define Y(v) ((v) * scale + dy)
	for (size_t li = 0; li < d->n; li++) {
		const svg_layer *l = &d->layers[li];
		void *p = NULL;
		if (GdipCreatePath(FILL_WINDING, &p) != 0) continue;
		float px = 0, py = 0, sx = 0, sy = 0;  // the pen, and its figure's start
		for (size_t i = 0; i < l->n; i++) {
			const float *a = l->path[i].a;
			switch (l->path[i].op) {
			case 1:
				GdipStartPathFigure(p);
				px = sx = X(a[0]), py = sy = Y(a[1]);
				break;
			case 2:
				GdipAddPathLine(p, px, py, X(a[0]), Y(a[1]));
				px = X(a[0]), py = Y(a[1]);
				break;
			case 3: {  // a quadratic curve as a cubic one
				float cx = X(a[0]), cy = Y(a[1]), x = X(a[2]), y = Y(a[3]);
				GdipAddPathBezier(p, px, py, px + (cx - px) * 2 / 3, py + (cy - py) * 2 / 3, x + (cx - x) * 2 / 3, y + (cy - y) * 2 / 3, x, y);
				px = x, py = y;
				break;
			}
			case 4:
				GdipAddPathBezier(p, px, py, X(a[0]), Y(a[1]), X(a[2]), Y(a[3]), X(a[4]), Y(a[5]));
				px = X(a[4]), py = Y(a[5]);
				break;
			case 5:
				GdipClosePathFigure(p);
				px = sx, py = sy;
				break;
			case 6:
				GdipAddPathRectangle(p, X(a[0]), Y(a[1]), a[2] * scale, a[3] * scale);
				break;
			case 7: {
				float x = X(a[0]), y = Y(a[1]), w = a[2] * scale, h = a[3] * scale, r = fminf(a[4] * scale, fminf(w / 2, h / 2));
				if (r <= 0) {
					GdipAddPathRectangle(p, x, y, w, h);
					break;
				}
				GdipStartPathFigure(p);
				GdipAddPathArc(p, x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
				GdipAddPathArc(p, x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
				GdipAddPathArc(p, x, y + h - 2 * r, 2 * r, 2 * r, 90, 90);
				GdipAddPathArc(p, x, y, 2 * r, 2 * r, 180, 90);
				GdipClosePathFigure(p);
				break;
			}
			case 8:
				GdipAddPathEllipse(p, X(a[0]), Y(a[1]), a[2] * scale, a[3] * scale);
				break;
			}
		}
		DWORD c, to;
		if (argb(l->fill, cur, &c)) {
			void *b = NULL;
			if (l->fade.kind) {
				if (!argb(l->fade, cur, &to)) to = c & 0x00FFFFFF;
				gp_point from = {0, dy}, end = {0, dy + l->fade_to * scale};
				GdipCreateLineBrush(&from, &end, c, to, WRAP_TILE_FLIP_XY, &b);
			} else {
				GdipCreateSolidFill(c, &b);
			}
			if (b) {
				GdipFillPath(g, b, p);
				GdipDeleteBrush(b);
			}
		}
		if (argb(l->stroke, cur, &c) && l->width > 0) {
			void *pen = NULL;
			if (GdipCreatePen1(c, l->width * scale, UNIT_WORLD, &pen) == 0) {
				GdipSetPenLineCap197819(pen, gp_cap[l->cap < 3 ? l->cap : 0], gp_cap[l->cap < 3 ? l->cap : 0], 0);
				GdipSetPenLineJoin(pen, gp_join[l->join < 3 ? l->join : 0]);
				GdipDrawPath(g, pen, p);
				GdipDeletePen(pen);
			}
		}
		GdipDeletePath(p);
	}
#undef X
#undef Y
	GdipDeleteGraphics(g);
}

typedef struct {
	HWND hwnd;
	bool shape;
	svg_drawing icon;
	int64_t color;
	svg_shape layers;
} drawn;

static void on_paint(drawn *v, HDC dc) {
	RECT r;
	GetClientRect(v->hwnd, &r);
	DrawThemeParentBackground(v->hwnd, dc, &r);
	float w = (float)r.right, h = (float)r.bottom;
	if (v->shape) {
		svg_drawing d = svg_shape_drawing(&v->layers, w, h, (float)dpi_of(v->hwnd) / 96);
		paint(dc, &d, 1, 0, 0, 0);
		svg_drawing_free(&d);
		return;
	}
	float scale, dx, dy;
	if (svg_fit(&v->icon, w, h, &scale, &dx, &dy)) paint(dc, &v->icon, scale, dx, dy, svg_rgba(v->color));
}

static bool window(void *ctx, HWND hwnd, UINT m, WPARAM wp, LPARAM lp, LRESULT *result) {
	drawn *v = ctx;
	switch (m) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		on_paint(v, dc);
		EndPaint(hwnd, &ps);
		*result = 0;
		return true;
	}
	case WM_ERASEBKGND:
		*result = 1;
		return true;
	case WM_SIZE:
		if (v->shape) InvalidateRect(hwnd, NULL, FALSE);
		break;
	case WM_NCDESTROY:
		svg_drawing_free(&v->icon);
		svg_shape_free(&v->layers);
		free(v);
		break;
	}
	return false;
}

static drawn *make(void *parent, bool shape) {
	drawn *v = calloc(1, sizeof *v);
	v->shape = shape;
	v->color = (int64_t)1 << 32 | 1;
	v->hwnd = asuka_win_window(0, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, (HWND)parent, window, v);
	if (!v->hwnd) {
		free(v);
		return NULL;
	}
	return v;
}

// ---- View ----

typedef struct {
	svg_View base;
	drawn *d;
} view;

static void view_icon(svg_View *self, const svg_Icon *ic) {
	view *v = (view *)self;
	svg_drawing_free(&v->d->icon);
	v->d->icon = svg_decode(ic->data.data, ic->data.len);
	InvalidateRect(v->d->hwnd, NULL, FALSE);
}

static void view_color(svg_View *self, int64_t c) {
	view *v = (view *)self;
	v->d->color = c;
	InvalidateRect(v->d->hwnd, NULL, FALSE);
}

// view_measure is the icon's size, in points, when its style gives none.
static bool view_measure(svg_View *self, float w, float h, float *ow, float *oh) {
	view *v = (view *)self;
	*ow = v->d->icon.width;
	*oh = v->d->icon.height;
	return true;
}

// The window (the host's) frees what it draws as it goes.
static void view_destroy(svg_View *self) { free(self); }

static const svg_View_vtable view_vt = {.destroy = view_destroy, .set_icon = view_icon, .set_color = view_color, .measure = view_measure};

svg_View *svg_make_View(svg_View_events events) {
	drawn *d = make(events.parent, false);
	if (!d) return NULL;
	view *v = calloc(1, sizeof *v);
	v->base.vt = &view_vt;
	v->base.widget = d->hwnd;
	v->d = d;
	return &v->base;
}

// ---- Shape ----

typedef struct {
	svg_Shape base;
	drawn *d;
} shape;

static void shape_layers(svg_Shape *self, svg_list_Layer ls) {
	shape *s = (shape *)self;
	svg_shape_free(&s->d->layers);
	s->d->layers = svg_shape_copy(ls);
	InvalidateRect(s->d->hwnd, NULL, FALSE);
}

// shape_measure: a Shape has no size of its own.
static bool shape_measure(svg_Shape *self, float w, float h, float *ow, float *oh) {
	*ow = 0;
	*oh = 0;
	return true;
}

static void shape_destroy(svg_Shape *self) { free(self); }

static const svg_Shape_vtable shape_vt = {.destroy = shape_destroy, .set_layers = shape_layers, .measure = shape_measure};

svg_Shape *svg_make_Shape(svg_Shape_events events) {
	drawn *d = make(events.parent, true);
	if (!d) return NULL;
	shape *s = calloc(1, sizeof *s);
	s->base.vt = &shape_vt;
	s->base.widget = d->hwnd;
	s->d = d;
	return &s->base;
}
