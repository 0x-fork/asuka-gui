// Canvas and its Graphics, Path, Shape, Shader (canvas.c), shapes' dashes
// included. The draw and path closures call the canvas.* and path.*
// builtins, as compiled code does.
#include "uih.h"

static ak_val call_b(const char *name, ak_val *args, uint32_t n) {
	ak_builtin_fn f = ak_builtin_find(name);
	if (!f) ak_panic("no builtin %s", name);
	return f(args, n);
}

static ak_val ref(void *p) { return (ak_val){.p = p}; }
static ak_val num(double f) { return (ak_val){.f = f}; }
static ak_val i64(int64_t i) { return (ak_val){.i = i}; }

// A path op on p (borrowed), the result let go: it's p.
static void pop(const char *name, void *p, double a, double b, double c, double d, double e) {
	ak_val args[6] = {ref(p), num(a), num(b), num(c), num(d), num(e)};
	ak_release(call_b(name, args, 6).p);
}

// ---- Canvas (TestCanvas) ----

static void *level, *other;
static int draws;
static ak_struct *dashed;
static ak_str *label;

static const uint8_t stroke_kinds[] = {AK_K_FLOAT, AK_K_INT, AK_K_INT, AK_K_REF, AK_K_FLOAT};
static const ak_shape stroke_shape = {"StrokeStyle", 5, stroke_kinds, NULL, NULL, 0, NULL, false, NULL};
static const uint8_t text_kinds[] = {AK_K_FLOAT, AK_K_INT, AK_K_INT, AK_K_INT};
static const ak_shape text_shape = {"CanvasText", 4, text_kinds, NULL, NULL, 0, NULL, false, NULL};
static ak_struct *centered;

static ak_val draw(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	draws++;
	ak_vm_cell_get(other, true);
	ak_val g = a[0];
	double w = call_b("canvas.width", &g, 1).f, h = call_b("canvas.height", &g, 1).f;
	ak_val p = call_b("canvas.path", &g, 1);
	pop("path.rounded_rect", p.p, 0, 0, w, h, 8);
	ak_val fill1[3] = {g, p, i64((int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_SECONDARY_BACKGROUND))};
	call_b("canvas.fill", fill1, 3);
	ak_release(p.p);
	p = call_b("canvas.path", &g, 1);
	pop("path.rect", p.p, 0, 0, w * ak_vm_cell_get(level, true).f, h, 0);
	ak_val fill2[3] = {g, p, i64((int64_t)ak_rgba(0x34, 0xc7, 0x59, 0xff))};
	call_b("canvas.fill", fill2, 3);
	ak_release(p.p);
	p = call_b("canvas.path", &g, 1);
	pop("path.move", p.p, 0, h, 0, 0, 0);
	pop("path.line", p.p, w, 0, 0, 0, 0);
	ak_val st[4] = {g, p, i64((int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_LABEL)), ref(dashed)};
	call_b("canvas.stroke", st, 4);
	ak_release(p.p);
	ak_val tx[5] = {g, ref(label), num(w / 2), num(h / 2), ref(centered)};
	call_b("canvas.text", tx, 5);
	return noval();
}

static ak_val canvas_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *v = ak_ui_new(AK_EL_CANVAS);
	ak_closure *d = fn(draw, NULL);
	set(v, AK_KEY_CANVAS_DRAW, (ak_val){.p = d});
	set_i(v, AK_KEY_WIDTH, ak_length_pt(200));
	set_i(v, AK_KEY_HEIGHT, ak_length_pt(40));
	return (ak_val){.p = v};
}

// A canvas draws at its laid-out size, again when what it read changes,
// and sends only a drawing that differs; drawing again allocates nothing.
TEST(canvas) {
	draws = 0;
	level = ak_vm_cell_new((ak_val){.f = 0.5}, AK_K_FLOAT, NULL).p;
	other = cell_int(0);
	dashed = ak_struct_new(&stroke_shape);
	dashed->f[0].f = 2;
	ak_arr *dash = ak_arr_new(AK_K_FLOAT, NULL, 2);
	ak_arr_push(&dash, num(4));
	ak_arr_push(&dash, num(2));
	dashed->f[3].p = dash;
	centered = ak_struct_new(&text_shape);
	centered->f[3].i = AK_TEXT_ALIGN_CENTER;
	label = ak_str_c("50%");
	ak_uitest *a = mount(canvas_app, NULL);
	ak_fake_view *v = nth(a, AK_KIND_CANVAS, 0);
	REQUIRE(v);
	ak_span cmds = bytes_of(v, AK_PROP_CANVAS_COMMANDS);
	CHECK(draws == 1 && cmds.len > 0 && (uint8_t)cmds.p[0] == AK_CANVAS_OP_FILL);
	// The rounded rect at the canvas's size.
	float rw, rh;
	memcpy(&rw, cmds.p + 1 + 8 + 4 + 1 + 8, 4);
	memcpy(&rh, cmds.p + 1 + 8 + 4 + 1 + 12, 4);
	CHECK(rw == 200 && rh == 40);
	reset(a);
	write_i(other, 1);  // draws again, the same drawing: nothing sent
	ak_ui_flush();
	CHECK(draws == 2 && sets(a) == 0);
	ak_vm_cell_set(level, (ak_val){.f = 0.75});
	ak_ui_flush();
	CHECK_EQ_INT(sets(a), 1);
	uint64_t allocs = 0;
	for (int i = 0; i < 50; i++) {
		uint64_t before = ak_alloc_count;
		write_i(other, peek_i(other) + 1);
		ak_ui_flush();
		allocs += ak_alloc_count - before;
	}
	if (allocs) T_FAIL("%llu allocations in 50 redraws", (unsigned long long)allocs);
	done(a);
	ak_release(level), ak_release(other), ak_release(dashed), ak_release(centered), ak_release(label);
}

// ---- Shape (TestShape, TestShapeDash) ----

static void *progress, *inset;

static ak_val ellipse(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	double i = ak_vm_cell_get(inset, true).f;
	ak_val p = call_b("path.new", NULL, 0);
	pop("path.ellipse", p.p, i, i, a[0].f - 2 * i, a[1].f - 2 * i, 0);
	return p;
}

static ak_val read_progress(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	return ak_vm_cell_get(progress, true);
}

static ak_val shape_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *v = ak_ui_new(AK_EL_SHAPE);
	ak_closure *p = fn(ellipse, NULL);
	set(v, AK_KEY_SHAPE_PATH, (ak_val){.p = p});
	set_i(v, AK_KEY_SHAPE_STROKE, (int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_ACCENT));
	set_f(v, AK_KEY_SHAPE_LINE_WIDTH, 4);
	set_i(v, AK_KEY_SHAPE_LINE_CAP, AK_LINE_CAP_ROUND);
	bind(v, AK_KEY_SHAPE_TRIM_END, read_progress, NULL);
	set_i(v, AK_KEY_WIDTH, ak_length_pt(100));
	set_i(v, AK_KEY_HEIGHT, ak_length_pt(100));
	return (ak_val){.p = v};
}

static void path_floats(const ak_fake_view *s, float out[4]) {
	ak_span b = bytes_of(s, AK_PROP_SHAPE_PATH);
	if (b.len != 17 || (uint8_t)b.p[0] != AK_PATH_OP_ELLIPSE) {
		T_FAIL("path of %u bytes", b.len);
		memset(out, 0, 16);
		return;
	}
	memcpy(out, b.p + 1, 16);
}

static ak_val more(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_vm_cell_set(progress, (ak_val){.f = 1});
	return noval();
}

// A shape's path is made for its size once it's laid out, and again when
// what it reads or its size changes; its trim animates in animate { }.
TEST(shape) {
	progress = ak_vm_cell_new((ak_val){.f = 0.25}, AK_K_FLOAT, NULL).p;
	inset = ak_vm_cell_new((ak_val){.f = 0}, AK_K_FLOAT, NULL).p;
	ak_uitest *a = mount(shape_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_view *s = nth(a, AK_KIND_SHAPE, 0);
	REQUIRE(s);
	float p[4];
	path_floats(s, p);
	CHECK(p[2] == 100 && p[3] == 100);
	CHECK_EQ_F(ak_fake_float(s, AK_PROP_SHAPE_TRIM_END), 0.25);
	CHECK_EQ_F(ak_fake_float(s, AK_PROP_SHAPE_LINE_WIDTH), 4);
	CHECK_EQ_INT(ak_fake_int(s, AK_PROP_SHAPE_LINE_CAP), AK_LINE_CAP_ROUND);
	ak_vm_cell_set(inset, (ak_val){.f = 2});
	ak_ui_flush();
	path_floats(s, p);
	CHECK(p[0] == 2 && p[2] == 96);
	ak_fake_reset(f);
	ak_closure *body = fn(more, NULL);
	ak_ui_animate(NULL, body);
	ak_release(body);
	ak_ui_flush();
	ak_fake_index_vec cmds = {0};
	ak_fake_commands(f, AK_CMD_ANIMATE_BEGIN, &cmds);
	CHECK(ak_fake_float(s, AK_PROP_SHAPE_TRIM_END) == 1 && cmds.len == 1);
	ak_vec_free(&cmds);
	CHECK(!has_prop(s, AK_PROP_SHAPE_DASH));  // a solid stroke sends no dash
	done(a);
	ak_release(progress), ak_release(inset);
}

static ak_val hline(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	ak_val p = call_b("path.new", NULL, 0);
	pop("path.move", p.p, 0, a[1].f / 2, 0, 0, 0);
	pop("path.line", p.p, a[0].f, a[1].f / 2, 0, 0, 0);
	return p;
}

static ak_val dash_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *v = ak_ui_new(AK_EL_SHAPE);
	ak_closure *p = fn(hline, NULL);
	set(v, AK_KEY_SHAPE_PATH, (ak_val){.p = p});
	set_i(v, AK_KEY_SHAPE_STROKE, (int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_ACCENT));
	ak_arr *dash = ak_arr_new(AK_K_FLOAT, NULL, 2);
	ak_arr_push(&dash, num(6));
	ak_arr_push(&dash, num(4));
	set(v, AK_KEY_SHAPE_DASH, (ak_val){.p = dash});
	set_f(v, AK_KEY_SHAPE_DASH_PHASE, 2);
	set_i(v, AK_KEY_WIDTH, ak_length_pt(100));
	set_i(v, AK_KEY_HEIGHT, ak_length_pt(10));
	return (ak_val){.p = v};
}

// A dash goes as its phase, then its lengths; a stroke's width is 1
// unless set.
TEST(shape_dash) {
	ak_uitest *a = mount(dash_app, NULL);
	ak_fake_view *s = nth(a, AK_KIND_SHAPE, 0);
	REQUIRE(s);
	ak_span b = bytes_of(s, AK_PROP_SHAPE_DASH);
	float d[3] = {0};
	REQUIRE(b.len == 12);
	memcpy(d, b.p, 12);
	CHECK(d[0] == 2 && d[1] == 6 && d[2] == 4);
	CHECK_EQ_F(ak_fake_float(s, AK_PROP_SHAPE_LINE_WIDTH), 1);
	CHECK_EQ_INT(bytes_of(s, AK_PROP_SHAPE_PATH).len, 18);
	done(a);
}

// ---- Shader (TestShader) ----

// A compiled shader's bytes (internal/shadercode's Encode): the programs,
// 6 lanes (Time's, Tint's 4, Count's), the Metal offsets, the uniforms.
static ak_bytes *shader_code(void) {
	ak_buf b = {0};
	ak_buf_wstr(&b, AK_STR("package main"));
	ak_buf_wstr(&b, AK_STR("metal"));
	ak_buf_wstr(&b, AK_STR("agsl"));
	ak_buf_u32(&b, 6);  // lanes
	ak_buf_u32(&b, 0), ak_buf_u32(&b, 0), ak_buf_u32(&b, 1);  // size, scale, time+1
	ak_buf_u32(&b, 32);
	for (int i = 0; i < 6; i++) ak_buf_u32(&b, (uint32_t)(4 * i));
	ak_buf_u32(&b, 3);
	ak_buf_wstr(&b, AK_STR("Time")), ak_buf_u32(&b, 0), ak_buf_u32(&b, 1), ak_buf_u8(&b, 0);
	ak_buf_wstr(&b, AK_STR("Tint")), ak_buf_u32(&b, 1), ak_buf_u32(&b, 4), ak_buf_u8(&b, 0);
	ak_buf_wstr(&b, AK_STR("Count")), ak_buf_u32(&b, 5), ak_buf_u32(&b, 1), ak_buf_u8(&b, 1);
	ak_bytes *code = ak_bytes_new(b.data, b.len);
	ak_buf_free(&b);
	return code;
}

static void *tint;
static const uint8_t uniform_kinds[] = {AK_K_REF, AK_K_REF};
static const ak_shape uniform_shape = {"Uniform", 2, uniform_kinds, NULL, NULL, 0, NULL, false, NULL};

static ak_val uniforms(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_arr *us = ak_arr_new(AK_K_REF, &uniform_shape, 3);
	ak_str *name = ak_str_c("Tint");
	ak_val args[2] = {ref(name), ak_vm_cell_get(tint, true)};
	ak_arr_push(&us, call_b("canvas.color_uniform", args, 2));
	ak_release(name);
	ak_struct *count = ak_struct_new(&uniform_shape);
	count->f[0].p = ak_str_c("Count");
	ak_arr *v = ak_arr_new(AK_K_FLOAT, NULL, 1);
	ak_arr_push(&v, num(3));
	count->f[1].p = v;
	ak_arr_push(&us, ref(count));
	ak_struct *nope = ak_struct_new(&uniform_shape);
	nope->f[0].p = ak_str_c("Nope");
	ak_arr *nv = ak_arr_new(AK_K_FLOAT, NULL, 1);
	ak_arr_push(&nv, num(1));
	nope->f[1].p = nv;
	ak_arr_push(&us, ref(nope));
	return ref(us);
}

static ak_val shader_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *v = ak_ui_new(AK_EL_SHADER);
	set(v, AK_KEY_SHADER_PROGRAM, ref(shader_code()));
	bind(v, AK_KEY_SHADER_UNIFORMS, uniforms, NULL);
	set_i(v, AK_KEY_SHADER_ANIMATED, 1);
	set_i(v, AK_KEY_HEIGHT, ak_length_pt(100));
	add(v, text("over it"));
	return (ak_val){.p = v};
}

static uint32_t lane(ak_span b, int i) {
	uint32_t w;
	memcpy(&w, b.p + 4 * i, 4);
	return w;
}

// A shader sends its compiled code once, and its uniforms' lanes when
// they change: a vec4's floats where its lanes are, an int as an int.
TEST(shader) {
	tint = cell_int((int64_t)ak_rgba(0x38, 0xbd, 0xf8, 0xff));
	ak_uitest *a = mount(shader_app, NULL);
	ak_fake_view *s = nth(a, AK_KIND_SHADER, 0);
	REQUIRE(s);
	CHECK(bytes_of(s, AK_PROP_SHADER_CODE).len > 0 && ak_fake_bool(s, AK_PROP_SHADER_ANIMATED));
	CHECK_EQ_INT(s->children.len, 1);
	ak_span lanes = bytes_of(s, AK_PROP_SHADER_UNIFORMS);
	REQUIRE(lanes.len == 24);
	float alpha, red;
	uint32_t w3 = lane(lanes, 4), w0 = lane(lanes, 1);
	memcpy(&alpha, &w3, 4), memcpy(&red, &w0, 4);
	CHECK(alpha == 1 && red > 0.2f);
	CHECK_EQ_INT(lane(lanes, 5), 3);
	CHECK_EQ_INT(lane(lanes, 0), 0);  // Time: the host's
	reset(a);
	write_i(tint, (int64_t)ak_rgba(0x38, 0xbd, 0xf8, 0xff));  // the same: nothing sent
	ak_ui_flush();
	CHECK_EQ_INT(sets(a), 0);
	write_i(tint, (int64_t)ak_rgba(0xff, 0, 0, 0xff));
	ak_ui_flush();
	CHECK_EQ_INT(sets(a), 1);
	done(a);
	ak_release(tint);
}

T_MAIN()
