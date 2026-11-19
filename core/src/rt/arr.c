#include "rt/rt.h"

// Sizes are in elements; an element is 1 << a->shift bytes (rt.h's
// ak_elem): what's moved and allocated is scaled by it.

static inline size_t bytes(const ak_arr *a, size_t n) { return n << a->shift; }

static ak_arr *arr_make(ak_kind kind, ak_elem elem, const ak_shape *shape, uint32_t cap) {
	ak_arr *a = ak_obj_new(AK_TAG_ARR, sizeof(ak_arr));
	a->h.aux = (uint16_t)kind;
	a->elem = (uint8_t)elem;
	a->shift = ak_elem_shift(elem);
	a->shape = shape;
	if (cap) {
		a->items = ak_alloc(bytes(a, cap));
		a->cap = cap;
	}
	return a;
}

ak_val ak_arr_get_packed(const ak_arr *a, uint32_t i) { return ak_elem_get(ak_arr_at(a, i), (ak_elem)a->elem); }
void ak_arr_put_packed(ak_arr *a, uint32_t i, ak_val v) { ak_elem_put(ak_arr_at(a, i), (ak_elem)a->elem, v); }

ak_arr *ak_arr_new(ak_kind elem, const ak_shape *shape, uint32_t cap) {
	ak_kind k = (ak_kind)(elem & 0xf);
	ak_elem e = (ak_elem)((unsigned)elem >> 4);
	return arr_make(k, e ? e : k == AK_K_BOOL ? AK_E_BOOL : AK_E_VAL, shape, cap);
}

ak_arr *ak_arr_new_packed(ak_kind kind, ak_elem elem, uint32_t cap) { return arr_make(kind, elem, NULL, cap); }

ak_arr *ak_arr_like(const ak_arr *a, uint32_t cap) {
	ak_arr *c = arr_make(ak_arr_kind(a), (ak_elem)a->elem, a->shape, cap);
	c->h.flags = a->h.flags;
	return c;
}

ak_arr *ak_arr_copy(const ak_arr *a) {
	ak_arr *c = ak_arr_like(a, a->len);
	if (a->len) memcpy(c->items, a->items, bytes(a, a->len));
	c->len = a->len;
	if (ak_arr_kind(a) == AK_K_REF) {
		ak_val *v = ak_arr_vals(c);
		for (uint32_t i = 0; i < c->len; i++) ak_retain(v[i].p);
	}
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
		void *b = ak_arr_base(a);
		memmove(b, a->items, bytes(a, a->len));
		a->items = b;
		a->cap += a->front;
		a->front = 0;
		return;
	}
	if (n > UINT32_MAX / 2 - a->len) ak_panic("out of memory (an array of %u + %u elements)", a->len, n);
	uint32_t c = a->cap ? a->cap * 2 : (a->shift ? 4 : 16);
	while (c < a->len + n) c *= 2;
	char *b = ak_realloc(ak_arr_base(a), bytes(a, (size_t)a->front + c));
	a->items = b + bytes(a, a->front);
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
		memmove((char *)a->items + bytes(a, k), a->items, bytes(a, a->len));
		a->items = (char *)a->items + bytes(a, k);
		a->front = k;
		a->cap -= k;
		return;
	}
	uint32_t k = a->len < 4 ? 4 : a->len;
	char *b = ak_alloc(bytes(a, (size_t)k + a->cap + (a->cap ? 0 : 4)));
	if (a->len) memcpy(b + bytes(a, k), a->items, bytes(a, a->len));
	ak_free(ak_arr_base(a));
	a->items = b + bytes(a, k);
	a->front = k;
	if (!a->cap) a->cap = 4;
}

// ak_arr_reserve makes room for exactly n more at the end (a buffer of
// that size, not twice): what's known to come.
void ak_arr_reserve(ak_arr **ap, uint32_t n) {
	ak_arr *a = ak_arr_mut(ap);
	if (n > UINT32_MAX - a->len) ak_panic("out of memory (an array of %u + %u elements)", a->len, n);
	if (a->len + n <= a->cap) return;
	char *b = ak_realloc(ak_arr_base(a), bytes(a, (size_t)a->front + a->len + n));
	a->items = b + bytes(a, a->front);
	a->cap = a->len + n;
}

void ak_arr_push_slow(ak_arr **ap, ak_val v) {
	ak_arr *a = ak_arr_mut(ap);
	grow(a, 1);
	ak_arr_put(a, a->len++, v);
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
		a->items = (char *)a->items - bytes(a, 1);
		a->front--;
		a->cap++;
		memmove(a->items, ak_arr_at(a, 1), bytes(a, (uint32_t)i));
	} else {
		grow(a, 1);
		memmove(ak_arr_at(a, (uint32_t)i + 1), ak_arr_at(a, (uint32_t)i), bytes(a, a->len - (uint32_t)i));
	}
	ak_arr_put(a, (uint32_t)i, v);
	a->len++;
}

ak_val ak_arr_remove_at(ak_arr **ap, int64_t i) {
	ak_arr *a = ak_arr_mut(ap);
	AK_ASSERT(ak_arr_check(a, i));
	ak_val v = ak_arr_get(a, (uint32_t)i);
	if ((uint32_t)i < a->len / 2 || i == 0) {
		memmove(ak_arr_at(a, 1), a->items, bytes(a, (uint32_t)i));
		a->items = ak_arr_at(a, 1);
		a->front++;
		a->cap--;
	} else {
		memmove(ak_arr_at(a, (uint32_t)i), ak_arr_at(a, (uint32_t)i + 1), bytes(a, a->len - (uint32_t)i - 1));
	}
	a->len--;
	return v;
}

void ak_arr_set(ak_arr **ap, int64_t i, ak_val v) {
	ak_arr *a = ak_arr_mut(ap);
	AK_ASSERT(ak_arr_check(a, i));
	if (ak_arr_kind(a) == AK_K_REF) ak_release(ak_arr_vals(a)[i].p);
	ak_arr_put(a, (uint32_t)i, v);
}

static void retain_all(ak_arr *a) {
	if (ak_arr_kind(a) != AK_K_REF) return;
	ak_val *v = ak_arr_vals(a);
	for (uint32_t i = 0; i < a->len; i++) ak_retain(v[i].p);
}

ak_arr *ak_arr_slice(ak_arr *a, int64_t from, int64_t to) {
	if (from < 0) from = 0;
	if (to > a->len) to = a->len;
	if (from == 0 && to == a->len) return ak_retain(a);
	uint32_t n = from < to ? (uint32_t)(to - from) : 0;
	ak_arr *c = ak_arr_like(a, n);
	if (n) memcpy(c->items, ak_arr_at(a, (uint32_t)from), bytes(a, n));
	c->len = n;
	retain_all(c);
	return c;
}

ak_arr *ak_arr_concat(ak_arr *a, ak_arr *b) {
	if (b->len == 0) return ak_retain(a);
	if (a->len == 0) return ak_retain(b);
	if (b->len > UINT32_MAX - a->len) ak_panic("out of memory (an array of %u + %u elements)", a->len, b->len);
	ak_arr *c = ak_arr_like(a, a->len + b->len);
	memcpy(c->items, a->items, bytes(a, a->len));
	if (b->elem == a->elem) {
		memcpy(ak_arr_at(c, a->len), b->items, bytes(b, b->len));
	} else {
		// Sized integers one made packed, the other as values (a map's
		// keys, a set's elements): b's as a's are.
		for (uint32_t i = 0; i < b->len; i++) ak_arr_put(c, a->len + i, ak_arr_get(b, i));
	}
	c->len = a->len + b->len;
	retain_all(c);
	return c;
}
