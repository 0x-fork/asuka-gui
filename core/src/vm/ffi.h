// ffi.h: C from Asuka (RFC 0008). What the glue lang/cffi generates for
// an app's extern declarations calls: the extern types' objects, the
// conversions that need the runtime, and callbacks (an Asuka closure
// passed as a C function pointer and its context).
//
// The glue's wrappers are builtins (vm.h's calling convention); its
// asuka_ffi_register registers them, and ak_vm_builtins_init calls it in
// a core built with the glue (AK_FFI), naming it so that a static
// library's linker keeps it.
#ifndef AK_FFI_H
#define AK_FFI_H

#include "rt/rt.h"
#include "../../include/asuka_ffi.h"

// The C function a wrapper calls, by its symbol (not its declaration,
// which may clash with a header the glue includes: free, strlen):
// extern int32_t f(void) __asm__(AK_FFI_SYM("f")).
#define AK_FFI_STR2(x) #x
#define AK_FFI_STR(x) AK_FFI_STR2(x)
#define AK_FFI_SYM(name) AK_FFI_STR(__USER_LABEL_PREFIX__) name

// ak_ffi_init notes the main thread. ak_vm_init calls it.
void ak_ffi_init(void);

// ---- Extern types ----

// An extern type's value: the pointer C gave, freed by release (NULL:
// never) as the last reference goes, or at once when the app calls the
// release function itself.
typedef struct ak_ffi_obj {
	ak_opaque h;
	void *ptr;
	void (*release)(void *ptr);
	const char *type;
} ak_ffi_obj;

// ak_ffi_obj_new holds ptr. A NULL ptr is none when opt; else it throws
// that fn returned NULL.
ak_val ak_ffi_obj_new(void *ptr, void (*release)(void *), const char *type, bool opt, const char *fn);
// ak_ffi_ptr is the pointer an extern type's value holds (NULL for
// none); it throws when the value was released.
void *ak_ffi_ptr(ak_val v, const char *fn);
// ak_ffi_released: the release function ran on v's pointer (the app
// called it): v holds nothing now.
void ak_ffi_released(ak_val v);

// ak_ffi_str copies a C string C returned. NULL is none when opt; else
// it throws that fn returned NULL.
ak_val ak_ffi_str(const char *s, bool opt, const char *fn);
// ak_ffi_str_arg copies a C string C passed a callback: NULL is none
// when opt, else "".
ak_val ak_ffi_str_arg(const char *s, bool opt);

// ---- Calls ----

// The glue brackets each C call with these: a callback runs at once
// only on a thread inside an extern call.
void ak_ffi_enter(void);
void ak_ffi_leave(void);

// ---- Callbacks ----

typedef struct ak_ffi_cb ak_ffi_cb;

// ak_ffi_cb_new makes the context of a closure passed to C (NULL for
// none); ak_ffi_cb_done drops the call's reference once C returned.
ak_ffi_cb *ak_ffi_cb_new(ak_val fn);
void ak_ffi_cb_done(ak_ffi_cb *cb);
// ak_ffi_cb_call is a trampoline's: it calls the closure with args
// (owned, their kinds in kinds) and returns its result (owned, of kind
// rk), or posts the call to the main thread and returns zero (see
// asuka_ffi.h). After an error the closure raised during the extern call,
// the next calls return zero without running: the wrapper throws it once
// C returns.
ak_val ak_ffi_cb_call(void *ctx, ak_val *args, const uint8_t *kinds, uint32_t n, ak_kind rk);

#endif
