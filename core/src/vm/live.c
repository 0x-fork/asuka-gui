// live.c: live(initial, source), a value the platform keeps current
// (the network's state, the appearance, a permission's), which follows
// a stream only while something reads it.
//
//	live.new(initial: T, source: fn() -> Stream<T>, kind: int) -> fn() -> T
//
// The function it returns reads the value. Read in a binding, an effect
// or a memo, it's tracked: the first such reader starts source's stream
// (under an owner of its own, not the reader's), each value the stream
// sends updates what read it, and when the last reader goes the stream
// is cancelled, so nothing watches the platform for no one. Read
// elsewhere (a handler), it's the last value known, and starts nothing.
//
// Underneath, the value is a memo whose unwatched hook stops the stream.
#include "vm/vm_int.h"
#include "rt/lib.h"
#include "rt/stream.h"

typedef struct {
	ak_opaque h;
	uint8_t kind;
	bool running;
	bool wanted;   // read tracked since it was last unwatched
	bool queued;   // a start is queued
	bool ended;    // the stream ended: not started again until unwatched
	ak_val value;
	ak_node node;        // written when a value comes
	ak_memo *memo;       // owned (no owner: disposed with the live)
	ak_closure *source;
	ak_owner *owner;     // the stream's, while it runs
	ak_val stream;       // while it runs
	ak_future *waiting;  // its pending next
	ak_comp_info info;
} ak_live;

static void stop(ak_live *l);
static void ask(void *ctx);
static void on_value(ak_live *l, ak_future *f);

// on_next holds a reference to l (the wait's): a wait already queued as
// l stops runs later, and finds l stopped, or waiting for another next.
static void on_next(void *ctx) {
	ak_live *l = ctx;
	ak_future *f = l->waiting;
	if (!f || f->state == AK_FUT_PENDING) {
		ak_release(l);
		return;
	}
	l->waiting = NULL;
	on_value(l, f);
	ak_release(l);
}

static void on_value(ak_live *l, ak_future *f) {
	bool more = f->state == AK_FUT_DONE && f->result.i;
	ak_release(f);
	if (!more || !l->running) {
		// The end, or an error (the value stays what it was).
		stop(l);
		l->ended = true;
		return;
	}
	ak_val v = ak_stream_value(l->stream);
	if (ak_val_eq(v, l->value, (ak_kind)l->kind)) {
		ak_release_val(v, (ak_kind)l->kind);
	} else {
		ak_release_val(l->value, (ak_kind)l->kind);
		l->value = v;
		ak_signal_write(&l->node);
	}
	ak_with_owner(l->owner, ask, l);
}

static void ask(void *ctx) {
	ak_live *l = ctx;
	ak_future *f = ak_stream_next(l->stream);
	if (!f) {
		ak_release(ak_error_take());
		return;
	}
	l->waiting = f;
	ak_future_wait(f, on_next, ak_retain(l));
}

static void start_in_owner(void *ctx) {
	ak_live *l = ctx;
	void *tok = ak_untrack_begin();
	ak_val s = ak_call(l->source, NULL, 0);
	ak_untrack_end(tok);
	if (ak_error_pending()) {
		ak_release(ak_error_take());
		return;
	}
	l->stream = s;
	l->running = true;
	ask(l);
}

static void start(ak_live *l) {
	l->owner = ak_root_new();
	ak_with_owner(l->owner, start_in_owner, l);
	if (!l->running) {
		ak_owner_dispose(l->owner);
		l->owner = NULL;
	}
}

// stop lets the wait's reference to l go last: it may be the last one
// (the memo that made l ran again), and l is used until then.
static void stop(ak_live *l) {
	bool removed = false;
	if (l->waiting) {
		ak_future *w = l->waiting;
		l->waiting = NULL;
		removed = ak_future_unwait(w, on_next, l);
		ak_release(w);
	}
	if (l->stream.p) {
		ak_stream_close(l->stream);
		ak_release(l->stream.p);
		l->stream.p = NULL;
	}
	if (l->owner) {
		ak_owner_dispose(l->owner);
		l->owner = NULL;
	}
	l->running = false;
	if (removed) ak_release(l);
}

// started starts the stream, from the ready queue: not in the memo's
// update, where the producer's first steps (which may write state)
// can't run.
static void started(void *ctx) {
	ak_live *l = ctx;
	l->queued = false;
	if (l->wanted && !l->running) start(l);
	ak_release(l);
}

static bool update(void *ctx) {
	ak_live *l = ctx;
	ak_signal_read(&l->node);
	l->wanted = true;
	if (!l->running && !l->queued && !l->ended) {
		l->queued = true;
		ak_ready(started, ak_retain(l));
	}
	return true;
}

static void unwatched(void *ctx) {
	ak_live *l = ctx;
	l->wanted = false;
	l->ended = false;
	stop(l);
}

static void live_drop(ak_opaque *o) {
	ak_live *l = (ak_live *)o;
	stop(l);
	if (l->memo) ak_owner_dispose(ak_memo_owner(l->memo));
	ak_signal_drop(&l->node);
	ak_release_val(l->value, (ak_kind)l->kind);
	ak_release(l->source);
}

static void make_memo(void *ctx) {
	ak_live *l = ctx;
	l->memo = ak_memo_new(update, l, &l->info);
}

static ak_val live_invoke(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	ak_live *l = c->caps[0].p;
	if (ak_tracking() && !ak_memo_read(l->memo)) return (ak_val){0};
	ak_retain_val(l->value, (ak_kind)l->kind);
	return l->value;
}

static const uint8_t live_caps[] = {AK_K_REF};

static ak_val live_new(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_live *l = ak_opaque_new(AK_TAG_OPAQUE, sizeof *l, live_drop);
	l->kind = (uint8_t)args[2].i;
	l->value = args[0];
	ak_retain_val(l->value, (ak_kind)l->kind);
	l->source = ak_retain(args[1].p);
	l->info = (ak_comp_info){.unwatched = unwatched, .label = "live"};
	// The memo is no reader's: made with no owner, it lives as long as
	// the live does.
	ak_with_owner(NULL, make_memo, l);
	ak_closure *f = ak_closure_new(live_invoke, NULL, 1, live_caps);
	f->caps[0].p = l;  // the closure's reference
	return (ak_val){.p = f};
}

const ak_lib_entry ak_live_lib = {"live.new", live_new};
