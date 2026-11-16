// The greeter's Badge on Linux: a GtkLabel showing its name; it says what
// it shows.
#include "asuka_linux.h"
#include "greeter.asuka.h"

#include <stdlib.h>

typedef struct {
	greeter_Badge base;
	greeter_Badge_events events;
} badge;

static void destroy(greeter_Badge *self) { free(self); }

static void set_name(greeter_Badge *self, const char *name) {
	badge *b = (badge *)self;
	char *text = asuka_sprintf("Linux badge for %s", name);
	G(gtk_label_set_text)((GtkLabel *)b->base.widget, text);
	greeter_Badge_shown(b->events, text);
	free(text);
}

static const greeter_Badge_vtable badge_vt = {.destroy = destroy, .set_name = set_name};

greeter_Badge *greeter_make_Badge(greeter_Badge_events events) {
	if (!asuka_linux()) return NULL;
	badge *b = calloc(1, sizeof *b);
	if (!b) return NULL;
	b->base.vt = &badge_vt;
	b->events = events;
	b->base.widget = G(gtk_label_new)("");
	return &b->base;
}
