// Linux: the location portal (org.freedesktop.portal.Location, which
// asks the user and works in a sandbox), else GeoClue2 on the system bus;
// a place's name through geocode-glib, where it's installed. Neither says
// what the user allowed before an app asks for a position, so it's what
// this run has seen: notAsked until a position comes (always: a desktop
// app runs out of sight) or a refusal (denied). No geofences: the
// package computes the regions from the positions. Main thread.
#include "asuka_linux.h"
#include "location.h"

#include <errno.h>
#include <math.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

extern char **environ;

#define PORTAL "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_LOCATION "org.freedesktop.portal.Location"
#define GEOCLUE "org.freedesktop.GeoClue2"

// ---- What's been seen ----

static struct {
	bool known, allowed;
} seen;

location_Authorization location_access(void) {
	if (!seen.known) return (location_Authorization){.level = location_Level_notAsked};
	if (seen.allowed) return (location_Authorization){.level = location_Level_always, .precise = true};
	return (location_Authorization){.level = location_Level_denied};
}

static void saw(bool allowed) {
	if (seen.known && seen.allowed == allowed) return;
	seen.known = true;
	seen.allowed = allowed;
	location_authorization_changed();
}

static bool is_denied(const asuka_error *e) { return e->status == ASUKA_DECLARED && e->index == location_LocationError_denied; }

static void denied(asuka_error *e) { asuka_error_declared(e, location_LocationError_denied); }

// no_service reports whether a D-Bus error says there's no such service
// (or method): the portal's missing, or GeoClue is.
static bool no_service(const GError *e) {
	static const char *const names[] = {"ServiceUnknown", "UnknownMethod", "UnknownInterface", "UnknownObject", "No such interface", "NameHasNoOwner"};
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (e && e->message && strstr(e->message, names[i])) return true;
	return false;
}

static double number(GVariant *dict, const char *key, bool *ok) {
	GVariant *v = G(g_variant_lookup_value)(dict, key, G_VARIANT_TYPE("d"));
	if (ok) *ok = v != NULL;
	if (!v) return 0;
	double d = G(g_variant_get_double)(v);
	G(g_variant_unref)(v);
	return d;
}

// position is the portal's or GeoClue's properties (a{sv}) as a Position.
static location_Fix position(GVariant *dict) {
	location_Fix p = {0};
	p.latitude = number(dict, "Latitude", NULL);
	p.longitude = number(dict, "Longitude", NULL);
	p.accuracy = number(dict, "Accuracy", NULL);
	double alt = number(dict, "Altitude", NULL);
	p.altitude = alt < -1e300 || isnan(alt) ? 0 : alt;  // -DBL_MAX: not known
	bool ok;
	double v = number(dict, "Speed", &ok);
	p.speed = ok && v >= 0 && !isnan(v) ? v : -1;
	v = number(dict, "Heading", &ok);
	p.course = ok && v >= 0 && !isnan(v) ? v : -1;
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	p.time = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
	GVariant *ts = G(g_variant_lookup_value)(dict, "Timestamp", G_VARIANT_TYPE("(tt)"));
	if (ts) {
		guint64 s = 0, us = 0;
		G(g_variant_get)(ts, "(tt)", &s, &us);
		if (s > 0) p.time = (int64_t)s * 1000000000 + (int64_t)us * 1000;
		G(g_variant_unref)(ts);
	}
	return p;
}

// ---- Sources: the portal's session, or GeoClue's client ----

typedef struct src {
	int refs;  // the stopper's, and each call and subscription's
	bool stopped;
	location_sink sink;
	GDBusConnection *conn;
	bool geoclue;
	char *session;  // the portal's session, GeoClue's client
	guint sub, resp;
	struct src *fallback;  // GeoClue's, when there's no portal
} src;

static src *ref(src *s) {
	s->refs++;
	return s;
}

static void unref(gpointer p) {
	src *s = p;
	if (--s->refs > 0) return;
	free(s->session);
	free(s);
}

static void moved(src *s, const location_Fix *p) {
	if (s->stopped) return;
	saw(true);
	s->sink.moved(s->sink.ctx, p);
}

static void failed(src *s, asuka_error *e) {
	if (s->stopped) {
		asuka_error_free(e);
		return;
	}
	if (is_denied(e)) saw(false);
	s->sink.failed(s->sink.ctx, e);
}

static void fail_gerror(src *s, GError *ge, const char *what) {
	asuka_error e = {0};
	asuka_linux_fail(&e, ge, what);
	failed(s, &e);
}

static void ignored(GObject *o, GAsyncResult *res, gpointer data) {
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, NULL);
	if (r) G(g_variant_unref)(r);
}

static char *token(void) {
	static int n;
	return asuka_sprintf("asukaloc%d", ++n);
}

// ---- GeoClue2, on the system bus ----

static void geoclue_stop(src *s) {
	if (s->stopped) return;
	s->stopped = true;
	if (s->sub) G(g_dbus_connection_signal_unsubscribe)(s->conn, s->sub);
	s->sub = 0;
	if (s->session)
		G(g_dbus_connection_call)(s->conn, GEOCLUE, s->session, GEOCLUE ".Client", "Stop", NULL, NULL, 0, -1, NULL, ignored, NULL);
}

static void geoclue_failed(src *s, GError *ge) {
	if (s->stopped) {
		if (ge) G(g_error_free)(ge);
		return;
	}
	bool access = ge && ge->message && strstr(ge->message, "AccessDenied");
	bool none = no_service(ge);
	geoclue_stop(s);
	s->stopped = false;  // to say why
	asuka_error e = {0};
	if (access) {
		denied(&e);
		G(g_error_free)(ge);
	} else if (none) {
		asuka_error_failed(&e, "gio", ge->code, "location: no location service (GeoClue isn't installed)");
		G(g_error_free)(ge);
	} else {
		asuka_linux_fail(&e, ge, "location");
	}
	failed(s, &e);
	s->stopped = true;
}

static void geoclue_got(GObject *o, GAsyncResult *res, gpointer data) {
	src *s = data;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, NULL);
	if (r && !s->stopped) {
		GVariant *props = G(g_variant_get_child_value)(r, 0);
		location_Fix p = position(props);
		G(g_variant_unref)(props);
		moved(s, &p);
	}
	if (r) G(g_variant_unref)(r);
	unref(s);
}

static void geoclue_updated(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal,
                            GVariant *params, gpointer data) {
	src *s = data;
	if (s->stopped || G(g_variant_n_children)(params) < 2) return;
	const gchar *at = NULL;
	G(g_variant_get)(params, "(&o&o)", NULL, &at);
	G(g_dbus_connection_call)(c, GEOCLUE, at, "org.freedesktop.DBus.Properties", "GetAll", G(g_variant_new)("(s)", GEOCLUE ".Location"),
	                          G_VARIANT_TYPE("(a{sv})"), 0, -1, NULL, geoclue_got, ref(s));
}

static void geoclue_started(GObject *o, GAsyncResult *res, gpointer data) {
	src *s = data;
	GError *ge = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, &ge);
	if (r) G(g_variant_unref)(r);
	else geoclue_failed(s, ge);
	unref(s);
}

static void geoclue_set(src *s, const char *prop, GVariant *v, GAsyncReadyCallback then);

static void geoclue_accuracy_set(GObject *o, GAsyncResult *res, gpointer data) {
	src *s = data;
	GError *ge = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, &ge);
	if (r) {
		G(g_variant_unref)(r);
		if (!s->stopped)
			G(g_dbus_connection_call)(s->conn, GEOCLUE, s->session, GEOCLUE ".Client", "Start", NULL, NULL, 0, -1, NULL, geoclue_started, ref(s));
	} else {
		geoclue_failed(s, ge);
	}
	unref(s);
}

static void geoclue_id_set(GObject *o, GAsyncResult *res, gpointer data) {
	src *s = data;
	GError *ge = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, &ge);
	if (r) {
		G(g_variant_unref)(r);
		if (!s->stopped) geoclue_set(s, "RequestedAccuracyLevel", G(g_variant_new_uint32)(8), geoclue_accuracy_set);  // exact
	} else {
		geoclue_failed(s, ge);
	}
	unref(s);
}

static void geoclue_set(src *s, const char *prop, GVariant *v, GAsyncReadyCallback then) {
	G(g_dbus_connection_call)(s->conn, GEOCLUE, s->session, "org.freedesktop.DBus.Properties", "Set",
	                          G(g_variant_new)("(ssv)", GEOCLUE ".Client", prop, v), NULL, 0, -1, NULL, then, ref(s));
}

static void geoclue_client(GObject *o, GAsyncResult *res, gpointer data) {
	src *s = data;
	GError *ge = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, &ge);
	if (!r) {
		geoclue_failed(s, ge);
		unref(s);
		return;
	}
	const gchar *client = NULL;
	G(g_variant_get)(r, "(&o)", &client);
	s->session = asuka_strdup(client);
	G(g_variant_unref)(r);
	if (s->stopped) {
		G(g_dbus_connection_call)(s->conn, GEOCLUE, s->session, GEOCLUE ".Client", "Stop", NULL, NULL, 0, -1, NULL, ignored, NULL);
		unref(s);
		return;
	}
	s->sub = G(g_dbus_connection_signal_subscribe)(s->conn, GEOCLUE, GEOCLUE ".Client", "LocationUpdated", s->session, NULL, 0,
	                                                geoclue_updated, ref(s), unref);
	// GeoClue's agent allows apps by their desktop file's name.
	char *id = asuka_strdup(program_invocation_short_name);
	char *dot = strrchr(id, '.');
	if (dot && dot != id) *dot = 0;
	geoclue_set(s, "DesktopId", G(g_variant_new_string)(id), geoclue_id_set);
	free(id);
	unref(s);
}

static src *geoclue(location_sink sink) {
	src *s = calloc(1, sizeof *s);
	s->refs = 1;
	s->sink = sink;
	s->geoclue = true;
	GError *ge = NULL;
	s->conn = asuka_linux_bus(G_BUS_TYPE_SYSTEM, &ge);
	if (!s->conn) {
		asuka_error e = {0};
		asuka_error_failed(&e, "gio", ge ? ge->code : 0, "location: no location service (%s)", ge && ge->message ? ge->message : "no system bus");
		if (ge) G(g_error_free)(ge);
		failed(s, &e);
		s->stopped = true;
		return s;
	}
	G(g_dbus_connection_call)(s->conn, GEOCLUE, "/org/freedesktop/GeoClue2/Manager", GEOCLUE ".Manager", "GetClient", NULL,
	                          G_VARIANT_TYPE("(o)"), 0, -1, NULL, geoclue_client, ref(s));
	return s;
}

// ---- The location portal, on the session bus ----

static void portal_updated(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal,
                           GVariant *params, gpointer data) {
	src *s = data;
	if (s->stopped || !s->session || G(g_variant_n_children)(params) < 2) return;
	GVariant *session = G(g_variant_get_child_value)(params, 0);
	bool ours = !strcmp(G(g_variant_get_string)(session, NULL), s->session);
	G(g_variant_unref)(session);
	if (!ours) return;
	GVariant *props = G(g_variant_get_child_value)(params, 1);
	location_Fix p = position(props);
	G(g_variant_unref)(props);
	moved(s, &p);
}

// The Start request's Response: whether the user allowed it.
static void portal_response(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal,
                            GVariant *params, gpointer data) {
	src *s = data;
	if (s->resp) G(g_dbus_connection_signal_unsubscribe)(s->conn, s->resp);
	s->resp = 0;
	guint32 code = 2;
	if (G(g_variant_n_children)(params) > 0) {
		GVariant *v = G(g_variant_get_child_value)(params, 0);
		code = G(g_variant_get_uint32)(v);
		G(g_variant_unref)(v);
	}
	if (code != 0 && !s->stopped) {
		asuka_error e = {0};
		denied(&e);
		failed(s, &e);
	}
}

static void portal_started(GObject *o, GAsyncResult *res, gpointer data) {
	src *s = data;
	GError *ge = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, &ge);
	if (r) G(g_variant_unref)(r);
	else if (!s->stopped) fail_gerror(s, ge, "location");
	else G(g_error_free)(ge);
	unref(s);
}

static void portal_session(GObject *o, GAsyncResult *res, gpointer data) {
	src *s = data;
	GError *ge = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)o, res, &ge);
	if (s->stopped) {
		if (r) {
			const gchar *session = NULL;
			G(g_variant_get)(r, "(&o)", &session);
			G(g_dbus_connection_call)(s->conn, PORTAL, session, "org.freedesktop.portal.Session", "Close", NULL, NULL, 0, -1, NULL, ignored, NULL);
			G(g_variant_unref)(r);
		} else {
			G(g_error_free)(ge);
		}
		unref(s);
		return;
	}
	if (!r) {
		if (no_service(ge)) {
			G(g_error_free)(ge);
			s->fallback = geoclue(s->sink);
		} else if (ge->message && strstr(ge->message, "NotAllowed")) {
			G(g_error_free)(ge);
			asuka_error e = {0};
			denied(&e);
			failed(s, &e);
		} else {
			fail_gerror(s, ge, "location");
		}
		unref(s);
		return;
	}
	const gchar *session = NULL;
	G(g_variant_get)(r, "(&o)", &session);
	s->session = asuka_strdup(session);
	G(g_variant_unref)(r);
	s->sub = G(g_dbus_connection_signal_subscribe)(s->conn, PORTAL, PORTAL_LOCATION, "LocationUpdated", PORTAL_PATH, NULL, 0, portal_updated,
	                                                ref(s), unref);
	// Start answers through a Request: its Response says whether the user
	// allowed it.
	char *tok = token();
	const char *unique = G(g_dbus_connection_get_unique_name)(s->conn);
	char *sender = asuka_strdup(unique && *unique == ':' ? unique + 1 : unique);
	for (char *c = sender; *c; c++)
		if (*c == '.') *c = '_';
	char *path = asuka_sprintf(PORTAL_PATH "/request/%s/%s", sender, tok);
	s->resp = G(g_dbus_connection_signal_subscribe)(s->conn, PORTAL, "org.freedesktop.portal.Request", "Response", path, NULL, 0, portal_response,
	                                                 ref(s), unref);
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	G(g_variant_builder_add)(&b, "{sv}", "handle_token", G(g_variant_new_string)(tok));
	G(g_dbus_connection_call)(s->conn, PORTAL, PORTAL_PATH, PORTAL_LOCATION, "Start", G(g_variant_new)("(osa{sv})", s->session, "", &b), NULL, 0, -1,
	                          NULL, portal_started, ref(s));
	free(path);
	free(sender);
	free(tok);
	unref(s);
}

static src *portal(location_sink sink) {
	GDBusConnection *conn = asuka_linux() ? asuka_linux_bus(G_BUS_TYPE_SESSION, NULL) : NULL;
	if (!conn) return geoclue(sink);
	src *s = calloc(1, sizeof *s);
	s->refs = 1;
	s->sink = sink;
	s->conn = conn;
	char *tok = token();
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	G(g_variant_builder_add)(&b, "{sv}", "session_handle_token", G(g_variant_new_string)(tok));
	G(g_variant_builder_add)(&b, "{sv}", "distance-threshold", G(g_variant_new_uint32)(0));
	G(g_variant_builder_add)(&b, "{sv}", "time-threshold", G(g_variant_new_uint32)(0));
	G(g_variant_builder_add)(&b, "{sv}", "accuracy", G(g_variant_new_uint32)(5));  // exact
	G(g_dbus_connection_call)(conn, PORTAL, PORTAL_PATH, PORTAL_LOCATION, "CreateSession", G(g_variant_new)("(a{sv})", &b), G_VARIANT_TYPE("(o)"), 0,
	                          -1, NULL, portal_session, ref(s));
	free(tok);
	return s;
}

void *location_start(location_sink sink) {
	if (!asuka_linux()) {
		asuka_error e = {0};
		asuka_error_unsupported(&e, "location: no GLib");
		sink.failed(sink.ctx, &e);
		return NULL;
	}
	return portal(sink);
}

void location_stop(void *source) {
	src *s = source;
	if (!s) return;
	if (s->fallback) {
		location_stop(s->fallback);
		s->fallback = NULL;
		s->stopped = true;
	} else if (s->geoclue) {
		geoclue_stop(s);
	} else if (!s->stopped) {
		s->stopped = true;
		if (s->sub) G(g_dbus_connection_signal_unsubscribe)(s->conn, s->sub);
		if (s->resp) G(g_dbus_connection_signal_unsubscribe)(s->conn, s->resp);
		s->sub = s->resp = 0;
		if (s->session)
			G(g_dbus_connection_call)(s->conn, PORTAL, s->session, "org.freedesktop.portal.Session", "Close", NULL, NULL, 0, -1, NULL, ignored, NULL);
	}
	unref(s);
}

// ---- One position ----

typedef struct {
	void (*done)(void *ctx, const location_Fix *p, asuka_error *e);
	void *ctx;
	void *source;
	bool starting, finished;
} fixer;

static void fix_moved(void *p, const location_Fix *pos) {
	fixer *f = p;
	if (f->finished) return;
	f->finished = true;
	f->done(f->ctx, pos, NULL);
	if (!f->starting) {
		location_stop(f->source);
		free(f);
	}
}

static void fix_failed(void *p, asuka_error *e) {
	fixer *f = p;
	if (f->finished) {
		asuka_error_free(e);
		return;
	}
	f->finished = true;
	f->done(f->ctx, NULL, e);
	if (!f->starting) {
		location_stop(f->source);
		free(f);
	}
}

void location_fix(void (*done)(void *ctx, const location_Fix *p, asuka_error *e), void *ctx) {
	fixer *f = calloc(1, sizeof *f);
	f->done = done;
	f->ctx = ctx;
	f->starting = true;
	f->source = location_start((location_sink){fix_moved, fix_failed, f});
	f->starting = false;
	if (f->finished) {
		location_stop(f->source);
		free(f);
	}
}

typedef struct {
	void (*done)(void *ctx);
	void *ctx;
} asking;

static void asked(void *p, const location_Fix *pos, asuka_error *e) {
	asking *a = p;
	if (e) asuka_error_free(e);
	a->done(a->ctx);
	free(a);
}

void location_request_access(void (*done)(void *ctx), void *ctx) {
	if (seen.known) {
		done(ctx);
		return;
	}
	asking *a = malloc(sizeof *a);
	*a = (asking){done, ctx};
	location_fix(asked, a);
}

// ---- A place's name: geocode-glib (Nominatim) ----

// geocoder opens geocode-glib (asuka_linux.def's GEOCODE functions):
// false when it isn't installed.
static bool geocoder(void) {
	return asuka_linux() && asuka_linux_load(ASUKA_LIB_GEOCODE) && G(geocode_location_new) && G(geocode_reverse_new_for_location) &&
	       G(geocode_reverse_resolve_async) && G(geocode_reverse_resolve_finish) && G(geocode_place_get_name) && G(geocode_place_get_area) &&
	       G(geocode_place_get_town) && G(geocode_place_get_state) && G(geocode_place_get_country);
}

typedef struct {
	void (*done)(void *ctx, char *name, asuka_error *e);
	void *ctx;
	gpointer location, reverse;
} naming;

// add appends v to the name unless it's there, joined with ", ".
static void add(asuka_writer *w, const char **have, int *n, const char *v) {
	for (int i = 0; i < *n; i++)
		if (!strcmp(have[i], v)) return;
	have[(*n)++] = v;
	if (w->len) asuka_writer_put(w, ", ", 2);
	asuka_writer_put(w, v, strlen(v));
}

static void resolved(GObject *o, GAsyncResult *res, gpointer data) {
	naming *n = data;
	GError *ge = NULL;
	gpointer place = G(geocode_reverse_resolve_finish)(n->reverse, res, &ge);
	G(g_object_unref)(n->reverse);
	G(g_object_unref)(n->location);
	if (!place) {
		asuka_error e = {0};
		if (ge) asuka_linux_fail(&e, ge, "location");
		else asuka_error_failed(&e, "gio", 0, "location: no name for this place");
		n->done(n->ctx, NULL, &e);
		free(n);
		return;
	}
	// The area (else the name), then the town (else the state, else the
	// country): "Kreuzberg, Berlin".
	const char *groups[2][3] = {{G(geocode_place_get_area)(place), G(geocode_place_get_name)(place), NULL}, {G(geocode_place_get_town)(place), G(geocode_place_get_state)(place), G(geocode_place_get_country)(place)}};
	asuka_writer w = {0};
	const char *have[2];
	int nhave = 0;
	for (int g = 0; g < 2; g++)
		for (int i = 0; i < 3; i++)
			if (groups[g][i] && *groups[g][i]) {
				add(&w, have, &nhave, groups[g][i]);
				break;
			}
	asuka_writer_u8(&w, 0);
	char *name = asuka_strdup((char *)w.data);
	asuka_writer_free(&w);
	G(g_object_unref)(place);
	n->done(n->ctx, name, NULL);
	free(n);
}

void location_geocode(double latitude, double longitude, void (*done)(void *ctx, char *name, asuka_error *e), void *ctx) {
	if (!geocoder()) {
		asuka_error e = {0};
		asuka_error_failed(&e, "gio", 0, "location: no geocoder (geocode-glib isn't installed)");
		done(ctx, NULL, &e);
		return;
	}
	naming *n = calloc(1, sizeof *n);
	n->done = done;
	n->ctx = ctx;
	n->location = G(geocode_location_new)(latitude, longitude, -1);  // GEOCODE_LOCATION_ACCURACY_UNKNOWN
	n->reverse = G(geocode_reverse_new_for_location)(n->location);
	G(geocode_reverse_resolve_async)(n->reverse, NULL, resolved, n);
}

// ---- The rest ----

static void reap(void *p) {
	pid_t pid = (pid_t)(intptr_t)p;
	waitpid(pid, NULL, 0);
}

void location_open_settings(asuka_error *err) {
	char *argv[] = {"gnome-control-center", "location", NULL};
	pid_t pid;
	if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) != 0) {
		asuka_error_unsupported(err, "no GNOME Settings");
		return;
	}
	asuka_desktop_spawn(reap, (void *)(intptr_t)pid);
}

location_Support location_support(void) { return (location_Support){.dwell = true}; }

void location_add_fence(const location_Region *r, asuka_error *err) { asuka_error_declared(err, location_LocationError_unavailable); }
void location_remove_fence(const char *id) {}
location_list_Region location_fences(void) { return (location_list_Region){0}; }
void location_watch_fences(void (*crossed)(const char *, location_Transition, const location_Fix *)) {}
