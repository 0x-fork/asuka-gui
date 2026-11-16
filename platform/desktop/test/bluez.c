// bluez.c: a BlueZ of the test's own, for asuka/bluetooth's Linux C
// (platform/desktop's TestBluetoothLinux): org.bluez on the session bus,
// with an adapter and a heart rate monitor to scan, connect to and read,
// and the calls BlueZ makes of an app's advertisement and GATT
// application. It plays its part as the app's calls come, and says what
// it saw ("bluez: ..."); the test reads both.
#include "asuka_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _GDBusNodeInfo GDBusNodeInfo;
typedef struct _GDBusInterfaceInfo GDBusInterfaceInfo;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct {
	void (*method_call)(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *, GDBusMethodInvocation *, gpointer);
	gpointer get_property, set_property;
	gpointer padding[8];
} vtable;

static struct {
	GDBusNodeInfo *(*node_info_new_for_xml)(const gchar *, GError **);
	GDBusInterfaceInfo *(*lookup)(GDBusNodeInfo *, const gchar *);
	guint (*register_object)(GDBusConnection *, const gchar *, GDBusInterfaceInfo *, const vtable *, gpointer, GDestroyNotify, GError **);
	gboolean (*emit)(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *, GError **);
	void (*ret)(GDBusMethodInvocation *, GVariant *);
	const gchar *(*sender)(GDBusMethodInvocation *);
	gpointer (*ref)(gpointer);
} d;

#define ADAPTER "/org/bluez/hci0"
#define DEVICE ADAPTER "/dev_AA_BB_CC_DD_EE_FF"
#define SERVICE DEVICE "/service000a"
#define CHAR SERVICE "/char000b"
#define DESC CHAR "/desc000d"
#define HR_SERVICE "0000180d-0000-1000-8000-00805f9b34fb"
#define HR_MEASUREMENT "00002a37-0000-1000-8000-00805f9b34fb"
#define BODY_SENSOR "00002a38-0000-1000-8000-00805f9b34fb"
#define CCCD "00002902-0000-1000-8000-00805f9b34fb"

static const char *xml =
    "<node>"
    " <interface name='org.freedesktop.DBus.ObjectManager'>"
    "  <method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method>"
    " </interface>"
    " <interface name='org.bluez.Adapter1'>"
    "  <method name='SetDiscoveryFilter'><arg type='a{sv}' direction='in'/></method>"
    "  <method name='StartDiscovery'/><method name='StopDiscovery'/>"
    " </interface>"
    " <interface name='org.bluez.LEAdvertisingManager1'>"
    "  <method name='RegisterAdvertisement'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>"
    "  <method name='UnregisterAdvertisement'><arg type='o' direction='in'/></method>"
    " </interface>"
    " <interface name='org.bluez.GattManager1'>"
    "  <method name='RegisterApplication'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>"
    "  <method name='UnregisterApplication'><arg type='o' direction='in'/></method>"
    " </interface>"
    " <interface name='org.bluez.Device1'><method name='Connect'/><method name='Disconnect'/></interface>"
    " <interface name='org.bluez.GattCharacteristic1'>"
    "  <method name='ReadValue'><arg type='a{sv}' direction='in'/><arg type='ay' direction='out'/></method>"
    "  <method name='WriteValue'><arg type='ay' direction='in'/><arg type='a{sv}' direction='in'/></method>"
    "  <method name='StartNotify'/><method name='StopNotify'/>"
    " </interface>"
    "</node>";

static GDBusConnection *bus;
static bool powered = true, discovering, connected, resolved, heard;
static char *app;  // the app's unique name on the bus
static char *advert, *char7, *char8;

static void say(const char *format, const char *s) {
	printf(format, s);
	printf("\n");
	fflush(stdout);
}

static GVariant *bytes(const guchar *p, gsize n) { return G(g_variant_new_fixed_array)(G_VARIANT_TYPE("y"), p, n, 1); }

static char *hex(GVariant *ay) {
	gsize n = 0;
	const guchar *p = G(g_variant_get_fixed_array)(ay, &n, 1);
	static char buf[256];
	buf[0] = 0;
	for (gsize i = 0; i < n && i < 80; i++) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%s%02x", i ? " " : "", p[i]);
	return buf;
}

static GVariant *props(const char *path, const char *iface) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	if (!strcmp(iface, "org.bluez.Adapter1")) {
		G(g_variant_builder_add)(&b, "{sv}", "Address", G(g_variant_new_string)("00:11:22:33:44:55"));
		G(g_variant_builder_add)(&b, "{sv}", "Powered", G(g_variant_new_boolean)(powered));
		G(g_variant_builder_add)(&b, "{sv}", "Discovering", G(g_variant_new_boolean)(discovering));
	} else if (!strcmp(iface, "org.bluez.Device1")) {
		const char *uuids[] = {HR_SERVICE};
		GVariantBuilder u;
		G(g_variant_builder_init)(&u, G_VARIANT_TYPE("as"));
		G(g_variant_builder_add)(&u, "s", uuids[0]);
		G(g_variant_builder_add)(&b, "{sv}", "Address", G(g_variant_new_string)("AA:BB:CC:DD:EE:FF"));
		G(g_variant_builder_add)(&b, "{sv}", "Adapter", G(g_variant_new_object_path)(ADAPTER));
		G(g_variant_builder_add)(&b, "{sv}", "Name", G(g_variant_new_string)("Polar H10"));
		G(g_variant_builder_add)(&b, "{sv}", "Alias", G(g_variant_new_string)("Polar H10"));
		G(g_variant_builder_add)(&b, "{sv}", "Connected", G(g_variant_new_boolean)(connected));
		G(g_variant_builder_add)(&b, "{sv}", "ServicesResolved", G(g_variant_new_boolean)(resolved));
		G(g_variant_builder_add)(&b, "{sv}", "UUIDs", G(g_variant_builder_end)(&u));
	} else if (!strcmp(iface, "org.bluez.GattService1")) {
		G(g_variant_builder_add)(&b, "{sv}", "UUID", G(g_variant_new_string)(HR_SERVICE));
		G(g_variant_builder_add)(&b, "{sv}", "Primary", G(g_variant_new_boolean)(true));
		G(g_variant_builder_add)(&b, "{sv}", "Device", G(g_variant_new_object_path)(DEVICE));
	} else if (!strcmp(iface, "org.bluez.GattCharacteristic1")) {
		GVariantBuilder f;
		G(g_variant_builder_init)(&f, G_VARIANT_TYPE("as"));
		G(g_variant_builder_add)(&f, "s", "read");
		G(g_variant_builder_add)(&f, "s", "write");
		G(g_variant_builder_add)(&f, "s", "notify");
		G(g_variant_builder_add)(&b, "{sv}", "UUID", G(g_variant_new_string)(HR_MEASUREMENT));
		G(g_variant_builder_add)(&b, "{sv}", "Service", G(g_variant_new_object_path)(SERVICE));
		G(g_variant_builder_add)(&b, "{sv}", "Flags", G(g_variant_builder_end)(&f));
		G(g_variant_builder_add)(&b, "{sv}", "MTU", G(g_variant_new)("q", (guint16)185));
	} else if (!strcmp(iface, "org.bluez.GattDescriptor1")) {
		G(g_variant_builder_add)(&b, "{sv}", "UUID", G(g_variant_new_string)(CCCD));
		G(g_variant_builder_add)(&b, "{sv}", "Characteristic", G(g_variant_new_object_path)(CHAR));
	}
	return G(g_variant_builder_end)(&b);
}

static GVariant *ifaces(const char *path, const char *const *names, size_t n) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sa{sv}}"));
	for (size_t i = 0; i < n; i++) G(g_variant_builder_add)(&b, "{s@a{sv}}", names[i], props(path, names[i]));
	return G(g_variant_builder_end)(&b);
}

static const char *adapter_ifs[] = {"org.bluez.Adapter1", "org.bluez.LEAdvertisingManager1", "org.bluez.GattManager1"};
static const char *device_ifs[] = {"org.bluez.Device1"};
static const char *service_ifs[] = {"org.bluez.GattService1"};
static const char *char_ifs[] = {"org.bluez.GattCharacteristic1"};
static const char *desc_ifs[] = {"org.bluez.GattDescriptor1"};

static GVariant *managed(void) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
	G(g_variant_builder_add)(&b, "{o@a{sa{sv}}}", ADAPTER, ifaces(ADAPTER, adapter_ifs, 3));
	G(g_variant_builder_add)(&b, "{o@a{sa{sv}}}", DEVICE, ifaces(DEVICE, device_ifs, 1));
	return G(g_variant_new)("(@a{oa{sa{sv}}})", G(g_variant_builder_end)(&b));
}

static void emit(const char *path, const char *iface, const char *signal, GVariant *params) {
	d.emit(bus, NULL, path, iface, signal, params, NULL);
}

// changed tells a property's new value, as BlueZ does.
static void changed(const char *path, const char *iface, const char *name, GVariant *v) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	G(g_variant_builder_add)(&b, "{sv}", name, v);
	GVariantBuilder none;
	G(g_variant_builder_init)(&none, G_VARIANT_TYPE("as"));
	emit(path, "org.freedesktop.DBus.Properties", "PropertiesChanged", G(g_variant_new)("(s@a{sv}@as)", iface, G(g_variant_builder_end)(&b), G(g_variant_builder_end)(&none)));
}

static void add(const char *path, const char *const *names) {
	emit("/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", G(g_variant_new)("(o@a{sa{sv}})", path, ifaces(path, names, 1)));
}

// ---- The central's part: what the app does to the heart rate monitor ----

static gboolean hear(gpointer p) {
	if (!heard) changed(DEVICE, "org.bluez.Device1", "RSSI", G(g_variant_new)("n", (gint16)-60));
	heard = true;
	return false;
}

static gboolean notify_value(gpointer p) {
	static const guchar v[] = {9, 8};
	changed(CHAR, "org.bluez.GattCharacteristic1", "Value", bytes(v, 2));
	return false;
}

static gboolean power_off(gpointer p) {
	powered = false;
	changed(ADAPTER, "org.bluez.Adapter1", "Powered", G(g_variant_new_boolean)(false));
	return false;
}

static gboolean drop(gpointer p) {
	emit(DEVICE, "org.bluez.Device1", "Disconnected", G(g_variant_new)("(ss)", "org.bluez.Reason.Remote", "Connection terminated by remote user"));
	connected = resolved = false;
	changed(DEVICE, "org.bluez.Device1", "Connected", G(g_variant_new_boolean)(false));
	G(g_timeout_add_full)(G_PRIORITY_DEFAULT, 200, power_off, NULL, NULL);
	return false;
}

// ---- The peripheral's part: a central reading what the app serves ----

typedef void (*reply_fn)(GVariant *r, GError *e, gpointer data);

typedef struct {
	reply_fn fn;
	gpointer data;
} pending;

static void replied(GObject *src, GAsyncResult *res, gpointer p) {
	pending *x = p;
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	x->fn(r, e, x->data);
	if (r) G(g_variant_unref)(r);
	if (e) G(g_error_free)(e);
	free(x);
}

static void call_app(const char *path, const char *iface, const char *method, GVariant *params, reply_fn fn, gpointer data) {
	pending *x = malloc(sizeof *x);
	*x = (pending){fn, data};
	G(g_dbus_connection_call)(bus, app, path, iface, method, params, NULL, 0, 5000, NULL, replied, x);
}

static void answered(GDBusMethodInvocation *inv, GVariant *r, GError *e) {
	if (e) say("bluez: the app failed: %s", e->message);
	d.ret(inv, NULL);
}

static void advert_read(GVariant *r, GError *e, gpointer inv) {
	if (r) {
		GVariant *all = G(g_variant_get_child_value)(r, 0);
		GVariant *name = G(g_variant_lookup_value)(all, "LocalName", NULL), *uuids = G(g_variant_lookup_value)(all, "ServiceUUIDs", NULL);
		GVariant *disc = G(g_variant_lookup_value)(all, "Discoverable", NULL), *type = G(g_variant_lookup_value)(all, "Type", NULL);
		GVariant *m = G(g_variant_lookup_value)(all, "ManufacturerData", NULL);
		char line[512];
		snprintf(line, sizeof line, "%s type=%s discoverable=%d uuids=%zu", name ? G(g_variant_get_string)(name, NULL) : "-",
		         type ? G(g_variant_get_string)(type, NULL) : "-", disc ? G(g_variant_get_boolean)(disc) : -1, uuids ? G(g_variant_n_children)(uuids) : 0);
		if (uuids && G(g_variant_n_children)(uuids)) {
			GVariant *u = G(g_variant_get_child_value)(uuids, 0);
			snprintf(line + strlen(line), sizeof line - strlen(line), " %s", G(g_variant_get_string)(u, NULL));
			G(g_variant_unref)(u);
		}
		if (m && G(g_variant_n_children)(m)) {
			GVariant *e0 = G(g_variant_get_child_value)(m, 0), *k = G(g_variant_get_child_value)(e0, 0), *v = G(g_variant_get_child_value)(e0, 1);
			GVariant *in = G(g_variant_get_variant)(v);
			snprintf(line + strlen(line), sizeof line - strlen(line), " company=%u data=%s", G(g_variant_get_uint16)(k), hex(in));
			G(g_variant_unref)(in), G(g_variant_unref)(v), G(g_variant_unref)(k), G(g_variant_unref)(e0);
		}
		say("bluez: advertisement %s", line);
		GVariant *vs[] = {name, uuids, disc, type, m, all};
		for (size_t i = 0; i < 6; i++)
			if (vs[i]) G(g_variant_unref)(vs[i]);
	}
	answered(inv, r, e);
}

static void released(GVariant *r, GError *e, gpointer p) { say("bluez: %s", e ? "release failed" : "released the advertisement"); }

static void notified(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer p) {
	GVariant *ch = G(g_variant_get_child_value)(params, 1), *v = G(g_variant_lookup_value)(ch, "Value", NULL);
	if (v) {
		say("bluez: notified %s", hex(v));
		G(g_variant_unref)(v);
	}
	G(g_variant_unref)(ch);
}

static void started_notify(GVariant *r, GError *e, gpointer p) { say("bluez: %s", e ? e->message : "subscribed"); }

static void wrote(GVariant *r, GError *e, gpointer p) {
	say("bluez: central wrote: %s", e ? e->message : "ok");
	G(g_dbus_connection_signal_subscribe)(bus, app, "org.freedesktop.DBus.Properties", "PropertiesChanged", char7, NULL, 0, notified, NULL, NULL);
	call_app(char7, "org.bluez.GattCharacteristic1", "StartNotify", NULL, started_notify, NULL);
}

static GVariant *central_options(bool offset) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	G(g_variant_builder_add)(&b, "{sv}", "device", G(g_variant_new_object_path)(DEVICE));
	if (offset) G(g_variant_builder_add)(&b, "{sv}", "offset", G(g_variant_new)("q", (guint16)1));
	G(g_variant_builder_add)(&b, "{sv}", "mtu", G(g_variant_new)("q", (guint16)185));
	return G(g_variant_builder_end)(&b);
}

static void central_read(GVariant *r, GError *e, gpointer p) {
	if (r) {
		GVariant *v = G(g_variant_get_child_value)(r, 0);
		say("bluez: central read %s", hex(v));
		G(g_variant_unref)(v);
	} else {
		say("bluez: central read failed: %s", e->message);
	}
	static const guchar one[] = {1};
	call_app(char8, "org.bluez.GattCharacteristic1", "WriteValue", G(g_variant_new)("(@ay@a{sv})", bytes(one, 1), central_options(false)), wrote, NULL);
}

static void app_read(GVariant *r, GError *e, gpointer inv) {
	if (r) {
		GVariant *objs = G(g_variant_get_child_value)(r, 0);
		gsize n = G(g_variant_n_children)(objs);
		for (gsize i = 0; i < n; i++) {
			GVariant *o = G(g_variant_get_child_value)(objs, i), *p = G(g_variant_get_child_value)(o, 0), *ifs = G(g_variant_get_child_value)(o, 1);
			GVariant *ch = G(g_variant_lookup_value)(ifs, "org.bluez.GattCharacteristic1", NULL);
			if (ch) {
				GVariant *u = G(g_variant_lookup_value)(ch, "UUID", NULL), *f = G(g_variant_lookup_value)(ch, "Flags", NULL);
				const char *uuid = G(g_variant_get_string)(u, NULL);
				char flags[256] = "";
				for (gsize k = 0; f && k < G(g_variant_n_children)(f); k++) {
					GVariant *x = G(g_variant_get_child_value)(f, k);
					snprintf(flags + strlen(flags), sizeof flags - strlen(flags), "%s%s", k ? "," : "", G(g_variant_get_string)(x, NULL));
					G(g_variant_unref)(x);
				}
				char line[400];
				snprintf(line, sizeof line, "%.4s flags=%s", uuid + 4, flags);
				say("bluez: app characteristic %s", line);
				if (!strcmp(uuid, HR_MEASUREMENT)) char7 = G(g_strdup)(G(g_variant_get_string)(p, NULL));
				if (!strcmp(uuid, BODY_SENSOR)) char8 = G(g_strdup)(G(g_variant_get_string)(p, NULL));
				G(g_variant_unref)(u);
				if (f) G(g_variant_unref)(f);
				G(g_variant_unref)(ch);
			}
			G(g_variant_unref)(ifs), G(g_variant_unref)(p), G(g_variant_unref)(o);
		}
		G(g_variant_unref)(objs);
	}
	answered(inv, r, e);
	if (char7 && char8)
		call_app(char7, "org.bluez.GattCharacteristic1", "ReadValue", G(g_variant_new)("(@a{sv})", central_options(true)), central_read, NULL);
}

// ---- BlueZ's methods ----

static void method(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GVariant *params,
                   GDBusMethodInvocation *inv, gpointer p) {
	if (!strcmp(name, "GetManagedObjects")) {
		d.ret(inv, managed());
		return;
	}
	say("bluez: %s", name);
	if (!strcmp(name, "SetDiscoveryFilter")) {
		GVariant *f = G(g_variant_get_child_value)(params, 0), *u = G(g_variant_lookup_value)(f, "UUIDs", NULL);
		if (u && G(g_variant_n_children)(u) == 1) {
			GVariant *x = G(g_variant_get_child_value)(u, 0);
			say("bluez: filter %s", G(g_variant_get_string)(x, NULL));
			G(g_variant_unref)(x);
		}
		if (u) G(g_variant_unref)(u);
		G(g_variant_unref)(f);
	} else if (!strcmp(name, "StartDiscovery")) {
		discovering = true;
		changed(ADAPTER, "org.bluez.Adapter1", "Discovering", G(g_variant_new_boolean)(true));
		G(g_idle_add_full)(G_PRIORITY_DEFAULT, hear, NULL, NULL);
	} else if (!strcmp(name, "StopDiscovery")) {
		discovering = false;
		changed(ADAPTER, "org.bluez.Adapter1", "Discovering", G(g_variant_new_boolean)(false));
	} else if (!strcmp(name, "Connect")) {
		connected = true;
		changed(DEVICE, "org.bluez.Device1", "Connected", G(g_variant_new_boolean)(true));
		add(SERVICE, service_ifs);
		add(CHAR, char_ifs);
		add(DESC, desc_ifs);
		resolved = true;
		changed(DEVICE, "org.bluez.Device1", "ServicesResolved", G(g_variant_new_boolean)(true));
	} else if (!strcmp(name, "ReadValue")) {
		static const guchar v[] = {1, 2, 3};
		d.ret(inv, G(g_variant_new)("(@ay)", bytes(v, 3)));
		return;
	} else if (!strcmp(name, "WriteValue")) {
		GVariant *v = G(g_variant_get_child_value)(params, 0), *o = G(g_variant_get_child_value)(params, 1), *t = G(g_variant_lookup_value)(o, "type", NULL);
		char line[300];
		snprintf(line, sizeof line, "%s %s", hex(v), t ? G(g_variant_get_string)(t, NULL) : "-");
		say("bluez: written %s", line);
		gsize n = 0;
		const guchar *b = G(g_variant_get_fixed_array)(v, &n, 1);
		if (n == 1 && b[0] == 0xdd) G(g_idle_add_full)(G_PRIORITY_DEFAULT, drop, NULL, NULL);
		if (t) G(g_variant_unref)(t);
		G(g_variant_unref)(o), G(g_variant_unref)(v);
	} else if (!strcmp(name, "StartNotify")) {
		G(g_idle_add_full)(G_PRIORITY_DEFAULT, notify_value, NULL, NULL);
	} else if (!strcmp(name, "RegisterAdvertisement")) {
		GVariant *o = G(g_variant_get_child_value)(params, 0);
		free(advert);
		advert = strdup(G(g_variant_get_string)(o, NULL));
		G(g_variant_unref)(o);
		free(app);
		app = strdup(d.sender(inv));
		call_app(advert, "org.freedesktop.DBus.Properties", "GetAll", G(g_variant_new)("(s)", "org.bluez.LEAdvertisement1"), advert_read, inv);
		return;
	} else if (!strcmp(name, "RegisterApplication")) {
		GVariant *o = G(g_variant_get_child_value)(params, 0);
		char *root = strdup(G(g_variant_get_string)(o, NULL));
		G(g_variant_unref)(o);
		free(app);
		app = strdup(d.sender(inv));
		call_app(root, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", NULL, app_read, inv);
		free(root);
		return;
	} else if (!strcmp(name, "UnregisterApplication")) {
		// The server's gone: BlueZ takes the advertisement back too.
		if (advert) call_app(advert, "org.bluez.LEAdvertisement1", "Release", NULL, released, NULL);
	}
	d.ret(inv, NULL);
}

static const vtable vt = {method, NULL, NULL, {0}};

int main(void) {
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!asuka_linux()) {
		fprintf(stderr, "bluez: no GLib\n");
		return 1;
	}
	*(void **)&d.node_info_new_for_xml = asuka_linux_sym("g_dbus_node_info_new_for_xml");
	*(void **)&d.lookup = asuka_linux_sym("g_dbus_node_info_lookup_interface");
	*(void **)&d.register_object = asuka_linux_sym("g_dbus_connection_register_object");
	*(void **)&d.emit = asuka_linux_sym("g_dbus_connection_emit_signal");
	*(void **)&d.ret = asuka_linux_sym("g_dbus_method_invocation_return_value");
	*(void **)&d.sender = asuka_linux_sym("g_dbus_method_invocation_get_sender");
	GError *e = NULL;
	bus = asuka_linux_bus(G_BUS_TYPE_SESSION, &e);
	GDBusNodeInfo *node = d.node_info_new_for_xml(xml, &e);
	if (!bus || !node) {
		fprintf(stderr, "bluez: %s\n", e ? e->message : "no bus");
		return 1;
	}
	struct {
		const char *path, *iface;
	} objs[] = {{"/", "org.freedesktop.DBus.ObjectManager"},    {ADAPTER, "org.bluez.Adapter1"}, {ADAPTER, "org.bluez.LEAdvertisingManager1"},
	            {ADAPTER, "org.bluez.GattManager1"},           {DEVICE, "org.bluez.Device1"},   {CHAR, "org.bluez.GattCharacteristic1"}};
	for (size_t i = 0; i < sizeof objs / sizeof *objs; i++)
		if (!d.register_object(bus, objs[i].path, d.lookup(node, objs[i].iface), &vt, NULL, NULL, &e)) {
			fprintf(stderr, "bluez: %s\n", e->message);
			return 1;
		}
	GVariant *r = G(g_dbus_connection_call_sync)(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
	                                             G(g_variant_new)("(su)", "org.bluez", 4), NULL, 0, 5000, NULL, &e);
	if (!r) {
		fprintf(stderr, "bluez: %s\n", e->message);
		return 1;
	}
	G(g_variant_unref)(r);
	say("bluez: %s", "ready");
	GMainLoop *loop = G(g_main_loop_new)(NULL, false);
	G(g_main_loop_run)(loop);
	return 0;
}
