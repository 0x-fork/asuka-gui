// testing.h: what asuka-run adds to test blocks (docs/testing.md):
// printing values (show.c), property tests and fuzzing (prop.c),
// benchmarks (bench.c), snapshots (snap.c) and coverage's counters
// (cover.c). main.c runs them.
#ifndef AK_RUNNER_TESTING_H
#define AK_RUNNER_TESTING_H

#include "vm/vm.h"
#include "rt/json.h"
#include "rt/rt.h"

#include <stdio.h>

// ---- main.c: one run of a test's function ----

// failure is how the run being made failed ("": it didn't).
extern char failure[2048];
// current is the test running (NULL: none).
extern const char *current;
// ak_runner_run runs f with args (borrowed) as main.c runs a test: in an
// owner and an app directory of its own, settled, cleaned up after.
// failure says how it failed.
void ak_runner_run(const ak_func *f, ak_val *args, uint32_t nargs);
// ak_runner_module is the module running; ak_runner_types, its types
// for descriptors (rt/json.h).
extern ak_module *ak_runner_module;
void ak_runner_types(ak_json_types *t);

// ---- JSON lines on stdout, which asuka test reads ----

void ak_jstr(ak_strb *b, const char *s, size_t n);  // a JSON string
void ak_jline(ak_strb *b);                           // writes b and a newline, flushed, and frees it

// ---- show.c ----

// ak_show writes v, of the descriptor's type (rt/json.h, and d a
// duration, c a color), as asuka test prints values: as the language
// writes them, a map's entries and a set's elements sorted, a line per
// item when it doesn't fit on one.
void ak_show(ak_strb *b, ak_val v, const char *desc, const ak_json_types *t);
ak_kind ak_desc_kind(const char *d);
const char *ak_desc_end(const char *d);
// ak_show_builtins_init registers test.show.
void ak_show_builtins_init(void);

// ---- prop.c ----

typedef struct {
	uint64_t seed;      // the base seed (asuka test --seed)
	uint32_t cases;     // generated cases per property test
	bool fuzz;          // fuzz instead: until fuzz_ns or fuzz_cases
	int64_t fuzz_ns;    // how long to fuzz each test (0: no limit but fuzz_cases)
	uint64_t fuzz_cases;
	const char *corpus; // a file of "test\tpath" lines: inputs every run tries first
} ak_prop_options;

// ak_prop_run runs module test i, a property test, as o says: its
// corpus's inputs, then its generated cases (or fuzzing); on a failure it
// shrinks the input and writes it as a JSON line, and sets failure.
void ak_prop_run(ak_module *m, uint32_t i, const ak_prop_options *o);

// ---- bench.c ----

typedef struct {
	int64_t time_ns;  // how long each bench's samples take together
	uint64_t n;       // or exactly this many iterations, once (0: time_ns)
	uint32_t samples;
} ak_bench_options;

void ak_bench_run(ak_module *m, uint32_t i, const ak_bench_options *o);
void ak_bench_builtins_init(void);

// ---- snap.c ----

typedef struct {
	const char *file;  // the stored snapshots: "key\0value\0" records
	bool update;       // write what differs (asuka test --update)
	bool ci;           // a snapshot not stored yet fails (in CI)
} ak_snap_options;

void ak_snap_init(const ak_snap_options *o);
void ak_snap_begin(const char *test, bool property);  // a test starts: its snapshots count from 1
void ak_snap_builtins_init(void);

// ---- cover.c ----

void ak_cover_builtins_init(void);
// ak_cover_counts are the counters' counts so far, n of them.
const uint64_t *ak_cover_counts(uint32_t *n);
bool ak_cover_write(const char *path);

#endif
