// bench.c: benchmarks (docs/testing.md#benchmarks). A bench's function
// takes how many times to run what it measures; bench.start and
// bench.stop, around that loop, read the clock and the allocations
// (ak_alloc_count, ak_alloc_bytes) so that its setup isn't counted.
//
// As Go's testing does, the count is calibrated: one iteration, then as
// many as the last run says fit in a sample's time (at most 100 times
// more each run), until a run takes that long. Then the samples, each of
// that many iterations; what's said is their median per iteration, and
// how far the fastest and the slowest are from it. Wall-clock time is the
// measurement: a bench never passes or fails on it, only on what it
// checks. Its result is a JSON line asuka test reads.
//
// A bench that drives an app on the fake host also says what crossed to
// the host per iteration: the batches applied, their ops and bytes, and
// the views measured (ak_fake_totals), the work a real host would do;
// and the layout nodes the core computed (ak_lnode_computed). The fake
// host keeps no log of ops while it measures (ak_uitest_log_ops): what's
// timed isn't keeping every iteration's.
#include "testing.h"
#include "base/thread.h"
#include "fake/fake.h"
#include "fake/uitest.h"
#include "layout/layout.h"

#include <stdlib.h>
#include <string.h>

static struct {
	int64_t start, ns;
	uint64_t allocs, bytes;  // at the start, then between start and stop
	ak_fake_work host;       // likewise
	uint64_t layouts;        // likewise
	bool started, stopped;
} clock_;

static ak_val bench_start(ak_val *args, uint32_t n) {
	AK_UNUSED(args);
	AK_UNUSED(n);
	clock_.started = true;
	clock_.stopped = false;
	ak_uitest_log_ops(false);
	clock_.allocs = ak_alloc_count;
	clock_.bytes = ak_alloc_bytes;
	clock_.host = ak_fake_totals;
	clock_.layouts = ak_lnode_computed;
	clock_.start = ak_now_ns();
	return (ak_val){0};
}

static ak_val bench_stop(ak_val *args, uint32_t n) {
	AK_UNUSED(args);
	AK_UNUSED(n);
	int64_t now = ak_now_ns();
	ak_uitest_log_ops(true);
	if (!clock_.started) return (ak_val){0};
	clock_.ns = now - clock_.start;
	clock_.allocs = ak_alloc_count - clock_.allocs;
	clock_.bytes = ak_alloc_bytes - clock_.bytes;
	clock_.host.applies = ak_fake_totals.applies - clock_.host.applies;
	clock_.host.ops = ak_fake_totals.ops - clock_.host.ops;
	clock_.host.bytes = ak_fake_totals.bytes - clock_.host.bytes;
	clock_.host.measures = ak_fake_totals.measures - clock_.host.measures;
	clock_.layouts = ak_lnode_computed - clock_.layouts;
	clock_.stopped = true;
	return (ak_val){0};
}

void ak_bench_builtins_init(void) {
	ak_builtin_register("bench.start", bench_start);
	ak_builtin_register("bench.stop", bench_stop);
}

typedef struct {
	int64_t ns;
	uint64_t allocs, bytes;
	ak_fake_work host;
	uint64_t layouts;
} sample;

// once runs the bench's function with n iterations; false when it failed
// (failure says how) or didn't reach its measure's end.
static bool once(const ak_func *f, uint64_t n, sample *s) {
	clock_.started = clock_.stopped = false;
	ak_val arg = {.i = (int64_t)n};
	ak_runner_run(f, &arg, 1);
	if (failure[0]) return false;
	if (!clock_.stopped) {
		snprintf(failure, sizeof failure, "it left before the end of what it measures (a return in it?)");
		return false;
	}
	*s = (sample){clock_.ns, clock_.allocs, clock_.bytes, clock_.host, clock_.layouts};
	return true;
}

static int cmp_sample(const void *a, const void *b) {
	double x = ((const double *)a)[0], y = ((const double *)b)[0];
	return x < y ? -1 : x > y;
}

void ak_bench_run(ak_module *m, uint32_t i, const ak_bench_options *o) {
	uint32_t fi;
	const char *name = ak_module_test(m, i, &fi);
	const ak_func *f = ak_module_func(m, fi);
	failure[0] = 0;
	uint32_t samples = o->n ? 1 : (o->samples ? o->samples : 10);
	uint64_t n = o->n;
	sample s;
	if (!n) {
		// Calibrate: a sample's share of the time.
		int64_t target = o->time_ns / samples;
		if (target < 1000000) target = 1000000;
		n = 1;
		for (;;) {
			if (!once(f, n, &s)) return;
			if (s.ns >= target || n >= 1000000000ULL) break;
			int64_t per = s.ns / (int64_t)n;
			if (per < 1) per = 1;
			uint64_t next = (uint64_t)(target / per);
			next += next / 5;  // a little over: the next run is likely the last
			if (next > n * 100) next = n * 100;
			if (next <= n) next = n + 1;
			if (next > 1000000000ULL) next = 1000000000ULL;
			n = next;
		}
	}
	double *ns = ak_alloc(sizeof(double) * samples);
	double allocs = 0, bytes = 0, applies = 0, ops = 0, host_bytes = 0, measures = 0, layouts = 0;
	for (uint32_t k = 0; k < samples; k++) {
		if (!once(f, n, &s)) {
			ak_free(ns);
			return;
		}
		ns[k] = (double)s.ns / (double)n;
		allocs += (double)s.allocs / (double)n;
		bytes += (double)s.bytes / (double)n;
		applies += (double)s.host.applies / (double)n;
		ops += (double)s.host.ops / (double)n;
		host_bytes += (double)s.host.bytes / (double)n;
		measures += (double)s.host.measures / (double)n;
		layouts += (double)s.layouts / (double)n;
	}
	allocs /= samples;
	bytes /= samples;
	applies /= samples;
	ops /= samples;
	host_bytes /= samples;
	measures /= samples;
	layouts /= samples;
	qsort(ns, samples, sizeof *ns, cmp_sample);
	double median = samples % 2 ? ns[samples / 2] : (ns[samples / 2 - 1] + ns[samples / 2]) / 2;
	ak_strb b = {0};
	ak_strb_cstr(&b, "{\"bench\":", 9);
	ak_jstr(&b, name, strlen(name));
	ak_strb_cstr(&b, ",\"n\":", 5);
	ak_strb_int(&b, (int64_t)n);
	ak_strb_cstr(&b, ",\"samples\":", 11);
	ak_strb_int(&b, samples);
	ak_strb_cstr(&b, ",\"nsPerOp\":", 11);
	ak_strb_float(&b, median);
	ak_strb_cstr(&b, ",\"minNsPerOp\":", 14);
	ak_strb_float(&b, ns[0]);
	ak_strb_cstr(&b, ",\"maxNsPerOp\":", 14);
	ak_strb_float(&b, ns[samples - 1]);
	ak_strb_cstr(&b, ",\"allocsPerOp\":", 15);
	ak_strb_float(&b, allocs);
	ak_strb_cstr(&b, ",\"bytesPerOp\":", 14);
	ak_strb_float(&b, bytes);
	if (applies > 0 || measures > 0 || layouts > 0) {
		ak_strb_cstr(&b, ",\"appliesPerOp\":", 16);
		ak_strb_float(&b, applies);
		ak_strb_cstr(&b, ",\"hostOpsPerOp\":", 16);
		ak_strb_float(&b, ops);
		ak_strb_cstr(&b, ",\"hostBytesPerOp\":", 18);
		ak_strb_float(&b, host_bytes);
		ak_strb_cstr(&b, ",\"measuresPerOp\":", 17);
		ak_strb_float(&b, measures);
		ak_strb_cstr(&b, ",\"layoutsPerOp\":", 16);
		ak_strb_float(&b, layouts);
	}
	ak_strb_cstr(&b, "}", 1);
	ak_jline(&b);
	ak_free(ns);
}
