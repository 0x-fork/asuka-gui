// The C stack's bounds, so that code that recurses (Asuka compiled to C,
// a call from C into the VM) throws before it overflows the thread's
// stack, whose size depends on the platform and the thread: 1 MB on
// iOS's main thread, 512 KB on a secondary thread of Apple's.
#if defined(__linux__) || defined(__ANDROID__)
#define _GNU_SOURCE  // pthread_getattr_np
#endif
#include "base/base.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__wasm__)
// wasm-ld's: the stack's bounds (it grows down from __stack_high; with
// the stack first, __stack_low is 0).
extern char __stack_low __attribute__((weak));
extern char __stack_high __attribute__((weak));
// The page's (platform/web/src/stack.js): whether a quarter of the
// engine's own stack is still free.
__attribute__((import_module("asuka"), import_name("stack_room"))) int js_stack_room(void);
#else
#include <pthread.h>
#endif

AK_TLS uintptr_t ak_stack_lim;

// margin is what's left above the limit for the code that runs past the
// check (a builtin's own calls, the error's message): a quarter of the
// stack, at most 256 KB.
static uintptr_t margin(uintptr_t size) {
	uintptr_t m = size / 4;
	return m > 256 * 1024 ? 256 * 1024 : m;
}

#if defined(__wasm__)
// The engine's stack. Wasm's frames are on the engine's own stack (V8's
// is about a megabyte, JavaScript's frames below them included), not
// only on the C stack in the core's memory (Zig's 16 MB): a call nested
// from C into the VM takes more of the engine's than of the C stack, and
// would run it out (a RangeError the app can't catch, which ends the
// page) long before the C stack's bound. Neither stack says how much of
// the other a frame takes, so the core asks: each time its C stack
// reaches STEP past where it last asked, the page tries a quarter of the
// engine's stack (stack.js) and says whether it fit. The window moves up
// as the stack unwinds, so a path that takes more of the engine's per
// byte of C stack is asked about too. Code that doesn't recurse never
// asks; one that does asks once per STEP it goes deeper.
#define STEP (16 * 1024)

uintptr_t ak_stack_ask, ak_stack_reask = UINTPTR_MAX;

bool ak_stack_engine(uintptr_t sp) {
	bool low = sp < ak_stack_ask && !js_stack_room();
	if (!low) {
		ak_stack_ask = sp > STEP ? sp - STEP : 0;
		ak_stack_reask = sp + STEP;
	}
	return low || sp < ak_stack_lim;
}
#endif

uintptr_t ak_stack_init(void) {
	uintptr_t lo = 0, size = 0;
#if defined(_WIN32)
	ULONG_PTR l = 0, h = 0;
	GetCurrentThreadStackLimits(&l, &h);
	lo = (uintptr_t)l;
	size = (uintptr_t)(h - l);
#elif defined(__wasm__)
	if (&__stack_high) {
		lo = (uintptr_t)&__stack_low;
		size = (uintptr_t)&__stack_high - lo;
	}
	uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
	ak_stack_ask = sp > STEP ? sp - STEP : 0;
	ak_stack_reask = sp + STEP;
#elif defined(__APPLE__)
	pthread_t t = pthread_self();
	uintptr_t hi = (uintptr_t)pthread_get_stackaddr_np(t);
	size = (uintptr_t)pthread_get_stacksize_np(t);
	lo = hi - size;
#else
	pthread_attr_t a;
	if (pthread_getattr_np(pthread_self(), &a) == 0) {
		void *addr = NULL;
		size_t n = 0;
		if (pthread_attr_getstack(&a, &addr, &n) == 0) {
			lo = (uintptr_t)addr;
			size = (uintptr_t)n;
		}
		pthread_attr_destroy(&a);
	}
#endif
	// Unknown: 1, below every frame, so it isn't asked again.
	ak_stack_lim = size ? lo + margin(size) : 1;
	return ak_stack_lim;
}

