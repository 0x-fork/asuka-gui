#define _POSIX_C_SOURCE 200809L  // nanosleep, fork and pipe under -std=c17
#include "t.h"
#include "base/base.h"
#include "base/thread.h"

#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

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

#if !defined(_WIN32)

static int panic_fd;
static void panic_to_pipe(const char *msg) {
	ssize_t w = write(panic_fd, msg, strlen(msg));
	AK_UNUSED(w);
}

// panics runs fn in a child process and gives the panic it ended with
// ("" if none: it returned, or crashed some other way).
static void panics(void (*fn)(void), char *out, size_t cap) {
	int p[2];
	out[0] = 0;
	if (pipe(p) != 0) return;
	pid_t pid = fork();
	if (pid == 0) {
		close(p[0]);
		panic_fd = p[1];
		ak_panic_hook = panic_to_pipe;
		alarm(30);  // a safety net only: it must panic, not loop
		fn();
		_exit(0);
	}
	close(p[1]);
	size_t n = 0;
	ssize_t r;
	while (n + 1 < cap && (r = read(p[0], out + n, cap - 1 - n)) > 0) n += (size_t)r;
	out[n] = 0;
	close(p[0]);
	int st;
	waitpid(pid, &st, 0);
}

// A vector or a buffer past what its sizes count is out of memory: a
// panic, not a size that wraps (a growth that doesn't, then writes past
// the end). (The sizes are faked: nothing that large is allocated.)
static uint8_t small[16];

static void vec_past_u32(void) {
	AK_VEC(uint8_t) v = {small, UINT32_MAX - 1, UINT32_MAX - 1};
	ak_vec_reserve(&v, 2);
}

static void vec_many(void) {
	AK_VEC(uint8_t) v = {small, 16, 16};
	ak_vec_reserve(&v, UINT32_MAX);
}

static void buf_past_size(void) {
	ak_buf b = {small, SIZE_MAX - 10, SIZE_MAX - 10};
	ak_buf_reserve(&b, 100);
}

static void buf_many(void) {
	ak_buf b = {small, 16, 16};
	ak_buf_reserve(&b, SIZE_MAX - 8);
}

// Two threads that panic at once: one panic is reported, whole, and the
// process ends with it (not the other's message over its own, or both
// written into each other's record). The first's hook lets the second
// panic before it writes.
static ak_mutex two_mu = AK_MUTEX_INIT;
static ak_cond two_cond;
static bool second_going;

static void slow_hook(const char *msg) {
	ak_mutex_lock(&two_mu);
	while (!second_going) ak_cond_wait(&two_cond, &two_mu);
	ak_mutex_unlock(&two_mu);
	struct timespec ts = {0, 100 * 1000000};  // for the second to get in
	nanosleep(&ts, NULL);
	panic_to_pipe(msg);
	panic_to_pipe("|");
}

static void second_panics(void *arg) {
	AK_UNUSED(arg);
	ak_mutex_lock(&two_mu);
	second_going = true;
	ak_cond_broadcast(&two_cond);
	ak_mutex_unlock(&two_mu);
	ak_panic("second");
}

static void two_threads_panic(void) {
	ak_cond_init(&two_cond);
	ak_panic_hook = slow_hook;
	if (!ak_thread_start(second_panics, NULL)) return;
	ak_panic("first");
}

TEST(concurrent_panics) {
	char msg[256];
	panics(two_threads_panic, msg, sizeof msg);
	CHECK_EQ_STR(msg, "first|");
}

TEST(growth_past_sizes) {
	char msg[256];
	void (*fns[])(void) = {vec_past_u32, vec_many, buf_past_size, buf_many};
	for (size_t i = 0; i < AK_LEN(fns); i++) {
		panics(fns[i], msg, sizeof msg);
		if (!strstr(msg, "out of memory")) T_FAIL("case %zu: %s", i, msg);
	}
}
#endif

// A wait whose deadline is past what a time holds waits for the signal.
static ak_mutex wait_mu = AK_MUTEX_INIT;
static ak_cond wait_cond;
static bool signaled;

static void signal_it(void *arg) {
	AK_UNUSED(arg);
	ak_mutex_lock(&wait_mu);
	signaled = true;
	ak_cond_signal(&wait_cond);
	ak_mutex_unlock(&wait_mu);
}

TEST(wait_forever) {
	ak_cond_init(&wait_cond);
	ak_mutex_lock(&wait_mu);
	REQUIRE(ak_thread_start(signal_it, NULL));
	while (!signaled) ak_cond_wait_ns(&wait_cond, &wait_mu, INT64_MAX);
	ak_mutex_unlock(&wait_mu);
	CHECK(signaled);
}

T_MAIN()
