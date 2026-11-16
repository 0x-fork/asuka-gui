// svg_test.c: icons' data decoded as internal/svgparse's Encode writes it, fitted
// to a box, and a Shape's layers resolved in one (platform/desktop's
// TestStdUnits).
#include "svg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;

#define CHECK(c)                                                    \
	do {                                                            \
		if (!(c)) {                                                 \
			printf("%s:%d: %s\n", __FILE__, __LINE__, #c);          \
			failed = 1;                                             \
		}                                                           \
	} while (0)

static void f32(asuka_writer *w, float v) {
	uint32_t u;
	memcpy(&u, &v, 4);
	asuka_writer_u32(w, u);
}

int main(void) {
	// A 24x24 icon: a stroked path of the icon's color (a move, a line, a
	// cubic, a close) and a red rounded rect.
	asuka_writer w = {0};
	float head[] = {0, 0, 24, 24, 24, 24};
	for (int i = 0; i < 6; i++) f32(&w, head[i]);
	asuka_writer_u32(&w, 2);
	asuka_writer_u8(&w, 0);  // no fill
	asuka_writer_u8(&w, 2), f32(&w, 0.5f);
	f32(&w, 2);
	asuka_writer_u8(&w, 1), asuka_writer_u8(&w, 2);  // round caps, bevel joins
	asuka_writer path = {0};
	asuka_writer_u8(&path, 1), f32(&path, 12), f32(&path, 3);
	asuka_writer_u8(&path, 2), f32(&path, 12), f32(&path, 15);
	asuka_writer_u8(&path, 4);
	for (int i = 0; i < 6; i++) f32(&path, (float)i);
	asuka_writer_u8(&path, 5);
	asuka_writer_u32(&w, (uint32_t)path.len);
	asuka_writer_put(&w, path.data, path.len);
	asuka_writer_free(&path);
	asuka_writer_u8(&w, 1), asuka_writer_u8(&w, 255), asuka_writer_u8(&w, 0), asuka_writer_u8(&w, 0), asuka_writer_u8(&w, 255);
	asuka_writer_u8(&w, 0);
	f32(&w, 1);
	asuka_writer_u8(&w, 0), asuka_writer_u8(&w, 0);
	asuka_writer_u32(&w, 1 + 5 * 4);
	asuka_writer_u8(&w, 7), f32(&w, 2), f32(&w, 2), f32(&w, 20), f32(&w, 10), f32(&w, 3);

	svg_drawing d = svg_decode(w.data, w.len);
	asuka_writer_free(&w);
	CHECK(d.width == 24 && d.height == 24 && d.n == 2);
	if (d.n == 2) {
		svg_layer *a = &d.layers[0], *b = &d.layers[1];
		CHECK(a->fill.kind == 0 && a->stroke.kind == 2 && a->stroke.opacity == 0.5f && a->width == 2 && a->cap == 1 && a->join == 2);
		CHECK(a->n == 4 && a->path[0].op == 1 && a->path[0].a[1] == 3 && a->path[2].op == 4 && a->path[2].a[5] == 5 && a->path[3].op == 5);
		CHECK(b->fill.kind == 1 && b->fill.r == 255 && b->fill.a == 255 && b->n == 1 && b->path[0].op == 7 && b->path[0].a[4] == 3);
	}
	// Fitted to a 48x24 box: as large as its height allows, centered
	// across.
	float scale, dx, dy;
	CHECK(svg_fit(&d, 48, 24, &scale, &dx, &dy) && scale == 1 && dx == 12 && dy == 0);
	svg_drawing_free(&d);
	CHECK(svg_decode((const uint8_t *)"short", 5).n == 0);

	// A Shape: a rect from 4 points in to the box's middle, rounded, then
	// an ellipse over the whole box, in a 200x100 box at 2 pixels a point.
	double rect[] = {6, 0, 4, 0, 4, 0.5, 0, 0.5, 0, 8, 7, 0, 0, 0, 0, 1, 0, 1, 0};
	int64_t fill = (int64_t)2 << 32 | 0x112233ff;
	svg_Layer layer = {.path = {{rect, sizeof rect / sizeof *rect}}, .fill = &fill, .width = 1.5, .cap = 2, .join = 1};
	svg_shape sh = svg_shape_copy((svg_list_Layer){&layer, 1});
	svg_drawing s = svg_shape_drawing(&sh, 200, 100, 2);
	CHECK(s.n == 1 && s.view_box[2] == 200 && s.view_box[3] == 100);
	if (s.n == 1) {
		svg_layer *l = &s.layers[0];
		CHECK(l->fill.kind == 1 && l->width == 3 && l->cap == 2 && l->join == 1 && l->n == 2);
		// The test host resolves a color as its own value (unit.c).
		CHECK(l->fill.r == 0x11 && l->fill.g == 0x22 && l->fill.b == 0x33);
		CHECK(l->path[0].op == 7 && l->path[0].a[0] == 8 && l->path[0].a[1] == 8 && l->path[0].a[2] == 92 && l->path[0].a[3] == 42 &&
		      l->path[0].a[4] == 16);
		CHECK(l->path[1].op == 8 && l->path[1].a[2] == 200 && l->path[1].a[3] == 100);
	}
	svg_drawing_free(&s);
	svg_shape_free(&sh);
	if (!failed) printf("svg: ok\n");
	return failed;
}
