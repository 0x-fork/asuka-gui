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

void *asuka_linux_sym(const char *name) {
	if (!asuka_linux()) return NULL;
	for (int i = L_COUNT - 1; i >= 0; i--) {
		if (!libs[i]) continue;
		void *f = dlsym(libs[i], name);
		if (f) return f;
	}
	return dlsym(RTLD_DEFAULT, name);
}

static struct {
	pthread_once_t once;
	gpointer (*from_file)(const char *, GError **);
	gpointer (*orient)(gpointer);
	int (*width)(gpointer), (*height)(gpointer), (*rowstride)(gpointer), (*channels)(gpointer), (*bits)(gpointer);
	gboolean (*has_alpha)(gpointer);
	guchar *(*pixels)(gpointer);
} pixbuf = {.once = PTHREAD_ONCE_INIT};

static void load_pixbuf(void) {
	if (!asuka_linux()) return;
	if (!dlopen("libgdk_pixbuf-2.0.so.0", RTLD_NOW | RTLD_GLOBAL)) return;
	*(void **)&pixbuf.from_file = asuka_linux_sym("gdk_pixbuf_new_from_file");
	*(void **)&pixbuf.orient = asuka_linux_sym("gdk_pixbuf_apply_embedded_orientation");
	*(void **)&pixbuf.width = asuka_linux_sym("gdk_pixbuf_get_width");
	*(void **)&pixbuf.height = asuka_linux_sym("gdk_pixbuf_get_height");
	*(void **)&pixbuf.rowstride = asuka_linux_sym("gdk_pixbuf_get_rowstride");
	*(void **)&pixbuf.channels = asuka_linux_sym("gdk_pixbuf_get_n_channels");
	*(void **)&pixbuf.bits = asuka_linux_sym("gdk_pixbuf_get_bits_per_sample");
	*(void **)&pixbuf.has_alpha = asuka_linux_sym("gdk_pixbuf_get_has_alpha");
	*(void **)&pixbuf.pixels = asuka_linux_sym("gdk_pixbuf_get_pixels");
}

bool asuka_linux_image_load(const char *path, int *width, int *height, uint8_t **pixels) {
	pthread_once(&pixbuf.once, load_pixbuf);
	if (!pixbuf.from_file || !pixbuf.orient || !pixbuf.width || !pixbuf.height || !pixbuf.rowstride || !pixbuf.channels || !pixbuf.bits ||
	    !pixbuf.has_alpha || !pixbuf.pixels)
		return false;
	GError *e = NULL;
	gpointer pb = pixbuf.from_file(path, &e);
	if (!pb) {
		if (e) G(g_error_free)(e);
		return false;
	}
	gpointer up = pixbuf.orient(pb);  // a new reference
	G(g_object_unref)(pb);
	if (!up) return false;
	int w = pixbuf.width(up), h = pixbuf.height(up), stride = pixbuf.rowstride(up), n = pixbuf.channels(up);
	bool alpha = pixbuf.has_alpha(up);
	const guchar *src = pixbuf.pixels(up);
	if (w <= 0 || h <= 0 || pixbuf.bits(up) != 8 || (n != 3 && n != 4) || !src) {
		G(g_object_unref)(up);
		return false;
	}
	uint8_t *out = malloc((size_t)w * h * 4);
	for (int y = 0; y < h; y++) {
		const guchar *s = src + (size_t)y * stride;
		uint8_t *d = out + (size_t)y * w * 4;
		for (int x = 0; x < w; x++, s += n, d += 4) {
			d[0] = s[0], d[1] = s[1], d[2] = s[2];
			d[3] = alpha && n == 4 ? s[3] : 255;
		}
	}
	G(g_object_unref)(up);
	*width = w, *height = h, *pixels = out;
	return true;
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
