// diag.c: run-time diagnostics (rfcs/0015): their handler, their format,
// and the sites reported so far.
#include "base/base.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

bool ak_diag_enabled;

static ak_diag_fn handler;
static void *handler_ctx;

void ak_diag_set_handler(ak_diag_fn fn, void *ctx) {
	handler = fn;
	handler_ctx = ctx;
}

void ak_diag_format(char *buf, size_t n, const char *code, const char *msg, const char *const *where, int nwhere) {
	size_t at = 0;
#define PUT(...) do { if (at < n) { int k = snprintf(buf + at, n - at, __VA_ARGS__); if (k > 0) at += (size_t)k; } } while (0)
	PUT("warning[%s]: %s", code, msg);
	for (int i = 0; i < nwhere; i++) PUT("\n  --> %s", where[i]);
	PUT("\n  = see https://asuka.egoist.dev/docs/errors/%s", code);
#undef PUT
}

void ak_diag(const char *code, const char *msg, const char *const *where, int nwhere) {
	if (handler) {
		handler(handler_ctx, code, msg, where, nwhere);
		return;
	}
	char buf[8192];
	ak_diag_format(buf, sizeof buf, code, msg, where, nwhere);
	fprintf(stderr, "%s\n", buf);
	fflush(stderr);
}

// The sites reported: (site, code) pairs, an open-addressed table that
// grows. Only a report adds to it, which is rare: never in the steady
// state.
typedef struct {
	const void *site;
	const char *code;
} reported;

static reported *seen;
static uint32_t nseen, capseen;

static uint32_t slot(const void *site, const char *code, uint32_t cap) {
	uint64_t h = (uint64_t)(uintptr_t)site * 0x9E3779B97F4A7C15ull;
	for (const char *c = code; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001B3ull;  // the code's text: a literal may be at two addresses
	return (uint32_t)(h >> 17) & (cap - 1);
}

static bool same(const reported *r, const void *site, const char *code) {
	return r->site == site && (r->code == code || strcmp(r->code, code) == 0);
}

bool ak_diag_first(const void *site, const char *code) {
	if (nseen * 2 >= capseen) {
		uint32_t cap = capseen ? capseen * 2 : 64;
		reported *t = ak_alloc(sizeof *t * cap);
		for (uint32_t i = 0; i < capseen; i++) {
			if (!seen[i].site) continue;
			uint32_t j = slot(seen[i].site, seen[i].code, cap);
			while (t[j].site) j = (j + 1) & (cap - 1);
			t[j] = seen[i];
		}
		ak_free(seen);
		seen = t;
		capseen = cap;
	}
	uint32_t j = slot(site, code, capseen);
	while (seen[j].site) {
		if (same(&seen[j], site, code)) return false;
		j = (j + 1) & (capseen - 1);
	}
	seen[j] = (reported){site, code};
	nseen++;
	return true;
}

void ak_diag_reset(void) {
	if (seen) memset(seen, 0, sizeof *seen * capseen);
	nseen = 0;
}

void ak_panic_code(const char *code, const char *fmt, ...) {
	char msg[960];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	ak_panic("error[%s]: %s", code, msg);
}
