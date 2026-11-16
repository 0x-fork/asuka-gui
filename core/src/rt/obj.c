#include "rt/rt.h"

#include <stdarg.h>
#include <stdio.h>

void *ak_obj_new(ak_tag tag, size_t size) {
	ak_obj *o = ak_alloc(size);
	o->rc = 1;
	o->tag = (uint8_t)tag;
	return o;
}

void ak_make_immortal(void *p) {
	if (!p) return;
	ak_obj *o = p;
	if (o->tag == AK_TAG_STR && o->rc != AK_RC_IMMORTAL) ak_str_hash((ak_str *)o);
	o->rc = AK_RC_IMMORTAL;
}

static void release_fields(ak_val *f, uint16_t n, const uint8_t *kinds) {
	for (uint16_t i = 0; i < n; i++)
		if (kinds[i] == AK_K_REF) ak_release(f[i].p);
}

void ak_destroy(ak_obj *o) {
	switch ((ak_tag)o->tag) {
	case AK_TAG_STR:
	case AK_TAG_BYTES:
		break;
	case AK_TAG_BOX:
		if (o->aux == AK_K_REF) ak_release(((ak_box *)o)->v.p);  // a var box
		break;
	case AK_TAG_ARR: {
		ak_arr *a = (ak_arr *)o;
		if (ak_arr_kind(a) == AK_K_REF)
			for (uint32_t i = 0; i < a->len; i++) ak_release(a->data[i].p);
		ak_free(a->data);
		break;
	}
	case AK_TAG_MAP: {
		ak_map *m = (ak_map *)o;
		ak_kind kk = (ak_kind)(m->h.aux & 0xf), vk = (ak_kind)(m->h.aux >> 4);
		for (uint32_t i = 0; i < m->len; i++) {
			if (m->dead[i]) continue;
			ak_release_val(m->keys[i], kk);
			ak_release_val(m->vals[i], vk);
		}
		ak_free(m->keys);
		ak_free(m->vals);
		ak_free(m->dead);
		ak_free(m->slots);
		break;
	}
	case AK_TAG_STRUCT: {
		ak_struct *s = (ak_struct *)o;
		release_fields(s->f, s->shape->nfields, s->shape->kinds);
		break;
	}
	case AK_TAG_ENUM: {
		ak_enum *e = (ak_enum *)o;
		const ak_case *c = &e->shape->cases[ak_enum_case(e)];
		release_fields(e->f, c->nfields, c->kinds);
		break;
	}
	case AK_TAG_CLOSURE: {
		ak_closure *c = (ak_closure *)o;
		if (c->capkinds) release_fields(c->caps, c->ncap, c->capkinds);
		if (c->drop) c->drop(c);
		break;
	}
	default: {
		ak_opaque *x = (ak_opaque *)o;
		if (x->drop) x->drop(x);
		break;
	}
	}
	ak_free(o);
}

// ---- Structs, enums, boxes, closures ----

ak_struct *ak_struct_new(const ak_shape *s) {
	ak_struct *o = ak_obj_new(AK_TAG_STRUCT, sizeof(ak_struct) + sizeof(ak_val) * s->nfields);
	o->shape = s;
	return o;
}

ak_struct *ak_struct_mut(ak_struct **sp) {
	ak_struct *s = *sp;
	if (ak_unique(s)) return s;
	ak_struct *c = ak_struct_new(s->shape);
	for (uint16_t i = 0; i < s->shape->nfields; i++) {
		c->f[i] = s->f[i];
		ak_retain_val(c->f[i], (ak_kind)s->shape->kinds[i]);
	}
	ak_release(s);
	*sp = c;
	return c;
}

ak_enum *ak_enum_new(const ak_shape *s, uint16_t ci) {
	AK_ASSERT(ci < s->ncases);
	const ak_case *c = &s->cases[ci];
	if (c->nfields == 0) {
		// Payloadless cases of a payload enum are shared and immortal.
		ak_shape *ms = (ak_shape *)s;
		if (!ms->singletons) ms->singletons = ak_alloc(sizeof(ak_enum *) * s->ncases);
		ak_enum **tab = ms->singletons;
		if (!tab[ci]) {
			ak_enum *e = ak_obj_new(AK_TAG_ENUM, sizeof(ak_enum));
			e->shape = s;
			e->h.aux = ci;
			ak_make_immortal(e);
			tab[ci] = e;
		}
		return tab[ci];
	}
	ak_enum *e = ak_obj_new(AK_TAG_ENUM, sizeof(ak_enum) + sizeof(ak_val) * c->nfields);
	e->shape = s;
	e->h.aux = ci;
	return e;
}

ak_box *ak_box_new(ak_kind k, ak_val v) {
	ak_box *b = ak_obj_new(AK_TAG_BOX, sizeof(ak_box));
	b->h.aux = (uint16_t)k;
	b->v = v;
	return b;
}

ak_closure *ak_closure_new(ak_invoke_fn invoke, const void *fn, uint16_t ncap, const uint8_t *capkinds) {
	ak_closure *c = ak_obj_new(AK_TAG_CLOSURE, sizeof(ak_closure) + sizeof(ak_val) * ncap);
	c->invoke = invoke;
	c->fn = fn;
	c->ncap = ncap;
	c->capkinds = capkinds;
	return c;
}

void *ak_opaque_new(ak_tag tag, size_t size, void (*drop)(ak_opaque *)) {
	ak_opaque *o = ak_obj_new(tag, size);
	o->drop = drop;
	return o;
}

// ---- Equality and hashing ----

static bool fields_eq(const ak_val *a, const ak_val *b, uint16_t n, const uint8_t *kinds) {
	for (uint16_t i = 0; i < n; i++)
		if (!ak_val_eq(a[i], b[i], (ak_kind)kinds[i])) return false;
	return true;
}

bool ak_obj_eq(const ak_obj *a, const ak_obj *b) {
	if (a == b) return true;
	if (!a || !b || a->tag != b->tag) return false;
	switch ((ak_tag)a->tag) {
	case AK_TAG_STR:
		return ak_str_eq((const ak_str *)a, (const ak_str *)b);
	case AK_TAG_BYTES: {
		const ak_bytes *x = (const ak_bytes *)a, *y = (const ak_bytes *)b;
		return x->len == y->len && memcmp(x->data, y->data, x->len) == 0;
	}
	case AK_TAG_BOX: {
		const ak_box *x = (const ak_box *)a, *y = (const ak_box *)b;
		return ak_val_eq(x->v, y->v, (ak_kind)x->h.aux);
	}
	case AK_TAG_ARR: {
		const ak_arr *x = (const ak_arr *)a, *y = (const ak_arr *)b;
		if (x->len != y->len) return false;
		ak_kind k = ak_arr_kind(x);
		for (uint32_t i = 0; i < x->len; i++)
			if (!ak_val_eq(x->data[i], y->data[i], k)) return false;
		return true;
	}
	case AK_TAG_MAP: {
		const ak_map *x = (const ak_map *)a, *y = (const ak_map *)b;
		if (x->live != y->live) return false;
		ak_kind vk = (ak_kind)(x->h.aux >> 4);
		for (uint32_t i = 0; i < x->len; i++) {
			if (x->dead[i]) continue;
			ak_val v;
			if (!ak_map_get(y, x->keys[i], &v) || !ak_val_eq(x->vals[i], v, vk)) return false;
		}
		return true;
	}
	case AK_TAG_STRUCT: {
		const ak_struct *x = (const ak_struct *)a, *y = (const ak_struct *)b;
		return x->shape == y->shape && fields_eq(x->f, y->f, x->shape->nfields, x->shape->kinds);
	}
	case AK_TAG_ENUM: {
		const ak_enum *x = (const ak_enum *)a, *y = (const ak_enum *)b;
		if (x->shape != y->shape || x->h.aux != y->h.aux) return false;
		const ak_case *c = &x->shape->cases[x->h.aux];
		return fields_eq(x->f, y->f, c->nfields, c->kinds);
	}
	default:
		return false;  // identity, checked above
	}
}

bool ak_val_eq(ak_val a, ak_val b, ak_kind k) {
	switch (k) {
	case AK_K_INT:
	case AK_K_BOOL:
		return a.i == b.i;
	case AK_K_FLOAT:
		return a.f == b.f;
	case AK_K_REF:
		return ak_obj_eq(a.p, b.p);
	default:
		return true;
	}
}

static uint64_t mix(uint64_t h, uint64_t v) {
	h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
	return h;
}

uint64_t ak_val_hash(ak_val v, ak_kind k) {
	switch (k) {
	case AK_K_INT:
	case AK_K_BOOL: {
		uint64_t x = (uint64_t)v.i;
		x ^= x >> 33;
		x *= 0xff51afd7ed558ccdull;
		x ^= x >> 33;
		return x;
	}
	case AK_K_FLOAT: {
		double f = v.f == 0 ? 0 : v.f;  // -0 == 0
		uint64_t u;
		memcpy(&u, &f, 8);
		return ak_val_hash((ak_val){.i = (int64_t)u}, AK_K_INT);
	}
	case AK_K_REF: {
		ak_obj *o = v.p;
		if (!o) return 0;
		switch ((ak_tag)o->tag) {
		case AK_TAG_STR:
			return ak_str_hash((ak_str *)o);
		case AK_TAG_STRUCT: {
			ak_struct *s = (ak_struct *)o;
			uint64_t h = 17;
			for (uint16_t i = 0; i < s->shape->nfields; i++) h = mix(h, ak_val_hash(s->f[i], (ak_kind)s->shape->kinds[i]));
			return h;
		}
		case AK_TAG_ENUM: {
			ak_enum *e = (ak_enum *)o;
			const ak_case *c = &e->shape->cases[e->h.aux];
			uint64_t h = mix(31, e->h.aux);
			for (uint16_t i = 0; i < c->nfields; i++) h = mix(h, ak_val_hash(e->f[i], (ak_kind)c->kinds[i]));
			return h;
		}
		case AK_TAG_BOX:
			return ak_val_hash(((ak_box *)o)->v, (ak_kind)o->aux);
		default:
			return (uint64_t)(uintptr_t)o;
		}
	}
	default:
		return 0;
	}
}

// ---- Copies for other threads ----

ak_val ak_deep_copy(ak_val v, ak_kind k) {
	if (k != AK_K_REF || !v.p) return v;
	ak_obj *o = v.p;
	if (o->rc == AK_RC_IMMORTAL && o->tag != AK_TAG_ENUM) return v;  // constants are never freed
	switch ((ak_tag)o->tag) {
	case AK_TAG_STR: {
		ak_str *s = (ak_str *)o;
		return (ak_val){.p = ak_str_new(s->data, s->len)};
	}
	case AK_TAG_BYTES: {
		ak_bytes *b = (ak_bytes *)o;
		return (ak_val){.p = ak_bytes_new(b->data, b->len)};
	}
	case AK_TAG_BOX: {
		// Its value too: a box of a reference holds it.
		ak_box *b = (ak_box *)o;
		ak_box *c = ak_box_new((ak_kind)b->h.aux, ak_deep_copy(b->v, (ak_kind)b->h.aux));
		c->h.flags = b->h.flags;
		return (ak_val){.p = c};
	}
	case AK_TAG_ARR: {
		ak_arr *a = (ak_arr *)o;
		ak_arr *c = ak_arr_new(ak_arr_kind(a), a->shape, a->len);
		c->h.flags = a->h.flags;
		for (uint32_t i = 0; i < a->len; i++) c->data[i] = ak_deep_copy(a->data[i], ak_arr_kind(a));
		c->len = a->len;
		return (ak_val){.p = c};
	}
	case AK_TAG_MAP: {
		ak_map *m = (ak_map *)o;
		ak_kind kk = (ak_kind)(m->h.aux & 0xf), vk = (ak_kind)(m->h.aux >> 4);
		ak_map *c = ak_map_new(kk, m->h.flags & 1, vk, m->vshape);
		for (uint32_t i = 0; i < m->len; i++) {
			if (m->dead[i]) continue;
			ak_map_set(&c, ak_deep_copy(m->keys[i], kk), ak_deep_copy(m->vals[i], vk));
		}
		return (ak_val){.p = c};
	}
	case AK_TAG_STRUCT: {
		ak_struct *s = (ak_struct *)o;
		ak_struct *c = ak_struct_new(s->shape);
		for (uint16_t i = 0; i < s->shape->nfields; i++) c->f[i] = ak_deep_copy(s->f[i], (ak_kind)s->shape->kinds[i]);
		return (ak_val){.p = c};
	}
	case AK_TAG_ENUM: {
		ak_enum *e = (ak_enum *)o;
		const ak_case *cs = &e->shape->cases[e->h.aux];
		if (cs->nfields == 0) return v;  // a shared immortal singleton
		ak_enum *c = ak_enum_new(e->shape, e->h.aux);
		for (uint16_t i = 0; i < cs->nfields; i++) c->f[i] = ak_deep_copy(e->f[i], (ak_kind)cs->kinds[i]);
		return (ak_val){.p = c};
	}
	default:
		ak_panic("ak_deep_copy: a %d object can't cross threads", o->tag);
	}
}

// ---- Constants ----

void ak_freeze(ak_val v, ak_kind k) {
	if (k != AK_K_REF || !v.p) return;
	ak_obj *o = v.p;
	if (o->rc == AK_RC_IMMORTAL) return;
	switch ((ak_tag)o->tag) {
	case AK_TAG_ARR: {
		ak_arr *a = (ak_arr *)o;
		for (uint32_t i = 0; i < a->len; i++) ak_freeze(a->data[i], ak_arr_kind(a));
		break;
	}
	case AK_TAG_MAP: {
		ak_map *m = (ak_map *)o;
		for (uint32_t i = 0; i < m->len; i++) {
			if (m->dead[i]) continue;
			ak_freeze(m->keys[i], (ak_kind)(m->h.aux & 0xf));
			ak_freeze(m->vals[i], (ak_kind)(m->h.aux >> 4));
		}
		break;
	}
	case AK_TAG_STRUCT: {
		ak_struct *s = (ak_struct *)o;
		for (uint16_t i = 0; i < s->shape->nfields; i++) ak_freeze(s->f[i], (ak_kind)s->shape->kinds[i]);
		break;
	}
	case AK_TAG_ENUM: {
		ak_enum *e = (ak_enum *)o;
		const ak_case *c = &e->shape->cases[e->h.aux];
		for (uint16_t i = 0; i < c->nfields; i++) ak_freeze(e->f[i], (ak_kind)c->kinds[i]);
		break;
	}
	case AK_TAG_BOX:
		ak_freeze(((ak_box *)o)->v, (ak_kind)o->aux);
		break;
	default:
		break;
	}
	ak_make_immortal(o);
}

// ---- Errors ----

static AK_TLS void *pending_error;

static const uint8_t error_kinds[] = {AK_K_REF};
static const char *const error_names[] = {"message"};
const ak_shape ak_shape_error = {
	.name = "Error",
	.nfields = 1,
	.kinds = error_kinds,
	.names = error_names,
	.is_error = true,
};

static const ak_shape cancel_shape = {
	.name = "Cancelled",
	.nfields = 1,
	.kinds = error_kinds,
	.names = error_names,
	.is_error = true,
};

void ak_throw(void *err) {
	if (pending_error) ak_release(pending_error);
	pending_error = err;
}

void ak_throw_msg(const char *fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof buf) n = sizeof buf - 1;
	ak_struct *e = ak_struct_new(&ak_shape_error);
	e->f[0].p = ak_str_new(buf, (size_t)n);
	ak_throw(e);
}

void ak_throw_cancel(void) {
	static ak_struct *cancel;
	if (!cancel) {
		cancel = ak_struct_new(&cancel_shape);
		cancel->f[0].p = ak_str_c("cancelled");
		ak_make_immortal(cancel->f[0].p);
		ak_make_immortal(cancel);
	}
	ak_throw(cancel);
}

bool ak_error_pending(void) { return pending_error != NULL; }

void *ak_error_take(void) {
	void *e = pending_error;
	pending_error = NULL;
	return e;
}

bool ak_is_cancel(const void *err) {
	return err && ((const ak_obj *)err)->tag == AK_TAG_STRUCT && ((const ak_struct *)err)->shape == &cancel_shape;
}

ak_str *ak_error_message(const void *err) {
	const ak_obj *o = err;
	if (o && o->tag == AK_TAG_STRUCT) {
		const ak_struct *s = err;
		if (s->shape->nfields > 0 && s->shape->kinds[0] == AK_K_REF && s->shape->names && strcmp(s->shape->names[0], "message") == 0) {
			const ak_obj *m = s->f[0].p;
			if (m && m->tag == AK_TAG_STR) return (ak_str *)m;
		}
	}
	if (o && o->tag == AK_TAG_STR) return (ak_str *)o;
	return ak_str_empty();
}
