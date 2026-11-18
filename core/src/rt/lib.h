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
// Strings by Unicode's tables (upper, graphemes, ignoreCase: lib.c),
// registered when a build names them (link.c's AK_USES_UNICODE).
extern const ak_lib_entry ak_lib_unicode[];
// Streams' (stream.c).
extern const ak_lib_entry ak_stream_lib[];
// Lazy sequences' and ordering by a key's (seq.c), registered when a
// build names them (link.c's AK_USES_SEQ).
extern const ak_lib_entry ak_seq_lib[];

// ak_spawn_elsewhere, when set, runs a spawn's closure c elsewhere (the
// web's workers: abi/web.c) and settles f (of kind k) once it's done;
// false: it can't (spawn then runs c as it would without it).
typedef struct ak_future ak_future;
extern bool (*ak_spawn_elsewhere)(ak_closure *c, ak_kind k, ak_future *f);
// ak_closure_deep_copy copies a closure whose captures are values for
// another thread: its captures deep-copied (ak_deep_copy), what it holds
// (the VM's module) held by the copy too. Main thread.
ak_closure *ak_closure_deep_copy(ak_closure *c);

// Where print writes (tests capture it); NULL: stdout.
extern void (*ak_lib_print_hook)(const char *s, size_t n);

#endif
