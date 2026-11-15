// vm.h: the bytecode VM. It loads a module the compiler wrote
// (lang/bytecode) and runs its functions on the main thread.
//
// Calls. ak_vm_call runs a function to completion and returns its result
// (owned); an error it throws is left pending (ak_error_pending). An
// async function called from C runs on a coroutine of its own: if it
// suspends (an await on something not done yet), ak_vm_call returns at
// once with a none result and the coroutine becomes a task that resumes
// when what it awaits completes.
//
// Closures the VM makes are ordinary rt closures: C code (the UI
// runtime) calls them with ak_call.
#ifndef AK_VM_H
#define AK_VM_H

#include "rt/rt.h"

typedef struct ak_module ak_module;
typedef struct ak_func ak_func;

// A builtin: a C function bytecode calls by name (CALLB). It borrows its
// arguments and returns an owned result; it throws with ak_throw.
typedef ak_val (*ak_builtin_fn)(ak_val *args, uint32_t nargs);

// ak_builtin_register makes fn callable by name. Builtins are registered
// once, before modules load (ak_vm_builtins_init registers the core's).
void ak_builtin_register(const char *name, ak_builtin_fn fn);
ak_builtin_fn ak_builtin_find(const char *name);
void ak_vm_builtins_init(void);
// ak_vm_init sets the VM up: its builtins, and the signal graph's hold
// on its errors. Once, on the main thread, before loading a module.
void ak_vm_init(void);

// ak_module_load reads a module. On failure it returns NULL with a
// message in err.
ak_module *ak_module_load(const uint8_t *p, size_t n, char *err, size_t errcap);
// ak_module_release drops the loader's reference; the module is freed
// when the last closure of its code goes too.
void ak_module_release(ak_module *m);

uint32_t ak_module_nfuncs(const ak_module *m);
const ak_func *ak_module_func(const ak_module *m, uint32_t i);
const ak_func *ak_module_find(const ak_module *m, const char *name);
uint32_t ak_module_init(const ak_module *m);
int32_t ak_module_app(const ak_module *m);
uint32_t ak_module_ntests(const ak_module *m);
const char *ak_module_test(const ak_module *m, uint32_t i, uint32_t *func);
const ak_shape *ak_module_shape(const ak_module *m, uint32_t i);
const ak_shape *ak_module_shape_named(const ak_module *m, const char *name);
const char *ak_func_name(const ak_func *f);
bool ak_func_async(const ak_func *f);

// ak_vm_call calls f with its arguments (borrowed).
ak_val ak_vm_call(const ak_func *f, ak_val *args, uint32_t nargs);

// ak_vm_closure makes a closure of f with no captures.
ak_closure *ak_vm_closure(const ak_func *f);

// The tasks async code runs on.
typedef struct ak_task ak_task;

// ak_vm_run_ready resumes every task whose await completed, until none
// is left. The UI runtime calls it after each event and posted callback.
void ak_vm_run_ready(void);
bool ak_vm_has_ready(void);

// ak_vm_traceback writes the VM's call stack (innermost first, a line per
// frame: function, file:line) into buf. For error reports.
void ak_vm_traceback(char *buf, size_t cap);

#endif
