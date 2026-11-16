// dock.c: platform/dock on Linux. The progress, like notify's badge, is the
// launcher API's (com.canonical.Unity.LauncherEntry, an Update signal on
// the session bus naming the app's desktop file): Dash to Dock and
// Ubuntu's dock, KDE Plasma's task manager and others draw it over the
// app's icon; GNOME's own dash shows none. Every Update carries the whole
// entry (count, progress, urgency), so one doesn't undo another in a dock
// that takes an Update for the whole state. Indeterminate is a full bar;
// paused and error show as normal (the API has no states).
//
// Attention, asked while none of the app's windows is active: the entry's
// urgent flag (the dock's icon highlighted), and the window in use (the
// one active last) marked urgent on X11 (its urgency hint, the taskbar's
// highlight), or presented on Wayland, where GTK 4 has no urgency: an
// activation without the user's token, which GNOME and KDE don't grant
// but show as a notification that the app is ready (a click on it brings
// the window), and which can't be withdrawn. It ends as the app's window
// becomes active, or on cancel.
#include "host.h"

#include <stdio.h>
#include <stdlib.h>

bool lx_is_x11(GdkDisplay *d);

static struct {
	int64_t count;
	double progress;
	bool showing;  // progress
	bool urgent;
} launch;

static struct {
	uint32_t id;  // the request's, 0: none
	uint32_t next;
	GdkSurface *surface;  // the X11 surface marked urgent
	AK_VEC(GtkWindow *) windows;  // the windows followed till one's active
} attn;

// update_launcher emits the entry's whole state.
static bool update_launcher(void) {
	GDBusConnection *bus = lx_session_bus();
	if (!bus) return false;
	char path[512], uri[512];
	snprintf(path, sizeof path, "/%s/LauncherEntry", S(H.app_id));
	for (char *p = path + 1; *p; p++)
		if (*p == '.') *p = '/';
	snprintf(uri, sizeof uri, "application://%s.desktop", S(H.app_id));
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&b, "{sv}", "count", g_variant_new_int64(launch.count));
	g_variant_builder_add(&b, "{sv}", "count-visible", g_variant_new_boolean(launch.count > 0));
	g_variant_builder_add(&b, "{sv}", "progress", g_variant_new_double(launch.progress));
	g_variant_builder_add(&b, "{sv}", "progress-visible", g_variant_new_boolean(launch.showing));
	g_variant_builder_add(&b, "{sv}", "urgent", g_variant_new_boolean(launch.urgent));
	lx_dbus_emit(bus, path, "com.canonical.Unity.LauncherEntry", "Update", g_variant_new("(sa{sv})", uri, &b));
	return true;
}

// lx_set_badge sets the launcher's count for the app (0: none); notify's
// badge.
bool lx_set_badge(int64_t n) {
	launch.count = n;
	return update_launcher();
}

// lx_app_active reports whether one of the app's windows is the active
// one.
bool lx_app_active(void) {
	for (uint32_t i = 0; i < H.wins.len; i++)
		if (H.wins.data[i]->window && gtk_window_is_active(H.wins.data[i]->window)) return true;
	return H.main && gtk_window_is_active(H.main->window);
}

static void end_attention(void);

static void on_active(GObject *w, GParamSpec *p, gpointer data) {
	(void)p;
	(void)data;
	if (gtk_window_is_active((GtkWindow *)w)) end_attention();
}

// end_attention takes the request back, if there's one.
static void end_attention(void) {
	if (!attn.id) return;
	attn.id = 0;
	for (uint32_t i = 0; i < attn.windows.len; i++) {
		g_signal_handlers_disconnect_matched(attn.windows.data[i], G_SIGNAL_MATCH_FUNC, 0, 0, NULL, (gpointer)on_active, NULL);
		g_object_unref(attn.windows.data[i]);
	}
	attn.windows.len = 0;
	if (attn.surface) {
		gdk_x11_surface_set_urgency_hint(attn.surface, 0);
		g_object_unref(attn.surface);
		attn.surface = NULL;
	}
	if (launch.urgent) {
		launch.urgent = false;
		update_launcher();
	}
}

// progress: u8 state (0 none, 1 normal, 2 indeterminate, 3 paused, 4
// error), f64 value.
static void dock_progress(ak_span args, reply *r) {
	uint8_t state = 0;
	double v = 0;
	if (args.len >= 9) {
		ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
		state = ak_read_u8(&rd);
		v = ak_read_f64(&rd);
	}
	launch.showing = state != 0;
	launch.progress = v < 0 ? 0 : v > 1 ? 1 : v;
	if (state == 2) launch.progress = 1;
	if (update_launcher())
		reply_ok(r, NULL, 0);
	else
		reply_err(r, "no session bus");
}

static void follow(GtkWindow *w) {
	if (!w) return;
	g_signal_connect(w, "notify::is-active", G_CALLBACK(on_active), NULL);
	ak_vec_push(&attn.windows, g_object_ref(w));
}

// attention: u8 critical; result: u32 the request's ID (0: the app is in
// use, nothing asked).
static void dock_attention(ak_span args, reply *r) {
	(void)args;
	uint32_t id = 0;
	if (!lx_app_active()) {
		end_attention();
		attn.id = ++attn.next;
		id = attn.id;
		launch.urgent = true;
		update_launcher();
		win *w = H.active_win ? H.active_win : H.main;
		if (w) {
			GdkSurface *s = gtk_native_get_surface((GtkNative *)w->window);
			if (s && lx_is_x11(gdk_surface_get_display(s)) && gdk_x11_surface_set_urgency_hint) {
				gdk_x11_surface_set_urgency_hint(s, 1);
				attn.surface = g_object_ref(s);
			} else if (s) {
				gtk_window_present(w->window);
			}
		}
		// Ended as a window of the app's becomes active.
		if (H.main) follow(H.main->window);
		for (uint32_t i = 0; i < H.wins.len; i++)
			if (H.wins.data[i] != H.main) follow(H.wins.data[i]->window);
	}
	reply_ok(r, &id, 4);
}

// cancelAttention: u32 the request's ID.
static void dock_cancel(ak_span args, reply *r) {
	if (args.len >= 4) {
		ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
		if (ak_read_u32(&rd) == attn.id) end_attention();
	}
	reply_ok(r, NULL, 0);
}

static const method_t dock_methods[] = {
	{"dock", "progress", dock_progress},
	{"dock", "attention", dock_attention},
	{"dock", "cancelAttention", dock_cancel},
};

static void dock_modules(void) { lx_register_methods(dock_methods, AK_LEN(dock_methods)); }

const area lx_area_dock = {
	.name = "dock",
	.modules = dock_modules,
};
