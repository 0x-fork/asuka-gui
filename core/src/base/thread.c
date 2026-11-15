#include "base/thread.h"

#include <time.h>

#if defined(_WIN32)

void ak_mutex_lock(ak_mutex *m) { AcquireSRWLockExclusive(m); }
void ak_mutex_unlock(ak_mutex *m) { ReleaseSRWLockExclusive(m); }
void ak_cond_init(ak_cond *c) { InitializeConditionVariable(c); }
void ak_cond_wait(ak_cond *c, ak_mutex *m) { SleepConditionVariableSRW(c, m, INFINITE, 0); }
bool ak_cond_wait_ns(ak_cond *c, ak_mutex *m, int64_t ns) {
	DWORD ms = ns <= 0 ? 0 : (DWORD)((ns + 999999) / 1000000);
	return SleepConditionVariableSRW(c, m, ms, 0) != 0;
}
void ak_cond_signal(ak_cond *c) { WakeConditionVariable(c); }
void ak_cond_broadcast(ak_cond *c) { WakeAllConditionVariable(c); }

typedef struct { void (*fn)(void *); void *arg; } start_t;
static DWORD WINAPI trampoline(LPVOID p) {
	start_t s = *(start_t *)p;
	ak_free(p);
	s.fn(s.arg);
	return 0;
}
bool ak_thread_start(void (*fn)(void *), void *arg) {
	start_t *s = ak_alloc(sizeof *s);
	s->fn = fn;
	s->arg = arg;
	HANDLE h = CreateThread(NULL, 0, trampoline, s, 0, NULL);
	if (!h) {
		ak_free(s);
		return false;
	}
	CloseHandle(h);
	return true;
}
int64_t ak_now_ns(void) {
	static LARGE_INTEGER freq;
	if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (int64_t)((double)c.QuadPart * 1e9 / (double)freq.QuadPart);
}
int64_t ak_wall_ns(void) {
	FILETIME ft;
	GetSystemTimePreciseAsFileTime(&ft);
	uint64_t t = ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - 116444736000000000ull;
	return (int64_t)t * 100;
}
uint64_t ak_thread_id(void) { return GetCurrentThreadId(); }

#elif defined(__wasm__)

void ak_mutex_lock(ak_mutex *m) { AK_UNUSED(m); }
void ak_mutex_unlock(ak_mutex *m) { AK_UNUSED(m); }
void ak_cond_init(ak_cond *c) { AK_UNUSED(c); }
void ak_cond_wait(ak_cond *c, ak_mutex *m) { AK_UNUSED(c); AK_UNUSED(m); }
bool ak_cond_wait_ns(ak_cond *c, ak_mutex *m, int64_t ns) { AK_UNUSED(c); AK_UNUSED(m); AK_UNUSED(ns); return false; }
void ak_cond_signal(ak_cond *c) { AK_UNUSED(c); }
void ak_cond_broadcast(ak_cond *c) { AK_UNUSED(c); }
bool ak_thread_start(void (*fn)(void *), void *arg) { AK_UNUSED(fn); AK_UNUSED(arg); return false; }
int64_t ak_now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
int64_t ak_wall_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
uint64_t ak_thread_id(void) { return 1; }

#else

void ak_mutex_lock(ak_mutex *m) { pthread_mutex_lock(m); }
void ak_mutex_unlock(ak_mutex *m) { pthread_mutex_unlock(m); }
void ak_cond_init(ak_cond *c) { pthread_cond_init(c, NULL); }
void ak_cond_wait(ak_cond *c, ak_mutex *m) { pthread_cond_wait(c, m); }
bool ak_cond_wait_ns(ak_cond *c, ak_mutex *m, int64_t ns) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	int64_t t = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec + (ns > 0 ? ns : 0);
	ts.tv_sec = (time_t)(t / 1000000000);
	ts.tv_nsec = (long)(t % 1000000000);
	return pthread_cond_timedwait(c, m, &ts) == 0;
}
void ak_cond_signal(ak_cond *c) { pthread_cond_signal(c); }
void ak_cond_broadcast(ak_cond *c) { pthread_cond_broadcast(c); }

typedef struct { void (*fn)(void *); void *arg; } start_t;
static void *trampoline(void *p) {
	start_t s = *(start_t *)p;
	ak_free(p);
	s.fn(s.arg);
	return NULL;
}
bool ak_thread_start(void (*fn)(void *), void *arg) {
	start_t *s = ak_alloc(sizeof *s);
	s->fn = fn;
	s->arg = arg;
	pthread_t t;
	if (pthread_create(&t, NULL, trampoline, s) != 0) {
		ak_free(s);
		return false;
	}
	pthread_detach(t);
	return true;
}
int64_t ak_now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
int64_t ak_wall_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
uint64_t ak_thread_id(void) { return (uint64_t)(uintptr_t)pthread_self(); }

#endif
