// Small blocks: what heap objects of a size that never changes take (a
// struct, an enum, a box, a closure, an array's or a map's header), a
// thread's freed ones kept by size class and reused before malloc's, as
// allocators' thread caches do (mimalloc's, glibc's tcache): a block
// taken or given back is a pointer popped or pushed, where malloc and
// free take tens of nanoseconds each, the most of what making and
// dropping an object costs.
//
// Each thread has its own cache, so no block is shared between threads
// while it's cached; a block freed on another thread than it was made
// on (a value spawn's worker made) just joins that thread's. A cache
// holds at most BUDGET bytes (more goes back to malloc), and goes back
// to malloc whole as its thread ends. Under a sanitizer there's no
// cache: AddressSanitizer sees every block freed, so a use after free
// is still caught.
#if !defined(_WIN32) && !defined(__wasm__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "base/base.h"

#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#elif !defined(__wasm__)
#include <pthread.h>
#endif

// (AK_SMALL_CACHE: the cache whatever the build, for its own test.)
#if defined(AK_SMALL_CACHE)
#elif defined(__SANITIZE_ADDRESS__)
#define NO_CACHE 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer)
#define NO_CACHE 1
#endif
#endif

#define CLASS(n) (((n) + 15) >> 4)  // 16 bytes each
#define CLASSES CLASS(AK_SMALL_MAX)
#ifndef BUDGET
#define BUDGET ((size_t)32 << 20)
#endif

#if !NO_CACHE

typedef struct block {
	struct block *next;
} block;

typedef struct {
	block *free[CLASSES + 1];
	size_t bytes;  // what the lists hold
} cache;

// The thread's cache: made on its first free, freed with what it holds
// as the thread ends (a pthread key's destructor, a fiber-local's
// callback), which gets it as its value, not through the thread-local
// (gone by then on some systems).
static AK_TLS cache *mine;

__attribute__((unused)) static void drain(void *p) {
	cache *c = p;
	if (!c) return;
	for (int k = 0; k <= CLASSES; k++) {
		for (block *b = c->free[k]; b;) {
			block *n = b->next;
			free(b);
			b = n;
		}
	}
	free(c);
}

#if defined(_WIN32)
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static DWORD fls;
static void WINAPI fls_drain(void *p) { drain(p); }
static BOOL CALLBACK make_key(PINIT_ONCE o, void *arg, void **ctx) {
	AK_UNUSED(o), AK_UNUSED(arg), AK_UNUSED(ctx);
	fls = FlsAlloc(fls_drain);
	return TRUE;
}
static void on_exit_drain(cache *c) {
	InitOnceExecuteOnce(&once, make_key, NULL, NULL);
	if (fls != FLS_OUT_OF_INDEXES) FlsSetValue(fls, c);
}
#elif defined(__wasm__)
static void on_exit_drain(cache *c) { AK_UNUSED(c); }
#else
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t key;
static void make_key(void) { pthread_key_create(&key, drain); }
static void on_exit_drain(cache *c) {
	pthread_once(&once, make_key);
	pthread_setspecific(key, c);
}
#endif

void *ak_alloc_small(size_t n) {
	AK_ASSERT(n <= AK_SMALL_MAX);
#ifndef NDEBUG
	ak_alloc_count++;
	ak_alloc_bytes += n;
#endif
	size_t k = CLASS(n ? n : 1);
	cache *c = mine;
	if (c) {
		block *b = c->free[k];
		if (b) {
			c->free[k] = b->next;
			c->bytes -= k << 4;
			memset(b, 0, k << 4);
			return b;
		}
	}
	void *p = calloc(1, k << 4);
	if (!p) ak_panic("out of memory (%zu bytes)", n);
	return p;
}

void ak_free_small(void *p, size_t n) {
	if (!p) return;
	size_t k = CLASS(n ? n : 1);
	cache *c = mine;
	if (AK_UNLIKELY(!c)) {
		c = calloc(1, sizeof *c);
		if (!c) {
			free(p);
			return;
		}
		mine = c;
		on_exit_drain(c);
	}
	if (c->bytes + (k << 4) > BUDGET) {
		free(p);
		return;
	}
	block *b = p;
	b->next = c->free[k];
	c->free[k] = b;
	c->bytes += k << 4;
}

#else

void *ak_alloc_small(size_t n) {
	AK_ASSERT(n <= AK_SMALL_MAX);
	return ak_alloc(n);
}

void ak_free_small(void *p, size_t n) {
	AK_UNUSED(n);
	ak_free(p);
}

#endif
