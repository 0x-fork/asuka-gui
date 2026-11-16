// asuka_ffi.h: what C that Asuka calls through extern declarations may
// use of the runtime (RFC 0008, docs/c.md). The build puts this
// directory on the include path of every [c] source.
#ifndef ASUKA_FFI_H
#define ASUKA_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

// An Asuka function passed to C is two parameters: a function pointer,
// and its context, the first argument C calls it with. The context is
// valid during the extern call that passed it. C that keeps the
// callback to call later retains the context before that call returns,
// and releases it once it won't call the callback again (any thread);
// the Asuka function lives until then.
//
// A call during the extern call, on the thread that made it, runs at
// once and returns the function's result. Any other call (later, or
// from another thread) is posted to the main thread, under the owner
// that made the callback (a component's: none once it's gone), and
// returns zero at once.
void asuka_callback_retain(void *ctx);
void asuka_callback_release(void *ctx);

#ifdef __cplusplus
}
#endif

#endif
