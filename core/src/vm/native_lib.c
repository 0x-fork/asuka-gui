// The VM's builtins for native packages (native/native.h):
//
//	native.new(cls string, iface int, errors [Error]) -> object
//	native.call(obj, method int, args tuple, argsDesc string, retDesc string, errors [Error]) -> result
//	native.start(same) -> Task<result>
//
// Descriptors are the module's string constants, which live as long as
// the code that uses them.
#include "vm/vm_int.h"
#include "native/native.h"

static ak_module *running(void) {
	ak_co *co = ak_vm_cur ? ak_vm_cur : &ak_vm_main;
	return co->nframes ? co->frames[co->nframes - 1].fn->mod : NULL;
}

static const ak_shape *shape(void *ctx, const char *name, size_t n) {
	ak_module *m = ctx;
	for (uint32_t i = 0; m && i < m->nshapes; i++) {
		const char *s = m->shapes[i].s.name;
		if (strlen(s) == n && memcmp(s, name, n) == 0) return &m->shapes[i].s;
	}
	return NULL;
}

// The types a module's native calls use, kept per module for callbacks
// that outlive the call that registered them.
static ak_json_types *types_of(ak_module *m) {
	static struct {
		ak_module *m;
		ak_json_types t;
	} cache[16];
	for (int i = 0; i < 16; i++)
		if (cache[i].m == m) return &cache[i].t;
	for (int i = 0; i < 16; i++) {
		if (!cache[i].m) {
			cache[i].m = m;
			cache[i].t = (ak_json_types){shape, NULL, m};
			return &cache[i].t;
		}
	}
	cache[0].m = m;
	cache[0].t = (ak_json_types){shape, NULL, m};
	return &cache[0].t;
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

void ak_native_vm_init(void) {
	ak_builtin_register("native.new", new_);
	ak_builtin_register("native.call", call);
	ak_builtin_register("native.start", start);
}
