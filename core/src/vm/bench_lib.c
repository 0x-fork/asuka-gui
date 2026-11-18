// keep(x) (lang/check/testing.go): x, through a call the C compiler
// can't see into, so that a benchmark's work, compiled to C, isn't left
// out because nothing reads its result (Rust's black_box). It's in every
// build: an app's code may call it as a bench's does. keep.ref is a
// reference's (the caller's register holds the result too).
#include "vm/vm_int.h"

static ak_val keep(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return args[0];
}

static ak_val keep_ref(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_retain(args[0].p);
	return args[0];
}

void ak_vm_bench_init(void) {
	ak_builtin_register("keep", keep);
	ak_builtin_register("keep.ref", keep_ref);
}
