// signal_impl.h: the graph's internals, shared by the files of
// core/src/signal.
#ifndef AK_SIGNAL_IMPL_H
#define AK_SIGNAL_IMPL_H

#include "signal/signal.h"

// A node's flags.
enum {
	AK_F_MEMO = 1u << 0,     // a memo: has deps and subs
	AK_F_EFFECT = 1u << 1,   // an effect: has deps, no subs
	AK_F_RENDER = 1u << 2,   // an effect the UI runtime makes for props; runs first
	AK_F_DIRTY = 1u << 3,    // a dep changed: must recompute
	AK_F_PENDING = 1u << 4,  // a dep might have changed: check deps first
	AK_F_QUEUED = 1u << 5,   // an effect in the queue
	AK_F_DISPOSED = 1u << 6, // disposed (or dropped); never runs again
};

// ak_link is an edge from dep to sub. It sits in two doubly linked
// lists: dep's subscribers and sub's dependencies. Links are reused
// across runs when a computation reads the same deps in the same order.
struct ak_link {
	ak_node *dep;
	ak_comp *sub;
	ak_link *prev_sub, *next_sub;
	ak_link *prev_dep, *next_dep;
	uint32_t version; // sub->run when this link was last confirmed
};

typedef struct {
	void (*fn)(void *ctx);
	void *ctx;
} ak_cleanup;

typedef struct ak_ctx_entry {
	const void *key;
	void *val;
	struct ak_ctx_entry *next;
} ak_ctx_entry;

struct ak_owner {
	ak_owner *parent;       // for contexts and catchers; disposes us if attached
	ak_owner *first, *last; // attached children
	ak_owner *prev, *next;  // siblings in the parent's list
	// catcher is the nearest owner, this one or above, that catches
	// errors; NULL for most.
	ak_owner *catcher;
	ak_error_fn on_error;
	void *error_ctx;
	ak_comp *comp; // the computation this owner is part of, if any
	ak_ctx_entry *ctx;
	AK_VEC(ak_cleanup) cleanups;
	uint32_t refs;
	bool attached, disposed;
};

struct ak_comp {
	ak_node node; // first: a memo's side as a dependency
	ak_link *deps, *deps_tail;
	uint32_t run; // increments each time it starts tracking
	uint64_t seq; // creation order, for effects
	union {
		void (*run)(void *ctx);
		bool (*update)(void *ctx);
	} fn;
	void *ctx;
	const ak_comp_info *info;
	void *err; // a memo's last error
	ak_owner owner;
};

// ak_frame is a run that holds the error raised in it until it returns.
typedef struct ak_frame {
	struct ak_frame *prev;
	void *err;
	bool failed;
} ak_frame;

typedef AK_VEC(ak_comp *) ak_comp_vec;

// ak_graph is all of the graph's state.
typedef struct ak_graph {
	ak_comp *active; // the computation tracking reads, NULL when untracked
	ak_owner *owner; // the current owner
	ak_frame *frame; // the innermost run
	int in_memo;     // >0 while a memo computes
	int batch_depth;
	bool flushing;
	ak_link *free_links; // chained through next_dep
	uint64_t seq;
	ak_comp_vec queue, running;
	void (*after_flush)(void *ctx);
	void *after_flush_ctx;
	ak_error_fn on_error;
	void *on_error_ctx;
	void (*err_retain)(void *err);
	void (*err_release)(void *err);
	void (*panic_hook)(void *ctx, const char *msg);
	void *panic_ctx;
	uintptr_t main_thread;
	bool thread_bound;
} ak_graph;

extern ak_graph *ak_sig;

static inline void *ak_sig_err_retain(void *err) {
	if (err && ak_sig->err_retain) ak_sig->err_retain(err);
	return err;
}

static inline void ak_sig_err_release(void *err) {
	if (err && ak_sig->err_release) ak_sig->err_release(err);
}

// graph.c
void ak_sig_track(ak_node *dep, ak_comp *sub);
void ak_sig_start_tracking(ak_comp *c);
void ak_sig_end_tracking(ak_comp *c);
void ak_sig_unlink_all_deps(ak_comp *c);
void ak_sig_unlink_all_subs(ak_node *n);
void ak_sig_propagate(ak_link *l, uint32_t flag);
void ak_sig_shallow_propagate(ak_link *l);
bool ak_sig_check_dirty(ak_comp *c);

// signal.c
bool ak_sig_memo_update(ak_comp *m);
void ak_sig_memo_unwatched(ak_comp *m);
void ak_sig_enqueue(ak_comp *e);
void ak_sig_flush(void);
const char *ak_sig_describe(const ak_comp *c, char *buf, size_t n);
void ak_sig_panic(const char *fmt, ...) AK_PRINTF(1, 2);

// owner.c
void ak_sig_owner_attach(ak_owner *o, ak_owner *parent);
void ak_sig_owner_clean(ak_owner *o);
void ak_sig_handle_error(ak_owner *o, void *err);

// thread.c
bool ak_sig_check_thread(void);

#endif
