// linux.c (platform/desktop/src): asuka_linux.h's helpers for packages'
// C (errors, the buses, types, the main thread); its functions are
// resolved by linux_syms.c. Built on Linux only.
#ifdef __linux__
#include "asuka_linux.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

bool asuka_linux_image_load(const char *path, int *width, int *height, uint8_t **pixels) {
	if (!asuka_linux() || !asuka_linux_load(ASUKA_LIB_PIXBUF) || !G(gdk_pixbuf_new_from_file) || !G(gdk_pixbuf_apply_embedded_orientation) ||
	    !G(gdk_pixbuf_get_bits_per_sample) || !G(gdk_pixbuf_get_has_alpha) || !G(gdk_pixbuf_read_pixels))
		return false;
	GError *e = NULL;
	GdkPixbuf *pb = G(gdk_pixbuf_new_from_file)(path, &e);
	if (!pb) {
		if (e) G(g_error_free)(e);
		return false;
	}
	GdkPixbuf *up = G(gdk_pixbuf_apply_embedded_orientation)(pb);  // a new reference
	G(g_object_unref)(pb);
	if (!up) return false;
	int w = G(gdk_pixbuf_get_width)(up), h = G(gdk_pixbuf_get_height)(up), stride = G(gdk_pixbuf_get_rowstride)(up),
	    n = G(gdk_pixbuf_get_n_channels)(up);
	bool alpha = G(gdk_pixbuf_get_has_alpha)(up);
	const guint8 *src = G(gdk_pixbuf_read_pixels)(up);
	if (w <= 0 || h <= 0 || G(gdk_pixbuf_get_bits_per_sample)(up) != 8 || (n != 3 && n != 4) || !src) {
		G(g_object_unref)(up);
		return false;
	}
	uint8_t *out = malloc((size_t)w * h * 4);
	if (!out) {
		G(g_object_unref)(up);
		return false;
	}
	for (int y = 0; y < h; y++) {
		const guint8 *row = src + (size_t)y * stride;
		uint8_t *d = out + (size_t)y * w * 4;
		for (int x = 0; x < w; x++, row += n, d += 4) {
			d[0] = row[0], d[1] = row[1], d[2] = row[2];
			d[3] = alpha && n == 4 ? row[3] : 255;
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

static bool gst_ready;

static void init_gst(void) { gst_ready = G(gst_init_check)(NULL, NULL, NULL); }

bool asuka_linux_gst(void) {
	static pthread_once_t gst_once = PTHREAD_ONCE_INIT;
	if (!asuka_linux() || !asuka_linux_load(ASUKA_LIB_GST) || !G(gst_init_check)) return false;
	asuka_linux_load(ASUKA_LIB_GSTAPP);
	asuka_linux_load(ASUKA_LIB_GSTVIDEO);
	asuka_linux_load(ASUKA_LIB_GSTAUDIO);
	pthread_once(&gst_once, init_gst);
	return gst_ready;
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
