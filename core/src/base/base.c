#include "base.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef AK_TEST
uint64_t ak_alloc_count;
#define AK_COUNT_ALLOC() (ak_alloc_count++)
#else
#define AK_COUNT_ALLOC() ((void)0)
#endif

void *ak_alloc(size_t n) {
	AK_COUNT_ALLOC();
	void *p = calloc(1, n ? n : 1);
	if (!p) ak_panic("out of memory (%zu bytes)", n);
	return p;
}

void *ak_realloc(void *p, size_t n) {
	AK_COUNT_ALLOC();
	void *q = realloc(p, n ? n : 1);
	if (!q) ak_panic("out of memory (%zu bytes)", n);
	return q;
}

void ak_free(void *p) { free(p); }

void ak_panic(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fputs("asuka: panic: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	abort();
}

void ak_buf_reserve(ak_buf *b, size_t n) {
	if (b->len + n <= b->cap) return;
	size_t c = b->cap ? b->cap * 2 : 256;
	while (c < b->len + n) c *= 2;
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
