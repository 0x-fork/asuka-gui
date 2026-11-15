// lib.h: the standard library's functions in C, shared by both backends.
// The VM calls them by name (CALLB); compiled C calls them directly.
//
// Convention: a function borrows its arguments and returns an owned
// result; it throws with ak_throw. A method that changes its receiver
// ("inout") changes args[0] in place: the caller passes the receiver's
// own slot (a register, a local), whose value it may replace (copy on
// write). Elements a function stores are retained.
#ifndef AK_LIB_H
#define AK_LIB_H

#include "rt/rt.h"

typedef ak_val (*ak_lib_fn)(ak_val *args, uint32_t nargs);

typedef struct {
	const char *name;
	ak_lib_fn fn;
} ak_lib_entry;

// The standard library's functions, NULL-terminated.
extern const ak_lib_entry ak_lib[];

// Where print writes (tests capture it); NULL: stdout.
extern void (*ak_lib_print_hook)(const char *s, size_t n);

#endif
