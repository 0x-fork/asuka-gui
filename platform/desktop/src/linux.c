// linux.c (platform/desktop/src): asuka_linux.h's functions, resolved with
// dlopen and dlsym the first time they're asked for. Built on Linux only.
#ifdef __linux__
#include "asuka_linux.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

asuka_linux_t asuka_linux_fns;

static pthread_once_t once = PTHREAD_ONCE_INIT;
static bool loaded;

enum { L_GLIB, L_GOBJECT, L_GIO, L_GTK, L_ADW, L_COUNT };

static void *libs[L_COUNT];

static void load(void) {
	static const char *names[L_COUNT] = {"libglib-2.0.so.0", "libgobject-2.0.so.0", "libgio-2.0.so.0", "libgtk-4.so.1", "libadwaita-1.so.0"};
	for (int i = 0; i < L_COUNT; i++) libs[i] = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
	if (!libs[L_GLIB] || !libs[L_GOBJECT] || !libs[L_GIO]) return;
#define lib_glib L_GLIB
#define lib_gobject L_GOBJECT
#define lib_gio L_GIO
#define lib_gtk L_GTK
#define lib_adw L_ADW
#define X(lib, ret, name, params)                                                \
	if (libs[lib_##lib]) *(void **)(&asuka_linux_fns.name) = dlsym(libs[lib_##lib], #name);
	ASUKA_LINUX_FUNCS(X)
#undef X
	loaded = true;
}

const asuka_linux_t *asuka_linux(void) {
	pthread_once(&once, load);
	return loaded ? &asuka_linux_fns : NULL;
}

void asuka_linux_fail(asuka_error *err, GError *e, const char *what) {
	if (!e) {
		asuka_error_failed(err, "gio", 0, "%s failed", what ? what : "a call");
		return;
	}
	if (what) asuka_error_failed(err, "gio", e->code, "%s: %s", what, e->message);
	else asuka_error_failed(err, "gio", e->code, "%s", e->message);
	G(g_error_free)(e);
}

GDBusConnection *asuka_linux_bus(int kind, GError **e) {
	static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
	static GDBusConnection *buses[3];
	if (kind < 1 || kind > 2 || !asuka_linux()) return NULL;
	pthread_mutex_lock(&mu);
	if (!buses[kind]) buses[kind] = G(g_bus_get_sync)(kind, NULL, e);
	GDBusConnection *c = buses[kind];
	pthread_mutex_unlock(&mu);
	return c;
}

bool asuka_linux_is_a(gpointer obj, const char *type) {
	if (!obj || !asuka_linux()) return false;
	GType t = G(g_type_from_name)(type);
	return t && G(g_type_check_instance_is_a)(obj, t);
}

bool asuka_linux_cancelled(const GError *e) {
	if (!e) return false;
	if (G(g_io_error_quark) && e->domain == G(g_io_error_quark)() && e->code == 19) return true;  // G_IO_ERROR_CANCELLED
	if (G(gtk_dialog_error_quark) && e->domain == G(gtk_dialog_error_quark)() && (e->code == 1 || e->code == 2)) return true;
	return false;
}

typedef struct {
	void (*fn)(void *);
	void *ctx;
} job;

static gboolean run_job(gpointer p) {
	job *j = p;
	j->fn(j->ctx);
	free(j);
	return 0;
}

void asuka_linux_main(void (*fn)(void *ctx), void *ctx) {
	job *j = malloc(sizeof *j);
	j->fn = fn;
	j->ctx = ctx;
	G(g_idle_add_full)(G_PRIORITY_DEFAULT, run_job, j, NULL);
}
#endif
