// calls_test.c: the registry's started calls (src/native.c) under
// ThreadSanitizer (platform/desktop's TestCallsRace): a call's cancel
// registered on a worker (as asuka_fetch registers its own) while the
// task goes on the main thread, the call answered from the worker, which
// frees the cancel's context as soon as it has (as asuka_desktop.h lets
// it). The core's half is stood in for here; posts run on a thread of
// their own.
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "asuka_desktop.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- The core's and the host's half ----

static atomic_int answers;

void asuka_native_done(uint64_t call, const uint8_t *p, size_t len) { atomic_fetch_add(&answers, 1); }
size_t asuka_native_invoke(uint64_t id, const uint8_t *p, size_t len, uint8_t **out) { return 0; }
void asuka_free(void *p) { free(p); }
void asuka_event(const uint8_t *buf, size_t len) {}

// Posts run in order on one thread, the "main" one here.
typedef struct post {
	struct post *next;
	void (*fn)(void *);
	void *ctx;
} post;
static pthread_mutex_t posts_mu = PTHREAD_MUTEX_INITIALIZER;
static post *posts, **posts_tail = &posts;

void asuka_desktop_post(void (*fn)(void *ctx), void *ctx) {
	post *p = malloc(sizeof *p);
	*p = (post){NULL, fn, ctx};
	pthread_mutex_lock(&posts_mu);
	*posts_tail = p;
	posts_tail = &p->next;
	pthread_mutex_unlock(&posts_mu);
}

static void run_posts(void) {
	for (;;) {
		pthread_mutex_lock(&posts_mu);
		post *p = posts;
		if (p) {
			posts = p->next;
			if (!posts) posts_tail = &posts;
		}
		pthread_mutex_unlock(&posts_mu);
		if (!p) return;
		p->fn(p->ctx);
		free(p);
	}
}

bool asuka_desktop_on_main(void) { return false; }

// ---- A package: start registers its cancel on a worker ----

typedef struct {
	asuka_call *call;
	atomic_int *cancels;
} job;

// The cancel reads its context, which the worker frees once it answered.
static void cancelled(void *p) {
	job *j = p;
	atomic_int *cancels = j->cancels;
	for (volatile int k = 0; k < 20000; k++) {  // long enough for the worker to answer meanwhile
	}
	atomic_fetch_add(cancels, 1);
	atomic_fetch_add(j->cancels, 0);  // still j's, after
}

// registered: the worker's cancel is set, the task may go (odd runs wait
// for it, so that the cancel and the answer meet).
static atomic_int registered;

static void *work(void *p) {
	job *j = p;
	asuka_call_on_cancel(j->call, cancelled, j);
	atomic_store(&registered, 1);
	asuka_writer w = asuka_call_begin();
	asuka_call_done(j->call, &w);
	free(j);  // answered: the context may go
	return NULL;
}

static atomic_int cancels;
static pthread_t worker;

static void *make(uint32_t iface, asuka_error *err) { return &cancels; }
static void call(void *obj, uint32_t iface, uint32_t method, asuka_reader *in, asuka_writer *out, asuka_error *err) {}
static void start(void *obj, uint32_t iface, uint32_t method, asuka_reader *in, asuka_call *c) {
	job *j = malloc(sizeof *j);
	*j = (job){c, obj};
	pthread_create(&worker, NULL, work, j);
}
static void release(void *obj, uint32_t iface) {}

static const asuka_native_provider provider = {"test/calls", make, call, start, release, NULL};

int main(void) {
	asuka_native_register(&provider);
	asuka_host_callbacks cb;
	memset(&cb, 0, sizeof cb);
	asuka_native_callbacks(&cb);
	uint8_t out[64], *big = NULL;
	cb.native_new((const uint8_t *)"test/calls", 10, 0, out, sizeof out, &big);
	uint64_t handle;
	memcpy(&handle, out + 1, 8);
	enum { runs = 2000 };
	for (uint64_t id = 1; id <= runs; id++) {
		atomic_store(&registered, 0);
		cb.native_start(handle, 0, NULL, 0, id);
		if (id & 1)
			while (!atomic_load(&registered)) {
			}
		cb.native_cancel(id);  // the task goes as the worker registers its cancel, or answers
		pthread_join(worker, NULL);
		run_posts();
	}
	int n = atomic_load(&answers);
	fprintf(stderr, "%d answers, %d cancels\n", n, atomic_load(&cancels));
	return n == runs ? 0 : 1;
}
