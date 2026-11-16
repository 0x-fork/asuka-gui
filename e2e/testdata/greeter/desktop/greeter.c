// The greeter's module on Windows and Linux: its greeting, in C.
#include "greeter.asuka.h"

#include <stdlib.h>

static void release(greeter_Greeter *self) { free(self); }

static char *hello(greeter_Greeter *self, const char *name, asuka_error *err) {
	(void)self, (void)err;
#ifdef _WIN32
	return asuka_sprintf("hello %s from Windows", name);
#else
	return asuka_sprintf("hello %s from Linux", name);
#endif
}

static const greeter_Greeter_vtable greeter_vt = {.release = release, .hello = hello};

greeter_Greeter *greeter_make_Greeter(asuka_error *err) {
	(void)err;
	greeter_Greeter *g = malloc(sizeof *g);
	if (g) g->vt = &greeter_vt;
	return g;
}
