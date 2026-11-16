// The greeter's Badge on Linux: a GtkLabel showing its name; it says what
// it shows.
#include "greeter.asuka.h"

#include "asuka_linux.h"

#include <stdlib.h>

typedef struct {
	greeter_Badge base;
	greeter_Badge_events events;
} badge;

static void b_destroy(greeter_Badge *self) { free(self); }

static void b_name(greeter_Badge *self, const char *name) {
	badge *b = (badge *)self;
	char *text = asuka_sprintf("Linux badge for %s", name);
	G(gtk_label_set_text)((GtkLabel *)b->base.widget, text);
	greeter_Badge_shown(b->events, text);
	free(text);
}

static const greeter_Badge_vtable badge_vt = {.destroy = b_destroy, .set_name = b_name};

greeter_Badge *greeter_make_Badge(greeter_Badge_events events) {
	if (!asuka_linux()) return NULL;
	badge *b = calloc(1, sizeof *b);
	b->base.vt = &badge_vt;
	b->base.widget = G(gtk_label_new)("");
	b->events = events;
	return &b->base;
}
