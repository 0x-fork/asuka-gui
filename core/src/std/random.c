// random.c: asuka/random's builtins: a fast generator (xoshiro256**)
// per thread, seeded from the system's randomness when a thread first
// uses it, or by random.seed for a sequence that repeats. Not for
// secrets: asuka/crypto's randomBytes is.
//
//	random.float() -> float       in [0, 1)
//	random.int(a, b) -> int       in [a, b] (either order; unbiased)
//	random.seed(n)                this thread's sequence from n
#include "std/std.h"

typedef struct {
	uint64_t s[4];
	bool seeded;
} rng;

static AK_TLS rng state;

static uint64_t splitmix(uint64_t *x) {
	uint64_t z = (*x += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}

static void seed(uint64_t x) {
	for (int i = 0; i < 4; i++) state.s[i] = splitmix(&x);
	state.seeded = true;
}

static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t next(void) {
	if (!state.seeded) {
		uint64_t x;
		if (!ak_os_random(&x, sizeof x)) x = (uint64_t)(uintptr_t)&state ^ 0x2545f4914f6cdd1dull;
		seed(x);
	}
	uint64_t *s = state.s;
	uint64_t r = rotl(s[1] * 5, 7) * 9;
	uint64_t t = s[1] << 17;
	s[2] ^= s[0], s[3] ^= s[1], s[1] ^= s[2], s[0] ^= s[3];
	s[2] ^= t;
	s[3] = rotl(s[3], 45);
	return r;
}

static ak_val float_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.f = (double)(next() >> 11) * (1.0 / 9007199254740992.0)};
}

// int_fn: Lemire's method, without bias.
static ak_val int_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t lo = args[0].i, hi = args[1].i;
	if (hi < lo) {
		int64_t t = lo;
		lo = hi, hi = t;
	}
	uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;  // 0: every int
	if (span == 0) return (ak_val){.i = (int64_t)next()};
	uint64_t threshold = -span % span;
	for (;;) {
		uint64_t r = next();
		if (r >= threshold) return (ak_val){.i = (int64_t)((uint64_t)lo + r % span)};
	}
}

static ak_val seed_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	seed((uint64_t)args[0].i);
	return (ak_val){0};
}

const ak_lib_entry ak_std_random[] = {
	{"random.float", float_fn},
	{"random.int", int_fn},
	{"random.seed", seed_fn},
	{NULL, NULL},
};
