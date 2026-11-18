// std.c: the packages' tables, and what several of them share.
#include "std/std.h"

// The packages' tables (ak_std_libs) are core/link/link.c's: a build
// with an app compiled in registers only those whose builtins it calls,
// so the linker leaves the rest of the core out.


ak_arr *ak_str_arr(uint32_t cap) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, cap);
	a->h.flags = 1;
	return a;
}

void ak_str_arr_push(ak_arr **a, const char *s, size_t n) {
	ak_arr_push(a, (ak_val){.p = ak_str_new(s, n)});
}
