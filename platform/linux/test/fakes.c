// fakes.c: what a desktop session has that the tests' session bus
// hasn't, so the host's D-Bus services can be driven under Xvfb: a
// notification server (org.freedesktop.Notifications: Notify,
// CloseNotification, GetCapabilities, GetServerInformation) and a
// StatusNotifierWatcher with a host registered (org.kde.
// StatusNotifierWatcher). What the host asks of them is written to the
// log (argv[1]), a line each, flushed: "notify <app> <summary> <body>",
// "close <id>", "register <service>". It runs until it's killed.
//
// Built with the host's own declarations (syms.c resolves GLib and GIO).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtk.h"

static FILE *out;
static guint32 next_id;

static const char notify_xml[] =
	"<node><interface name='org.freedesktop.Notifications'>"
	"<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/>"
	"<arg type='s' direction='in'/><arg type='as' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/><arg type='u' direction='out'/></method>"
	"<method name='CloseNotification'><arg type='u' direction='in'/></method>"
	"<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
	"<method name='GetServerInformation'><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
	"<signal name='NotificationClosed'><arg type='u'/><arg type='u'/></signal>"
	"<signal name='ActionInvoked'><arg type='u'/><arg type='s'/></signal>"
	"</interface></node>";

static const char watcher_xml[] =
	"<node><interface name='org.kde.StatusNotifierWatcher'>"
	"<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>"
	"<method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>"
	"<property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
	"<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
	"<property name='ProtocolVersion' type='i' access='read'/>"
	"<signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>"
	"<signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>"
	"<signal name='StatusNotifierHostRegistered'/>"
	"</interface></node>";

static void on_notify_call(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
	(void)sender;
	(void)iface;
	(void)data;
	if (!strcmp(method, "Notify")) {
		const gchar *app, *icon, *summary, *body;
		guint32 replaces;
		g_variant_get_child(params, 0, "&s", &app);
		g_variant_get_child(params, 1, "u", &replaces);
		g_variant_get_child(params, 2, "&s", &icon);
		g_variant_get_child(params, 3, "&s", &summary);
		g_variant_get_child(params, 4, "&s", &body);
		guint32 id = replaces ? replaces : ++next_id;
		fprintf(out, "notify %s %s %s\n", app, summary, body);
		fflush(out);
		g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", id));
		return;
	}
	if (!strcmp(method, "CloseNotification")) {
		guint32 id;
		g_variant_get(params, "(u)", &id);
		fprintf(out, "close %u\n", id);
		fflush(out);
		g_dbus_connection_emit_signal(c, NULL, path, "org.freedesktop.Notifications", "NotificationClosed", g_variant_new("(uu)", id, 3), NULL);
		g_dbus_method_invocation_return_value(inv, NULL);
		return;
	}
	if (!strcmp(method, "GetCapabilities")) {
		const char *caps[] = {"actions", "body", NULL};
		g_dbus_method_invocation_return_value(inv, g_variant_new("(@as)", g_variant_new_strv(caps, -1)));
		return;
	}
	g_dbus_method_invocation_return_value(inv, g_variant_new("(ssss)", "fakes", "asuka", "1", "1.2"));
}

static void on_watcher_call(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
	(void)c;
	(void)sender;
	(void)path;
	(void)iface;
	(void)data;
	if (!strcmp(method, "RegisterStatusNotifierItem")) {
		const gchar *service;
		g_variant_get(params, "(&s)", &service);
		fprintf(out, "register %s\n", service);
		fflush(out);
	}
	g_dbus_method_invocation_return_value(inv, NULL);
}

static GVariant *on_watcher_property(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GError **err, gpointer data) {
	(void)c;
	(void)sender;
	(void)path;
	(void)iface;
	(void)err;
	(void)data;
	if (!strcmp(name, "IsStatusNotifierHostRegistered")) return g_variant_new_boolean(1);
	if (!strcmp(name, "ProtocolVersion")) return g_variant_new_int32(0);
	return g_variant_new_strv(NULL, 0);
}

static const GDBusInterfaceVTable notify_vt = {on_notify_call, NULL, NULL, {0}};
static const GDBusInterfaceVTable watcher_vt = {on_watcher_call, on_watcher_property, NULL, {0}};

static void export(GDBusConnection *bus, const char *path, const char *xml, const char *iface, const GDBusInterfaceVTable *vt) {
	GError *e = NULL;
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &e);
	if (!info || !g_dbus_connection_register_object(bus, path, g_dbus_node_info_lookup_interface(info, iface), vt, NULL, NULL, &e)) {
		fprintf(stderr, "fakes: %s: %s\n", iface, e ? e->message : "?");
		exit(1);
	}
}

static void own(GDBusConnection *bus, const char *name) {
	GVariant *r = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", name, 4 /* do not queue */), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
	guint32 code = 0;
	if (r) g_variant_get(r, "(u)", &code);
	if (code != 1) {
		fprintf(stderr, "fakes: can't own %s (%u)\n", name, code);
		exit(1);
	}
	g_variant_unref(r);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: fakes <log>\n");
		return 2;
	}
	char err[512];
	if (!lx_gtk_load(err, sizeof err)) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	out = fopen(argv[1], "w");
	if (!out) return 1;
	GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	if (!bus) {
		fprintf(stderr, "fakes: no session bus\n");
		return 1;
	}
	export(bus, "/org/freedesktop/Notifications", notify_xml, "org.freedesktop.Notifications", &notify_vt);
	export(bus, "/StatusNotifierWatcher", watcher_xml, "org.kde.StatusNotifierWatcher", &watcher_vt);
	own(bus, "org.freedesktop.Notifications");
	own(bus, "org.kde.StatusNotifierWatcher");
	fprintf(out, "ready\n");
	fflush(out);
	for (;;) g_main_context_iteration(NULL, 1);
}
