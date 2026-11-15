#include "rt/lib.h"
#include "rt/sched.h"
#include "base/thread.h"

#include <stdio.h>
#include <stdlib.h>

#define A(i) (args[i])
#define STR(i) ((ak_str *)args[i].p)
#define ARR(i) ((ak_arr *)args[i].p)
#define MAP(i) ((ak_map *)args[i].p)
#define CLOS(i) ((ak_closure *)args[i].p)
#define RET_I(x) return (ak_val){.i = (int64_t)(x)}
#define RET_F(x) return (ak_val){.f = (x)}
#define RET_P(x) return (ak_val){.p = (x)}
#define VOID return (ak_val){0}

void (*ak_lib_print_hook)(const char *s, size_t n);

// ---- Output ----

static ak_val lib_print(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *s = STR(0);
	if (ak_lib_print_hook) {
		ak_lib_print_hook(s->data, s->len);
	} else {
		fwrite(s->data, 1, s->len, stdout);
		fputc('\n', stdout);
		fflush(stdout);
	}
	VOID;
}

static ak_val lib_panic(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_panic("%s", STR(0)->data);
}

static ak_val lib_error(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_struct *e = ak_struct_new(&ak_shape_error);
	e->f[0].p = ak_retain(STR(0));
	RET_P(e);
}

static ak_val lib_error_message(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	RET_P(ak_retain(ak_error_message(args[0].p)));
}

// rt.freeze(value, kind): the value, made a constant (rt.h's ak_freeze).
static ak_val rt_freeze(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_kind k = (ak_kind)args[1].i;
	ak_freeze(args[0], k);
	ak_retain_val(args[0], k);
	return args[0];
}

// ---- Strings ----

static ak_val str_len(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(ak_str_count(STR(0))); }
static ak_val str_bytes_len(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(STR(0)->len); }
static ak_val str_upper(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_str_upper(STR(0))); }
static ak_val str_lower(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_str_lower(STR(0))); }
static ak_val str_trim(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_str_trim(STR(0))); }
static ak_val str_contains(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(ak_str_contains(STR(0), STR(1))); }
static ak_val str_has_prefix(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(ak_str_has_prefix(STR(0), STR(1))); }
static ak_val str_has_suffix(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(ak_str_has_suffix(STR(0), STR(1))); }
static ak_val str_replace(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_str_replace(STR(0), STR(1), STR(2))); }
static ak_val str_split(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_str_split(STR(0), STR(1))); }
static ak_val str_is_empty(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(STR(0)->len == 0); }

// str.find returns int? (a box, or none).
static ak_val str_find(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t i = ak_str_find(STR(0), STR(1));
	if (i < 0) RET_P(NULL);
	RET_P(ak_box_new(AK_K_INT, (ak_val){.i = i}));
}

// str.slice(s, from, to): character offsets, clamped.
static int64_t char_to_byte(const ak_str *s, int64_t c) {
	if (c <= 0) return 0;
	int64_t k = 0;
	for (uint32_t i = 0; i < s->len; i++) {
		if (((uint8_t)s->data[i] & 0xc0) != 0x80) {
			if (k == c) return i;
			k++;
		}
	}
	return s->len;
}

static ak_val str_slice(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *s = STR(0);
	RET_P(ak_str_slice(s, char_to_byte(s, A(1).i), char_to_byte(s, A(2).i)));
}

static ak_val str_to_int(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t v;
	if (!ak_str_to_int(STR(0), &v)) RET_P(NULL);
	RET_P(ak_box_new(AK_K_INT, (ak_val){.i = v}));
}

static ak_val str_to_float(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	double v;
	if (!ak_str_to_float(STR(0), &v)) RET_P(NULL);
	RET_P(ak_box_new(AK_K_FLOAT, (ak_val){.f = v}));
}

static ak_val str_repeat(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_strb b = {0};
	for (int64_t i = 0; i < A(1).i; i++) ak_strb_str(&b, STR(0));
	RET_P(ak_strb_finish(&b));
}

static ak_val str_join(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	ak_strb b = {0};
	for (uint32_t i = 0; i < a->len; i++) {
		if (i) ak_strb_str(&b, STR(1));
		if (a->data[i].p) ak_strb_str(&b, a->data[i].p);
	}
	RET_P(ak_strb_finish(&b));
}

// str.chars: an array of one-character strings.
static ak_val str_chars(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *empty = ak_str_empty();
	RET_P(ak_str_split(STR(0), empty));
}

static ak_val str_from_float_digits(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	char buf[64];
	int64_t d = A(1).i;
	if (d < 0) d = 0;
	if (d > 20) d = 20;
	int w = snprintf(buf, sizeof buf, "%.*f", (int)d, A(0).f);
	RET_P(ak_str_new(buf, (size_t)w));
}

// ---- Arrays ----

static ak_kind ek(const ak_arr *a) { return ak_arr_kind(a); }

static ak_val arr_push(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_retain_val(A(1), ek(ARR(0)));
	ak_arr_push((ak_arr **)&args[0].p, A(1));
	VOID;
}

static ak_val arr_insert(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_retain_val(A(2), ek(ARR(0)));
	ak_arr_insert((ak_arr **)&args[0].p, A(1).i, A(2));
	VOID;
}

static ak_val arr_remove_at(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	if (!ak_arr_check(ARR(0), A(1).i)) {
		ak_throw_msg("index %lld out of range (length %u)", (long long)A(1).i, ARR(0)->len);
		VOID;
	}
	return ak_arr_remove_at((ak_arr **)&args[0].p, A(1).i);
}

// arr.remove_where(a, pred) removes the elements pred is true of; the
// number removed.
static ak_val arr_remove_where(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	ak_closure *pred = CLOS(1);
	int64_t removed = 0;
	// Find the first to remove before copying, so a no-op shares.
	uint32_t i = 0;
	for (; i < a->len; i++) {
		ak_val r = ak_call(pred, &a->data[i], 1);
		if (ak_error_pending()) VOID;
		if (r.i) break;
	}
	if (i == a->len) RET_I(0);
	a = ak_arr_mut((ak_arr **)&args[0].p);
	uint32_t j = i;
	for (; i < a->len; i++) {
		bool rm = true;
		if (removed > 0) {
			ak_val r = ak_call(pred, &a->data[i], 1);
			if (ak_error_pending()) {
				// Keep the rest as it was.
				for (; i < a->len; i++) a->data[j++] = a->data[i];
				a->len = j;
				VOID;
			}
			rm = r.i != 0;
		}
		if (rm) {
			ak_release_val(a->data[i], ek(a));
			removed++;
		} else {
			a->data[j++] = a->data[i];
		}
	}
	a->len = j;
	RET_I(removed);
}

static ak_val arr_clear(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	ak_arr *e = ak_arr_new(ek(a), a->shape, 0);
	e->h.flags = a->h.flags;
	ak_release(a);
	args[0].p = e;
	VOID;
}

static ak_val arr_pop(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	if (a->len == 0) RET_P(NULL);
	ak_val v = ak_arr_remove_at((ak_arr **)&args[0].p, a->len - 1);
	if (ek(a) == AK_K_REF) RET_P(v.p);
	RET_P(ak_box_new(ek(a), v));
}

static ak_val opt(ak_kind k, ak_val v) {
	// An optional of kind k: references as they are (retained), scalars
	// boxed.
	if (k == AK_K_REF) RET_P(ak_retain(v.p));
	RET_P(ak_box_new(k, v));
}

static ak_val arr_first(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	if (!a->len) RET_P(NULL);
	return opt(ek(a), a->data[0]);
}

static ak_val arr_last(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	if (!a->len) RET_P(NULL);
	return opt(ek(a), a->data[a->len - 1]);
}

static ak_val arr_get(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	if (!ak_arr_check(a, A(1).i)) RET_P(NULL);
	return opt(ek(a), a->data[A(1).i]);
}

static ak_val arr_is_empty(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(ARR(0)->len == 0); }

// arr.map(a, f, kind, shape?) where kind is the result's element kind.
static ak_val arr_map(ak_val *args, uint32_t n) {
	ak_arr *a = ARR(0);
	ak_kind k = (ak_kind)A(2).i;
	const ak_shape *shape = n > 3 ? args[3].p : NULL;
	ak_arr *out = ak_arr_new(k, shape, a->len);
	for (uint32_t i = 0; i < a->len; i++) {
		ak_val v = ak_call(CLOS(1), &a->data[i], 1);
		if (ak_error_pending()) {
			ak_release(out);
			VOID;
		}
		out->data[out->len++] = v;
		if (k == AK_K_REF && v.p && ((ak_obj *)v.p)->tag == AK_T_STR) out->h.flags = 1;
	}
	RET_P(out);
}

static ak_val arr_filter(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	ak_arr *out = NULL;
	for (uint32_t i = 0; i < a->len; i++) {
		ak_val r = ak_call(CLOS(1), &a->data[i], 1);
		if (ak_error_pending()) {
			ak_release(out);
			VOID;
		}
		if (r.i && out) {
			ak_retain_val(a->data[i], ek(a));
			out->data[out->len++] = a->data[i];
		} else if (!r.i && !out) {
			// The first one left out: copy what came before.
			out = ak_arr_new(ek(a), a->shape, a->len);
			out->h.flags = a->h.flags;
			for (uint32_t j = 0; j < i; j++) {
				ak_retain_val(a->data[j], ek(a));
				out->data[out->len++] = a->data[j];
			}
		}
	}
	RET_P(out ? out : ak_retain(a));
}

static ak_val arr_count(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	int64_t c = 0;
	for (uint32_t i = 0; i < a->len; i++) {
		ak_val r = ak_call(CLOS(1), &a->data[i], 1);
		if (ak_error_pending()) VOID;
		c += r.i != 0;
	}
	RET_I(c);
}

static ak_val arr_any(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	for (uint32_t i = 0; i < a->len; i++) {
		ak_val r = ak_call(CLOS(1), &a->data[i], 1);
		if (ak_error_pending()) VOID;
		if (r.i) RET_I(1);
	}
	RET_I(0);
}

static ak_val arr_all(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	for (uint32_t i = 0; i < a->len; i++) {
		ak_val r = ak_call(CLOS(1), &a->data[i], 1);
		if (ak_error_pending()) VOID;
		if (!r.i) RET_I(0);
	}
	RET_I(1);
}

// arr.index(a, pred): int?
static ak_val arr_index(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	for (uint32_t i = 0; i < a->len; i++) {
		ak_val r = ak_call(CLOS(1), &a->data[i], 1);
		if (ak_error_pending()) VOID;
		if (r.i) RET_P(ak_box_new(AK_K_INT, (ak_val){.i = i}));
	}
	RET_P(NULL);
}

// arr.find(a, pred): T?
static ak_val arr_find(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	for (uint32_t i = 0; i < a->len; i++) {
		ak_val r = ak_call(CLOS(1), &a->data[i], 1);
		if (ak_error_pending()) VOID;
		if (r.i) return opt(ek(a), a->data[i]);
	}
	RET_P(NULL);
}

static ak_val arr_contains(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	for (uint32_t i = 0; i < a->len; i++)
		if (ak_val_eq(a->data[i], A(1), ek(a))) RET_I(1);
	RET_I(0);
}

static ak_val arr_sum(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	if (ek(a) == AK_K_FLOAT) {
		double s = 0;
		for (uint32_t i = 0; i < a->len; i++) s += a->data[i].f;
		RET_F(s);
	}
	int64_t s = 0;
	for (uint32_t i = 0; i < a->len; i++) s += a->data[i].i;
	RET_I(s);
}

static ak_val arr_reversed(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *a = ARR(0);
	ak_arr *out = ak_arr_new(ek(a), a->shape, a->len);
	out->h.flags = a->h.flags;
	for (uint32_t i = a->len; i-- > 0;) {
		ak_retain_val(a->data[i], ek(a));
		out->data[out->len++] = a->data[i];
	}
	RET_P(out);
}

static ak_val arr_slice(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_arr_slice(ARR(0), A(1).i, A(2).i)); }
static ak_val arr_concat(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_arr_concat(ARR(0), ARR(1))); }

// Sorting: a stable merge sort, by a "less" closure or, without one, by
// the elements' natural order (numbers, strings).

typedef struct {
	ak_closure *less;
	ak_kind k;
	bool failed;
} sort_ctx;

static bool lt(sort_ctx *c, ak_val a, ak_val b) {
	if (c->failed) return false;
	if (c->less) {
		ak_val pair[2] = {a, b};
		ak_val r = ak_call(c->less, pair, 2);
		if (ak_error_pending()) {
			c->failed = true;
			return false;
		}
		return r.i != 0;
	}
	switch (c->k) {
	case AK_K_FLOAT: return a.f < b.f;
	case AK_K_REF: return ak_str_cmp(a.p, b.p) < 0;
	default: return a.i < b.i;
	}
}

static void msort(sort_ctx *c, ak_val *v, ak_val *tmp, uint32_t n) {
	if (n < 2) return;
	if (n <= 12) {
		for (uint32_t i = 1; i < n; i++) {
			ak_val x = v[i];
			uint32_t j = i;
			while (j > 0 && lt(c, x, v[j - 1])) {
				v[j] = v[j - 1];
				j--;
			}
			v[j] = x;
		}
		return;
	}
	uint32_t h = n / 2;
	msort(c, v, tmp, h);
	msort(c, v + h, tmp, n - h);
	uint32_t i = 0, j = h, k = 0;
	while (i < h && j < n) tmp[k++] = lt(c, v[j], v[i]) ? v[j++] : v[i++];
	while (i < h) tmp[k++] = v[i++];
	while (j < n) tmp[k++] = v[j++];
	memcpy(v, tmp, sizeof(ak_val) * n);
}

static ak_val arr_sorted(ak_val *args, uint32_t n) {
	ak_arr *a = ak_arr_copy(ARR(0));
	sort_ctx c = {n > 1 ? CLOS(1) : NULL, ek(a), false};
	ak_val *tmp = ak_alloc(sizeof(ak_val) * (a->len ? a->len : 1));
	msort(&c, a->data, tmp, a->len);
	ak_free(tmp);
	if (c.failed) {
		ak_release(a);
		VOID;
	}
	RET_P(a);
}

static ak_val arr_sort(ak_val *args, uint32_t n) {
	ak_arr *a = ak_arr_mut((ak_arr **)&args[0].p);
	sort_ctx c = {n > 1 ? CLOS(1) : NULL, ek(a), false};
	ak_val *tmp = ak_alloc(sizeof(ak_val) * (a->len ? a->len : 1));
	msort(&c, a->data, tmp, a->len);
	ak_free(tmp);
	VOID;
}

// arr.range(from, to): [from, to)
static ak_val arr_range(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t from = A(0).i, to = A(1).i;
	uint32_t len = to > from ? (uint32_t)(to - from) : 0;
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, len);
	for (uint32_t i = 0; i < len; i++) a->data[i].i = from + i;
	a->len = len;
	RET_P(a);
}

// ---- Maps ----

static ak_val map_has(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_val v;
	RET_I(ak_map_get(MAP(0), A(1), &v));
}

static ak_val map_delete(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	RET_I(ak_map_delete((ak_map **)&args[0].p, A(1)));
}

static ak_val map_keys(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_map_keys(MAP(0))); }
static ak_val map_values(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_map_values(MAP(0))); }
static ak_val map_is_empty(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(MAP(0)->live == 0); }

// ---- Numbers ----

static ak_val m_sqrt(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(sqrt(A(0).f)); }
static ak_val m_pow(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(pow(A(0).f, A(1).f)); }
static ak_val m_floor(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(floor(A(0).f)); }
static ak_val m_ceil(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(ceil(A(0).f)); }
static ak_val m_round(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(round(A(0).f)); }
static ak_val m_sin(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(sin(A(0).f)); }
static ak_val m_cos(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(cos(A(0).f)); }
static ak_val m_atan2(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(atan2(A(0).f, A(1).f)); }
static ak_val m_absf(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(fabs(A(0).f)); }
static ak_val m_absi(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(A(0).i < 0 ? -A(0).i : A(0).i); }
static ak_val m_mini(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(A(0).i < A(1).i ? A(0).i : A(1).i); }
static ak_val m_maxi(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_I(A(0).i > A(1).i ? A(0).i : A(1).i); }
static ak_val m_minf(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(fmin(A(0).f, A(1).f)); }
static ak_val m_maxf(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(fmax(A(0).f, A(1).f)); }
static ak_val m_clampf(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_F(fmin(fmax(A(0).f, A(1).f), A(2).f)); }

static uint64_t rng_state = 0x853c49e6748fea9bull;

static uint64_t rng_next(void) {
	// splitmix64
	uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}

static ak_val m_random(ak_val *args, uint32_t n) {
	AK_UNUSED(args);
	AK_UNUSED(n);
	RET_F((double)(rng_next() >> 11) * (1.0 / 9007199254740992.0));
}

static ak_val m_random_int(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t lo = A(0).i, hi = A(1).i;
	if (hi <= lo) RET_I(lo);
	RET_I(lo + (int64_t)(rng_next() % (uint64_t)(hi - lo)));
}

static ak_val m_seed(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	rng_state = (uint64_t)A(0).i;
	VOID;
}

// ---- Time ----

static ak_val t_sleep(ak_val *args, uint32_t n) { AK_UNUSED(n); RET_P(ak_sleep(A(0).i)); }
static ak_val t_now(ak_val *args, uint32_t n) { AK_UNUSED(args); AK_UNUSED(n); RET_I(ak_wall_ns()); }
static ak_val t_monotonic(ak_val *args, uint32_t n) { AK_UNUSED(args); AK_UNUSED(n); RET_I(ak_sched_now()); }

// ---- Futures ----

// all(futures): a future of their results, in order, failing with the
// first error (and cancelling the rest).
typedef struct {
	ak_future *out;
	ak_arr *fs;
	uint32_t left;
} all_ctx;

static void all_drop(void *p) {
	all_ctx *c = p;
	ak_release(c->fs);
	ak_free(c);
}

static void all_step(void *p);

typedef struct {
	all_ctx *c;
	uint32_t i;
} all_item;

static void all_cancel(ak_future *f) {
	all_ctx *c = f->ctx;
	for (uint32_t i = 0; i < c->fs->len; i++) ak_future_cancel(c->fs->data[i].p);
}

static void all_one(void *p) {
	all_item *it = p;
	all_ctx *c = it->c;
	ak_free(it);
	if (c->out->state != AK_FUT_PENDING) {
		ak_release(c->out);
		return;
	}
	if (--c->left == 0) all_step(c);
	else {
		// Fail fast on an error.
		for (uint32_t i = 0; i < c->fs->len; i++) {
			ak_future *f = c->fs->data[i].p;
			if (f->state == AK_FUT_FAILED) {
				all_step(c);
				break;
			}
		}
	}
	ak_release(c->out);
}

static void all_step(void *p) {
	all_ctx *c = p;
	for (uint32_t i = 0; i < c->fs->len; i++) {
		ak_future *f = c->fs->data[i].p;
		if (f->state == AK_FUT_FAILED) {
			ak_future_reject(c->out, ak_retain(f->error));
			for (uint32_t j = 0; j < c->fs->len; j++) ak_future_cancel(c->fs->data[j].p);
			return;
		}
	}
	if (c->left) return;
	ak_kind k = c->fs->len ? (ak_kind)((ak_future *)c->fs->data[0].p)->kind : AK_K_VOID;
	ak_arr *res = ak_arr_new(k == AK_K_VOID ? AK_K_INT : k, NULL, c->fs->len);
	for (uint32_t i = 0; i < c->fs->len; i++) {
		ak_future *f = c->fs->data[i].p;
		ak_retain_val(f->result, k);
		res->data[res->len++] = f->result;
	}
	ak_future_resolve(c->out, (ak_val){.p = res});
}

static ak_val f_all(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *fs = ARR(0);
	ak_future *out = ak_future_new(AK_K_REF);
	all_ctx *c = ak_alloc(sizeof *c);
	c->out = out;
	c->fs = ak_retain(fs);
	c->left = fs->len;
	out->ctx = c;
	out->drop_ctx = all_drop;
	out->cancel = all_cancel;
	if (fs->len == 0) {
		all_step(c);
		RET_P(out);
	}
	for (uint32_t i = 0; i < fs->len; i++) {
		all_item *it = ak_alloc(sizeof *it);
		it->c = c;
		it->i = i;
		ak_retain(out);
		ak_future_wait(fs->data[i].p, all_one, it);
	}
	RET_P(out);
}

// race(futures): the first to settle; the rest are cancelled.
typedef struct {
	ak_future *out;
	ak_arr *fs;
} race_ctx;

static void race_drop(void *p) {
	race_ctx *c = p;
	ak_release(c->fs);
	ak_free(c);
}

static void race_cancel(ak_future *f) {
	race_ctx *c = f->ctx;
	for (uint32_t i = 0; i < c->fs->len; i++) ak_future_cancel(c->fs->data[i].p);
}

static void race_one(void *p) {
	ak_future *out = p;
	race_ctx *c = out->ctx;
	if (out->state == AK_FUT_PENDING && c) {
		for (uint32_t i = 0; i < c->fs->len; i++) {
			ak_future *f = c->fs->data[i].p;
			if (f->state == AK_FUT_DONE) {
				ak_retain_val(f->result, (ak_kind)f->kind);
				ak_future_resolve(out, f->result);
				break;
			}
			if (f->state == AK_FUT_FAILED) {
				ak_future_reject(out, ak_retain(f->error));
				break;
			}
		}
		if (out->state != AK_FUT_PENDING)
			for (uint32_t i = 0; i < c->fs->len; i++) ak_future_cancel(c->fs->data[i].p);
	}
	ak_release(out);
}

static ak_val f_race(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *fs = ARR(0);
	ak_kind k = fs->len ? (ak_kind)((ak_future *)fs->data[0].p)->kind : AK_K_VOID;
	ak_future *out = ak_future_new(k);
	race_ctx *c = ak_alloc(sizeof *c);
	c->out = out;
	c->fs = ak_retain(fs);
	out->ctx = c;
	out->drop_ctx = race_drop;
	out->cancel = race_cancel;
	for (uint32_t i = 0; i < fs->len; i++) {
		ak_retain(out);
		ak_future_wait(fs->data[i].p, race_one, out);
	}
	RET_P(out);
}

const ak_lib_entry ak_lib[] = {
	{"print", lib_print},
	{"panic", lib_panic},
	{"error", lib_error},
	{"error.message", lib_error_message},
	{"rt.freeze", rt_freeze},

	{"str.len", str_len},
	{"str.bytes_len", str_bytes_len},
	{"str.upper", str_upper},
	{"str.lower", str_lower},
	{"str.trim", str_trim},
	{"str.contains", str_contains},
	{"str.has_prefix", str_has_prefix},
	{"str.has_suffix", str_has_suffix},
	{"str.replace", str_replace},
	{"str.split", str_split},
	{"str.find", str_find},
	{"str.slice", str_slice},
	{"str.is_empty", str_is_empty},
	{"str.to_int", str_to_int},
	{"str.to_float", str_to_float},
	{"str.repeat", str_repeat},
	{"str.join", str_join},
	{"str.chars", str_chars},
	{"str.fixed", str_from_float_digits},

	{"arr.push", arr_push},
	{"arr.insert", arr_insert},
	{"arr.remove_at", arr_remove_at},
	{"arr.remove_where", arr_remove_where},
	{"arr.clear", arr_clear},
	{"arr.pop", arr_pop},
	{"arr.first", arr_first},
	{"arr.last", arr_last},
	{"arr.get", arr_get},
	{"arr.is_empty", arr_is_empty},
	{"arr.map", arr_map},
	{"arr.filter", arr_filter},
	{"arr.count", arr_count},
	{"arr.any", arr_any},
	{"arr.all", arr_all},
	{"arr.index", arr_index},
	{"arr.find", arr_find},
	{"arr.contains", arr_contains},
	{"arr.sum", arr_sum},
	{"arr.reversed", arr_reversed},
	{"arr.slice", arr_slice},
	{"arr.concat", arr_concat},
	{"arr.sorted", arr_sorted},
	{"arr.sort", arr_sort},
	{"arr.range", arr_range},

	{"map.has", map_has},
	{"map.delete", map_delete},
	{"map.keys", map_keys},
	{"map.values", map_values},
	{"map.is_empty", map_is_empty},

	{"math.sqrt", m_sqrt},
	{"math.pow", m_pow},
	{"math.floor", m_floor},
	{"math.ceil", m_ceil},
	{"math.round", m_round},
	{"math.sin", m_sin},
	{"math.cos", m_cos},
	{"math.atan2", m_atan2},
	{"math.absf", m_absf},
	{"math.absi", m_absi},
	{"math.mini", m_mini},
	{"math.maxi", m_maxi},
	{"math.minf", m_minf},
	{"math.maxf", m_maxf},
	{"math.clampf", m_clampf},
	{"math.random", m_random},
	{"math.random_int", m_random_int},
	{"math.seed", m_seed},

	{"time.sleep", t_sleep},
	{"time.now", t_now},
	{"time.monotonic", t_monotonic},

	{"async.all", f_all},
	{"async.race", f_race},
	{NULL, NULL},
};
