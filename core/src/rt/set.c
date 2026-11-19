#include "rt/rt.h"

// Sets: maps from their elements to nothing (rt.h). The map keeps the
// order and the index; a set's values are void (zeros nothing reads or
// releases), and inserting an element it has changes nothing (the
// element it holds stays, as Swift's insert keeps the member).

ak_map *ak_set_new(ak_kind elem, bool string_elems) {
	ak_map *s = ak_map_new(elem, string_elems, AK_K_VOID, NULL);
	s->h.flags |= AK_MAP_SET;
	return s;
}

static ak_kind elem_kind(const ak_map *s) { return (ak_kind)(s->h.aux & 0xf); }

// like is a new empty set of s's elements.
static ak_map *like(const ak_map *s) { return ak_set_new(elem_kind(s), s->h.flags & AK_MAP_STRKEYS); }

bool ak_set_has(const ak_map *s, ak_val x) {
	ak_val v;
	return ak_map_get(s, x, &v);
}

bool ak_set_insert(ak_map **s, ak_val x) {
	if (!ak_unique(*s) && ak_set_has(*s, x)) {  // what a shared set has: no copy
		ak_release_val(x, elem_kind(*s));
		return false;
	}
	return ak_map_put_new(s, x, (ak_val){0});
}

bool ak_set_remove(ak_map **s, ak_val x) { return ak_map_delete(s, x); }

// add puts a borrowed element in r, unless it has it.
static void add(ak_map **r, ak_val x) {
	ak_retain_val(x, elem_kind(*r));
	ak_map_put_new(r, x, (ak_val){0});
}

ak_map *ak_set_union(const ak_map *a, const ak_map *b) {
	ak_map *r = like(a);
	for (uint32_t i = 0; i < a->len; i++)
		if (!a->dead[i]) add(&r, a->keys[i]);
	for (uint32_t i = 0; i < b->len; i++)
		if (!b->dead[i]) add(&r, b->keys[i]);
	return r;
}

ak_map *ak_set_intersection(const ak_map *a, const ak_map *b) {
	ak_map *r = like(a);
	for (uint32_t i = 0; i < a->len; i++)
		if (!a->dead[i] && ak_set_has(b, a->keys[i])) add(&r, a->keys[i]);
	return r;
}

ak_map *ak_set_subtracting(const ak_map *a, const ak_map *b) {
	ak_map *r = like(a);
	for (uint32_t i = 0; i < a->len; i++)
		if (!a->dead[i] && !ak_set_has(b, a->keys[i])) add(&r, a->keys[i]);
	return r;
}

bool ak_set_is_subset(const ak_map *a, const ak_map *b) {
	if (a->live > b->live) return false;
	for (uint32_t i = 0; i < a->len; i++)
		if (!a->dead[i] && !ak_set_has(b, a->keys[i])) return false;
	return true;
}

ak_map *ak_set_from_arr(const ak_arr *a) {
	ak_map *r = ak_set_new(ak_arr_kind(a), a->h.flags & 1);
	for (uint32_t i = 0; i < a->len; i++) add(&r, ak_arr_get(a, i));
	return r;
}
