// syms.c: the host's pointers to asuka_linux.def's functions, named as
// the functions (gtk.h declares them), copied from what platform/desktop's
// linux_syms.c resolves (asuka_linux, asuka_linux_load), so the host and
// native packages' C share one set of declarations and one dlopen of each
// library.
#include "gtk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC visibility push(hidden)
#define SYM(lib, ret, name, args) ret(*name) args;
#include "asuka_linux.def"
#undef SYM
#pragma GCC visibility pop

// copy takes the resolved functions' pointers.
static void copy(void) {
#define SYM(lib, ret, name, args) name = asuka_linux_fns.name;
#include "asuka_linux.def"
#undef SYM
}

bool lx_gtk_load(char *err, size_t errn) {
	asuka_linux();  // GLib's stack, cairo, Pango, GTK, libadwaita
	static const int required[] = {ASUKA_LIB_GLIB, ASUKA_LIB_GOBJECT, ASUKA_LIB_GIO, ASUKA_LIB_CAIRO, ASUKA_LIB_PANGO, ASUKA_LIB_PANGOCAIRO, ASUKA_LIB_GTK, ASUKA_LIB_ADW};
	for (size_t i = 0; i < sizeof required / sizeof *required; i++)
		if (!asuka_linux_load(required[i])) {
			snprintf(err, errn, "asuka: %s isn't installed: the Linux host needs GTK 4.14 and libadwaita 1.5 or later", asuka_linux_soname(required[i]));
			return false;
		}
	copy();
	// What the host can't do without (GTK older than 4.14 lacks some of
	// these): say which, rather than crash later.
	static const char *const must[] = {"gtk_widget_allocate", "gtk_snapshot_append_color", "adw_navigation_view_new", "adw_toolbar_view_new", "adw_dialog_new", "adw_alert_dialog_new"};
	for (size_t i = 0; i < sizeof must / sizeof *must; i++)
		if (!lx_sym(must[i])) {
			snprintf(err, errn, "asuka: %s isn't in the system's GTK or libadwaita: the Linux host needs GTK 4.14 and libadwaita 1.5 or later", must[i]);
			return false;
		}
	if (getenv("ASUKA_SYMS")) {
#define SYM(lib, ret, fn, args) \
	if (!fn) fprintf(stderr, "asuka: no %s\n", #fn);
#include "asuka_linux.def"
#undef SYM
	}
	return true;
}

bool lx_lib_load(int lib) {
	bool ok = asuka_linux_load(lib);
	if (ok) copy();
	return ok;
}

void *lx_sym(const char *name) { return asuka_linux_sym(name); }
