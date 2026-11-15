// thread.c: the main-thread check (signal/thread_debug.go), over
// pthreads or Win32.
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "signal/signal_impl.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static uintptr_t self(void) { return (uintptr_t)GetCurrentThreadId(); }
#elif defined(__wasm__) && !defined(_REENTRANT)
static uintptr_t self(void) { return 1; } // one thread
#else
#include <pthread.h>
// pthread_t is a scalar on every platform the core targets.
static uintptr_t self(void) { return (uintptr_t)pthread_self(); }
#endif

#define g ak_sig

void ak_signal_bind_thread(void) {
	g->main_thread = self();
	g->thread_bound = true;
}

bool ak_signal_on_main_thread(void) { return !g->thread_bound || self() == g->main_thread; }

// ak_sig_check_thread reports whether the caller may write; in debug
// builds, a write off the main thread panics.
bool ak_sig_check_thread(void) {
#if defined(AK_DEBUG) || defined(AK_TEST)
	if (AK_UNLIKELY(!ak_signal_on_main_thread())) {
		ak_sig_panic("signal: write off the main thread; use asuka_post");
		return false;
	}
#endif
	return true;
}
