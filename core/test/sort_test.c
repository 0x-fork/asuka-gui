// sort_test.c: sorted() and sort() (rt/lib.c) against the plain stable
// merge sort they replace: ints by the radix sort (in place past its
// buffer's size), floats with NaN and -0, strings, a less-than closure
// (stable: equal elements keep their order); sorted(key:) and sort(key:)
// (rt/seq.c), stable, ascending and descending.
#include "t.h"
#include "rt/rt.h"
#include "rt/lib.h"

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	for (const ak_lib_entry *e = ak_seq_lib; e->name; e++)
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
				// Numbers, and strings that share 8 bytes or more (the
				// prefixes sorting compares first), are prefixes of each
				// other, or hold 0 bytes.
				char s[40];
				int len = snprintf(s, sizeof s, "w%llu", (unsigned long long)(next() % (n + 5)));
				if (round % 3 == 1) {
					static const char *heads[] = {"", "a", "ab", "abcdefg", "abcdefgh", "abcdefghij", "abcdefgh\0", "ab\0"};
					int h = (int)(next() % 8);
					size_t hl = h == 6 ? 9 : h == 7 ? 3 : strlen(heads[h]);
					memcpy(s, heads[h], hl);
					len = (int)hl + (int)(next() % 3);
					for (int k = (int)hl; k < len; k++) s[k] = (char)("a\0z"[next() % 3]);
				}
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

static int cmp_i64(const void *a, const void *b) {
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
	return x < y ? -1 : x > y;
}

static int cmp_u64(const void *a, const void *b) {
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

// Ints past the radix sort's buffer, sorted in place: split by their
// highest bits that differ (all 64, or a few: a bucket as big as all of
// them, split again), signed and unsigned, many equal.
TEST(big_int_sorts) {
	static const uint32_t sizes[] = {32769, 100000, 300001};
	for (int round = 0; round < 18; round++) {
		uint32_t n = sizes[round % 3];
		int dist = round / 3;  // what the ints are
		bool unsig = round % 2 == 1;
		ak_arr *a = ak_arr_new(AK_K_INT, NULL, n);
		int64_t *want = malloc(sizeof(int64_t) * n);
		for (uint32_t i = 0; i < n; i++) {
			uint64_t r = next();
			int64_t v;
			switch (dist) {
			case 0: v = (int64_t)r; break;                                     // all 64 bits
			case 1: v = (int64_t)(r % 2147483648u); break;                    // 31 bits, as the benchmarks'
			case 2: v = (int64_t)(r % 7) - 3; break;                          // few, negative
			case 3: v = r % 10 ? 1000000 + (int64_t)(r % 50000) : (int64_t)r; break; // most in one bucket
			case 4: v = (int64_t)(r & 0xff00ff00ff00ff00ull); break;          // bytes that never differ
			default: v = i % 2 ? (int64_t)i : -(int64_t)i * 4096; break;     // runs
			}
			ak_arr_vals(a)[i].i = v;
			want[i] = v;
		}
		a->len = n;
		qsort(want, n, sizeof(int64_t), unsig ? cmp_u64 : cmp_i64);
		ak_val args[4] = {{.p = a}, {.p = NULL}, {.i = 0}, {.i = unsig}};
		ak_arr *got = fn("arr.sorted")(args, 4).p;
		for (uint32_t i = 0; i < n; i++)
			if (ak_arr_vals(got)[i].i != want[i]) {
				T_FAIL("round %d (%u ints, distribution %d%s): %u is %lld, not %lld", round, n, dist, unsig ? ", unsigned" : "", i,
				       (long long)ak_arr_vals(got)[i].i, (long long)want[i]);
				break;
			}
		ak_release(got);
		ak_release(a);
		free(want);
	}
}

static ak_val key_low(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	return (ak_val){.i = args[0].i % 97};
}

// By a key: equal keys keep their order, either way; sort(key:) moves
// the elements round in place, as sorted(key:) puts them in a new array.
TEST(key_sorts) {
	ak_closure *key = ak_closure_new(key_low, NULL, 0, NULL);
	static const uint32_t sizes[] = {0, 1, 2, 13, 100, 5000, 70001};
	for (int round = 0; round < 14; round++) {
		uint32_t n = sizes[round % 7];
		bool desc = round >= 7;
		ak_arr *a = ak_arr_new(AK_K_INT, NULL, n);
		ak_val *want = malloc(sizeof(ak_val) * (n + 1));
		for (uint32_t i = 0; i < n; i++) {
			// Each int's low bits its key; its high bits where it was.
			ak_val v = {.i = (int64_t)(next() % 97) + 97 * (int64_t)i};
			ak_arr_vals(a)[i] = v;
			want[i] = v;
		}
		a->len = n;
		// The stable order: by key (ascending or descending), then by place.
		for (uint32_t i = 1; i < n; i++) {
			ak_val x = want[i];
			uint32_t j = i;
			while (j > 0 && (desc ? want[j - 1].i % 97 < x.i % 97 : want[j - 1].i % 97 > x.i % 97)) {
				want[j] = want[j - 1];
				j--;
			}
			want[j] = x;
		}
		ak_val args[5] = {{.p = a}, {.p = key}, {.i = desc}, {.p = NULL}, {.i = AK_K_INT}};
		ak_arr *got = fn("seq.arr_sorted_key")(args, 5).p;
		if (got->len != n || !same_bits(got, want)) T_FAIL("round %d (%u, %s): sorted(key:) differently", round, n, desc ? "descending" : "ascending");
		fn("seq.arr_sort_key")(args, 5);  // in place
		if (!same_bits(args[0].p, want)) T_FAIL("round %d (%u): sort(key:) differently", round, n);
		ak_release(got);
		ak_release(args[0].p);
		free(want);
	}
	ak_release(key);
}

T_MAIN()
