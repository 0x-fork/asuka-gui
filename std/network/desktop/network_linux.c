// asuka/network on Linux: GNetworkMonitor (NetworkManager's, the portal's
// in a sandbox) says whether the network is there and metered;
// NetworkManager, when it runs, says what carries it (its primary
// connection's type), asked as it changes. Linux has no low data mode.
// Everything runs on the main thread, where GLib's signals come.
#include "asuka_linux.h"
#include "network.asuka.h"

#include <stdlib.h>
#include <string.h>

static network_ConnectionType kind;  // NetworkManager's, 0: not known

static GNetworkMonitor *monitor(asuka_error *err) {
	static GNetworkMonitor *m;
	if (!m && asuka_linux()) m = G(g_network_monitor_get_default)();
	if (!m && err) asuka_error_unsupported(err, "no network monitor");
	return m;
}

static network_Status state(void) {
	GNetworkMonitor *m = monitor(NULL);
	network_Status s = {0};
	if (!m || !G(g_network_monitor_get_network_available)(m) || G(g_network_monitor_get_connectivity)(m) != G_NETWORK_CONNECTIVITY_FULL) return s;
	s.connected = true;
	s.type = kind ? kind : network_ConnectionType_other;
	s.metered = G(g_network_monitor_get_network_metered)(m);
	return s;
}

static network_ConnectionType nm_type(const char *t) {
	static const struct {
		const char *name;
		network_ConnectionType kind;
	} types[] = {
	    {"802-11-wireless", network_ConnectionType_wifi}, {"wifi-p2p", network_ConnectionType_wifi},       {"gsm", network_ConnectionType_cellular},
	    {"cdma", network_ConnectionType_cellular},        {"wwan", network_ConnectionType_cellular},       {"802-3-ethernet", network_ConnectionType_ethernet},
	    {"pppoe", network_ConnectionType_ethernet},       {"infiniband", network_ConnectionType_ethernet}, {"veth", network_ConnectionType_ethernet},
	};
	if (!*t) return 0;
	for (size_t i = 0; i < sizeof types / sizeof *types; i++)
		if (strcmp(t, types[i].name) == 0) return types[i].kind;
	return network_ConnectionType_other;
}

static network_Status m_current(network_Monitor *self, asuka_error *err) {
	if (!monitor(err)) return (network_Status){0};
	return state();
}

typedef struct {
	network_fn_Status changed;
	asuka_call *call;
	GNetworkMonitor *m;
	gulong handlers[3];
	guint sub;
	network_Status last;
	bool first, done;
	int asking;  // NetworkManager calls out
} watching;

static void report(watching *w) {
	if (w->done) return;
	network_Status s = state();
	if (!w->first && memcmp(&s, &w->last, sizeof s) == 0) return;
	w->first = false;
	w->last = s;
	network_fn_Status_call(w->changed, &s);
}

static void finish(watching *w) {
	if (w->asking == 0 && w->done) free(w);
}

static void got_type(GObject *src, GAsyncResult *res, gpointer p) {
	watching *w = p;
	w->asking--;
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	kind = 0;
	if (r) {
		GVariant *v = NULL;
		G(g_variant_get)(r, "(v)", &v);
		if (v && G(g_variant_is_of_type)(v, G_VARIANT_TYPE("s"))) kind = nm_type(G(g_variant_get_string)(v, NULL));
		if (v) G(g_variant_unref)(v);
		G(g_variant_unref)(r);
	} else if (e) {
		G(g_error_free)(e);
	}
	report(w);
	finish(w);
}

// read asks NetworkManager what carries the connection, then reports.
static void read(watching *w) {
	if (w->done) return;
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	if (!c) {
		report(w);
		return;
	}
	w->asking++;
	G(g_dbus_connection_call)(c, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties", "Get",
	                          G(g_variant_new)("(ss)", "org.freedesktop.NetworkManager", "PrimaryConnectionType"), G_VARIANT_TYPE("(v)"),
	                          G_DBUS_CALL_FLAGS_NONE, 2000, NULL, got_type, w);
}

static void on_changed(gpointer obj, gpointer a, gpointer p) { read(p); }
static void on_notify(gpointer obj, gpointer spec, gpointer p) { read(p); }
static void on_signal(GDBusConnection *c, const gchar *s, const gchar *path, const gchar *i, const gchar *n, GVariant *v, gpointer p) { read(p); }

static void stop(void *p) {
	watching *w = p;
	for (int i = 0; i < 3; i++)
		if (w->handlers[i]) G(g_signal_handler_disconnect)(w->m, w->handlers[i]);
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	if (w->sub && c) G(g_dbus_connection_signal_unsubscribe)(c, w->sub);
	w->done = true;
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(w->call, &e);
	finish(w);
}

static void m_watch(network_Monitor *self, network_fn_Status changed, asuka_call *call) {
	asuka_error e = {0};
	GNetworkMonitor *m = monitor(&e);
	if (!m) {
		asuka_call_fail(call, &e);
		return;
	}
	watching *w = calloc(1, sizeof *w);
	w->changed = changed;
	w->call = call;
	w->m = m;
	w->first = true;
	w->handlers[0] = g_signal_connect(m, "network-changed", on_changed, w);
	w->handlers[1] = g_signal_connect(m, "notify::network-metered", on_notify, w);
	w->handlers[2] = g_signal_connect(m, "notify::connectivity", on_notify, w);
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	// The primary connection changing (Wi-Fi to a cable) isn't the
	// monitor's change.
	if (c)
		w->sub = G(g_dbus_connection_signal_subscribe)(c, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged",
		                                               "/org/freedesktop/NetworkManager", NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_signal, w, NULL);
	read(w);
	asuka_call_on_cancel(call, stop, w);
}

static const network_Monitor_vtable vt = {.current = m_current, .watch = m_watch};

network_Monitor *network_make_Monitor(asuka_error *err) {
	network_Monitor *m = malloc(sizeof *m);
	m->vt = &vt;
	return m;
}
