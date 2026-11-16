// asuka/appearance on Linux: the settings portal's appearance
// (org.freedesktop.appearance: accent-color, GNOME 47's and KDE's, an
// sRGB triple; contrast, 1 for high), read and followed by its
// SettingChanged while watched. Without the portal (or a contrast in it),
// high contrast is libadwaita's (its style manager's, which reads the
// portal or a HighContrast theme), and there's no accent. Linux desktops
// have no Reduce Transparency. Main thread, where GLib's signals come.
#include "asuka_linux.h"
#include "appearance.asuka.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS "org.freedesktop.portal.Settings"
#define NS "org.freedesktop.appearance"

// What the portal said, kept while watched (and after: the last known).
static struct {
	int accent[3];
	bool has_accent, contrast, contrast_known;
} portal;

static appearance_Settings settings(void) {
	appearance_Settings s = {.hasAccent = portal.has_accent, .red = portal.accent[0], .green = portal.accent[1], .blue = portal.accent[2]};
	if (portal.contrast_known) {
		s.increaseContrast = portal.contrast;
	} else if (G(adw_style_manager_get_default)) {
		gpointer m = G(adw_style_manager_get_default)();
		if (m) s.increaseContrast = G(adw_style_manager_get_high_contrast)(m);
	}
	return s;
}

// unwrap is v without its variants (one level or two), referenced.
static GVariant *unwrap(GVariant *v) {
	G(g_variant_ref_sink)(v);
	while (v && G(g_variant_is_of_type)(v, G_VARIANT_TYPE("v"))) {
		GVariant *in = G(g_variant_get_variant)(v);
		G(g_variant_unref)(v);
		v = in;
	}
	return v;
}

// setting takes a setting's value (NULL: none).
static void setting(const char *key, GVariant *v) {
	if (strcmp(key, "accent-color") == 0) {
		portal.has_accent = false;
		if (!v || !G(g_variant_is_of_type)(v, G_VARIANT_TYPE("(ddd)"))) return;
		double rgb[3];
		G(g_variant_get)(v, "(ddd)", &rgb[0], &rgb[1], &rgb[2]);
		for (int i = 0; i < 3; i++) {
			// Out of the range: the user set none.
			if (!(rgb[i] >= 0 && rgb[i] <= 1)) return;
			portal.accent[i] = (int)round(rgb[i] * 255);
		}
		portal.has_accent = true;
	} else if (strcmp(key, "contrast") == 0) {
		portal.contrast_known = v && G(g_variant_is_of_type)(v, G_VARIANT_TYPE("u"));
		portal.contrast = portal.contrast_known && G(g_variant_get_uint32)(v) == 1;
	}
}

typedef struct {
	appearance_fn_Settings changed;
	asuka_call *call;
	gpointer style;
	gulong handler;
	guint sub;
	int reading;
	bool done, first;
	appearance_Settings last;
} watching;

static void report(watching *w) {
	if (w->done) return;
	appearance_Settings s = settings();
	if (!w->first && memcmp(&s, &w->last, sizeof s) == 0) return;
	w->first = false;
	w->last = s;
	appearance_fn_Settings_call(w->changed, &s);
}

typedef struct {
	watching *w;
	char key[16];
	bool read;  // Read (before version 2), after ReadOne failed
} reading;

static void got(GObject *src, GAsyncResult *res, gpointer p) {
	reading *q = p;
	watching *w = q->w;
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	if (!r && !q->read) {
		// ReadOne is version 2's; Read wraps the value in one more variant.
		if (e) G(g_error_free)(e);
		q->read = true;
		G(g_dbus_connection_call)((GDBusConnection *)src, ASUKA_PORTAL, ASUKA_PORTAL_PATH, SETTINGS, "Read", G(g_variant_new)("(ss)", NS, q->key), NULL, 0, 2000, NULL, got, q);
		return;
	}
	if (e) G(g_error_free)(e);
	GVariant *v = NULL;
	if (r && G(g_variant_n_children)(r) > 0) v = unwrap(G(g_variant_get_child_value)(r, 0));
	setting(q->key, v);
	if (v) G(g_variant_unref)(v);
	if (r) G(g_variant_unref)(r);
	w->reading--;
	report(w);
	if (w->done && w->reading == 0) free(w);
	free(q);
}

static void on_setting(GDBusConnection *c, const gchar *s, const gchar *path, const gchar *i, const gchar *n, GVariant *params, gpointer p) {
	watching *w = p;
	const gchar *ns = NULL, *key = NULL;
	GVariant *v = NULL;
	G(g_variant_get)(params, "(&s&sv)", &ns, &key, &v);
	if (ns && strcmp(ns, NS) == 0) {
		GVariant *u = unwrap(v);
		setting(key, u);
		if (u) G(g_variant_unref)(u);
		v = NULL;
		report(w);
	}
	if (v) G(g_variant_unref)(v);
}

static void on_contrast(gpointer obj, gpointer spec, gpointer p) { report(p); }

static void stop(void *p) {
	watching *w = p;
	if (w->handler) G(g_signal_handler_disconnect)(w->style, w->handler);
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SESSION, NULL);
	if (w->sub && c) G(g_dbus_connection_signal_unsubscribe)(c, w->sub);
	w->done = true;
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(w->call, &e);
	if (w->reading == 0) free(w);
}

static appearance_Settings s_current(appearance_System *self, asuka_error *err) { return settings(); }

static void s_watch(appearance_System *self, appearance_fn_Settings changed, asuka_call *call) {
	watching *w = calloc(1, sizeof *w);
	w->changed = changed;
	w->call = call;
	w->first = true;
	if (G(adw_style_manager_get_default) && (w->style = G(adw_style_manager_get_default)()))
		w->handler = g_signal_connect(w->style, "notify::high-contrast", on_contrast, w);
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SESSION, NULL);
	if (c) {
		w->sub = G(g_dbus_connection_signal_subscribe)(c, ASUKA_PORTAL, SETTINGS, "SettingChanged", ASUKA_PORTAL_PATH, NULL, 0, on_setting, w, NULL);
		static const char *keys[] = {"accent-color", "contrast"};
		for (int i = 0; i < 2; i++) {
			reading *q = calloc(1, sizeof *q);
			q->w = w;
			strcpy(q->key, keys[i]);
			w->reading++;
			G(g_dbus_connection_call)(c, ASUKA_PORTAL, ASUKA_PORTAL_PATH, SETTINGS, "ReadOne", G(g_variant_new)("(ss)", NS, keys[i]), NULL, 0, 2000, NULL, got, q);
		}
	}
	report(w);
	asuka_call_on_cancel(call, stop, w);
}

static const appearance_System_vtable vt = {.current = s_current, .watch = s_watch};

appearance_System *appearance_make_System(asuka_error *err) {
	if (!asuka_linux()) {
		asuka_error_unsupported(err, "no GLib");
		return NULL;
	}
	appearance_System *s = malloc(sizeof *s);
	s->vt = &vt;
	return s;
}
