// fuzz.h: events from a host that sends anything, for the UI tests: a
// mounted app's views get random kinds of events with random payloads,
// now and then on a view gone or none, now and then bytes that aren't an
// event at all. The core takes them without reading or writing out of
// bounds (ASan, UBSan), and its views all go with the app.
//
// AK_FUZZ_SEED and AK_FUZZ_EVENTS (per app) search further than a test's
// own: AK_FUZZ_SEED=7 AK_FUZZ_EVENTS=200000 go test -count=1 ./core -run TestC/Nav
#ifndef AK_TEST_FUZZ_H
#define AK_TEST_FUZZ_H

#include "t.h"
#include "fake/uitest.h"
#include "ui/ui_int.h"

static uint64_t fuzz_state;

static inline uint64_t fuzz_next(void) {
	uint64_t x = fuzz_state;
	x ^= x << 13, x ^= x >> 7, x ^= x << 17;
	return fuzz_state = x;
}

static inline uint32_t fuzz_n(uint32_t n) { return (uint32_t)(fuzz_next() % n); }

// fuzz_seed starts the sequence (AK_FUZZ_SEED's, else the test's) and
// says how many events each app gets (AK_FUZZ_EVENTS, else n).
static inline int fuzz_seed(uint64_t seed, int n) {
	const char *s = getenv("AK_FUZZ_SEED"), *e = getenv("AK_FUZZ_EVENTS");
	fuzz_state = (s ? strtoull(s, NULL, 0) : seed) | 1;
	return e ? atoi(e) : n;
}

static inline int64_t fuzz_int(void) {
	static const int64_t edges[] = {0, 1, -1, 2, 3, 7, 100, INT32_MAX, INT32_MIN, INT64_MAX, INT64_MIN, (int64_t)1 << 32, AK_SCREEN_TOOLBAR_ACTIONS + 1, AK_SCREEN_PRIMARY_ACTION};
	return fuzz_n(4) ? edges[fuzz_n(AK_LEN(edges))] : (int64_t)fuzz_next();
}

static inline double fuzz_float(void) {
	static const double edges[] = {0, -0.0, 1, -1, 0.5, 1e9, -1e9, 1e300, NAN, INFINITY, -INFINITY, 390, 844};
	return fuzz_n(3) ? edges[fuzz_n(AK_LEN(edges))] : (double)(int32_t)fuzz_next() / 7.0;
}

static uint8_t fuzz_bytes[64];

// fuzz_span is random bytes, or a payload as the core's decoders read it
// (a count, then items) gone wrong, or text (UTF-8, some of it cut).
static inline ak_span fuzz_span(void) {
	uint32_t n = fuzz_n(sizeof fuzz_bytes + 1);
	for (uint32_t i = 0; i < n; i++) fuzz_bytes[i] = (uint8_t)fuzz_next();
	switch (fuzz_n(3)) {
	case 0:
		if (n >= 4) {
			uint32_t count = fuzz_n(4) ? fuzz_n(4) : (uint32_t)fuzz_next();
			memcpy(fuzz_bytes, &count, 4);
		}
		break;
	case 1: {
		static const char *const texts[] = {"a", "héllo", "日本語", "👩‍👩‍👧", "\xf0\x9f", "\xc3", "ab\0c"};
		const char *t = texts[fuzz_n(AK_LEN(texts))];
		n = (uint32_t)strlen(t);
		memcpy(fuzz_bytes, t, n);
		break;
	}
	}
	return (ak_span){(const char *)fuzz_bytes, n};
}

static int fuzz_failures;

static inline void fuzz_fail(void *ctx, const char *msg) {
	AK_UNUSED(ctx);
	if (fuzz_failures++ < 5) T_FAIL("fake host: %s", msg);
}

// fuzz_events sends n events to a's views.
static inline void fuzz_events(ak_uitest *a, int n) {
	ak_fake *f = ak_uitest_host(a);
	f->fail_fn = fuzz_fail;
	fuzz_failures = 0;
	ak_buf b = {0};
	for (int i = 0; i < n; i++) {
		b.len = 0;
		if (fuzz_n(10) == 0) {
			ak_span s = fuzz_span();
			ak_ui_dispatch((const uint8_t *)s.p, s.len);
			continue;
		}
		ak_id ids[256];
		uint32_t nids = 0;
		for (uint32_t k = 0; k < f->slots_cap && nids < AK_LEN(ids); k++)
			if (f->slots[k]) ids[nids++] = f->slots[k]->id;
		ak_event ev = {0};
		ev.id = nids && fuzz_n(16) ? ids[fuzz_n(nids)] : fuzz_n(2) ? 0 : (ak_id)fuzz_next();
		ev.kind = (ak_event_kind)fuzz_n(AK_EVENT_COUNT);
		ev.b = fuzz_n(2);
		ev.i = fuzz_int();
		ev.f = fuzz_float();
		ev.x = fuzz_float(), ev.y = fuzz_float();
		ev.s = fuzz_span();
		ev.env = f->env;
		if (fuzz_n(2)) ev.env.width = (float)fuzz_float(), ev.env.height = (float)fuzz_float();
		if (fuzz_n(4) == 0) ev.env.font_scale = (float)fuzz_float(), ev.env.scale = (float)fuzz_float();
		ev.g = (ak_gesture){(uint8_t)fuzz_n(6), (float)fuzz_float(), (float)fuzz_float(), (float)fuzz_float(), (float)fuzz_float(), (float)fuzz_float(), (float)fuzz_float(), (float)fuzz_float(), (float)fuzz_float()};
		ak_event_append(&b, &ev);
		ak_ui_dispatch(b.data, b.len);
		if (fuzz_n(8) == 0) ak_ui_flush();
		if (fuzz_n(8) == 0) ak_sched_run_posted();
	}
	ak_buf_free(&b);
	f->fail_fn = NULL;
}

#endif
