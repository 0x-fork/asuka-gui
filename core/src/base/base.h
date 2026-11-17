// base.h: what every file of the core includes. C17, main thread only
// unless a function says otherwise.
#ifndef AK_BASE_H
#define AK_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define AK_NAN ((float)NAN)
#define AK_INF ((float)INFINITY)

static inline bool ak_isnan(float f) { return f != f; }
static inline float ak_or0(float f) { return ak_isnan(f) ? 0 : f; }
static inline float ak_minf(float a, float b) { return a < b ? a : b; }
static inline float ak_maxf(float a, float b) { return a > b ? a : b; }

#define AK_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define AK_UNUSED(x) ((void)(x))

#if defined(__GNUC__) || defined(__clang__)
#define AK_LIKELY(x) __builtin_expect(!!(x), 1)
#define AK_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define AK_NORETURN __attribute__((noreturn))
#define AK_NOINLINE __attribute__((noinline))
#define AK_ALWAYS_INLINE inline __attribute__((always_inline))
#define AK_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define AK_LIKELY(x) (x)
#define AK_UNLIKELY(x) (x)
#define AK_NORETURN
#define AK_NOINLINE
#define AK_ALWAYS_INLINE inline
#define AK_PRINTF(f, a)
#endif

// AK_TLS marks a variable each thread has its own of: the VM's state,
// which spawn's worker threads run on too.
#if defined(_MSC_VER)
#define AK_TLS __declspec(thread)
#else
#define AK_TLS _Thread_local
#endif

// ak_stack_low reports whether this thread's C stack is nearly used up:
// code that recurses (Asuka compiled to C, a call from C into the VM)
// throws a stack overflow then, rather than overflowing it.
extern AK_TLS uintptr_t ak_stack_lim;
uintptr_t ak_stack_init(void);
#if defined(__wasm__)
// On the web the C stack (in the core's memory) isn't the only one: the
// engine runs wasm's frames on its own stack, JavaScript's, which can
// run out first. Past each step the C stack grows, below ak_stack_ask
// (or back above ak_stack_reask), ak_stack_engine asks the page.
extern uintptr_t ak_stack_ask, ak_stack_reask;
bool ak_stack_engine(uintptr_t sp);
#endif
static inline bool ak_stack_low(void) {
	uintptr_t lim = ak_stack_lim;
	if (AK_UNLIKELY(!lim)) lim = ak_stack_init();
	uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
#if defined(__wasm__)
	if (AK_UNLIKELY(sp < ak_stack_ask || sp > ak_stack_reask)) return ak_stack_engine(sp);
#endif
	return sp < lim;
}

// Allocation. Never returns NULL: running out of memory aborts.
void *ak_alloc(size_t n);  // zeroed
void *ak_realloc(void *p, size_t n);
void ak_free(void *p);
// ak_alloc_count counts the calling thread's calls to ak_alloc and
// ak_realloc, so tests can assert that a hot path allocates nothing; each
// thread's own, so other threads (spawn's workers, the native side's)
// neither race with it nor add to what a test counts.
extern AK_TLS uint64_t ak_alloc_count;

// ak_panic reports a bug in the core or the app and aborts. Any thread:
// the first to panic reports and aborts; one that panics as it does waits
// for the process to end.
AK_NORETURN void ak_panic(const char *fmt, ...) AK_PRINTF(1, 2);
// ak_panic_hook, when set, gets a panic's message before the core aborts
// (platform/crash's record keeps it).
extern void (*ak_panic_hook)(const char *msg);

// Run-time diagnostics (rfcs/0015): what the core reports of the app as
// it runs, each with its code in lang/diag's table (load-waterfall,
// long-hold...). The detectors run only when ak_diag_enabled, which the
// development hosts (asuka dev's players) and the fake host (asuka test,
// asuka run) set: off, each costs one predictable branch, outside the
// loops over the graph's nodes, and the bookkeeping they need isn't kept.
extern bool ak_diag_enabled;

// ak_diag reports a diagnostic: its code, a message, and the places in
// the app's source it concerns (each a site's label: "Profile's load at
// profile.asuka:8"). It goes to the handler (by default: stderr, which
// a player sends to asuka dev with its logs). Main thread.
void ak_diag(const char *code, const char *msg, const char *const *where, int nwhere);
typedef void (*ak_diag_fn)(void *ctx, const char *code, const char *msg, const char *const *where, int nwhere);
// ak_diag_set_handler sets where diagnostics go (NULL: stderr).
void ak_diag_set_handler(ak_diag_fn fn, void *ctx);
// ak_diag_format writes a diagnostic as a terminal shows it into buf:
//
//	warning[long-hold]: Profile's load has held …
//	  --> Profile's load at profile.asuka:8
//	  = see https://asuka.egoist.dev/docs/errors/long-hold
void ak_diag_format(char *buf, size_t n, const char *code, const char *msg, const char *const *where, int nwhere);
// ak_diag_first reports whether site (a static address: a function of
// the app's) hasn't had code reported in this run yet, and notes that it
// has: each is reported once per site, so a list's thousand rows say it
// once. ak_diag_reset starts a run (a test, a reload) afresh.
bool ak_diag_first(const void *site, const char *code);
void ak_diag_reset(void);
// ak_panic_code is ak_panic of a diagnostic: the message says its code
// (error[effect-loop]: …), in every build, so a crash report says which.
AK_NORETURN void ak_panic_code(const char *code, const char *fmt, ...) AK_PRINTF(2, 3);

#define AK_ASSERT(c) do { if (AK_UNLIKELY(!(c))) ak_panic("%s:%d: assertion failed: %s", __FILE__, __LINE__, #c); } while (0)

// A growable array: `AK_VEC(T) v = {0};` then ak_vec_push(&v, x).
#define AK_VEC(T) struct { T *data; uint32_t len, cap; }
#define ak_vec_reserve(v, n) do { \
	if ((uint64_t)(v)->len + (n) > (v)->cap) (v)->data = ak_vec_grow((v)->data, &(v)->cap, (v)->len, (n), sizeof(*(v)->data)); \
	} while (0)
// ak_vec_grow reallocates a vector's data for len + n elements of size
// elem, doubling its capacity; past what a u32 counts, or a size_t's
// bytes, is out of memory.
void *ak_vec_grow(void *data, uint32_t *cap, uint32_t len, uint64_t n, size_t elem);
#define ak_vec_push(v, x) do { ak_vec_reserve(v, 1); (v)->data[(v)->len++] = (x); } while (0)
#define ak_vec_pop(v) ((v)->data[--(v)->len])
#define ak_vec_free(v) do { ak_free((v)->data); (v)->data = NULL; (v)->len = (v)->cap = 0; } while (0)

// A byte buffer, reused across writes.
typedef struct {
	uint8_t *data;
	size_t len, cap;
} ak_buf;

void ak_buf_reserve(ak_buf *b, size_t n);
static inline void ak_buf_reset(ak_buf *b) { b->len = 0; }
void ak_buf_free(ak_buf *b);
void ak_buf_put(ak_buf *b, const void *p, size_t n);
static inline void ak_buf_u8(ak_buf *b, uint8_t v) { ak_buf_reserve(b, 1); b->data[b->len++] = v; }
void ak_buf_u16(ak_buf *b, uint16_t v);
void ak_buf_u32(ak_buf *b, uint32_t v);
void ak_buf_u64(ak_buf *b, uint64_t v);
void ak_buf_f32(ak_buf *b, float v);
void ak_buf_f64(ak_buf *b, double v);
void ak_buf_str(ak_buf *b, const char *s, size_t n);  // u32 length + bytes
// ak_buf_printf appends formatted text (no terminating NUL).
void ak_buf_printf(ak_buf *b, const char *fmt, ...) AK_PRINTF(2, 3);

// Little-endian readers over a byte span; reading past the end sets
// r->err and returns 0.
typedef struct {
	const uint8_t *p;
	size_t len, off;
	bool err;
} ak_reader;

uint8_t ak_read_u8(ak_reader *r);
uint16_t ak_read_u16(ak_reader *r);
uint32_t ak_read_u32(ak_reader *r);
uint64_t ak_read_u64(ak_reader *r);
float ak_read_f32(ak_reader *r);
double ak_read_f64(ak_reader *r);
// ak_read_bytes returns a pointer into the span and its length in *n.
const uint8_t *ak_read_bytes(ak_reader *r, uint32_t *n);

#endif
