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
// The sized integers' (int.c).
extern const ak_lib_entry ak_int_lib[];
// Lazy sequences' and ordering by a key's (seq.c), registered when a
// build names them (link.c's AK_USES_SEQ).
extern const ak_lib_entry ak_seq_lib[];
// B-trees' (tree.c: std/collections' BTree), registered when a build
// names them (link.c's AK_USES_TREE).
extern const ak_lib_entry ak_tree_lib[];

// ak_spawn_elsewhere, when set, runs a spawn's closure c elsewhere (the
// web's workers: abi/web.c) and settles f (of kind k) once it's done;
// false: it can't (spawn then runs c as it would without it).
typedef struct ak_future ak_future;
extern bool (*ak_spawn_elsewhere)(ak_closure *c, ak_kind k, ak_future *f);
// ak_closure_deep_copy copies a closure whose captures are values for
// another thread: its captures deep-copied (ak_deep_copy), what it holds
// (the VM's module) held by the copy too. Main thread.
ak_closure *ak_closure_deep_copy(ak_closure *c);

// ak_stdio_write writes a then b (either may be empty) to stdout (fd 1)
// or stderr (2) at once and unbuffered: one writev a call where there's
// a descriptor (Go's os.Stdout's model), stdio flushed at once
// elsewhere. print's, print.text's and os.writeTo's: what C's stdio
// buffers isn't theirs, so C that writes stdout through stdio flushes it.
void ak_stdio_write(int fd, const void *a, size_t an, const void *b, size_t bn);

// Where print writes (tests capture it); NULL: stdout.
extern void (*ak_lib_print_hook)(const char *s, size_t n);
// Where print.text writes text as it is, to stdout (fd 1) or stderr (2):
// eprint's, and print's with a terminator of its own (tests capture it);
// NULL: the stream.
extern void (*ak_lib_write_hook)(int fd, const char *s, size_t n);

#endif
