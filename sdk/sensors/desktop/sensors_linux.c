// platform/sensors on Linux: iio-sensor-proxy (net.hadess.SensorProxy, on
// the system bus), which convertible laptops and tablets run. It gives
// the ambient light (in lux, when the sensor says so) and the
// accelerometer's orientation, not its readings: the accelerometer reads
// 1 g along the axis that's up (portrait upright: y; on its left side:
// -x; flat or unknown: z, face up), each time the orientation changes.
// Gyroscopes, magnetometers, barometers and pedometers have no service
// an app can read. Windows apps that aren't packaged have no sensor API
// to read without WinRT's activation: no C for Windows (Asuka's side
// answers not available). Main thread.
#include "asuka_linux.h"
#include "sensors.asuka.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROXY "net.hadess.SensorProxy"
#define PROXY_PATH "/net/hadess/SensorProxy"
#define PROPS "org.freedesktop.DBus.Properties"

// known are the proxy's properties as last read: asked for as the
// package's object is made (as the app starts), so that available, which
// answers at once, has them; and again as each stream starts.
static GVariant *known;

typedef void (*props_fn)(GVariant *props, void *ctx);

typedef struct {
	props_fn fn;
	void *ctx;
} asking;

static void got_props(GObject *src, GAsyncResult *res, gpointer p) {
	asking *a = p;
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	if (e) G(g_error_free)(e);
	if (known) G(g_variant_unref)(known), known = NULL;
	if (r) {
		known = G(g_variant_get_child_value)(r, 0);
		G(g_variant_unref)(r);
	}
	if (a->fn) a->fn(known, a->ctx);
	free(a);
}

// read_props asks the proxy for its properties: fn gets them (NULL: no
// proxy).
static void read_props(props_fn fn, void *ctx) {
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	if (!c) {
		if (fn) fn(NULL, ctx);
		return;
	}
	asking *a = malloc(sizeof *a);
	*a = (asking){fn, ctx};
	G(g_dbus_connection_call)(c, PROXY, PROXY_PATH, PROPS, "GetAll", G(g_variant_new)("(s)", PROXY), G_VARIANT_TYPE("(a{sv})"), 0, 3000, NULL, got_props, a);
}

static bool flag(GVariant *props, const char *name) {
	GVariant *v = props ? G(g_variant_lookup_value)(props, name, G_VARIANT_TYPE("b")) : NULL;
	bool b = v && G(g_variant_get_boolean)(v);
	if (v) G(g_variant_unref)(v);
	return b;
}

static char *str(GVariant *props, const char *name) {
	GVariant *v = props ? G(g_variant_lookup_value)(props, name, G_VARIANT_TYPE("s")) : NULL;
	char *s = asuka_strdup(v ? G(g_variant_get_string)(v, NULL) : "");
	if (v) G(g_variant_unref)(v);
	return s;
}

static double number(GVariant *props, const char *name) {
	GVariant *v = props ? G(g_variant_lookup_value)(props, name, G_VARIANT_TYPE("d")) : NULL;
	double d = v ? G(g_variant_get_double)(v) : 0;
	if (v) G(g_variant_unref)(v);
	return d;
}

static bool has_in(GVariant *props, sensors_Sensor s) {
	if (s == sensors_Sensor_accelerometer) return flag(props, "HasAccelerometer");
	if (s == sensors_Sensor_light) {
		char *unit = str(props, "LightLevelUnit");
		bool ok = flag(props, "HasAmbientLight") && strcmp(unit, "lux") == 0;
		free(unit);
		return ok;
	}
	return false;
}

static bool s_available(sensors_Sensors *self, sensors_Sensor s, asuka_error *err) { return has_in(known, s); }

// since_boot is the time since the system booted (CLOCK_BOOTTIME, as
// Android's sensor timestamps; suspended time counts).
static int64_t since_boot(void) {
	struct timespec ts;
	clock_gettime(7, &ts);  // CLOCK_BOOTTIME
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

typedef struct {
	sensors_Sensor sensor;
	int64_t every, last;
	sensors_fn_Sample reading;
	asuka_call *call;
	guint sub;
	char *orientation;
	double light;
	bool started, done;
} watching;

static void send(watching *w) {
	int64_t at = since_boot();
	if (w->last >= 0 && at - w->last < w->every * 9 / 10) return;
	w->last = at;
	double v[3] = {0, 0, 1};  // flat (or not known): face up
	size_t n = 3;
	if (w->sensor == sensors_Sensor_light) {
		v[0] = w->light;
		n = 1;
	} else if (strcmp(w->orientation, "normal") == 0) {
		v[0] = 0, v[1] = 1, v[2] = 0;
	} else if (strcmp(w->orientation, "bottom-up") == 0) {
		v[0] = 0, v[1] = -1, v[2] = 0;
	} else if (strcmp(w->orientation, "left-up") == 0) {
		v[0] = -1, v[1] = 0, v[2] = 0;
	} else if (strcmp(w->orientation, "right-up") == 0) {
		v[0] = 1, v[1] = 0, v[2] = 0;
	}
	sensors_Sample s = {{v, n}, at};
	sensors_fn_Sample_call(w->reading, &s);
}

static void on_changed(GDBusConnection *c, const gchar *s, const gchar *path, const gchar *i, const gchar *n, GVariant *params, gpointer p) {
	watching *w = p;
	if (G(g_variant_n_children)(params) < 2) return;
	GVariant *changed = G(g_variant_get_child_value)(params, 1);
	if (w->sensor == sensors_Sensor_accelerometer) {
		GVariant *o = G(g_variant_lookup_value)(changed, "AccelerometerOrientation", G_VARIANT_TYPE("s"));
		if (o && strcmp(G(g_variant_get_string)(o, NULL), w->orientation) != 0) {
			free(w->orientation);
			w->orientation = asuka_strdup(G(g_variant_get_string)(o, NULL));
			send(w);
		}
		if (o) G(g_variant_unref)(o);
	} else {
		GVariant *l = G(g_variant_lookup_value)(changed, "LightLevel", G_VARIANT_TYPE("d"));
		if (l && G(g_variant_get_double)(l) != w->light) {
			w->light = G(g_variant_get_double)(l);
			send(w);
		}
		if (l) G(g_variant_unref)(l);
	}
	G(g_variant_unref)(changed);
}

static void ignore(GObject *src, GAsyncResult *res, gpointer p) {
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	if (r) G(g_variant_unref)(r);
	if (e) G(g_error_free)(e);
}

// follow claims the sensor from the proxy, sends its reading now and
// each change.
static void follow(GVariant *props, void *p) {
	watching *w = p;
	if (w->done) {
		free(w->orientation);
		free(w);
		return;
	}
	asuka_error e = {0};
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	if (!has_in(props, w->sensor) || !c) {
		asuka_error_declared(&e, sensors_SensorError_notAvailable);
		asuka_call_fail(w->call, &e);  // answered: stop won't come
		free(w);
		return;
	}
	w->started = true;
	G(g_dbus_connection_call)(c, PROXY, PROXY_PATH, PROXY, w->sensor == sensors_Sensor_light ? "ClaimLight" : "ClaimAccelerometer", NULL, NULL, 0, 3000, NULL, ignore, NULL);
	w->orientation = str(props, "AccelerometerOrientation");
	w->light = number(props, "LightLevel");
	w->sub = G(g_dbus_connection_signal_subscribe)(c, PROXY, PROPS, "PropertiesChanged", PROXY_PATH, NULL, 0, on_changed, w, NULL);
	send(w);  // the reading now: it changes only as the device turns
}

static void stop(void *p) {
	watching *w = p;
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, NULL);
	if (w->started && c) {
		G(g_dbus_connection_signal_unsubscribe)(c, w->sub);
		G(g_dbus_connection_call)(c, PROXY, PROXY_PATH, PROXY, w->sensor == sensors_Sensor_light ? "ReleaseLight" : "ReleaseAccelerometer", NULL, NULL, 0, 3000, NULL, ignore, NULL);
	}
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(w->call, &e);
	if (w->started) {
		free(w->orientation);
		free(w);
	} else {
		w->done = true;  // follow frees it as the properties come
	}
}

static void s_watch(sensors_Sensors *self, sensors_Sensor sensor, int64_t every, sensors_fn_Sample reading, asuka_call *call) {
	watching *w = calloc(1, sizeof *w);
	w->sensor = sensor;
	w->every = every;
	w->last = -1;
	w->reading = reading;
	w->call = call;
	read_props(follow, w);
	asuka_call_on_cancel(call, stop, w);
}

static void s_steps(sensors_Sensors *self, int64_t from, int64_t to, asuka_call *call) {
	asuka_error e = {0};
	asuka_error_declared(&e, sensors_SensorError_notAvailable);
	asuka_call_fail(call, &e);
}

static void s_simulate(sensors_Sensors *self, const char *json, asuka_call *call) {
	asuka_error e = {0};
	asuka_error_unsupported(&e, "only on the iOS simulator");
	asuka_call_fail(call, &e);
}

static const sensors_Sensors_vtable vt = {.available = s_available, .watch = s_watch, .steps = s_steps, .simulate = s_simulate};

sensors_Sensors *sensors_make_Sensors(asuka_error *err) {
	if (!asuka_linux()) {
		asuka_error_unsupported(err, "no GLib");
		return NULL;
	}
	read_props(NULL, NULL);
	sensors_Sensors *s = malloc(sizeof *s);
	s->vt = &vt;
	return s;
}
