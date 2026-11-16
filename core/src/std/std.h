// std.h: the standard library's packages written in C: the builtins
// their `native fn`s are (lang/LOWERING.md, "The standard library's
// native fns"), named <package>.<fn>. Each package's .asuka (std/<name>)
// declares them and writes the rest of its API on top of them in Asuka.
//
// Like rt/lib.h's, a builtin borrows its arguments and returns an owned
// result, throws with ak_throw, and may run on spawn's worker threads:
// nothing here keeps state that isn't per thread or immutable.
#ifndef AK_STD_H
#define AK_STD_H

#include "rt/lib.h"

extern const ak_lib_entry ak_std_time[];
extern const ak_lib_entry ak_std_url[];
extern const ak_lib_entry ak_std_regex[];
extern const ak_lib_entry ak_std_encoding[];
extern const ak_lib_entry ak_std_crypto[];
extern const ak_lib_entry ak_std_html[];
extern const ak_lib_entry ak_std_random[];

// Every package's table, NULL-terminated.
extern const ak_lib_entry *const ak_std_libs[];

// ---- Shared helpers ----

// ak_utf8_decode reads the code point at s[*i] (n bytes in all) and
// moves *i past it; what isn't UTF-8 reads as U+FFFD, one byte.
uint32_t ak_utf8_decode(const char *s, size_t n, size_t *i);
// ak_utf8_encode writes c's UTF-8 into out (4 bytes room); its length.
size_t ak_utf8_encode(uint32_t c, char *out);

// ak_os_random fills p with n bytes of the system's randomness (any
// thread); false if the system gave none.
bool ak_os_random(void *p, size_t n);

// An array of strings (the elements' flag set, so arrays compare them).
ak_arr *ak_str_arr(uint32_t cap);
// Pushes a new string of s[0:n] onto *a.
void ak_str_arr_push(ak_arr **a, const char *s, size_t n);

// ak_sha256 and ak_sha1 hash p into out (32 and 20 bytes).
void ak_sha256(const void *p, size_t n, uint8_t out[32]);
void ak_sha1(const void *p, size_t n, uint8_t out[20]);

#endif
