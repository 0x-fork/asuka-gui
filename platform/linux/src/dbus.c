// dbus.c: what the host's D-Bus services share: the session bus, a
// connection of one's own, calls answered later, objects exported from
// introspection XML, signals emitted and followed.
#include "host.h"

#include <stdlib.h>

// lx_session_bus is the session bus (GIO's shared connection; NULL with
// none: a headless system).
GDBusConnection *lx_session_bus(void) {
	static GDBusConnection *bus;
	static bool tried;
	if (tried) return bus;
	tried = true;
	GError *e = NULL;
	bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &e);
	if (e) g_error_free(e);
	return bus;
}

// lx_private_bus is a new connection to the session bus of the caller's
// own (g_bus_get's is shared): NULL with what went wrong in *err.
GDBusConnection *lx_private_bus(char **err) {
	GError *e = NULL;
	gchar *addr = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, NULL, &e);
	if (!addr) {
		*err = g_strdup(e ? e->message : "no session bus");
		if (e) g_error_free(e);
		return NULL;
	}
	GDBusConnection *c = g_dbus_connection_new_for_address_sync(addr, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &e);
	g_free(addr);
	if (!c) {
		*err = g_strdup(e ? e->message : "no session bus");
		if (e) g_error_free(e);
	}
	return c;
}

// lx_close_bus closes a connection of one's own and lets it go.
void lx_close_bus(GDBusConnection *c) {
	if (!c) return;
	g_dbus_connection_close_sync(c, NULL, NULL);
	g_object_unref(c);
}

typedef struct {
	lx_dbus_reply cb;
	void *ctx;
} call_t;

static void call_done(GObject *src, GAsyncResult *res, gpointer data) {
	call_t *c = data;
	GError *e = NULL;
	GVariant *r = g_dbus_connection_call_finish((GDBusConnection *)src, res, &e);
	if (c->cb) c->cb(c->ctx, r, e ? e->message : NULL);
	if (r) g_variant_unref(r);
	if (e) g_error_free(e);
	free(c);
}

// lx_dbus_call calls a method (params floating, taken) and gives cb its
// answer or its error, later; cb may be NULL.
void lx_dbus_call(GDBusConnection *conn, const char *dest, const char *path, const char *iface, const char *method, GVariant *params, int timeout_ms, lx_dbus_reply cb, void *ctx) {
	call_t *c = malloc(sizeof *c);
	if (!c) abort();
	c->cb = cb;
	c->ctx = ctx;
	g_dbus_connection_call(conn, dest, path, iface, method, params, NULL, G_DBUS_CALL_FLAGS_NONE, timeout_ms, NULL, call_done, c);
}

// lx_dbus_export exports an object implementing one interface of the
// introspection XML: 0 with what went wrong in *err.
guint lx_dbus_export(GDBusConnection *conn, const char *path, const char *xml, const char *iface, const GDBusInterfaceVTable *vt, void *ctx, char **err) {
	GError *e = NULL;
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &e);
	if (!info) {
		*err = g_strdup(e ? e->message : "bad introspection XML");
		if (e) g_error_free(e);
		return 0;
	}
	GDBusInterfaceInfo *ii = g_dbus_node_info_lookup_interface(info, iface);
	guint id = ii ? g_dbus_connection_register_object(conn, path, ii, vt, ctx, NULL, &e) : 0;
	g_dbus_node_info_unref(info);  // the registration keeps the interface's
	if (!id) {
		*err = g_strdup(e ? e->message : "no such interface in the XML");
		if (e) g_error_free(e);
	}
	return id;
}

// lx_dbus_emit emits a signal (params floating, taken; NULL: none).
void lx_dbus_emit(GDBusConnection *conn, const char *path, const char *iface, const char *name, GVariant *params) {
	if (!conn) {
		if (params) g_variant_unref(g_variant_ref_sink(params));
		return;
	}
	g_dbus_connection_emit_signal(conn, NULL, path, iface, name, params, NULL);
}
