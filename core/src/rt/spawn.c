// spawn: a closure run on a worker thread. Its captures are copied in
// (values only: the checker keeps state, stores and views out), its
// result or error is copied back, and the main thread resolves the
// future. A cancelled spawn runs to its end; its result is dropped.
//
// Where the core has no threads (the web), a host may run the work
// elsewhere (ak_spawn_elsewhere: the page's Web Workers, abi/web.c);
// without one it runs in place.
#include "rt/sched.h"
#include "rt/lib.h"
#include "base/thread.h"

typedef struct job {
	// The copy the worker runs, let go on the main thread as the result
	// is delivered: it holds the module (the VM's code), whose constants,
	// shapes and payloadless cases the result may hold too, so the module
	// is freed only on the main thread and only once that result is
	// delivered (and, if nobody waits for it, gone).
	ak_closure *c;
	ak_future *fut;  // retained; touched on the main thread only
	ak_kind kind;
	ak_val result;
	void *err;
	struct job *next;
} job;

static ak_mutex mu = AK_MUTEX_INIT;
static ak_cond cond;
static job *head, *tail;
static int workers, idle;

static void deliver(void *p) {
	job *j = p;
	if (j->err) ak_future_reject(j->fut, j->err);
	else ak_future_resolve(j->fut, j->result);
	ak_release(j->fut);
	ak_release(j->c);
	ak_free(j);
	ak_sched_unhold();
}

static void worker(void *arg) {
	AK_UNUSED(arg);
	for (;;) {
		ak_mutex_lock(&mu);
		while (!head) {
			idle++;
			ak_cond_wait(&cond, &mu);
			idle--;
		}
		job *j = head;
		head = j->next;
		if (!head) tail = NULL;
		ak_mutex_unlock(&mu);

		ak_val v = ak_call(j->c, NULL, 0);
		if (ak_error_pending()) {
			void *e = ak_error_take();
			j->err = ak_deep_copy((ak_val){.p = e}, AK_K_REF).p;
			ak_release(e);
		} else {
			j->result = ak_deep_copy(v, j->kind);
			ak_release_val(v, j->kind);
		}
		ak_post(deliver, j);
	}
}

static ak_closure *copy_closure(ak_closure *c) {
	ak_closure *n = ak_closure_new(c->invoke, c->fn, c->ncap, c->capkinds);
	for (uint16_t i = 0; i < c->ncap; i++) n->caps[i] = ak_deep_copy(c->caps[i], c->capkinds ? (ak_kind)c->capkinds[i] : AK_K_INT);
	n->drop = c->drop;
	if (c->drop == NULL) return n;
	// The copy holds what the original holds (the VM: its module).
	extern void ak_closure_copied(ak_closure *from, ak_closure *to);
	ak_closure_copied(c, n);
	return n;
}

bool (*ak_spawn_elsewhere)(ak_closure *c, ak_kind k, ak_future *f);

// async.spawn(closure, kind): a future of the closure's result, of kind.
static ak_val spawn_fn(ak_val *args, uint32_t nargs) {
	ak_closure *c = args[0].p;
	ak_kind k = nargs > 1 ? (ak_kind)args[1].i : AK_K_VOID;
	ak_future *f = ak_future_new(k);
	if (ak_spawn_elsewhere && ak_spawn_elsewhere(c, k, f)) return (ak_val){.p = f};
	job *j = ak_alloc(sizeof *j);
	j->c = copy_closure(c);
	j->fut = ak_retain(f);
	j->kind = k;
	ak_mutex_lock(&mu);
	if (!workers) ak_cond_init(&cond);
	bool started = true;
	if (idle == 0 && workers < 4) {
		started = ak_thread_start(worker, NULL);
		if (started) workers++;
	}
	if (!workers) {
		ak_mutex_unlock(&mu);
		// No threads (the web): run it here, now.
		ak_val v = ak_call(j->c, NULL, 0);
		ak_release(j->c);
		if (ak_error_pending()) ak_future_reject(f, ak_error_take());
		else ak_future_resolve(f, v);
		ak_release(j->fut);
		ak_free(j);
		return (ak_val){.p = f};
	}
	ak_sched_hold();  // until deliver runs
	if (tail) tail->next = j;
	else head = j;
	tail = j;
	ak_cond_signal(&cond);
	ak_mutex_unlock(&mu);
	return (ak_val){.p = f};
}

const ak_lib_entry ak_lib_spawn = {"async.spawn", spawn_fn};
