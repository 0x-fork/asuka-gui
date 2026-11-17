#include "t.h"
#include "rt/rt.h"

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

T_MAIN()
