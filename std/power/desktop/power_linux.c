// asuka/power on Linux, through the system's services over D-Bus: UPower
// for the battery (its OnBattery, and the display device: the batteries
// as one, its percentage and whether it charges), GIO's power profile
// monitor for the power saver profile (power-profiles-daemon, through the
// portal in a sandbox), logind for sleep (PrepareForSleep) and the
// session's lock (LockedHint), and the screen saver's ActiveChanged
// (GNOME's, KDE's), which comes as the screen locks. Linux has no thermal
// state for apps: nominal. The idle time is GNOME's idle monitor's
// (Mutter), else the screen saver's (KDE). What's followed starts with
// the first watcher and goes on (the subscriptions are cheap), answering
// each watcher. Main thread, where GLib's signals come.
#include "asuka_linux.h"
#include "power.asuka.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UPOWER "org.freedesktop.UPower"
#define UPOWER_PATH "/org/freedesktop/UPower"
#define UPOWER_DISPLAY "/org/freedesktop/UPower/devices/DisplayDevice"
#define LOGIN1 "org.freedesktop.login1"
#define LOGIN1_PATH "/org/freedesktop/login1"
#define PROPS "org.freedesktop.DBus.Properties"

typedef struct following {
	power_fn_Status changed;
	power_fn_Event happened;
	bool watch;
	asuka_call *call;
	power_Status last;
	bool first;
	struct following *next;
} following;

static struct {
	bool started;
	int reading;  // UPower reads in progress
	bool again;   // read again once they're done
	bool on_battery, present, charging, saver;
	double percentage;
	GPowerProfileMonitor *profiles;
	bool locked, lock_known;
	following *list;
} p;

static power_Status state(void) {
	power_Status s = {.level = -1, .lowPower = p.saver};
	if (p.present) {
		s.battery = true;
		s.onBattery = p.on_battery;
		s.charging = p.charging;
		double v = p.percentage < 0 ? 0 : p.percentage > 100 ? 100 : p.percentage;
		s.level = round(v) / 100;
	}
	return s;
}

static void report(following *f) {
	power_Status s = state();
	if (!f->first && memcmp(&s, &f->last, sizeof s) == 0) return;
	f->first = false;
	f->last = s;
	power_fn_Status_call(f->changed, &s);
}

static void power_changed(void) {
	for (following *f = p.list; f; f = f->next)
		if (f->watch) report(f);
}

static void emit(power_Event e) {
	for (following *f = p.list; f; f = f->next)
		if (!f->watch) power_fn_Event_call(f->happened, e);
}

// first_bool is a signal's or reply's first value, if it's a boolean.
static bool first_bool(GVariant *v, bool *out) {
	if (!v || G(g_variant_n_children)(v) < 1) return false;
	GVariant *c = G(g_variant_get_child_value)(v, 0);
	if (G(g_variant_is_of_type)(c, G_VARIANT_TYPE("v"))) {
		GVariant *in = G(g_variant_get_variant)(c);
		G(g_variant_unref)(c);
		c = in;
	}
	bool ok = G(g_variant_is_of_type)(c, G_VARIANT_TYPE("b"));
	if (ok) *out = G(g_variant_get_boolean)(c);
	G(g_variant_unref)(c);
	return ok;
}

static void set_locked(bool on) {
	if (p.lock_known && p.locked == on) return;
	bool known = p.lock_known;
	p.locked = on;
	p.lock_known = true;
	if (known) emit(on ? power_Event_lock : power_Event_unlock);
}

static void read_upower(void);

static void read_done(void) {
	if (--p.reading > 0) return;
	if (p.again) {
		p.again = false;
		read_upower();
		return;
	}
	power_changed();
}

static GVariant *dict_of(GObject *src, GAsyncResult *res) {
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	if (!r) {
		if (e) G(g_error_free)(e);
		return NULL;
	}
	GVariant *d = G(g_variant_get_child_value)(r, 0);
	G(g_variant_unref)(r);
	return d;
}

static void got_upower(GObject *src, GAsyncResult *res, gpointer data) {
	GVariant *d = dict_of(src, res);
	p.on_battery = false;
	if (d) {
		GVariant *v = G(g_variant_lookup_value)(d, "OnBattery", G_VARIANT_TYPE("b"));
		if (v) p.on_battery = G(g_variant_get_boolean)(v), G(g_variant_unref)(v);
		G(g_variant_unref)(d);
	}
	read_done();
}

static void got_display(GObject *src, GAsyncResult *res, gpointer data) {
	GVariant *d = dict_of(src, res);
	p.present = false;
	p.percentage = 0;
	p.charging = false;
	if (d) {
		GVariant *v;
		if ((v = G(g_variant_lookup_value)(d, "IsPresent", G_VARIANT_TYPE("b")))) p.present = G(g_variant_get_boolean)(v), G(g_variant_unref)(v);
		// Type: 2 battery, 3 UPS; the display device of a desktop without
		// one isn't present.
		guint32 type = 0;
		if ((v = G(g_variant_lookup_value)(d, "Type", G_VARIANT_TYPE("u")))) type = G(g_variant_get_uint32)(v), G(g_variant_unref)(v);
		if (type != 2 && type != 3) p.present = false;
		if ((v = G(g_variant_lookup_value)(d, "Percentage", G_VARIANT_TYPE("d")))) p.percentage = G(g_variant_get_double)(v), G(g_variant_unref)(v);
		guint32 st = 0;
		if ((v = G(g_variant_lookup_value)(d, "State", G_VARIANT_TYPE("u")))) st = G(g_variant_get_uint32)(v), G(g_variant_unref)(v);
		p.charging = st == 1 || st == 5;  // charging, pending charge
		G(g_variant_unref)(d);
	}
	read_done();
}

// read_upower reads UPower's OnBattery and its display device.
static void read_upower(void) {
	if (p.reading > 0) {
		p.again = true;
		return;
	}
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	if (!c) {
		power_changed();
		return;
	}
	p.reading = 2;
	G(g_dbus_connection_call)(c, UPOWER, UPOWER_PATH, PROPS, "GetAll", G(g_variant_new)("(s)", UPOWER), G_VARIANT_TYPE("(a{sv})"), 0, 2000, NULL, got_upower, NULL);
	G(g_dbus_connection_call)(c, UPOWER, UPOWER_DISPLAY, PROPS, "GetAll", G(g_variant_new)("(s)", UPOWER ".Device"), G_VARIANT_TYPE("(a{sv})"), 0, 2000, NULL, got_display, NULL);
}

static void on_upower(GDBusConnection *c, const gchar *s, const gchar *path, const gchar *i, const gchar *n, GVariant *v, gpointer d) { read_upower(); }

static void on_sleep(GDBusConnection *c, const gchar *s, const gchar *path, const gchar *i, const gchar *n, GVariant *v, gpointer d) {
	bool sleeping;
	if (first_bool(v, &sleeping)) emit(sleeping ? power_Event_suspend : power_Event_resume);
}

static void on_saver(GDBusConnection *c, const gchar *s, const gchar *path, const gchar *i, const gchar *n, GVariant *v, gpointer d) {
	bool on;
	if (first_bool(v, &on)) set_locked(on);
}

static void on_profile(gpointer obj, gpointer spec, gpointer data) {
	p.saver = G(g_power_profile_monitor_get_power_saver_enabled)(p.profiles);
	power_changed();
}

static void got_locked(GObject *src, GAsyncResult *res, gpointer data) {
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	if (!r) {
		if (e) G(g_error_free)(e);
		return;
	}
	bool v;
	if (first_bool(r, &v)) set_locked(v);
	G(g_variant_unref)(r);
}

static void read_locked(GDBusConnection *c, const char *path) {
	G(g_dbus_connection_call)(c, LOGIN1, path, PROPS, "Get", G(g_variant_new)("(ss)", LOGIN1 ".Session", "LockedHint"), G_VARIANT_TYPE("(v)"), 0, 2000, NULL, got_locked, NULL);
}

static void on_session(GDBusConnection *c, const gchar *s, const gchar *path, const gchar *i, const gchar *n, GVariant *v, gpointer d) {
	if (!v || G(g_variant_n_children)(v) < 3) return;
	GVariant *changed = G(g_variant_get_child_value)(v, 1);
	GVariant *hint = G(g_variant_lookup_value)(changed, "LockedHint", G_VARIANT_TYPE("b"));
	G(g_variant_unref)(changed);
	if (hint) {
		set_locked(G(g_variant_get_boolean)(hint));
		G(g_variant_unref)(hint);
		return;
	}
	GVariant *inv = G(g_variant_get_child_value)(v, 2);
	for (gsize k = 0; k < G(g_variant_n_children)(inv); k++) {
		GVariant *name = G(g_variant_get_child_value)(inv, k);
		if (strcmp(G(g_variant_get_string)(name, NULL), "LockedHint") == 0) read_locked(c, path);
		G(g_variant_unref)(name);
	}
	G(g_variant_unref)(inv);
}

static void follow_session(GDBusConnection *c, const char *path) {
	G(g_dbus_connection_signal_subscribe)(c, LOGIN1, PROPS, "PropertiesChanged", path, NULL, 0, on_session, NULL, NULL);
	read_locked(c, path);
}

static void got_session(GObject *src, GAsyncResult *res, gpointer by_pid) {
	GError *e = NULL;
	GDBusConnection *c = (GDBusConnection *)src;
	GVariant *r = G(g_dbus_connection_call_finish)(c, res, &e);
	if (r) {
		const gchar *path = NULL;
		G(g_variant_get)(r, "(&o)", &path);
		if (path) follow_session(c, path);
		G(g_variant_unref)(r);
		return;
	}
	if (e) G(g_error_free)(e);
	// A process outside the session (a user service): the session the
	// environment names.
	const char *id = getenv("XDG_SESSION_ID");
	if (by_pid && id && *id)
		G(g_dbus_connection_call)(c, LOGIN1, LOGIN1_PATH, LOGIN1 ".Manager", "GetSession", G(g_variant_new)("(s)", id), G_VARIANT_TYPE("(o)"), 0, 2000, NULL, got_session, NULL);
}

// start follows the power's sources, once.
static void start(void) {
	if (p.started || !asuka_linux()) return;
	p.started = true;
	if (G(g_power_profile_monitor_dup_default)) {
		p.profiles = G(g_power_profile_monitor_dup_default)();
		if (p.profiles) {
			p.saver = G(g_power_profile_monitor_get_power_saver_enabled)(p.profiles);
			g_signal_connect(p.profiles, "notify::power-saver-enabled", on_profile, NULL);
		}
	}
	GDBusConnection *sys = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	if (sys) {
		G(g_dbus_connection_signal_subscribe)(sys, UPOWER, PROPS, "PropertiesChanged", UPOWER_PATH, NULL, 0, on_upower, NULL, NULL);
		G(g_dbus_connection_signal_subscribe)(sys, UPOWER, PROPS, "PropertiesChanged", UPOWER_DISPLAY, NULL, 0, on_upower, NULL, NULL);
		G(g_dbus_connection_signal_subscribe)(sys, LOGIN1, LOGIN1 ".Manager", "PrepareForSleep", LOGIN1_PATH, NULL, 0, on_sleep, NULL, NULL);
		G(g_dbus_connection_call)(sys, LOGIN1, LOGIN1_PATH, LOGIN1 ".Manager", "GetSessionByPID", G(g_variant_new)("(u)", (guint32)getpid()), G_VARIANT_TYPE("(o)"), 0, 2000, NULL, got_session, (gpointer)1);
		read_upower();
	}
	GDBusConnection *ses = asuka_linux_bus(G_BUS_TYPE_SESSION, NULL);
	if (ses) {
		G(g_dbus_connection_signal_subscribe)(ses, NULL, "org.gnome.ScreenSaver", "ActiveChanged", NULL, NULL, 0, on_saver, NULL, NULL);
		G(g_dbus_connection_signal_subscribe)(ses, NULL, "org.freedesktop.ScreenSaver", "ActiveChanged", NULL, NULL, 0, on_saver, NULL, NULL);
	}
}

static power_Status m_current(power_Monitor *self, asuka_error *err) {
	start();
	return state();
}

static void stop(void *ctx) {
	following *f = ctx;
	for (following **q = &p.list; *q; q = &(*q)->next)
		if (*q == f) {
			*q = f->next;
			break;
		}
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(f->call, &e);
	free(f);
}

static following *add(asuka_call *call) {
	start();
	following *f = calloc(1, sizeof *f);
	f->call = call;
	f->first = true;
	f->next = p.list;
	p.list = f;
	asuka_call_on_cancel(call, stop, f);
	return f;
}

static void m_watch(power_Monitor *self, power_fn_Status changed, asuka_call *call) {
	following *f = add(call);
	f->changed = changed;
	f->watch = true;
	report(f);
}

static void m_events(power_Monitor *self, power_fn_Event happened, asuka_call *call) { add(call)->happened = happened; }

static void idle_unknown(asuka_call *call) {
	asuka_error e = {0};
	asuka_error_declared(&e, power_PowerError_idleUnknown);
	asuka_call_fail(call, &e);
}

static void got_saver_idle(GObject *src, GAsyncResult *res, gpointer call) {
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	if (!r) {
		if (e) G(g_error_free)(e);
		idle_unknown(call);
		return;
	}
	guint32 s = 0;
	G(g_variant_get)(r, "(u)", &s);
	G(g_variant_unref)(r);
	power_Monitor_idle_done(call, (int64_t)s * 1000000000);
}

static void got_mutter_idle(GObject *src, GAsyncResult *res, gpointer call) {
	GError *e = NULL;
	GDBusConnection *c = (GDBusConnection *)src;
	GVariant *r = G(g_dbus_connection_call_finish)(c, res, &e);
	if (!r) {
		if (e) G(g_error_free)(e);
		G(g_dbus_connection_call)(c, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "GetSessionIdleTime", NULL,
		                          G_VARIANT_TYPE("(u)"), 0, 2000, NULL, got_saver_idle, call);
		return;
	}
	guint64 ms = 0;
	G(g_variant_get)(r, "(t)", &ms);
	G(g_variant_unref)(r);
	power_Monitor_idle_done(call, (int64_t)ms * 1000000);
}

static void m_idle(power_Monitor *self, asuka_call *call) {
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SESSION, NULL);
	if (!c) {
		idle_unknown(call);
		return;
	}
	G(g_dbus_connection_call)(c, "org.gnome.Mutter.IdleMonitor", "/org/gnome/Mutter/IdleMonitor/Core", "org.gnome.Mutter.IdleMonitor", "GetIdletime", NULL,
	                          G_VARIANT_TYPE("(t)"), 0, 2000, NULL, got_mutter_idle, call);
}

static const power_Monitor_vtable vt = {.current = m_current, .watch = m_watch, .events_ = m_events, .idle = m_idle};

power_Monitor *power_make_Monitor(asuka_error *err) {
	if (!asuka_linux()) {
		asuka_error_unsupported(err, "no GLib");
		return NULL;
	}
	power_Monitor *m = malloc(sizeof *m);
	m->vt = &vt;
	return m;
}
