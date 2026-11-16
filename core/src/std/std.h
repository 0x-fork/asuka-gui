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
extern const ak_lib_entry ak_std_fs[];
extern const ak_lib_entry ak_std_crash[];

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

// ak_sha256, ak_sha512 and ak_sha1 hash p into out (32, 64 and 20
// bytes).
void ak_sha256(const void *p, size_t n, uint8_t out[32]);
void ak_sha512(const void *p, size_t n, uint8_t out[64]);
void ak_sha1(const void *p, size_t n, uint8_t out[20]);

// The same over what comes in pieces (a file, streamed): init, update
// with each piece, final.
typedef struct {
	uint32_t h[8];
	uint8_t buf[64];
	size_t n;
	uint64_t len;
} ak_sha256_ctx;
void ak_sha256_init(ak_sha256_ctx *c);
void ak_sha256_update(ak_sha256_ctx *c, const void *p, size_t n);
void ak_sha256_final(ak_sha256_ctx *c, uint8_t out[32]);

typedef struct {
	uint64_t h[8];
	uint8_t buf[128];
	size_t n;
	uint64_t len;
} ak_sha512_ctx;
void ak_sha512_init(ak_sha512_ctx *c);
void ak_sha512_update(ak_sha512_ctx *c, const void *p, size_t n);
void ak_sha512_final(ak_sha512_ctx *c, uint8_t out[64]);

// Ed25519 (RFC 8032, ed25519.c): a key pair is a 32-byte seed (the
// private key; Go's and the CLI's 64-byte private keys are the seed and
// the public key) and its 32-byte public key. Verifying is strict as Go's
// crypto/ed25519 is: S below the group's order, A a point on the curve.
void ak_ed25519_public(const uint8_t seed[32], uint8_t pub[32]);
void ak_ed25519_sign(const uint8_t seed[32], const uint8_t pub[32], const uint8_t *m, size_t n, uint8_t sig[64]);
bool ak_ed25519_verify(const uint8_t pub[32], const uint8_t *m, size_t n, const uint8_t sig[64]);

// The app's directory (fs.appDir): what the host set with
// asuka_set_app_dir, else ASUKA_DATA, else the platform's default.
void ak_fs_set_app_dir(const char *path);
// ak_fs_app_dir is that directory's path (ak_free it).
char *ak_fs_app_dir(void);

// asuka/crash's record (crash.c): the app's lifecycle (AK_LIFECYCLE_*),
// for its reports' context; and whether the app compiled into the build
// reads its reports (lang/cgen sets it).
void ak_crash_lifecycle(int state);
extern bool ak_app_crash;
// For tests (core/runner): a new empty directory in the system's
// temporary one (ak_free the path; NULL when it can't be made), and
// removing a directory and what's in it.
char *ak_fs_temp_dir(void);
bool ak_fs_remove_tree(const char *path);

#endif
