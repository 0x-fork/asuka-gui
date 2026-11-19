// stream.h: reading a Stream<T> from C (stream.c), as for await does.
#ifndef AK_STREAM_H
#define AK_STREAM_H

#include "rt/rt.h"
#include "rt/sched.h"

// ak_stream_next asks for the next value: a future of a bool, whether
// one came (false: the end); it fails with what the producer threw. NULL
// (and an error pending) when s is read already.
ak_future *ak_stream_next(ak_val s);
// ak_stream_ready reports whether a value is queued: next's answer
// would be true at once (for await's: stream.ready, core/src/vm/task.c).
bool ak_stream_ready(ak_val s);
// ak_stream_value takes the value next said came (owned).
ak_val ak_stream_value(ak_val s);
// ak_stream_close cancels the producer.
void ak_stream_close(ak_val s);

// Sending from C, many values at once (std/fs's lines, a chunk's). A
// producer's send is a closure; ak_stream_sender is the stream it sends
// to when it's a stream's send (NULL for any other closure, which is
// called a value; or when the stream went, or stopped: what it's sent
// then is let go of). ak_stream_put queues v (owned) without answering a
// waiting reader; ak_stream_wake answers it, once a batch is in.
typedef struct ak_stream ak_stream;
ak_stream *ak_stream_sender(ak_closure *send);
void ak_stream_put(ak_stream *s, ak_val v);
void ak_stream_wake(ak_stream *s);

#endif
