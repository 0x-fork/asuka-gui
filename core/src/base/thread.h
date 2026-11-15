// thread.h: the few threading primitives the core needs (the post
// queue, timers, spawn's workers): pthreads, or Win32 on Windows.
#ifndef AK_THREAD_H
#define AK_THREAD_H

#include "base/base.h"

#if defined(_WIN32)
#include <windows.h>
typedef SRWLOCK ak_mutex;
typedef CONDITION_VARIABLE ak_cond;
typedef HANDLE ak_thread;
#define AK_MUTEX_INIT SRWLOCK_INIT
#elif defined(__wasm__)
typedef int ak_mutex;
typedef int ak_cond;
typedef int ak_thread;
#define AK_MUTEX_INIT 0
#else
#include <pthread.h>
typedef pthread_mutex_t ak_mutex;
typedef pthread_cond_t ak_cond;
typedef pthread_t ak_thread;
#define AK_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#endif

void ak_mutex_lock(ak_mutex *m);
void ak_mutex_unlock(ak_mutex *m);
void ak_cond_init(ak_cond *c);
void ak_cond_wait(ak_cond *c, ak_mutex *m);
// ak_cond_wait_ns waits at most ns nanoseconds; false on a timeout.
bool ak_cond_wait_ns(ak_cond *c, ak_mutex *m, int64_t ns);
void ak_cond_signal(ak_cond *c);
void ak_cond_broadcast(ak_cond *c);
// ak_thread_start runs fn(arg) on a new thread; false when threads
// aren't available (wasm).
bool ak_thread_start(void (*fn)(void *), void *arg);
// ak_now_ns is a monotonic clock in nanoseconds.
int64_t ak_now_ns(void);
// ak_wall_ns is the time since the Unix epoch in nanoseconds.
int64_t ak_wall_ns(void);
// ak_thread_id names the calling thread, for main-thread checks.
uint64_t ak_thread_id(void);

#endif
