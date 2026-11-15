// vm_int.h: the VM's insides, shared by its files.
#ifndef AK_VM_INT_H
#define AK_VM_INT_H

#include "vm/vm.h"
#include <stdatomic.h>
#include "rt/sched.h"
#include "vm/opcodes_gen.h"
#include "signal/signal.h"

typedef struct { uint32_t start, end, target; } ak_handler;
typedef struct { uint32_t pc, line; } ak_line;

// A function's body compiled to C (lang/cgen): it takes its arguments
// (borrowed; it retains what it keeps) and the running closure, and
// returns its result (owned), throwing as builtins do.
typedef ak_val (*ak_native_body)(ak_val *args, ak_closure *clos);

struct ak_func {
	ak_module *mod;
	ak_native_body native;  // set when the function was compiled to C
	ak_str *name;
	ak_str *file;
	uint16_t nparams;
	uint16_t nregs;
	uint8_t *kinds;     // per register
	uint8_t *capkinds;  // per capture
	uint16_t ncap;
	uint8_t ret;        // the result's kind
	uint8_t flags;      // bytecode.FAsync, FThrows
	uint16_t *code;
	uint32_t ncode;
	ak_handler *handlers;
	uint32_t nhandlers;
	ak_line *lines;
	uint32_t nlines;
	uint32_t index;
};

#define AK_F_ASYNC 1
#define AK_F_THROWS 2

typedef struct {
	ak_str *name;
	uint32_t func;
} ak_method;

typedef struct {
	ak_shape s;
	struct ak_module *mod;
	ak_method *methods;
	uint32_t nmethods;
	// Storage the shape's arrays point into.
	uint8_t *kinds;
	const ak_shape **shapes;
	const char **names;
	ak_case *cases;
	const char **case_names;  // made on first use (json_lib.c)
} ak_vm_shape;

struct ak_module {
	_Atomic uint32_t rc;  // the loader's, one per live closure of its code and per task; any thread
	int64_t *ints;
	uint32_t nints;
	double *floats;
	uint32_t nfloats;
	ak_str **strings;
	uint32_t nstrings;
	ak_vm_shape *shapes;
	uint32_t nshapes;
	ak_func *funcs;
	uint32_t nfuncs;
	ak_builtin_fn *builtins;
	uint32_t nbuiltins;
	ak_val *globals;
	uint8_t *gkinds;
	uint32_t nglobals;
	uint32_t init;
	int32_t app;
	struct { ak_str *name; uint32_t func; } *tests;
	uint32_t ntests;
	struct { ak_str **data; uint32_t len, cap; } names;  // what shapes' names point into
};

void ak_module_retain(ak_module *m);

// A call frame on a coroutine.
typedef struct {
	const ak_func *fn;
	ak_closure *clos;  // retained for the call, or NULL
	uint32_t pc;       // where to resume (in words)
	uint32_t callpc;   // the caller's call instruction, for its handlers
	uint32_t base;     // r0's index in the coroutine's registers
	uint16_t dst;      // the caller's register for the result
	uint8_t boundary;  // returning from it returns to C
} ak_frame;

typedef struct ak_co {
	ak_val *regs;
	uint32_t top, cap;
	ak_frame *frames;
	uint32_t nframes, fcap;
	struct ak_task *task;  // NULL for the main coroutine
} ak_co;

// A task: an async function running on its own coroutine, and the
// future of its result.
struct ak_task {
	ak_future *fut;     // retained
	ak_co co;
	ak_future *awaiting;  // retained while suspended
	ak_owner *owner;      // its own owner, under the one that started it
	ak_module *mod;       // retained while it runs
	bool running, cancelled;
};

extern AK_TLS ak_co *ak_vm_cur;  // the coroutine running now (NULL: the main one)
extern AK_TLS ak_co ak_vm_main;

// The interpreter: runs co from its top frame until a boundary frame
// returns (its result in *out) or the coroutine suspends (true).
bool ak_vm_run(ak_co *co, ak_val *out);
// ak_vm_push pushes a frame calling f with args; false (an error thrown)
// on stack overflow.
bool ak_vm_push(ak_co *co, const ak_func *f, ak_closure *clos, ak_val *args, uint32_t nargs, uint16_t dst, bool boundary, uint32_t callpc);
ak_val ak_vm_closure_invoke(ak_closure *c, ak_val *args, uint32_t nargs);
// ak_vm_task_start starts f as a task and returns its future (owned).
ak_future *ak_vm_task_start(const ak_func *f, ak_closure *clos, ak_val *args, uint32_t nargs);
void ak_vm_task_resume(void *task);

// The reactive glue (react.c): cells, memos, accessors, effects, owners.
ak_val ak_vm_cell_new(ak_val v, ak_kind k, ak_str *keep_key);
ak_val ak_vm_cell_get(void *cell, bool track);
void ak_vm_cell_set(void *cell, ak_val v);         // takes v
ak_val ak_vm_cell_take(void *cell);
void ak_vm_cell_put(void *cell, ak_val v);         // takes v
ak_val ak_vm_memo_new(ak_closure *c, ak_kind k);
ak_val ak_vm_memo_get(void *memo);
ak_val ak_vm_acc_value(ak_val v, ak_kind k);
ak_val ak_vm_acc_func(ak_closure *c, ak_kind k);
ak_val ak_vm_acc_cell(void *cell_or_memo);
ak_val ak_vm_acc_get(void *acc);
ak_val ak_vm_untrack(ak_closure *c, ak_val *args, uint32_t nargs);
void ak_vm_effect(ak_closure *c, bool render);
void ak_vm_cleanup(ak_closure *c);
void ak_vm_batch(bool begin);
// ak_vm_method finds an interface method of a struct's or an enum's
// shape (NULL: none).
const ak_func *ak_vm_method(void *obj, ak_str *name);
void ak_vm_react_init(void);
void ak_vm_json_init(void);
void ak_vm_task_own(ak_task *t);  // ties a task to the current owner
bool ak_vm_task_run(ak_task *t, ak_val *out);  // ak_vm_run, under the task's owner
// ak_vm_task_done lets the task's owner go; err (owned, or NULL) is an
// error nobody can see through the task's future, raised to the owner's
// catcher.
void ak_vm_task_done(ak_task *t, void *err);

#endif
