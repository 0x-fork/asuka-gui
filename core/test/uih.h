// uih.h: the helpers the UI runtime's tests share (ui_test.c's): closures
// of C functions, keys set and bound, cells, mounting on the fake host,
// and checks of what it was sent. Each test file includes it once.
#ifndef AK_TEST_UIH_H
#define AK_TEST_UIH_H

#include "t.h"
#include "fake/uitest.h"
#include "ui/ui_ext.h"

#include <stdio.h>

// ---- Helpers ----

// The prelude's structs, in types.asuka's field order.
static const ak_shape *const no_shapes[8];
static const uint8_t edges_kinds[] = {AK_K_FLOAT, AK_K_FLOAT, AK_K_FLOAT, AK_K_FLOAT, AK_K_FLOAT, AK_K_FLOAT};
static const ak_shape edges_shape = {"Edges", 6, edges_kinds, no_shapes, NULL, 0, NULL, false, NULL};
static const uint8_t todo_kinds[] = {AK_K_INT, AK_K_REF, AK_K_BOOL};
static const ak_shape todo_shape = {"Todo", 3, todo_kinds, no_shapes, NULL, 0, NULL, false, NULL};
static const uint8_t range_kinds[] = {AK_K_INT, AK_K_INT};
static const ak_shape range_shape = {"TextRange", 2, range_kinds, no_shapes, NULL, 0, NULL, false, NULL};

static inline void setup(void) {
	ak_signal_bind_thread();
	ak_vm_init();
	ak_ui_builtins_init();
}

static inline ak_val edges4(float t, float r, float b, float l) {
	ak_struct *s = ak_struct_new(&edges_shape);
	s->f[0].f = t, s->f[1].f = r, s->f[2].f = b, s->f[3].f = l;
	return (ak_val){.p = s};
}

static inline ak_val edges_se(float start, float end) {
	ak_struct *s = ak_struct_new(&edges_shape);
	s->f[4].f = start, s->f[5].f = end;
	return (ak_val){.p = s};
}

// set sets a key and lets the value go.
static inline void set(ak_view *v, uint16_t key, ak_val x) {
	ak_ui_set(v, key, x);
	ak_release_val(x, (ak_kind)ak_key_kinds[key]);
}
static inline void set_str(ak_view *v, uint16_t key, const char *s) { set(v, key, (ak_val){.p = ak_str_c(s)}); }
static inline void set_i(ak_view *v, uint16_t key, int64_t i) { ak_ui_set(v, key, (ak_val){.i = i}); }
static inline void set_f(ak_view *v, uint16_t key, double f) { ak_ui_set(v, key, (ak_val){.f = f}); }

// add adds child and lets it go (the parent holds it).
static inline void add(ak_view *p, ak_view *c) {
	ak_ui_add(p, c);
	ak_release(c);
}

static inline ak_view *text(const char *s) {
	ak_view *t = ak_ui_new(AK_EL_TEXT);
	set_str(t, AK_KEY_TEXT_TEXT, s);
	return t;
}

static inline ak_closure *fn(ak_invoke_fn f, const void *data) { return ak_closure_new(f, data, 0, NULL); }

// A closure's capture kinds outlive it: static.
static const uint8_t ref_cap[] = {AK_K_REF};

static inline void bind(ak_view *v, uint16_t key, ak_invoke_fn f, const void *data) {
	ak_closure *c = fn(f, data);
	ak_ui_bind(v, key, c);
	ak_release(c);
}

static inline void on(ak_view *v, uint16_t ev, ak_invoke_fn f, const void *data) {
	ak_closure *c = fn(f, data);
	ak_ui_on(v, ev, c);
	ak_release(c);
}

static inline void *cell_int(int64_t v) { return ak_vm_cell_new((ak_val){.i = v}, AK_K_INT, NULL).p; }
static inline void *cell_ref(void *p) { return ak_vm_cell_new((ak_val){.p = p}, AK_K_REF, NULL).p; }
static inline int64_t peek_i(void *cell) { return ak_vm_cell_get(cell, false).i; }
static inline void write_i(void *cell, int64_t v) { ak_vm_cell_set(cell, (ak_val){.i = v}); }
static inline void write_p(void *cell, void *p) { ak_vm_cell_set(cell, (ak_val){.p = p}); }  // takes p

static inline ak_val read_cell(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	return ak_vm_cell_get((void *)c->fn, true);
}

static inline ak_uitest *mount(ak_invoke_fn build, const void *data) {
	setup();
	ak_closure *c = fn(build, data);
	ak_uitest *a = ak_uitest_mount(c, NULL);
	ak_release(c);
	return a;
}

static inline const char *cstr(ak_buf *b) {
	ak_buf_u8(b, 0);
	b->len--;
	return (const char *)b->data;
}

// done unmounts the app and checks it leaves nothing: no native view,
// no live view object.
static inline void done(ak_uitest *a) {
	ak_buf err = {0};
	if (!ak_uitest_unmount(a, &err)) T_FAIL("%s", cstr(&err));
	ak_buf_free(&err);
	ak_release(a);
	ak_ui_stats s;
	ak_ui_get_stats(&s);
	if (s.views != 0 || s.native != 0) T_FAIL("%u views, %u native views outlived the app", s.views, s.native);
}

static inline ak_fake_view *root(ak_uitest *a) { return ak_fake_root(ak_uitest_host(a)); }

static inline void check_tree(ak_uitest *a, const char *want) {
	ak_buf b = {0};
	ak_fake_tree(root(a), &b);
	CHECK_EQ_STR(cstr(&b), want);
	ak_buf_free(&b);
}

static inline void check_text(ak_fake_view *v, const char *want) {
	ak_buf b = {0};
	ak_fake_text(v, &b);
	CHECK_EQ_STR(cstr(&b), want);
	ak_buf_free(&b);
}

static inline void dump_log(ak_fake *f) {
	ak_buf b = {0};
	ak_fake_log(f, &b);
	fprintf(stderr, "%s\n", cstr(&b));
	ak_buf_free(&b);
}

static inline ak_fake_view *one(ak_uitest *a, ak_view_kind k, const char *s) { return ak_fake_one(ak_uitest_host(a), k, s); }

static inline ak_fake_view *nth(ak_uitest *a, ak_view_kind k, uint32_t i) {
	ak_fake_view_vec vs = {0};
	ak_fake_of_kind(ak_uitest_host(a), k, &vs);
	ak_fake_view *v = i < vs.len ? vs.data[i] : NULL;
	ak_vec_free(&vs);
	return v;
}

static inline uint32_t count_kind(ak_uitest *a, ak_view_kind k) {
	ak_fake_view_vec vs = {0};
	ak_fake_of_kind(ak_uitest_host(a), k, &vs);
	uint32_t n = vs.len;
	ak_vec_free(&vs);
	return n;
}

static inline void check_frame(const ak_fake_view *v, float x, float y, float w, float h) {
	if (!v) {
		T_FAIL("no view");
		return;
	}
	if (v->frame[0] != x || v->frame[1] != y || v->frame[2] != w || v->frame[3] != h)
		T_FAIL("frame %g,%g %gx%g, want %g,%g %gx%g", v->frame[0], v->frame[1], v->frame[2], v->frame[3], x, y, w, h);
}


// ---- More: events, props ----

static inline ak_val noval(void) { return (ak_val){0}; }

// dispatch sends e to the core as the host does (it needn't listen).
static inline void dispatch(ak_uitest *a, ak_event e) { ak_fake_dispatch(ak_uitest_host(a), &e); }

static inline ak_span bytes_of(const ak_fake_view *v, ak_prop p) { return ak_fake_string(v, p); }

static inline bool has_prop(const ak_fake_view *v, ak_prop p) { return ak_fake_prop_of(v, p) != NULL; }

static inline uint32_t sets(ak_uitest *a) { return ak_fake_count(ak_uitest_host(a), AK_OP_SET_PROP); }

static inline void reset(ak_uitest *a) { ak_fake_reset(ak_uitest_host(a)); }

#endif
