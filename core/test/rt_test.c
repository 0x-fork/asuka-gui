#include "t.h"
#include "rt/rt.h"
#include "rt/sched.h"
#include "rt/lib.h"
#include "base/thread.h"

#include <pthread.h>
#include <stdatomic.h>

static ak_str *S(const char *s) { return ak_str_c(s); }

TEST(str_basics) {
	ak_str *a = S("hello"), *b = S(" world");
	ak_str *c = ak_str_concat(a, b);
	CHECK_EQ_STR(c->data, "hello world");
	CHECK(ak_str_eq(c, c));
	ak_str *d = S("hello world");
	CHECK(ak_str_eq(c, d));
	CHECK_EQ_INT(ak_str_hash(c), ak_str_hash(d));
	CHECK_EQ_INT(ak_str_find(c, b), 5);
	ak_str *e = ak_str_slice(c, 6, 100);
	CHECK_EQ_STR(e->data, "world");
	ak_str *u = ak_str_upper(e);
	CHECK_EQ_STR(u->data, "WORLD");
	ak_str *h = S("héllo");
	CHECK_EQ_INT(ak_str_count(h), 5);
	ak_release(h);
	ak_release(a); ak_release(b); ak_release(c); ak_release(d); ak_release(e); ak_release(u);
}

TEST(float_format) {
	struct { double v; const char *want; } cases[] = {
		{1, "1"}, {1.5, "1.5"}, {0.1, "0.1"}, {-2.25, "-2.25"}, {1e21, "1e+21"},
		{0.1 + 0.2, "0.30000000000000004"}, {100, "100"}, {3.14159, "3.14159"}, {1e-7, "1e-7"},
		// As JavaScript prints them: the shortest digits that read back,
		// not the double's exact expansion.
		{123456789012345680000.0, "123456789012345680000"}, {9223372036854775807.0, "9223372036854776000"},
		{1e20, "100000000000000000000"}, {-1e21, "-1e+21"}, {1.5e300, "1.5e+300"}, {5e-324, "5e-324"},
		{1.2345678901234567e-6, "0.0000012345678901234567"}, {1e-6, "0.000001"}, {-1.5e-7, "-1.5e-7"},
		{1.7976931348623157e308, "1.7976931348623157e+308"}, {123.456, "123.456"}, {0.5, "0.5"},
		{2.220446049250313e-16, "2.220446049250313e-16"}, {2251799813685248.5, "2251799813685248.5"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		ak_str *s = ak_str_from_float(cases[i].v);
		CHECK_EQ_STR(s->data, cases[i].want);
		ak_release(s);
	}
}

TEST(str_to_number) {
	struct { const char *s; bool ok; int64_t v; } ints[] = {
		{"42", true, 42}, {"-7", true, -7}, {"+7", true, 7}, {"007", true, 7},
		{"9223372036854775807", true, INT64_MAX}, {"-9223372036854775808", true, INT64_MIN},
		// Long, but in range: no cap on the length.
		{"0000000000000000000000000000000000000001", true, 1},
		{"9223372036854775808", false, 0}, {"", false, 0}, {"-", false, 0}, {"+", false, 0},
		{" 12", false, 0}, {"12 ", false, 0}, {"\t12", false, 0}, {"\n12", false, 0}, {"1_000", false, 0},
		{"0x10", false, 0}, {"1.0", false, 0}, {"--1", false, 0}, {"+-1", false, 0}, {"1e3", false, 0},
	};
	for (size_t i = 0; i < AK_LEN(ints); i++) {
		ak_str *s = S(ints[i].s);
		int64_t v = 0;
		bool ok = ak_str_to_int(s, &v);
		if (ok != ints[i].ok) fprintf(stderr, "toInt(\"%s\")\n", ints[i].s);
		CHECK(ok == ints[i].ok);
		if (ok) CHECK(v == ints[i].v);
		ak_release(s);
	}
	struct { const char *s; bool ok; double v; } floats[] = {
		{"2.5", true, 2.5}, {"-2.5", true, -2.5}, {"+2.5", true, 2.5}, {".5", true, 0.5}, {"5.", true, 5},
		{"1e3", true, 1000}, {"2.5E-3", true, 0.0025}, {"1e+2", true, 100}, {"42", true, 42},
		{"Infinity", true, INFINITY}, {"-Infinity", true, -INFINITY}, {"inf", true, INFINITY},
		{"1e400", true, INFINITY},
		{"", false, 0}, {".", false, 0}, {"-", false, 0}, {"e3", false, 0}, {"1e", false, 0}, {"1e+", false, 0},
		{" 2.5", false, 0}, {"2.5 ", false, 0}, {"0x1p3", false, 0}, {"1,5", false, 0}, {"nan(1)", false, 0},
		{"infinit", false, 0}, {"1.2.3", false, 0},
	};
	for (size_t i = 0; i < AK_LEN(floats); i++) {
		ak_str *s = S(floats[i].s);
		double v = 0;
		bool ok = ak_str_to_float(s, &v);
		if (ok != floats[i].ok) fprintf(stderr, "toFloat(\"%s\")\n", floats[i].s);
		CHECK(ok == floats[i].ok);
		if (ok) CHECK(v == floats[i].v);
		ak_release(s);
	}
	// NaN reads back as what it prints as.
	ak_str *n = S("NaN");
	double v = 0;
	CHECK(ak_str_to_float(n, &v) && v != v);
	ak_release(n);
	// A string with a NUL in it isn't a number.
	ak_str *z = ak_str_new("1\0002", 3);
	int64_t iv;
	CHECK(!ak_str_to_int(z, &iv));
	ak_release(z);
}

TEST(strb) {
	ak_strb b = {0};
	ak_strb_cstr(&b, "Count: ", 7);
	ak_strb_int(&b, 42);
	ak_strb_cstr(&b, ", ", 2);
	ak_strb_bool(&b, true);
	ak_str *s = ak_strb_finish(&b);
	CHECK_EQ_STR(s->data, "Count: 42, true");
	ak_release(s);
}

TEST(split_replace) {
	ak_str *s = S("a,b,,c");
	ak_str *comma = S(",");
	ak_arr *parts = ak_str_split(s, comma);
	CHECK_EQ_INT(parts->len, 4);
	CHECK_EQ_STR(((ak_str *)ak_arr_vals(parts)[3].p)->data, "c");
	CHECK_EQ_STR(((ak_str *)ak_arr_vals(parts)[2].p)->data, "");
	ak_str *dash = S("-");
	ak_str *r = ak_str_replace(s, comma, dash);
	CHECK_EQ_STR(r->data, "a-b--c");
	ak_release(s); ak_release(comma); ak_release(parts); ak_release(dash); ak_release(r);
}

TEST(arr_cow) {
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, 0);
	for (int i = 0; i < 10; i++) ak_arr_push(&a, (ak_val){.i = i});
	ak_arr *b = ak_retain(a);  // shared
	ak_arr_set(&b, 0, (ak_val){.i = 100});
	CHECK(a != b);
	CHECK_EQ_INT(ak_arr_vals(a)[0].i, 0);
	CHECK_EQ_INT(ak_arr_vals(b)[0].i, 100);
	ak_arr *c = a;
	ak_arr_push(&c, (ak_val){.i = 10});  // unique: in place
	CHECK(c == a);
	CHECK_EQ_INT(a->len, 11);
	ak_val v = ak_arr_remove_at(&a, 5);
	CHECK_EQ_INT(v.i, 5);
	CHECK_EQ_INT(a->len, 10);
	ak_release(a);
	ak_release(b);
}

TEST(arr_of_strings_eq) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 0), *b = ak_arr_new(AK_K_REF, NULL, 0);
	ak_arr_push(&a, (ak_val){.p = S("x")});
	ak_arr_push(&b, (ak_val){.p = S("x")});
	CHECK(ak_val_eq((ak_val){.p = a}, (ak_val){.p = b}, AK_K_REF));
	ak_arr_push(&b, (ak_val){.p = S("y")});
	CHECK(!ak_val_eq((ak_val){.p = a}, (ak_val){.p = b}, AK_K_REF));
	ak_release(a);
	ak_release(b);
}

TEST(map) {
	ak_map *m = ak_map_new(AK_K_REF, true, AK_K_INT, NULL);
	char buf[16];
	for (int i = 0; i < 100; i++) {
		int n = snprintf(buf, sizeof buf, "k%d", i);
		ak_map_set(&m, (ak_val){.p = ak_str_new(buf, (size_t)n)}, (ak_val){.i = i * 2});
	}
	CHECK_EQ_INT(m->live, 100);
	ak_str *k = S("k42");
	ak_val v;
	REQUIRE(ak_map_get(m, (ak_val){.p = k}, &v));
	CHECK_EQ_INT(v.i, 84);
	CHECK(ak_map_delete(&m, (ak_val){.p = k}));
	CHECK(!ak_map_get(m, (ak_val){.p = k}, &v));
	CHECK_EQ_INT(m->live, 99);
	ak_map_set(&m, (ak_val){.p = ak_retain(k)}, (ak_val){.i = 1});
	ak_arr *keys = ak_map_keys(m);
	CHECK_EQ_INT(keys->len, 100);
	CHECK_EQ_STR(((ak_str *)ak_arr_vals(keys)[0].p)->data, "k0");
	CHECK_EQ_STR(((ak_str *)ak_arr_vals(keys)[99].p)->data, "k42");  // re-added: last
	ak_map *shared = ak_retain(m);
	ak_map_set(&shared, (ak_val){.p = S("new")}, (ak_val){.i = 7});
	CHECK(shared != m);
	CHECK_EQ_INT(m->live, 100);
	CHECK_EQ_INT(shared->live, 101);
	ak_release(keys); ak_release(k); ak_release(m); ak_release(shared);
}

static const uint8_t pt_kinds[] = {AK_K_INT, AK_K_REF};
static const char *const pt_names[] = {"x", "label"};
static const ak_shape pt_shape = {.name = "Point", .nfields = 2, .kinds = pt_kinds, .names = pt_names};

TEST(struct_cow_eq_hash) {
	ak_struct *p = ak_struct_new(&pt_shape);
	p->f[0].i = 1;
	p->f[1].p = S("a");
	ak_struct *q = ak_retain(p);
	ak_struct_mut(&q)->f[0].i = 2;
	CHECK(p != q);
	CHECK_EQ_INT(p->f[0].i, 1);
	CHECK(!ak_obj_eq(&p->h, &q->h));
	ak_struct_mut(&q)->f[0].i = 1;
	CHECK(ak_obj_eq(&p->h, &q->h));
	CHECK(ak_val_hash((ak_val){.p = p}, AK_K_REF) == ak_val_hash((ak_val){.p = q}, AK_K_REF));
	ak_val c = ak_deep_copy((ak_val){.p = p}, AK_K_REF);
	CHECK(c.p != p);
	CHECK(((ak_struct *)c.p)->f[1].p != p->f[1].p);
	CHECK(ak_obj_eq(c.p, &p->h));
	ak_release(c.p); ak_release(p); ak_release(q);
}

static const uint8_t res_ok_kinds[] = {AK_K_INT};
static const ak_case res_cases[] = {{.name = "pending"}, {.name = "ready", .nfields = 1, .kinds = res_ok_kinds}};
static ak_shape res_shape = {.name = "Res", .ncases = 2, .cases = res_cases};

TEST(enum_payloads) {
	ak_enum *a = ak_enum_new(&res_shape, 0), *b = ak_enum_new(&res_shape, 0);
	CHECK(a == b);  // shared singleton
	ak_enum *r = ak_enum_new(&res_shape, 1);
	r->f[0].i = 5;
	ak_enum *s = ak_enum_new(&res_shape, 1);
	s->f[0].i = 5;
	CHECK(ak_obj_eq(&r->h, &s->h));
	CHECK(!ak_obj_eq(&r->h, &a->h));
	ak_release(r); ak_release(s); ak_release(a); ak_release(b);
	ak_free(((ak_enum **)res_shape.singletons)[0]);  // the immortal singleton
	ak_free(res_shape.singletons);
}

// A map keyed by structs and enums (Hashable, made of what is): a key
// equal to one in it, though another object, finds it.
TEST(map_struct_enum_keys) {
	ak_map *m = ak_map_new(AK_K_REF, false, AK_K_INT, NULL);
	for (int i = 0; i < 40; i++) {
		ak_struct *p = ak_struct_new(&pt_shape);
		p->f[0].i = i;
		p->f[1].p = S(i % 2 ? "odd" : "even");
		ak_map_set(&m, (ak_val){.p = p}, (ak_val){.i = i});
	}
	ak_struct *k = ak_struct_new(&pt_shape);
	k->f[0].i = 17;
	k->f[1].p = S("odd");
	ak_val v;
	REQUIRE(ak_map_get(m, (ak_val){.p = k}, &v));
	CHECK_EQ_INT(v.i, 17);
	ak_struct_mut(&k)->f[1].p = (ak_release(k->f[1].p), S("even"));
	CHECK(!ak_map_get(m, (ak_val){.p = k}, &v));
	ak_release(k);

	ak_map *e = ak_map_new(AK_K_REF, false, AK_K_INT, NULL);
	ak_enum *r = ak_enum_new(&res_shape, 1), *s = ak_enum_new(&res_shape, 1);
	r->f[0].i = 5;
	s->f[0].i = 5;
	ak_map_set(&e, (ak_val){.p = r}, (ak_val){.i = 1});
	ak_enum *t = ak_enum_new(&res_shape, 1);
	t->f[0].i = 6;
	ak_map_set(&e, (ak_val){.p = t}, (ak_val){.i = 2});
	REQUIRE(ak_map_get(e, (ak_val){.p = s}, &v));
	CHECK_EQ_INT(v.i, 1);
	ak_map_set(&e, (ak_val){.p = s}, (ak_val){.i = 3});  // the same key: replaced
	CHECK_EQ_INT(e->live, 2);
	ak_release(m);
	ak_release(e);
}

TEST(errors) {
	CHECK(!ak_error_pending());
	ak_throw_msg("bad %d", 3);
	CHECK(ak_error_pending());
	void *e = ak_error_take();
	CHECK(!ak_error_pending());
	CHECK_EQ_STR(ak_error_message(e)->data, "bad 3");
	CHECK(!ak_is_cancel(e));
	ak_release(e);
	ak_throw_cancel();
	e = ak_error_take();
	CHECK(ak_is_cancel(e));
	// A long message (a UI test's screen) is kept whole.
	char long_msg[2000];
	memset(long_msg, 'x', sizeof long_msg - 1);
	long_msg[sizeof long_msg - 2] = 'z';
	long_msg[sizeof long_msg - 1] = 0;
	ak_throw_msg("%s!", long_msg);
	e = ak_error_take();
	CHECK(ak_error_message(e)->len == sizeof long_msg);
	if (ak_error_message(e)->len == sizeof long_msg) CHECK_EQ_STR(ak_error_message(e)->data + sizeof long_msg - 2, "z!");
	ak_release(e);
}

static ak_val add_caps(ak_closure *c, ak_val *args, uint32_t n) {
	return (ak_val){.i = c->caps[0].i + (n ? args[0].i : 0)};
}

TEST(closure) {
	static const uint8_t kinds[] = {AK_K_INT};
	ak_closure *c = ak_closure_new(add_caps, NULL, 1, kinds);
	c->caps[0].i = 40;
	ak_val arg = {.i = 2};
	CHECK_EQ_INT(ak_call(c, &arg, 1).i, 42);
	ak_release(c);
}

// The constants made on first use (a payloadless case's shared value, the
// empty string, true and false) are made once when threads (spawn's
// workers) ask for them at the same time: each gets the one value, whole.
enum { RACERS = 4 };
static const ak_case race_cases[] = {{.name = "a"}, {.name = "b", .nfields = 1, .kinds = (const uint8_t[]){AK_K_INT}}};
static ak_shape race_shape;
static atomic_int race_ready, race_round;
static ak_enum *race_got[RACERS];
static ak_str *race_strs[RACERS][3];

static void *racer(void *arg) {
	int me = (int)(intptr_t)arg;
	for (int round = 1; round <= 200; round++) {
		while (atomic_load(&race_round) != round) {}
		atomic_fetch_add(&race_ready, 1);
		while (atomic_load(&race_ready) < RACERS) {}
		race_got[me] = ak_enum_new(&race_shape, 0);
		if (round == 1) {
			race_strs[me][0] = ak_str_empty();
			race_strs[me][1] = ak_str_from_bool(true);
			race_strs[me][2] = ak_str_from_bool(false);
		}
		atomic_fetch_add(&race_ready, 1);
	}
	return NULL;
}

TEST(constants_made_once_across_threads) {
	pthread_t th[RACERS];
	for (int i = 0; i < RACERS; i++) REQUIRE(pthread_create(&th[i], NULL, racer, (void *)(intptr_t)i) == 0);
	int same = 0;
	for (int round = 1; round <= 200; round++) {
		race_shape = (ak_shape){.name = "Race", .ncases = 2, .cases = race_cases};
		atomic_store(&race_ready, 0);
		atomic_store(&race_round, round);
		while (atomic_load(&race_ready) < 2 * RACERS) {}
		bool ok = true;
		for (int i = 0; i < RACERS; i++) ok &= race_got[i] != NULL && race_got[i] == ((ak_enum **)race_shape.singletons)[0];
		same += ok;
		// As a module's unload: its singletons, immortal, then their table.
		ak_free(((ak_enum **)race_shape.singletons)[0]);
		ak_free(race_shape.singletons);
	}
	for (int i = 0; i < RACERS; i++) pthread_join(th[i], NULL);
	CHECK_EQ_INT(same, 200);
	for (int i = 0; i < RACERS; i++) {
		CHECK(race_strs[i][0] == ak_str_empty());
		CHECK(race_strs[i][1] != NULL && race_strs[i][1] == ak_str_from_bool(true));
		CHECK(race_strs[i][2] != NULL && race_strs[i][2] == ak_str_from_bool(false));
	}
}

// A spawn's copy of its closure holds the module (the VM's), whose
// constants and payloadless cases its result may hold: it's let go on the
// main thread, once the result is delivered, never on the worker (where a
// dev reload's last reference would free the module under the result).
static _Atomic uint64_t spawn_dropped_on;
static ak_val spawn_body(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(args);
	AK_UNUSED(n);
	return (ak_val){.i = 7};
}
static void spawn_drop(ak_closure *c) {
	AK_UNUSED(c);
	atomic_store(&spawn_dropped_on, ak_thread_id());
}

TEST(spawn_lets_go_of_its_closure_on_the_main_thread) {
	extern const ak_lib_entry ak_lib_spawn;
	ak_closure *c = ak_closure_new(spawn_body, NULL, 0, NULL);
	c->drop = spawn_drop;
	ak_val args[2] = {{.p = c}, {.i = AK_K_INT}};
	ak_future *f = ak_lib_spawn.fn(args, 2).p;
	c->drop = NULL;
	ak_release(c);
	while (f->state == AK_FUT_PENDING) {
		ak_sched_wait_posted();
		ak_sched_run_posted();
	}
	CHECK_EQ_INT(f->result.i, 7);
	CHECK(atomic_load(&spawn_dropped_on) == ak_thread_id());
	ak_release(f);
}

// math.random's generator is shared by the threads that draw from it
// (spawn's workers): drawing at once, each draw is a value of its own,
// none lost to a race or drawn twice.
enum { DRAWERS = 4, DRAWS = 100000 };
static int64_t drawn[DRAWERS * DRAWS];
static atomic_int draw_go;
static ak_lib_fn draw_fn;

static void *drawer(void *arg) {
	int me = (int)(intptr_t)arg;
	while (!atomic_load(&draw_go)) {}
	ak_val range[2] = {{.i = INT64_MIN}, {.i = INT64_MAX}};
	for (int i = 0; i < DRAWS; i++) drawn[me * DRAWS + i] = draw_fn(range, 2).i;
	return NULL;
}

static int cmp_i64(const void *a, const void *b) {
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
	return (x > y) - (x < y);
}

TEST(math_random_across_threads) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++)
		if (strcmp(e->name, "math.random_int") == 0) draw_fn = e->fn;
	REQUIRE(draw_fn != NULL);
	pthread_t th[DRAWERS];
	for (int i = 0; i < DRAWERS; i++) REQUIRE(pthread_create(&th[i], NULL, drawer, (void *)(intptr_t)i) == 0);
	atomic_store(&draw_go, 1);
	for (int i = 0; i < DRAWERS; i++) pthread_join(th[i], NULL);
	qsort(drawn, AK_LEN(drawn), sizeof drawn[0], cmp_i64);
	int twice = 0;
	for (size_t i = 1; i < AK_LEN(drawn); i++) twice += drawn[i] == drawn[i - 1];
	CHECK_EQ_INT(twice, 0);
}

// Posted callbacks run when one of them has the host run them again (a
// modal dialog's loop, which handles the host's wake): each once, none
// lost.
static char ran_posted[16];
static size_t nran_posted;

static void note(void *ctx) { ran_posted[nran_posted++] = (char)(intptr_t)ctx; }

static void nests(void *ctx) {
	note(ctx);
	ak_post(note, (void *)(intptr_t)'d');
	ak_sched_run_posted();
}

TEST(run_posted_nested) {
	ak_post(nests, (void *)(intptr_t)'a');
	ak_post(note, (void *)(intptr_t)'b');
	ak_post(note, (void *)(intptr_t)'c');
	ak_sched_run_posted();
	ak_sched_run_posted();
	ran_posted[nran_posted] = 0;
	CHECK_EQ_INT(nran_posted, 4);
	CHECK(strchr(ran_posted, 'a') && strchr(ran_posted, 'b') && strchr(ran_posted, 'c') && strchr(ran_posted, 'd'));
}

// ---- Deep values ----
//
// A value nests as deep as an app makes it: releasing, comparing,
// hashing, copying (to a spawn's thread and back) and freezing one walk
// it without recursing, so a chain a million long fits a spawn's 512 KB
// stack, and a shallow one walks without allocating.

static const uint8_t list_cons_kinds[] = {AK_K_INT, AK_K_REF};
static const ak_case list_cases[] = {{.name = "empty"}, {.name = "cons", .nfields = 2, .kinds = list_cons_kinds}};
static ak_shape list_shape = {.name = "List", .ncases = 2, .cases = list_cases};

static const uint8_t tree_leaf_kinds[] = {AK_K_INT};
static const uint8_t tree_node_kinds[] = {AK_K_REF, AK_K_REF};
static const ak_case tree_cases[] = {{.name = "leaf", .nfields = 1, .kinds = tree_leaf_kinds}, {.name = "node", .nfields = 2, .kinds = tree_node_kinds}};
static ak_shape tree_shape = {.name = "Tree", .ncases = 2, .cases = tree_cases};

static const uint8_t link_kinds[] = {AK_K_INT, AK_K_REF};
static const ak_shape link_shape = {.name = "Link", .nfields = 2, .kinds = link_kinds};

// A list: each link's tail is its last field.
static ak_obj *deep_list(int depth, int bottom) {
	ak_enum *l = ak_enum_new(&list_shape, 1);
	l->f[0].i = bottom;
	l->f[1].p = ak_enum_new(&list_shape, 0);
	for (int i = 1; i < depth; i++) {
		ak_enum *c = ak_enum_new(&list_shape, 1);
		c->f[0].i = i;
		c->f[1].p = l;
		l = c;
	}
	return &l->h;
}

// A tree deep on its left: each node's deep side isn't its last field,
// so a walk keeps a frame for every level.
static ak_obj *deep_tree(int depth, int bottom) {
	ak_enum *t = ak_enum_new(&tree_shape, 0);
	t->f[0].i = bottom;
	for (int i = 1; i < depth; i++) {
		ak_enum *leaf = ak_enum_new(&tree_shape, 0), *n = ak_enum_new(&tree_shape, 1);
		leaf->f[0].i = i;
		n->f[0].p = t;
		n->f[1].p = leaf;
		t = n;
	}
	return &t->h;
}

static ak_obj *deep_links(int depth, int bottom) {
	ak_struct *l = ak_struct_new(&link_shape);
	l->f[0].i = bottom;
	for (int i = 1; i < depth; i++) {
		ak_struct *c = ak_struct_new(&link_shape);
		c->f[0].i = i;
		c->f[1].p = l;
		l = c;
	}
	return &l->h;
}

// Arrays in arrays, each with a string before the one it holds.
static ak_obj *deep_arrays(int depth, int bottom) {
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, 1);
	ak_arr_push(&a, (ak_val){.i = bottom});
	for (int i = 1; i < depth; i++) {
		ak_arr *c = ak_arr_new(AK_K_REF, NULL, 2);
		ak_arr_push(&c, (ak_val){.p = ak_str_c("x")});
		ak_arr_push(&c, (ak_val){.p = a});
		a = c;
	}
	return &a->h;
}

// Maps in maps, a deleted entry in each.
static ak_obj *deep_maps(int depth, int bottom) {
	ak_map *m = ak_map_new(AK_K_REF, true, AK_K_REF, NULL);
	ak_map_set(&m, (ak_val){.p = ak_str_c("v")}, (ak_val){.p = ak_str_from_int(bottom)});
	for (int i = 1; i < depth; i++) {
		ak_map *c = ak_map_new(AK_K_REF, true, AK_K_REF, NULL);
		ak_str *gone = ak_str_c("gone");
		ak_map_set(&c, (ak_val){.p = ak_retain(gone)}, (ak_val){.p = NULL});
		ak_map_set(&c, (ak_val){.p = ak_str_c("in")}, (ak_val){.p = m});
		ak_map_set(&c, (ak_val){.p = ak_str_c("none")}, (ak_val){.p = NULL});
		ak_map_delete(&c, (ak_val){.p = gone});
		ak_release(gone);
		m = c;
	}
	return &m->h;
}

static ak_obj *deep_boxes(int depth, int bottom) {
	ak_box *b = ak_box_new(AK_K_INT, (ak_val){.i = bottom});
	for (int i = 1; i < depth; i++) b = ak_box_new(AK_K_REF, (ak_val){.p = b});
	return &b->h;
}

typedef struct {
	ak_obj *v;
	bool copied, equal, same_hash;
} deep_job;

// What a spawn's worker does with a value: its copy in, compared and
// hashed there, and a copy of that back out.
static void *deep_worker(void *arg) {
	deep_job *j = arg;
	ak_val in = ak_deep_copy((ak_val){.p = j->v}, AK_K_REF);
	j->copied = in.p != j->v;
	j->equal = ak_obj_eq(in.p, j->v);
	j->same_hash = ak_val_hash(in, AK_K_REF) == ak_val_hash((ak_val){.p = j->v}, AK_K_REF);
	ak_val out = ak_deep_copy(in, AK_K_REF);
	ak_release(in.p);
	j->v = out.p;
	return NULL;
}

static void check_deep(const char *what, ak_obj *(*build)(int, int), int depth) {
	int64_t live = __atomic_load_n(&ak_obj_live, __ATOMIC_RELAXED);
	ak_obj *a = build(depth, 0), *b = build(depth, 0), *c = build(depth, 1);
	if (!ak_obj_eq(a, b) || ak_obj_eq(a, c)) T_FAIL("%s: compared wrong", what);
	if (ak_val_hash((ak_val){.p = a}, AK_K_REF) != ak_val_hash((ak_val){.p = b}, AK_K_REF)) T_FAIL("%s: equal values hash apart", what);
	// On a thread with a spawn worker's stack.
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 512 * 1024);
	deep_job j = {.v = a};
	pthread_t th;
	if (pthread_create(&th, &attr, deep_worker, &j) != 0) {
		T_FAIL("%s: no thread", what);
		return;
	}
	pthread_join(th, NULL);
	pthread_attr_destroy(&attr);
	if (!j.copied || !j.equal || !j.same_hash || j.v == a || !ak_obj_eq(j.v, a)) T_FAIL("%s: copied across threads wrong", what);
	// Frozen as a module's constant, then freed as its module unloads.
	ak_frozen made = {0};
	ak_freeze((ak_val){.p = b}, AK_K_REF, &made);
	if (made.len < (uint32_t)depth || b->rc != AK_RC_IMMORTAL || !ak_obj_eq(a, b)) T_FAIL("%s: frozen wrong", what);
	ak_frozen_free(&made);
	ak_release(a);
	ak_release(c);
	ak_release(j.v);
	int64_t left = __atomic_load_n(&ak_obj_live, __ATOMIC_RELAXED) - live;
	if (left != 0) T_FAIL("%s: %lld objects left", what, (long long)left);
}

static void free_singletons(ak_shape *s) {
	ak_enum **tab = s->singletons;
	if (!tab) return;
	for (uint16_t i = 0; i < s->ncases; i++) ak_free(tab[i]);
	ak_free(tab);
	s->singletons = NULL;
}

TEST(deep_values) {
	enum { DEPTH = 1000000 };
	check_deep("list", deep_list, DEPTH);
	check_deep("tree", deep_tree, DEPTH);
	check_deep("links", deep_links, DEPTH);
	check_deep("arrays", deep_arrays, DEPTH);
	check_deep("maps", deep_maps, DEPTH / 10);
	check_deep("boxes", deep_boxes, DEPTH);
	free_singletons(&list_shape);
	free_singletons(&tree_shape);
}

// A value nested less deep than a walk's frames on the C stack compares
// and hashes without allocating; releasing one never allocates.
TEST(shallow_values_walk_without_allocating) {
	ak_obj *a = deep_tree(16, 0), *b = deep_tree(16, 0);  // a frame a level: 15
	ak_arr *xs = ak_arr_new(AK_K_REF, &pt_shape, 0), *ys = ak_arr_new(AK_K_REF, &pt_shape, 0);
	for (int i = 0; i < 100; i++) {
		ak_struct *p = ak_struct_new(&pt_shape), *q = ak_struct_new(&pt_shape);
		p->f[0].i = q->f[0].i = i;
		p->f[1].p = S("p");
		q->f[1].p = S("p");
		ak_arr_push(&xs, (ak_val){.p = p});
		ak_arr_push(&ys, (ak_val){.p = q});
	}
	ak_obj *l = deep_list(1000, 0), *m = deep_list(1000, 0);
	uint64_t n = ak_alloc_count;
	CHECK(ak_obj_eq(a, b));
	CHECK(ak_obj_eq(&xs->h, &ys->h));
	CHECK(ak_obj_eq(l, m));  // a chain: one frame
	CHECK(ak_val_hash((ak_val){.p = a}, AK_K_REF) == ak_val_hash((ak_val){.p = b}, AK_K_REF));
	CHECK(ak_val_hash((ak_val){.p = xs}, AK_K_REF) == ak_val_hash((ak_val){.p = ys}, AK_K_REF));
	CHECK(ak_val_hash((ak_val){.p = l}, AK_K_REF) == ak_val_hash((ak_val){.p = m}, AK_K_REF));
	ak_release(a);
	ak_release(b);
	ak_release(xs);
	ak_release(ys);
	ak_release(l);
	ak_release(m);
	CHECK_EQ_INT(ak_alloc_count - n, 0);
	free_singletons(&list_shape);
}

// A value with what it holds in other places hashes as it compares:
// arrays and bytes by what they hold, maps by their keys in any order.
TEST(hash_follows_equality) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 0), *b = ak_arr_new(AK_K_REF, NULL, 0);
	ak_arr_push(&a, (ak_val){.p = ak_bytes_new("xy", 2)});
	ak_arr_push(&b, (ak_val){.p = ak_bytes_new("xy", 2)});
	ak_map *m = ak_map_new(AK_K_REF, true, AK_K_INT, NULL), *n = ak_map_new(AK_K_REF, true, AK_K_INT, NULL);
	ak_map_set(&m, (ak_val){.p = S("a")}, (ak_val){.i = 1});
	ak_map_set(&m, (ak_val){.p = S("b")}, (ak_val){.i = 2});
	ak_map_set(&n, (ak_val){.p = S("b")}, (ak_val){.i = 2});
	ak_map_set(&n, (ak_val){.p = S("a")}, (ak_val){.i = 1});
	ak_arr_push(&a, (ak_val){.p = m});
	ak_arr_push(&b, (ak_val){.p = n});
	CHECK(ak_obj_eq(&a->h, &b->h));
	CHECK(ak_val_hash((ak_val){.p = a}, AK_K_REF) == ak_val_hash((ak_val){.p = b}, AK_K_REF));
	ak_release(a);
	ak_release(b);
}

// ---- Buffered output (rt.h: std/os's Writer) ----

static char out_got[256];
static size_t out_len;
static int out_writes;

static void out_hook(int fd, const char *s, size_t n) {
	AK_UNUSED(fd);
	if (out_len + n < sizeof out_got) memcpy(out_got + out_len, s, n);
	out_len += n;
	out_got[out_len < sizeof out_got ? out_len : sizeof out_got - 1] = 0;
	out_writes++;
}

// A stream's buffer: puts are copies (no allocation, nothing written)
// until it fills; then it's written whole, a put larger than it goes
// at once, and another thread's write goes through without touching it.
TEST(stdio_buffer) {
	void (*hook)(int, const char *, size_t) = ak_lib_write_hook;
	ak_lib_write_hook = out_hook;
	ak_mark_main_thread();
	ak_stdio_flush_all();
	out_len = 0, out_writes = 0;
	ak_stdio_buffer(1, 8);
	ak_stdio_buffer(1, 4);  // the largest asked for stays
	REQUIRE(ak_outbufs[1].cap == 8);
	uint64_t before = ak_alloc_count;
	ak_stdio_put(1, "ab", 2);
	ak_stdio_put(1, "cd", 2);
	CHECK_EQ_INT(ak_alloc_count - before, 0);
	CHECK_EQ_INT(out_writes, 0);
	ak_stdio_put(1, "efghijklmnopqrstuvwxyz", 22);  // more than it holds
	CHECK_EQ_INT(out_writes, 2);  // what it held, then the put itself
	CHECK_EQ_STR(out_got, "abcdefghijklmnopqrstuvwxyz");
	ak_stdio_put(1, "1", 1);
	uint32_t mine = ak_thread_epoch;
	ak_thread_epoch = 0;  // as another thread
	ak_stdio_put(1, "2", 1);
	ak_stdio_flush(1);  // not its buffer: nothing
	ak_thread_epoch = mine;
	CHECK_EQ_STR(out_got, "abcdefghijklmnopqrstuvwxyz2");
	ak_stdio_put(2, "e", 1);  // stderr's: its own, unbuffered (a cap of 0)
	CHECK_EQ_STR(out_got, "abcdefghijklmnopqrstuvwxyz2e");
	ak_stdio_flush_all();
	CHECK_EQ_STR(out_got, "abcdefghijklmnopqrstuvwxyz2e1");
	ak_lib_write_hook = hook;
}

T_MAIN()
