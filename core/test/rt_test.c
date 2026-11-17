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
	CHECK_EQ_STR(((ak_str *)parts->data[3].p)->data, "c");
	CHECK_EQ_STR(((ak_str *)parts->data[2].p)->data, "");
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
	CHECK_EQ_INT(a->data[0].i, 0);
	CHECK_EQ_INT(b->data[0].i, 100);
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
	CHECK_EQ_STR(((ak_str *)keys->data[0].p)->data, "k0");
	CHECK_EQ_STR(((ak_str *)keys->data[99].p)->data, "k42");  // re-added: last
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

T_MAIN()
