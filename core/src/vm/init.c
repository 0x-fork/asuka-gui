#include "vm/vm_int.h"
#include "rt/lib.h"

void ak_vm_builtins_init(void) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++) ak_builtin_register(e->name, e->fn);
}

void ak_vm_init(void) {
	static bool done;
	if (done) return;
	done = true;
	ak_vm_builtins_init();
	ak_vm_react_init();
}
