// fakeportal.c: a location portal (org.freedesktop.portal.Location) on
// the session bus, for asuka/location's Linux tests: it owns
// org.freedesktop.portal.Desktop, answers CreateSession and Start as
// xdg-desktop-portal does (Start's answer a Request's Response signal),
// and moves each started session north from 52.52° N 13.405° E, 0.001°
// (about 111 m) every 50 ms, as LocationUpdated signals.
//
//	fakeportal allow|deny   prints "ready" once the name is its
//
// GIO is resolved at run time, as the packages' C has it, so it builds
// with nothing but a C compiler.
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _GVariant GVariant;
typedef struct {
	uint32_t domain;
	int code;
	char *message;
} GError;
typedef void GDBusConnection;
typedef void GDBusMethodInvocation;
typedef void GDBusNodeInfo;
typedef void GMainLoop;
typedef struct {
	void (*method_call)(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *method, GVariant *params,
	                    GDBusMethodInvocation *inv, void *data);
	void *get_property, *set_property;
	void *padding[8];
} vtable;

static GDBusConnection *(*bus_get_sync)(int, void *, GError **);
static GDBusNodeInfo *(*node_info_new_for_xml)(const char *, GError **);
static void *(*node_info_lookup_interface)(GDBusNodeInfo *, const char *);
static unsigned (*register_object)(GDBusConnection *, const char *, void *, const vtable *, void *, void *, GError **);
static int (*unregister_object)(GDBusConnection *, unsigned);
static GVariant *(*call_sync)(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *, const void *, int, int, void *,
                              GError **);
static void (*return_value)(GDBusMethodInvocation *, GVariant *);
static int (*emit_signal)(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *, GError **);
static GVariant *(*variant_new)(const char *, ...);
static GVariant *(*new_parsed)(const char *, ...);
static GVariant *(*get_child_value)(GVariant *, size_t);
static GVariant *(*lookup_value)(GVariant *, const char *, const void *);
static const char *(*get_string)(GVariant *, size_t *);
static void (*variant_unref)(GVariant *);
static unsigned (*timeout_add)(unsigned, int (*)(void *), void *);
static int (*source_remove)(unsigned);
static GMainLoop *(*main_loop_new)(void *, int);
static void (*main_loop_run)(GMainLoop *);

static bool allow;
static GDBusConnection *conn;
static void *location_iface, *session_iface;

#define DESKTOP "/org/freedesktop/portal/desktop"

typedef struct session {
	char *path, *request, *peer;
	unsigned timer, reg;
	int step;
	struct session *next;
} session;

static session *sessions;

static const char xml[] = "<node>"
                          " <interface name='org.freedesktop.portal.Location'>"
                          "  <method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
                          "  <method name='Start'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/>"
                          "   <arg type='o' direction='out'/></method>"
                          "  <signal name='LocationUpdated'><arg type='o'/><arg type='a{sv}'/></signal>"
                          " </interface>"
                          " <interface name='org.freedesktop.portal.Session'><method name='Close'/></interface>"
                          "</node>";

// option is an option's string (session_handle_token, handle_token).
static char *option(GVariant *options, const char *key) {
	GVariant *v = lookup_value(options, key, "s");
	char *s = strdup(v ? get_string(v, NULL) : "t");
	if (v) variant_unref(v);
	return s;
}

// object is a session's or a request's path, as the portal makes them.
static char *object(const char *kind, const char *sender, const char *tok) {
	char *s = strdup(sender[0] == ':' ? sender + 1 : sender);
	for (char *p = s; *p; p++)
		if (*p == '.') *p = '_';
	size_t n = strlen(kind) + strlen(s) + strlen(tok) + 64;
	char *path = malloc(n);
	snprintf(path, n, DESKTOP "/%s/%s/%s", kind, s, tok);
	free(s);
	return path;
}

static session *find(const char *path) {
	for (session *s = sessions; s; s = s->next)
		if (!strcmp(s->path, path)) return s;
	return NULL;
}

static int move(void *p) {
	session *s = p;
	if (s->step >= 400) return 0;
	GVariant *params = new_parsed("(%o, {'Latitude': <%d>, 'Longitude': <13.405>, 'Accuracy': <10.0>, 'Altitude': <34.0>, 'Speed': <1.5>,"
	                              " 'Heading': <90.0>, 'Timestamp': <(%t, %t)>})",
	                              s->path, 52.52 + 0.001 * s->step, (uint64_t)(1700000000 + s->step), (uint64_t)500000);
	emit_signal(conn, s->peer, DESKTOP, "org.freedesktop.portal.Location", "LocationUpdated", params, NULL);
	s->step++;
	return 1;
}

static int respond(void *p) {
	session *s = p;
	emit_signal(conn, s->peer, s->request, "org.freedesktop.portal.Request", "Response", new_parsed("(%u, @a{sv} {})", allow ? 0u : 1u), NULL);
	if (allow) {
		move(s);
		s->timer = timeout_add(50, move, s);
	}
	return 0;
}

static void call(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *method, GVariant *params,
                 GDBusMethodInvocation *inv, void *data) {
	if (!strcmp(method, "CreateSession")) {
		GVariant *options = get_child_value(params, 0);
		char *tok = option(options, "session_handle_token");
		variant_unref(options);
		session *s = calloc(1, sizeof *s);
		s->path = object("session", sender, tok);
		s->peer = strdup(sender);
		free(tok);
		s->reg = register_object(conn, s->path, session_iface, data, NULL, NULL, NULL);
		s->next = sessions;
		sessions = s;
		return_value(inv, variant_new("(o)", s->path));
	} else if (!strcmp(method, "Start")) {
		GVariant *at = get_child_value(params, 0), *options = get_child_value(params, 2);
		session *s = find(get_string(at, NULL));
		char *tok = option(options, "handle_token");
		variant_unref(at);
		variant_unref(options);
		char *request = object("request", sender, tok);
		free(tok);
		return_value(inv, variant_new("(o)", request));
		if (s) {
			free(s->request);
			s->request = request;
			timeout_add(10, respond, s);
		} else {
			free(request);
		}
	} else if (!strcmp(method, "Close")) {
		for (session **q = &sessions; *q; q = &(*q)->next)
			if (!strcmp((*q)->path, path)) {
				session *s = *q;
				*q = s->next;
				if (s->timer) source_remove(s->timer);
				unregister_object(conn, s->reg);
				break;  // its strings stay: a timeout may still hold it
			}
		return_value(inv, NULL);
	}
}

static void *sym(void *lib, const char *name) {
	void *p = dlsym(lib, name);
	if (!p) {
		fprintf(stderr, "fakeportal: no %s\n", name);
		exit(1);
	}
	return p;
}

int main(int argc, char **argv) {
	allow = argc < 2 || strcmp(argv[1], "deny") != 0;
	void *glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_GLOBAL), *gio = dlopen("libgio-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
	if (!glib || !gio) {
		fprintf(stderr, "fakeportal: no GIO\n");
		return 1;
	}
	*(void **)&bus_get_sync = sym(gio, "g_bus_get_sync");
	*(void **)&node_info_new_for_xml = sym(gio, "g_dbus_node_info_new_for_xml");
	*(void **)&node_info_lookup_interface = sym(gio, "g_dbus_node_info_lookup_interface");
	*(void **)&register_object = sym(gio, "g_dbus_connection_register_object");
	*(void **)&unregister_object = sym(gio, "g_dbus_connection_unregister_object");
	*(void **)&call_sync = sym(gio, "g_dbus_connection_call_sync");
	*(void **)&return_value = sym(gio, "g_dbus_method_invocation_return_value");
	*(void **)&emit_signal = sym(gio, "g_dbus_connection_emit_signal");
	*(void **)&variant_new = sym(glib, "g_variant_new");
	*(void **)&new_parsed = sym(glib, "g_variant_new_parsed");
	*(void **)&get_child_value = sym(glib, "g_variant_get_child_value");
	*(void **)&lookup_value = sym(glib, "g_variant_lookup_value");
	*(void **)&get_string = sym(glib, "g_variant_get_string");
	*(void **)&variant_unref = sym(glib, "g_variant_unref");
	*(void **)&timeout_add = sym(glib, "g_timeout_add");
	*(void **)&source_remove = sym(glib, "g_source_remove");
	*(void **)&main_loop_new = sym(glib, "g_main_loop_new");
	*(void **)&main_loop_run = sym(glib, "g_main_loop_run");
	GError *e = NULL;
	conn = bus_get_sync(2, NULL, &e);  // G_BUS_TYPE_SESSION
	if (!conn) {
		fprintf(stderr, "fakeportal: %s\n", e ? e->message : "no session bus");
		return 1;
	}
	GDBusNodeInfo *node = node_info_new_for_xml(xml, &e);
	location_iface = node_info_lookup_interface(node, "org.freedesktop.portal.Location");
	session_iface = node_info_lookup_interface(node, "org.freedesktop.portal.Session");
	static vtable vt = {.method_call = call};
	if (!register_object(conn, DESKTOP, location_iface, &vt, &vt, NULL, &e)) {
		fprintf(stderr, "fakeportal: %s\n", e->message);
		return 1;
	}
	// DBUS_NAME_FLAG_DO_NOT_QUEUE; the reply's 1: the primary owner.
	GVariant *r = call_sync(conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
	                        variant_new("(su)", "org.freedesktop.portal.Desktop", 4u), NULL, 0, -1, NULL, &e);
	if (!r) {
		fprintf(stderr, "fakeportal: %s\n", e->message);
		return 1;
	}
	printf("ready\n");
	fflush(stdout);
	main_loop_run(main_loop_new(NULL, 0));
	return 0;
}
