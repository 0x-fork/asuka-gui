// The VM's JSON builtins: json.decode(text, desc) and json.encode(value,
// desc, indent), with shapes found in the module that's running.
#include "vm/vm_int.h"
#include "rt/json.h"

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

// Payloadless enums are ints; their case names come as a shape of the
// same name whose cases have no payloads (the compiler writes one for
// each enum JSON may read or write).
static bool cases(void *ctx, const char *name, size_t n, const char *const **out, uint32_t *count) {
	const ak_shape *s = shape(ctx, name, n);
	if (!s || !s->ncases) return false;
	ak_vm_shape *vs = (ak_vm_shape *)s;
	if (!vs->case_names) {
		vs->case_names = ak_alloc(sizeof(char *) * s->ncases);
		for (uint16_t i = 0; i < s->ncases; i++) vs->case_names[i] = s->cases[i].name;
	}
	*out = vs->case_names;
	*count = s->ncases;
	return true;
}

static ak_val decode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *text = args[0].p, *desc = args[1].p;
	ak_json_types t = {shape, cases, running()};
	return ak_json_decode(text->data, text->len, desc->data, &t);
}

static ak_val encode(ak_val *args, uint32_t n) {
	ak_str *desc = args[1].p;
	ak_json_types t = {shape, cases, running()};
	return (ak_val){.p = ak_json_encode(args[0], desc->data, &t, n > 2 ? (int)args[2].i : 0)};
}

void ak_vm_json_init(void) {
	ak_builtin_register("json.decode", decode);
	ak_builtin_register("json.encode", encode);
}
