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

T_MAIN()
