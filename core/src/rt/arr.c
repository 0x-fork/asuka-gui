#include "rt/rt.h"

ak_arr *ak_arr_new(ak_kind elem, const ak_shape *shape, uint32_t cap) {
	ak_arr *a = ak_obj_new(AK_T_ARR, sizeof(ak_arr));
	a->h.aux = (uint16_t)elem;
	a->shape = shape;
	if (cap) {
		a->data = ak_alloc(sizeof(ak_val) * cap);
		a->cap = cap;
	}
	return a;
}

ak_arr *ak_arr_copy(const ak_arr *a) {
	ak_arr *c = ak_arr_new(ak_arr_kind(a), a->shape, a->len);
	c->h.flags = a->h.flags;
	if (a->len) memcpy(c->data, a->data, sizeof(ak_val) * a->len);
	c->len = a->len;
	if (ak_arr_kind(a) == AK_K_REF)
		for (uint32_t i = 0; i < c->len; i++) ak_retain(c->data[i].p);
	return c;
}

ak_arr *ak_arr_mut(ak_arr **ap) {
	ak_arr *a = *ap;
	if (ak_unique(a)) return a;
	ak_arr *c = ak_arr_copy(a);
	ak_release(a);
	*ap = c;
	return c;
}

static void grow(ak_arr *a, uint32_t n) {
	if (a->len + n <= a->cap) return;
	uint32_t c = a->cap ? a->cap * 2 : 4;
	while (c < a->len + n) c *= 2;
	a->data = ak_realloc(a->data, sizeof(ak_val) * c);
	a->cap = c;
}

void ak_arr_push(ak_arr **ap, ak_val v) {
	ak_arr *a = ak_arr_mut(ap);
	grow(a, 1);
	a->data[a->len++] = v;
}

bool ak_arr_check(const ak_arr *a, int64_t i) { return i >= 0 && i < (int64_t)a->len; }

void ak_arr_insert(ak_arr **ap, int64_t i, ak_val v) {
	ak_arr *a = ak_arr_mut(ap);
	if (i < 0) i = 0;
	if (i > a->len) i = a->len;
	grow(a, 1);
	memmove(a->data + i + 1, a->data + i, sizeof(ak_val) * (a->len - (uint32_t)i));
	a->data[i] = v;
	a->len++;
}

ak_val ak_arr_remove_at(ak_arr **ap, int64_t i) {
	ak_arr *a = ak_arr_mut(ap);
	AK_ASSERT(ak_arr_check(a, i));
	ak_val v = a->data[i];
	memmove(a->data + i, a->data + i + 1, sizeof(ak_val) * (a->len - (uint32_t)i - 1));
	a->len--;
	return v;
}

void ak_arr_set(ak_arr **ap, int64_t i, ak_val v) {
	ak_arr *a = ak_arr_mut(ap);
	AK_ASSERT(ak_arr_check(a, i));
	ak_release_val(a->data[i], ak_arr_kind(a));
	a->data[i] = v;
}

ak_arr *ak_arr_slice(ak_arr *a, int64_t from, int64_t to) {
	if (from < 0) from = 0;
	if (to > a->len) to = a->len;
	if (from == 0 && to == a->len) return ak_retain(a);
	uint32_t n = from < to ? (uint32_t)(to - from) : 0;
	ak_arr *c = ak_arr_new(ak_arr_kind(a), a->shape, n);
	c->h.flags = a->h.flags;
	for (uint32_t i = 0; i < n; i++) {
		c->data[i] = a->data[from + i];
		ak_retain_val(c->data[i], ak_arr_kind(a));
	}
	c->len = n;
	return c;
}

ak_arr *ak_arr_concat(ak_arr *a, ak_arr *b) {
	if (b->len == 0) return ak_retain(a);
	if (a->len == 0) return ak_retain(b);
	ak_arr *c = ak_arr_new(ak_arr_kind(a), a->shape, a->len + b->len);
	c->h.flags = a->h.flags;
	memcpy(c->data, a->data, sizeof(ak_val) * a->len);
	memcpy(c->data + a->len, b->data, sizeof(ak_val) * b->len);
	c->len = a->len + b->len;
	if (ak_arr_kind(a) == AK_K_REF)
		for (uint32_t i = 0; i < c->len; i++) ak_retain(c->data[i].p);
	return c;
}
