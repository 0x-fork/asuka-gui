#include "t.h"
#include "base/base.h"
#include "base/thread.h"

TEST(buf_roundtrip) {
	ak_buf b = {0};
	ak_buf_u8(&b, 7);
	ak_buf_u16(&b, 0x1234);
	ak_buf_u32(&b, 0xdeadbeef);
	ak_buf_u64(&b, 0x0102030405060708ull);
	ak_buf_f32(&b, 1.5f);
	ak_buf_f64(&b, -2.25);
	ak_buf_str(&b, "hi", 2);
	ak_reader r = {b.data, b.len, 0, false};
	CHECK_EQ_INT(ak_read_u8(&r), 7);
	CHECK_EQ_INT(ak_read_u16(&r), 0x1234);
	CHECK_EQ_INT(ak_read_u32(&r), 0xdeadbeef);
	CHECK(ak_read_u64(&r) == 0x0102030405060708ull);
	CHECK_EQ_F(ak_read_f32(&r), 1.5);
	CHECK_EQ_F(ak_read_f64(&r), -2.25);
	uint32_t n;
	const uint8_t *s = ak_read_bytes(&r, &n);
	CHECK_EQ_INT(n, 2);
	CHECK(memcmp(s, "hi", 2) == 0);
	CHECK(!r.err);
	ak_read_u8(&r);
	CHECK(r.err);
	ak_buf_free(&b);
}

TEST(vec) {
	AK_VEC(int) v = {0};
	for (int i = 0; i < 100; i++) ak_vec_push(&v, i);
	CHECK_EQ_INT(v.len, 100);
	CHECK_EQ_INT(v.data[99], 99);
	CHECK_EQ_INT(ak_vec_pop(&v), 99);
	ak_vec_free(&v);
}

// ak_alloc_count is each thread's own: what another thread allocates
// (spawn's workers, the native side's) neither races with the main
// thread's count nor changes what a test counts on it.
static ak_mutex worker_mu = AK_MUTEX_INIT;
static ak_cond worker_cond;
static bool worker_go, worker_finished;
static uint64_t worker_count;

static void allocates(void *arg) {
	AK_UNUSED(arg);
	ak_mutex_lock(&worker_mu);
	while (!worker_go) ak_cond_wait(&worker_cond, &worker_mu);
	ak_mutex_unlock(&worker_mu);
	uint64_t before = ak_alloc_count;
	for (int i = 0; i < 100; i++) ak_free(ak_alloc(16));
	ak_mutex_lock(&worker_mu);
	worker_count = ak_alloc_count - before;
	worker_finished = true;
	ak_cond_broadcast(&worker_cond);
	ak_mutex_unlock(&worker_mu);
}

TEST(alloc_count_per_thread) {
	ak_cond_init(&worker_cond);
	REQUIRE(ak_thread_start(allocates, NULL));
	uint64_t before = ak_alloc_count;
	ak_mutex_lock(&worker_mu);
	worker_go = true;
	ak_cond_broadcast(&worker_cond);
	while (!worker_finished) ak_cond_wait(&worker_cond, &worker_mu);
	ak_mutex_unlock(&worker_mu);
	CHECK_EQ_INT(worker_count, 100);
	CHECK_EQ_INT(ak_alloc_count - before, 0);
}

T_MAIN()
