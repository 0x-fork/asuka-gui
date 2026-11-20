// The allocator (base/alloc.c): blocks zeroed and kept as they grow (a
// big one's pages moved), small blocks zeroed again as they're reused,
// blocks freed on another thread than made them (spawn's workers, the
// I/O threads), also after that thread ended, and a spike's memory
// given back (ak_alloc_trim).
// Under AddressSanitizer it's the C library's (the build's own check of
// every block); TestCThreads runs it again under ThreadSanitizer with
// mimalloc.
#include "t.h"
#include "base/base.h"
#include "base/thread.h"

static bool zero(const unsigned char *p, size_t n) {
	for (size_t i = 0; i < n; i++)
		if (p[i]) return false;
	return true;
}

TEST(alloc_zeroed_and_grown) {
	// Sizes in each of the allocator's ranges: small, medium, large, huge.
	size_t sizes[] = {0, 1, 7, 16, 100, 256, 1000, 4096, 70000, 600000, 5 << 20, 40 << 20};
	for (size_t i = 0; i < AK_LEN(sizes); i++) {
		size_t n = sizes[i];
		unsigned char *p = ak_alloc(n);
		REQUIRE(p != NULL);
		CHECK(zero(p, n));
		for (size_t j = 0; j < n; j++) p[j] = (unsigned char)(j * 7 + 1);
		// Grown and shrunk, what it held is kept.
		size_t m = n * 3 + 10;
		p = ak_realloc(p, m);
		bool kept = true;
		for (size_t j = 0; j < n; j++) kept = kept && p[j] == (unsigned char)(j * 7 + 1);
		CHECK(kept);
		p = ak_realloc(p, n / 2 + 1);
		CHECK(p[0] == (n ? 1 : p[0]));
		ak_free(p);
	}
	ak_free(NULL);
}

// A big block grown by doubling (an array's, a string's) moves its
// pages to the new block, or is copied and its pages released: what it
// held is kept at every size, whatever offset in its first page it
// starts at, and memory made after reads as zeros.
TEST(alloc_grown_big_kept) {
	for (int start = 0; start < 3; start++) {
		size_t n = ((size_t)1 << 20) + (size_t)start * 12345 + 8;
		unsigned char *p = ak_alloc(n);
		for (size_t j = 0; j < n; j++) p[j] = (unsigned char)(j * 13 + 5);
		while (n < ((size_t)48 << 20)) {
			size_t m = n * 2 + (size_t)start * 40;
			p = ak_realloc(p, m);
			bool kept = true;
			for (size_t j = 0; j < n; j++) kept = kept && p[j] == (unsigned char)(j * 13 + 5);
			REQUIRE(kept);
			for (size_t j = n; j < m; j++) p[j] = (unsigned char)(j * 13 + 5);
			n = m;
		}
		// What the grown-out-of blocks were is the allocator's again.
		unsigned char *q[4];
		for (int k = 0; k < 4; k++) {
			q[k] = ak_alloc((size_t)(3 + k) << 20);
			CHECK(zero(q[k], (size_t)(3 + k) << 20));
			memset(q[k], 0xa5, (size_t)(3 + k) << 20);
		}
		bool kept = true;
		for (size_t j = 0; j < n; j++) kept = kept && p[j] == (unsigned char)(j * 13 + 5);
		CHECK(kept);
		for (int k = 0; k < 4; k++) ak_free(q[k]);
		ak_free(p);
	}
}

TEST(alloc_small_reused_zeroed) {
	// A small block freed and made again comes back zeroed, whatever was
	// in it.
	for (size_t n = 8; n <= AK_SMALL_MAX; n += 24) {
		void *ps[64];
		for (int i = 0; i < 64; i++) {
			ps[i] = ak_alloc_small(n);
			REQUIRE(zero(ps[i], n));
			memset(ps[i], 0xab, n);
		}
		for (int i = 0; i < 64; i++) ak_free_small(ps[i], n);
		for (int i = 0; i < 64; i++) {
			ps[i] = ak_alloc_small(n);
			CHECK(zero(ps[i], n));
		}
		for (int i = 0; i < 64; i++) ak_free_small(ps[i], n);
	}
}

// Blocks made on one thread and freed on another, both ways: a worker
// makes a batch and hands it over, the main thread frees it and hands
// back a batch of its own for the worker to free; then the worker ends,
// its blocks still alive, and the main thread frees them after.
enum { BATCH = 2000, ROUNDS = 20 };
static ak_mutex mu = AK_MUTEX_INIT;
static ak_cond cond;
static void *to_main[BATCH], *to_worker[BATCH];
static int round_made, round_freed;  // under mu
static bool worker_done;

// The i'th block's size: every third a small block (at most
// AK_SMALL_MAX), the others any size.
static size_t size_of(int i) {
	size_t n = (size_t)(i % 7 == 0 ? 3000 + i : 8 + (i * 13) % 240);
	return i % 3 ? n : n < AK_SMALL_MAX ? n : AK_SMALL_MAX;
}

static void fill(void **ps, unsigned char tag) {
	for (int i = 0; i < BATCH; i++) {
		size_t n = size_of(i);
		unsigned char *p = i % 3 ? ak_alloc(n) : ak_alloc_small(n);
		memset(p, tag, n);
		ps[i] = p;
	}
}

static int drain(void **ps, unsigned char tag) {
	int bad = 0;
	for (int i = 0; i < BATCH; i++) {
		size_t n = size_of(i);
		unsigned char *p = ps[i];
		if (p[0] != tag) bad++;
		if (i % 3) ak_free(p);
		else ak_free_small(p, n);
		ps[i] = NULL;
	}
	return bad;
}

static int worker_bad;

static void worker(void *arg) {
	AK_UNUSED(arg);
	for (int r = 0; r < ROUNDS; r++) {
		fill(to_main, (unsigned char)(r + 1));
		ak_mutex_lock(&mu);
		round_made = r + 1;
		ak_cond_broadcast(&cond);
		while (round_freed < r + 1) ak_cond_wait(&cond, &mu);
		ak_mutex_unlock(&mu);
		// The main thread's batch, freed here.
		worker_bad += drain(to_worker, (unsigned char)(0x80 + r));
	}
	// A last batch, alive as the thread ends.
	fill(to_main, 0x7f);
	ak_mutex_lock(&mu);
	worker_done = true;
	ak_cond_broadcast(&cond);
	ak_mutex_unlock(&mu);
}

TEST(alloc_across_threads) {
	ak_cond_init(&cond);
	REQUIRE(ak_thread_start(worker, NULL));
	int bad = 0;
	for (int r = 0; r < ROUNDS; r++) {
		ak_mutex_lock(&mu);
		while (round_made < r + 1) ak_cond_wait(&cond, &mu);
		ak_mutex_unlock(&mu);
		bad += drain(to_main, (unsigned char)(r + 1));
		fill(to_worker, (unsigned char)(0x80 + r));
		ak_mutex_lock(&mu);
		round_freed = r + 1;
		ak_cond_broadcast(&cond);
		ak_mutex_unlock(&mu);
	}
	ak_mutex_lock(&mu);
	while (!worker_done) ak_cond_wait(&cond, &mu);
	ak_mutex_unlock(&mu);
	CHECK_EQ_INT(worker_bad, 0);
	// The ended thread's blocks, freed here: its pages are the main
	// thread's to take now.
	bad += drain(to_main, 0x7f);
	CHECK_EQ_INT(bad, 0);
	// And the memory is the main thread's to use again.
	fill(to_main, 0x11);
	CHECK_EQ_INT(drain(to_main, 0x11), 0);
}

TEST(alloc_trim) {
	// A spike of small blocks: the heap grows (mimalloc's; the C
	// library's says nothing), and once they're freed and trimmed it
	// hasn't any more.
	enum { N = 100000 };
	void **ps = ak_alloc(sizeof(void *) * N);
	for (int i = 0; i < N; i++) ps[i] = ak_alloc(48 + i % 64);
	bool grew = ak_alloc_grown();
	for (int i = 0; i < N; i++) ak_free(ps[i]);
	ak_free(ps);
	ak_alloc_trim();
	CHECK(!ak_alloc_grown());
	if (grew) {
		// Made again, it's grown again.
		ps = ak_alloc(sizeof(void *) * N);
		for (int i = 0; i < N; i++) ps[i] = ak_alloc(48 + i % 64);
		CHECK(ak_alloc_grown());
		for (int i = 0; i < N; i++) ak_free(ps[i]);
		ak_free(ps);
		ak_alloc_trim();
	}
}

T_MAIN()
