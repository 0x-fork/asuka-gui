#include "vm/vm_int.h"

// Tasks: an async function on a coroutine of its own. A task runs at
// once, until it finishes or awaits something not done yet; a future's
// completion resumes it from the ready queue. Its future holds its
// result. Cancelling the future (its owner went away) resumes it with a
// cancellation, which unwinds it through its handlers and defers.

static void task_cancel(ak_future *f);

static void finish(ak_task *t, ak_val out, bool failed) {
	ak_future *f = t->fut;
	t->fut = NULL;
	f->cancel = NULL;
	f->ctx = NULL;
	void *err = failed ? ak_error_take() : NULL;
	// An error nobody can see (no one else holds the future, so no one
	// can await it) goes to the catcher of the task's owner, unless the
	// task was cancelled: its owner went, and what it threw unwinding (a
	// defer's) is no one's to report.
	void *unseen = err && !ak_is_cancel(err) && !t->cancelled && f->h.h.rc == 1 && !f->waiters ? ak_retain(err) : NULL;
	AK_ASSERT(t->co.nframes == 0);
	// Settle the future before the owner goes: disposing it cancels what
	// is still pending.
	if (failed) ak_future_reject(f, err);
	else ak_future_resolve(f, out);
	ak_vm_task_done(t, unseen);
	ak_module_release(t->mod);
	ak_free(t->co.regs);
	ak_free(t->co.frames);
	ak_free(t);
	ak_release(f);  // the task's reference
}

static void step(ak_task *t) {
	ak_val out;
	t->running = true;
	bool suspended = ak_vm_task_run(t, &out);
	t->running = false;
	if (!suspended) finish(t, out, ak_error_pending());
}

ak_future *ak_vm_task_start(const ak_func *f, ak_closure *clos, ak_val *args, uint32_t nargs) {
	ak_future *fut = ak_future_new((ak_kind)f->ret);
	ak_task *t = ak_alloc(sizeof *t);
	t->fut = ak_retain(fut);
	t->co.task = t;
	t->mod = f->mod;
	ak_module_retain(f->mod);  // its code runs on until it finishes
	fut->cancel = task_cancel;
	fut->ctx = t;
	ak_vm_task_own(t);
	if (!ak_vm_push(&t->co, f, clos, args, nargs, 0xFFFF, true, 0)) finish(t, (ak_val){0}, true);
	else step(t);
	return fut;
}

void ak_vm_task_resume(void *p) {
	ak_task *t = p;
	ak_future *f = t->awaiting;
	t->awaiting = NULL;
	ak_release(f);
	step(t);
}

static void task_cancel(ak_future *f) {
	ak_task *t = f->ctx;
	if (!t || t->cancelled) return;
	t->cancelled = true;
	if (t->running || !t->awaiting) return;  // its next await throws
	// Stop waiting: resume it now, and its AWAIT throws the cancellation.
	ak_future *w = t->awaiting;
	// Settled already: its resume is on the ready queue, and the task's
	// next await throws.
	if (!ak_future_unwait(w, ak_vm_task_resume, t)) return;
	// Nobody else can see what it waited for (the task's register and
	// t->awaiting hold the only references): stop that work too.
	if (w->h.h.rc <= 2u + w->held) ak_future_cancel(w);
	// It unwinds through its defers here, not later from the ready queue:
	// its owner going cancels it, and what the owner's cleanups let go
	// after (the component's memos, which a defer may read) is still
	// there. An error already unwinding (the canceller's) waits for it.
	void *pending = ak_error_pending() ? ak_error_take() : NULL;
	ak_vm_task_resume(t);
	if (pending) ak_throw(pending);
}
