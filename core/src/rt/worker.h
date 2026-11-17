// worker.h: workers (worker.c), and the hosts that run them elsewhere.
#ifndef AK_WORKER_H
#define AK_WORKER_H

#include "rt/lib.h"

// A host that runs workers elsewhere (the web's Web Workers: abi/web.c).
// Main thread.
typedef struct ak_worker_host {
	// start makes a worker: init, a closure whose captures are values,
	// makes its state there. *id names it from then on; false: it can't
	// (the worker then runs as it would without a host).
	bool (*start)(ak_closure *init, uint64_t *id);
	// call runs c (its captures values, its one argument the worker's
	// state) after the calls before it, and settles f (of kind k) with
	// its result: a call cancelled by stop fails with the cancellation.
	void (*call)(uint64_t id, ak_closure *c, ak_kind k, ak_future *f);
	// stop ends the worker once its queued calls ran, or, cancel, once
	// the one running did (those queued cancelled). Called again with
	// cancel, a stop that's waiting for its queue stops at once.
	void (*stop)(uint64_t id, bool cancel);
} ak_worker_host;

extern const ak_worker_host *ak_worker_elsewhere;

// worker.new and worker.call (CALLB).
extern const ak_lib_entry ak_worker_lib[];

#endif
