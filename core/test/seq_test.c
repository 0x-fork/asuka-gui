// Arrays as double-ended queues (rt/arr.c's room at the front), and
// lazy sequences (rt/seq.c): what a chain computes, counted, and what it
// allocates.
#include "t.h"
#include "rt/lib.h"

static ak_val I(int64_t i) { return (ak_val){.i = i}; }

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *e = ak_seq_lib; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	for (const ak_lib_entry *e = ak_lib; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	return NULL;
}

// ints lists an array of ints: "3 1 2".
static void ints(const ak_arr *a, char *buf, size_t n) {
	size_t w = 0;
	buf[0] = 0;
	for (uint32_t i = 0; i < a->len && w < n; i++)
		w += (size_t)snprintf(buf + w, n - w, w ? " %lld" : "%lld", (long long)a->data[i].i);
}

TEST(deque_both_ends) {
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, 0);
	for (int i = 0; i < 4; i++) {
		ak_arr_push(&a, I(10 + i));
		ak_arr_insert(&a, 0, I(-i));
	}
	char buf[128];
	ints(a, buf, sizeof buf);
	CHECK_EQ_STR(buf, "-3 -2 -1 0 10 11 12 13");
	CHECK_EQ_INT(ak_arr_remove_at(&a, 0).i, -3);
	CHECK_EQ_INT(ak_arr_remove_at(&a, a->len - 1).i, 13);
	ak_arr_insert(&a, 2, I(99));  // the shorter side moves
	CHECK_EQ_INT(ak_arr_remove_at(&a, 4).i, 10);
	ints(a, buf, sizeof buf);
	CHECK_EQ_STR(buf, "-2 -1 99 0 11 12");
	ak_release(a);
}

// A million pushes at the start and pops at the end (and the other way)
// allocate a buffer O(log n) times, and a queue that stays short keeps a
// short buffer: the room popping makes at the front is reused.
TEST(deque_amortized) {
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, 0);
	uint64_t n0 = ak_alloc_count;
	for (int i = 0; i < 1000000; i++) ak_arr_insert(&a, 0, I(i));
	CHECK(ak_alloc_count - n0 < 64);
	for (int i = 0; i < 1000000; i++) CHECK_EQ_INT(ak_arr_remove_at(&a, a->len - 1).i, i);
	ak_release(a);

	a = ak_arr_new(AK_K_INT, NULL, 0);
	n0 = ak_alloc_count;
	int64_t next = 0;
	for (int i = 0; i < 1000000; i++) {
		ak_arr_push(&a, I(i));
		if (a->len > 100) CHECK_EQ_INT(ak_arr_remove_at(&a, 0).i, next++);
	}
	CHECK(ak_alloc_count - n0 < 16);
	CHECK(a->front + a->cap <= 512);
	ak_release(a);
}

// The middle against a model: insertions and removals anywhere.
TEST(deque_model) {
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, 0);
	int64_t model[4096];
	uint32_t n = 0;
	uint64_t r = 7;
	for (int step = 0; step < 20000; step++) {
		r = r * 6364136223846793005u + 1442695040888963407u;
		uint32_t at = n ? (uint32_t)((r >> 33) % (n + 1)) : 0;
		if (n < 4000 && ((r >> 20) & 3) != 0) {
			ak_arr_insert(&a, at, I(step));
			memmove(model + at + 1, model + at, sizeof(int64_t) * (n - at));
			model[at] = step;
			n++;
		} else if (n) {
			if (at == n) at--;
			CHECK_EQ_INT(ak_arr_remove_at(&a, at).i, model[at]);
			memmove(model + at, model + at + 1, sizeof(int64_t) * (n - at - 1));
			n--;
		}
	}
	REQUIRE(a->len == n);
	for (uint32_t i = 0; i < n; i++) CHECK_EQ_INT(a->data[i].i, model[i]);
	// A copy has no room at the front, and is the same array.
	ak_arr *c = ak_arr_copy(a);
	CHECK_EQ_INT(c->front, 0);
	CHECK(ak_val_eq((ak_val){.p = a}, (ak_val){.p = c}, AK_K_REF));
	ak_release(c);
	ak_release(a);
}

// ---- Lazy sequences ----

static int64_t calls;

static ak_val times10(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(n);
	calls++;
	return I(args[0].i * 10);
}

static ak_val odd(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(n);
	calls++;
	return I(args[0].i % 2 != 0);
}

static ak_val fails_at_3(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(n);
	if (args[0].i == 3) ak_throw_msg("three");
	return I(1);
}

static ak_arr *range(int64_t n) {
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, (uint32_t)n);
	for (int64_t i = 0; i < n; i++) a->data[a->len++] = I(i);
	return a;
}

static ak_val call(const char *name, ak_val *args, uint32_t n) {
	ak_lib_fn f = fn(name);
	if (!f) {
		fprintf(stderr, "no builtin %s\n", name);
		abort();
	}
	return f(args, n);
}

// map, then filter, then take(3): only what the three need is computed,
// and the chain is read again from the start.
TEST(seq_laziness) {
	ak_arr *xs = range(1000);
	ak_arr *proto = ak_arr_new(AK_K_INT, NULL, 0);
	ak_closure *t10 = ak_closure_new(times10, NULL, 0, NULL), *o = ak_closure_new(odd, NULL, 0, NULL);
	ak_val a1[] = {{.p = xs}, {.p = proto}};
	ak_val s = call("seq.of_array", a1, 2);
	ak_val a2[] = {s, {.p = t10}, {.p = proto}};
	ak_val m = call("seq.map", a2, 3);
	ak_val a5[] = {s, {.p = o}};
	ak_val fo = call("seq.filter", a5, 2);
	ak_val a6[] = {fo, {.p = t10}, {.p = proto}};
	ak_val fm = call("seq.map", a6, 3);
	ak_val a7[] = {fm, {.i = 3}};
	ak_val t = call("seq.take", a7, 2);
	calls = 0;
	ak_val a8[] = {t, {.p = proto}};
	ak_val out = call("seq.to_array", a8, 2);
	char buf[64];
	ints(out.p, buf, sizeof buf);
	CHECK_EQ_STR(buf, "10 30 50");
	// odd of 0..5 (six calls) and times10 of the three odd ones.
	CHECK_EQ_INT(calls, 9);
	ak_release(out.p);
	// Read again: the same, from the start; no array per element.
	calls = 0;
	uint64_t n0 = ak_alloc_count;
	ak_val a9[] = {t};
	CHECK_EQ_INT(call("seq.len", a9, 1).i, 3);
	CHECK_EQ_INT(calls, 9);
	CHECK(ak_alloc_count - n0 <= 4);  // the cursors: one per stage
	// Counting the whole map allocates the same, however long.
	n0 = ak_alloc_count;
	ak_val a10[] = {m};
	CHECK_EQ_INT(call("seq.len", a10, 1).i, 1000);
	CHECK(ak_alloc_count - n0 <= 2);
	ak_release(t.p);
	ak_release(fm.p);
	ak_release(fo.p);
	ak_release(m.p);
	ak_release(s.p);
	ak_release(t10);
	ak_release(o);
	ak_release(proto);
	ak_release(xs);
}

// A closure that raises stops the read: the consumer passes the error
// on, and lets go of what it made.
TEST(seq_errors) {
	ak_arr *xs = range(10);
	ak_arr *proto = ak_arr_new(AK_K_INT, NULL, 0);
	ak_closure *bad = ak_closure_new(fails_at_3, NULL, 0, NULL);
	int64_t live = ak_obj_live;
	ak_val a1[] = {{.p = xs}, {.p = proto}};
	ak_val s = call("seq.of_array", a1, 2);
	ak_val a2[] = {s, {.p = bad}};
	ak_val f = call("seq.filter", a2, 2);
	ak_val a3[] = {f, {.p = proto}};
	ak_val out = call("seq.to_array", a3, 2);
	CHECK(ak_error_pending());
	CHECK(out.p == NULL);
	void *e = ak_error_take();
	CHECK_EQ_STR(ak_error_message(e)->data, "three");
	ak_release(e);
	ak_release(f.p);
	ak_release(s.p);
	CHECK_EQ_INT(ak_obj_live, live);
	ak_release(bad);
	ak_release(proto);
	ak_release(xs);
}

// windows and chunked, over an endless count.
TEST(seq_windows_chunks) {
	ak_arr *proto = ak_arr_new(AK_K_INT, NULL, 0), *aproto = ak_arr_new(AK_K_REF, NULL, 0);
	ak_val a1[] = {{.i = 1}, {.i = 1}, {.p = proto}};
	ak_val from = call("seq.from", a1, 3);
	ak_val a2[] = {from, {.i = 3}, {.p = aproto}};
	ak_val w = call("seq.windows", a2, 3);
	ak_val a3[] = {w, {.i = 3}};
	ak_val w3 = call("seq.take", a3, 2);
	ak_val a4[] = {w3, {.p = aproto}};
	ak_val out = call("seq.to_array", a4, 2);
	ak_arr *ws = out.p;
	REQUIRE(ws->len == 3);
	char buf[64];
	ints(ws->data[2].p, buf, sizeof buf);
	CHECK_EQ_STR(buf, "3 4 5");
	ak_release(out.p);
	ak_val a5[] = {from, {.i = 0}, {.p = aproto}};
	CHECK(call("seq.chunked", a5, 3).p == NULL);
	CHECK(ak_error_pending());
	ak_release(ak_error_take());
	ak_release(w3.p);
	ak_release(w.p);
	ak_release(from.p);
	ak_release(proto);
	ak_release(aproto);
}

T_MAIN();
