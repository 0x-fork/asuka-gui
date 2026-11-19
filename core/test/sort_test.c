// sort_test.c: sorted() and sort() (rt/lib.c) against the plain stable
// merge sort they replace: ints by the radix sort, floats with NaN and
// -0, strings, a less-than closure (stable: equal elements keep their
// order).
#include "t.h"
#include "rt/rt.h"
#include "rt/lib.h"

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	abort();
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint64_t next(void) {
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

typedef bool (*less_fn)(ak_val a, ak_val b);

static void insertion(ak_val *v, uint32_t n, less_fn lt) {
	for (uint32_t i = 1; i < n; i++) {
		ak_val x = v[i];
		uint32_t j = i;
		while (j > 0 && lt(x, v[j - 1])) {
			v[j] = v[j - 1];
			j--;
		}
		v[j] = x;
	}
}

// The merge sort sorted() always ran: what it does with NaN (which < doesn't
// order) is what it must still do.
static void merge(ak_val *v, ak_val *tmp, uint32_t n, less_fn lt) {
	if (n <= 12) {
		insertion(v, n, lt);
		return;
	}
	uint32_t h = n / 2;
	merge(v, tmp, h, lt);
	merge(v + h, tmp, n - h, lt);
	uint32_t i = 0, j = h, k = 0;
	while (i < h && j < n) tmp[k++] = lt(v[j], v[i]) ? v[j++] : v[i++];
	while (i < h) tmp[k++] = v[i++];
	while (j < n) tmp[k++] = v[j++];
	memcpy(v, tmp, sizeof(ak_val) * n);
}

static bool lt_int(ak_val a, ak_val b) { return a.i < b.i; }
static bool lt_float(ak_val a, ak_val b) { return a.f < b.f; }
static bool lt_str(ak_val a, ak_val b) { return ak_str_cmp(a.p, b.p) < 0; }
static bool lt_low(ak_val a, ak_val b) { return (a.i & 15) < (b.i & 15); }

static ak_val call_low(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	return (ak_val){.i = lt_low(args[0], args[1])};
}

static bool same_bits(const ak_arr *a, const ak_val *want) {
	for (uint32_t i = 0; i < a->len; i++)
		if (ak_arr_vals(a)[i].i != want[i].i) return false;
	return true;
}

TEST(sorted_as_insertion) {
	ak_closure *low = ak_closure_new(call_low, NULL, 0, NULL);
	for (int round = 0; round < 300; round++) {
		uint32_t n = round < 100 ? (uint32_t)round : (uint32_t)(next() % 3000);
		int kind = round % 4;  // ints, floats, strings, ints by a closure
		ak_arr *a = ak_arr_new(kind == 1 ? AK_K_FLOAT : kind == 2 ? AK_K_REF : AK_K_INT, NULL, n);
		if (kind == 2) a->h.flags = 1;
		ak_val *want = malloc(sizeof(ak_val) * (n + 1));
		for (uint32_t i = 0; i < n; i++) {
			ak_val v;
			switch (kind) {
			case 1: {
				static const double odd[] = {NAN, -0.0, 0.0, INFINITY, -INFINITY, 1.5};
				v.f = next() % 4 == 0 ? odd[next() % 6] : (double)(int64_t)(next() % 2001) - 1000;
				break;
			}
			case 2: {
				char s[24];
				int len = snprintf(s, sizeof s, "w%llu", (unsigned long long)(next() % (n + 5)));
				v.p = ak_str_new(s, (size_t)len);
				break;
			}
			default:
				// Small, huge, negative, equal: every byte matters to the
				// radix sort, and most passes are skipped for some rounds.
				v.i = round % 8 < 4 ? (int64_t)next() : (int64_t)(next() % 100) - 50;
			}
			ak_arr_vals(a)[i] = v;
			want[i] = v;
		}
		a->len = n;
		ak_val *tmp = malloc(sizeof(ak_val) * (n + 1));
		merge(want, tmp, n, kind == 1 ? lt_float : kind == 2 ? lt_str : kind == 3 ? lt_low : lt_int);
		free(tmp);
		ak_val args[2] = {{.p = a}, {.p = low}};
		ak_arr *got = fn("arr.sorted")(args, kind == 3 ? 2 : 1).p;
		if (!same_bits(got, want)) T_FAIL("round %d (kind %d, %u elements): sorted differently", round, kind, n);
		fn("arr.sort")(args, kind == 3 ? 2 : 1);  // in place
		if (!same_bits(args[0].p, want)) T_FAIL("round %d: sort differently", round);
		ak_release(got);
		ak_release(args[0].p);
		free(want);
	}
	ak_release(low);
}

T_MAIN()
