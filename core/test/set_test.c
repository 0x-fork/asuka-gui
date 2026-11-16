// Sets: ak_set_* over the insertion-ordered map (rt/set.c).
#include "t.h"
#include "rt/rt.h"

static ak_val I(int64_t i) { return (ak_val){.i = i}; }
static ak_val S(const char *s) { return (ak_val){.p = ak_str_c(s)}; }

// elems lists a set's elements of ints in order: "3 1 2".
static void elems(const ak_map *s, char *buf, size_t n) {
	size_t w = 0;
	buf[0] = 0;
	for (uint32_t i = 0; i < s->len && w < n; i++) {
		if (s->dead[i]) continue;
		w += (size_t)snprintf(buf + w, n - w, w ? " %lld" : "%lld", (long long)s->keys[i].i);
	}
}

TEST(set_insert_remove_order) {
	ak_map *s = ak_set_new(AK_K_INT, false);
	CHECK(ak_map_is_set(s));
	CHECK(ak_set_insert(&s, I(3)));
	CHECK(ak_set_insert(&s, I(1)));
	CHECK(ak_set_insert(&s, I(2)));
	CHECK(!ak_set_insert(&s, I(3)));  // already: stays where it is
	CHECK_EQ_INT(s->live, 3);
	char buf[64];
	elems(s, buf, sizeof buf);
	CHECK_EQ_STR(buf, "3 1 2");
	CHECK(ak_set_remove(&s, I(1)));
	CHECK(!ak_set_remove(&s, I(1)));
	CHECK(!ak_set_has(s, I(1)));
	CHECK(ak_set_insert(&s, I(1)));  // again: last
	elems(s, buf, sizeof buf);
	CHECK_EQ_STR(buf, "3 2 1");
	ak_release(s);
}

TEST(set_strings_cow) {
	ak_map *s = ak_set_new(AK_K_REF, true);
	ak_set_insert(&s, S("a"));
	ak_set_insert(&s, S("b"));
	ak_val dup = S("a");
	CHECK(!ak_set_insert(&s, dup));  // takes (and releases) dup
	ak_map *shared = ak_retain(s);
	ak_set_insert(&shared, S("c"));
	CHECK(shared != s);
	CHECK(ak_map_is_set(shared));  // the copy is a set too
	CHECK_EQ_INT(s->live, 2);
	CHECK_EQ_INT(shared->live, 3);
	ak_val c = S("c");
	CHECK(!ak_set_has(s, c));
	CHECK(ak_set_has(shared, c));
	ak_release(c.p);
	ak_release(s);
	ak_release(shared);
}

TEST(set_algebra) {
	ak_map *a = ak_set_new(AK_K_INT, false), *b = ak_set_new(AK_K_INT, false);
	for (int i = 1; i <= 4; i++) ak_set_insert(&a, I(i));
	for (int i = 3; i <= 5; i++) ak_set_insert(&b, I(i));
	char buf[64];
	ak_map *u = ak_set_union(a, b), *n = ak_set_intersection(a, b), *d = ak_set_subtracting(a, b);
	elems(u, buf, sizeof buf);
	CHECK_EQ_STR(buf, "1 2 3 4 5");
	elems(n, buf, sizeof buf);
	CHECK_EQ_STR(buf, "3 4");
	elems(d, buf, sizeof buf);
	CHECK_EQ_STR(buf, "1 2");
	CHECK(ak_map_is_set(u) && ak_map_is_set(n) && ak_map_is_set(d));
	CHECK(ak_set_is_subset(n, a));
	CHECK(ak_set_is_subset(n, b));
	CHECK(!ak_set_is_subset(a, b));
	CHECK(ak_set_is_subset(a, u));
	ak_map *e = ak_set_new(AK_K_INT, false);
	CHECK(ak_set_is_subset(e, a));
	ak_release(u); ak_release(n); ak_release(d); ak_release(e);
	ak_release(a); ak_release(b);
}

TEST(set_eq_copy_freeze) {
	ak_map *a = ak_set_new(AK_K_REF, true), *b = ak_set_new(AK_K_REF, true);
	ak_set_insert(&a, S("x"));
	ak_set_insert(&a, S("y"));
	ak_set_insert(&b, S("y"));
	ak_set_insert(&b, S("x"));
	CHECK(ak_val_eq((ak_val){.p = a}, (ak_val){.p = b}, AK_K_REF));  // order doesn't count
	ak_set_insert(&b, S("z"));
	CHECK(!ak_val_eq((ak_val){.p = a}, (ak_val){.p = b}, AK_K_REF));
	ak_val c = ak_deep_copy((ak_val){.p = a}, AK_K_REF);
	CHECK(c.p != a && ak_map_is_set(c.p));
	CHECK(ak_val_eq(c, (ak_val){.p = a}, AK_K_REF));
	ak_release(c.p);
	ak_release(b);
	ak_freeze((ak_val){.p = a}, AK_K_REF);
	CHECK_EQ_INT(a->h.rc, AK_RC_IMMORTAL);
}

TEST(set_from_arr) {
	ak_arr *xs = ak_arr_new(AK_K_INT, NULL, 4);
	int64_t v[] = {5, 6, 5, 7};
	for (int i = 0; i < 4; i++) ak_arr_push(&xs, I(v[i]));
	ak_map *s = ak_set_from_arr(xs);
	char buf[64];
	elems(s, buf, sizeof buf);
	CHECK_EQ_STR(buf, "5 6 7");
	ak_release(s);
	ak_release(xs);
}

// Asking a set, inserting what it has, removing and comparing allocate
// nothing; neither does inserting while it has room (60 elements in
// room for 64).
TEST(set_allocs) {
	ak_map *s = ak_set_new(AK_K_REF, true), *t = ak_set_new(AK_K_REF, true);
	ak_val keys[60];
	char buf[16];
	for (int i = 0; i < 60; i++) {
		int n = snprintf(buf, sizeof buf, "k%d", i);
		keys[i] = (ak_val){.p = ak_str_new(buf, (size_t)n)};
		ak_set_insert(&s, (ak_val){.p = ak_retain(keys[i].p)});
		if (i % 2 == 0) ak_set_insert(&t, (ak_val){.p = ak_retain(keys[i].p)});
	}
	uint64_t before = ak_alloc_count;
	int found = 0;
	for (int i = 0; i < 60; i++) found += ak_set_has(s, keys[i]);
	for (int i = 0; i < 60; i++) CHECK(!ak_set_insert(&s, (ak_val){.p = ak_retain(keys[i].p)}));
	CHECK(ak_set_is_subset(t, s));
	CHECK(!ak_val_eq((ak_val){.p = s}, (ak_val){.p = t}, AK_K_REF));
	CHECK(ak_set_remove(&s, keys[3]));
	CHECK(ak_set_insert(&s, (ak_val){.p = ak_retain(keys[3].p)}));  // into the room it had
	CHECK_EQ_INT(ak_alloc_count - before, 0);
	CHECK_EQ_INT(found, 60);
	for (int i = 0; i < 60; i++) ak_release(keys[i].p);
	ak_release(s);
	ak_release(t);
}

T_MAIN();
