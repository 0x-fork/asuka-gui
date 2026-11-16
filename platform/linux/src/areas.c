// areas.c: the areas the host asks in turn (host.h's area).
#include "host.h"

extern const area lx_area_controls, lx_area_anim, lx_area_split;

const area *const lx_areas[] = {
	&lx_area_split,
	&lx_area_controls,
	&lx_area_anim,
};
const size_t lx_area_count = AK_LEN(lx_areas);
