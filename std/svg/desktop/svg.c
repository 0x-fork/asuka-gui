// svg.c: icons' data decoded, Shapes' layers resolved (svg.h), for
// svg_linux.c and svg_windows.c.
#include "svg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const uint8_t *d;
	size_t n, at;
} rd;

static uint8_t u8(rd *r) { return r->at < r->n ? r->d[r->at++] : 0; }

static uint32_t u32(rd *r) {
	if (r->at + 4 > r->n) {
		r->at = r->n;
		return 0;
	}
	const uint8_t *p = r->d + r->at;
	r->at += 4;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float f32(rd *r) {
	uint32_t u = u32(r);
	float f;
	memcpy(&f, &u, 4);
	return f;
}

static svg_paint paint(rd *r) {
	switch (u8(r)) {
	case 1: {
		svg_paint p = {.kind = 1};
		p.r = u8(r), p.g = u8(r), p.b = u8(r), p.a = u8(r);
		return p;
	}
	case 2:
		return (svg_paint){.kind = 2, .opacity = f32(r)};
	}
	return (svg_paint){0};
}

static int nargs(uint8_t op) {
	static const int n[] = {-1, 2, 2, 4, 6, 0, 4, 5, 4};
	return op < sizeof n / sizeof *n ? n[op] : -1;
}

static void add_seg(svg_layer *l, size_t *cap, svg_seg s) {
	if (l->n == *cap) {
		*cap = *cap ? *cap * 2 : 8;
		l->path = realloc(l->path, *cap * sizeof *l->path);
	}
	l->path[l->n++] = s;
}

static void add_layer(svg_drawing *d, size_t *cap, svg_layer l) {
	if (d->n == *cap) {
		*cap = *cap ? *cap * 2 : 4;
		d->layers = realloc(d->layers, *cap * sizeof *d->layers);
	}
	d->layers[d->n++] = l;
}

svg_drawing svg_decode(const uint8_t *data, size_t n) {
	svg_drawing out = {0};
	if (n < 24) return out;
	rd r = {data, n, 0};
	for (int i = 0; i < 4; i++) out.view_box[i] = f32(&r);
	out.width = f32(&r), out.height = f32(&r);
	uint32_t count = u32(&r);
	size_t cap = 0;
	for (uint32_t i = 0; i < count && r.at < r.n; i++) {
		svg_layer l = {0};
		l.fill = paint(&r), l.stroke = paint(&r);
		l.width = f32(&r);
		l.cap = u8(&r), l.join = u8(&r);
		uint32_t len = u32(&r);
		size_t end = r.at + len > r.n ? r.n : r.at + len, scap = 0;
		while (r.at < end) {
			svg_seg s = {.op = u8(&r)};
			int k = nargs(s.op);
			if (k < 0) break;
			for (int j = 0; j < k; j++) s.a[j < 6 ? j : 5] = f32(&r);  // a rounded rect's radius is its fifth
			add_seg(&l, &scap, s);
		}
		r.at = end;
		add_layer(&out, &cap, l);
	}
	return out;
}

void svg_drawing_free(svg_drawing *d) {
	for (size_t i = 0; i < d->n; i++) free(d->layers[i].path);
	free(d->layers);
	*d = (svg_drawing){0};
}

bool svg_fit(const svg_drawing *d, float w, float h, float *scale, float *dx, float *dy) {
	float vx = d->view_box[0], vy = d->view_box[1], vw = d->view_box[2], vh = d->view_box[3];
	if (vw <= 0 || vh <= 0) return false;
	float s = fminf(w / vw, h / vh);
	*scale = s;
	*dx = (w - vw * s) / 2 - vx * s;
	*dy = (h - vh * s) / 2 - vy * s;
	return s > 0;
}

svg_shape svg_shape_copy(svg_list_Layer ls) {
	svg_shape out = {.n = ls.len};
	out.layers = calloc(ls.len ? ls.len : 1, sizeof *out.layers);
	for (size_t i = 0; i < ls.len; i++) {
		const svg_Layer *l = &ls.data[i];
		svg_shape_layer *o = &out.layers[i];
		o->n = l->path.data.len;
		o->path = malloc((o->n ? o->n : 1) * sizeof *o->path);
		if (o->n) memcpy(o->path, l->path.data.data, o->n * sizeof *o->path);
		if ((o->has_fill = l->fill)) o->fill = *l->fill;
		if ((o->has_stroke = l->stroke)) o->stroke = *l->stroke;
		if ((o->has_fade = l->fade)) o->fade = *l->fade;
		o->width = (float)l->width;
		o->cap = (uint8_t)(l->cap < 0 ? 0 : l->cap > 2 ? 2 : l->cap);
		o->join = (uint8_t)(l->join < 0 ? 0 : l->join > 2 ? 2 : l->join);
	}
	return out;
}

void svg_shape_free(svg_shape *s) {
	for (size_t i = 0; i < s->n; i++) free(s->layers[i].path);
	free(s->layers);
	*s = (svg_shape){0};
}

uint32_t svg_rgba(int64_t color) { return asuka_desktop_color(color ? (uint64_t)color : ((uint64_t)1 << 32 | 1)); }

static svg_paint paint_of(bool has, int64_t c) {
	if (!has) return (svg_paint){0};
	uint32_t v = svg_rgba(c);
	return (svg_paint){.kind = 1, .r = (uint8_t)(v >> 24), .g = (uint8_t)(v >> 16), .b = (uint8_t)(v >> 8), .a = (uint8_t)v};
}

svg_drawing svg_shape_drawing(const svg_shape *sh, float w, float h, float s) {
	svg_drawing d = {.view_box = {0, 0, w, h}, .width = w, .height = h};
	size_t cap = 0;
	for (size_t li = 0; li < sh->n; li++) {
		const svg_shape_layer *l = &sh->layers[li];
		svg_layer out = {.fill = paint_of(l->has_fill, l->fill), .stroke = paint_of(l->has_stroke, l->stroke), .width = l->width * s,
		                 .cap = l->cap, .join = l->join};
		if (out.fill.kind && l->has_fade) out.fade = paint_of(true, l->fade), out.fade_to = h;
		const double *data = l->path;
		size_t n = l->n, i = 0, scap = 0;
		// pt reads a point: a fraction of the box plus points.
#define PT(X, Y)                                                                          \
	do {                                                                                  \
		if (i + 4 > n) {                                                                  \
			i = n;                                                                        \
			X = Y = 0;                                                                    \
		} else {                                                                          \
			X = (float)data[i] * w + (float)data[i + 1] * s;                              \
			Y = (float)data[i + 2] * h + (float)data[i + 3] * s;                          \
			i += 4;                                                                       \
		}                                                                                 \
	} while (0)
		bool done = false;
		while (i < n && !done) {
			int op = (int)data[i++];
			float ax, ay, bx, by, x, y;
			switch (op) {
			case 1:
			case 2:
				PT(x, y);
				add_seg(&out, &scap, (svg_seg){.op = (uint8_t)op, .a = {x, y}});
				break;
			case 3:
				PT(ax, ay);
				PT(x, y);
				add_seg(&out, &scap, (svg_seg){.op = 3, .a = {ax, ay, x, y}});
				break;
			case 4:
				PT(ax, ay);
				PT(bx, by);
				PT(x, y);
				add_seg(&out, &scap, (svg_seg){.op = 4, .a = {ax, ay, bx, by, x, y}});
				break;
			case 5:
				add_seg(&out, &scap, (svg_seg){.op = 5});
				break;
			case 6:
			case 7: {
				PT(ax, ay);
				PT(bx, by);
				float rx = fminf(ax, bx), ry = fminf(ay, by), rw = fabsf(bx - ax), rh = fabsf(by - ay);
				if (op == 7) {
					add_seg(&out, &scap, (svg_seg){.op = 8, .a = {rx, ry, rw, rh}});
					break;
				}
				float r = i < n ? (float)data[i] * s : 0;
				i++;
				r = fmaxf(0, fminf(r, fminf(rw / 2, rh / 2)));
				add_seg(&out, &scap, (svg_seg){.op = 7, .a = {rx, ry, rw, rh, r}});
				break;
			}
			default:
				done = true;
			}
		}
#undef PT
		add_layer(&d, &cap, out);
	}
	return d;
}
