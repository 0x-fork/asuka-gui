// Small blocks (base/small.c): a thread's freed ones reused by size
// class, zeroed again, up to its budget; a thread's cache goes back to
// malloc as it ends. Built here with the cache on whatever the build
// (the core's own is off under a sanitizer), its functions renamed.
#include "t.h"
#include "base/thread.h"

#define AK_SMALL_CACHE 1
#define BUDGET 4096
#define ak_alloc_small t_alloc_small
#define ak_free_small t_free_small
#include "base/small.c"

TEST(small_reuse) {
	unsigned char *a = t_alloc_small(40);
	REQUIRE(a != NULL);
	for (int i = 0; i < 40; i++) CHECK(a[i] == 0);
	memset(a, 0xab, 40);
	t_free_small(a, 40);
	// 33 to 48 bytes are one class: the block comes back, zeroed whole.
	unsigned char *b = t_alloc_small(48);
	CHECK(b == a);
	for (int i = 0; i < 48; i++) CHECK(b[i] == 0);
	// Another class doesn't take it.
	t_free_small(b, 48);
	unsigned char *c = t_alloc_small(64);
	CHECK(c != b);
	unsigned char *d = t_alloc_small(33);
	CHECK(d == b);
	t_free_small(c, 64);
	t_free_small(d, 33);
}

TEST(small_budget) {
	// Past the budget, blocks go back to malloc: the cache holds no more.
	void *ps[64];
	for (int i = 0; i < 64; i++) ps[i] = t_alloc_small(AK_SMALL_MAX);
	for (int i = 0; i < 64; i++) t_free_small(ps[i], AK_SMALL_MAX);
	REQUIRE(mine != NULL);
	CHECK(mine->bytes <= BUDGET);
	CHECK(mine->bytes > BUDGET - AK_SMALL_MAX);  // as full as it gets
	for (int i = 0; i < 64; i++) ps[i] = t_alloc_small(AK_SMALL_MAX);
	CHECK(mine->free[CLASS(AK_SMALL_MAX)] == NULL);  // what it held, taken
	for (int i = 0; i < 64; i++) t_free_small(ps[i], AK_SMALL_MAX);
}

// A thread's cache: its own, and freed as it ends (LeakSanitizer would
// report its blocks otherwise).
static ak_mutex mu = AK_MUTEX_INIT;
static ak_cond cond;
static bool finished;
static void *seen;

static void caches(void *arg) {
	AK_UNUSED(arg);
	void *p = t_alloc_small(24);
	t_free_small(p, 24);
	ak_mutex_lock(&mu);
	seen = mine;
	finished = true;
	ak_cond_broadcast(&cond);
	ak_mutex_unlock(&mu);
}

TEST(small_per_thread) {
	void *p = t_alloc_small(24);
	t_free_small(p, 24);
	ak_cond_init(&cond);
	REQUIRE(ak_thread_start(caches, NULL));
	ak_mutex_lock(&mu);
	while (!finished) ak_cond_wait(&cond, &mu);
	ak_mutex_unlock(&mu);
	CHECK(seen != NULL);
	CHECK(seen != mine);
	// This thread's block is still its own.
	CHECK(t_alloc_small(24) == p);
	t_free_small(p, 24);
}

T_MAIN()
