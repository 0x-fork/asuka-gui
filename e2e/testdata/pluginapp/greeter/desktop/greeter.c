// The greeter package's C, for Windows and Linux: its module here, its
// view's widget in greeter_linux.c and greeter_windows.c.
#include "greeter.asuka.h"

#include <stdlib.h>

static char *g_hello(greeter_Greeter *self, const char *name, asuka_error *err) {
	(void)self, (void)err;
#ifdef _WIN32
	return asuka_sprintf("hello %s from Windows", name);
#else
	return asuka_sprintf("hello %s from Linux", name);
#endif
}

static void g_release(greeter_Greeter *self) { free(self); }

static const greeter_Greeter_vtable greeter_vt = {.release = g_release, .hello = g_hello};

greeter_Greeter *greeter_make_Greeter(asuka_error *err) {
	(void)err;
	greeter_Greeter *g = malloc(sizeof *g);
	g->vt = &greeter_vt;
	return g;
}
