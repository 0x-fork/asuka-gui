// monocypher.c: Monocypher 4.0.2 (third_party/monocypher), compiled in
// under the core's names (monocypher.h). What std/crypto doesn't call of
// it (X25519, EdDSA, Elligator) the link leaves out.
//
// Argon2's lanes (its parallelism) are filled a thread each, as RFC 9106
// means them to be and Go's x/crypto fills them: Monocypher fills a
// slice's segments one after the other, and calls AK_MC_ARGON2_LANES
// (our change to it, marked there) to fill them at once.
#include "std/monocypher.h"
#include "base/thread.h"

#define AK_MC_ARGON2_LANES ak_mc_argon2_lanes
#include "../../third_party/monocypher/monocypher.c"

// A slice's lanes being filled: each thread takes the next lane until
// none are left; the caller fills them too and waits for the rest.
typedef struct {
	void (*fn)(void *, u32);
	void *ctx;
	u32 n, next, done;
	u32 started, left;  // threads started, and those that have left fill_lanes
	ak_mutex mu;
	ak_cond cv;
} lanes;

static bool take_lane(lanes *l, u32 *lane) {
	ak_mutex_lock(&l->mu);
	bool ok = l->next < l->n;
	if (ok) *lane = l->next++;
	ak_mutex_unlock(&l->mu);
	return ok;
}

static void fill_lanes(lanes *l) {
	u32 lane;
	while (take_lane(l, &lane)) {
		l->fn(l->ctx, lane);
		ak_mutex_lock(&l->mu);
		if (++l->done == l->n) ak_cond_broadcast(&l->cv);
		ak_mutex_unlock(&l->mu);
	}
}

static void lane_thread(void *arg) {
	lanes *l = arg;
	fill_lanes(l);
	ak_mutex_lock(&l->mu);
	l->left++;
	ak_cond_broadcast(&l->cv);
	ak_mutex_unlock(&l->mu);
}

// At most this many threads for a slice besides the caller's.
enum { LANE_THREADS = 7 };

static void run_lanes(void (*fn)(void *, u32), void *ctx, u32 n) {
	if (n == 1) {
		fn(ctx, 0);
		return;
	}
	lanes l = {.fn = fn, .ctx = ctx, .n = n};
	ak_mutex_init(&l.mu);
	ak_cond_init(&l.cv);
	// Threads that can't start leave their lanes to the ones that did.
	for (u32 i = 1; i < n && i <= LANE_THREADS; i++) {
		ak_mutex_lock(&l.mu);
		l.started++;
		ak_mutex_unlock(&l.mu);
		if (!ak_thread_start(lane_thread, &l)) {
			ak_mutex_lock(&l.mu);
			l.started--;
			ak_mutex_unlock(&l.mu);
			break;
		}
	}
	fill_lanes(&l);
	// Every lane done, and every thread out (l is this frame's).
	ak_mutex_lock(&l.mu);
	while (l.done < l.n || l.left < l.started) ak_cond_wait(&l.cv, &l.mu);
	ak_mutex_unlock(&l.mu);
	ak_cond_destroy(&l.cv);
	ak_mutex_destroy(&l.mu);
}

void (*ak_mc_argon2_lanes)(void (*fn)(void *, u32), void *ctx, u32 n) = run_lanes;
