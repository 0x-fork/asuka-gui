#include "t.h"
#include "rt/json.h"

// struct Todo { title: string; done: bool; tags: [string]; due: int? }
static const uint8_t todo_kinds[] = {AK_K_REF, AK_K_BOOL, AK_K_REF, AK_K_REF};
static const char *const todo_names[] = {"title\0s", "done\0b", "tags\0[s", "due\0?i"};
static const ak_shape todo = {.name = "Todo", .nfields = 4, .kinds = todo_kinds, .names = todo_names};

static const char *const filter_cases[] = {"all", "unread", "flagged"};

// struct Node { name: string; next: Node? }: a type that nests itself.
static const uint8_t node_kinds[] = {AK_K_REF, AK_K_REF};
static const char *const node_names[] = {"name\0s", "next\0?SNode;"};
static const ak_shape node = {.name = "Node", .nfields = 2, .kinds = node_kinds, .names = node_names};

// enum Shape { circle(radius: float, label: string, tags: [string]), dot, at(where: Node) }
static const uint8_t circle_kinds[] = {AK_K_FLOAT, AK_K_REF, AK_K_REF};
static const char *const circle_names[] = {"radius\0f", "label\0s", "tags\0[s"};
static const uint8_t at_kinds[] = {AK_K_REF};
static const char *const at_names[] = {"where\0SNode;"};
static const ak_case shape_cases[] = {
	{.name = "circle", .nfields = 3, .kinds = circle_kinds, .names = circle_names},
	{.name = "dot"},
	{.name = "at", .nfields = 1, .kinds = at_kinds, .names = at_names},
};
static const ak_shape shape_enum = {.name = "Shape", .ncases = 3, .cases = shape_cases};

static const ak_shape *shape(void *ctx, const char *name, size_t n) {
	AK_UNUSED(ctx);
	if (n == 4 && memcmp(name, "Todo", 4) == 0) return &todo;
	if (n == 4 && memcmp(name, "Node", 4) == 0) return &node;
	if (n == 5 && memcmp(name, "Shape", 5) == 0) return &shape_enum;
	if (n == 5 && memcmp(name, "Error", 5) == 0) return &ak_shape_error;
	return NULL;
}

static bool cases(void *ctx, const char *name, size_t n, const char *const **out, uint32_t *count) {
	AK_UNUSED(ctx);
	if (n == 6 && memcmp(name, "Filter", 6) == 0) {
		*out = filter_cases;
		*count = 3;
		return true;
	}
	return false;
}

static const ak_json_types types = {shape, cases, NULL};

TEST(decode_structs) {
	const char *src = "[{\"title\": \"milk \\u00e9\", \"done\": true, \"tags\": [\"a\",\"b\"], \"extra\": {\"x\": [1,2]}},\n {\"title\": \"eggs\", \"done\": false, \"due\": 3}]";
	ak_val v = ak_json_decode(src, strlen(src), "[STodo;", &types);
	REQUIRE(!ak_error_pending());
	ak_arr *a = v.p;
	REQUIRE(a->len == 2);
	ak_struct *t0 = a->data[0].p, *t1 = a->data[1].p;
	CHECK_EQ_STR(((ak_str *)t0->f[0].p)->data, "milk é");
	CHECK_EQ_INT(t0->f[1].i, 1);
	CHECK_EQ_INT(((ak_arr *)t0->f[2].p)->len, 2);
	CHECK(t0->f[3].p == NULL);
	CHECK_EQ_INT(((ak_box *)t1->f[3].p)->v.i, 3);
	CHECK_EQ_INT(((ak_arr *)t1->f[2].p)->len, 0);  // missing: empty
	ak_str *out = ak_json_encode(v, "[STodo;", &types, 0);
	CHECK_EQ_STR(out->data, "[{\"title\":\"milk é\",\"done\":true,\"tags\":[\"a\",\"b\"],\"due\":null},{\"title\":\"eggs\",\"done\":false,\"tags\":[],\"due\":3}]");
	ak_release(out);
	ak_release(a);
}

TEST(errors_say_where) {
	const char *src = "{\"title\": \"x\",\n \"done\": 3}";
	ak_val v = ak_json_decode(src, strlen(src), "STodo;", &types);
	CHECK(v.p == NULL);
	void *e = ak_error_take();
	REQUIRE(e != NULL);
	CHECK_EQ_STR(ak_error_message(e)->data, "json: 2:10: expected true or false");
	ak_release(e);
	// A missing required field (no default to start from: a struct).
	src = "{\"title\": \"x\"}";
	v = ak_json_decode(src, strlen(src), "STodo;", &types);
	CHECK(!ak_error_pending());
	ak_release(v.p);
}

TEST(maps_enums_numbers) {
	const char *src = "{\"a\": \"unread\", \"b\": \"flagged\"}";
	ak_val v = ak_json_decode(src, strlen(src), "{eFilter;", &types);
	REQUIRE(!ak_error_pending());
	ak_val got;
	ak_str *k = ak_str_c("b");
	REQUIRE(ak_map_get(v.p, (ak_val){.p = k}, &got));
	CHECK_EQ_INT(got.i, 2);
	ak_str *out = ak_json_encode(v, "{eFilter;", &types, 2);
	CHECK_EQ_STR(out->data, "{\n  \"a\": \"unread\",\n  \"b\": \"flagged\"\n}");
	ak_release(out);
	ak_release(k);
	ak_release(v.p);
	src = "[1.5, 2e3, -0.25]";
	v = ak_json_decode(src, strlen(src), "[f", &types);
	CHECK_EQ_F(((ak_arr *)v.p)->data[1].f, 2000);
	ak_release(v.p);
	src = "1.5";
	v = ak_json_decode(src, strlen(src), "i", &types);
	void *e = ak_error_take();
	CHECK(e != NULL);
	ak_release(e);
}

// Go's encoding/json writes a nil slice or map as null: an empty one.
TEST(null_collections) {
	const char *src = "null";
	ak_val v = ak_json_decode(src, strlen(src), "[f", &types);
	REQUIRE(!ak_error_pending());
	CHECK_EQ_INT(((ak_arr *)v.p)->len, 0);
	ak_release(v.p);
	v = ak_json_decode(src, strlen(src), "{eFilter;", &types);
	REQUIRE(!ak_error_pending());
	CHECK_EQ_INT(((ak_map *)v.p)->len, 0);
	ak_release(v.p);
	v = ak_json_decode(src, strlen(src), "<s", &types);
	REQUIRE(!ak_error_pending());
	CHECK_EQ_INT(((ak_map *)v.p)->len, 0);
	ak_release(v.p);
	src = " nul";
	v = ak_json_decode(src, strlen(src), "[f", &types);
	void *e = ak_error_take();
	CHECK(e != NULL);
	ak_release(e);
}

TEST(bytes_base64) {
	ak_bytes *b = ak_bytes_new("hello!?", 7);
	ak_str *s = ak_json_encode((ak_val){.p = b}, "y", &types, 0);
	CHECK_EQ_STR(s->data, "\"aGVsbG8hPw==\"");
	ak_val back = ak_json_decode(s->data, s->len, "y", &types);
	CHECK(ak_obj_eq(back.p, &b->h));
	ak_release(back.p);
	ak_release(s);
	ak_release(b);
}

// decode_fails decodes src as desc and wants an error whose message
// contains want, and nothing left behind.
static void decode_fails(const char *src, size_t n, const char *desc, const char *want) {
	int64_t live = ak_obj_live;
	ak_val v = ak_json_decode(src, n, desc, &types);
	void *e = ak_error_take();
	if (!e) {
		T_FAIL("%.40s: no error", src);
		return;
	}
	CHECK(v.p == NULL);
	if (!strstr(ak_error_message(e)->data, want)) T_FAIL("%.40s: %s, want %s", src, ak_error_message(e)->data, want);
	ak_release(e);
	CHECK_EQ_INT(ak_obj_live, live);
}

// An enum's case as an object, {"case": {"field": value, ...}}: a field
// left out starts empty (a string, a collection) or is required (a
// struct); one written twice is the last; the fields are separated by
// commas.
TEST(enum_object_payloads) {
	const char *src = "{\"circle\": {\"radius\": 1}}";
	ak_val v = ak_json_decode(src, strlen(src), "EShape;", &types);
	REQUIRE(!ak_error_pending());
	ak_enum *e = v.p;
	CHECK_EQ_INT(ak_enum_case(e), 0);
	CHECK_EQ_F(e->f[0].f, 1);
	CHECK(e->f[1].p != NULL && ((ak_str *)e->f[1].p)->len == 0);
	CHECK(e->f[2].p != NULL && ((ak_arr *)e->f[2].p)->len == 0);
	ak_release(e);

	int64_t live = ak_obj_live;
	src = "{\"circle\": {\"label\": \"a\", \"label\": \"b\", \"radius\": 2}}";
	v = ak_json_decode(src, strlen(src), "EShape;", &types);
	REQUIRE(!ak_error_pending());
	e = v.p;
	CHECK_EQ_STR(((ak_str *)e->f[1].p)->data, "b");
	ak_release(e);
	CHECK_EQ_INT(ak_obj_live, live);

	src = "{\"circle\": {\"label\": \"a\" \"radius\": 2}}";
	decode_fails(src, strlen(src), "EShape;", "expected ',' or '}'");
	src = "{\"circle\": {\"label\": \"a\",}}";
	decode_fails(src, strlen(src), "EShape;", "expected a string");
	src = "{\"at\": {}}";
	decode_fails(src, strlen(src), "EShape;", "a missing field where");
	src = "\"nope";
	decode_fails(src, strlen(src), "EShape;", "a string that doesn't end");
}

// Nesting deeper than AK_JSON_MAX_DEPTH is an error, not a stack
// overflow: in a value of a type that nests itself, and in one skipped.
TEST(nesting_depth) {
	size_t depth = 1000000;
	char *src = malloc(depth * 10 + 64);
	const char *head = "{\"title\":\"x\",\"extra\":";
	size_t n = strlen(head);
	memcpy(src, head, n);
	for (size_t i = 0; i < depth; i++) src[n++] = '[';
	for (size_t i = 0; i < depth; i++) src[n++] = ']';
	src[n++] = '}';
	decode_fails(src, n, "STodo;", "nested too deeply");
	n = 0;
	for (size_t i = 0; i < depth; i++) {
		memcpy(src + n, "{\"next\":", 8);
		n += 8;
	}
	decode_fails(src, n, "SNode;", "nested too deeply");
	// As deep as it may go is fine: the field's object, then its arrays.
	n = strlen(head);
	memcpy(src, head, n);
	for (int i = 0; i < AK_JSON_MAX_DEPTH - 1; i++) src[n++] = '[';
	for (int i = 0; i < AK_JSON_MAX_DEPTH - 1; i++) src[n++] = ']';
	src[n++] = '}';
	ak_val v = ak_json_decode(src, n, "STodo;", &types);
	CHECK(!ak_error_pending());
	ak_release(v.p);
	free(src);
}

// An integer out of int's range is an error (not clamped, not a float
// cast's undefined behavior); a number is read whole, however long.
TEST(integer_range) {
	decode_fails("1e300", 5, "i", "out of range");
	decode_fails("-1e19", 5, "i", "out of range");
	decode_fails("9223372036854775808", 19, "i", "out of range");
	decode_fails("-9223372036854775809", 20, "i", "out of range");
	decode_fails("9.3e18", 6, "i", "out of range");
	ak_val v = ak_json_decode("-9223372036854775808", 20, "i", &types);
	CHECK(!ak_error_pending());
	CHECK(v.i == INT64_MIN);
	v = ak_json_decode("9.2e18", 6, "i", &types);
	CHECK(!ak_error_pending());
	CHECK_EQ_INT(v.i, 9200000000000000000LL);
	// 0.000…0001e100: more digits than a small buffer holds.
	char longf[128] = "0.";
	for (int i = 0; i < 99; i++) strcat(longf, "0");
	strcat(longf, "1e100");
	v = ak_json_decode(longf, strlen(longf), "f", &types);
	CHECK(!ak_error_pending());
	CHECK_EQ_F(v.f, 1);
	decode_fails("1-2", 3, "f", "a bad number");
	decode_fails("--1", 3, "i", "a bad number");
}

// A surrogate escape without its pair is U+FFFD, and the escape after it
// is still read.
TEST(surrogates) {
	static const struct {
		const char *src, *want;
	} cases[] = {
		{"\"\\ud83d\\ude00\"", "\xf0\x9f\x98\x80"},
		{"\"\\ud83d\\u0041\"", "\xef\xbf\xbd" "A"},
		{"\"\\udc00x\"", "\xef\xbf\xbd" "x"},
		{"\"\\ud83d\"", "\xef\xbf\xbd"},
		{"\"\\ud83d\\ud83d\\ude00\"", "\xef\xbf\xbd\xf0\x9f\x98\x80"},
		{"\"\\ud83d\\n\"", "\xef\xbf\xbd\n"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		ak_val v = ak_json_decode(cases[i].src, strlen(cases[i].src), "s", &types);
		REQUIRE(!ak_error_pending());
		CHECK_EQ_STR(((ak_str *)v.p)->data, cases[i].want);
		ak_release(v.p);
	}
}

// A string that fails (a bad escape) leaves nothing behind wherever it
// is: a field, an element, bytes, a key.
TEST(bad_strings_leave_nothing) {
	const char *src = "{\"title\":\"ab\\q\"}";
	decode_fails(src, strlen(src), "STodo;", "a bad escape");
	src = "[\"ab\\q\"]";
	decode_fails(src, strlen(src), "[s", "a bad escape");
	src = "\"ab\\q\"";
	decode_fails(src, strlen(src), "y", "a bad escape");
	src = "{\"a\\q\":1}";
	decode_fails(src, strlen(src), "{i", "a bad escape");
}

// A shape the core defines (Error's) has its fields' descriptors too.
TEST(core_shapes) {
	ak_struct *e = ak_struct_new(&ak_shape_error);
	e->f[0].p = ak_str_c("boom");
	ak_str *s = ak_json_encode((ak_val){.p = e}, "SError;", &types, 0);
	CHECK_EQ_STR(s->data, "{\"message\":\"boom\"}");
	ak_val back = ak_json_decode(s->data, s->len, "SError;", &types);
	REQUIRE(!ak_error_pending());
	CHECK_EQ_STR(((ak_str *)((ak_struct *)back.p)->f[0].p)->data, "boom");
	ak_release(back.p);
	ak_release(s);
	ak_release(e);
}

T_MAIN()
