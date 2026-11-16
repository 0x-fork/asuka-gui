// std/svg on Linux: a GtkDrawingArea whose draw function fills and
// strokes the layers with Cairo (GTK's), fitted to it; currentColor is
// the view's color, the label's the widget's own CSS color. A Shape's
// layers are resolved in the area's size each time it draws.
#include "asuka_linux.h"
#include "svg.h"

#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>

typedef struct _cairo_pattern cairo_pattern_t;

static struct {
	bool tried, ok;
	GtkWidget *(*area_new)(void);
	void (*set_draw_func)(GtkWidget *, void (*)(GtkWidget *, cairo_t *, int, int, gpointer), gpointer, GDestroyNotify);
	void (*queue_draw)(GtkWidget *);
	void (*queue_resize)(GtkWidget *);
	void (*get_color)(GtkWidget *, GdkRGBA *);  // GTK 4.10
	void (*save)(cairo_t *);
	void (*restore)(cairo_t *);
	void (*new_path)(cairo_t *);
	void (*move_to)(cairo_t *, double, double);
	void (*line_to)(cairo_t *, double, double);
	void (*curve_to)(cairo_t *, double, double, double, double, double, double);
	void (*close_path)(cairo_t *);
	void (*new_sub_path)(cairo_t *);
	void (*rectangle)(cairo_t *, double, double, double, double);
	void (*arc)(cairo_t *, double, double, double, double, double);
	void (*set_source_rgba)(cairo_t *, double, double, double, double);
	void (*set_source)(cairo_t *, cairo_pattern_t *);
	void (*fill_preserve)(cairo_t *);
	void (*stroke)(cairo_t *);
	void (*set_line_width)(cairo_t *, double);
	void (*set_line_cap)(cairo_t *, int);
	void (*set_line_join)(cairo_t *, int);
	cairo_pattern_t *(*pattern_create_linear)(double, double, double, double);
	void (*pattern_add_color_stop_rgba)(cairo_pattern_t *, double, double, double, double, double);
	void (*pattern_destroy)(cairo_pattern_t *);
} gk;

static bool load(void) {
	if (gk.tried) return gk.ok;
	gk.tried = true;
	if (!asuka_linux()) return false;
	dlopen("libcairo.so.2", RTLD_NOW | RTLD_GLOBAL);  // GTK's own: loaded with it
#define SYM(f, name) *(void **)&gk.f = asuka_linux_sym(name)
	SYM(area_new, "gtk_drawing_area_new");
	SYM(set_draw_func, "gtk_drawing_area_set_draw_func");
	SYM(queue_draw, "gtk_widget_queue_draw");
	SYM(queue_resize, "gtk_widget_queue_resize");
	SYM(get_color, "gtk_widget_get_color");
	SYM(save, "cairo_save");
	SYM(restore, "cairo_restore");
	SYM(new_path, "cairo_new_path");
	SYM(move_to, "cairo_move_to");
	SYM(line_to, "cairo_line_to");
	SYM(curve_to, "cairo_curve_to");
	SYM(close_path, "cairo_close_path");
	SYM(new_sub_path, "cairo_new_sub_path");
	SYM(rectangle, "cairo_rectangle");
	SYM(arc, "cairo_arc");
	SYM(set_source_rgba, "cairo_set_source_rgba");
	SYM(set_source, "cairo_set_source");
	SYM(fill_preserve, "cairo_fill_preserve");
	SYM(stroke, "cairo_stroke");
	SYM(set_line_width, "cairo_set_line_width");
	SYM(set_line_cap, "cairo_set_line_cap");
	SYM(set_line_join, "cairo_set_line_join");
	SYM(pattern_create_linear, "cairo_pattern_create_linear");
	SYM(pattern_add_color_stop_rgba, "cairo_pattern_add_color_stop_rgba");
	SYM(pattern_destroy, "cairo_pattern_destroy");
#undef SYM
	gk.ok = gk.area_new && gk.set_draw_func && gk.queue_draw && gk.queue_resize && gk.save && gk.restore && gk.new_path && gk.move_to &&
	        gk.line_to && gk.curve_to && gk.close_path && gk.new_sub_path && gk.rectangle && gk.arc && gk.set_source_rgba && gk.set_source &&
	        gk.fill_preserve && gk.stroke && gk.set_line_width && gk.set_line_cap && gk.set_line_join && gk.pattern_create_linear &&
	        gk.pattern_add_color_stop_rgba && gk.pattern_destroy;
	return gk.ok;
}

typedef struct {
	float r, g, b, a;
} rgba;

// color is a paint's color with currentColor cur: false when it paints
// nothing.
static bool color(svg_paint p, rgba cur, rgba *out) {
	switch (p.kind) {
	case 1:
		*out = (rgba){p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, p.a / 255.0f};
		return p.a > 0;
	case 2:
		*out = (rgba){cur.r, cur.g, cur.b, cur.a * p.opacity};
		return out->a > 0;
	}
	return false;
}

// paint fills and strokes d's layers, scaled by scale and moved by dx,
// dy; currentColor is cur.
static void paint(cairo_t *cr, const svg_drawing *d, float scale, float dx, float dy, rgba cur) {
#define X(v) ((double)(v) * scale + dx)
#define Y(v) ((double)(v) * scale + dy)
	for (size_t li = 0; li < d->n; li++) {
		const svg_layer *l = &d->layers[li];
		gk.new_path(cr);
		double px = 0, py = 0, sx = 0, sy = 0;  // the pen, and its figure's start
		for (size_t i = 0; i < l->n; i++) {
			const float *a = l->path[i].a;
			switch (l->path[i].op) {
			case 1:
				gk.move_to(cr, px = sx = X(a[0]), py = sy = Y(a[1]));
				break;
			case 2:
				gk.line_to(cr, px = X(a[0]), py = Y(a[1]));
				break;
			case 3: {  // a quadratic curve as a cubic one
				double cx = X(a[0]), cy = Y(a[1]), x = X(a[2]), y = Y(a[3]);
				gk.curve_to(cr, px + (cx - px) * 2 / 3, py + (cy - py) * 2 / 3, x + (cx - x) * 2 / 3, y + (cy - y) * 2 / 3, x, y);
				px = x, py = y;
				break;
			}
			case 4:
				gk.curve_to(cr, X(a[0]), Y(a[1]), X(a[2]), Y(a[3]), px = X(a[4]), py = Y(a[5]));
				break;
			case 5:
				gk.close_path(cr);
				px = sx, py = sy;
				break;
			case 6:
				gk.rectangle(cr, X(a[0]), Y(a[1]), a[2] * scale, a[3] * scale);
				break;
			case 7: {
				double x = X(a[0]), y = Y(a[1]), w = a[2] * scale, h = a[3] * scale, r = fmin(a[4] * scale, fmin(w / 2, h / 2));
				gk.new_sub_path(cr);
				gk.arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
				gk.arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
				gk.arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
				gk.arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
				gk.close_path(cr);
				break;
			}
			case 8: {  // an ellipse: four cubic curves (a circle's kappa)
				const double k = 0.5522848;
				double rx = a[2] * scale / 2, ry = a[3] * scale / 2, cx = X(a[0]) + rx, cy = Y(a[1]) + ry;
				gk.move_to(cr, cx + rx, cy);
				gk.curve_to(cr, cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry);
				gk.curve_to(cr, cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy);
				gk.curve_to(cr, cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry);
				gk.curve_to(cr, cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy);
				gk.close_path(cr);
				break;
			}
			}
		}
		rgba c, to;
		if (color(l->fill, cur, &c)) {
			if (l->fade.kind) {
				color(l->fade, cur, &to);
				cairo_pattern_t *g = gk.pattern_create_linear(0, dy, 0, dy + l->fade_to * scale);
				gk.pattern_add_color_stop_rgba(g, 0, c.r, c.g, c.b, c.a);
				gk.pattern_add_color_stop_rgba(g, 1, to.r, to.g, to.b, to.a);
				gk.set_source(cr, g);
				gk.fill_preserve(cr);
				gk.pattern_destroy(g);
			} else {
				gk.set_source_rgba(cr, c.r, c.g, c.b, c.a);
				gk.fill_preserve(cr);  // the winding rule, Cairo's default
			}
		}
		if (color(l->stroke, cur, &c) && l->width > 0) {
			gk.set_source_rgba(cr, c.r, c.g, c.b, c.a);
			gk.set_line_width(cr, l->width * scale);
			gk.set_line_cap(cr, l->cap);  // Cairo's butt, round, square and miter, round, bevel are ours
			gk.set_line_join(cr, l->join);
			gk.stroke(cr);
		}
		gk.new_path(cr);
	}
#undef X
#undef Y
}

typedef struct {
	bool shape;
	svg_drawing icon;
	int64_t color;
	svg_shape layers;
} drawn;

// current is the view's currentColor: the label's is the widget's CSS
// color (which follows the theme), another the host's.
static rgba current(GtkWidget *w, int64_t c) {
	if ((c == 0 || c == ((int64_t)1 << 32 | 1)) && gk.get_color) {
		GdkRGBA fg;
		gk.get_color(w, &fg);
		return (rgba){fg.red, fg.green, fg.blue, fg.alpha};
	}
	uint32_t v = svg_rgba(c);
	return (rgba){(v >> 24) / 255.0f, (v >> 16 & 255) / 255.0f, (v >> 8 & 255) / 255.0f, (v & 255) / 255.0f};
}

static void draw(GtkWidget *area, cairo_t *cr, int w, int h, gpointer p) {
	drawn *v = p;
	gk.save(cr);
	if (v->shape) {
		svg_drawing d = svg_shape_drawing(&v->layers, (float)w, (float)h, 1);
		paint(cr, &d, 1, 0, 0, current(area, 0));
		svg_drawing_free(&d);
	} else {
		float scale, dx, dy;
		if (svg_fit(&v->icon, (float)w, (float)h, &scale, &dx, &dy)) paint(cr, &v->icon, scale, dx, dy, current(area, v->color));
	}
	gk.restore(cr);
}

static void free_drawn(gpointer p) {
	drawn *v = p;
	svg_drawing_free(&v->icon);
	svg_shape_free(&v->layers);
	free(v);
}

static GtkWidget *area(drawn *v) {
	GtkWidget *a = gk.area_new();
	G(g_object_ref_sink)(a);
	gk.set_draw_func(a, draw, v, free_drawn);  // the area frees v as it goes
	return a;
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
	gk.queue_resize(self->widget);
	gk.queue_draw(self->widget);
}

static void view_color(svg_View *self, int64_t c) {
	((view *)self)->d->color = c;
	gk.queue_draw(self->widget);
}

// view_measure is the icon's size, in points, when its style gives none.
static bool view_measure(svg_View *self, float w, float h, float *ow, float *oh) {
	view *v = (view *)self;
	*ow = v->d->icon.width;
	*oh = v->d->icon.height;
	return true;
}

static void view_destroy(svg_View *self) {
	G(g_object_unref)(self->widget);
	free(self);
}

static const svg_View_vtable view_vt = {.destroy = view_destroy, .set_icon = view_icon, .set_color = view_color, .measure = view_measure};

svg_View *svg_make_View(svg_View_events events) {
	if (!load()) return NULL;
	view *v = calloc(1, sizeof *v);
	v->base.vt = &view_vt;
	v->d = calloc(1, sizeof *v->d);
	v->d->color = (int64_t)1 << 32 | 1;
	v->base.widget = area(v->d);
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
	gk.queue_draw(self->widget);
}

// shape_measure: a Shape has no size of its own.
static bool shape_measure(svg_Shape *self, float w, float h, float *ow, float *oh) {
	*ow = 0;
	*oh = 0;
	return true;
}

static void shape_destroy(svg_Shape *self) {
	G(g_object_unref)(self->widget);
	free(self);
}

static const svg_Shape_vtable shape_vt = {.destroy = shape_destroy, .set_layers = shape_layers, .measure = shape_measure};

svg_Shape *svg_make_Shape(svg_Shape_events events) {
	if (!load()) return NULL;
	shape *s = calloc(1, sizeof *s);
	s->base.vt = &shape_vt;
	s->d = calloc(1, sizeof *s->d);
	s->d->shape = true;
	s->base.widget = area(s->d);
	return &s->base;
}
