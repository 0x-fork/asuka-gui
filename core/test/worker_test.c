// Workers (rt/worker.c): a thread each, its calls run in order on its
// state, its owner's disposal cancelling what's queued, and everything it
// held let go of: its state on its thread, the closures (which hold the
// VM's module) on the main thread. core_test.go runs this file under
// ASan and UBSan, and again under ThreadSanitizer (TestCThreads).
#include "t.h"
#include "rt/rt.h"
#include "rt/sched.h"
#include "rt/lib.h"
#include "rt/worker.h"
#include "signal/signal.h"
#include "base/thread.h"

#include <stdatomic.h>
#include <time.h>

static ak_lib_fn worker_new, worker_call;

static void find_lib(void) {
	for (const ak_lib_entry *e = ak_worker_lib; e->name; e++) {
		if (strcmp(e->name, "worker.new") == 0) worker_new = e->fn;
		if (strcmp(e->name, "worker.call") == 0) worker_call = e->fn;
	}
}

// What ran where: the threads the closures ran and were let go of on.
static _Atomic uint64_t main_id, init_ran_on, call_ran_on;
static atomic_int drops, drops_off_main;

static void dropped(ak_closure *c) {
	AK_UNUSED(c);
	atomic_fetch_add(&drops, 1);
	if (ak_thread_id() != atomic_load(&main_id)) atomic_fetch_add(&drops_off_main, 1);
}

// The state: a box of the ints its calls added.
static atomic_int init_throws;

static ak_val init_body(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(args), AK_UNUSED(n);
	atomic_store(&init_ran_on, ak_thread_id());
	if (atomic_load(&init_throws)) {
		ak_throw_msg("no state today");
		return (ak_val){0};
	}
	return (ak_val){.p = ak_box_new(AK_K_REF, (ak_val){.p = ak_arr_new(AK_K_INT, NULL, 0)})};
}

// A call that holds the thread until the test lets it go.
static ak_mutex gate_mu = AK_MUTEX_INIT;
static ak_cond gate_cond;
static bool gate_open, gate_waiting;

static void gate_wait(void) {
	ak_mutex_lock(&gate_mu);
	gate_waiting = true;
	ak_cond_broadcast(&gate_cond);
	while (!gate_open) ak_cond_wait(&gate_cond, &gate_mu);
	ak_mutex_unlock(&gate_mu);
}

static void gate_reset(void) {
	ak_mutex_lock(&gate_mu);
	gate_open = gate_waiting = false;
	ak_mutex_unlock(&gate_mu);
}

static void gate_until_waiting(void) {
	ak_mutex_lock(&gate_mu);
	while (!gate_waiting) ak_cond_wait(&gate_cond, &gate_mu);
	ak_mutex_unlock(&gate_mu);
}

static void gate_release(void) {
	ak_mutex_lock(&gate_mu);
	gate_open = true;
	ak_cond_broadcast(&gate_cond);
	ak_mutex_unlock(&gate_mu);
}

// add(x): x appended to the state, its count returned; x < 0 waits at
// the gate first; x == 0 throws.
static ak_val add_body(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	atomic_store(&call_ran_on, ak_thread_id());
	int64_t x = c->caps[0].i;
	if (x < 0) gate_wait();
	if (x == 0) {
		ak_throw_msg("zero");
		return (ak_val){0};
	}
	ak_box *b = args[0].p;
	ak_arr **a = (ak_arr **)&b->v.p;
	ak_arr_push(a, (ak_val){.i = x});
	return (ak_val){.i = (*a)->len};
}

static const uint8_t int_cap[] = {AK_K_INT};

static ak_val new_worker(void) {
	ak_closure *init = ak_closure_new(init_body, NULL, 0, NULL);
	init->drop = dropped;
	ak_val args[1] = {{.p = init}};
	ak_val w = worker_new(args, 1);
	init->drop = NULL;  // the caller's own: only the worker's copy counts
	ak_release(init);
	return w;
}

static ak_future *call(ak_val w, int64_t x) {
	ak_closure *c = ak_closure_new(add_body, NULL, 1, int_cap);
	c->caps[0].i = x;
	c->drop = dropped;
	ak_val args[3] = {w, {.p = c}, {.i = AK_K_INT}};
	ak_future *f = worker_call(args, 3).p;
	c->drop = NULL;
	ak_release(c);
	return f;
}

static void pump_until_settled(ak_future *f) {
	while (f->state == AK_FUT_PENDING) {
		ak_sched_wait_posted();
		ak_sched_run_posted();
	}
}

// pump_until_idle runs what the threads post until nothing holds the
// scheduler: every call delivered, every stopped worker's thread ended.
static void pump_until_idle(void) {
	while (ak_sched_held()) {
		ak_sched_wait_posted();
		ak_sched_run_posted();
	}
	ak_sched_run_posted();
}

static void setup(void) {
	if (!worker_new) find_lib();
	atomic_store(&main_id, ak_thread_id());
	atomic_store(&drops, 0);
	atomic_store(&drops_off_main, 0);
	atomic_store(&init_throws, 0);
	ak_cond_init(&gate_cond);
	gate_reset();
}

typedef struct {
	ak_val w;
} made;

static void make_in_owner(void *ctx) { ((made *)ctx)->w = new_worker(); }

TEST(calls_run_in_order_on_its_thread) {
	setup();
	int64_t live = ak_obj_live;
	ak_val w = new_worker();
	ak_future *f[5];
	for (int i = 0; i < 5; i++) f[i] = call(w, 10 + i);
	for (int i = 0; i < 5; i++) {
		pump_until_settled(f[i]);
		CHECK_EQ_INT(f[i]->state, AK_FUT_DONE);
		CHECK_EQ_INT(f[i]->result.i, i + 1);  // the state had the calls before it
	}
	CHECK(atomic_load(&init_ran_on) != atomic_load(&main_id));
	CHECK(atomic_load(&call_ran_on) == atomic_load(&init_ran_on));
	for (int i = 0; i < 5; i++) ak_release(f[i]);
	ak_release(w.p);
	pump_until_idle();
	CHECK_EQ_INT(ak_obj_live, live);
	CHECK_EQ_INT(atomic_load(&drops), 6);  // the init's copy and each call's
	CHECK_EQ_INT(atomic_load(&drops_off_main), 0);
}

TEST(an_error_crosses_back) {
	setup();
	int64_t live = ak_obj_live;
	ak_val w = new_worker();
	ak_future *a = call(w, 0), *b = call(w, 3);
	pump_until_settled(a);
	pump_until_settled(b);
	REQUIRE(a->state == AK_FUT_FAILED);
	CHECK_EQ_STR(ak_error_message(a->error)->data, "zero");
	CHECK_EQ_INT(b->result.i, 1);  // the worker goes on
	ak_release(a), ak_release(b), ak_release(w.p);
	pump_until_idle();
	CHECK_EQ_INT(ak_obj_live, live);
}

TEST(an_init_that_throws_fails_each_call) {
	setup();
	atomic_store(&init_throws, 1);
	int64_t live = ak_obj_live;
	ak_val w = new_worker();
	ak_future *a = call(w, 1), *b = call(w, 2);
	pump_until_settled(a);
	pump_until_settled(b);
	REQUIRE(a->state == AK_FUT_FAILED && b->state == AK_FUT_FAILED);
	CHECK_EQ_STR(ak_error_message(a->error)->data, "no state today");
	CHECK(a->error != b->error);
	ak_release(a), ak_release(b), ak_release(w.p);
	pump_until_idle();
	CHECK_EQ_INT(ak_obj_live, live);
}

// Disposing its owner: the call running finishes, those queued fail at
// once with the cancellation, and later calls too; the thread lets go of
// the state, and the closures (the module's holders) go on the main
// thread.
TEST(disposing_its_owner_cancels_the_queued_calls) {
	setup();
	int64_t live = ak_obj_live;
	ak_owner *o = ak_root_new();
	made m = {0};
	ak_with_owner(o, make_in_owner, &m);
	ak_future *running = call(m.w, -1);
	gate_until_waiting();
	ak_future *queued[3];
	for (int i = 0; i < 3; i++) queued[i] = call(m.w, i + 1);
	ak_owner_dispose(o);
	for (int i = 0; i < 3; i++) {
		REQUIRE(queued[i]->state == AK_FUT_FAILED);
		CHECK(ak_is_cancel(queued[i]->error));
	}
	CHECK(running->state == AK_FUT_PENDING);
	ak_future *after = call(m.w, 9);
	REQUIRE(after->state == AK_FUT_FAILED);
	CHECK(ak_is_cancel(after->error));
	gate_release();
	pump_until_settled(running);
	CHECK_EQ_INT(running->state, AK_FUT_DONE);
	CHECK_EQ_INT(running->result.i, 1);
	ak_release(running), ak_release(after), ak_release(m.w.p);
	for (int i = 0; i < 3; i++) ak_release(queued[i]);
	pump_until_idle();
	CHECK_EQ_INT(ak_obj_live, live);
	CHECK_EQ_INT(atomic_load(&drops), 5);  // the init's, the running call's, the 3 queued's (the one after never ran)
	CHECK_EQ_INT(atomic_load(&drops_off_main), 0);
}

// Let go of with no owner (a test's, a program's), it stops once the
// calls queued ran.
TEST(its_last_reference_lets_the_queue_run) {
	setup();
	int64_t live = ak_obj_live;
	ak_val w = new_worker();
	ak_future *first = call(w, -1);
	gate_until_waiting();
	ak_future *f[3];
	for (int i = 0; i < 3; i++) f[i] = call(w, i + 1);
	ak_release(w.p);
	gate_release();
	pump_until_settled(first);
	for (int i = 0; i < 3; i++) {
		pump_until_settled(f[i]);
		CHECK_EQ_INT(f[i]->state, AK_FUT_DONE);
		CHECK_EQ_INT(f[i]->result.i, i + 2);
	}
	ak_release(first);
	for (int i = 0; i < 3; i++) ak_release(f[i]);
	pump_until_idle();
	CHECK_EQ_INT(ak_obj_live, live);
	CHECK_EQ_INT(atomic_load(&drops_off_main), 0);
}

// Disposed after it was let go of (the owner outlives the handle), and
// disposed while its thread is still starting: each stops once.
TEST(stopping_twice) {
	setup();
	int64_t live = ak_obj_live;
	ak_owner *o = ak_root_new();
	made m = {0};
	ak_with_owner(o, make_in_owner, &m);
	ak_future *f = call(m.w, 4);
	ak_release(m.w.p);
	ak_owner_dispose(o);
	pump_until_idle();
	CHECK(f->state != AK_FUT_PENDING);
	if (f->state == AK_FUT_FAILED) CHECK(ak_is_cancel(f->error));
	ak_release(f);
	CHECK_EQ_INT(ak_obj_live, live);
	CHECK_EQ_INT(atomic_load(&drops_off_main), 0);
}

T_MAIN()
