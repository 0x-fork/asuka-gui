// dbus.c: what the host's D-Bus services share: the session bus, a
// connection of one's own, calls answered later, objects exported from
// introspection XML, signals emitted and followed.
#include "host.h"

#include <stdio.h>
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

// lx_close_bus closes a connection of one's own and lets it go, what
// it has queued sent first: closing doesn't send it, and a call's reply
// (a menu item's click) is queued as the app hears of the call, which
// may close it (or quit) then.
void lx_close_bus(GDBusConnection *c) {
	if (!c) return;
	g_dbus_connection_flush_sync(c, NULL, NULL);
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

// ---- XDG desktop portals ----
//
// What the modules use that GTK doesn't already go through them for. They
// work in a Flatpak sandbox and out of one, wherever xdg-desktop-portal
// runs.

typedef struct {
	GDBusConnection *conn;
	char *path;
	guint sub;
	bool answered;
	int refs;  // the call's, and the answer's until it comes
	lx_portal_done done;
	void *ctx;
} portal_req;

static void portal_unref(portal_req *q) {
	if (--q->refs) return;
	free(q->path);
	free(q);
}

static void portal_end(portal_req *q, uint32_t code, GVariant *results, const char *err) {
	q->answered = true;
	if (q->sub) g_dbus_connection_signal_unsubscribe(q->conn, q->sub);
	q->sub = 0;
	q->done(q->ctx, code, results, err);
}

static void portal_response(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
	(void)c;
	(void)sender;
	(void)path;
	(void)iface;
	(void)signal;
	portal_req *q = data;
	if (q->answered) return;
	guint32 code = 2;
	GVariant *results = NULL;
	if (g_variant_is_of_type(params, G_VARIANT_TYPE("(ua{sv})"))) g_variant_get(params, "(u@a{sv})", &code, &results);
	portal_end(q, code, results, NULL);
	if (results) g_variant_unref(results);
	portal_unref(q);
}

static void portal_listen(portal_req *q) {
	q->sub = g_dbus_connection_signal_subscribe(q->conn, LX_PORTAL, "org.freedesktop.portal.Request", "Response", q->path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, portal_response, q, NULL);
}

static void portal_called(void *ctx, GVariant *rep, const char *err) {
	portal_req *q = ctx;
	if (q->answered) {  // answered before the call returned
		portal_unref(q);
		return;
	}
	if (err) {
		portal_end(q, 2, NULL, err);
		portal_unref(q);  // the answer's
		portal_unref(q);
		return;
	}
	// An older portal's request path isn't the token's.
	const gchar *p = NULL;
	if (rep && g_variant_is_of_type(rep, G_VARIANT_TYPE("(o)"))) g_variant_get(rep, "(&o)", &p);
	if (p && strcmp(p, q->path)) {
		g_dbus_connection_signal_unsubscribe(q->conn, q->sub);
		free(q->path);
		q->path = str_dup(ak_cstr(p));
		portal_listen(q);
	}
	portal_unref(q);
}

static void portal_called_fds(GObject *src, GAsyncResult *res, gpointer data) {
	GError *e = NULL;
	GVariant *r = g_dbus_connection_call_with_unix_fd_list_finish((GDBusConnection *)src, NULL, res, &e);
	portal_called(data, r, e ? e->message : NULL);
	if (r) g_variant_unref(r);
	if (e) g_error_free(e);
}

// lx_portal_request calls a portal's method that answers through a
// Request (its Response signal): params gets the handle token to put in
// its options and returns the call's parameters (floating). done gets the
// response (0 done, 1 cancelled by the user, 2 another end) and its
// results (a{sv}; NULL on an error), once, on the main thread.
void lx_portal_request(const char *iface, const char *method, GVariant *(*params)(const char *token, void *ctx), lx_portal_done done, void *ctx) {
	lx_portal_request_fds(iface, method, params, NULL, done, ctx);
}

// lx_portal_request_fds is lx_portal_request passing file descriptors
// (the call's handles index them).
void lx_portal_request_fds(const char *iface, const char *method, GVariant *(*params)(const char *token, void *ctx), GUnixFDList *fds, lx_portal_done done, void *ctx) {
	GDBusConnection *bus = lx_session_bus();
	if (!bus) {
		done(ctx, 2, NULL, "no session bus");
		return;
	}
	static unsigned tokens;
	char token[32];
	snprintf(token, sizeof token, "asuka%u", ++tokens);
	ak_buf sender = {0};
	for (const char *s = g_dbus_connection_get_unique_name(bus) + 1; *s; s++) ak_buf_u8(&sender, *s == '.' ? '_' : (uint8_t)*s);
	portal_req *q = calloc(1, sizeof *q);
	if (!q) abort();
	q->conn = bus;
	q->refs = 2;
	q->done = done;
	q->ctx = ctx;
	char path[512];
	snprintf(path, sizeof path, "/org/freedesktop/portal/desktop/request/%s/%s", lx_buf_cstr(&sender), token);
	ak_buf_free(&sender);
	q->path = str_dup(ak_cstr(path));
	portal_listen(q);
	if (fds) {
		g_dbus_connection_call_with_unix_fd_list(bus, LX_PORTAL, LX_PORTAL_PATH, iface, method, params(token, ctx), NULL, G_DBUS_CALL_FLAGS_NONE, -1, fds, NULL, portal_called_fds, q);
		return;
	}
	lx_dbus_call(bus, LX_PORTAL, LX_PORTAL_PATH, iface, method, params(token, ctx), -1, portal_called, q);
}

// lx_no_portal reports whether a portal call's error is the portal's (or
// the interface's) not being there.
bool lx_no_portal(const char *err) {
	static const char *const marks[] = {"ServiceUnknown", "UnknownMethod", "UnknownInterface", "UnknownObject", "No such interface", "NameHasNoOwner"};
	if (!err) return false;
	for (size_t i = 0; i < AK_LEN(marks); i++)
		if (strstr(err, marks[i])) return true;
	return false;
}
