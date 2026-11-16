// The kitchen package's C, for Windows and Linux: its module.
#include "kitchen.asuka.h"

#include <stdlib.h>

static char *k_name(kitchen_Kitchen *self, asuka_error *err) {
	(void)self, (void)err;
	return asuka_strdup("home");
}

static kitchen_list_Item k_items(kitchen_Kitchen *self, const char *filter, asuka_error *err) {
	(void)self, (void)err;
	kitchen_list_Item l = {calloc(1, sizeof(kitchen_Item)), 1};
	kitchen_Item *it = &l.data[0];
	it->name = asuka_strdup(filter);
	it->weight = 1.5;
	it->unit = kitchen_Unit_gram;
	it->tags.len = 2;
	it->tags.data = malloc(2 * sizeof(char *));
	it->tags.data[0] = asuka_strdup("dry");
	it->tags.data[1] = asuka_strdup("fine");
	return l;
}

static void k_release(kitchen_Kitchen *self) { free(self); }

static const kitchen_Kitchen_vtable kitchen_vt = {.release = k_release, .name = k_name, .items = k_items};

kitchen_Kitchen *kitchen_make_Kitchen(asuka_error *err) {
	(void)err;
	kitchen_Kitchen *k = malloc(sizeof *k);
	k->vt = &kitchen_vt;
	return k;
}
