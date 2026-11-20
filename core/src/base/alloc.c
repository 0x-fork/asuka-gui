// alloc.c: the core's allocator, what ak_alloc, ak_realloc, ak_free and
// the small blocks' ak_alloc_small and ak_free_small take memory from.
//
// mimalloc (core/third_party/mimalloc), compiled into this file so its
// fast paths inline into ak_alloc's: a thread's own pages of blocks by
// size class, a block taken or given back a pointer popped or pushed off
// the page's free list, a block freed on another thread than it was made
// on pushed atomically onto its page's list for the owner to collect
// (spawn's workers, the I/O threads), and pages nothing uses returned to
// the system after a moment. The C libraries' allocators are slower at
// what Asuka programs do most (strings and arrays made and dropped by the
// million): glibc's malloc and free take long paths and consolidate as
// they go (half of a JSON round trip's time on Linux was in them), musl's
// is slower still, and Apple's and Android's scudo are slower too
// (bench/compiled/README.md). It's only the core's: nothing else in the
// process (the C library's malloc, a host's, a native package's) uses it,
// its exported names are the core's own (mimalloc_names.h), so an app
// linking another mimalloc doesn't clash with it, and it changes nothing
// of the process's (no environment variables read, transparent huge
// pages left as the system has them).
//
// Under AddressSanitizer or MemorySanitizer (the core's tests: `go test
// ./core`) it's the C library's malloc, so the sanitizer sees every block
// made and freed and still catches a use after free. A web build keeps
// its C library's (dlmalloc) too: a module's linear memory never shrinks,
// so its high-water mark is what it keeps, and dlmalloc's is a third of
// mimalloc's on churning blocks; and the module stays smaller.
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE  // mimalloc's madvise, syscall, realpath (before any header)
#define _GNU_SOURCE      // mremap (ak_realloc's)
#endif
#include "base/base.h"

// (-DAK_ALLOC_SYSTEM=1: the C library's whatever the build, to compare.)
#ifndef AK_ALLOC_SYSTEM
#if defined(__SANITIZE_ADDRESS__) || defined(__wasm__)
#define AK_ALLOC_SYSTEM 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer)
#define AK_ALLOC_SYSTEM 1
#endif
#endif
#endif

AK_TLS uint64_t ak_alloc_count;
AK_TLS uint64_t ak_alloc_bytes;

#ifndef NDEBUG
#define COUNT(n) (ak_alloc_count++, ak_alloc_bytes += (n))
#else
#define COUNT(n) ((void)0)
#endif

#if AK_ALLOC_SYSTEM

#include <stdlib.h>

void *ak_alloc(size_t n) {
	COUNT(n);
	void *p = calloc(1, n ? n : 1);
	if (!p) ak_panic("out of memory (%zu bytes)", n);
	return p;
}

void *ak_realloc(void *p, size_t n) {
	COUNT(n);
	void *q = realloc(p, n ? n : 1);
	if (!q) ak_panic("out of memory (%zu bytes)", n);
	return q;
}

void ak_free(void *p) { free(p); }

void *ak_alloc_small(size_t n) {
	AK_ASSERT(n <= AK_SMALL_MAX);
	return ak_alloc(n);
}

void ak_free_small(void *p, size_t n) {
	AK_UNUSED(n);
	free(p);
}

void *ak_alloc_raw(size_t n) {
	COUNT(n);
	void *p = malloc(n ? n : 1);
	if (!p) ak_panic("out of memory (%zu bytes)", n);
	return p;
}

void *ak_alloc_small_raw(size_t n) {
	AK_ASSERT(n <= AK_SMALL_MAX);
	return ak_alloc_raw(n);
}

bool ak_alloc_grown(void) { return false; }
void ak_alloc_trim(void) {}

#else

// mimalloc's configuration: no statistics, profiling or sampling (they
// cost on every call); in a build with assertions (tests', players'), its
// basic checks (a block freed twice or that it didn't make, a write past
// a block's end), each a panic (on_error).
#ifdef NDEBUG
#define MI_DEBUG 0
#else
#define MI_DEBUG 1
#endif
#define MI_STATS 0
#define MI_PROFILE 0
#define MI_GUARDED 0
#define MI_SECURE 0
#define MI_NO_CXX_NEW_HANDLER 1  // no C++ (a weak std::get_new_handler)
// Nothing of the process's own: no MIMALLOC_ environment variables (a
// process-wide mimalloc's), and transparent huge pages as the system has
// them, never asked for (on Linux, a 2 MB page for each arena's first
// touch: 4 MB more for a program that allocates little) or turned off for
// the whole process (mimalloc's default on Android: prctl).
#define MI_NO_GETENV 1
#define MI_NO_THP 1
#define MI_DEFAULT_ALLOW_THP 1
#if defined(__SANITIZE_THREAD__)
#define MI_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MI_TSAN 1
#endif
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"  // free.c's debug checks (a false positive)
#endif
#include "base/mimalloc_names.h"
#include "../../third_party/mimalloc/src/static.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static AK_NOINLINE AK_NORETURN void oom(size_t n) { ak_panic("out of memory (%zu bytes)", n); }

#ifndef NDEBUG
// In a build with assertions, what mimalloc's checks find is a panic, not
// a message and a block leaked: a block freed twice, written past its
// end, or freed by ak_free that it didn't make (the C library's: memory
// that crossed between the core and a host the wrong way).
static void on_error(int err, void *arg) {
	AK_UNUSED(arg);
	switch (err) {
	case EAGAIN: ak_panic("the allocator: a block freed twice");
	case EFAULT: ak_panic("the allocator: its memory corrupted (a write past a block's end?)");
	case EINVAL: ak_panic("the allocator: a block freed that it didn't make (the C library's malloc's?)");
	}
}

__attribute__((constructor)) static void on_errors(void) { mi_register_error(on_error, NULL); }
#endif

void *ak_alloc(size_t n) {
	COUNT(n);
	void *p = mi_zalloc(n);
	if (AK_UNLIKELY(!p)) oom(n);
	return p;
}

// A big block grown by ak_realloc (an array or a string that doubles as
// it fills) moves to a new one, and leaves its pages to the system, not
// to the allocator. Freed, they'd stay resident until reused (mimalloc
// purges what's freed a few seconds later, and on macOS a purge, an
// madvise, leaves them counted resident until the system needs them),
// and a growing buffer's never are by it: each is twice the last, so a
// 40 MB array built by pushes held 64 MB of the buffers it outgrew. Its
// whole pages are moved into the new block by the system (macOS's
// mach_vm_remap, Linux's mremap), not copied, as macOS's malloc grows a
// large block (vm_copy): no copy, and no page of the new block faulted
// in for it; the old range is mapped anew, empty. Where the pages can't
// be moved (the blocks don't sit alike in their pages), the bytes are
// copied and the old pages released. Only blocks of AK_GROWN_RELEASE or
// more: a smaller one's pages go back to the allocator, for the blocks
// made next.
#define AK_GROWN_RELEASE ((size_t)1 << 20)

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

// fresh_pages maps [a, b) (whole pages) anew, empty: they read as zeros
// when next touched, and aren't resident till then.
static void fresh_pages(uintptr_t a, uintptr_t b) {
#if defined(__APPLE__)
	// An madvise leaves them counted resident: mapped anew, they're not.
	// Tagged as mimalloc tags its memory, for vmmap.
	void *r = mmap((void *)a, b - a, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, VM_MAKE_TAG(100), 0);
#else
	void *r = mmap((void *)a, b - a, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
	if (r == MAP_FAILED) ak_panic("out of memory (mapping %zu bytes)", (size_t)(b - a));
}

// move_pages moves [a, b) (whole pages) to to, which the pages there
// are replaced by, leaving [a, b) empty: whether it could.
static bool move_pages(uintptr_t a, uintptr_t b, uintptr_t to) {
#if defined(__APPLE__)
	mach_vm_address_t t = to;
	vm_prot_t cur, max;
	if (mach_vm_remap(mach_task_self(), &t, b - a, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(), a, FALSE, &cur, &max,
	                  VM_INHERIT_DEFAULT) != KERN_SUCCESS)
		return false;
	fresh_pages(a, b);  // shared with to's until then
	return true;
#elif defined(__linux__) || defined(__ANDROID__)
	if (mremap((void *)a, b - a, b - a, MREMAP_MAYMOVE | MREMAP_FIXED, (void *)to) == MAP_FAILED) return false;
	fresh_pages(a, b);  // unmapped by the move
	return true;
#else
	AK_UNUSED(a), AK_UNUSED(b), AK_UNUSED(to);
	return false;
#endif
}

// grown_block is a new block of n bytes (n > p's) for p's bytes to move
// to: placed in its pages as p is in its own (blocks this big start
// anywhere in their first page), so that p's whole pages can move whole.
static void *grown_block(const void *p, size_t n) {
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
	size_t ps = _mi_os_page_size();
	return mi_malloc_aligned_at(n, ps, (ps - ((uintptr_t)p & (ps - 1))) & (ps - 1));
#else
	AK_UNUSED(p);
	return mi_malloc(n);
#endif
}

// grown_into puts the n bytes at p into q (a block of its own, as long
// or longer), p's pages moved or released.
static void grown_into(void *q, void *p, size_t n) {
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
	size_t ps = _mi_os_page_size();
	uintptr_t s = (uintptr_t)p, a = (s + ps - 1) & ~(uintptr_t)(ps - 1), b = (s + n) & ~(uintptr_t)(ps - 1);
	if (b > a) {
		uintptr_t d = (uintptr_t)q + (a - s);
		if ((d & (ps - 1)) == 0 && move_pages(a, b, d)) {
			memcpy(q, p, a - s);                         // the head, before p's first whole page
			memcpy((char *)q + (b - s), (void *)b, s + n - b);  // the tail, past its last
			return;
		}
		memcpy(q, p, n);
		fresh_pages(a, b);
		return;
	}
#endif
	memcpy(q, p, n);
}

void *ak_realloc(void *p, size_t n) {
	COUNT(n);
	if (p && n > AK_GROWN_RELEASE) {
		size_t old = mi_usable_size(p);
		if (old >= AK_GROWN_RELEASE && n > old) {
			void *q = grown_block(p, n);
			if (AK_UNLIKELY(!q)) oom(n);
			grown_into(q, p, old);
			mi_free(p);
			return q;
		}
	}
	void *q = mi_realloc(p, n ? n : 1);
	if (AK_UNLIKELY(!q)) oom(n);
	return q;
}

void ak_free(void *p) { mi_free(p); }

// Small blocks are mimalloc's small allocations: its thread-local pages
// are the per-thread cache of freed blocks by size class that small
// blocks need, and a page whose blocks are all freed goes back to the
// system.
void *ak_alloc_small(size_t n) {
	AK_ASSERT(n <= AK_SMALL_MAX);
	COUNT(n);
	void *p = mi_zalloc_small(n);
	if (AK_UNLIKELY(!p)) oom(n);
	return p;
}

void ak_free_small(void *p, size_t n) {
	AK_UNUSED(n);
	mi_free_small(p);
}

void *ak_alloc_raw(size_t n) {
	COUNT(n);
	void *p = mi_malloc(n);
	if (AK_UNLIKELY(!p)) oom(n);
	return p;
}

void *ak_alloc_small_raw(size_t n) {
	AK_ASSERT(n <= AK_SMALL_MAX);
	COUNT(n);
	void *p = mi_malloc_small(n);
	if (AK_UNLIKELY(!p)) oom(n);
	return p;
}

// Giving memory back. A thread's pages whose blocks are all freed stay
// its own, for its next blocks, until it collects (every so many slow
// allocations), and pages collected go back to the system a moment later
// (mimalloc's purge delay): a thread that stops allocating after a spike
// would hold it. ak_alloc_grown says the thread's pages grew since it
// last trimmed (by TRIM_PAGES: 2 MB of 64 KB pages), and ak_alloc_trim
// collects them and gives back what's free now.
#define TRIM_PAGES 32
static AK_TLS size_t trimmed;

bool ak_alloc_grown(void) {
	mi_theap_t *t = _mi_theap_default();
	if (!t || !mi_theap_is_initialized(t)) return false;
	if (t->page_count < trimmed) trimmed = t->page_count;  // it collected itself
	return t->page_count > trimmed + TRIM_PAGES;
}

void ak_alloc_trim(void) {
	mi_collect(true);
	mi_theap_t *t = _mi_theap_default();
	trimmed = t && mi_theap_is_initialized(t) ? t->page_count : 0;
}

#endif
