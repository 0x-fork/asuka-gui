#include "vm/vm_int.h"
#include "rt/lib.h"

extern const ak_lib_entry ak_lib_spawn;

void ak_vm_builtins_init(void) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++) ak_builtin_register(e->name, e->fn);
	ak_builtin_register(ak_lib_spawn.name, ak_lib_spawn.fn);
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
	ak_vm_builtins_init();
	ak_vm_json_init();
	ak_vm_react_init();
}
