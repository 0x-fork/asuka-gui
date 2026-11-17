// svg.h: what svg's Windows and Linux C share (svg.c): an icon's layers
// as the compiler encodes them (internal/svgparse's Encode: transforms
// applied, arcs made curves), and a Shape's layers resolved in its box,
// both as a drawing each platform fills and strokes with its own vector
// drawing (Cairo, GDI+).
#ifndef SVG_H
#define SVG_H

#include "svg.asuka.h"

// svg_paint is a layer's fill or stroke: kind 0 none, 1 rgba, 2 the
// icon's color (currentColor) at opacity.
typedef struct {
	uint8_t kind;
	uint8_t r, g, b, a;
	float opacity;
} svg_paint;

// svg_seg is a path's segment: its op and arguments. 1 move (x, y), 2
// line (x, y), 3 quad (cx, cy, x, y), 4 cubic (ax, ay, bx, by, x, y), 5
// close, 6 rect (x, y, w, h), 7 rounded rect (x, y, w, h, r), 8 ellipse
// in a box (x, y, w, h).
typedef struct {
	uint8_t op;
	float a[6];
} svg_seg;

typedef struct {
	svg_paint fill, stroke;
	float width;
	uint8_t cap, join;  // butt, round, square; miter, round, bevel
	svg_seg *path;
	size_t n;
	// A Shape's fill that fades: from fill at y 0 to fade at fade_to.
	svg_paint fade;
	float fade_to;
} svg_layer;

typedef struct {
	float view_box[4];
	float width, height;
	svg_layer *layers;
	size_t n;
} svg_drawing;

// svg_decode reads an icon's data (empty, if it's too short).
svg_drawing svg_decode(const uint8_t *d, size_t n);
void svg_drawing_free(svg_drawing *d);

// svg_fit is where the view box goes in a box of w×h: scaled by *scale
// and moved by *dx, *dy (xMidYMid meet); false when it has no size.
bool svg_fit(const svg_drawing *d, float w, float h, float *scale, float *dx, float *dy);

// svg_shape is a Shape's layers, as the app set them (copied).
typedef struct {
	double *path;
	size_t n;
	bool has_fill, has_stroke, has_fade;
	int64_t fill, stroke, fade;  // the wire's colors, resolved as it draws
	float width;
	uint8_t cap, join;
} svg_shape_layer;

typedef struct {
	svg_shape_layer *layers;
	size_t n;
} svg_shape;

svg_shape svg_shape_copy(svg_list_Layer ls);
void svg_shape_free(svg_shape *s);

// svg_shape_drawing is a Shape's layers as a drawing in a box of w×h
// pixels, s pixels a point (a point is a fraction of the box plus
// points), drawn at scale 1: its view box is the box. Its colors are
// resolved for the appearance shown now.
svg_drawing svg_shape_drawing(const svg_shape *sh, float w, float h, float s);

// svg_rgba is a color as the wire has it, resolved (asuka_desktop_color):
// 0xRRGGBBAA. The platform's default (0) is the label's.
uint32_t svg_rgba(int64_t color);

#endif
