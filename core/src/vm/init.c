#include "vm/vm_int.h"
#include "rt/lib.h"
#include "std/std.h"
#include "vm/ffi.h"

#if defined(AK_FFI)
// The glue lang/cffi generates for the app's extern declarations (RFC
// 0008): named here, so that a static library's linker keeps it.
void asuka_ffi_register(void);
#endif

extern const ak_lib_entry ak_lib_spawn;
extern const ak_lib_entry ak_live_lib;

void ak_vm_builtins_init(void) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++) ak_builtin_register(e->name, e->fn);
	for (const ak_lib_entry *e = ak_stream_lib; e->name; e++) ak_builtin_register(e->name, e->fn);
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++) ak_builtin_register(e->name, e->fn);
	ak_builtin_register(ak_lib_spawn.name, ak_lib_spawn.fn);
	ak_builtin_register(ak_live_lib.name, ak_live_lib.fn);
#if defined(AK_FFI)
	asuka_ffi_register();
#endif
}

// A closure spawn copied for a worker holds the module too.
void ak_closure_copied(ak_closure *from, ak_closure *to) {
	AK_UNUSED(to);
	if (from->invoke == ak_vm_closure_invoke) ak_module_retain(((const ak_func *)from->fn)->mod);
}

void ak_vm_init(void) {
	static bool done;
	if (done) return;
	done = true;
	ak_ffi_init();
	ak_vm_builtins_init();
	ak_vm_json_init();
	extern void ak_native_vm_init(void);
	ak_native_vm_init();
	extern void ak_sqlite_init(void);
	ak_sqlite_init();
	extern void ak_std_host_init(void);
	ak_std_host_init();
	ak_vm_react_init();
}
