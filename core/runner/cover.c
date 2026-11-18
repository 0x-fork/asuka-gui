// cover.c: coverage's counters (asuka test --cover). A module compiled
// with coverage calls cover.hit(i) as statement i runs (lang/ir/cover.go);
// asuka-run -cover file writes the counts, a line each, as it ends, and
// the fuzzer reads them as cases run (prop.c).
#include "testing.h"

#include <stdio.h>
#include <string.h>

static uint64_t *counts;
static uint32_t ncounts, cap;

static ak_val cover_hit(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t i = args[0].i;
	if (i < 0) return (ak_val){0};
	if ((uint64_t)i >= cap) {
		uint32_t c = cap ? cap : 256;
		while (c <= (uint64_t)i) c *= 2;
		counts = ak_realloc(counts, sizeof(uint64_t) * c);
		memset(counts + cap, 0, sizeof(uint64_t) * (c - cap));
		cap = c;
	}
	if ((uint32_t)i >= ncounts) ncounts = (uint32_t)i + 1;
	counts[i]++;
	return (ak_val){0};
}

void ak_cover_builtins_init(void) { ak_builtin_register("cover.hit", cover_hit); }

const uint64_t *ak_cover_counts(uint32_t *n) {
	*n = ncounts;
	return counts;
}

bool ak_cover_write(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	for (uint32_t i = 0; i < ncounts; i++) fprintf(f, "%llu\n", (unsigned long long)counts[i]);
	return fclose(f) == 0;
}
