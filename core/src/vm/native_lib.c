// The VM's builtins for native packages (native/native.h):
//
//	native.new(cls string, iface int, errors [Error]) -> object
//	native.call(obj, method int, args tuple, argsDesc string, retDesc string, errors [Error]) -> result
//	native.start(same) -> Task<result>
//
// and a native package's views (ui/native_view.c), what a `native
// component` of a package lowers to:
//
//	ui.native(type string) -> View
//	ui.native_set(v View, name string, desc string, value)
//	ui.native_bind(v View, name string, desc string, thunk fn() -> value)
//	ui.native_on(v View, name string, desc string, handler fn(params…))
//
// a prop's value encoded by its descriptor; an event's arguments decoded
// by desc, its parameters' descriptors in order.
//
// Descriptors are the module's string constants, which live as long as
// the code that uses them.
#include "vm/vm_int.h"
#include "native/native.h"
#include "ui/ui.h"
#include "signal/signal.h"

static ak_module *running(void) { return ak_vm_running_module(); }

static const ak_shape *shape(void *ctx, const char *name, size_t n) {
	ak_module *m = ctx;
	for (uint32_t i = 0; m && i < m->nshapes; i++) {
		const char *s = m->shapes[i].s.name;
		if (strlen(s) == n && memcmp(s, name, n) == 0) return &m->shapes[i].s;
	}
	return NULL;
}

// The types a module's native calls use, on the module: callbacks that
// outlive the call that registered them keep a pointer to them.
static const ak_json_types *types_of(ak_module *m) {
	static const ak_json_types none = {shape, NULL, NULL};
	if (!m) return &none;
	if (!m->native_types.shape) m->native_types = (ak_json_types){shape, NULL, m};
	return &m->native_types;
}

static ak_val new_(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_str *cls = a[0].p;
	return ak_native_new(cls->data, cls->len, (uint32_t)a[1].i, a[2].p);
}

static ak_val call(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_str *ad = a[3].p, *rd = a[4].p;
	return ak_native_call(a[0].p, (uint32_t)a[1].i, a[2], ad->data, rd->data, a[5].p, types_of(running()));
}

static ak_val start(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_str *ad = a[3].p, *rd = a[4].p;
	return (ak_val){.p = ak_native_start(a[0].p, (uint32_t)a[1].i, a[2], ad->data, rd->data, a[5].p, types_of(running()))};
}

static ak_view *view_arg(ak_val v, const char *name) {
	if (!v.p || ((ak_obj *)v.p)->tag != AK_TAG_VIEW) ak_panic("%s: not a View", name);
	return v.p;
}

static ak_val ui_native(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = ak_ui_native(a[0].p)};
}

static void native_set(ak_view *v, ak_str *name, ak_str *desc, ak_val value) {
	ak_buf b = {0};
	ak_native_encode(&b, value, desc->data, types_of(running()));
	if (!ak_error_pending()) ak_ui_native_set(v, name, b.data, b.len);
	ak_buf_free(&b);
}

static ak_val ui_native_set(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	native_set(view_arg(a[0], "ui.native_set"), a[1].p, a[2].p, a[3]);
	return (ak_val){0};
}

// A prop that follows a closure, in a render effect.
typedef struct {
	ak_view *v;
	ak_str *name, *desc;
	ak_closure *thunk;
	const ak_json_types *types;
	ak_comp_info info;
} native_binding;

static void binding_run(void *ctx) {
	native_binding *b = ctx;
	ak_val v = ak_call(b->thunk, NULL, 0);
	if (ak_error_pending()) {
		void *err = ak_error_take();
		if (ak_is_cancel(err)) ak_release(err);
		else ak_raise(err);
		return;
	}
	ak_buf buf = {0};
	ak_native_encode(&buf, v, b->desc->data, b->types);
	if (!ak_error_pending()) ak_ui_native_set(b->v, b->name, buf.data, buf.len);
	ak_buf_free(&buf);
	ak_release_val(v, ak_native_desc_kind(b->desc->data));
}

static void binding_drop(void *ctx) {
	native_binding *b = ctx;
	ak_release(b->v);
	ak_release(b->name);
	ak_release(b->desc);
	ak_release(b->thunk);
	ak_free(b);
}

static ak_val ui_native_bind(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	native_binding *b = ak_alloc(sizeof *b);
	b->v = ak_retain(view_arg(a[0], "ui.native_bind"));
	b->name = ak_retain(a[1].p);
	b->desc = ak_retain(a[2].p);
	b->thunk = ak_retain(a[3].p);
	b->types = types_of(running());
	b->info = (ak_comp_info){.drop = binding_drop, .label = "native prop"};
	ak_effect_new(binding_run, b, AK_EFFECT_RENDER, &b->info);
	return (ak_val){0};
}

static ak_val ui_native_on(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_ui_native_on(view_arg(a[0], "ui.native_on"), a[1].p, a[2].p, types_of(running()), a[3].p);
	return (ak_val){0};
}

void ak_native_vm_init(void) {
	ak_builtin_register("ui.native", ui_native);
	ak_builtin_register("ui.native_set", ui_native_set);
	ak_builtin_register("ui.native_bind", ui_native_bind);
	ak_builtin_register("ui.native_on", ui_native_on);
	ak_builtin_register("native.new", new_);
	ak_builtin_register("native.call", call);
	ak_builtin_register("native.start", start);
}
