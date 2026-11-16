// shape.c: a Shape is a view of the host's that draws its outline with
// GDI+, antialiased: filled, stroked (caps, joins, dashes), trimmed
// (host/windows' shape.go). Its path's format is the Canvas's too
// (canvas.c builds its paths here).
#include "host.h"
#include "parts.h"
#include "draw.h"

struct wh_shape {
	uint8_t *path;
	uint32_t path_len;
	ak_color fill, stroke;
	float line_width;
	int32_t cap, join;
	float trim_start, trim_end;
	float *dash;
	uint32_t ndash;
	float dash_phase;
};

void wh_shape_new(node *n) {
	n->shape = wh_alloc(sizeof *n->shape);
	n->shape->line_width = 1;
	n->shape->trim_end = 1;
}

void wh_shape_free(node *n) {
	if (!n->shape) return;
	free(n->shape->path);
	free(n->shape->dash);
	free(n->shape);
	n->shape = NULL;
}

void wh_shape_set_prop(node *n, uint16_t p, const ak_value *v) {
	wh_shape *s = n->shape;
	if (!s) return;
	switch (p) {
	case AK_PROP_SHAPE_PATH:
		free(s->path);
		s->path = v->s.len ? wh_alloc(v->s.len) : NULL;
		if (s->path) memcpy(s->path, v->s.p, v->s.len);
		s->path_len = (uint32_t)v->s.len;
		break;
	case AK_PROP_SHAPE_FILL: s->fill = (ak_color)v->i; break;
	case AK_PROP_SHAPE_STROKE: s->stroke = (ak_color)v->i; break;
	case AK_PROP_SHAPE_LINE_WIDTH: s->line_width = (float)v->f; break;
	case AK_PROP_SHAPE_LINE_CAP: s->cap = (int32_t)v->i; break;
	case AK_PROP_SHAPE_LINE_JOIN: s->join = (int32_t)v->i; break;
	case AK_PROP_SHAPE_TRIM_START: s->trim_start = (float)v->f; break;
	case AK_PROP_SHAPE_TRIM_END: s->trim_end = (float)v->f; break;
	case AK_PROP_SHAPE_DASH: {
		free(s->dash);
		s->dash = NULL, s->ndash = 0;
		const uint8_t *b = (const uint8_t *)v->s.p;
		if (v->s.len >= 8) {
			s->dash_phase = wh_f32(b);
			s->ndash = (uint32_t)(v->s.len - 4) / 4;
			s->dash = wh_alloc(s->ndash * sizeof *s->dash);
			for (uint32_t i = 0; i < s->ndash; i++) s->dash[i] = wh_f32(b + 4 + 4 * i);
		}
		break;
	}
	}
	wh_redraw(n);
}

// wh_build_path makes a GDI+ path from Shape's path format (u8 PathOp,
// then its f32 arguments), in pixels at k.
GpPath *wh_build_path(const uint8_t *b, size_t n, float k) {
	GpPath *p = g_path();
	if (!p) return NULL;
	size_t i = 0;
#define F() (i + 4 <= n ? (i += 4, wh_f32(b + i - 4)) : (i = n + 1, 0.0f))
	float cx = 0, cy = 0;  // the current point
	while (i < n) {
		uint8_t op = b[i++];
		switch (op) {
		case AK_PATH_OP_MOVE: {
			GdipStartPathFigure(p);
			float x = F() * k;
			float y = F() * k;
			cx = x, cy = y;
			break;
		}
		case AK_PATH_OP_LINE: {
			float x = F() * k;
			float y = F() * k;
			GdipAddPathLine(p, cx, cy, x, y);
			cx = x, cy = y;
			break;
		}
		case AK_PATH_OP_QUAD: {
			float qx = F() * k;
			float qy = F() * k;
			float x = F() * k;
			float y = F() * k;
			// A quadratic is the cubic with its control points 2/3 of the
			// way to its own.
			GdipAddPathBezier(p, cx, cy, cx + 2 * (qx - cx) / 3, cy + 2 * (qy - cy) / 3, x + 2 * (qx - x) / 3, y + 2 * (qy - y) / 3, x, y);
			cx = x, cy = y;
			break;
		}
		case AK_PATH_OP_CUBIC: {
			float x1 = F() * k;
			float y1 = F() * k;
			float x2 = F() * k;
			float y2 = F() * k;
			float x = F() * k;
			float y = F() * k;
			GdipAddPathBezier(p, cx, cy, x1, y1, x2, y2, x, y);
			cx = x, cy = y;
			break;
		}
		case AK_PATH_OP_CLOSE:
			GdipClosePathFigure(p);
			break;
		case AK_PATH_OP_RECT: {
			float x = F() * k;
			float y = F() * k;
			float w = F() * k;
			float h = F() * k;
			GdipStartPathFigure(p);
			GdipAddPathRectangle(p, x, y, w, h);
			break;
		}
		case AK_PATH_OP_ROUNDED_RECT: {
			float x = F() * k;
			float y = F() * k;
			float w = F() * k;
			float h = F() * k;
			float r = F() * k;
			g_path_round_rect(p, x, y, w, h, r);
			break;
		}
		case AK_PATH_OP_ELLIPSE: {
			float x = F() * k;
			float y = F() * k;
			float w = F() * k;
			float h = F() * k;
			GdipStartPathFigure(p);
			GdipAddPathEllipse(p, x, y, w, h);
			break;
		}
		case AK_PATH_OP_ARC: {
			float ax = F() * k;
			float ay = F() * k;
			float r = F() * k;
			float start = F();
			float end = F();
			float cw = F();
			float sweep = end - start;
			if (cw != 0)
				while (sweep < 0) sweep += 2 * (float)M_PI;
			else
				while (sweep > 0) sweep -= 2 * (float)M_PI;
			float deg = (float)(180 / M_PI);
			GdipAddPathArc(p, ax - r, ay - r, 2 * r, 2 * r, start * deg, sweep * deg);
			cx = ax + r * cosf(end), cy = ay + r * sinf(end);
			break;
		}
		default:
			return p;
		}
	}
#undef F
	return p;
}

static float dist(const GpPointF *a, const GpPointF *b) { return hypotf(b->X - a->X, b->Y - a->Y); }

// trimmed is the part of a path from from to to (0 to 1 of its length),
// as a polyline of its flattened curves (p is flattened in place).
static GpPath *trimmed(GpPath *p, float from, float to) {
	GpPath *out = g_path();
	GdipFlattenPath(p, NULL, 0.25f);
	INT n = 0;
	GdipGetPointCount(p, &n);
	if (n < 2) return out;
	GpPointF *raw = wh_alloc((size_t)n * sizeof *raw);
	BYTE *types = wh_alloc((size_t)n);
	GdipGetPathPoints(p, raw, n);
	GdipGetPathTypes(p, types, n);
	// The points, each marked where a figure starts; a closed figure ends
	// with its first point again.
	GpPointF *pts = wh_alloc((size_t)n * 2 * sizeof *pts);
	bool *starts = wh_alloc((size_t)n * 2);
	int np = 0, first = 0;
	for (int i = 0; i < n; i++) {
		bool start = (types[i] & 7) == 0;
		if (start) first = np;
		pts[np] = raw[i], starts[np++] = start;
		if ((types[i] & 0x80) && first < np) pts[np] = pts[first], starts[np++] = false;
	}
	float total = 0;
	for (int i = 1; i < np; i++)
		if (!starts[i]) total += dist(&pts[i - 1], &pts[i]);
	float a = from * total, b = to * total, at = 0;
	for (int i = 1; i < np; i++) {
		if (starts[i]) {
			GdipStartPathFigure(out);
			continue;
		}
		float d = dist(&pts[i - 1], &pts[i]);
		float s0 = at, s1 = at + d;
		at = s1;
		if (s1 <= a || s0 >= b || d == 0) continue;
		float t0 = wh_maxf((a - s0) / d, 0), t1 = wh_minf((b - s0) / d, 1);
		GpPointF p0 = pts[i - 1], p1 = pts[i];
		GdipAddPathLine(out, p0.X + (p1.X - p0.X) * t0, p0.Y + (p1.Y - p0.Y) * t0, p0.X + (p1.X - p0.X) * t1, p0.Y + (p1.Y - p0.Y) * t1);
	}
	free(raw);
	free(types);
	free(pts);
	free(starts);
	return out;
}

// wh_style_pen gives a pen its caps (butt, round, square: GDI+'s flat,
// round, square), its join (miter, round, bevel) and its dashes (in
// points; GDI+'s are in multiples of the pen's width).
void wh_style_pen(GpPen *pen, int cap, int join, const float *dash, uint32_t ndash, float phase, float width) {
	static const GpLineCap caps[3] = {LineCapFlat, LineCapRound, LineCapSquare};
	static const GpLineJoin joins[3] = {LineJoinMiter, LineJoinRound, LineJoinBevel};
	cap = cap < 0 ? 0 : cap > 2 ? 2 : cap;
	join = join < 0 ? 0 : join > 2 ? 2 : join;
	GdipSetPenStartCap(pen, caps[cap]);
	GdipSetPenEndCap(pen, caps[cap]);
	GdipSetPenLineJoin(pen, joins[join]);
	if (!ndash) return;
	float w = wh_maxf(width, 0.01f);
	float *d = wh_alloc(ndash * sizeof *d);
	for (uint32_t i = 0; i < ndash; i++) d[i] = wh_maxf(dash[i] / w, 0.01f);
	GdipSetPenDashArray(pen, d, (INT)ndash);
	GdipSetPenDashOffset(pen, phase / w);
	free(d);
}

void wh_paint_shape(node *n, gfx g) {
	wh_shape *s = n->shape;
	if (!s || !s->path_len) return;
	float k = wh_scale_of(n);
	GpPath *p = wh_build_path(s->path, s->path_len, k);
	if (!p) return;
	rgba c;
	if (wh_resolve(s->fill, &c) && c.a > 0) {
		GpSolidFill *b = NULL;
		GdipCreateSolidFill(rgba_argb(c), &b);
		GdipFillPath(g, (GpBrush *)b, p);
		GdipDeleteBrush((GpBrush *)b);
	}
	if (!wh_resolve(s->stroke, &c) || c.a == 0 || s->line_width <= 0) {
		GdipDeletePath(p);
		return;
	}
	if (s->trim_start > 0 || s->trim_end < 1) {
		if (s->trim_end <= s->trim_start) {
			GdipDeletePath(p);
			return;
		}
		GpPath *t = trimmed(p, s->trim_start, s->trim_end);
		GdipDeletePath(p);
		p = t;
	}
	GpPen *pen = NULL;
	GdipCreatePen1(rgba_argb(c), s->line_width * k, UnitPixel, &pen);
	if (pen) {
		wh_style_pen(pen, s->cap, s->join, s->dash, s->ndash, s->dash_phase, s->line_width);
		GdipDrawPath(g, pen, p);
		GdipDeletePen(pen);
	}
	GdipDeletePath(p);
}
