#include "rt/rt.h"

ak_arr *ak_arr_new(ak_kind elem, const ak_shape *shape, uint32_t cap) {
	ak_arr *a = ak_obj_new(AK_TAG_ARR, sizeof(ak_arr));
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

// grow makes room for n more elements at the end: the free slots at the
// front, when they're as many as the elements and the n (a queue's,
// popped at the front), slid to the end; else a buffer twice as big.
static void grow(ak_arr *a, uint32_t n) {
	if (a->len + n <= a->cap) return;
	if (a->front && a->front >= a->len + n) {
		ak_val *b = ak_arr_base(a);
		memmove(b, a->data, sizeof(ak_val) * a->len);
		a->data = b;
		a->cap += a->front;
		a->front = 0;
		return;
	}
	uint32_t c = a->cap ? a->cap * 2 : 4;
	while (c < a->len + n) c *= 2;
	ak_val *b = ak_realloc(a->data ? ak_arr_base(a) : NULL, sizeof(ak_val) * ((size_t)a->front + c));
	a->data = b + a->front;
	a->cap = c;
}

// grow_front makes room for one more element at the start: half the
// free slots at the end, when they're as many as the elements; else a
// buffer with as many free slots at the front as there are elements.
static void grow_front(ak_arr *a) {
	if (a->front) return;
	uint32_t free = a->cap - a->len;
	if (free >= 4 && free >= a->len) {
		uint32_t k = (free + 1) / 2;
		memmove(a->data + k, a->data, sizeof(ak_val) * a->len);
		a->data += k;
		a->front = k;
		a->cap -= k;
		return;
	}
	uint32_t k = a->len < 4 ? 4 : a->len;
	ak_val *b = ak_alloc(sizeof(ak_val) * ((size_t)k + a->cap + (a->cap ? 0 : 4)));
	if (a->len) memcpy(b + k, a->data, sizeof(ak_val) * a->len);
	ak_free(a->data ? ak_arr_base(a) : NULL);
	a->data = b + k;
	a->front = k;
	if (!a->cap) a->cap = 4;
}

void ak_arr_push(ak_arr **ap, ak_val v) {
	ak_arr *a = ak_arr_mut(ap);
	grow(a, 1);
	a->data[a->len++] = v;
}

bool ak_arr_check(const ak_arr *a, int64_t i) { return i >= 0 && i < (int64_t)a->len; }

// An insertion or a removal moves the elements on its shorter side: at
// either end, none.
void ak_arr_insert(ak_arr **ap, int64_t i, ak_val v) {
	ak_arr *a = ak_arr_mut(ap);
	if (i < 0) i = 0;
	if (i > a->len) i = a->len;
	if ((uint32_t)i < a->len / 2 || (i == 0 && a->len)) {
		grow_front(a);
		a->data--;
		a->front--;
		a->cap++;
		memmove(a->data, a->data + 1, sizeof(ak_val) * (uint32_t)i);
	} else {
		grow(a, 1);
		memmove(a->data + i + 1, a->data + i, sizeof(ak_val) * (a->len - (uint32_t)i));
	}
	a->data[i] = v;
	a->len++;
}

ak_val ak_arr_remove_at(ak_arr **ap, int64_t i) {
	ak_arr *a = ak_arr_mut(ap);
	AK_ASSERT(ak_arr_check(a, i));
	ak_val v = a->data[i];
	if ((uint32_t)i < a->len / 2 || i == 0) {
		memmove(a->data + 1, a->data, sizeof(ak_val) * (uint32_t)i);
		a->data++;
		a->front++;
		a->cap--;
	} else {
		memmove(a->data + i, a->data + i + 1, sizeof(ak_val) * (a->len - (uint32_t)i - 1));
	}
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
