// B-trees: std/collections' SortedMap and SortedSet (rt/tree.c), against
// a model, shared and copied, and walked by what walks values (obj.c's
// destroying, comparing, hashing, copying, freezing; keep.c).
#include "t.h"
#include "rt/rt.h"
#include "vm/keep.h"

static ak_val I(int64_t i) { return (ak_val){.i = i}; }
static ak_tree_order INTS = {.mode = AK_TM_INT};
static ak_tree_order STRS = {.mode = AK_TM_STR};

static uint64_t rng = 88172645463325252ull;
static uint64_t next(void) {
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

// check_node checks n's subtree: its keys in order, between lo and hi
// (ints), its count of keys, every leaf at depth, what's past its keys
// NULL; its entries' count.
static int64_t check_node(const ak_tree *t, const ak_tnode *n, int depth, int64_t lo, int64_t hi, bool root, bool *ok) {
	uint32_t len = ak_tnode_len(n);
	if (!root && (len < AK_TREE_MIN || len > AK_TREE_MAX)) *ok = false;
	const ak_val *k = ak_tnode_keys(n);
	for (uint32_t i = 0; i < len; i++) {
		if (k[i].i <= lo || k[i].i >= hi) *ok = false;
		if (i > 0 && k[i - 1].i >= k[i].i) *ok = false;
	}
	// The unused slots of each group of references are NULL.
	if (t->kk == AK_K_REF)
		for (uint32_t i = len; i < AK_TREE_MAX; i++) *ok &= k[i].p == NULL;
	if (t->vk == AK_K_REF)
		for (uint32_t i = len; i < AK_TREE_MAX; i++) *ok &= ak_tnode_vals(n)[i].p == NULL;
	if (!ak_tnode_leaf(n))
		for (uint32_t i = len + 1; i <= AK_TREE_MAX; i++) *ok &= ak_tnode_kid(n, i) == NULL;
	if (ak_tnode_leaf(n)) {
		if (depth != t->height) *ok = false;
		return len;
	}
	int64_t c = len;
	for (uint32_t i = 0; i <= len; i++)
		c += check_node(t, ak_tnode_kid(n, i), depth + 1, i ? k[i - 1].i : lo, i < len ? k[i].i : hi, false, ok);
	return c;
}

static bool valid(const ak_tree *t) {
	bool ok = true;
	if (!t->root.p) return t->count == 0;
	int64_t c = check_node(t, t->root.p, 0, INT64_MIN, INT64_MAX, true, &ok);
	return ok && c == t->count;
}

// ints lists a tree's keys in order.
static int64_t keys_of(const ak_tree *t, int64_t *out) {
	ak_titer it;
	ak_titer_start(&it, t);
	const ak_tnode *n;
	uint32_t i;
	int64_t c = 0;
	while (ak_titer_next(&it, &n, &i)) out[c++] = ak_tnode_keys(n)[i].i;
	return c;
}

TEST(tree_against_a_model) {
	enum { N = 4000 };
	static int64_t model[N];  // the value of k, or -1
	static int64_t got[N];
	for (int i = 0; i < N; i++) model[i] = -1;
	ak_tree *t = ak_tree_new(AK_TM_INT, AK_K_INT, AK_K_INT);
	for (int step = 0; step < 60000; step++) {
		int64_t k = (int64_t)(next() % N);
		if (next() % 3 == 0) {
			ak_val v = {0};
			bool gone = ak_tree_delete(t, INTS, I(k), &v);
			CHECK_EQ_INT(gone, model[k] >= 0);
			if (gone) CHECK_EQ_INT(v.i, model[k]);
			model[k] = -1;
		} else {
			CHECK_EQ_INT(ak_tree_put(t, INTS, I(k), I(step)), model[k] < 0);
			model[k] = step;
		}
		if (step % 5000 == 0) REQUIRE(valid(t));
	}
	REQUIRE(valid(t));
	int64_t c = keys_of(t, got), j = 0;
	for (int k = 0; k < N; k++) {
		if (model[k] < 0) continue;
		REQUIRE(j < c);
		CHECK_EQ_INT(got[j], k);
		j++;
		const ak_tnode *at;
		uint32_t i;
		REQUIRE(ak_tree_find(t, INTS, I(k), &at, &i));
		CHECK_EQ_INT(ak_tnode_vals(at)[i].i, model[k]);
	}
	CHECK_EQ_INT(j, c);
	// Nearest keys.
	const ak_tnode *at;
	uint32_t i;
	for (int64_t k = -1; k <= N; k += 97) {
		int64_t below = -1, above = -1;
		for (int64_t x = k - 1; x >= 0; x--)
			if (x < N && model[x] >= 0) {
				below = x;
				break;
			}
		for (int64_t x = k + 1; x < N; x++)
			if (x >= 0 && model[x] >= 0) {
				above = x;
				break;
			}
		bool lo = ak_tree_near(t, INTS, I(k), true, false, &at, &i);
		CHECK_EQ_INT(lo ? ak_tnode_keys(at)[i].i : -1, below);
		bool hi = ak_tree_near(t, INTS, I(k), false, false, &at, &i);
		CHECK_EQ_INT(hi ? ak_tnode_keys(at)[i].i : -1, above);
	}
	// Everything out, one by one.
	for (int k = 0; k < N; k++)
		if (model[k] >= 0) CHECK(ak_tree_delete(t, INTS, I(k), NULL));
	CHECK(t->count == 0 && t->root.p == NULL && valid(t));
	ak_release(t);
}

TEST(tree_appended_in_order) {
	ak_tree *t = ak_tree_new(AK_TM_INT, AK_K_INT, AK_K_VOID);
	for (int64_t k = 0; k < 10000; k++) ak_tree_append(t, I(k * 3), (ak_val){0});
	REQUIRE(valid(t));
	CHECK_EQ_INT(t->count, 10000);
	const ak_tnode *at;
	uint32_t i;
	CHECK(ak_tree_find(t, INTS, I(2997), &at, &i) && !ak_tree_find(t, INTS, I(2998), &at, &i));
	ak_release(t);
}

// strs makes a tree of n strings "k<i>" to their i, as SortedMap<string,
// int> keeps them.
static ak_tree *strs(int n) {
	ak_tree *t = ak_tree_new(AK_TM_STR, AK_K_REF, AK_K_INT);
	char buf[32];
	for (int i = 0; i < n; i++) {
		snprintf(buf, sizeof buf, "k%05d", (i * 7919) % n);
		ak_str *s = ak_str_c(buf);
		ak_tree_put(t, STRS, (ak_val){.p = s}, I((i * 7919) % n));
		ak_release(s);
	}
	return t;
}

TEST(tree_copies_share_their_nodes) {
	int64_t live = ak_obj_live;
	ak_tree *a = strs(3000);
	ak_tree *b = ak_retain(a);
	// b changes: a copy of its header and of the nodes on the paths it
	// changes; a stays as it was.
	ak_tree_mut(&b);
	REQUIRE(a != b && a->root.p == b->root.p);
	for (int i = 0; i < 3000; i += 2) {
		char buf[32];
		snprintf(buf, sizeof buf, "k%05d", i);
		ak_str *s = ak_str_c(buf);
		CHECK(ak_tree_delete(b, STRS, (ak_val){.p = s}, NULL));
		ak_release(s);
	}
	CHECK_EQ_INT(a->count, 3000);
	CHECK_EQ_INT(b->count, 1500);
	CHECK(!ak_val_eq((ak_val){.p = a}, (ak_val){.p = b}, AK_K_REF));
	ak_str *k = ak_str_c("k00002");
	const ak_tnode *at;
	uint32_t i;
	CHECK(ak_tree_find(a, STRS, (ak_val){.p = k}, &at, &i) && !ak_tree_find(b, STRS, (ak_val){.p = k}, &at, &i));
	ak_release(k);
	// Equal by their entries, however they were made; hashed alike.
	ak_tree *c = strs(3000);
	CHECK(ak_val_eq((ak_val){.p = a}, (ak_val){.p = c}, AK_K_REF));
	CHECK_EQ_INT(ak_val_hash((ak_val){.p = a}, AK_K_REF), ak_val_hash((ak_val){.p = c}, AK_K_REF));
	// Copied for another thread: equal, nothing shared.
	ak_val d = ak_deep_copy((ak_val){.p = b}, AK_K_REF);
	CHECK(d.p != b && ((ak_tree *)d.p)->root.p != b->root.p);
	CHECK(ak_val_eq(d, (ak_val){.p = b}, AK_K_REF));
	ak_release(d.p);
	ak_release(a);
	ak_release(b);
	ak_release(c);
	CHECK_EQ_INT(ak_obj_live, live);
}

TEST(tree_frozen_then_copied) {
	ak_tree *t = strs(500);
	ak_frozen made = {0};
	ak_freeze((ak_val){.p = t}, AK_K_REF, &made);
	CHECK(t->h.rc == AK_RC_IMMORTAL && ((ak_obj *)t->root.p)->rc == AK_RC_IMMORTAL);
	ak_tree *c = t;
	ak_str *k = ak_str_c("k00007");
	CHECK(ak_tree_delete(ak_tree_mut(&c), STRS, (ak_val){.p = k}, NULL));
	CHECK(c != t && c->count == 499 && t->count == 500);
	ak_release(k);
	ak_release(c);
	ak_frozen_free(&made);
}

TEST(tree_kept_across_a_reload) {
	ak_tree *t = strs(700);
	ak_buf b = {0};
	REQUIRE(ak_keep_encode(&b, (ak_val){.p = t}, AK_K_REF));
	ak_reader r = {b.data, b.len, 0, false};
	ak_val out;
	REQUIRE(ak_keep_decode(&r, AK_K_REF, NULL, NULL, &out));
	ak_tree *u = out.p;
	CHECK(u->mode == AK_TM_STR && u->count == 700);
	CHECK(ak_val_eq(out, (ak_val){.p = t}, AK_K_REF));
	ak_buf_free(&b);
	ak_release(u);
	ak_release(t);
}

TEST(tree_nested_deep_lets_go_in_bounded_stack) {
	// A tree whose one value is a tree whose one value is a tree…: let
	// go of without recursing (a spawn's thread has 512 KB of stack).
	int64_t live = ak_obj_live;
	ak_tree *top = NULL;
	for (int i = 0; i < 200000; i++) {
		ak_tree *t = ak_tree_new(AK_TM_INT, AK_K_INT, AK_K_REF);
		if (top) {
			ak_tree_put(t, INTS, I(i), (ak_val){.p = top});
			ak_release(top);
		}
		top = t;
	}
	ak_release(top);
	CHECK_EQ_INT(ak_obj_live, live);
}
T_MAIN();
