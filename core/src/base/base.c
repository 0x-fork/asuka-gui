#include "base.h"
#include "base/thread.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

AK_TLS uint64_t ak_alloc_count;

void *ak_alloc(size_t n) {
	ak_alloc_count++;
	void *p = calloc(1, n ? n : 1);
	if (!p) ak_panic("out of memory (%zu bytes)", n);
	return p;
}

void *ak_realloc(void *p, size_t n) {
	ak_alloc_count++;
	void *q = realloc(p, n ? n : 1);
	if (!q) ak_panic("out of memory (%zu bytes)", n);
	return q;
}

void ak_free(void *p) { free(p); }

void (*ak_panic_hook)(const char *msg);

// The thread reporting a panic (0: none). One panic is reported at a
// time: its message, the hook's record (platform/crash's .crash file, in
// buffers of the process's) are written whole, by the thread whose
// panic it is, and the process ends with it.
static _Atomic uint64_t panicking;

void ak_panic(const char *fmt, ...) {
	static char msg[1024];
	uint64_t me = ak_thread_id(), none = 0;
	if (!atomic_compare_exchange_strong(&panicking, &none, me)) {
		// A panic in the hook, on the thread reporting one: end now.
		if (none == me) abort();
		// Another thread's is being reported: it ends the process.
		ak_mutex m = AK_MUTEX_INIT;
		ak_cond c;
		ak_cond_init(&c);
		ak_mutex_lock(&m);
		for (;;) ak_cond_wait(&c, &m);
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	fprintf(stderr, "asuka: panic: %s\n", msg);
	if (ak_panic_hook) ak_panic_hook(msg);
	abort();
}

void *ak_vec_grow(void *data, uint32_t *cap, uint32_t len, uint64_t n, size_t elem) {
	uint64_t need = (uint64_t)len + n;
	if (need > UINT32_MAX) ak_panic("out of memory (a vector of %llu elements)", (unsigned long long)need);
	uint64_t c = *cap ? (uint64_t)*cap * 2 : 8;
	while (c < need) c *= 2;  // u64: no wrap, need <= 2^32
	if (c > UINT32_MAX) c = UINT32_MAX;
	if (c > SIZE_MAX / elem) ak_panic("out of memory (a vector of %llu elements)", (unsigned long long)c);
	data = ak_realloc(data, (size_t)c * elem);
	*cap = (uint32_t)c;
	return data;
}

void ak_buf_reserve(ak_buf *b, size_t n) {
	if (n <= b->cap - b->len) return;  // len <= cap: no wrap
	if (n > SIZE_MAX - b->len) ak_panic("out of memory (a buffer of %zu + %zu bytes)", b->len, n);
	size_t need = b->len + n;
	size_t c = b->cap ? (b->cap > SIZE_MAX / 2 ? need : b->cap * 2) : 256;
	while (c < need) c = c > SIZE_MAX / 2 ? need : c * 2;
	b->data = ak_realloc(b->data, c);
	b->cap = c;
}

void ak_buf_free(ak_buf *b) {
	ak_free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

void ak_buf_put(ak_buf *b, const void *p, size_t n) {
	ak_buf_reserve(b, n);
	if (n) memcpy(b->data + b->len, p, n);
	b->len += n;
}

void ak_buf_u16(ak_buf *b, uint16_t v) {
	uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
	ak_buf_put(b, t, 2);
}

void ak_buf_u32(ak_buf *b, uint32_t v) {
	uint8_t t[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
	ak_buf_put(b, t, 4);
}

void ak_buf_u64(ak_buf *b, uint64_t v) {
	ak_buf_u32(b, (uint32_t)v);
	ak_buf_u32(b, (uint32_t)(v >> 32));
}

void ak_buf_f32(ak_buf *b, float v) {
	uint32_t u;
	memcpy(&u, &v, 4);
	ak_buf_u32(b, u);
}

void ak_buf_f64(ak_buf *b, double v) {
	uint64_t u;
	memcpy(&u, &v, 8);
	ak_buf_u64(b, u);
}

void ak_buf_str(ak_buf *b, const char *s, size_t n) {
	ak_buf_u32(b, (uint32_t)n);
	ak_buf_put(b, s, n);
}

void ak_buf_printf(ak_buf *b, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char tmp[256];
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if ((size_t)n < sizeof tmp) {
		ak_buf_put(b, tmp, (size_t)n);
		return;
	}
	ak_buf_reserve(b, (size_t)n + 1);
	va_start(ap, fmt);
	vsnprintf((char *)b->data + b->len, (size_t)n + 1, fmt, ap);
	va_end(ap);
	b->len += (size_t)n;
}

static bool need(ak_reader *r, size_t n) {
	if (r->err || r->len - r->off < n) {
		r->err = true;
		return false;
	}
	return true;
}

uint8_t ak_read_u8(ak_reader *r) {
	if (!need(r, 1)) return 0;
	return r->p[r->off++];
}

uint16_t ak_read_u16(ak_reader *r) {
	if (!need(r, 2)) return 0;
	const uint8_t *p = r->p + r->off;
	r->off += 2;
	return (uint16_t)(p[0] | p[1] << 8);
}

uint32_t ak_read_u32(ak_reader *r) {
	if (!need(r, 4)) return 0;
	const uint8_t *p = r->p + r->off;
	r->off += 4;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint64_t ak_read_u64(ak_reader *r) {
	uint64_t lo = ak_read_u32(r);
	uint64_t hi = ak_read_u32(r);
	return lo | hi << 32;
}

float ak_read_f32(ak_reader *r) {
	uint32_t u = ak_read_u32(r);
	float f;
	memcpy(&f, &u, 4);
	return f;
}

double ak_read_f64(ak_reader *r) {
	uint64_t u = ak_read_u64(r);
	double f;
	memcpy(&f, &u, 8);
	return f;
}

const uint8_t *ak_read_bytes(ak_reader *r, uint32_t *n) {
	uint32_t len = ak_read_u32(r);
	if (!need(r, len)) {
		*n = 0;
		return NULL;
	}
	const uint8_t *p = r->p + r->off;
	r->off += len;
	*n = len;
	return p;
}
