// The greeter's Badge on Windows: a static control showing its name; it
// says what it shows.
#include "greeter.asuka.h"

#include "asuka_windows.h"

#include <stdlib.h>

typedef struct {
	greeter_Badge base;
	greeter_Badge_events events;
} badge;

static void b_destroy(greeter_Badge *self) { free(self); }

static void b_name(greeter_Badge *self, const char *name) {
	badge *b = (badge *)self;
	char *text = asuka_sprintf("Windows badge for %s", name);
	wchar_t *w = asuka_wide(text);
	SetWindowTextW((HWND)b->base.widget, w);
	free(w);
	greeter_Badge_shown(b->events, text);
	free(text);
}

static const greeter_Badge_vtable badge_vt = {.destroy = b_destroy, .set_name = b_name};

greeter_Badge *greeter_make_Badge(greeter_Badge_events events) {
	badge *b = calloc(1, sizeof *b);
	b->base.vt = &badge_vt;
	b->base.widget = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, (HWND)events.parent, NULL,
	                                 GetModuleHandleW(NULL), NULL);
	b->events = events;
	return &b->base;
}
