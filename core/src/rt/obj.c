#include "rt/rt.h"

#include <stdarg.h>
#include <stdio.h>

int64_t ak_obj_live;

void *ak_obj_new(ak_tag tag, size_t size) {
	__atomic_add_fetch(&ak_obj_live, 1, __ATOMIC_RELAXED);
	ak_obj *o = ak_alloc(size);
	o->rc = 1;
	o->tag = (uint8_t)tag;
	return o;
}

void ak_make_immortal(void *p) {
	if (!p) return;
	ak_obj *o = p;
	if (o->rc == AK_RC_IMMORTAL) return;
	if (o->tag == AK_TAG_STR) ak_str_hash((ak_str *)o);
	o->rc = AK_RC_IMMORTAL;
	// A constant, never destroyed: not one ak_obj_live counts.
	__atomic_sub_fetch(&ak_obj_live, 1, __ATOMIC_RELAXED);
}

// ---- Walking values ----
//
// A value nests as deep as an app makes it (a list as an enum, built in
// a loop a million links long), so nothing that walks one recurses on
// the C stack: a spawn's thread has 512 KB of it, iOS's main thread
// 1 MB. Destroying a value keeps its way back in the dying objects
// themselves (no memory at all); comparing, hashing, copying and
// freezing keep the objects they're inside of on a walk's stack, whose
// first frames are on the C stack (a value nested less deep than that
// walks without allocating) and which grows on the heap past them. An
// object whose values are plain (scalars, strings, none) is settled as
// it's entered, without a frame, and one the walk is done with once it
// goes into its last reference gives its frame to that one, so a chain
// (a list's tail, a struct's next) takes one frame however long it is.

// A walk's place in an object: its values (an array's elements, a map's
// entries' values, a struct's or an enum case's fields, a box's value, a
// closure's captures), up to its last reference. A map's keys are ints
// or strings, which hold nothing: a walk doesn't go into them.
typedef struct {
	ak_obj *a;
	ak_obj *b;             // comparing: the other one; copying: the copy
	ak_val *x, *y;         // a's values, b's (NULL for a map: by key)
	const uint8_t *kinds;  // their kinds, or NULL: all references,
	const uint8_t *dead;   // but for a map's deleted entries
	uint32_t i, end;       // the next value, past the last reference
} walk_frame;

static uint32_t last_ref(const uint8_t *kinds, uint32_t n) {
	while (n > 0 && kinds[n - 1] != AK_K_REF) n--;
	return n;
}

static uint32_t map_end(const ak_map *m) {
	if ((m->h.aux >> 4) != AK_K_REF) return 0;
	uint32_t n = m->len;
	while (n > 0 && m->dead[n - 1]) n--;
	return n;
}

// frame_at sets f at the start of a's values, and returns past its last
// reference: 0 when it holds none.
static AK_ALWAYS_INLINE uint32_t frame_at(walk_frame *f, ak_obj *a) {
	*f = (walk_frame){.a = a};
	switch ((ak_tag)a->tag) {
	case AK_TAG_BOX:
		f->x = &((ak_box *)a)->v;
		return f->end = a->aux == AK_K_REF;
	case AK_TAG_ARR: {
		ak_arr *r = (ak_arr *)a;
		f->x = r->data;
		return f->end = ak_arr_kind(r) == AK_K_REF ? r->len : 0;
	}
	case AK_TAG_MAP: {
		ak_map *m = (ak_map *)a;
		f->x = m->vals;
		f->dead = m->dead;
		return f->end = map_end(m);
	}
	case AK_TAG_STRUCT: {
		ak_struct *s = (ak_struct *)a;
		f->x = s->f;
		f->kinds = s->shape->kinds;
		return f->end = last_ref(f->kinds, s->shape->nfields);
	}
	case AK_TAG_ENUM: {
		ak_enum *e = (ak_enum *)a;
		const ak_case *c = &e->shape->cases[e->h.aux];
		f->x = e->f;
		f->kinds = c->kinds;
		return f->end = last_ref(c->kinds, c->nfields);
	}
	case AK_TAG_CLOSURE: {
		ak_closure *c = (ak_closure *)a;
		f->x = c->caps;
		f->kinds = c->capkinds;
		return f->end = c->capkinds ? last_ref(c->capkinds, c->ncap) : 0;
	}
	default:
		return 0;
	}
}

// values is o's values (as frame_at's x): NULL for a map's, which are
// reached by key.
static ak_val *values(ak_obj *o) {
	switch ((ak_tag)o->tag) {
	case AK_TAG_BOX: return &((ak_box *)o)->v;
	case AK_TAG_ARR: return ((ak_arr *)o)->data;
	case AK_TAG_STRUCT: return ((ak_struct *)o)->f;
	case AK_TAG_ENUM: return ((ak_enum *)o)->f;
	default: return NULL;
	}
}

// skip reports whether f's i'th value isn't a reference to walk.
static inline bool skip(const walk_frame *f, uint32_t i) {
	return f->kinds ? f->kinds[i] != AK_K_REF : f->dead && f->dead[i];
}

// holds_values reports whether o is a value a walk goes into
// (comparing, hashing, copying, freezing): an array, a map, a struct,
// an enum or a box. A closure or a runtime object is compared and
// hashed by identity, never copied, and frozen without what it holds.
static bool holds_values(const ak_obj *o) {
	switch ((ak_tag)o->tag) {
	case AK_TAG_BOX:
	case AK_TAG_ARR:
	case AK_TAG_MAP:
	case AK_TAG_STRUCT:
	case AK_TAG_ENUM:
		return true;
	default:
		return false;
	}
}

#define WALK_FRAMES 16

// A walk's stack: the objects it's inside of but the one it's in, whose
// frame the walk keeps itself.
typedef struct {
	walk_frame *f;
	uint32_t len, cap;
	walk_frame mem[WALK_FRAMES];
} walk;

static void walk_init(walk *w) {
	w->f = w->mem;
	w->len = 0;
	w->cap = WALK_FRAMES;
}

// walk_push keeps f, the frame of the object the walk leaves for one
// it holds, to go back to: unless that was its last reference, which
// the walk is done with as it goes in.
static inline void walk_push(walk *w, const walk_frame *f) {
	if (f->i == f->end) return;
	if (AK_UNLIKELY(w->len == w->cap)) {
		bool heap = w->f != w->mem;
		walk_frame *s = ak_vec_grow(heap ? w->f : NULL, &w->cap, w->len, 1, sizeof *s);
		if (!heap) memcpy(s, w->mem, sizeof w->mem);
		w->f = s;
	}
	w->f[w->len++] = *f;
}

// walk_pop goes back to the last frame kept: false when there's none,
// the walk over.
static inline bool walk_pop(walk *w, walk_frame *f) {
	if (w->len == 0) return false;
	*f = w->f[--w->len];
	return true;
}

static void walk_done(walk *w) {
	if (w->f != w->mem) ak_free(w->f);
}

// ---- Destroying ----

// free_mem frees o's memory: its buffers and itself. What it held is
// released already.
static void free_mem(ak_obj *o) {
	switch ((ak_tag)o->tag) {
	case AK_TAG_ARR:
		ak_free(((ak_arr *)o)->data);
		break;
	case AK_TAG_MAP: {
		ak_map *m = (ak_map *)o;
		ak_free(m->keys);
		ak_free(m->vals);
		ak_free(m->dead);
		ak_free(m->slots);
		break;
	}
	default:
		break;
	}
	ak_free(o);
}

// finish destroys o, whose values are released: its map's keys, its
// drop, its memory.
static void finish(ak_obj *o) {
	switch ((ak_tag)o->tag) {
	case AK_TAG_STR:
	case AK_TAG_BYTES:
	case AK_TAG_BOX:
	case AK_TAG_ARR:
	case AK_TAG_STRUCT:
	case AK_TAG_ENUM:
		break;
	case AK_TAG_MAP: {
		ak_map *m = (ak_map *)o;
		if ((m->h.aux & 0xf) == AK_K_REF)
			for (uint32_t i = 0; i < m->len; i++)
				if (!m->dead[i]) ak_release(m->keys[i].p);
		break;
	}
	case AK_TAG_CLOSURE: {
		ak_closure *c = (ak_closure *)o;
		if (c->drop) c->drop(c);
		break;
	}
	default: {
		ak_opaque *x = (ak_opaque *)o;
		if (x->drop) x->drop(x);
		break;
	}
	}
	__atomic_sub_fetch(&ak_obj_live, 1, __ATOMIC_RELAXED);
	free_mem(o);
}

static inline bool is_leaf(const ak_obj *o) { return o->tag == AK_TAG_STR || o->tag == AK_TAG_BYTES; }

// free_leaf destroys a string or bytes, which hold nothing.
static inline void free_leaf(ak_obj *o) {
	__atomic_sub_fetch(&ak_obj_live, 1, __ATOMIC_RELAXED);
	ak_free(o);
}

// ak_destroy frees o and releases what it holds, destroying each of
// those its release leaves unheld, without recursing. An object it goes
// into another from keeps the way back itself: its count, unused once
// it's dying, is where its walk is (the index of its next value), and
// the slot of the value it went into holds the object it's inside of.
// One whose last reference that was needs no way back: a value is
// finished as the walk goes into its last.
void ak_destroy(ak_obj *o) {
	if (is_leaf(o)) {
		free_leaf(o);
		return;
	}
	ak_obj *up = NULL;
	walk_frame f;
	frame_at(&f, o);
	uint32_t i = 0;  // o's next value
	for (;;) {
		if (i < f.end) {
			uint32_t at = i++;
			ak_obj *c = f.x[at].p;
			if (skip(&f, at) || !c || c->rc == AK_RC_IMMORTAL || --c->rc != 0) continue;
			if (is_leaf(c)) {
				free_leaf(c);
				continue;
			}
			walk_frame g;
			if (frame_at(&g, c) == 0) {
				finish(c);  // holds nothing: no way back needed
				continue;
			}
			if (i == f.end && holds_values(o)) {
				// o's last: done with o (a value, which has no drop to
				// run after what it holds is gone), so no way back to it.
				o->rc = 0;
				finish(o);
			} else {
				o->rc = i;
				f.x[at].p = up;
				up = o;
			}
			o = c;
			f = g;
			i = 0;
			continue;
		}
		o->rc = 0;  // as a drop sees it
		finish(o);
		if (!up) return;
		o = up;
		frame_at(&f, o);
		i = o->rc;
		up = f.x[i - 1].p;
	}
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
		// A shape is shared by the threads that run its module's code
		// (spawn's workers), so its table and each case's value are put
		// there once, whole, whichever thread asks first.
		ak_shape *ms = (ak_shape *)s;
		void **tab = ak_constant_get(&ms->singletons);
		if (!tab) {
			void **made = ak_alloc(sizeof(ak_enum *) * s->ncases);
			if (__atomic_compare_exchange_n(&ms->singletons, (void **)&tab, made, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) tab = made;
			else ak_free(made);
		}
		ak_enum *e = ak_constant_get(&tab[ci]);
		if (e) return e;
		e = ak_obj_new(AK_TAG_ENUM, sizeof(ak_enum));
		e->shape = s;
		e->h.aux = ci;
		return ak_constant_put(&tab[ci], e);
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

static inline bool scalar_eq(ak_val a, ak_val b, ak_kind k) {
	switch (k) {
	case AK_K_INT:
	case AK_K_BOOL:
		return a.i == b.i;
	case AK_K_FLOAT:
		return a.f == b.f;
	default:
		return true;
	}
}

// fields_eq compares two structs' or enum cases' scalar fields, and
// sets f at their references.
static bool fields_eq(walk_frame *f, ak_val *a, ak_val *b, uint32_t n, const uint8_t *kinds) {
	uint32_t end = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (kinds[i] == AK_K_REF) end = i + 1;
		else if (!scalar_eq(a[i], b[i], (ak_kind)kinds[i])) return false;
	}
	f->x = a;
	f->y = b;
	f->kinds = kinds;
	f->end = end;
	return true;
}

// eq_enter compares a and b (neither NULL, nor the same object) but for
// the references they hold, and sets f at those for the walk to
// compare: whether they're the same kind of object, of the same length,
// shape and case, and the scalars they hold; strings and bytes whole.
static AK_ALWAYS_INLINE bool eq_enter(walk_frame *f, ak_obj *a, ak_obj *b) {
	*f = (walk_frame){.a = a, .b = b};
	if (a->tag != b->tag) return false;
	switch ((ak_tag)a->tag) {
	case AK_TAG_STR:
		return ak_str_eq((const ak_str *)a, (const ak_str *)b);
	case AK_TAG_BYTES: {
		const ak_bytes *x = (const ak_bytes *)a, *y = (const ak_bytes *)b;
		return x->len == y->len && memcmp(x->data, y->data, x->len) == 0;
	}
	case AK_TAG_BOX: {
		ak_box *x = (ak_box *)a, *y = (ak_box *)b;
		if (x->h.aux != AK_K_REF) return scalar_eq(x->v, y->v, (ak_kind)x->h.aux);
		f->x = &x->v;
		f->y = &y->v;
		f->end = 1;
		return true;
	}
	case AK_TAG_ARR: {
		ak_arr *x = (ak_arr *)a, *y = (ak_arr *)b;
		if (x->len != y->len) return false;
		ak_kind k = ak_arr_kind(x);
		if (k == AK_K_REF) {
			f->x = x->data;
			f->y = y->data;
			f->end = x->len;
			return true;
		}
		for (uint32_t i = 0; i < x->len; i++)
			if (!scalar_eq(x->data[i], y->data[i], k)) return false;
		return true;
	}
	case AK_TAG_MAP: {
		ak_map *x = (ak_map *)a, *y = (ak_map *)b;
		if (x->live != y->live) return false;
		ak_kind vk = (ak_kind)(x->h.aux >> 4);
		if (vk == AK_K_REF) {
			// Each value is compared with the one b has for its key.
			f->x = x->vals;
			f->dead = x->dead;
			f->end = map_end(x);
			return true;
		}
		for (uint32_t i = 0; i < x->len; i++) {
			if (x->dead[i]) continue;
			ak_val v;
			if (!ak_map_get(y, x->keys[i], &v) || !scalar_eq(x->vals[i], v, vk)) return false;
		}
		return true;
	}
	case AK_TAG_STRUCT: {
		ak_struct *x = (ak_struct *)a, *y = (ak_struct *)b;
		return x->shape == y->shape && fields_eq(f, x->f, y->f, x->shape->nfields, x->shape->kinds);
	}
	case AK_TAG_ENUM: {
		ak_enum *x = (ak_enum *)a, *y = (ak_enum *)b;
		if (x->shape != y->shape || x->h.aux != y->h.aux) return false;
		const ak_case *c = &x->shape->cases[x->h.aux];
		return fields_eq(f, x->f, y->f, c->nfields, c->kinds);
	}
	default:
		return false;  // identity, which isn't
	}
}

// What eq_next found.
enum { NEXT_DONE, NEXT_ENTER, NEXT_UNEQUAL };

// eq_next compares f's next pairs of values that are plainly equal or
// not (the same object, none, strings), up to one to go into: in *a and
// *b (NEXT_ENTER), or f's end (NEXT_DONE), or one that differs
// (NEXT_UNEQUAL).
static AK_ALWAYS_INLINE int eq_next(walk_frame *f, ak_obj **a, ak_obj **b) {
	while (f->i < f->end) {
		uint32_t i = f->i++;
		if (skip(f, i)) continue;
		ak_obj *c = f->x[i].p, *d;
		if (f->y) {
			d = f->y[i].p;
		} else {
			ak_val v;
			if (!ak_map_get((const ak_map *)f->b, ((const ak_map *)f->a)->keys[i], &v)) return NEXT_UNEQUAL;
			d = v.p;
		}
		if (c == d) continue;
		if (!c || !d) return NEXT_UNEQUAL;
		if (c->tag == AK_TAG_STR && d->tag == AK_TAG_STR) {
			if (!ak_str_eq((const ak_str *)c, (const ak_str *)d)) return NEXT_UNEQUAL;
			continue;
		}
		*a = c;
		*b = d;
		return NEXT_ENTER;
	}
	return NEXT_DONE;
}

// eq_walk goes on comparing from top, the frame of the object compared
// so far, into a and b, the pair it holds next.
static AK_NOINLINE bool eq_walk(const walk_frame *top, ak_obj *a, ak_obj *b) {
	walk w;
	walk_init(&w);
	walk_frame f = *top;
	int next;
	for (;;) {
		// Into a and b: settled here when what they hold is plain, and
		// the walk's next frame when it isn't.
		walk_frame g;
		if (!eq_enter(&g, a, b)) {
			next = NEXT_UNEQUAL;
			break;
		}
		next = eq_next(&g, &a, &b);
		if (next == NEXT_UNEQUAL) break;
		if (next == NEXT_ENTER) {
			walk_push(&w, &f);
			f = g;
			continue;
		}
		while ((next = eq_next(&f, &a, &b)) == NEXT_DONE && walk_pop(&w, &f)) {}
		if (next != NEXT_ENTER) break;
	}
	walk_done(&w);
	return next == NEXT_DONE;
}

bool ak_obj_eq(const ak_obj *a, const ak_obj *b) {
	if (a == b) return true;
	if (!a || !b || a->tag != b->tag) return false;
	if (a->tag == AK_TAG_STR) return ak_str_eq((const ak_str *)a, (const ak_str *)b);
	// What a and b hold, here while it's scalars and strings, the common
	// case; the walk from the first pair that holds more.
	walk_frame f;
	if (!eq_enter(&f, (ak_obj *)a, (ak_obj *)b)) return false;
	ak_obj *c, *d;
	switch (eq_next(&f, &c, &d)) {
	case NEXT_DONE:
		return true;
	case NEXT_ENTER:
		return eq_walk(&f, c, d);
	default:
		return false;
	}
}

bool ak_val_eq(ak_val a, ak_val b, ak_kind k) {
	if (k == AK_K_REF) return ak_obj_eq(a.p, b.p);
	return scalar_eq(a, b, k);
}

static inline uint64_t mix(uint64_t h, uint64_t v) {
	h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
	return h;
}

static inline uint64_t hash_int(uint64_t x) {
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	return x;
}

static inline uint64_t hash_scalar(ak_val v, ak_kind k) {
	switch (k) {
	case AK_K_INT:
	case AK_K_BOOL:
		return hash_int((uint64_t)v.i);
	case AK_K_FLOAT: {
		double f = v.f == 0 ? 0 : v.f;  // -0 == 0
		uint64_t u;
		memcpy(&u, &f, 8);
		return hash_int(u);
	}
	default:
		return 0;
	}
}

// fields_hash mixes a struct's or enum case's scalar fields into h, and
// sets f at its references.
static uint64_t fields_hash(walk_frame *f, uint64_t h, ak_val *x, uint32_t n, const uint8_t *kinds) {
	uint32_t end = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (kinds[i] == AK_K_REF) end = i + 1;
		else h = mix(h, hash_scalar(x[i], (ak_kind)kinds[i]));
	}
	f->x = x;
	f->kinds = kinds;
	f->end = end;
	return h;
}

// hash_enter mixes into h what o (not NULL) is but for the references
// it holds, and sets f at those for the walk to mix in after it, in
// order: equal values mix in the same. A map's keys stand for it (in
// any order, as == compares it); a closure or a runtime object is its
// identity.
static AK_ALWAYS_INLINE uint64_t hash_enter(walk_frame *f, uint64_t h, ak_obj *o) {
	*f = (walk_frame){.a = o};
	switch ((ak_tag)o->tag) {
	case AK_TAG_STR:
		return mix(h, ak_str_hash((ak_str *)o));
	case AK_TAG_BYTES: {
		const ak_bytes *b = (const ak_bytes *)o;
		uint64_t x = 0xcbf29ce484222325ull;  // FNV-1a
		for (uint32_t i = 0; i < b->len; i++) x = (x ^ b->data[i]) * 0x100000001b3ull;
		return mix(h, x);
	}
	case AK_TAG_BOX: {
		ak_box *b = (ak_box *)o;
		if (b->h.aux != AK_K_REF) return mix(h, hash_scalar(b->v, (ak_kind)b->h.aux));
		f->x = &b->v;
		f->end = 1;
		return mix(h, 7);
	}
	case AK_TAG_ARR: {
		ak_arr *a = (ak_arr *)o;
		h = mix(mix(h, 13), a->len);
		ak_kind k = ak_arr_kind(a);
		if (k == AK_K_REF) {
			f->x = a->data;
				f->end = a->len;
			return h;
		}
		for (uint32_t i = 0; i < a->len; i++) h = mix(h, hash_scalar(a->data[i], k));
		return h;
	}
	case AK_TAG_MAP: {
		const ak_map *m = (const ak_map *)o;
		ak_kind kk = (ak_kind)(m->h.aux & 0xf);
		uint64_t keys = 0;
		for (uint32_t i = 0; i < m->len; i++)
			if (!m->dead[i]) keys += ak_val_hash(m->keys[i], kk);
		return mix(mix(mix(h, 11), m->live), keys);
	}
	case AK_TAG_STRUCT: {
		ak_struct *s = (ak_struct *)o;
		return fields_hash(f, mix(h, 17), s->f, s->shape->nfields, s->shape->kinds);
	}
	case AK_TAG_ENUM: {
		ak_enum *e = (ak_enum *)o;
		const ak_case *c = &e->shape->cases[e->h.aux];
		return fields_hash(f, mix(mix(h, 31), e->h.aux), e->f, c->nfields, c->kinds);
	}
	default:
		return mix(h, (uint64_t)(uintptr_t)o);
	}
}

// hash_next mixes into *h f's next values that are none or strings, up
// to one to go into (true, in *o), or f's end.
static AK_ALWAYS_INLINE bool hash_next(walk_frame *f, uint64_t *h, ak_obj **o) {
	while (f->i < f->end) {
		uint32_t i = f->i++;
		if (skip(f, i)) continue;
		ak_obj *c = f->x[i].p;
		if (!c) {
			*h = mix(*h, 0);
		} else if (c->tag == AK_TAG_STR) {
			*h = mix(*h, ak_str_hash((ak_str *)c));
		} else {
			*o = c;
			return true;
		}
	}
	return false;
}

// hash_walk goes on hashing from top, the frame of the object hashed so
// far (into h), into o, the next it holds.
static AK_NOINLINE uint64_t hash_walk(const walk_frame *top, uint64_t h, ak_obj *o) {
	walk w;
	walk_init(&w);
	walk_frame f = *top;
	for (;;) {
		// Into o: settled here when what it holds is plain, and the
		// walk's next frame when it isn't.
		walk_frame g;
		h = hash_enter(&g, h, o);
		if (hash_next(&g, &h, &o)) {
			walk_push(&w, &f);
			f = g;
			continue;
		}
		bool more;
		while (!(more = hash_next(&f, &h, &o)) && walk_pop(&w, &f)) {}
		if (!more) break;
	}
	walk_done(&w);
	return h;
}

uint64_t ak_val_hash(ak_val v, ak_kind k) {
	if (k != AK_K_REF) return hash_scalar(v, k);
	ak_obj *o = v.p;
	if (!o) return 0;
	if (o->tag == AK_TAG_STR) return ak_str_hash((ak_str *)o);
	// What o holds, here while it's scalars and strings; the walk from
	// the first that holds more.
	walk_frame f;
	uint64_t h = hash_enter(&f, 0, o);
	ak_obj *c;
	return hash_next(&f, &h, &c) ? hash_walk(&f, h, c) : h;
}

// ---- Copies for other threads ----

// shell is o's copy for another thread but for the references it holds
// (NULL until a walk fills them in), or o itself when it needn't be
// copied: a constant, never freed.
static ak_obj *shell(ak_obj *o) {
	if (o->rc == AK_RC_IMMORTAL && o->tag != AK_TAG_ENUM) return o;
	switch ((ak_tag)o->tag) {
	case AK_TAG_STR: {
		ak_str *s = (ak_str *)o;
		return &ak_str_new(s->data, s->len)->h;
	}
	case AK_TAG_BYTES: {
		ak_bytes *b = (ak_bytes *)o;
		return &ak_bytes_new(b->data, b->len)->h;
	}
	case AK_TAG_BOX: {
		ak_box *b = (ak_box *)o;
		ak_box *c = ak_box_new((ak_kind)b->h.aux, b->h.aux == AK_K_REF ? (ak_val){0} : b->v);
		c->h.flags = b->h.flags;
		return &c->h;
	}
	case AK_TAG_ARR: {
		ak_arr *a = (ak_arr *)o;
		ak_arr *c = ak_arr_new(ak_arr_kind(a), a->shape, a->len);
		c->h.flags = a->h.flags;
		c->len = a->len;
		if (ak_arr_kind(a) != AK_K_REF && a->len) memcpy(c->data, a->data, sizeof(ak_val) * a->len);
		return &c->h;
	}
	case AK_TAG_MAP: {
		// Its entries with references are put in as the walk copies them,
		// in order.
		ak_map *m = (ak_map *)o;
		ak_kind kk = (ak_kind)(m->h.aux & 0xf), vk = (ak_kind)(m->h.aux >> 4);
		ak_map *c = ak_map_new(kk, m->h.flags & AK_MAP_STRKEYS, vk, m->vshape);
		c->h.flags = m->h.flags;
		if (vk != AK_K_REF)
			for (uint32_t i = 0; i < m->len; i++)
				if (!m->dead[i]) ak_map_set(&c, ak_deep_copy(m->keys[i], kk), m->vals[i]);
		return &c->h;
	}
	case AK_TAG_STRUCT: {
		ak_struct *s = (ak_struct *)o;
		ak_struct *c = ak_struct_new(s->shape);
		for (uint16_t i = 0; i < s->shape->nfields; i++)
			if (s->shape->kinds[i] != AK_K_REF) c->f[i] = s->f[i];
		return &c->h;
	}
	case AK_TAG_ENUM: {
		ak_enum *e = (ak_enum *)o;
		const ak_case *cs = &e->shape->cases[e->h.aux];
		if (cs->nfields == 0) return o;  // a shared immortal singleton
		ak_enum *c = ak_enum_new(e->shape, e->h.aux);
		for (uint16_t i = 0; i < cs->nfields; i++)
			if (cs->kinds[i] != AK_K_REF) c->f[i] = e->f[i];
		return &c->h;
	}
	default:
		ak_panic("ak_deep_copy: a %d object can't cross threads", o->tag);
	}
}

// copy_at sets f at the start of x's values, and y's, its copy.
static AK_ALWAYS_INLINE void copy_at(walk_frame *f, ak_obj *x, ak_obj *y) {
	frame_at(f, x);
	f->b = y;
	f->y = values(y);
}

// copy_next copies f's next values into f's copy, up to one that holds
// others (true: it in *x, its copy, in place, in *y), or f's end.
static AK_ALWAYS_INLINE bool copy_next(walk_frame *f, ak_obj **x, ak_obj **y) {
	while (f->i < f->end) {
		uint32_t i = f->i++;
		if (skip(f, i)) continue;
		ak_obj *c = f->x[i].p, *d = c ? shell(c) : NULL;
		if (f->y) {
			f->y[i].p = d;
		} else {
			ak_map *m = (ak_map *)f->b;
			ak_map_set(&m, ak_deep_copy(((ak_map *)f->a)->keys[i], (ak_kind)(f->a->aux & 0xf)), (ak_val){.p = d});
		}
		if (d && d != c && holds_values(c)) {
			*x = c;
			*y = d;
			return true;
		}
	}
	return false;
}

// copy_walk goes on copying from top, the frame of the object copied so
// far, into x, the next it holds, and y, its copy.
static AK_NOINLINE void copy_walk(const walk_frame *top, ak_obj *x, ak_obj *y) {
	walk w;
	walk_init(&w);
	walk_frame f = *top;
	for (;;) {
		walk_frame g;
		copy_at(&g, x, y);
		if (copy_next(&g, &x, &y)) {
			walk_push(&w, &f);
			f = g;
			continue;
		}
		bool more;
		while (!(more = copy_next(&f, &x, &y)) && walk_pop(&w, &f)) {}
		if (!more) break;
	}
	walk_done(&w);
}

ak_val ak_deep_copy(ak_val v, ak_kind k) {
	if (k != AK_K_REF || !v.p) return v;
	ak_obj *o = v.p, *c = shell(o);
	if (c == o || !holds_values(o)) return (ak_val){.p = c};
	walk_frame f;
	copy_at(&f, o, c);
	ak_obj *x, *y;
	if (copy_next(&f, &x, &y)) copy_walk(&f, x, y);
	return (ak_val){.p = c};
}

// ---- Constants ----

// freeze_one makes o (not immortal) immortal, and a map's keys.
static void freeze_one(ak_obj *o, ak_frozen *made) {
	if (o->tag == AK_TAG_MAP) {
		ak_map *m = (ak_map *)o;
		if ((m->h.aux & 0xf) == AK_K_REF)
			for (uint32_t i = 0; i < m->len; i++) {
				ak_obj *key = m->keys[i].p;
				if (m->dead[i] || !key || key->rc == AK_RC_IMMORTAL) continue;
				ak_make_immortal(key);
				if (made) ak_vec_push(made, key);
			}
	}
	ak_make_immortal(o);
	if (made) ak_vec_push(made, o);
}

// freeze_next freezes f's next values, up to one that holds others
// (true, in *o), or f's end.
static bool freeze_next(walk_frame *f, ak_frozen *made, ak_obj **o) {
	while (f->i < f->end) {
		uint32_t i = f->i++;
		ak_obj *c = f->x[i].p;
		if (skip(f, i) || !c || c->rc == AK_RC_IMMORTAL) continue;
		freeze_one(c, made);
		if (holds_values(c)) {
			*o = c;
			return true;
		}
	}
	return false;
}

void ak_freeze(ak_val v, ak_kind k, ak_frozen *made) {
	if (k != AK_K_REF || !v.p) return;
	ak_obj *o = v.p;
	if (o->rc == AK_RC_IMMORTAL) return;
	freeze_one(o, made);
	if (!holds_values(o)) return;
	walk w;
	walk_init(&w);
	walk_frame f = {0};
	for (;;) {
		walk_frame g;
		frame_at(&g, o);
		if (freeze_next(&g, made, &o)) {
			walk_push(&w, &f);
			f = g;
			continue;
		}
		bool more;
		while (!(more = freeze_next(&f, made, &o)) && walk_pop(&w, &f)) {}
		if (!more) break;
	}
	walk_done(&w);
}

void ak_frozen_free(ak_frozen *made) {
	// First what they hold that wasn't frozen with them: a closure's
	// captures (freezing leaves them as they are), a runtime object's own
	// (its drop). Each of made is still whole as they're released, so
	// whichever of them a release reads (an immortal's count), in any
	// order, it reads.
	for (uint32_t i = 0; i < made->len; i++) {
		ak_obj *o = made->data[i];
		if (o->tag == AK_TAG_CLOSURE) {
			ak_closure *c = (ak_closure *)o;
			if (c->capkinds)
				for (uint16_t j = 0; j < c->ncap; j++)
					if (c->capkinds[j] == AK_K_REF) ak_release(c->caps[j].p);
			if (c->drop) c->drop(c);
		} else if (!holds_values(o) && o->tag != AK_TAG_STR && o->tag != AK_TAG_BYTES) {
			ak_opaque *x = (ak_opaque *)o;
			if (x->drop) x->drop(x);
		}
	}
	// Then their memory. What each holds now is immortal (frozen with it,
	// or a constant from before) or released: freeing one reads none of
	// the others.
	for (uint32_t i = 0; i < made->len; i++) free_mem(made->data[i]);
	ak_vec_free(made);
}

void *ak_constant_put(void **slot, void *v) {
	ak_frozen made = {0};
	ak_freeze((ak_val){.p = v}, AK_K_REF, &made);
	void *had = NULL;
	if (__atomic_compare_exchange_n(slot, &had, v, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		ak_vec_free(&made);
		return v;
	}
	ak_frozen_free(&made);  // nothing else saw it
	return had;
}

// ---- Errors ----

static AK_TLS void *pending_error;

static const uint8_t error_kinds[] = {AK_K_REF};
// Names carry their field's JSON descriptor after the NUL, as the
// compiler writes them (rt/json.c's ak_json_field_desc).
static const char *const error_names[] = {"message\0s"};
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

void (*ak_throw_hook)(void *err);
void (*ak_frame_hook)(const char *fn, const char *file, uint32_t line);

void ak_throw(void *err) {
	if (ak_throw_hook && err) ak_throw_hook(err);
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
	ak_str *s;
	if ((size_t)n < sizeof buf) {
		s = ak_str_new(buf, (size_t)n);
	} else {
		// Longer (a UI test's screen): formatted again, whole.
		s = ak_str_new(NULL, (size_t)n);
		va_start(ap, fmt);
		vsnprintf(s->data, (size_t)n + 1, fmt, ap);
		va_end(ap);
	}
	ak_struct *e = ak_struct_new(&ak_shape_error);
	e->f[0].p = s;
	ak_throw(e);
}

void ak_throw_cancel(void) {
	static void *cancel;
	void *c = ak_constant_get(&cancel);
	if (!c) {
		ak_struct *made = ak_struct_new(&cancel_shape);
		made->f[0].p = ak_str_c("cancelled");
		c = ak_constant_put(&cancel, made);
	}
	ak_throw(c);
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

// ---- What a builtin changed in place ----

AK_TLS ak_change ak_last_change;

void ak_change_key(ak_val key, ak_kind kk, bool len) {
	ak_last_change = (ak_change){.what = AK_CHANGE_KEY, .len = len, .hash = ak_val_hash(key, kk)};
}
