// builtins.c: the UI runtime as the VM's builtins (CALLB), what the
// compiler lowers views to (lang/LOWERING.md's "Elements", "Control flow
// in views", "Components"). Each borrows its arguments and returns an
// owned result, as every builtin does (vm.h).
//
// Arguments, in register order (kinds as rt.h's: int, float, bool, ref;
// "?" a ref that may be none):
//
//	name           arguments                                          result
//	ui.new         el: int (AK_EL_*)                                  View
//	ui.set         v: View, key: int (AK_KEY_*), value: the key's kind
//	               (ak_key_kinds[key])                                -
//	ui.bind        v: View, key: int, thunk: fn() -> the key's kind   -
//	ui.on          v: View, event: int (AK_EV_*), handler: fn(the
//	               event's params) (async: returns its future)        -
//	ui.add         v: View, child: View?                              -
//	ui.slot        v: View, slot: int (AK_KEY_<EL>_<SLOT>), w: View?   -
//	ui.ref         v: View                                            View (v)
//	ui.fragment    -                                                  View
//	ui.show        cond: fn() -> bool, then: fn() -> View?,
//	               otherwise: (fn() -> View?)?                        View
//	ui.for         items: fn() -> [T], key: (fn(T) -> K)? (none: the
//	               position), row: fn(item: Acc<T>, index: Acc<int>)
//	               -> View?, keyKind: int (K's kind)                  View
//	ui.switch      selector: fn() -> int, build: fn(case: int)
//	               -> View?                                           View
//	ui.dynamic     build: fn() -> View?                               View
//	ui.lazy_for    list: View, items, key, row, keyKind (as ui.for)   -
//	ui.use         key: string (the type's name)                      ref?
//	ui.provide     key: string, value: ref                            -
//	ui.task        body: async fn()                                   -
//	ui.enter       name: string (the component's)                     -
//	ui.leave       -                                                  -
//	ui.focus       v: View                                            -
//	ui.blur        v: View                                            -
//	ui.scroll_to   v: View, x: float, y: float, animated: bool        -
//	ui.environment -                                                  Environment
//	ui.push        build: fn() -> View   (throws: not available yet)  -
//	ui.pop         -                     (likewise)                   -
//	ui.present     build: fn() -> View   (likewise)                   -
//	ui.dismiss     -                     (likewise)                   -
//
// A View is an ak_view (ui.h); Acc<T> an accessor the row reads with
// AGET. Thunks and handlers are closures (rt.h), the VM's or compiled C's.
#include "ui/ui_int.h"

static ak_view *view_arg(ak_val *a, uint32_t n, uint32_t i, const char *name) {
	if (i >= n || !a[i].p || ((ak_obj *)a[i].p)->tag != AK_TAG_VIEW) ak_panic("%s: argument %u isn't a View", name, i);
	return a[i].p;
}

static ak_val none(void) { return (ak_val){0}; }

static ak_val b_new(ak_val *a, uint32_t n) {
	if (n < 1 || a[0].i <= 0 || a[0].i >= AK_EL__COUNT) {
		ak_throw_msg("ui.new: no element %lld", n ? (long long)a[0].i : 0LL);
		return none();
	}
	return (ak_val){.p = ak_ui_new((uint16_t)a[0].i)};
}

static ak_val b_set(ak_val *a, uint32_t n) {
	ak_ui_set(view_arg(a, n, 0, "ui.set"), (uint16_t)a[1].i, a[2]);
	return none();
}

static ak_val b_bind(ak_val *a, uint32_t n) {
	ak_ui_bind(view_arg(a, n, 0, "ui.bind"), (uint16_t)a[1].i, a[2].p);
	return none();
}

static ak_val b_on(ak_val *a, uint32_t n) {
	ak_ui_on(view_arg(a, n, 0, "ui.on"), (uint16_t)a[1].i, a[2].p);
	return none();
}

static ak_val b_add(ak_val *a, uint32_t n) {
	ak_ui_add(view_arg(a, n, 0, "ui.add"), n > 1 ? a[1].p : NULL);
	return none();
}

static ak_val b_slot(ak_val *a, uint32_t n) {
	ak_ui_slot(view_arg(a, n, 0, "ui.slot"), (uint16_t)a[1].i, a[2].p);
	return none();
}

static ak_val b_ref(ak_val *a, uint32_t n) { return (ak_val){.p = ak_ui_ref(view_arg(a, n, 0, "ui.ref"))}; }

static ak_val b_fragment(ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	return (ak_val){.p = ak_ui_fragment()};
}

static ak_val b_show(ak_val *a, uint32_t n) { return (ak_val){.p = ak_ui_show(a[0].p, a[1].p, n > 2 ? a[2].p : NULL)}; }

static ak_val b_for(ak_val *a, uint32_t n) {
	ak_kind k = n > 3 ? (ak_kind)a[3].i : AK_K_INT;
	return (ak_val){.p = ak_ui_for(a[0].p, a[1].p, a[2].p, k)};
}

static ak_val b_switch(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = ak_ui_switch(a[0].p, a[1].p)};
}

static ak_val b_dynamic(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = ak_ui_dynamic(a[0].p)};
}

static ak_val b_lazy_for(ak_val *a, uint32_t n) {
	ak_kind k = n > 4 ? (ak_kind)a[4].i : AK_K_INT;
	ak_ui_lazy_for(view_arg(a, n, 0, "ui.lazy_for"), a[1].p, a[2].p, a[3].p, k);
	return none();
}

static ak_val b_use(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	return ak_ui_use(a[0].p);
}

static ak_val b_provide(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_ui_provide(a[0].p, a[1]);
	return none();
}

static ak_val b_task(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_ui_task(a[0].p);
	return none();
}

static ak_val b_enter(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_ui_enter(a[0].p);
	return none();
}

static ak_val b_leave(ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	ak_ui_leave();
	return none();
}

static ak_val b_focus(ak_val *a, uint32_t n) {
	ak_ui_focus(view_arg(a, n, 0, "ui.focus"));
	return none();
}

static ak_val b_blur(ak_val *a, uint32_t n) {
	ak_ui_blur(view_arg(a, n, 0, "ui.blur"));
	return none();
}

static ak_val b_scroll_to(ak_val *a, uint32_t n) {
	ak_ui_scroll_to(view_arg(a, n, 0, "ui.scroll_to"), (float)a[1].f, (float)a[2].f, a[3].i != 0);
	return none();
}

static ak_val b_environment(ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	return ak_ui_environment_value();
}

static ak_val b_push(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_ui_push(a[0].p);
	return none();
}

static ak_val b_pop(ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	ak_ui_pop();
	return none();
}

static ak_val b_present(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_ui_present(a[0].p);
	return none();
}

static ak_val b_dismiss(ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	ak_ui_dismiss();
	return none();
}

void ak_ui_builtins_init(void) {
	static const struct {
		const char *name;
		ak_builtin_fn fn;
	} t[] = {
		{"ui.new", b_new},
		{"ui.set", b_set},
		{"ui.bind", b_bind},
		{"ui.on", b_on},
		{"ui.add", b_add},
		{"ui.slot", b_slot},
		{"ui.ref", b_ref},
		{"ui.fragment", b_fragment},
		{"ui.show", b_show},
		{"ui.for", b_for},
		{"ui.switch", b_switch},
		{"ui.dynamic", b_dynamic},
		{"ui.lazy_for", b_lazy_for},
		{"ui.use", b_use},
		{"ui.provide", b_provide},
		{"ui.task", b_task},
		{"ui.enter", b_enter},
		{"ui.leave", b_leave},
		{"ui.focus", b_focus},
		{"ui.blur", b_blur},
		{"ui.scroll_to", b_scroll_to},
		{"ui.environment", b_environment},
		{"ui.push", b_push},
		{"ui.pop", b_pop},
		{"ui.present", b_present},
		{"ui.dismiss", b_dismiss},
	};
	for (size_t i = 0; i < AK_LEN(t); i++) ak_builtin_register(t[i].name, t[i].fn);
}
