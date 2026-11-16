// gtk.h: GLib, GTK 4, libadwaita and the rest as the Linux host calls
// them: platform/desktop's asuka_linux.h (the one set of declarations,
// native packages' too) and the host's own pointers to the functions of
// asuka_linux.def, named as the functions, resolved as the host starts
// (syms.c).
#ifndef AK_LINUX_GTK_H
#define AK_LINUX_GTK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define ASUKA_LINUX_HOST 1
#include "asuka_linux.h"


// The functions: pointers named as GLib's and GTK's functions, hidden in
// the executable (so they never interpose on the libraries' own), NULL
// for one the system's libraries don't have (an older GTK's, or an
// optional library not installed).
#pragma GCC visibility push(hidden)
#define SYM(lib, ret, name, args) extern ret(*name) args;
#include "asuka_linux.def"
#undef SYM
#pragma GCC visibility pop

// lx_gtk_load opens GTK, libadwaita and the GLib stack and resolves their
// functions; false with a message in err (its size errn) when one isn't
// installed.
bool lx_gtk_load(char *err, size_t errn);
// lx_lib_load opens an optional library and resolves its functions:
// false when the system hasn't it. Main thread.
bool lx_lib_load(int lib);
// lx_sym is any symbol of the loaded libraries, NULL if none has it.
void *lx_sym(const char *name);

// Conveniences over the GLib API, as its macros are.
#define G_CALLBACK(f) ((GCallback)(f))
// g_utf8_next_char: GLib's macro (g_utf8_skip), as a function.
static inline const char *g_utf8_next_char(const char *p) {
	unsigned char c = (unsigned char)*p;
	return p + (c < 0xc0 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : c < 0xf8 ? 4 : c < 0xfc ? 5 : 6);
}
// g_strstrip: GLib's macro.
#define g_strstrip(s) g_strchomp(g_strchug(s))
#define GPOINTER_TO_INT(p) ((int)(intptr_t)(p))
#define GINT_TO_POINTER(i) ((gpointer)(intptr_t)(i))
#define g_signal_connect(o, s, f, d) g_signal_connect_data((o), (s), G_CALLBACK(f), (d), NULL, (GConnectFlags)0)
#define g_signal_connect_after(o, s, f, d) g_signal_connect_data((o), (s), G_CALLBACK(f), (d), NULL, G_CONNECT_AFTER)
#define AK_IS(o, typefn) ((o) && typefn && g_type_check_instance_is_a((GTypeInstance *)(o), typefn()))
#define G_OBJECT(o) ((GObject *)(o))
#define GTK_WIDGET(o) ((GtkWidget *)(o))

#endif
