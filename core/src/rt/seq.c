// seq.c: Seq<T>, a lazy sequence (docs/language/types.md), and ordering
// by a key (an array's sorted(key:), min(key:), max(key:)).
//
// A Seq is an immutable description: a source (an array, a map, a set, a
// string's characters, a range, a generator) and the adapters chained on
// it (map, filter, take, ...), each an ak_seq holding the one before. It
// computes nothing until something reads it: a for loop, or a consumer
// (toArray, reduce, first, ...). Each read opens a cursor over the chain,
// a tree of cur mirroring it, which pulls one element at a time through
// the stages, so a chain builds no intermediate arrays, a take(n) or a
// first(where:) stops pulling once it has what it needs, and an endless
// generator is fine. A Seq can be read again: each read is a new cursor,
// from the start. Reading allocates per cursor, not per element (a
// tuple made by enumerated, zip or a map's source, an array by chunked
// or windows, aside: they're the values).
//
// What a source holds is its value as the Seq was made (a copy, as any
// value): a change to the array after doesn't change the Seq.
//
// A stage's closure doesn't throw (the checker says so): an adapter calls
// it later, when the chain is read. A consumer's closure may: the
// consumer calls it at once, and throws what it throws. Either may raise
// a runtime error (an index out of range), which stops the read and is
// passed on as any.
//
// The lowering passes, after a method's arguments, a proto: an empty
// array of the Seq's elements' type (its kind and shape: a tuple's, for
// the tuples a stage makes), or for a consumer an empty value of what it
// returns ([T], Set<T>, [K: V]), which it fills.
#include "rt/lib.h"

#define A(i) (args[i])
#define CLOS(i) ((ak_closure *)args[i].p)
#define ARR(i) ((ak_arr *)args[i].p)
#define RET_I(x) return (ak_val){.i = (int64_t)(x)}
#define RET_F(x) return (ak_val){.f = (x)}
#define RET_P(x) return (ak_val){.p = (x)}
#define RET_VOID return (ak_val){0}

enum {
	// Sources.
	S_ARR,      // a: the array
	S_MAP,      // a: the map; elements (key, value)
	S_SET,      // a: the set
	S_STR,      // a: the string; elements its characters
	S_RANGE,    // n to m; incl: m too
	S_FROM,     // n, n + m, n + 2m, ... without end
	S_REPEAT,   // a, n times (n < 0: without end)
	S_SUCC,     // a, then f of the one before, until f gives none
	S_UNFOLD,   // a: the state (of kind skind); f(state) gives (element, next state), or none
	// Adapters, on src.
	S_MAPF,     // f(x)
	S_FILTER,   // the x with f(x)
	S_COMPACT,  // f(x), without nones
	S_FLAT,     // the elements of the Seq f(x) gives
	S_ENUM,     // (i, x)
	S_ZIP,      // (x, y), y other's
	S_TAKE,     // the first n
	S_SKIP,     // after the first n
	S_TAKEW,    // up to the first x f isn't true of
	S_SKIPW,    // from the first x f isn't true of
	S_CHUNK,    // arrays of n
	S_WIN,      // each n in a row (a window sliding by one)
	S_EACH,     // f(x) called, x passed on
	S_UNIQUE,   // the first of equal elements
	S_CONCAT,   // src's, then other's
};

typedef struct ak_seq ak_seq;

struct ak_seq {
	ak_opaque h;
	uint8_t op;
	uint8_t kind;           // the elements'
	uint8_t akind;          // a's
	bool incl;
	const ak_shape *shape;  // the elements' (a tuple's, for the stages that make one), or NULL
	ak_seq *src, *other;
	ak_val a;
	ak_closure *f;
	int64_t n, m;
};

static void seq_drop(ak_opaque *o) {
	ak_seq *s = (ak_seq *)o;
	ak_release(s->src);
	ak_release(s->other);
	ak_release_val(s->a, (ak_kind)s->akind);
	ak_release(s->f);
}

// mk makes a stage of op, its elements those of proto (an empty array of
// their type), or src's when proto is NULL.
static ak_seq *mk(int op, ak_seq *src, const ak_arr *proto) {
	ak_seq *s = ak_opaque_new(AK_TAG_OPAQUE, sizeof(ak_seq), seq_drop);
	s->op = (uint8_t)op;
	s->src = ak_retain(src);
	s->akind = AK_K_VOID;
	if (proto) {
		s->kind = (uint8_t)ak_arr_kind(proto);
		s->shape = proto->shape;
	} else if (src) {
		s->kind = src->kind;
		s->shape = src->shape;
	}
	return s;
}

// ---- Cursors ----

typedef struct cur cur;

struct cur {
	const ak_seq *s;
	cur *src, *other, *inner;
	ak_seq *inner_seq;   // flatMap's current inner Seq
	ak_val v;            // the current element, when has
	bool has;
	bool done, started, second;
	int64_t i;
	ak_val st;           // successors' last element, unfold's state, unique's set, windows' buffer
	uint8_t stkind;
};

static cur *open_cur(const ak_seq *s);

static void close_cur(cur *c) {
	if (!c) return;
	if (c->has) ak_release_val(c->v, (ak_kind)c->s->kind);
	ak_release_val(c->st, (ak_kind)c->stkind);
	close_cur(c->src);
	close_cur(c->other);
	close_cur(c->inner);
	ak_release(c->inner_seq);
	ak_free(c);
}

static cur *open_cur(const ak_seq *s) {
	cur *c = ak_alloc(sizeof(cur));
	memset(c, 0, sizeof *c);
	c->s = s;
	c->stkind = AK_K_VOID;
	if (s->src) c->src = open_cur(s->src);
	if (s->other) c->other = open_cur(s->other);
	switch (s->op) {
	case S_RANGE:
	case S_FROM: c->i = s->n; break;
	case S_UNFOLD:
		c->st = s->a;
		c->stkind = s->akind;
		ak_retain_val(c->st, (ak_kind)c->stkind);
		break;
	case S_UNIQUE:
		c->st.p = ak_set_new((ak_kind)s->kind, s->kind == AK_K_REF);
		c->stkind = AK_K_REF;
		break;
	default: break;
	}
	return c;
}

// take moves the current element out of c: the caller owns it.
static inline ak_val take(cur *c) {
	c->has = false;
	return c->v;
}

static inline void put(cur *c, ak_val v) {
	c->v = v;
	c->has = true;
}

// opt is v (borrowed, of kind k) as an optional: a reference as it is,
// retained; a scalar boxed.
static ak_val opt(ak_kind k, ak_val v) {
	if (k == AK_K_REF) RET_P(ak_retain(v.p));
	RET_P(ak_box_new(k, v));
}

// unopt is an optional's value (owned) as a value of kind k: a box let
// go of for its scalar.
static ak_val unopt(ak_kind k, ak_val o) {
	if (k == AK_K_REF) return o;
	ak_val v = ((ak_box *)o.p)->v;
	ak_release(o.p);
	return v;
}

static ak_val pair(const ak_shape *shape, ak_val a, ak_val b) {
	ak_struct *t = ak_struct_new(shape);
	t->f[0] = a;
	t->f[1] = b;
	RET_P(t);
}

// push appends v (owned) to *a, keeping the flag that says its elements
// are strings.
static void push(ak_arr **a, ak_val v) {
	if (ak_arr_kind(*a) == AK_K_REF && v.p && ((ak_obj *)v.p)->tag == AK_TAG_STR) (*a)->h.flags = 1;
	ak_arr_push(a, v);
}

static inline bool call_bool(ak_closure *f, ak_val *x) {
	ak_val r = ak_call(f, x, 1);
	return !ak_error_pending() && r.i;
}

// adv moves c to its next element, which it holds (c->v): false at the
// end, or when a closure threw (ak_error_pending).
static bool adv(cur *c) {
	const ak_seq *s = c->s;
	ak_kind k = (ak_kind)s->kind;
	if (c->has) {
		ak_release_val(c->v, k);
		c->has = false;
	}
	if (c->done) return false;
	switch (s->op) {
	case S_ARR: {
		const ak_arr *a = s->a.p;
		if (c->i >= a->len) return false;
		ak_val v = a->data[c->i++];
		ak_retain_val(v, k);
		put(c, v);
		return true;
	}
	case S_MAP:
	case S_SET: {
		const ak_map *m = s->a.p;
		while (c->i < m->len && m->dead[c->i]) c->i++;
		if (c->i >= m->len) return false;
		ak_kind kk = (ak_kind)(m->h.aux & 0xf);
		ak_val key = m->keys[c->i];
		ak_retain_val(key, kk);
		if (s->op == S_SET) {
			c->i++;
			put(c, key);
			return true;
		}
		ak_val val = m->vals[c->i++];
		ak_retain_val(val, (ak_kind)(m->h.aux >> 4));
		put(c, pair(s->shape, key, val));
		return true;
	}
	case S_STR: {
		const ak_str *str = s->a.p;
		if (c->i >= str->len) return false;
		uint32_t i = (uint32_t)c->i, j = i + 1;
		while (j < str->len && ((uint8_t)str->data[j] & 0xc0) == 0x80) j++;
		c->i = j;
		put(c, (ak_val){.p = ak_str_new(str->data + i, j - i)});
		return true;
	}
	case S_RANGE:
		if (s->incl ? c->i > s->m : c->i >= s->m) return false;
		put(c, (ak_val){.i = c->i});
		if (c->i == s->m) c->done = true;  // through Int.max: no i + 1
		else c->i++;
		return true;
	case S_FROM:
		if (c->started) {
			// The one after the last: past int's range, an overflow, as +.
			if (__builtin_add_overflow(c->i, s->m, &c->i)) {
				c->done = true;
				ak_throw_msg("integer overflow");
				return false;
			}
		}
		c->started = true;
		put(c, (ak_val){.i = c->i});
		return true;
	case S_REPEAT:
		if (s->n >= 0 && c->i >= s->n) return false;
		c->i++;
		ak_retain_val(s->a, k);
		put(c, s->a);
		return true;
	case S_SUCC: {
		ak_val v;
		if (!c->started) {
			c->started = true;
			v = s->a;
			ak_retain_val(v, k);
		} else {
			ak_val r = ak_call(s->f, &c->st, 1);
			if (ak_error_pending() || !r.p) {
				c->done = true;
				return false;
			}
			v = unopt(k, r);
		}
		ak_release_val(c->st, (ak_kind)c->stkind);
		c->st = v;
		c->stkind = (uint8_t)k;
		ak_retain_val(v, k);
		put(c, v);
		return true;
	}
	case S_UNFOLD: {
		ak_val r = ak_call(s->f, &c->st, 1);
		if (ak_error_pending() || !r.p) {
			c->done = true;
			return false;
		}
		ak_struct *t = r.p;
		ak_val v = t->f[0], next = t->f[1];
		ak_retain_val(v, k);
		ak_retain_val(next, (ak_kind)c->stkind);
		ak_release(t);
		ak_release_val(c->st, (ak_kind)c->stkind);
		c->st = next;
		put(c, v);
		return true;
	}
	case S_MAPF: {
		if (!adv(c->src)) return false;
		ak_val r = ak_call(s->f, &c->src->v, 1);
		if (ak_error_pending()) return false;
		put(c, r);
		return true;
	}
	case S_FILTER:
		while (adv(c->src)) {
			if (call_bool(s->f, &c->src->v)) {
				put(c, take(c->src));
				return true;
			}
			if (ak_error_pending()) return false;
		}
		return false;
	case S_COMPACT:
		while (adv(c->src)) {
			ak_val r = ak_call(s->f, &c->src->v, 1);
			if (ak_error_pending()) return false;
			if (r.p) {
				put(c, unopt(k, r));
				return true;
			}
		}
		return false;
	case S_FLAT:
		for (;;) {
			if (c->inner) {
				if (adv(c->inner)) {
					put(c, take(c->inner));
					return true;
				}
				close_cur(c->inner);
				c->inner = NULL;
				ak_release(c->inner_seq);
				c->inner_seq = NULL;
				if (ak_error_pending()) return false;
			}
			if (!adv(c->src)) return false;
			ak_val r = ak_call(s->f, &c->src->v, 1);
			if (ak_error_pending()) return false;
			c->inner_seq = r.p;
			c->inner = open_cur(c->inner_seq);
		}
	case S_ENUM:
		if (!adv(c->src)) return false;
		put(c, pair(s->shape, (ak_val){.i = c->i++}, take(c->src)));
		return true;
	case S_ZIP:
		// The second isn't pulled once the first has ended.
		if (!adv(c->src) || !adv(c->other)) return false;
		put(c, pair(s->shape, take(c->src), take(c->other)));
		return true;
	case S_TAKE:
		if (c->i >= s->n) return false;  // and src isn't pulled again
		c->i++;
		if (!adv(c->src)) return false;
		put(c, take(c->src));
		return true;
	case S_SKIP:
		for (; c->i < s->n; c->i++)
			if (!adv(c->src)) return false;
		if (!adv(c->src)) return false;
		put(c, take(c->src));
		return true;
	case S_TAKEW:
		if (!adv(c->src)) return false;
		if (!call_bool(s->f, &c->src->v)) {
			c->done = true;
			return false;
		}
		put(c, take(c->src));
		return true;
	case S_SKIPW:
		while (adv(c->src)) {
			if (c->started || !call_bool(s->f, &c->src->v)) {
				if (ak_error_pending()) return false;
				c->started = true;
				put(c, take(c->src));
				return true;
			}
		}
		return false;
	case S_CHUNK: {
		ak_kind ek = (ak_kind)s->src->kind;
		ak_arr *out = NULL;
		while (out ? out->len < s->n : true) {
			if (!adv(c->src)) break;
			if (!out) out = ak_arr_new(ek, s->src->shape, (uint32_t)(s->n < 64 ? s->n : 64));
			push(&out, take(c->src));
		}
		if (ak_error_pending() || !out) {
			ak_release(out);
			c->done = true;
			return false;
		}
		if (out->len < s->n) c->done = true;
		put(c, (ak_val){.p = out});
		return true;
	}
	case S_WIN: {
		// st: the last n elements, a ring from c->i once it's full.
		ak_kind ek = (ak_kind)s->src->kind;
		if (!c->st.p) {
			c->st.p = ak_arr_new(ek, s->src->shape, (uint32_t)(s->n < 64 ? s->n : 64));
			c->stkind = AK_K_REF;
		}
		ak_arr *buf = c->st.p;
		while (buf->len < s->n) {
			if (!adv(c->src)) return false;
			push((ak_arr **)&c->st.p, take(c->src));
			buf = c->st.p;
		}
		if (c->started) {
			if (!adv(c->src)) return false;
			ak_release_val(buf->data[c->i], ek);
			buf->data[c->i] = take(c->src);
			if (ek == AK_K_REF && buf->data[c->i].p && ((ak_obj *)buf->data[c->i].p)->tag == AK_TAG_STR) buf->h.flags = 1;
			c->i = (c->i + 1) % s->n;
		}
		c->started = true;
		ak_arr *out = ak_arr_new(ek, buf->shape, (uint32_t)s->n);
		out->h.flags = buf->h.flags;
		for (int64_t j = 0; j < s->n; j++) {
			ak_val v = buf->data[(c->i + j) % s->n];
			ak_retain_val(v, ek);
			out->data[out->len++] = v;
		}
		put(c, (ak_val){.p = out});
		return true;
	}
	case S_EACH:
		if (!adv(c->src)) return false;
		ak_call(s->f, &c->src->v, 1);
		if (ak_error_pending()) return false;
		put(c, take(c->src));
		return true;
	case S_UNIQUE:
		while (adv(c->src)) {
			ak_val v = c->src->v;
			if (ak_set_has(c->st.p, v)) continue;
			ak_retain_val(v, k);
			ak_set_insert((ak_map **)&c->st.p, v);
			put(c, take(c->src));
			return true;
		}
		return false;
	case S_CONCAT:
		if (!c->second) {
			if (adv(c->src)) {
				put(c, take(c->src));
				return true;
			}
			if (ak_error_pending()) return false;
			c->second = true;
		}
		if (!adv(c->other)) return false;
		put(c, take(c->other));
		return true;
	}
	return false;
}

// ---- Sources ----

#define PROTO(i) ((const ak_arr *)args[i].p)

static ak_seq *source(int op, ak_val a, ak_kind ak, const ak_arr *proto) {
	ak_seq *s = mk(op, NULL, proto);
	ak_retain_val(a, ak);
	s->a = a;
	s->akind = (uint8_t)ak;
	return s;
}

// seq.of_array(a, proto), seq.of_map(m, proto), seq.of_set(s, proto),
// seq.of_string(s, proto).
static ak_val seq_of_array(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(source(S_ARR, A(0), AK_K_REF, PROTO(1))); }
static ak_val seq_of_map(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(source(S_MAP, A(0), AK_K_REF, PROTO(1))); }
static ak_val seq_of_set(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(source(S_SET, A(0), AK_K_REF, PROTO(1))); }
static ak_val seq_of_string(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(source(S_STR, A(0), AK_K_REF, PROTO(1))); }

// seq.range(from, to, through, proto): from..<to, or from..to.
static ak_val seq_range(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_seq *s = mk(S_RANGE, NULL, PROTO(3));
	s->n = A(0).i;
	s->m = A(1).i;
	s->incl = A(2).i != 0;
	RET_P(s);
}

// seq.from(start, by, proto): start, start + by, ... without end.
static ak_val seq_from(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_seq *s = mk(S_FROM, NULL, PROTO(2));
	s->n = A(0).i;
	s->m = A(1).i;
	RET_P(s);
}

// seq.repeat(x, count, proto): count < 0 without end.
static ak_val seq_repeat(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_arr *proto = PROTO(2);
	ak_seq *s = source(S_REPEAT, A(0), ak_arr_kind(proto), proto);
	s->n = A(1).i;
	RET_P(s);
}

// seq.successors(first, next, proto): first, next(first), ... until next
// gives none.
static ak_val seq_successors(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_arr *proto = PROTO(2);
	ak_seq *s = source(S_SUCC, A(0), ak_arr_kind(proto), proto);
	s->f = ak_retain(CLOS(1));
	RET_P(s);
}

// seq.unfold(state, stateKind, next, proto): next(state) gives (element,
// next state), or none for the end.
static ak_val seq_unfold(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_seq *s = source(S_UNFOLD, A(0), (ak_kind)A(1).i, PROTO(3));
	s->f = ak_retain(CLOS(2));
	RET_P(s);
}

// ---- Adapters ----

#define SEQ(i) ((ak_seq *)args[i].p)

static ak_seq *with_f(int op, ak_val *args, const ak_arr *proto) {
	ak_seq *s = mk(op, SEQ(0), proto);
	s->f = ak_retain(CLOS(1));
	return s;
}

static ak_val seq_map(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(with_f(S_MAPF, args, PROTO(2))); }
static ak_val seq_filter(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(with_f(S_FILTER, args, NULL)); }
static ak_val seq_compact_map(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(with_f(S_COMPACT, args, PROTO(2))); }
static ak_val seq_flat_map(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(with_f(S_FLAT, args, PROTO(2))); }
static ak_val seq_take_while(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(with_f(S_TAKEW, args, NULL)); }
static ak_val seq_skip_while(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(with_f(S_SKIPW, args, NULL)); }
static ak_val seq_on_each(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(with_f(S_EACH, args, NULL)); }
static ak_val seq_enumerated(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(mk(S_ENUM, SEQ(0), PROTO(1))); }
static ak_val seq_unique(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(mk(S_UNIQUE, SEQ(0), NULL)); }

static ak_val seq_zip(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_seq *s = mk(S_ZIP, SEQ(0), PROTO(2));
	s->other = ak_retain(SEQ(1));
	RET_P(s);
}

static ak_val seq_concat(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_seq *s = mk(S_CONCAT, SEQ(0), NULL);
	s->other = ak_retain(SEQ(1));
	RET_P(s);
}

static ak_val counted(int op, ak_val *args, const ak_arr *proto) {
	ak_seq *s = mk(op, SEQ(0), proto);
	s->n = A(1).i < 0 ? 0 : A(1).i;
	RET_P(s);
}

static ak_val seq_take(ak_val *args, uint32_t n) { AK_UNUSED(n); return counted(S_TAKE, args, NULL); }
static ak_val seq_skip(ak_val *args, uint32_t n) { AK_UNUSED(n); return counted(S_SKIP, args, NULL); }

// seq.chunked(s, size, proto), seq.windows(s, size, proto): a size under
// 1 is an error, as an array's chunked's is.
static ak_val sized(int op, ak_val *args, const char *what) {
	if (A(1).i < 1) {
		ak_throw_msg("%s of %lld: the size must be 1 or more", what, (long long)A(1).i);
		RET_VOID;
	}
	return counted(op, args, PROTO(2));
}

static ak_val seq_chunked(ak_val *args, uint32_t n) { AK_UNUSED(n); return sized(S_CHUNK, args, "chunked"); }
static ak_val seq_windows(ak_val *args, uint32_t n) { AK_UNUSED(n); return sized(S_WIN, args, "windows"); }

// ---- Reading: a for loop's cursor ----

typedef struct {
	ak_opaque h;
	cur *c;
} seq_iter;

static void iter_drop(ak_opaque *o) { close_cur(((seq_iter *)o)->c); }

// seq.iter(s): a cursor before the first element; seq.next(it) moves it
// on (false at the end; it raises what a stage raised), seq.value(it) is
// the element it's at.
static ak_val seq_iter_new(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	seq_iter *it = ak_opaque_new(AK_TAG_OPAQUE, sizeof(seq_iter), iter_drop);
	it->c = open_cur(SEQ(0));
	RET_P(it);
}

static ak_val seq_next(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	RET_I(adv(((seq_iter *)args[0].p)->c));
}

static ak_val seq_value(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return take(((seq_iter *)args[0].p)->c);
}

// ---- Consumers ----

// A read: a cursor over s, closed by done, which says whether it ended
// without an error.
static cur *start(ak_val *args) { return open_cur(SEQ(0)); }

static bool done(cur *c) {
	close_cur(c);
	return !ak_error_pending();
}

// seq.to_array(s, proto): proto, an empty [T], filled.
static ak_val seq_to_array(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_seq *s = SEQ(0);
	if (s->op == S_ARR) RET_P(ak_retain(s->a.p));  // the array itself: nothing to compute
	ak_arr *out = ak_retain(args[1].p);
	cur *c = start(args);
	while (adv(c)) push(&out, take(c));
	if (!done(c)) {
		ak_release(out);
		RET_VOID;
	}
	RET_P(out);
}

// seq.to_set(s, proto): proto, an empty Set<T>, filled.
static ak_val seq_to_set(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_map *out = ak_retain(args[1].p);
	cur *c = start(args);
	while (adv(c)) ak_set_insert(&out, take(c));
	if (!done(c)) {
		ak_release(out);
		RET_VOID;
	}
	RET_P(out);
}

// seq.to_map(s, proto): of (key, value) pairs; a key that comes again
// takes the later value, where the key first came.
static ak_val seq_to_map(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_map *out = ak_retain(args[1].p);
	cur *c = start(args);
	while (adv(c)) {
		ak_struct *t = c->v.p;
		ak_val k = t->f[0], v = t->f[1];
		ak_retain_val(k, (ak_kind)t->shape->kinds[0]);
		ak_retain_val(v, (ak_kind)t->shape->kinds[1]);
		ak_map_set(&out, k, v);
	}
	if (!done(c)) {
		ak_release(out);
		RET_VOID;
	}
	RET_P(out);
}

// seq.len(s): how many elements; seq.count_where(s, p).
static ak_val seq_len(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	cur *c = start(args);
	int64_t k = 0;
	while (adv(c)) k++;
	if (!done(c)) RET_VOID;
	RET_I(k);
}

static ak_val seq_count_where(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	cur *c = start(args);
	int64_t k = 0;
	while (adv(c)) {
		k += call_bool(CLOS(1), &c->v);
		if (ak_error_pending()) break;
	}
	if (!done(c)) RET_VOID;
	RET_I(k);
}

// seq.first(s), seq.last(s), seq.find(s, p): a T?.
static ak_val seq_first(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	cur *c = start(args);
	ak_val r = {.p = NULL};
	if (adv(c)) r = opt((ak_kind)c->s->kind, c->v);
	if (!done(c)) {
		ak_release(r.p);
		RET_VOID;
	}
	return r;
}

static ak_val seq_last(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	cur *c = start(args);
	ak_kind k = (ak_kind)c->s->kind;
	ak_val last = {0};
	bool any = false;
	while (adv(c)) {
		if (any) ak_release_val(last, k);
		last = take(c);
		any = true;
	}
	if (!done(c)) {
		if (any) ak_release_val(last, k);
		RET_VOID;
	}
	if (!any) RET_P(NULL);
	ak_val r = opt(k, last);
	ak_release_val(last, k);
	return r;
}

static ak_val seq_find(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	cur *c = start(args);
	ak_val r = {.p = NULL};
	while (adv(c)) {
		if (call_bool(CLOS(1), &c->v)) {
			r = opt((ak_kind)c->s->kind, c->v);
			break;
		}
		if (ak_error_pending()) break;
	}
	if (!done(c)) {
		ak_release(r.p);
		RET_VOID;
	}
	return r;
}

// seq.any(s, p), seq.all(s, p): they stop at the first that decides.
static ak_val seq_any_all(ak_val *args, bool all) {
	cur *c = start(args);
	bool r = all;
	while (adv(c)) {
		bool t = call_bool(CLOS(1), &c->v);
		if (ak_error_pending()) break;
		if (t != all) {
			r = !all;
			break;
		}
	}
	if (!done(c)) RET_VOID;
	RET_I(r);
}

static ak_val seq_any(ak_val *args, uint32_t n) { AK_UNUSED(n); return seq_any_all(args, false); }
static ak_val seq_all(ak_val *args, uint32_t n) { AK_UNUSED(n); return seq_any_all(args, true); }

static ak_val seq_contains(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	cur *c = start(args);
	bool r = false;
	while (adv(c))
		if (ak_val_eq(c->v, A(1), (ak_kind)c->s->kind)) {
			r = true;
			break;
		}
	if (!done(c)) RET_VOID;
	RET_I(r);
}

// seq.reduce(s, initial, combine, kindU).
static ak_val seq_reduce(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_kind ku = (ak_kind)A(3).i;
	ak_val acc = A(1);
	ak_retain_val(acc, ku);
	cur *c = start(args);
	while (adv(c)) {
		ak_val two[2] = {acc, c->v};
		ak_val r = ak_call(CLOS(2), two, 2);
		ak_release_val(acc, ku);
		if (ak_error_pending()) {
			close_cur(c);
			RET_VOID;
		}
		acc = r;
	}
	if (!done(c)) {
		ak_release_val(acc, ku);
		RET_VOID;
	}
	return acc;
}

// seq.sum(s): ints checked, as +.
static ak_val seq_sum(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	cur *c = start(args);
	if (c->s->kind == AK_K_FLOAT) {
		double s = 0;
		while (adv(c)) s += c->v.f;
		if (!done(c)) RET_VOID;
		RET_F(s);
	}
	int64_t s = 0;
	while (adv(c)) {
		if (__builtin_add_overflow(s, c->v.i, &s)) {
			close_cur(c);
			ak_throw_msg("integer overflow");
			RET_VOID;
		}
	}
	if (!done(c)) RET_VOID;
	RET_I(s);
}

// seq.joined(s, separator): of strings.
static ak_val seq_joined(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *sep = args[1].p;
	ak_strb b = {0};
	cur *c = start(args);
	bool first = true;
	while (adv(c)) {
		if (!first) ak_strb_str(&b, sep);
		first = false;
		ak_strb_str(&b, c->v.p);
	}
	ak_str *r = ak_strb_finish(&b);
	if (!done(c)) {
		ak_release(r);
		RET_VOID;
	}
	RET_P(r);
}

// ---- Ordering ----
//
// As sorted() orders: numbers, strings, an enum's cases in their order;
// or by a closure: a less-than (by:), or a type's compare method (the
// lowering passes it, for a struct's or an enum's).

typedef struct {
	ak_closure *f;
	bool compare;  // f is a compare method: (a, b) -> int
	ak_kind k;
	bool failed;
} order;

static bool less(order *o, ak_val a, ak_val b) {
	if (o->failed) return false;
	if (o->f) {
		ak_val two[2] = {a, b};
		ak_val r = ak_call(o->f, two, 2);
		if (ak_error_pending()) {
			o->failed = true;
			return false;
		}
		return o->compare ? r.i < 0 : r.i != 0;
	}
	switch (o->k) {
	case AK_K_FLOAT: return a.f < b.f;
	case AK_K_REF:
		if (a.p && ((ak_obj *)a.p)->tag == AK_TAG_ENUM) return ak_enum_case(a.p) < ak_enum_case(b.p);
		return ak_str_cmp(a.p, b.p) < 0;
	default: return a.i < b.i;
	}
}

// seq.min(s, less?, compare?), seq.max: the least (greatest), the first
// of equal ones.
static ak_val extreme(ak_val *args, uint32_t n, bool greatest) {
	cur *c = start(args);
	ak_kind k = (ak_kind)c->s->kind;
	order o = {n > 1 ? CLOS(1) : NULL, n > 2 && A(2).i, k, false};
	ak_val best = {0};
	bool any = false;
	while (adv(c)) {
		if (!any) {
			best = take(c);
			any = true;
			continue;
		}
		bool better = greatest ? less(&o, best, c->v) : less(&o, c->v, best);
		if (o.failed) break;
		if (better) {
			ak_release_val(best, k);
			best = take(c);
		}
	}
	if (!done(c)) {
		if (any) ak_release_val(best, k);
		RET_VOID;
	}
	if (!any) RET_P(NULL);
	ak_val r = opt(k, best);
	ak_release_val(best, k);
	return r;
}

static ak_val seq_min(ak_val *args, uint32_t n) { return extreme(args, n, false); }
static ak_val seq_max(ak_val *args, uint32_t n) { return extreme(args, n, true); }

// By a key: key(x) is computed once per element, and keys are ordered
// as sorted() orders them (cmp: the key type's compare method, or none).
// args: the receiver, key, [descending,] cmp, kindU.

typedef struct {
	ak_val k;
	uint32_t i;
} keyed;

// keyed_less orders a and b by their keys, then by where they were:
// stable either way.
static bool keyed_less(order *o, bool desc, const keyed *a, const keyed *b) {
	if (desc ? less(o, b->k, a->k) : less(o, a->k, b->k)) return true;
	if (o->failed) return false;
	if (desc ? less(o, a->k, b->k) : less(o, b->k, a->k)) return false;
	return a->i < b->i;
}

static void keyed_sort(order *o, bool desc, keyed *v, keyed *tmp, uint32_t n) {
	if (n < 2 || o->failed) return;
	if (n <= 12) {
		for (uint32_t i = 1; i < n; i++) {
			keyed x = v[i];
			uint32_t j = i;
			while (j > 0 && keyed_less(o, desc, &x, &v[j - 1])) {
				v[j] = v[j - 1];
				j--;
			}
			v[j] = x;
		}
		return;
	}
	uint32_t h = n / 2;
	keyed_sort(o, desc, v, tmp, h);
	keyed_sort(o, desc, v + h, tmp, n - h);
	uint32_t i = 0, j = h, k = 0;
	while (i < h && j < n) tmp[k++] = keyed_less(o, desc, &v[j], &v[i]) ? v[j++] : v[i++];
	while (i < h) tmp[k++] = v[i++];
	while (j < n) tmp[k++] = v[j++];
	memcpy(v, tmp, sizeof(keyed) * n);
}

// sort_by_key sorts a's elements (a unique array) by key: false when key
// or the comparison threw.
static bool sort_by_key(ak_arr *a, ak_closure *key, bool desc, ak_closure *cmp, ak_kind ku) {
	uint32_t n = a->len;
	keyed *v = ak_alloc(sizeof(keyed) * (n ? n : 1) * 2);
	uint32_t made = 0;
	bool ok = true;
	for (; made < n; made++) {
		v[made] = (keyed){ak_call(key, &a->data[made], 1), made};
		if (ak_error_pending()) {
			ok = false;
			break;
		}
	}
	order o = {cmp, cmp != NULL, ku, false};
	if (ok) {
		keyed_sort(&o, desc, v, v + n, n);
		ok = !o.failed;
	}
	if (ok) {
		ak_val *data = ak_alloc(sizeof(ak_val) * (n ? n : 1));
		for (uint32_t i = 0; i < n; i++) data[i] = a->data[v[i].i];
		memcpy(a->data, data, sizeof(ak_val) * n);
		ak_free(data);
	}
	for (uint32_t i = 0; i < made; i++) ak_release_val(v[i].k, ku);
	ak_free(v);
	return ok;
}

#define OPT_CLOS(i) (args[i].p ? (ak_closure *)args[i].p : NULL)

// arr.sorted_key(a, key, descending, cmp, kindU) and arr.sort_key (inout).
static ak_val arr_sorted_key(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ak_arr_copy(ARR(0));
	if (!sort_by_key(a, CLOS(1), A(2).i != 0, OPT_CLOS(3), (ak_kind)A(4).i)) {
		ak_release(a);
		RET_VOID;
	}
	RET_P(a);
}

static ak_val arr_sort_key(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	// In place: sort_by_key moves the elements only once nothing threw,
	// so a throw leaves the array as it was.
	ak_arr *a = ak_arr_mut((ak_arr **)&args[0].p);
	if (!sort_by_key(a, CLOS(1), A(2).i != 0, OPT_CLOS(3), (ak_kind)A(4).i)) RET_VOID;
	ak_change_range(0, a->len, false);
	RET_VOID;
}

// keyed_extreme is the least (greatest) of what next gives by key, the
// first of equal ones: next(ctx, &x) gives each element, borrowed.
typedef bool (*next_fn)(void *ctx, ak_val *x);

static ak_val keyed_extreme(next_fn next, void *ctx, ak_kind k, ak_closure *key, ak_closure *cmp, ak_kind ku, bool greatest) {
	order o = {cmp, cmp != NULL, ku, false};
	ak_val best = {0}, bestk = {0}, x;
	bool any = false;
	while (next(ctx, &x)) {
		ak_val kx = ak_call(key, &x, 1);
		if (ak_error_pending()) break;
		bool better = !any || (greatest ? less(&o, bestk, kx) : less(&o, kx, bestk));
		if (o.failed) {
			ak_release_val(kx, ku);
			break;
		}
		if (better) {
			if (any) {
				ak_release_val(best, k);
				ak_release_val(bestk, ku);
			}
			ak_retain_val(x, k);
			best = x;
			bestk = kx;
			any = true;
		} else {
			ak_release_val(kx, ku);
		}
	}
	ak_val r = {.p = NULL};
	if (any) {
		if (!ak_error_pending()) r = opt(k, best);
		ak_release_val(best, k);
		ak_release_val(bestk, ku);
	}
	return r;
}

typedef struct {
	const ak_arr *a;
	uint32_t i;
} arr_walk;

static bool arr_next(void *ctx, ak_val *x) {
	arr_walk *w = ctx;
	if (w->i >= w->a->len) return false;
	*x = w->a->data[w->i++];
	return true;
}

static bool cur_next(void *ctx, ak_val *x) {
	cur *c = ctx;
	if (!adv(c)) return false;
	*x = c->v;
	return true;
}

// arr.min_key(a, key, cmp, kindU), arr.max_key; seq.min_key, seq.max_key.
static ak_val arr_min_key(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	arr_walk w = {ARR(0), 0};
	return keyed_extreme(arr_next, &w, ak_arr_kind(w.a), CLOS(1), OPT_CLOS(2), (ak_kind)A(3).i, false);
}

static ak_val arr_max_key(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	arr_walk w = {ARR(0), 0};
	return keyed_extreme(arr_next, &w, ak_arr_kind(w.a), CLOS(1), OPT_CLOS(2), (ak_kind)A(3).i, true);
}

static ak_val seq_keyed_extreme(ak_val *args, bool greatest) {
	cur *c = start(args);
	ak_val r = keyed_extreme(cur_next, c, (ak_kind)c->s->kind, CLOS(1), OPT_CLOS(2), (ak_kind)A(3).i, greatest);
	if (!done(c)) {
		ak_release(r.p);
		RET_VOID;
	}
	return r;
}

static ak_val seq_min_key(ak_val *args, uint32_t n) { AK_UNUSED(n); return seq_keyed_extreme(args, false); }
static ak_val seq_max_key(ak_val *args, uint32_t n) { AK_UNUSED(n); return seq_keyed_extreme(args, true); }

const ak_lib_entry ak_seq_lib[] = {
	{"seq.of_array", seq_of_array},
	{"seq.of_map", seq_of_map},
	{"seq.of_set", seq_of_set},
	{"seq.of_string", seq_of_string},
	{"seq.range", seq_range},
	{"seq.from", seq_from},
	{"seq.repeat", seq_repeat},
	{"seq.successors", seq_successors},
	{"seq.unfold", seq_unfold},
	{"seq.map", seq_map},
	{"seq.filter", seq_filter},
	{"seq.compact_map", seq_compact_map},
	{"seq.flat_map", seq_flat_map},
	{"seq.enumerated", seq_enumerated},
	{"seq.zip", seq_zip},
	{"seq.take", seq_take},
	{"seq.skip", seq_skip},
	{"seq.take_while", seq_take_while},
	{"seq.skip_while", seq_skip_while},
	{"seq.chunked", seq_chunked},
	{"seq.windows", seq_windows},
	{"seq.on_each", seq_on_each},
	{"seq.unique", seq_unique},
	{"seq.concat", seq_concat},
	{"seq.iter", seq_iter_new},
	{"seq.next", seq_next},
	{"seq.value", seq_value},
	{"seq.to_array", seq_to_array},
	{"seq.to_set", seq_to_set},
	{"seq.to_map", seq_to_map},
	{"seq.len", seq_len},
	{"seq.count_where", seq_count_where},
	{"seq.first", seq_first},
	{"seq.last", seq_last},
	{"seq.find", seq_find},
	{"seq.any", seq_any},
	{"seq.all", seq_all},
	{"seq.contains", seq_contains},
	{"seq.reduce", seq_reduce},
	{"seq.sum", seq_sum},
	{"seq.joined", seq_joined},
	{"seq.min", seq_min},
	{"seq.max", seq_max},
	{"seq.min_key", seq_min_key},
	{"seq.max_key", seq_max_key},
	{"arr.sorted_key", arr_sorted_key},
	{"arr.sort_key", arr_sort_key},
	{"arr.min_key", arr_min_key},
	{"arr.max_key", arr_max_key},
	{NULL, NULL},
};
