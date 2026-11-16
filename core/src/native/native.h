// native.h: native packages' runtime (docs/native.md): objects a
// platform's side makes, calls into them, and callbacks out of them,
// over the host's callbacks (asuka.h's native_*), in the wire format of
// native/codec.go (little-endian; values in declaration order).
//
// Types come as descriptors, rt/json.h's plus:
//   o           a native object (a u64 handle; 0: none)
//   F<n><T...>R a function the native side calls back: n (one digit)
//               parameter types, then the result's (v: none)
//   v           nothing (a result)
// int is i64, float f64, Duration i64 nanoseconds, a struct its fields
// in order, an optional a u8 (0: none) then the value, an array or a map
// a u32 count then its items.
#ifndef AK_NATIVE_H
#define AK_NATIVE_H

#include "asuka.h"
#include "rt/rt.h"
#include "rt/json.h"
#include "rt/sched.h"

void ak_native_set_host(const asuka_host_callbacks *cb);

bool ak_native_encode(ak_buf *b, ak_val v, const char *desc, const ak_json_types *t);
// ak_native_decode reads a value; it throws on a short or bad answer.
ak_val ak_native_decode(ak_reader *r, const char *desc, const ak_json_types *t);

// Objects: an rt object (AK_TAG_NATIVE) holding a handle, let go on the
// platform's side when its last reference goes.
typedef struct ak_native_obj {
	ak_opaque h;
	uint64_t handle;
} ak_native_obj;

// ak_native_wrap wraps a handle the platform's side made.
ak_native_obj *ak_native_wrap(uint64_t handle);

// ak_native_new makes an object of class cls's interface iface; errors
// are the package's declared errors' messages, by index.
ak_val ak_native_new(const char *cls, size_t cls_len, uint32_t iface, ak_arr *errors);
// ak_native_call calls method with args (a tuple of the argument types
// in args_desc) and decodes the result as ret_desc.
ak_val ak_native_call(ak_native_obj *o, uint32_t method, ak_val args, const char *args_desc, const char *ret_desc, ak_arr *errors, const ak_json_types *t);
// ak_native_start is the same for a method that runs asynchronously: a
// future of its result; cancelling the future cancels the call.
ak_future *ak_native_start(ak_native_obj *o, uint32_t method, ak_val args, const char *args_desc, const char *ret_desc, ak_arr *errors, const ak_json_types *t);

// From the host (asuka.h): a started call's answer, and a callback's
// call (its answer into *out, ak_alloc'd, which the caller frees with
// asuka's free; none: 0).
void ak_native_done(uint64_t call, const uint8_t *p, size_t len);
size_t ak_native_invoke(uint64_t id, const uint8_t *p, size_t len, uint8_t **out);

// ak_native_desc_skip is past the descriptor's first type;
// ak_native_desc_kind is that type's kind.
const char *ak_native_desc_skip(const char *d);
ak_kind ak_native_desc_kind(const char *d);

void ak_native_vm_init(void);

#endif
