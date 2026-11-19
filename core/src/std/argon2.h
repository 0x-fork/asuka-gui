// argon2.h: Argon2 (argon2.c), with Monocypher's crypto_argon2's
// arguments and results, its lanes on threads and its compression
// function on the CPU's vector unit. password.c's.
#ifndef AK_ARGON2_H
#define AK_ARGON2_H

#include "std/monocypher.h"

// ak_argon2 is crypto_argon2: work is config.nb_blocks KiB, 16-byte
// aligned (malloc's), and wiped after. Any thread; it starts up to a
// thread a lane more, and waits for them.
void ak_argon2(uint8_t *hash, uint32_t hash_size, void *work, crypto_argon2_config config, crypto_argon2_inputs inputs,
	crypto_argon2_extras extras);

// ak_argon2_threads: at most this many threads a hash, its caller's
// among them (the tests: 1, the lanes one after another).
extern int ak_argon2_threads;

#endif
