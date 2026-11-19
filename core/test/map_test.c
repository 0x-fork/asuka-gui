// map_test.c: maps' index (rt/map.c) against a plain model: insertion
// order kept through deletes and growth, tombstones passed and reused,
// copies of shared maps, keys whose hashes collide.
#include "t.h"
#include "rt/rt.h"

static uint64_t rng = 0x2545f4914f6cdd1dull;
static uint64_t next(void) {
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

// The model: keys in insertion order, a value each, a flag for removed.
typedef struct {
	int64_t keys[4096], vals[4096];
	bool gone[4096];
	int n;
} model;

static int model_find(const model *md, int64_t k) {
	for (int i = 0; i < md->n; i++)
		if (!md->gone[i] && md->keys[i] == k) return i;
	return -1;
}

static void model_set(model *md, int64_t k, int64_t v) {
	int i = model_find(md, k);
	if (i >= 0) {
		md->vals[i] = v;
		return;
	}
	md->keys[md->n] = k, md->vals[md->n] = v, md->gone[md->n] = false;
	md->n++;
}

static bool model_del(model *md, int64_t k) {
	int i = model_find(md, k);
	if (i < 0) return false;
	md->gone[i] = true;
	return true;
}

// same: m holds the model's entries, in its order.
static bool same(const ak_map *m, const model *md) {
	uint32_t live = 0;
	for (int i = 0; i < md->n; i++) live += !md->gone[i];
	if (m->live != live) return false;
	ak_arr *ks = ak_map_keys(m), *vs = ak_map_values(m);
	uint32_t j = 0;
	bool ok = true;
	for (int i = 0; i < md->n && ok; i++) {
		if (md->gone[i]) continue;
		ok = ks->data[j].i == md->keys[i] && vs->data[j].i == md->vals[i];
		ak_val v;
		ok = ok && ak_map_get(m, (ak_val){.i = md->keys[i]}, &v) && v.i == md->vals[i];
		j++;
	}
	ak_release(ks);
	ak_release(vs);
	return ok;
}

// Keys whose hashes share their low bits: one long probe sequence.
static int64_t colliding[600];

static void make_colliding(void) {
	int n = 0;
	for (int64_t k = 0; n < (int)AK_LEN(colliding); k++) {
		uint64_t h = ak_val_hash((ak_val){.i = k}, AK_K_INT);
		if (((uint32_t)(h ^ h >> 32) & 1023) == 77) colliding[n++] = k;
	}
}

TEST(map_as_model) {
	make_colliding();
	for (int round = 0; round < 40; round++) {
		model *md = calloc(1, sizeof *md);
		ak_map *m = ak_map_new(AK_K_INT, false, AK_K_INT, NULL);
		ak_map *shared = NULL;
		for (int op = 0; op < 3000 && md->n < 4000; op++) {
			int64_t k = round % 2 ? colliding[next() % 600] : (int64_t)(next() % 500) - 250;
			switch (next() % 10) {
			case 0:
			case 1:
			case 2: {
				bool had = ak_map_delete(&m, (ak_val){.i = k});
				CHECK(had == model_del(md, k));
				break;
			}
			case 3:
				// A copy that's shared: the next change copies it.
				if (shared) ak_release(shared);
				shared = ak_retain(m);
				break;
			default: {
				int64_t v = (int64_t)next();
				ak_map_set(&m, (ak_val){.i = k}, (ak_val){.i = v});
				model_set(md, k, v);
			}
			}
			if (op % 97 == 0 && !same(m, md)) {
				T_FAIL("round %d, op %d: the map isn't the model", round, op);
				break;
			}
		}
		CHECK(same(m, md));
		if (shared) ak_release(shared);
		ak_release(m);
		free(md);
	}
}

TEST(map_string_keys) {
	// Keys equal but not the same object find each other; growth from
	// empty through tens of thousands keeps them all.
	ak_map *m = ak_map_new(AK_K_REF, true, AK_K_INT, NULL);
	for (int i = 0; i < 40000; i++) {
		char k[24];
		int n = snprintf(k, sizeof k, "key%d", i);
		ak_map_set(&m, (ak_val){.p = ak_str_new(k, (size_t)n)}, (ak_val){.i = i});
	}
	CHECK_EQ_INT(m->live, 40000);
	for (int i = 0; i < 40000; i += 7) {
		char k[24];
		int n = snprintf(k, sizeof k, "key%d", i);
		ak_str *s = ak_str_new(k, (size_t)n);
		ak_val v = {0};
		CHECK(ak_map_get(m, (ak_val){.p = s}, &v) && v.i == i);
		if (i % 2) CHECK(ak_map_delete(&m, (ak_val){.p = s}));
		ak_release(s);
	}
	ak_str *none = ak_str_c("key-1");
	ak_val v;
	CHECK(!ak_map_get(m, (ak_val){.p = none}, &v));
	ak_release(none);
	// Insertion order, the deleted aside.
	ak_arr *ks = ak_map_keys(m);
	CHECK_EQ_STR(((ak_str *)ks->data[0].p)->data, "key0");
	CHECK_EQ_STR(((ak_str *)ks->data[7].p)->data, "key8");
	ak_release(ks);
	ak_release(m);
}

// A set of references other than strings, flagged as sets of any
// references are: equal values that are different objects are one.
TEST(set_of_values_not_strings) {
	ak_map *s = ak_set_new(AK_K_REF, true);
	for (int i = 0; i < 100; i++) {
		ak_bytes *b = ak_bytes_new("\x00\x01\x02", 3);
		b->data[0] = (uint8_t)(i % 10);
		ak_set_insert(&s, (ak_val){.p = b});
	}
	CHECK_EQ_INT(s->live, 10);
	ak_bytes *k = ak_bytes_new("\x07\x01\x02", 3);
	CHECK(ak_set_has(s, (ak_val){.p = k}));
	ak_release(k);
	ak_release(s);
}

TEST(set_insert_once) {
	ak_map *s = ak_set_new(AK_K_REF, true);
	ak_str *a = ak_str_c("a"), *a2 = ak_str_c("a");
	CHECK(ak_set_insert(&s, (ak_val){.p = ak_retain(a)}));
	CHECK(!ak_set_insert(&s, (ak_val){.p = ak_retain(a2)}));  // the member stays the first
	CHECK(s->keys[0].p == a);
	CHECK_EQ_INT(s->live, 1);
	ak_map *t = ak_retain(s);  // shared: inserting what it has copies nothing
	CHECK(!ak_set_insert(&t, (ak_val){.p = ak_retain(a2)}));
	CHECK(t == s);
	ak_release(t);
	ak_release(s);
	ak_release(a);
	ak_release(a2);
}

T_MAIN()
