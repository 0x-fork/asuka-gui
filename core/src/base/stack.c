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
