// shape.c: Shape views.
//
// A Shape is drawn by its view with GSK: its outline a GskPath, filled
// (nonzero, as Core Graphics fills) and stroked with a GskStroke (caps,
// joins, dashes). The path, the stroke and the trimmed outline are made
// as the props change, so a frame only appends them. Trim takes the part
// of the outline between two of its lengths (a GskPathMeasure's points),
// and moves inside an Animate on the view's frame clock, with the
// animation's curve, as a CAShapeLayer's strokeStart and strokeEnd do.
//
// lx_path_svg turns Shape's path format (Canvas's paths too) into SVG
// path data, which gsk_path_parse reads.
#include "host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	GskPath *path;           // the outline (NULL: none)
	GskPathMeasure *measure;  // of path, made as it's first trimmed
	GskPath *drawn;          // the trimmed outline the stroke draws (path itself, untrimmed)
	GskStroke *stroke;
	ak_color fill, color;
	float line_width;
	int cap, join;
	AK_VEC(float) dash;
	float dash_phase;
	float trim[2], target[2];  // shown and set: start and end, 0 to 1
	// The trim's animation: on the view's frame clock.
	guint tick;
	animation anim;
	gint64 start;
	float from[2], to[2];
} shape_view;

static shape_view *shape_of(node *n) { return n->kind == AK_KIND_SHAPE ? n->ext : NULL; }

// ---- SVG path data ----

static float arg(ak_reader *r) {
	float v = ak_read_f32(r);
	return v != v || v > 1e9f || v < -1e9f ? 0 : v;
}

static void cmd(ak_buf *b, char c, int n, const float *v) {
	if (b->len) ak_buf_u8(b, ' ');
	ak_buf_u8(b, (uint8_t)c);
	for (int i = 0; i < n; i++) {
		char num[32];
		int k = snprintf(num, sizeof num, " %.9g", (double)v[i]);
		ak_buf_put(b, num, (size_t)k);
	}
}

#define CMD(b, c, ...) cmd(b, c, (int)(sizeof((float[]){__VA_ARGS__}) / sizeof(float)), (float[]){__VA_ARGS__})

static void svg_arc(ak_buf *b, float rx, float ry, bool sweep, float x, float y) { CMD(b, 'A', rx, ry, 0, 0, sweep ? 1 : 0, x, y); }

static void box(ak_buf *b, float x, float y, float w, float h) {
	CMD(b, 'M', x, y);
	CMD(b, 'L', x + w, y);
	CMD(b, 'L', x + w, y + h);
	CMD(b, 'L', x, y + h);
	cmd(b, 'Z', 0, NULL);
}

// lx_path_svg appends the path (u8 PathOp, then its f32 arguments) as SVG
// path data. Arcs go as SVG arcs of a quarter turn at most; a rectangle,
// rounded or not, and an ellipse are closed figures of their own,
// clockwise on screen from their top left (an ellipse from its right), as
// Core Graphics adds them.
void lx_path_svg(const uint8_t *p, size_t len, ak_buf *b) {
	ak_reader r = {p, len, 0, false};
	bool current = false;
	while (r.off < len && !r.err) {
		switch (ak_read_u8(&r)) {
		case AK_PATH_OP_MOVE: {
			float x = arg(&r), y = arg(&r);
			CMD(b, 'M', x, y);
			current = true;
			break;
		}
		case AK_PATH_OP_LINE: {
			float x = arg(&r), y = arg(&r);
			if (current)
				CMD(b, 'L', x, y);
			else
				CMD(b, 'M', x, y);
			current = true;
			break;
		}
		case AK_PATH_OP_QUAD: {
			float cx = arg(&r), cy = arg(&r), x = arg(&r), y = arg(&r);
			if (!current) CMD(b, 'M', cx, cy);
			CMD(b, 'Q', cx, cy, x, y);
			current = true;
			break;
		}
		case AK_PATH_OP_CUBIC: {
			float x1 = arg(&r), y1 = arg(&r), x2 = arg(&r), y2 = arg(&r), x = arg(&r), y = arg(&r);
			if (!current) CMD(b, 'M', x1, y1);
			CMD(b, 'C', x1, y1, x2, y2, x, y);
			current = true;
			break;
		}
		case AK_PATH_OP_CLOSE:
			if (current) cmd(b, 'Z', 0, NULL);
			break;
		case AK_PATH_OP_RECT: {
			float x = arg(&r), y = arg(&r), w = arg(&r), h = arg(&r);
			box(b, x, y, w, h);
			current = true;
			break;
		}
		case AK_PATH_OP_ROUNDED_RECT: {
			float x = arg(&r), y = arg(&r), w = arg(&r), h = arg(&r);
			float rad = fminf(arg(&r), fminf(fabsf(w) / 2, fabsf(h) / 2));
			current = true;
			if (rad <= 0) {
				box(b, x, y, w, h);
				break;
			}
			CMD(b, 'M', x + rad, y);
			CMD(b, 'L', x + w - rad, y);
			svg_arc(b, rad, rad, true, x + w, y + rad);
			CMD(b, 'L', x + w, y + h - rad);
			svg_arc(b, rad, rad, true, x + w - rad, y + h);
			CMD(b, 'L', x + rad, y + h);
			svg_arc(b, rad, rad, true, x, y + h - rad);
			CMD(b, 'L', x, y + rad);
			svg_arc(b, rad, rad, true, x + rad, y);
			cmd(b, 'Z', 0, NULL);
			break;
		}
		case AK_PATH_OP_ELLIPSE: {
			float x = arg(&r), y = arg(&r), w = arg(&r), h = arg(&r);
			float rx = w / 2, ry = h / 2, cx = x + rx, cy = y + ry;
			if (rx == 0 || ry == 0) break;
			float ax = fabsf(rx), ay = fabsf(ry);
			CMD(b, 'M', cx + rx, cy);
			svg_arc(b, ax, ay, true, cx, cy + ry);
			svg_arc(b, ax, ay, true, cx - rx, cy);
			svg_arc(b, ax, ay, true, cx, cy - ry);
			svg_arc(b, ax, ay, true, cx + rx, cy);
			cmd(b, 'Z', 0, NULL);
			current = true;
			break;
		}
		case AK_PATH_OP_ARC: {
			float cx = arg(&r), cy = arg(&r), rad = arg(&r);
			double start = arg(&r), end = arg(&r);
			float cw = arg(&r);
			// The screen's clockwise (y down) is the angle growing.
			double sweep = end - start;
			if (cw != 0) {
				while (sweep < 0) sweep += 2 * M_PI;
				sweep = fmin(sweep, 2 * M_PI);
			} else {
				while (sweep > 0) sweep -= 2 * M_PI;
				sweep = fmax(sweep, -2 * M_PI);
			}
			float x0 = cx + rad * (float)cos(start), y0 = cy + rad * (float)sin(start);
			if (current)
				CMD(b, 'L', x0, y0);  // a line joins it, from the figure's last point
			else
				CMD(b, 'M', x0, y0);
			current = true;
			if (rad <= 0 || sweep == 0) break;
			int k = (int)ceil(fabs(sweep) / (M_PI / 2) - 1e-9);
			for (int i = 1; i <= k; i++) {
				double a = start + sweep * i / k;
				svg_arc(b, fabsf(rad), fabsf(rad), sweep > 0, cx + rad * (float)cos(a), cy + rad * (float)sin(a));
			}
			break;
		}
		default:
			return;
		}
	}
}

// lx_build_path makes a GskPath of Shape's path format: NULL if it draws
// nothing.
GskPath *lx_build_path(const uint8_t *p, size_t len) {
	ak_buf b = {0};
	lx_path_svg(p, len, &b);
	if (!b.len) {
		ak_buf_free(&b);
		return NULL;
	}
	GskPath *path = gsk_path_parse(lx_buf_cstr(&b));
	ak_buf_free(&b);
	if (path && gsk_path_is_empty(path)) {
		gsk_path_unref(path);
		return NULL;
	}
	return path;
}

// lx_new_stroke makes a GskStroke, its miter limit Core Graphics' (10).
GskStroke *lx_new_stroke(float width, int cap, int join, const float *dash, size_t n, float phase) {
	GskStroke *st = gsk_stroke_new(fmaxf(width, 0));
	gsk_stroke_set_line_cap(st, (GskLineCap)cap);
	gsk_stroke_set_line_join(st, (GskLineJoin)join);
	gsk_stroke_set_miter_limit(st, 10);
	float drawn = 0;
	for (size_t i = 0; i < n; i++) drawn += fmaxf(dash[i], 0);
	if (n && drawn > 0) {
		gsk_stroke_set_dash(st, dash, n);
		gsk_stroke_set_dash_offset(st, phase);
	}
	return st;
}

// lx_trim_path is the part of path from a to b (fractions of its length),
// its measure made in *m if it has none.
GskPath *lx_trim_path(GskPath *path, GskPathMeasure **m, float a, float b) {
	if (!*m) *m = gsk_path_measure_new(path);
	float length = gsk_path_measure_get_length(*m);
	if (!(length > 0)) return NULL;
	GskPathPoint p0, p1;
	if (!gsk_path_measure_get_point(*m, a * length, &p0) || !gsk_path_measure_get_point(*m, b * length, &p1)) return NULL;
	GskPathBuilder *builder = gsk_path_builder_new();
	gsk_path_builder_add_segment(builder, path, &p0, &p1);
	return gsk_path_builder_free_to_path(builder);
}

// ---- The view ----

static void unref_drawn(shape_view *s) {
	if (s->drawn && s->drawn != s->path) gsk_path_unref(s->drawn);
	s->drawn = NULL;
}

// trimmed makes the outline the stroke draws: the path, or its part
// between the trim's ends.
static void trimmed(shape_view *s) {
	unref_drawn(s);
	if (!s->path) return;
	float a = fmaxf(s->trim[0], 0), b = fminf(s->trim[1], 1);
	if (a <= 0 && b >= 1) {
		s->drawn = s->path;
		return;
	}
	if (b <= a) return;  // nothing of it
	s->drawn = lx_trim_path(s->path, &s->measure, a, b);
}

// set_path takes path as the outline (NULL: none), letting go of the old.
static void set_path(shape_view *s, GskPath *path) {
	unref_drawn(s);
	if (s->measure) {
		gsk_path_measure_unref(s->measure);
		s->measure = NULL;
	}
	if (s->path) gsk_path_unref(s->path);
	s->path = path;
	trimmed(s);
}

static void free_stroke(shape_view *s) {
	if (s->stroke) gsk_stroke_free(s->stroke);
	s->stroke = NULL;
}

static void stop_tick(node *n, shape_view *s) {
	if (s->tick && n->widget) gtk_widget_remove_tick_callback(n->widget, s->tick);
	s->tick = 0;
}

static void free_shape(node *n) {
	shape_view *s = n->ext;
	stop_tick(n, s);
	set_path(s, NULL);
	free_stroke(s);
	ak_vec_free(&s->dash);
	free(s);
	n->ext = NULL;
}

static bool shape_create(node *n) {
	if (n->kind != AK_KIND_SHAPE) return false;
	shape_view *s = calloc(1, sizeof *s);
	if (!s) abort();
	s->line_width = 1;
	s->trim[1] = s->target[1] = 1;
	n->ext = s;
	n->ext_free = free_shape;
	return true;
}

static float lerpf(float a, float b, double f) { return a + (b - a) * (float)f; }

static gboolean trim_tick(GtkWidget *w, GdkFrameClock *clock, gpointer data) {
	(void)w;
	(void)clock;
	node *n = data;
	shape_view *s = shape_of(n);
	if (!s) return 0;
	bool done;
	double f = anim_progress(&s->anim, (double)(g_get_monotonic_time() - s->start) / 1e6, &done);
	s->trim[0] = lerpf(s->from[0], s->to[0], f);
	s->trim[1] = lerpf(s->from[1], s->to[1], f);
	if (done) {
		s->trim[0] = s->to[0];
		s->trim[1] = s->to[1];
		s->tick = 0;
	}
	trimmed(s);
	lx_queue_draw(n);
	return !done;
}

// set_trim shows the trim set: at once, or moving there inside an Animate.
static void set_trim(node *n, shape_view *s) {
	bool same = s->target[0] == s->trim[0] && s->target[1] == s->trim[1];
	if (same && !s->tick) return;
	if (!H.animating || !n->placed || !n->widget) {
		stop_tick(n, s);
		s->trim[0] = s->target[0];
		s->trim[1] = s->target[1];
		trimmed(s);
		return;
	}
	if (s->tick && s->to[0] == s->target[0] && s->to[1] == s->target[1]) return;  // both ends set in one animation: one tween
	stop_tick(n, s);
	s->anim = H.anim;
	s->start = g_get_monotonic_time();
	s->from[0] = s->trim[0];
	s->from[1] = s->trim[1];
	s->to[0] = s->target[0];
	s->to[1] = s->target[1];
	s->tick = gtk_widget_add_tick_callback(n->widget, trim_tick, n, NULL);
}

static bool shape_prop(node *n, ak_prop p, const ak_value *v) {
	shape_view *s = shape_of(n);
	if (!s) return false;
	switch (p) {
	case AK_PROP_SHAPE_PATH:
		if (!s->path && n->widget) gtk_widget_set_can_target(n->widget, 0);  // clicks go to what's under it, as a shape layer takes none
		set_path(s, lx_build_path((const uint8_t *)v->s.p, v->s.len));
		break;
	case AK_PROP_SHAPE_FILL: s->fill = (ak_color)v->i; break;
	case AK_PROP_SHAPE_STROKE: s->color = (ak_color)v->i; break;
	case AK_PROP_SHAPE_LINE_WIDTH:
		s->line_width = (float)v->f;
		free_stroke(s);
		break;
	case AK_PROP_SHAPE_LINE_CAP:
		s->cap = v->i < 0 ? 0 : v->i > 2 ? 2 : (int)v->i;  // butt, round, square: GSK's order
		free_stroke(s);
		break;
	case AK_PROP_SHAPE_LINE_JOIN:
		s->join = v->i < 0 ? 0 : v->i > 2 ? 2 : (int)v->i;  // miter, round, bevel
		free_stroke(s);
		break;
	case AK_PROP_SHAPE_DASH: {
		s->dash.len = 0;
		s->dash_phase = 0;
		if (v->s.len >= 8) {
			ak_reader r = {(const uint8_t *)v->s.p, v->s.len, 0, false};
			s->dash_phase = ak_read_f32(&r);
			while (r.off + 4 <= r.len) ak_vec_push(&s->dash, ak_read_f32(&r));
		}
		free_stroke(s);
		break;
	}
	case AK_PROP_SHAPE_TRIM_START:
	case AK_PROP_SHAPE_TRIM_END:
		s->target[p == AK_PROP_SHAPE_TRIM_END] = (float)v->f;
		set_trim(n, s);
		break;
	default:
		return false;
	}
	lx_queue_draw(n);
	return true;
}

static bool shape_draw(node *n, GtkSnapshot *snap, const graphene_rect_t *b) {
	(void)b;
	shape_view *s = shape_of(n);
	if (!s) return false;
	if (!s->path) return true;
	GdkRGBA c;
	if (s->fill && lx_resolve(s->fill, &c) && c.alpha > 0) gtk_snapshot_append_fill(snap, s->path, GSK_FILL_RULE_WINDING, &c);
	if (!s->drawn || s->line_width <= 0 || !s->color) return true;
	if (lx_resolve(s->color, &c) && c.alpha > 0) {
		if (!s->stroke) s->stroke = lx_new_stroke(s->line_width, s->cap, s->join, s->dash.data, s->dash.len, s->dash_phase);
		gtk_snapshot_append_stroke(snap, s->drawn, s->stroke, &c);
	}
	return true;
}

const area lx_area_shape = {
	.name = "shape",
	.create = shape_create,
	.prop = shape_prop,
	.draw = shape_draw,
};
