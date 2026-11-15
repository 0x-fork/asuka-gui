// stream.h: reading a Stream<T> from C (stream.c), as for await does.
#ifndef AK_STREAM_H
#define AK_STREAM_H

#include "rt/rt.h"
#include "rt/sched.h"

// ak_stream_next asks for the next value: a future of a bool, whether
// one came (false: the end); it fails with what the producer threw. NULL
// (and an error pending) when s is read already.
ak_future *ak_stream_next(ak_val s);
// ak_stream_value takes the value next said came (owned).
ak_val ak_stream_value(ak_val s);
// ak_stream_close cancels the producer.
void ak_stream_close(ak_val s);

#endif
