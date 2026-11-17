// stream.c: Stream<T>, values that come over time
// (docs/language/async.md). A stream is made from a producer, an async closure given a
// send function; it starts when the stream is first read, under the
// owner reading it, and sends values into the stream's queue. A reader
// (for await) asks for the next value, waiting when none is queued.
// The stream ends when the producer returns, fails when it throws, and
// closing the stream (the loop ending, the reader going) cancels it.
//
//	stream.new(produce: async fn(send: fn(T)) throws, kind: int) -> Stream
//	stream.next(s) -> Task<bool>   whether a value came (false: the end);
//	                               it throws what the producer threw
//	stream.value(s) -> T           the value next said came
//	stream.close(s)                cancels the producer
//
// The send function holds the stream through a link the stream clears
// when it goes, not a reference: the producer's task holds send, and the
// stream holds the task, which would be a cycle.
#include "rt/lib.h"
#include "rt/sched.h"
#include "rt/stream.h"

enum { IDLE, RUNNING, ENDED, FAILED, CLOSED };

typedef struct ak_stream ak_stream;

typedef struct {
	ak_opaque h;
	ak_stream *s;  // borrowed; NULL once the stream went
} stream_link;

struct ak_stream {
	ak_opaque h;
	uint8_t kind;
	uint8_t state;
	ak_closure *produce;  // until it starts
	ak_future *task;      // the producer's, while it runs
	ak_future *waiting;   // a next that waits for a value
	void *error;          // what the producer threw, until a next takes it
	stream_link *link;
	ak_val *q;            // the queue: a ring of values
	uint32_t head, len, cap;
};

static void link_drop(ak_opaque *o) { AK_UNUSED(o); }

static void producer_done(void *ctx);

// The producer's task is waited for through the link (retained by the
// wait): a wait already queued as the stream stops, or goes, runs later,
// and finds the stream gone, or the task not its own any more.
static void stop(ak_stream *s) {
	if (s->task) {
		ak_future *t = s->task;
		s->task = NULL;
		if (ak_future_unwait(t, producer_done, s->link)) ak_release(s->link);
		ak_future_cancel(t);
		ak_release(t);
	}
}

static void clear_queue(ak_stream *s) {
	for (uint32_t i = 0; i < s->len; i++) ak_release_val(s->q[(s->head + i) % s->cap], (ak_kind)s->kind);
	s->head = s->len = 0;
}

static void stream_drop(ak_opaque *o) {
	ak_stream *s = (ak_stream *)o;
	stop(s);
	if (s->link) {
		s->link->s = NULL;
		ak_release(s->link);
	}
	if (s->waiting) {
		ak_future_cancel(s->waiting);
		ak_release(s->waiting);
	}
	clear_queue(s);
	ak_free(s->q);
	if (s->produce) ak_release(s->produce);
	if (s->error) ak_release(s->error);
}

// wake answers a waiting next: a value came (true), or the end (false),
// or the producer's error.
static void wake(ak_stream *s) {
	ak_future *w = s->waiting;
	if (!w) return;
	if (s->len > 0) {
		s->waiting = NULL;
		ak_future_resolve(w, (ak_val){.i = 1});
	} else if (s->state == FAILED && s->error) {
		s->waiting = NULL;
		void *err = s->error;
		s->error = NULL;
		s->state = ENDED;
		ak_future_reject(w, err);
	} else if (s->state != RUNNING) {
		s->waiting = NULL;
		ak_future_resolve(w, (ak_val){.i = 0});
	} else {
		return;
	}
	ak_release(w);
}

static void producer_done(void *ctx) {
	stream_link *l = ctx;
	ak_stream *s = l->s;
	ak_release(l);
	if (!s || !s->task) return;  // gone, or closed: stop let the task go
	ak_future *t = s->task;
	s->task = NULL;
	if (t->state == AK_FUT_FAILED && !ak_is_cancel(t->error)) {
		s->state = FAILED;
		s->error = ak_retain(t->error);
	} else if (s->state == RUNNING) {
		s->state = ENDED;
	}
	ak_release(t);
	wake(s);
}

static ak_val send_invoke(ak_closure *c, ak_val *args, uint32_t n) {
	stream_link *l = c->caps[0].p;
	ak_stream *s = l->s;
	if (!s || s->state != RUNNING || n < 1) return (ak_val){0};
	if (s->len == s->cap) {
		uint32_t cap = s->cap ? s->cap * 2 : 8;
		ak_val *q = ak_alloc(sizeof *q * cap);
		for (uint32_t i = 0; i < s->len; i++) q[i] = s->q[(s->head + i) % s->cap];
		ak_free(s->q);
		s->q = q, s->cap = cap, s->head = 0;
	}
	ak_retain_val(args[0], (ak_kind)s->kind);
	s->q[(s->head + s->len) % s->cap] = args[0];
	s->len++;
	wake(s);
	return (ak_val){0};
}

static const uint8_t link_kinds[] = {AK_K_REF};

static void start(ak_stream *s) {
	ak_closure *p = s->produce;
	s->produce = NULL;
	s->state = RUNNING;
	ak_closure *send = ak_closure_new(send_invoke, NULL, 1, link_kinds);
	send->caps[0].p = ak_retain(s->link);
	ak_val arg = {.p = send};
	ak_val r = ak_call(p, &arg, 1);
	ak_release(send);
	ak_release(p);
	if (ak_error_pending()) {
		void *err = ak_error_take();
		if (ak_is_cancel(err)) {
			ak_release(err);
			s->state = ENDED;
		} else {
			s->state = FAILED;
			s->error = err;
		}
		return;
	}
	ak_obj *o = r.p;
	if (o && o->tag == AK_TAG_TASK) {
		s->task = r.p;
		ak_future_wait(s->task, producer_done, ak_retain(s->link));
	} else {
		// A producer that didn't wait has sent all it sends.
		if (o) ak_release(o);
		s->state = ENDED;
	}
}

static ak_stream *stream_arg(ak_val v, const char *name) {
	ak_opaque *o = v.p;
	if (!o || o->h.tag != AK_TAG_OPAQUE || o->drop != stream_drop) ak_panic("%s: not a stream", name);
	return (ak_stream *)o;
}

static ak_val stream_new(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_stream *s = ak_opaque_new(AK_TAG_OPAQUE, sizeof *s, stream_drop);
	s->produce = ak_retain(args[0].p);
	s->kind = (uint8_t)args[1].i;
	s->link = ak_opaque_new(AK_TAG_OPAQUE, sizeof *s->link, link_drop);
	s->link->s = s;
	return (ak_val){.p = s};
}

static ak_val stream_next(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_stream *s = stream_arg(args[0], "stream.next");
	if (s->waiting) {
		ak_throw_msg("a stream has one reader: it's read already");
		return (ak_val){0};
	}
	if (s->state == IDLE) start(s);
	ak_future *f = ak_future_new(AK_K_BOOL);
	s->waiting = ak_retain(f);
	wake(s);
	return (ak_val){.p = f};
}

static ak_val stream_value(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_stream *s = stream_arg(args[0], "stream.value");
	if (s->len == 0) ak_panic("stream.value: nothing came");
	ak_val v = s->q[s->head];
	s->head = (s->head + 1) % s->cap;
	s->len--;
	return v;  // the queue's reference, now the caller's
}

static ak_val stream_close(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_stream *s = stream_arg(args[0], "stream.close");
	stop(s);
	clear_queue(s);
	if (s->produce) ak_release(s->produce), s->produce = NULL;
	s->state = CLOSED;
	return (ak_val){0};
}

ak_future *ak_stream_next(ak_val s) {
	ak_val v = stream_next(&s, 1);
	return v.p;
}

ak_val ak_stream_value(ak_val s) { return stream_value(&s, 1); }

void ak_stream_close(ak_val s) { stream_close(&s, 1); }

const ak_lib_entry ak_stream_lib[] = {
	{"stream.new", stream_new},
	{"stream.next", stream_next},
	{"stream.value", stream_value},
	{"stream.close", stream_close},
	{NULL, NULL},
};
