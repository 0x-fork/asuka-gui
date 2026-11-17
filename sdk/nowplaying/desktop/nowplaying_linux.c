// platform/nowplaying on Linux: an MPRIS player (org.mpris.MediaPlayer2 and
// its .Player) on the session bus, which GNOME Shell's and KDE Plasma's
// media controls, their lock screens, the desktops' media keys
// (gsd-media-keys, KDE's), and playerctl show and drive. The player is
// exported on a session bus connection of its own, with the bus name
// org.mpris.MediaPlayer2.<app>.instance<pid>; clear closes it, so the
// name goes and the desktops drop the player at once. Clients advance the
// position at the rate themselves (they read Position, which this
// extrapolates, when they need it) and hear of jumps through Seeked.
#include "asuka_linux.h"
#include "nowplaying.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PATH "/org/mpris/MediaPlayer2"
#define ROOT "org.mpris.MediaPlayer2"
#define PLAYER "org.mpris.MediaPlayer2.Player"

static const char xml[] =
    "<node>"
    "<interface name='org.mpris.MediaPlayer2'>"
    "<method name='Raise'/><method name='Quit'/>"
    "<property name='CanQuit' type='b' access='read'/><property name='CanRaise' type='b' access='read'/>"
    "<property name='HasTrackList' type='b' access='read'/><property name='Identity' type='s' access='read'/>"
    "<property name='DesktopEntry' type='s' access='read'/><property name='SupportedUriSchemes' type='as' access='read'/>"
    "<property name='SupportedMimeTypes' type='as' access='read'/>"
    "</interface>"
    "<interface name='org.mpris.MediaPlayer2.Player'>"
    "<method name='Next'/><method name='Previous'/><method name='Pause'/><method name='PlayPause'/><method name='Stop'/><method name='Play'/>"
    "<method name='Seek'><arg direction='in' name='Offset' type='x'/></method>"
    "<method name='SetPosition'><arg direction='in' name='TrackId' type='o'/><arg direction='in' name='Position' type='x'/></method>"
    "<method name='OpenUri'><arg direction='in' name='Uri' type='s'/></method>"
    "<signal name='Seeked'><arg name='Position' type='x'/></signal>"
    "<property name='PlaybackStatus' type='s' access='read'/><property name='Rate' type='d' access='read'/>"
    "<property name='Metadata' type='a{sv}' access='read'/><property name='Volume' type='d' access='read'/>"
    "<property name='Position' type='x' access='read'/><property name='MinimumRate' type='d' access='read'/>"
    "<property name='MaximumRate' type='d' access='read'/><property name='CanGoNext' type='b' access='read'/>"
    "<property name='CanGoPrevious' type='b' access='read'/><property name='CanPlay' type='b' access='read'/>"
    "<property name='CanPause' type='b' access='read'/><property name='CanSeek' type='b' access='read'/>"
    "<property name='CanControl' type='b' access='read'/>"
    "</interface>"
    "</node>";

// The player, main thread only.
static struct {
	GDBusConnection *conn;  // its own session bus connection; NULL while nothing shows
	guint regs[2];
	gpointer node;
	nowplaying_Info *info;
	double at;  // when info came, seconds
	int track;  // counts the tracks, for their ids
	char *art_url;
} p;

static double now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// position is where playback is now, in seconds.
static double position(void) {
	nowplaying_Info *i = p.info;
	if (!i) return 0;
	double pos = i->position;
	double t = now() - p.at;
	if (i->state == nowplaying_State_playing && t > 0) pos += t * i->rate;
	if (i->duration > 0 && pos > i->duration) pos = i->duration;
	return pos > 0 ? pos : 0;
}

static gint64 micros(double s) { return (gint64)llround(s * 1e6); }

static char *track_id(void) { return asuka_sprintf("/dev/asuka/track/%d", p.track); }

// element makes s an element of a bus name: ASCII letters, digits, _ and
// -, not starting with a digit.
static char *element(const char *s) {
	asuka_writer w = {0};
	for (const char *q = s; *q; q++) {
		char c = *q;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-') asuka_writer_u8(&w, (uint8_t)c);
		else if (c >= '0' && c <= '9') {
			if (!w.len) asuka_writer_u8(&w, '_');
			asuka_writer_u8(&w, (uint8_t)c);
		} else asuka_writer_u8(&w, '_');
	}
	if (!w.len) asuka_writer_put(&w, "asuka", 5);
	asuka_writer_u8(&w, 0);
	return (char *)w.data;
}

static GVariant *strings(const char *one) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("as"));
	if (one) G(g_variant_builder_add)(&b, "s", one);
	return G(g_variant_builder_end)(&b);
}

static GVariant *metadata(void) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	char *id = track_id();
	G(g_variant_builder_add)(&b, "{sv}", "mpris:trackid", G(g_variant_new_object_path)(id));
	free(id);
	nowplaying_Info *i = p.info;
	if (i) {
		if (*i->title) G(g_variant_builder_add)(&b, "{sv}", "xesam:title", G(g_variant_new_string)(i->title));
		if (*i->artist) G(g_variant_builder_add)(&b, "{sv}", "xesam:artist", strings(i->artist));
		if (*i->album) G(g_variant_builder_add)(&b, "{sv}", "xesam:album", G(g_variant_new_string)(i->album));
		if (i->duration > 0) G(g_variant_builder_add)(&b, "{sv}", "mpris:length", G(g_variant_new_int64)(micros(i->duration)));
		if (p.art_url) G(g_variant_builder_add)(&b, "{sv}", "mpris:artUrl", G(g_variant_new_string)(p.art_url));
	}
	return G(g_variant_builder_end)(&b);
}

// property is a property's value, a floating variant (NULL: none).
static GVariant *property(const char *name) {
	nowplaying_Info *i = p.info;
	if (!strcmp(name, "CanQuit") || !strcmp(name, "HasTrackList")) return G(g_variant_new_boolean)(false);
	if (!strcmp(name, "CanRaise") || !strcmp(name, "CanControl")) return G(g_variant_new_boolean)(true);
	if (!strcmp(name, "Identity")) return G(g_variant_new_string)(asuka_desktop_app_name());
	if (!strcmp(name, "DesktopEntry")) return G(g_variant_new_string)(asuka_desktop_app_id());
	if (!strcmp(name, "SupportedUriSchemes") || !strcmp(name, "SupportedMimeTypes")) return strings(NULL);
	if (!strcmp(name, "PlaybackStatus"))
		return G(g_variant_new_string)(!i ? "Stopped" : i->state == nowplaying_State_playing ? "Playing" : i->state == nowplaying_State_paused ? "Paused" : "Stopped");
	if (!strcmp(name, "Rate")) return G(g_variant_new_double)(i && i->rate > 0 ? i->rate : 1);
	if (!strcmp(name, "Volume")) return G(g_variant_new_double)(1);
	if (!strcmp(name, "MinimumRate")) return G(g_variant_new_double)(0.25);
	if (!strcmp(name, "MaximumRate")) return G(g_variant_new_double)(4);
	if (!strcmp(name, "Position")) return G(g_variant_new_int64)(micros(position()));
	if (!strcmp(name, "Metadata")) return metadata();
	if (!strcmp(name, "CanGoNext")) return G(g_variant_new_boolean)(nowplaying_has(i, nowplaying_Command_next));
	if (!strcmp(name, "CanGoPrevious")) return G(g_variant_new_boolean)(nowplaying_has(i, nowplaying_Command_previous));
	if (!strcmp(name, "CanPlay")) return G(g_variant_new_boolean)(nowplaying_has(i, nowplaying_Command_play));
	if (!strcmp(name, "CanPause")) return G(g_variant_new_boolean)(nowplaying_has(i, nowplaying_Command_pause));
	if (!strcmp(name, "CanSeek"))
		return G(g_variant_new_boolean)(nowplaying_has(i, nowplaying_Command_seek) || nowplaying_has(i, nowplaying_Command_skipForward) ||
		                                nowplaying_has(i, nowplaying_Command_skipBackward));
	return NULL;
}

static GVariant *get_property(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *prop, GError **e,
                              gpointer d) {
	return property(prop);
}

// changed emits PropertiesChanged with the properties' values.
static void changed(const char *iface, const char *const *props) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	for (const char *const *q = props; *q; q++) G(g_variant_builder_add)(&b, "{sv}", *q, property(*q));
	G(g_dbus_connection_emit_signal)(p.conn, NULL, PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged",
	                                 G(g_variant_new)("(s@a{sv}@as)", iface, G(g_variant_builder_end)(&b), strings(NULL)), NULL);
}

// method answers a client's call: the player's commands go to the
// watchers.
static void method_call(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params,
                        GDBusMethodInvocation *inv, gpointer d) {
	if (!strcmp(method, "OpenUri")) {
		G(g_dbus_method_invocation_return_dbus_error)(inv, "org.freedesktop.DBus.Error.NotSupported", "the player opens no URIs");
		return;
	}
	G(g_dbus_method_invocation_return_value)(inv, NULL);
	nowplaying_Info *i = p.info;
	if (!strcmp(method, "Raise")) {
		GtkWindow *w = asuka_desktop_window(0);
		if (w && G(gtk_window_present)) G(gtk_window_present)(w);
	} else if (!strcmp(method, "Play")) nowplaying_send(i, nowplaying_Command_play, 0);
	else if (!strcmp(method, "Pause")) nowplaying_send(i, nowplaying_Command_pause, 0);
	else if (!strcmp(method, "PlayPause")) nowplaying_send(i, nowplaying_Command_togglePlayPause, 0);
	else if (!strcmp(method, "Stop")) nowplaying_send(i, nowplaying_Command_stop, 0);
	else if (!strcmp(method, "Next")) nowplaying_send(i, nowplaying_Command_next, 0);
	else if (!strcmp(method, "Previous")) nowplaying_send(i, nowplaying_Command_previous, 0);
	else if (!strcmp(method, "Seek")) {
		// By an offset, in µs: a skip where the app skips, else a seek.
		gint64 off = 0;
		G(g_variant_get)(params, "(x)", &off);
		double s = off / 1e6;
		if (s > 0 && nowplaying_has(i, nowplaying_Command_skipForward)) nowplaying_send(i, nowplaying_Command_skipForward, s);
		else if (s < 0 && nowplaying_has(i, nowplaying_Command_skipBackward)) nowplaying_send(i, nowplaying_Command_skipBackward, -s);
		else if (s != 0 && nowplaying_has(i, nowplaying_Command_seek)) {
			double to = position() + s;
			if (to < 0) to = 0;
			if (i->duration > 0 && to > i->duration) nowplaying_send(i, nowplaying_Command_next, 0);  // past the end: the next track
			else nowplaying_send(i, nowplaying_Command_seek, to);
		}
	} else if (!strcmp(method, "SetPosition")) {
		// Ignored for another track, and out of the track.
		const gchar *id = NULL;
		gint64 us = 0;
		G(g_variant_get)(params, "(&ox)", &id, &us);
		double s = us / 1e6;
		char *mine = track_id();
		if (i && id && !strcmp(id, mine) && s >= 0 && (i->duration == 0 || s <= i->duration)) nowplaying_send(i, nowplaying_Command_seek, s);
		free(mine);
	}
}

static const GDBusInterfaceVTable vtable = {method_call, get_property, NULL, {0}};

static void ignore(GObject *src, GAsyncResult *res, gpointer d) {
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, NULL);
	if (r) G(g_variant_unref)(r);
}

// open exports the player on a connection of its own and owns its name
// there.
static bool open_player(asuka_error *err) {
	GError *e = NULL;
	gchar *addr = G(g_dbus_address_get_for_bus_sync)(G_BUS_TYPE_SESSION, NULL, &e);
	if (!addr) {
		asuka_linux_fail(err, e, "nowplaying: the session bus");
		return false;
	}
	// G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | MESSAGE_BUS_CONNECTION
	GDBusConnection *conn = G(g_dbus_connection_new_for_address_sync)(addr, 1 | 8, NULL, NULL, &e);
	G(g_free)(addr);
	if (!conn) {
		asuka_linux_fail(err, e, "nowplaying: the session bus");
		return false;
	}
	if (!p.node) p.node = G(g_dbus_node_info_new_for_xml)(xml, NULL);
	const char *ifaces[2] = {ROOT, PLAYER};
	for (int k = 0; k < 2; k++) {
		p.regs[k] = G(g_dbus_connection_register_object)(conn, PATH, G(g_dbus_node_info_lookup_interface)(p.node, ifaces[k]), &vtable, NULL, NULL, &e);
		if (!p.regs[k]) {
			if (k == 1) G(g_dbus_connection_unregister_object)(conn, p.regs[0]);
			G(g_dbus_connection_close_sync)(conn, NULL, NULL);
			G(g_object_unref)(conn);
			asuka_linux_fail(err, e, "nowplaying: MPRIS");
			return false;
		}
	}
	p.conn = conn;
	char *el = element(asuka_desktop_app_name());
	char *name = asuka_sprintf("%s.%s.instance%d", ROOT, el, (int)getpid());
	free(el);
	// DBUS_NAME_FLAG_DO_NOT_QUEUE: the name is the connection's until it
	// closes.
	G(g_dbus_connection_call)(conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
	                          G(g_variant_new)("(su)", name, 4u), NULL, 0, 5000, NULL, ignore, NULL);
	free(name);
	return true;
}

// file_uri is an artwork's source as a URI: a URL, a file, an asset.
static char *file_uri(const char *source) {
	if (strstr(source, "://")) return asuka_strdup(source);
	char *path = source[0] == '/' ? asuka_strdup(source) : asuka_sprintf("%s/%s", asuka_desktop_assets_dir(), source);
	char abs[PATH_MAX];
	GFile *f = G(g_file_new_for_path)(realpath(path, abs) ? abs : path);
	gchar *u = G(g_file_get_uri)(f);
	G(g_object_unref)(f);
	char *out = asuka_strdup(u);
	G(g_free)(u);
	free(path);
	return out;
}

// set shows info: the player is exported (and its name owned) when
// something first shows; after, what changed is announced.
static void c_set(nowplaying_Center *self, const nowplaying_Info *info, asuka_error *err) {
	nowplaying_Info *old = p.info;
	double expected = position();
	if (!old || strcmp(old->title, info->title) || strcmp(old->artist, info->artist) || strcmp(old->album, info->album) || old->duration != info->duration)
		p.track++;
	bool had = old != NULL;
	p.info = nowplaying_copy(info);
	p.at = now();
	nowplaying_free_info(old);
	free(p.art_url);
	p.art_url = *info->artwork ? file_uri(info->artwork) : NULL;
	if (!p.conn) {
		if (!open_player(err)) {
			nowplaying_free_info(p.info);
			p.info = NULL;
		}
		return;
	}
	static const char *const props[] = {"PlaybackStatus", "Rate", "Metadata", "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause", "CanSeek", NULL};
	changed(PLAYER, props);
	// Clients advance the position at the rate, and hear only of jumps.
	if (had && fabs(info->position - expected) > 0.25)
		G(g_dbus_connection_emit_signal)(p.conn, NULL, PATH, PLAYER, "Seeked", G(g_variant_new)("(x)", micros(info->position)), NULL);
}

// clear takes the player off the bus: its connection closes, and its name
// with it. What it has queued goes first: closing doesn't send it, and a
// command's reply is queued as the command goes to the app, which may
// stop showing (or quit) on it.
static void c_clear(nowplaying_Center *self, asuka_error *err) {
	nowplaying_free_info(p.info);
	p.info = NULL;
	if (!p.conn) return;
	G(g_dbus_connection_unregister_object)(p.conn, p.regs[0]);
	G(g_dbus_connection_unregister_object)(p.conn, p.regs[1]);
	G(g_dbus_connection_flush_sync)(p.conn, NULL, NULL);
	G(g_dbus_connection_close_sync)(p.conn, NULL, NULL);
	G(g_object_unref)(p.conn);
	p.conn = NULL;
}

static void c_release(nowplaying_Center *self) {}  // the one center lives with the app

static const nowplaying_Center_vtable vt = {.release = c_release, .set = c_set, .clear = c_clear, .watch = nowplaying_watch};

nowplaying_Center *nowplaying_make_Center(asuka_error *err) {
	static nowplaying_Center center = {&vt};
	if (!asuka_linux()) {
		asuka_error_unsupported(err, "no GLib");
		return NULL;
	}
	return &center;
}
