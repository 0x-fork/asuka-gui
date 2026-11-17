#include "t.h"
#include "native/native.h"

// A platform's side, for the test: objects are handles; method 1 joins
// its (string, int) arguments; method 2 throws a declared error; method 3
// answers later (started); method 4 calls back the function it's given.
static uint64_t released;
static uint64_t pending_call;
static uint64_t callback_id;

static void put(uint8_t *out, size_t cap, uint8_t **big, ak_buf *b, size_t *n) {
	if (b->len <= cap) memcpy(out, b->data, b->len);
	else {
		*big = malloc(b->len);
		memcpy(*big, b->data, b->len);
	}
	*n = b->len;
	ak_buf_free(b);
}

static size_t nnew(const uint8_t *cls, size_t len, uint32_t iface, uint8_t *out, size_t cap, uint8_t **big) {
	ak_buf b = {0};
	ak_buf_u8(&b, 0);
	ak_buf_u64(&b, len == 5 && memcmp(cls, "Clock", 5) == 0 ? 40 + iface : 0);
	size_t n;
	put(out, cap, big, &b, &n);
	return n;
}

static size_t ncall(uint64_t h, uint32_t method, const uint8_t *in, size_t len, uint8_t *out, size_t cap, uint8_t **big) {
	ak_reader r = {in, len, 0, false};
	ak_buf b = {0};
	if (method == 1) {
		uint32_t sn;
		const uint8_t *s = ak_read_bytes(&r, &sn);
		int64_t i = (int64_t)ak_read_u64(&r);
		char tmp[64];
		int w = snprintf(tmp, sizeof tmp, "%.*s#%lld@%llu", (int)sn, s, (long long)i, (unsigned long long)h);
		ak_buf_u8(&b, 0);
		ak_buf_str(&b, tmp, (size_t)w);
	} else if (method == 2) {
		ak_buf_u8(&b, 1);
		ak_buf_u32(&b, 0);
	} else if (method == 4) {
		callback_id = ak_read_u64(&r);
		ak_buf_u8(&b, 0);
	}
	size_t n;
	put(out, cap, big, &b, &n);
	return n;
}

static void nstart(uint64_t h, uint32_t method, const uint8_t *in, size_t len, uint64_t call) { pending_call = call; }
static void ncancel(uint64_t call) {}
static void nrelease(uint64_t h) { released = h; }
static void nfree(void *p) { free(p); }

static asuka_host_callbacks host = {.native_new = nnew, .native_call = ncall, .native_start = nstart, .native_cancel = ncancel, .native_release = nrelease, .native_free = nfree};

static const uint8_t tuple_kinds[] = {AK_K_REF, AK_K_INT};
static const ak_shape tuple = {.name = "(string, int)", .nfields = 2, .kinds = tuple_kinds};

TEST(objects_and_calls) {
	ak_native_set_host(&host);
	ak_str *declared = ak_str_c("low power");
	ak_arr *errors = ak_arr_new(AK_K_REF, NULL, 0);
	ak_struct *e = ak_struct_new(&ak_shape_error);
	e->f[0].p = declared;
	ak_arr_push(&errors, (ak_val){.p = e});
	ak_val o = ak_native_new("Clock", 5, 2, errors);
	REQUIRE(!ak_error_pending());
	ak_native_obj *obj = o.p;
	CHECK_EQ_INT(obj->handle, 42);
	ak_struct *args = ak_struct_new(&tuple);
	args->f[0].p = ak_str_c("tick");
	args->f[1].i = 3;
	ak_val r = ak_native_call(obj, 1, (ak_val){.p = args}, "si", "s", errors, NULL);
	REQUIRE(!ak_error_pending());
	CHECK_EQ_STR(((ak_str *)r.p)->data, "tick#3@42");
	ak_release(r.p);
	ak_native_call(obj, 2, (ak_val){.p = args}, "si", "v", errors, NULL);
	void *err = ak_error_take();
	REQUIRE(err != NULL);
	CHECK_EQ_STR(ak_error_message(err)->data, "low power");
	ak_release(err);
	ak_release(args);
	ak_release(obj);
	CHECK_EQ_INT(released, 42);
	ak_release(errors);
}

TEST(started_calls_answer_later) {
	ak_native_set_host(&host);
	ak_native_obj *obj = ak_native_wrap(7);
	ak_future *f = ak_native_start(obj, 3, (ak_val){0}, "", "i", NULL, NULL);
	CHECK(f->state == AK_FUT_PENDING);
	uint8_t answer[9] = {0, 99};  // ok, then i64 99
	ak_native_done(pending_call, answer, sizeof answer);
	CHECK(f->state == AK_FUT_PENDING);  // delivered on the main thread's next turn
	ak_sched_run_posted();
	REQUIRE(f->state == AK_FUT_DONE);
	CHECK_EQ_INT(f->result.i, 99);
	ak_release(f);
	ak_release(obj);
}

// A module, as the VM's: its descriptors are its strings, freed with it
// (a dev reload lets it go while a call is still out).
typedef struct {
	int rc;
	char *ret;  // a result's descriptor
	ak_json_types types;
} test_module;

static void module_hold(void *ctx, bool keep) {
	test_module *m = ctx;
	if (keep) {
		m->rc++;
		return;
	}
	if (--m->rc) return;
	free(m->ret);
	free(m);
}

static test_module *module_new(const char *ret) {
	test_module *m = calloc(1, sizeof *m);
	m->rc = 1;
	m->ret = malloc(strlen(ret) + 1);
	strcpy(m->ret, ret);
	m->types = (ak_json_types){NULL, NULL, m, module_hold};
	return m;
}

// A started call holds the module its answer is read by: the module
// let go before the answer comes is freed only once it's read.
TEST(started_call_outlives_its_module) {
	ak_native_set_host(&host);
	ak_native_obj *obj = ak_native_wrap(7);
	test_module *m = module_new("i");
	ak_future *f = ak_native_start(obj, 3, (ak_val){0}, "", m->ret, NULL, &m->types);
	CHECK_EQ_INT(m->rc, 2);
	module_hold(m, false);  // the reload: only the call holds it now
	uint8_t answer[9] = {0, 99};
	ak_native_done(pending_call, answer, sizeof answer);
	ak_sched_run_posted();
	REQUIRE(f->state == AK_FUT_DONE);
	CHECK_EQ_INT(f->result.i, 99);
	ak_release(f);
	ak_release(obj);
}

static ak_val twice(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(n);
	return (ak_val){.i = args[0].i * 2};
}

TEST(callbacks) {
	ak_native_set_host(&host);
	ak_native_obj *obj = ak_native_wrap(9);
	ak_closure *c = ak_closure_new(twice, NULL, 0, NULL);
	ak_struct *args = ak_struct_new(&(ak_shape){.name = "(fn)", .nfields = 1, .kinds = (const uint8_t[]){AK_K_REF}});
	args->f[0].p = ak_retain(c);
	ak_native_call(obj, 4, (ak_val){.p = args}, "F1ii", "v", NULL, NULL);
	REQUIRE(callback_id != 0);
	uint8_t in[8] = {21};
	uint8_t *out;
	size_t n = ak_native_invoke(callback_id, in, sizeof in, &out);
	REQUIRE(n == 8);
	CHECK_EQ_INT(out[0], 42);
	ak_free(out);
	ak_release(args);
	ak_release(c);
	ak_release(obj);
}

// A callback that registers callbacks of its own (passing closures to
// native methods) grows the callbacks' table under the call: the result
// is still encoded with the callback's own descriptor.
static ak_val plain(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(args);
	AK_UNUSED(n);
	return (ak_val){.i = 1};
}

static void pass(ak_native_obj *obj, ak_closure *c) {
	ak_struct *args = ak_struct_new(&(ak_shape){.name = "(fn)", .nfields = 1, .kinds = (const uint8_t[]){AK_K_REF}});
	args->f[0].p = ak_retain(c);
	ak_native_call(obj, 4, (ak_val){.p = args}, "F0i", "v", NULL, NULL);
	ak_release(args);
}

static ak_val registers(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(args);
	AK_UNUSED(n);
	for (int i = 0; i < 64; i++) {
		ak_closure *k = ak_closure_new(plain, NULL, 0, NULL);
		pass((ak_native_obj *)c->fn, k);
		ak_release(k);
	}
	return (ak_val){.i = 7};
}

TEST(callback_registers_callbacks) {
	ak_native_set_host(&host);
	ak_native_obj *obj = ak_native_wrap(9);
	ak_closure *c = ak_closure_new(registers, obj, 0, NULL);
	pass(obj, c);
	uint64_t id = callback_id;
	uint8_t *out;
	size_t n = ak_native_invoke(id, NULL, 0, &out);
	REQUIRE(n == 8);
	CHECK_EQ_INT(out[0], 7);
	ak_free(out);
	ak_release(c);
	ak_release(obj);
}

// A callback lives as long as the object it was passed to: then the
// closure, and the module its descriptor is in, go; a call after that
// finds none. The same closure passed with another descriptor is another
// callback, its calls read and answered by its own.
static ak_val halve(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(n);
	return (ak_val){.f = args[0].f / 2};
}

TEST(callbacks_go_with_their_object) {
	ak_native_set_host(&host);
	int64_t live = ak_obj_live;
	ak_native_obj *obj = ak_native_wrap(9);
	test_module *m = module_new("F1ffv");
	ak_closure *c = ak_closure_new(halve, NULL, 0, NULL);
	ak_struct *args = ak_struct_new(&(ak_shape){.name = "(fn)", .nfields = 1, .kinds = (const uint8_t[]){AK_K_REF}});
	args->f[0].p = ak_retain(c);
	ak_native_call(obj, 4, (ak_val){.p = args}, m->ret, "v", NULL, &m->types);
	uint64_t floats = callback_id;
	ak_native_call(obj, 4, (ak_val){.p = args}, m->ret, "v", NULL, &m->types);
	CHECK_EQ_INT(callback_id, floats);  // the same callback
	ak_native_call(obj, 4, (ak_val){.p = args}, "F1ii", "v", NULL, &m->types);
	uint64_t ints = callback_id;
	CHECK(ints != floats);
	ak_release(args);
	ak_release(c);
	CHECK_EQ_INT(m->rc, 3);  // the module's own, then each callback's
	module_hold(m, false);   // the reload
	double x = 21;
	uint8_t in[8];
	memcpy(in, &x, 8);
	uint8_t *out;
	REQUIRE(ak_native_invoke(floats, in, sizeof in, &out) == 8);
	double y;
	memcpy(&y, out, 8);
	CHECK_EQ_F(y, 10.5);
	ak_free(out);
	ak_release(obj);  // its callbacks go, and with the last, the module
	CHECK_EQ_INT(ak_obj_live, live);
	CHECK_EQ_INT(ak_native_invoke(floats, in, sizeof in, &out), 0);
	CHECK_EQ_INT(ak_native_invoke(ints, in, sizeof in, &out), 0);
}

// An answer cut short, or wrong, throws and leaves nothing built behind:
// no half-decoded value (a scalar read as an option's box), nothing
// leaked.
TEST(short_answers) {
	static const struct {
		const char *desc;
		uint8_t bytes[16];
		size_t n;
	} cases[] = {
		{"?i", {1, 0x41, 0x41, 0x41, 0x41}, 5},
		{"?f", {1, 1, 2, 3}, 4},
		{"?s", {1, 5, 0, 0, 0, 'a'}, 6},
		{"[s", {2, 0, 0, 0, 1, 0, 0, 0, 'a'}, 9},
		{"[?i", {2, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1}, 15},
		{"{s", {1, 0, 0, 0, 1, 0, 0, 0, 'k', 3, 0, 0, 0}, 13},
		{"[i", {0xff, 0xff, 0xff, 0x7f}, 4},
		{"{i", {0xff, 0xff, 0xff, 0x7f}, 4},
		{"?[?s", {1, 1, 0, 0, 0, 1, 2, 0, 0, 0, 'x'}, 11},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		ak_reader r = {cases[i].bytes, cases[i].n, 0, false};
		ak_val v = ak_native_decode(&r, cases[i].desc, NULL);
		void *e = ak_error_take();
		if (!e) T_FAIL("%s: no error", cases[i].desc);
		ak_release(e);
		CHECK(v.p == NULL);
		ak_release_val(v, AK_K_REF);  // every case is a reference
	}
}

// A callback whose arguments are cut short isn't called and frees them.
static int called;
static ak_val counts(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c);
	AK_UNUSED(args);
	AK_UNUSED(n);
	called++;
	return (ak_val){0};
}

TEST(callback_short_arguments) {
	ak_native_set_host(&host);
	ak_native_obj *obj = ak_native_wrap(9);
	ak_closure *c = ak_closure_new(counts, NULL, 0, NULL);
	ak_struct *args = ak_struct_new(&(ak_shape){.name = "(fn)", .nfields = 1, .kinds = (const uint8_t[]){AK_K_REF}});
	args->f[0].p = ak_retain(c);
	ak_native_call(obj, 4, (ak_val){.p = args}, "F2?i[sv", "v", NULL, NULL);
	uint8_t in[] = {1, 0x41, 0x41, 0x41, 0x41};
	uint8_t *out;
	CHECK_EQ_INT(ak_native_invoke(callback_id, in, sizeof in, &out), 0);
	uint8_t in2[] = {0, 2, 0, 0, 0, 1, 0, 0, 0, 'a'};
	CHECK_EQ_INT(ak_native_invoke(callback_id, in2, sizeof in2, &out), 0);
	CHECK_EQ_INT(called, 0);
	ak_release(args);
	ak_release(c);
	ak_release(obj);
}

// Answers from the native side, random: decoded, then released as their
// descriptor's kind, error or not (as ak_native_invoke does), nothing is
// read past the answer, freed wrongly or leaked.
static uint64_t fuzz_state = 0x9e3779b97f4a7c15u;
static uint32_t fuzz_rand(void) {
	fuzz_state ^= fuzz_state << 13;
	fuzz_state ^= fuzz_state >> 7;
	fuzz_state ^= fuzz_state << 17;
	return (uint32_t)fuzz_state;
}

static ak_kind desc_kind(const char *d) {
	switch (*d) {
	case 'i': return AK_K_INT;
	case 'f': return AK_K_FLOAT;
	case 'b': return AK_K_BOOL;
	case 'v': return AK_K_VOID;
	default: return AK_K_REF;
	}
}

TEST(fuzz_answers) {
	static const char *const descs[] = {"i", "f", "b", "s", "y", "o", "?i", "?f", "?b", "?s", "?o", "[i", "[s", "[?i", "[[s", "{s", "{?i", "{[s", "?[s", "?{i", "[{?s"};
	int64_t live = ak_obj_live;
	uint8_t r[48];
	for (int it = 0; it < 40000; it++) {
		size_t n = fuzz_rand() % sizeof r;
		for (size_t i = 0; i < n; i++) {
			uint32_t k = fuzz_rand() % 8;
			r[i] = k < 5 ? (uint8_t)(fuzz_rand() % 3) : (uint8_t)fuzz_rand();
		}
		uint8_t *c = malloc(n ? n : 1);
		if (n) memcpy(c, r, n);
		const char *d = descs[fuzz_rand() % AK_LEN(descs)];
		ak_reader rd = {c, n, 0, false};
		ak_val v = ak_native_decode(&rd, d, NULL);
		void *e = ak_error_take();
		if (e) {
			if (v.p != NULL && desc_kind(d) == AK_K_REF) T_FAIL("%s: a value and an error", d);
			ak_release(e);
		}
		ak_release_val(v, desc_kind(d));
		free(c);
	}
	CHECK_EQ_INT(ak_obj_live, live);
}

// A host that says its answer is longer than the buffer it wrote it to,
// without memory of its own for it: a bad answer, not a read past it.
static size_t too_long(uint64_t h, uint32_t method, const uint8_t *in, size_t len, uint8_t *out, size_t cap, uint8_t **big) {
	AK_UNUSED(h);
	AK_UNUSED(method);
	AK_UNUSED(in);
	AK_UNUSED(len);
	AK_UNUSED(big);
	// Ok, then a string that runs past out, within the length said.
	uint32_t n = (uint32_t)cap;
	out[0] = 0;
	memcpy(out + 1, &n, 4);
	return cap + 4096;
}

TEST(answer_longer_than_its_buffer) {
	asuka_host_callbacks h = host;
	h.native_call = too_long;
	ak_native_set_host(&h);
	ak_native_obj *obj = ak_native_wrap(9);
	ak_val v = ak_native_call(obj, 1, (ak_val){0}, "", "s", NULL, NULL);
	void *e = ak_error_take();
	CHECK(e != NULL);
	CHECK(v.p == NULL);
	ak_release(e);
	ak_release(obj);
	ak_native_set_host(&host);
}

T_MAIN()
