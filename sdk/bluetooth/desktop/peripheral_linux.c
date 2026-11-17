// peripheral_linux.c: the peripheral role on BlueZ: the app's objects,
// which BlueZ calls. An advertisement is an LEAdvertisement1 registered
// with the adapter's LEAdvertisingManager1; a server an application (an
// ObjectManager over its services and characteristics) registered with
// its GattManager1. BlueZ calls a characteristic's ReadValue and
// WriteValue as centrals read and write it: the call is answered once
// the app answers the request (bt_engine_respond).
#include "bluetooth_linux.h"

#include <stdlib.h>
#include <string.h>

static const char *objects_xml =
    "<node>"
    " <interface name='org.freedesktop.DBus.ObjectManager'>"
    "  <method name='GetManagedObjects'><arg name='objects' type='a{oa{sa{sv}}}' direction='out'/></method>"
    " </interface>"
    " <interface name='org.bluez.LEAdvertisement1'>"
    "  <method name='Release'/>"
    "  <property name='Type' type='s' access='read'/>"
    "  <property name='ServiceUUIDs' type='as' access='read'/>"
    "  <property name='ManufacturerData' type='a{qv}' access='read'/>"
    "  <property name='ServiceData' type='a{sv}' access='read'/>"
    "  <property name='LocalName' type='s' access='read'/>"
    "  <property name='Includes' type='as' access='read'/>"
    "  <property name='Discoverable' type='b' access='read'/>"
    " </interface>"
    " <interface name='org.bluez.GattService1'>"
    "  <property name='UUID' type='s' access='read'/>"
    "  <property name='Primary' type='b' access='read'/>"
    " </interface>"
    " <interface name='org.bluez.GattCharacteristic1'>"
    "  <method name='ReadValue'><arg name='options' type='a{sv}' direction='in'/><arg name='value' type='ay' direction='out'/></method>"
    "  <method name='WriteValue'><arg name='value' type='ay' direction='in'/><arg name='options' type='a{sv}' direction='in'/></method>"
    "  <method name='StartNotify'/>"
    "  <method name='StopNotify'/>"
    "  <method name='Confirm'/>"
    "  <property name='UUID' type='s' access='read'/>"
    "  <property name='Service' type='o' access='read'/>"
    "  <property name='Flags' type='as' access='read'/>"
    "  <property name='Value' type='ay' access='read'/>"
    " </interface>"
    "</node>";

// Where the app's objects are, under its own name on the bus (BlueZ
// tells them apart by their owner).
#define BASE_PATH "/dev/asuka/bluetooth"

// The central of a subscription: BlueZ calls StartNotify as the first
// central subscribes and StopNotify as the last one leaves, without
// saying which.
static const bt_central pseudo_central = {"00:00:00:00:00:00", "BlueZ"};

static gpointer iface_info(const char *name) {
	static gpointer node;
	if (!node) {
		GError *e = NULL;
		node = G(g_dbus_node_info_new_for_xml)(objects_xml, &e);
		if (!node) {
			if (e) G(g_error_free)(e);
			return NULL;
		}
	}
	return G(g_dbus_node_info_lookup_interface)(node, name);
}

static guint export(const char *path, const char *iface, const GDBusInterfaceVTable *vt, void *data, char **why) {
	GError *e = NULL;
	gpointer info = iface_info(iface);
	guint id = info ? G(g_dbus_connection_register_object)(bl.conn, path, info, vt, data, NULL, &e) : 0;
	if (!id) {
		*why = asuka_strdup(e ? e->message : "the object isn't exported");
		if (e) G(g_error_free)(e);
	}
	return id;
}

static void unexport(guint *ids, size_t n) {
	for (size_t i = 0; i < n; i++)
		if (ids[i]) G(g_dbus_connection_unregister_object)(bl.conn, ids[i]);
}

static const char *peripheral_adapter(const char *iface, char **why) {
	const char *st = bl_current();
	if (strcmp(st, BT_ON) != 0) {
		*why = asuka_strdup(st);
		return NULL;
	}
	const char *a = bl_adapter();
	if (!a || !bl_props(a, iface)) {
		*why = asuka_sprintf("not available: the adapter can't %s (BlueZ has no %s on it)", !strcmp(iface, BL_IF_GATT_MANAGER) ? "serve GATT services" : "advertise",
		                     iface);
		return NULL;
	}
	return a;
}

// register_error is a registration's refusal in the engine's words.
static char *register_error(const char *name, const char *msg, const char *what) {
	char *why = bl_client_error(name, msg, false);
	if (msg && *msg && !strcmp(why, msg)) {
		char *w = asuka_sprintf("BlueZ can't %s: %s", what, msg);  // too much data, too many advertisements
		free(why);
		why = w;
	}
	return why;
}

static void ignore(void *ctx, GVariant *r, const char *name, const char *msg) {}

// ---- Advertisements ----

typedef struct advert {
	uint32_t id;
	char *path, *adapter;
	char *local_name;
	bt_strings services;
	bt_entries service_data, manufacturer_data;
	bool connectable, tx_power;
	guint reg;
	bool on;       // registered
	bool stopped;  // stopped while registering
	bool gone;     // forgotten (the engine's no longer)
	bt_done done;
	void *ctx;
	struct advert *next;
} advert;

static advert *adverts;

static advert *advert_of(uint32_t id) {
	for (advert *a = adverts; a; a = a->next)
		if (a->id == id && !a->gone) return a;
	return NULL;
}

static void advert_unlink(advert *a) {
	for (advert **p = &adverts; *p; p = &(*p)->next)
		if (*p == a) {
			*p = a->next;
			break;
		}
	a->gone = true;
}

static void advert_free(advert *a) {
	free(a->path);
	free(a->adapter);
	free(a->local_name);
	bt_strings_free(&a->services);
	bt_entries_free(&a->service_data);
	bt_entries_free(&a->manufacturer_data);
	free(a);
}

typedef struct {
	uint32_t id;
	char *err;
} stopped_ctx;

static void run_advertise_stopped(void *p) {
	stopped_ctx *s = p;
	bt_on_advertise_stopped(s->id, s->err);
	free(s->err);
	free(s);
}

static void run_server_stopped(void *p) {
	stopped_ctx *s = p;
	bt_on_server_stopped(s->id, s->err);
	free(s->err);
	free(s);
}

// end_advert ends an advertisement the platform stopped.
static void end_advert(advert *a, const char *why) {
	advert_unlink(a);
	if (a->reg) G(g_dbus_connection_unregister_object)(bl.conn, a->reg);
	a->reg = 0;
	stopped_ctx *s = malloc(sizeof *s);
	*s = (stopped_ctx){a->id, asuka_strdup(why)};
	bl_defer(run_advertise_stopped, s);
	if (a->on || !a->done) advert_free(a);  // else the registration's answer frees it
}

static void advert_method(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params,
                          GDBusMethodInvocation *inv, gpointer data) {
	advert *a = data;
	G(g_dbus_method_invocation_return_value)(inv, NULL);
	// BlueZ stopped it (the adapter went, its instances were taken).
	if (!strcmp(method, "Release") && !a->gone) end_advert(a, "BlueZ released the advertisement");
	bl_flush();
}

static GVariant *advert_property(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GError **err,
                                 gpointer data) {
	advert *a = data;
	if (!strcmp(name, "Type")) return G(g_variant_new_string)(a->connectable ? "peripheral" : "broadcast");
	if (!strcmp(name, "ServiceUUIDs")) {
		GVariantBuilder b;
		G(g_variant_builder_init)(&b, G_VARIANT_TYPE("as"));
		for (size_t i = 0; i < a->services.len; i++) {
			char *s = bt_short(a->services.data[i]);
			G(g_variant_builder_add)(&b, "s", s);
			free(s);
		}
		return G(g_variant_builder_end)(&b);
	}
	if (!strcmp(name, "ManufacturerData")) {
		GVariantBuilder b;
		G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{qv}"));
		for (size_t i = 0; i < a->manufacturer_data.len; i++) {
			char *end;
			unsigned long id = strtoul(a->manufacturer_data.data[i].key, &end, 16);
			if (*end || id > 0xffff) continue;
			bt_entry *e = &a->manufacturer_data.data[i];
			G(g_variant_builder_add)(&b, "{qv}", (guint16)id, bl_byte_array(e->value.data, e->value.len));
		}
		return G(g_variant_builder_end)(&b);
	}
	if (!strcmp(name, "ServiceData")) {
		GVariantBuilder b;
		G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
		for (size_t i = 0; i < a->service_data.len; i++) {
			bt_entry *e = &a->service_data.data[i];
			char *s = bt_short(e->key);
			G(g_variant_builder_add)(&b, "{sv}", s, bl_byte_array(e->value.data, e->value.len));
			free(s);
		}
		return G(g_variant_builder_end)(&b);
	}
	if (!strcmp(name, "LocalName")) return a->local_name && *a->local_name ? G(g_variant_new_string)(a->local_name) : NULL;
	if (!strcmp(name, "Includes")) {
		const char *inc[] = {"tx-power"};
		return bl_string_array(inc, a->tx_power ? 1 : 0);
	}
	// A connectable advertisement is general discoverable, as the other
	// platforms' are (iOS's scanners want the flag); a broadcast has no
	// flags.
	if (!strcmp(name, "Discoverable")) return a->connectable ? G(g_variant_new_boolean)(true) : NULL;
	return NULL;
}

static const GDBusInterfaceVTable advert_vt = {advert_method, advert_property, NULL, {0}};

static void unregister_advert(advert *a) {
	a->on = false;
	bl_call_bluez(a->adapter, BL_IF_ADV_MANAGER, "UnregisterAdvertisement", G(g_variant_new)("(o)", a->path), ignore, NULL);
	if (a->reg) G(g_dbus_connection_unregister_object)(bl.conn, a->reg);
	a->reg = 0;
}

static void advert_registered(void *ctx, GVariant *r, const char *name, const char *msg) {
	advert *a = ctx;
	bt_done done = a->done;
	void *dctx = a->ctx;
	a->done = NULL;
	if (!r) {
		if (!a->gone) advert_unlink(a);
		if (a->reg) G(g_dbus_connection_unregister_object)(bl.conn, a->reg);
		char *why = register_error(name, msg, "advertise it");
		bl_answer(done, dctx, why);
		free(why);
		advert_free(a);
		return;
	}
	a->on = true;
	bl_answer(done, dctx, NULL);
	if (a->stopped || a->gone) {
		unregister_advert(a);
		advert_free(a);
	}
}

typedef struct {
	uint32_t id;
	bt_advertising args;
	bt_done done;
	void *ctx;
} advertise_args;

void bt_engine_stop_advertise(uint32_t id);

static void start_advertise(void *p) {
	advertise_args *x = p;
	char *why = NULL;
	const char *adapter = peripheral_adapter(BL_IF_ADV_MANAGER, &why);
	if (!adapter) {
		bl_answer(x->done, x->ctx, why);
		free(why);
		goto out;
	}
	bt_engine_stop_advertise(x->id);
	advert *a = calloc(1, sizeof *a);
	*a = (advert){.id = x->id, .path = asuka_sprintf("%s/advertisement%u", BASE_PATH, x->id), .adapter = asuka_strdup(adapter),
	              .local_name = asuka_strdup(x->args.local_name), .connectable = x->args.connectable, .tx_power = x->args.include_tx_power,
	              .done = x->done, .ctx = x->ctx};
	for (size_t i = 0; i < x->args.services.len; i++) bt_strings_push(&a->services, asuka_strdup(x->args.services.data[i]));
	for (size_t i = 0; i < x->args.service_data.len; i++)
		bt_entries_push(&a->service_data, asuka_strdup(x->args.service_data.data[i].key), x->args.service_data.data[i].value.data,
		                x->args.service_data.data[i].value.len);
	for (size_t i = 0; i < x->args.manufacturer_data.len; i++)
		bt_entries_push(&a->manufacturer_data, asuka_strdup(x->args.manufacturer_data.data[i].key), x->args.manufacturer_data.data[i].value.data,
		                x->args.manufacturer_data.data[i].value.len);
	a->reg = export(a->path, BL_IF_ADVERT, &advert_vt, a, &why);
	if (!a->reg) {
		bl_answer(x->done, x->ctx, why);
		free(why);
		advert_free(a);
		goto out;
	}
	a->next = adverts;
	adverts = a;
	bl_call_bluez(adapter, BL_IF_ADV_MANAGER, "RegisterAdvertisement", G(g_variant_new)("(o@a{sv})", a->path, bl_empty_dict()), advert_registered, a);
out:
	free(x->args.local_name);
	bt_strings_free(&x->args.services);
	bt_entries_free(&x->args.service_data);
	bt_entries_free(&x->args.manufacturer_data);
	free(x);
	bl_flush();
}

void bt_engine_advertise(uint32_t id, const bt_advertising *args, bt_done done, void *ctx) {
	advertise_args *x = calloc(1, sizeof *x);
	x->id = id, x->done = done, x->ctx = ctx;
	x->args.local_name = asuka_strdup(args->local_name);
	x->args.connectable = args->connectable, x->args.include_tx_power = args->include_tx_power;
	for (size_t i = 0; i < args->services.len; i++) bt_strings_push(&x->args.services, asuka_strdup(args->services.data[i]));
	for (size_t i = 0; i < args->service_data.len; i++)
		bt_entries_push(&x->args.service_data, asuka_strdup(args->service_data.data[i].key), args->service_data.data[i].value.data,
		                args->service_data.data[i].value.len);
	for (size_t i = 0; i < args->manufacturer_data.len; i++)
		bt_entries_push(&x->args.manufacturer_data, asuka_strdup(args->manufacturer_data.data[i].key), args->manufacturer_data.data[i].value.data,
		                args->manufacturer_data.data[i].value.len);
	bl_start(start_advertise, x);
}

void bt_engine_stop_advertise(uint32_t id) {
	advert *a = advert_of(id);
	if (!a) return;
	advert_unlink(a);
	if (!a->on) {
		a->stopped = true;  // unregistered once registered
		return;
	}
	unregister_advert(a);
	advert_free(a);
}

// ---- Servers ----

typedef struct local {
	char *path, *iface, *uuid;
	bool primary;   // a service's
	char *parent;   // a characteristic's service
	uint32_t handle;
	int properties;
	bt_strings flags;
	bool has_fixed;
	asuka_bytes fixed;  // a fixed value
	asuka_bytes value;  // the last notified
	bool notifying;
	struct server *server;
} local;

typedef struct server {
	uint32_t id;
	char *root, *adapter;
	guint *regs;
	size_t nregs;
	local *objects;  // services, each followed by its characteristics
	size_t nobjects;
	bool on, stopped, gone;
	bt_done done;
	void *ctx;
	struct server *next;
} server;

static server *servers;

// A central's read or write waiting for the app's answer.
typedef struct request {
	uint32_t id, server;
	GDBusMethodInvocation *inv;
	bool write;
	struct request *next;
} request;

static request *requests;
static uint32_t next_request;

static server *server_of(uint32_t id) {
	for (server *s = servers; s; s = s->next)
		if (s->id == id && !s->gone) return s;
	return NULL;
}

// server_error is an ATT error as the D-Bus error BlueZ answers a
// central with.
static void server_error(int code, bool write, const char **name, char **msg) {
	if (code >= 0x80 && code <= 0x9f) *name = "org.bluez.Error.Failed", *msg = asuka_sprintf("0x%02x", code);
	else if ((code == 0x02 && !write) || (code == 0x03 && write)) *name = "org.bluez.Error.NotPermitted", *msg = asuka_strdup("not permitted");
	else if (code == 0x06) *name = "org.bluez.Error.NotSupported", *msg = asuka_strdup("not supported");
	else if (code == 0x08) *name = "org.bluez.Error.NotAuthorized", *msg = asuka_strdup("not authorized");
	else if (code == 0x0d) *name = "org.bluez.Error.InvalidValueLength", *msg = asuka_strdup("invalid value length");
	else if (code == 0x07) *name = "org.bluez.Error.InvalidOffset", *msg = asuka_strdup("invalid offset");
	else if (code == 0xfe) *name = "org.bluez.Error.InProgress", *msg = asuka_strdup("in progress");
	else if (code == 0xfd) *name = "org.bluez.Error.ImproperlyConfigured", *msg = asuka_strdup("improperly configured");
	else *name = "org.bluez.Error.Rejected", *msg = asuka_sprintf("ATT error 0x%02x", code);
}

static void return_error(GDBusMethodInvocation *inv, int code, bool write) {
	const char *name;
	char *msg;
	server_error(code, write, &name, &msg);
	G(g_dbus_method_invocation_return_dbus_error)(inv, name, msg);
	free(msg);
}

// fail_requests answers the server's requests waiting: it's gone.
static void fail_requests(uint32_t server_id) {
	for (request **p = &requests; *p;) {
		request *r = *p;
		if (r->server == server_id) {
			*p = r->next;
			return_error(r->inv, 0x0e, r->write);
			free(r);
		} else {
			p = &r->next;
		}
	}
}

static void server_free(server *s) {
	unexport(s->regs, s->nregs);
	free(s->regs);
	for (size_t i = 0; i < s->nobjects; i++) {
		local *o = &s->objects[i];
		free(o->path);
		free(o->iface);
		free(o->uuid);
		free(o->parent);
		bt_strings_free(&o->flags);
		free(o->fixed.data);
		free(o->value.data);
	}
	free(s->objects);
	free(s->root);
	free(s->adapter);
	free(s);
}

static void server_unlink(server *s) {
	for (server **p = &servers; *p; p = &(*p)->next)
		if (*p == s) {
			*p = s->next;
			break;
		}
	s->gone = true;
	fail_requests(s->id);
}

static GVariant *local_property(local *o, const char *name) {
	if (!strcmp(name, "UUID")) return G(g_variant_new_string)(o->uuid);
	bool svc = !strcmp(o->iface, BL_IF_GATT_SERVICE);
	if (!strcmp(name, "Primary") && svc) return G(g_variant_new_boolean)(o->primary);
	if (!strcmp(name, "Service") && !svc) return G(g_variant_new_object_path)(o->parent);
	if (!strcmp(name, "Flags") && !svc) return bl_string_array((const char *const *)o->flags.data, o->flags.len);
	if (!strcmp(name, "Value") && !svc) {
		const asuka_bytes *v = o->value.data || !o->has_fixed ? &o->value : &o->fixed;
		return bl_byte_array(v->data, v->len);
	}
	return NULL;
}

static GVariant *local_get(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GError **err, gpointer data) {
	return local_property(data, name);
}

// managed_objects is GetManagedObjects' a{oa{sa{sv}}}: the objects with
// their properties, as BlueZ reads the application.
static GVariant *managed_objects(server *s) {
	GVariantBuilder objs;
	G(g_variant_builder_init)(&objs, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
	for (size_t i = 0; i < s->nobjects; i++) {
		local *o = &s->objects[i];
		static const char *svc_props[] = {"UUID", "Primary"}, *char_props[] = {"UUID", "Service", "Flags"};
		bool svc = !strcmp(o->iface, BL_IF_GATT_SERVICE);
		GVariantBuilder props;
		G(g_variant_builder_init)(&props, G_VARIANT_TYPE("a{sv}"));
		for (size_t k = 0; k < (svc ? 2u : 3u); k++) {
			const char *n = svc ? svc_props[k] : char_props[k];
			GVariant *v = local_property(o, n);
			if (v) G(g_variant_builder_add)(&props, "{sv}", n, v);
		}
		GVariantBuilder ifs;
		G(g_variant_builder_init)(&ifs, G_VARIANT_TYPE("a{sa{sv}}"));
		G(g_variant_builder_add)(&ifs, "{s@a{sv}}", o->iface, G(g_variant_builder_end)(&props));
		G(g_variant_builder_add)(&objs, "{o@a{sa{sv}}}", o->path, G(g_variant_builder_end)(&ifs));
	}
	return G(g_variant_builder_end)(&objs);
}

static void root_method(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params,
                        GDBusMethodInvocation *inv, gpointer data) {
	if (strcmp(method, "GetManagedObjects") != 0) {
		G(g_dbus_method_invocation_return_dbus_error)(inv, BL_ERR_UNKNOWN_METHOD, "no such method");
		return;
	}
	G(g_dbus_method_invocation_return_value)(inv, G(g_variant_new)("(@a{oa{sa{sv}}})", managed_objects(data)));
}

static const GDBusInterfaceVTable root_vt = {root_method, NULL, NULL, {0}};

typedef struct {
	uint32_t server, request, handle;
	bt_central central;
	asuka_bytes value;
	int offset;
	bool read, on, subscription;
} request_ctx;

static void run_request(void *p) {
	request_ctx *r = p;
	if (r->subscription) bt_on_subscription(r->server, r->handle, &r->central, r->on);
	else if (r->read) bt_on_read(r->server, r->request, r->handle, &r->central, r->offset);
	else bt_on_write(r->server, r->request, r->handle, &r->central, r->value, r->offset);
	free(r->central.id);
	free(r->central.name);
	free(r->value.data);
	free(r);
}

// central_of is the device a call's options name (BlueZ's "device").
static bt_central central_of(GVariant *opts) {
	GVariant *dv = bl_option(opts, "device");
	const char *p = bl_str(dv);
	bt_central c;
	if (!p || !*p) {
		c = (bt_central){asuka_strdup(pseudo_central.id), asuka_strdup(pseudo_central.name)};
	} else {
		bl_iface *f = bl_props(p, BL_IF_DEVICE);
		bl_device d = bl_parse_device(f);
		c.id = d.address ? asuka_strdup(d.address) : bl_address_from_path(p);
		c.name = bl_device_name(&d);
		bl_device_free(&d);
	}
	if (dv) G(g_variant_unref)(dv);
	return c;
}

static int option_int(GVariant *opts, const char *key) {
	GVariant *v = bl_option(opts, key);
	int n = 0;
	if (v) {
		bl_int(v, &n);
		G(g_variant_unref)(v);
	}
	return n;
}

static void local_method(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params,
                         GDBusMethodInvocation *inv, gpointer data) {
	local *o = data;
	server *s = o->server;
	if (!strcmp(method, "ReadValue")) {
		GVariant *opts = G(g_variant_get_child_value)(params, 0);
		int offset = option_int(opts, "offset");
		if (o->has_fixed) {
			if ((size_t)offset > o->fixed.len) return_error(inv, 0x07, false);
			else G(g_dbus_method_invocation_return_value)(inv, G(g_variant_new)("(@ay)", bl_byte_array(o->fixed.data + offset, o->fixed.len - (size_t)offset)));
		} else if (s->gone) {
			return_error(inv, 0x0e, false);
		} else {
			request *r = calloc(1, sizeof *r);
			*r = (request){++next_request, s->id, inv, false, requests};
			requests = r;
			request_ctx *x = calloc(1, sizeof *x);
			*x = (request_ctx){.server = s->id, .request = r->id, .handle = o->handle, .central = central_of(opts), .offset = offset, .read = true};
			bl_defer(run_request, x);
		}
		G(g_variant_unref)(opts);
	} else if (!strcmp(method, "WriteValue")) {
		GVariant *v = G(g_variant_get_child_value)(params, 0), *opts = G(g_variant_get_child_value)(params, 1);
		GVariant *t = bl_option(opts, "type");
		// BlueZ doesn't wait for the app's answer to a write of a
		// characteristic that takes writes without response (any write of
		// it): it said yes already.
		bool response = !(o->properties & BT_PROP_WRITE_NO_RESPONSE) && !(t && bl_str(t) && !strcmp(bl_str(t), "command"));
		if (t) G(g_variant_unref)(t);
		if (s->gone) {
			return_error(inv, 0x0e, true);
		} else {
			request_ctx *x = calloc(1, sizeof *x);
			*x = (request_ctx){.server = s->id, .handle = o->handle, .central = central_of(opts), .value = bl_bytes(v), .offset = option_int(opts, "offset")};
			if (response) {
				request *r = calloc(1, sizeof *r);
				*r = (request){++next_request, s->id, inv, true, requests};
				requests = r;
				x->request = r->id;
			} else {
				G(g_dbus_method_invocation_return_value)(inv, NULL);
			}
			bl_defer(run_request, x);
		}
		G(g_variant_unref)(opts);
		G(g_variant_unref)(v);
	} else if (!strcmp(method, "StartNotify") || !strcmp(method, "StopNotify")) {
		G(g_dbus_method_invocation_return_value)(inv, NULL);
		bool on = !strcmp(method, "StartNotify");
		if (o->notifying != on && !s->gone) {
			o->notifying = on;
			request_ctx *x = calloc(1, sizeof *x);
			*x = (request_ctx){.server = s->id, .handle = o->handle, .central = {asuka_strdup(pseudo_central.id), asuka_strdup(pseudo_central.name)},
			                   .on = on, .subscription = true};
			bl_defer(run_request, x);
		}
	} else if (!strcmp(method, "Confirm")) {
		G(g_dbus_method_invocation_return_value)(inv, NULL);  // an indication acknowledged
	} else {
		G(g_dbus_method_invocation_return_dbus_error)(inv, BL_ERR_UNKNOWN_METHOD, "no such method");
	}
	bl_flush();
}

static const GDBusInterfaceVTable local_vt = {local_method, local_get, NULL, {0}};

static void unregister_server(server *s) {
	s->on = false;
	bl_call_bluez(s->adapter, BL_IF_GATT_MANAGER, "UnregisterApplication", G(g_variant_new)("(o)", s->root), ignore, NULL);
}

static void server_registered(void *ctx, GVariant *r, const char *name, const char *msg) {
	server *s = ctx;
	bt_done done = s->done;
	void *dctx = s->ctx;
	s->done = NULL;
	if (!r) {
		if (!s->gone) server_unlink(s);
		char *why = register_error(name, msg, "serve them");
		bl_answer(done, dctx, why);
		free(why);
		server_free(s);
		return;
	}
	s->on = true;
	bl_answer(done, dctx, NULL);
	if (s->stopped || s->gone) {
		unregister_server(s);
		server_free(s);
	}
}

// flags are a characteristic's BlueZ flags: its properties, encrypted
// where its permissions say so.
static bt_strings flags_of(int props, int perms) {
	bt_strings f = {0};
	if (props & BT_PROP_BROADCAST) bt_strings_push(&f, asuka_strdup("broadcast"));
	if (props & BT_PROP_READ) bt_strings_push(&f, asuka_strdup(perms & BT_PERM_READ_ENCRYPTED ? "encrypt-read" : "read"));
	if (props & BT_PROP_WRITE_NO_RESPONSE) bt_strings_push(&f, asuka_strdup("write-without-response"));
	if (props & BT_PROP_WRITE) bt_strings_push(&f, asuka_strdup(perms & BT_PERM_WRITE_ENCRYPTED ? "encrypt-write" : "write"));
	if (props & BT_PROP_NOTIFY) bt_strings_push(&f, asuka_strdup("notify"));
	if (props & BT_PROP_INDICATE) bt_strings_push(&f, asuka_strdup("indicate"));
	if (props & BT_PROP_SIGNED_WRITE) bt_strings_push(&f, asuka_strdup("authenticated-signed-writes"));
	if (props & BT_PROP_EXTENDED) bt_strings_push(&f, asuka_strdup("extended-properties"));
	return f;
}

typedef struct {
	uint32_t id;
	bt_local_service *services;
	size_t n;
	bt_done done;
	void *ctx;
} serve_args;

static void serve_args_free(serve_args *x) {
	for (size_t i = 0; i < x->n; i++) {
		for (size_t j = 0; j < x->services[i].ncharacteristics; j++) {
			free(x->services[i].characteristics[j].uuid);
			free(x->services[i].characteristics[j].value.data);
		}
		free(x->services[i].characteristics);
		free(x->services[i].uuid);
	}
	free(x->services);
	free(x);
}

void bt_engine_unserve(uint32_t id);

static void start_serve(void *p) {
	serve_args *x = p;
	char *why = NULL;
	const char *adapter = peripheral_adapter(BL_IF_GATT_MANAGER, &why);
	if (!adapter) {
		bl_answer(x->done, x->ctx, why);
		free(why);
		serve_args_free(x);
		bl_flush();
		return;
	}
	bt_engine_unserve(x->id);
	server *s = calloc(1, sizeof *s);
	*s = (server){.id = x->id, .root = asuka_sprintf("%s/server%u", BASE_PATH, x->id), .adapter = asuka_strdup(adapter), .done = x->done, .ctx = x->ctx};
	size_t total = x->n;
	for (size_t i = 0; i < x->n; i++) total += x->services[i].ncharacteristics;
	s->objects = calloc(total ? total : 1, sizeof *s->objects);
	for (size_t i = 0; i < x->n; i++) {
		bt_local_service *ls = &x->services[i];
		local *sv = &s->objects[s->nobjects++];
		*sv = (local){.path = asuka_sprintf("%s/service%zu", s->root, i), .iface = asuka_strdup(BL_IF_GATT_SERVICE), .uuid = bt_normalize(ls->uuid),
		              .primary = ls->primary, .server = s};
		for (size_t j = 0; j < ls->ncharacteristics; j++) {
			bt_local_characteristic *lc = &ls->characteristics[j];
			local *ch = &s->objects[s->nobjects++];
			*ch = (local){.path = asuka_sprintf("%s/char%zu", sv->path, j), .iface = asuka_strdup(BL_IF_GATT_CHAR), .uuid = bt_normalize(lc->uuid),
			              .parent = asuka_strdup(sv->path), .handle = lc->handle, .properties = lc->properties,
			              .flags = flags_of(lc->properties, lc->permissions), .has_fixed = lc->has_value, .server = s};
			if (lc->has_value) ch->fixed = asuka_bytes_copy(lc->value.data, lc->value.len);
		}
	}
	s->regs = calloc(s->nobjects + 1, sizeof *s->regs);
	s->regs[s->nregs++] = export(s->root, BL_IF_OBJECT_MANAGER, &root_vt, s, &why);
	for (size_t i = 0; i < s->nobjects && s->regs[s->nregs - 1]; i++) s->regs[s->nregs++] = export(s->objects[i].path, s->objects[i].iface, &local_vt, &s->objects[i], &why);
	if (!s->regs[s->nregs - 1]) {
		bl_answer(x->done, x->ctx, why);
		free(why);
		server_free(s);
		serve_args_free(x);
		bl_flush();
		return;
	}
	s->next = servers;
	servers = s;
	bl_call_bluez(adapter, BL_IF_GATT_MANAGER, "RegisterApplication", G(g_variant_new)("(o@a{sv})", s->root, bl_empty_dict()), server_registered, s);
	serve_args_free(x);
	bl_flush();
}

void bt_engine_serve(uint32_t id, const bt_local_service *services, size_t n, bt_done done, void *ctx) {
	serve_args *x = calloc(1, sizeof *x);
	*x = (serve_args){id, calloc(n ? n : 1, sizeof(bt_local_service)), n, done, ctx};
	for (size_t i = 0; i < n; i++) {
		const bt_local_service *s = &services[i];
		x->services[i] = (bt_local_service){asuka_strdup(s->uuid), s->primary, calloc(s->ncharacteristics ? s->ncharacteristics : 1, sizeof(bt_local_characteristic)),
		                                    s->ncharacteristics};
		for (size_t j = 0; j < s->ncharacteristics; j++) {
			const bt_local_characteristic *c = &s->characteristics[j];
			x->services[i].characteristics[j] = (bt_local_characteristic){c->handle, asuka_strdup(c->uuid), c->properties, c->permissions, c->has_value,
			                                                              asuka_bytes_copy(c->value.data, c->value.len)};
		}
	}
	bl_start(start_serve, x);
}

void bt_engine_unserve(uint32_t id) {
	server *s = server_of(id);
	if (!s) return;
	server_unlink(s);
	if (!s->on) {
		s->stopped = true;  // unregistered once registered
		return;
	}
	unregister_server(s);
	server_free(s);
}

void bt_engine_respond(uint32_t server_id, uint32_t request_id, asuka_bytes value, int error) {
	for (request **p = &requests; *p; p = &(*p)->next) {
		request *r = *p;
		if (r->id != request_id || r->server != server_id) continue;
		*p = r->next;
		if (error) return_error(r->inv, error, r->write);
		else if (r->write) G(g_dbus_method_invocation_return_value)(r->inv, NULL);
		else G(g_dbus_method_invocation_return_value)(r->inv, G(g_variant_new)("(@ay)", bl_byte_array(value.data, value.len)));
		free(r);
		return;
	}
}

// bt_engine_notify sends the value to the characteristic's subscribers:
// BlueZ notifies (or indicates) a PropertiesChanged of its Value to
// every central subscribed; it can't to some of them only.
void bt_engine_notify(uint32_t server_id, uint32_t handle, asuka_bytes value) {
	server *s = server_of(server_id);
	for (size_t i = 0; s && i < s->nobjects; i++) {
		local *o = &s->objects[i];
		if (strcmp(o->iface, BL_IF_GATT_CHAR) != 0 || o->handle != handle) continue;
		free(o->value.data);
		o->value = asuka_bytes_copy(value.data, value.len);
		GVariantBuilder b;
		G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
		G(g_variant_builder_add)(&b, "{sv}", "Value", bl_byte_array(value.data, value.len));
		GVariant *params = G(g_variant_new)("(s@a{sv}@as)", BL_IF_GATT_CHAR, G(g_variant_builder_end)(&b), bl_string_array(NULL, 0));
		G(g_dbus_connection_emit_signal)(bl.conn, NULL, o->path, BL_IF_PROPERTIES, "PropertiesChanged", params, NULL);
	}
}

// bl_peripheral_gone: BlueZ (or the adapter) went: the advertisements
// and the servers end.
void bl_peripheral_gone(const char *why) {
	while (adverts) end_advert(adverts, why);
	while (servers) {
		server *s = servers;
		server_unlink(s);
		unexport(s->regs, s->nregs);
		s->nregs = 0;
		stopped_ctx *x = malloc(sizeof *x);
		*x = (stopped_ctx){s->id, asuka_strdup(why)};
		bl_defer(run_server_stopped, x);
		if (s->on || !s->done) server_free(s);  // else the registration's answer frees it
	}
}
