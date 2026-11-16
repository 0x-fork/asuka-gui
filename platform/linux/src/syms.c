// syms.c: opens the libraries the host uses and resolves syms.def's
// functions with dlsym, as the host starts (the required ones) or as an
// area first needs an optional library (WebKitGTK, GStreamer, Xlib…).
#include "gtk.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC visibility push(hidden)
#define SYM(lib, ret, name, args) ret(*name) args;
#include "syms.def"
#undef SYM
#pragma GCC visibility pop

typedef struct {
	const char *names[6];  // sonames, tried in order
} lib_names;

static const lib_names sonames[AK_LIB_COUNT] = {
	[AK_LIB_GLIB] = {{"libglib-2.0.so.0", "libglib-2.0.so"}},
	[AK_LIB_GOBJECT] = {{"libgobject-2.0.so.0", "libgobject-2.0.so"}},
	[AK_LIB_GIO] = {{"libgio-2.0.so.0", "libgio-2.0.so"}},
	[AK_LIB_CAIRO] = {{"libcairo.so.2", "libcairo.so"}},
	[AK_LIB_PANGO] = {{"libpango-1.0.so.0", "libpango-1.0.so"}},
	[AK_LIB_PANGOCAIRO] = {{"libpangocairo-1.0.so.0", "libpangocairo-1.0.so"}},
	[AK_LIB_GTK] = {{"libgtk-4.so.1", "libgtk-4.so"}},
	[AK_LIB_ADW] = {{"libadwaita-1.so.0", "libadwaita-1.so"}},
	[AK_LIB_WEBKIT] = {{"libwebkitgtk-6.0.so.4", "libwebkitgtk-6.0.so"}},
	[AK_LIB_JSC] = {{"libjavascriptcoregtk-6.0.so.1", "libjavascriptcoregtk-6.0.so"}},
	[AK_LIB_GST] = {{"libgstreamer-1.0.so.0", "libgstreamer-1.0.so"}},
	[AK_LIB_GSTVIDEO] = {{"libgstvideo-1.0.so.0", "libgstvideo-1.0.so"}},
	[AK_LIB_GSTAPP] = {{"libgstapp-1.0.so.0", "libgstapp-1.0.so"}},
	[AK_LIB_GSTAUDIO] = {{"libgstaudio-1.0.so.0", "libgstaudio-1.0.so"}},
	[AK_LIB_X11] = {{"libX11.so.6", "libX11.so"}},
	[AK_LIB_XEXT] = {{"libXext.so.6", "libXext.so"}},
	[AK_LIB_FC] = {{"libfontconfig.so.1", "libfontconfig.so"}},
	[AK_LIB_PANGOFT2] = {{"libpangoft2-1.0.so.0", "libpangoft2-1.0.so"}},
	[AK_LIB_EPOXY] = {{"libepoxy.so.0", "libepoxy.so"}},
	[AK_LIB_SECRET] = {{"libsecret-1.so.0", "libsecret-1.so"}},
	[AK_LIB_SOUP] = {{"libsoup-3.0.so.0", "libsoup-3.0.so"}},
	[AK_LIB_EDS] = {{"libedataserver-1.2.so.27", "libedataserver-1.2.so.26", "libedataserver-1.2.so.25", "libedataserver-1.2.so.24", "libedataserver-1.2.so"}},
	[AK_LIB_EBOOK] = {{"libebook-1.2.so.21", "libebook-1.2.so.20", "libebook-1.2.so.19", "libebook-1.2.so"}},
	[AK_LIB_EBOOKC] = {{"libebook-contacts-1.2.so.4", "libebook-contacts-1.2.so.3", "libebook-contacts-1.2.so"}},
	[AK_LIB_ECAL] = {{"libecal-2.0.so.3", "libecal-2.0.so.2", "libecal-2.0.so.1", "libecal-2.0.so"}},
	[AK_LIB_ICAL] = {{"libical-glib.so.3", "libical-glib.so"}},
};

static void *handles[AK_LIB_COUNT];
static bool tried[AK_LIB_COUNT];

// The table of every function: its library, its name and where its
// pointer goes.
typedef struct {
	int lib;
	const char *name;
	void **slot;
} sym_entry;

static const sym_entry table[] = {
#define SYM(lib, ret, name, args) {AK_LIB_##lib, #name, (void **)&name},
#include "syms.def"
#undef SYM
};

static bool open_lib(int lib) {
	if (tried[lib]) return handles[lib] != NULL;
	tried[lib] = true;
	for (int i = 0; i < 6 && sonames[lib].names[i]; i++) {
		handles[lib] = dlopen(sonames[lib].names[i], RTLD_NOW | RTLD_GLOBAL);
		if (handles[lib]) break;
	}
	if (!handles[lib]) return false;
	for (size_t i = 0; i < sizeof table / sizeof *table; i++)
		if (table[i].lib == lib) *table[i].slot = dlsym(handles[lib], table[i].name);
	return true;
}

bool lx_gtk_load(char *err, size_t errn) {
	static const int required[] = {AK_LIB_GLIB, AK_LIB_GOBJECT, AK_LIB_GIO, AK_LIB_CAIRO, AK_LIB_PANGO, AK_LIB_PANGOCAIRO, AK_LIB_GTK, AK_LIB_ADW};
	open_lib(AK_LIB_PANGOFT2);  // optional: the app's fonts
	for (size_t i = 0; i < sizeof required / sizeof *required; i++) {
		int lib = required[i];
		if (!open_lib(lib)) {
			snprintf(err, errn, "asuka: %s isn't installed (%s): the Linux host needs GTK 4.14 and libadwaita 1.5 or later", sonames[lib].names[0], dlerror());
			return false;
		}
	}
	// What the host can't do without (GTK older than 4.14 lacks some of
	// these): say which, rather than crash later.
	static const char *const must[] = {"gtk_widget_allocate", "gtk_snapshot_append_color", "adw_navigation_view_new", "adw_toolbar_view_new", "adw_dialog_new", "adw_alert_dialog_new"};
	for (size_t i = 0; i < sizeof must / sizeof *must; i++)
		if (!lx_sym(must[i])) {
			snprintf(err, errn, "asuka: %s isn't in the system's GTK or libadwaita: the Linux host needs GTK 4.14 and libadwaita 1.5 or later", must[i]);
			return false;
		}
	if (getenv("ASUKA_SYMS")) {
		for (size_t i = 0; i < sizeof table / sizeof *table; i++)
			if (handles[table[i].lib] && !*table[i].slot) fprintf(stderr, "asuka: no %s\n", table[i].name);
	}
	return true;
}

bool lx_lib_load(int lib) { return lib >= 0 && lib < AK_LIB_COUNT && open_lib(lib); }

void *lx_sym(const char *name) {
	for (int i = 0; i < AK_LIB_COUNT; i++)
		if (handles[i]) {
			void *p = dlsym(handles[i], name);
			if (p) return p;
		}
	return NULL;
}
