// asuka/bluetooth on Linux: BlueZ, on the system bus (org.bluez), through
// GDBus, every call asynchronous. The objects BlueZ exports (its
// adapters, the devices it knows, a connected device's GATT attributes)
// are read once with GetManagedObjects and followed with
// InterfacesAdded, InterfacesRemoved and PropertiesChanged; what the app
// does goes through their methods: Adapter1's discovery (the scans),
// Device1's Connect and Disconnect, GattCharacteristic1's ReadValue,
// WriteValue, StartNotify, StopNotify. As a peripheral, the app exports
// its own objects for BlueZ to call (bluetooth_linux_peripheral.c).
//
// No bluetoothd on the bus (or no adapter) is "unsupported"; a bus that
// won't let the app talk to it (a Flatpak without
// --system-talk-name=org.bluez) is "denied". Linux asks no permission:
// a request is the state.
//
// What the engine tells bluetooth.c (its answers, its events) waits in a
// queue until the engine's done with what it's doing (bl_flush): no
// event reaches the package while the engine walks its own lists.
#include "bluetooth_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct bl bl;
struct gx gx;

// ---- GIO's functions this package calls (asuka_linux.h has the rest) ----

static bool load_gx(void) {
	static bool tried, ok;
	if (tried) return ok;
	tried = true;
	if (!asuka_linux()) return false;
	ok = true;
#define S(f, name) ok = (*(void **)&gx.f = asuka_linux_sym(name)) && ok
	S(node_info_new_for_xml, "g_dbus_node_info_new_for_xml");
	S(node_info_lookup_interface, "g_dbus_node_info_lookup_interface");
	S(register_object, "g_dbus_connection_register_object");
	S(unregister_object, "g_dbus_connection_unregister_object");
	S(emit_signal, "g_dbus_connection_emit_signal");
	S(return_value, "g_dbus_method_invocation_return_value");
	S(return_dbus_error, "g_dbus_method_invocation_return_dbus_error");
	S(variant_ref, "g_variant_ref");
	S(variant_new_uint16, "g_variant_new_uint16");
	S(variant_new_int16, "g_variant_new_int16");
	S(variant_new_byte, "g_variant_new_byte");
#undef S
	return ok;
}

// ---- The queue: what bluetooth.c is told, once the engine's done ----

typedef struct deferred {
	void (*run)(void *ctx);
	void *ctx;
	struct deferred *next;
} deferred;

static deferred *queue_head, *queue_tail;

void bl_defer(void (*run)(void *ctx), void *ctx) {
	deferred *d = malloc(sizeof *d);
	*d = (deferred){run, ctx, NULL};
	if (queue_tail) queue_tail->next = d;
	else queue_head = d;
	queue_tail = d;
}

void bl_flush(void) {
	while (queue_head) {
		deferred *d = queue_head;
		queue_head = d->next;
		if (!queue_head) queue_tail = NULL;
		d->run(d->ctx);
		free(d);
	}
}

// The answers, deferred.

typedef struct {
	bt_done done;
	void *ctx;
	char *err;
} answer;

static void run_answer(void *p) {
	answer *a = p;
	a->done(a->ctx, a->err);
	free(a->err);
	free(a);
}

void bl_answer(bt_done done, void *ctx, const char *err) {
	if (!done) return;
	answer *a = malloc(sizeof *a);
	*a = (answer){done, ctx, err ? asuka_strdup(err) : NULL};
	bl_defer(run_answer, a);
}

// ---- Values ----

GVariant *bl_ref(GVariant *v) { return v ? gx.variant_ref(v) : NULL; }

bool bl_is(GVariant *v, const char *type) { return v && strcmp(G(g_variant_get_type_string)(v), type) == 0; }

const char *bl_str(GVariant *v) {
	if (bl_is(v, "s") || bl_is(v, "o")) return G(g_variant_get_string)(v, NULL);
	return NULL;
}

bool bl_bool(GVariant *v) { return bl_is(v, "b") && G(g_variant_get_boolean)(v); }

bool bl_int(GVariant *v, int *out) {
	if (!v) return false;
	const char *t = G(g_variant_get_type_string)(v);
	switch (t[0] && !t[1] ? t[0] : 0) {
	case 'n': *out = G(g_variant_get_int16)(v); return true;
	case 'q': *out = G(g_variant_get_uint16)(v); return true;
	case 'i': *out = G(g_variant_get_int32)(v); return true;
	case 'u': *out = (int)G(g_variant_get_uint32)(v); return true;
	case 'y': *out = G(g_variant_get_byte)(v); return true;
	case 'x': *out = (int)G(g_variant_get_int64)(v); return true;
	case 't': *out = (int)G(g_variant_get_uint64)(v); return true;
	}
	return false;
}

asuka_bytes bl_bytes(GVariant *v) {
	if (!bl_is(v, "ay")) return (asuka_bytes){0};
	gsize n = 0;
	const void *p = G(g_variant_get_fixed_array)(v, &n, 1);
	return asuka_bytes_copy(p, n);
}

bt_strings bl_strings(GVariant *v) {
	bt_strings out = {0};
	if (!bl_is(v, "as") && !bl_is(v, "ao")) return out;
	gsize n = G(g_variant_n_children)(v);
	for (gsize i = 0; i < n; i++) {
		GVariant *c = G(g_variant_get_child_value)(v, i);
		bt_strings_push(&out, asuka_strdup(G(g_variant_get_string)(c, NULL)));
		G(g_variant_unref)(c);
	}
	return out;
}

GVariant *bl_byte_array(const void *p, size_t n) {
	return G(g_variant_new_fixed_array)(G_VARIANT_TYPE("y"), n ? p : "", n, 1);
}

GVariant *bl_string_array(const char *const *s, size_t n) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("as"));
	for (size_t i = 0; i < n; i++) G(g_variant_builder_add)(&b, "s", s[i]);
	return G(g_variant_builder_end)(&b);
}

GVariant *bl_empty_dict(void) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	return G(g_variant_builder_end)(&b);
}

// bl_option is a{sv} dict's entry key's value (a reference), NULL if none.
GVariant *bl_option(GVariant *dict, const char *key) {
	if (!dict || !bl_is(dict, "a{sv}")) return NULL;
	return G(g_variant_lookup_value)(dict, key, NULL);
}

// ---- BlueZ's objects ----

static bl_object *find_object(const char *path, bool make) {
	for (size_t i = 0; i < bl.nobjects; i++)
		if (strcmp(bl.objects[i].path, path) == 0) return &bl.objects[i];
	if (!make) return NULL;
	bl.objects = realloc(bl.objects, (bl.nobjects + 1) * sizeof *bl.objects);
	bl.objects[bl.nobjects] = (bl_object){asuka_strdup(path), NULL, 0};
	return &bl.objects[bl.nobjects++];
}

static void free_iface(bl_iface *f) {
	for (size_t k = 0; k < f->nprops; k++) {
		free(f->props[k].name);
		G(g_variant_unref)(f->props[k].value);
	}
	free(f->props);
	free(f->name);
}

static void free_object(bl_object *o) {
	for (size_t j = 0; j < o->nifaces; j++) free_iface(&o->ifaces[j]);
	free(o->ifaces);
	free(o->path);
}

static void clear_objects(void) {
	for (size_t i = 0; i < bl.nobjects; i++) free_object(&bl.objects[i]);
	free(bl.objects);
	bl.objects = NULL;
	bl.nobjects = 0;
}

static bl_iface *find_iface(bl_object *o, const char *iface, bool make) {
	if (!o) return NULL;
	for (size_t i = 0; i < o->nifaces; i++)
		if (strcmp(o->ifaces[i].name, iface) == 0) return &o->ifaces[i];
	if (!make) return NULL;
	o->ifaces = realloc(o->ifaces, (o->nifaces + 1) * sizeof *o->ifaces);
	o->ifaces[o->nifaces] = (bl_iface){asuka_strdup(iface), NULL, 0};
	return &o->ifaces[o->nifaces++];
}

bl_iface *bl_props(const char *path, const char *iface) { return find_iface(find_object(path, false), iface, false); }

GVariant *bl_prop(bl_iface *f, const char *name) {
	for (size_t i = 0; f && i < f->nprops; i++)
		if (strcmp(f->props[i].name, name) == 0) return f->props[i].value;
	return NULL;
}

GVariant *bl_get(const char *path, const char *iface, const char *name) { return bl_prop(bl_props(path, iface), name); }

static void set_prop(bl_iface *f, const char *name, GVariant *v) {
	for (size_t i = 0; i < f->nprops; i++)
		if (strcmp(f->props[i].name, name) == 0) {
			G(g_variant_unref)(f->props[i].value);
			f->props[i].value = v;
			return;
		}
	f->props = realloc(f->props, (f->nprops + 1) * sizeof *f->props);
	f->props[f->nprops++] = (bl_prop_t){asuka_strdup(name), v};
}

static void del_prop(bl_iface *f, const char *name) {
	for (size_t i = 0; i < f->nprops; i++)
		if (strcmp(f->props[i].name, name) == 0) {
			free(f->props[i].name);
			G(g_variant_unref)(f->props[i].value);
			f->props[i] = f->props[--f->nprops];
			return;
		}
}

// set_props merges an a{sv} of properties into an interface's.
static void set_props(bl_iface *f, GVariant *dict) {
	GVariantIter it;
	G(g_variant_iter_init)(&it, dict);
	GVariant *e;
	while ((e = G(g_variant_iter_next_value)(&it))) {
		GVariant *k = G(g_variant_get_child_value)(e, 0), *v = G(g_variant_get_child_value)(e, 1);
		GVariant *inner = G(g_variant_get_variant)(v);
		set_prop(f, G(g_variant_get_string)(k, NULL), inner);
		G(g_variant_unref)(v);
		G(g_variant_unref)(k);
		G(g_variant_unref)(e);
	}
}

// add_ifaces adds a{sa{sv}}'s interfaces to the object at path.
static void add_ifaces(const char *path, GVariant *ifs) {
	bl_object *o = find_object(path, true);
	GVariantIter it;
	G(g_variant_iter_init)(&it, ifs);
	GVariant *e;
	while ((e = G(g_variant_iter_next_value)(&it))) {
		GVariant *k = G(g_variant_get_child_value)(e, 0), *props = G(g_variant_get_child_value)(e, 1);
		bl_iface *f = find_iface(o, G(g_variant_get_string)(k, NULL), true);
		set_props(f, props);
		G(g_variant_unref)(props);
		G(g_variant_unref)(k);
		G(g_variant_unref)(e);
	}
}

static bool has_iface(GVariant *ifs, const char *iface) {
	GVariant *v = G(g_variant_lookup_value)(ifs, iface, NULL);
	if (v) G(g_variant_unref)(v);
	return v != NULL;
}

// bl_paths is the objects' paths with the interface (under prefix, if
// any), sorted.
static int by_string(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

bt_strings bl_paths(const char *iface, const char *prefix) {
	bt_strings out = {0};
	size_t pn = prefix ? strlen(prefix) : 0;
	for (size_t i = 0; i < bl.nobjects; i++) {
		if (prefix && strncmp(bl.objects[i].path, prefix, pn) != 0) continue;
		if (find_iface(&bl.objects[i], iface, false)) bt_strings_push(&out, asuka_strdup(bl.objects[i].path));
	}
	if (out.len) qsort(out.data, out.len, sizeof *out.data, by_string);
	return out;
}

// ---- D-Bus calls ----

// bl_error splits a GDBus error into a D-Bus error's name and its
// message (as GDBus writes a remote one: "GDBus.Error:<name>: <msg>").
void bl_error(const GError *e, char **name, char **msg) {
	*name = NULL, *msg = NULL;
	if (!e) return;
	const char *s = e->message, *g = s ? strstr(s, "GDBus.Error:") : NULL;
	if (g) s = g + strlen("GDBus.Error:");
	if (!s) s = "";
	const char *colon = strstr(s, ": ");
	size_t n = colon ? (size_t)(colon - s) : strlen(s);
	bool dotted = false;
	int dots = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '.') dots++;
		if (s[i] == ' ' || s[i] == '\t' || s[i] == ':') {
			dots = -100;
			break;
		}
	}
	dotted = dots >= 2;
	if (dotted) {
		*name = asuka_strndup(s, n);
		*msg = asuka_strdup(colon ? colon + 2 : "");
	} else {
		*msg = asuka_strdup(s);
	}
}

typedef struct {
	bl_reply done;
	void *ctx;
} call_ctx;

static void call_finished(GObject *src, GAsyncResult *res, gpointer p) {
	call_ctx *c = p;
	GError *e = NULL;
	GVariant *r = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	char *name = NULL, *msg = NULL;
	if (!r) bl_error(e, &name, &msg);
	if (e) G(g_error_free)(e);
	c->done(c->ctx, r, name, msg);
	if (r) G(g_variant_unref)(r);
	free(name);
	free(msg);
	free(c);
	bl_flush();
}

void bl_call(const char *dest, const char *path, const char *iface, const char *method, GVariant *params, int timeout, bl_reply done, void *ctx) {
	call_ctx *c = malloc(sizeof *c);
	*c = (call_ctx){done, ctx};
	G(g_dbus_connection_call)(bl.conn, dest, path, iface, method, params, NULL, G_DBUS_CALL_FLAGS_NONE, timeout, NULL, call_finished, c);
}

static void ignore_reply(void *ctx, GVariant *r, const char *name, const char *msg) {}

void bl_call_bluez(const char *path, const char *iface, const char *method, GVariant *params, bl_reply done, void *ctx) {
	bl_call(BL_BLUEZ, path, iface, method, params, BL_CALL_TIMEOUT, done ? done : ignore_reply, ctx);
}

// ---- Errors as the engine words them ----

static const char *common(const char *name, const char *msg, char **owned) {
	if (name) {
		if (!strcmp(name, BL_ERR_SERVICE_UNKNOWN) || !strcmp(name, BL_ERR_NAME_HAS_NO_OWNER)) return BT_UNSUPPORTED;
		if (!strcmp(name, BL_ERR_ACCESS_DENIED)) return BT_DENIED;
		if (!strcmp(name, "org.bluez.Error.NotReady")) return BT_OFF;
		if (!strcmp(name, BL_ERR_NO_REPLY) || !strcmp(name, BL_ERR_TIMEOUT) || !strcmp(name, BL_ERR_TIMED_OUT)) return BT_TIMEOUT;
		return *owned = asuka_strdup(msg && *msg ? msg : name);
	}
	if (msg && (strstr(msg, "Timeout was reached") || strstr(msg, "timed out"))) return BT_TIMEOUT;
	if (!msg || !*msg) return "failed";
	return *owned = asuka_strdup(msg);
}

char *bl_bus_state(const char *name, const char *msg) {
	if (name && (!strcmp(name, BL_ERR_SERVICE_UNKNOWN) || !strcmp(name, BL_ERR_NAME_HAS_NO_OWNER))) return asuka_strdup(BT_UNSUPPORTED);
	if (name && !strcmp(name, BL_ERR_ACCESS_DENIED)) return asuka_strdup(BT_DENIED);
	if (!name && msg && strstr(msg, "AccessDenied")) return asuka_strdup(BT_DENIED);
	return NULL;
}

char *bl_client_error(const char *name, const char *msg, bool write) {
	int att = 0;
	if (!msg) msg = "";
	if (name) {
		if (!strcmp(name, "org.bluez.Error.NotPermitted")) {
			if (!strncmp(msg, "Read not permitted", 18)) att = 0x02;
			else if (!strncmp(msg, "Write not permitted", 19)) att = 0x03;
			else if (!strncmp(msg, "Not paired", 10)) att = 0x05;
			else att = write ? 0x03 : 0x02;
		} else if (!strcmp(name, "org.bluez.Error.NotAuthorized")) {
			att = 0x08;
		} else if (!strcmp(name, "org.bluez.Error.InvalidOffset")) {
			att = 0x07;
		} else if (!strcmp(name, "org.bluez.Error.InvalidValueLength")) {
			att = 0x0d;
		} else if (!strcmp(name, "org.bluez.Error.InvalidArguments")) {
			if (!strncmp(msg, "Invalid offset", 14)) att = 0x07;
			else if (!strncmp(msg, "Invalid Length", 14) || !strncmp(msg, "Invalid length", 14)) att = 0x0d;
		} else if (!strcmp(name, "org.bluez.Error.NotSupported")) {
			att = 0x06;
		} else if (!strcmp(name, "org.bluez.Error.NotConnected")) {
			return asuka_strdup(BT_DISCONNECTED);
		} else if (!strcmp(name, "org.bluez.Error.Failed")) {
			const char *a = strstr(msg, "ATT error: ");
			if (a) {
				unsigned long n = strtoul(a + strlen("ATT error: "), NULL, 0);
				if (n > 0 && n < 256) att = (int)n;
			}
			if (!att && !strncmp(msg, "Not connected", 13)) return asuka_strdup(BT_DISCONNECTED);
		} else if (!strcmp(name, BL_ERR_UNKNOWN_OBJECT) || !strcmp(name, BL_ERR_UNKNOWN_METHOD)) {
			return asuka_strdup(BT_DISCONNECTED);
		}
	}
	if (att) return asuka_sprintf("att %d: %s", att, *msg ? msg : "refused");
	char *owned = NULL;
	const char *w = common(name, msg, &owned);
	return owned ? owned : asuka_strdup(w);
}

static char *connect_error(const char *name, const char *msg) {
	if (name && (!strcmp(name, "org.bluez.Error.Failed") || !strcmp(name, "org.bluez.Error.ConnectionAttemptFailed")) && msg) {
		if (!strcmp(msg, "le-connection-abort-by-local") || !strcmp(msg, "br-connection-page-timeout") || !strcmp(msg, "Page Timeout") ||
		    !strcmp(msg, "Connection timed out"))
			return asuka_strdup(BT_TIMEOUT);
		if (!strcmp(msg, "br-connection-canceled") || !strcmp(msg, "le-connection-canceled") || !strcmp(msg, "Connection canceled"))
			return asuka_strdup(BT_CANCELED);
	}
	if (name && (!strcmp(name, "org.bluez.Error.DoesNotExist") || !strcmp(name, BL_ERR_UNKNOWN_OBJECT) || !strcmp(name, BL_ERR_UNKNOWN_METHOD)))
		return asuka_strdup(BT_NOT_FOUND);
	char *owned = NULL;
	const char *w = common(name, msg, &owned);
	return owned ? owned : asuka_strdup(w);
}

char *bl_connect_error(const char *name, const char *msg) { return connect_error(name, msg); }

bool bl_retry(const char *name, const char *msg) {
	if (!name) return msg && strstr(msg, "Timeout was reached");
	static const char *again[] = {"org.bluez.Error.Failed", "org.bluez.Error.ConnectionAttemptFailed", "org.bluez.Error.NotAvailable", "org.bluez.Error.InProgress",
	                              BL_ERR_NO_REPLY, BL_ERR_TIMEOUT, BL_ERR_TIMED_OUT};
	for (size_t i = 0; i < sizeof again / sizeof *again; i++)
		if (!strcmp(name, again[i])) return !msg || !strstr(msg, "canceled");
	return false;
}

const char *bl_reason(const char *name) {
	if (!name || !*name || !strcmp(name, "org.bluez.Reason.Unknown")) return BT_REASON_UNKNOWN;
	if (!strcmp(name, "org.bluez.Reason.Timeout")) return BT_REASON_TIMEOUT;
	if (!strcmp(name, "org.bluez.Reason.Remote")) return BT_REASON_REMOTE;
	if (!strcmp(name, "org.bluez.Reason.Local") || !strcmp(name, "org.bluez.Reason.Suspend")) return BT_REASON_LOCAL;
	return BT_REASON_FAILED;  // Authentication, and any BlueZ adds
}

int bl_properties(GVariant *flags) {
	bt_strings f = bl_strings(flags);
	int p = 0;
	for (size_t i = 0; i < f.len; i++) {
		const char *s = f.data[i];
		if (!strcmp(s, "broadcast")) p |= BT_PROP_BROADCAST;
		else if (!strcmp(s, "read") || !strcmp(s, "encrypt-read") || !strcmp(s, "encrypt-authenticated-read") || !strcmp(s, "secure-read")) p |= BT_PROP_READ;
		else if (!strcmp(s, "write-without-response")) p |= BT_PROP_WRITE_NO_RESPONSE;
		else if (!strcmp(s, "write") || !strcmp(s, "encrypt-write") || !strcmp(s, "encrypt-authenticated-write") || !strcmp(s, "secure-write")) p |= BT_PROP_WRITE;
		else if (!strcmp(s, "notify")) p |= BT_PROP_NOTIFY;
		else if (!strcmp(s, "indicate")) p |= BT_PROP_INDICATE;
		else if (!strcmp(s, "authenticated-signed-writes")) p |= BT_PROP_SIGNED_WRITE;
		else if (!strcmp(s, "extended-properties") || !strcmp(s, "reliable-write") || !strcmp(s, "writable-auxiliaries")) p |= BT_PROP_EXTENDED;
	}
	bt_strings_free(&f);
	return p;
}

char *bl_device_path(const char *path) {
	const char *d = path ? strstr(path, "/dev_") : NULL;
	if (!d) return asuka_strdup("");
	const char *slash = strchr(d + 1, '/');
	return slash ? asuka_strndup(path, (size_t)(slash - path)) : asuka_strdup(path);
}

char *bl_address_from_path(const char *path) {
	const char *d = path ? strstr(path, "/dev_") : NULL;
	if (!d) return asuka_strdup("");
	d += 5;
	size_t n = strcspn(d, "/");
	if (n != 17) return asuka_strdup("");
	char *a = asuka_strndup(d, n);
	for (char *c = a; *c; c++)
		if (*c == '_') *c = ':';
	return a;
}

// ---- Devices, as BlueZ has them ----

static void entries_of(GVariant *v, bt_entries *out, bool uuids) {
	if (!v || (!bl_is(v, "a{sv}") && !bl_is(v, "a{qv}"))) return;
	bool company = bl_is(v, "a{qv}");
	gsize n = G(g_variant_n_children)(v);
	for (gsize i = 0; i < n; i++) {
		GVariant *e = G(g_variant_get_child_value)(v, i);
		GVariant *k = G(g_variant_get_child_value)(e, 0), *vv = G(g_variant_get_child_value)(e, 1), *in = G(g_variant_get_variant)(vv);
		char *key = company ? asuka_sprintf("%04x", G(g_variant_get_uint16)(k)) : uuids ? bt_normalize(G(g_variant_get_string)(k, NULL))
		                                                                                 : asuka_strdup(G(g_variant_get_string)(k, NULL));
		asuka_bytes b = bl_bytes(in);
		bt_entries_push(out, key, b.data, b.len);
		free(b.data);
		G(g_variant_unref)(in);
		G(g_variant_unref)(vv);
		G(g_variant_unref)(k);
		G(g_variant_unref)(e);
	}
}

bl_device bl_parse_device(bl_iface *p) {
	bl_device d = {0};
	d.adapter = bl_str(bl_prop(p, "Adapter"));
	d.address = bl_str(bl_prop(p, "Address"));
	d.name = bl_str(bl_prop(p, "Name"));
	d.alias = bl_str(bl_prop(p, "Alias"));
	d.has_rssi = bl_int(bl_prop(p, "RSSI"), &d.rssi);
	d.has_tx_power = bl_int(bl_prop(p, "TxPower"), &d.tx_power);
	bt_strings u = bl_strings(bl_prop(p, "UUIDs"));
	for (size_t i = 0; i < u.len; i++) bt_strings_push(&d.uuids, bt_normalize(u.data[i]));
	bt_strings_free(&u);
	entries_of(bl_prop(p, "ServiceData"), &d.service_data, true);
	entries_of(bl_prop(p, "ManufacturerData"), &d.manufacturer_data, false);
	d.connected = bl_bool(bl_prop(p, "Connected"));
	d.services_resolved = bl_bool(bl_prop(p, "ServicesResolved"));
	return d;
}

void bl_device_free(bl_device *d) {
	bt_strings_free(&d->uuids);
	bt_entries_free(&d->service_data);
	bt_entries_free(&d->manufacturer_data);
}

// bl_device_name is the device's name as the app sees it: its alias
// where the user gave it one (BlueZ's default alias is the address).
char *bl_device_name(const bl_device *d) {
	const char *addr = d->address ? d->address : "";
	char *dashed = asuka_strdup(addr);
	for (char *c = dashed; *c; c++)
		if (*c == ':') *c = '-';
	const char *n = d->alias && *d->alias && strcmp(d->alias, dashed) != 0 ? d->alias : d->name;
	free(dashed);
	return asuka_strdup(n ? n : "");
}

bool bl_matches(const bl_device *d, const bt_strings *services) {
	if (services->len == 0) return true;
	for (size_t i = 0; i < services->len; i++) {
		for (size_t j = 0; j < d->uuids.len; j++)
			if (!strcmp(d->uuids.data[j], services->data[i])) return true;
		for (size_t j = 0; j < d->service_data.len; j++)
			if (!strcmp(d->service_data.data[j].key, services->data[i])) return true;
	}
	return false;
}

int64_t bl_now_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bt_advertisement bl_advertisement_of(const bl_device *d) {
	bt_advertisement a = {.id = asuka_strdup(d->address ? d->address : ""), .name = bl_device_name(d), .rssi = d->rssi,
	                      .local_name = asuka_strdup(d->name ? d->name : ""), .has_tx_power = d->has_tx_power, .tx_power = d->tx_power,
	                      .connectable = true, .time_ms = bl_now_ms()};
	for (size_t i = 0; i < d->uuids.len; i++) bt_strings_push(&a.services, asuka_strdup(d->uuids.data[i]));
	for (size_t i = 0; i < d->service_data.len; i++)
		bt_entries_push(&a.service_data, asuka_strdup(d->service_data.data[i].key), d->service_data.data[i].value.data, d->service_data.data[i].value.len);
	for (size_t i = 0; i < d->manufacturer_data.len; i++)
		bt_entries_push(&a.manufacturer_data, asuka_strdup(d->manufacturer_data.data[i].key), d->manufacturer_data.data[i].value.data,
		                d->manufacturer_data.data[i].value.len);
	return a;
}

// ---- The state ----

// bl_adapter is the adapter the app uses: the first (hci0), NULL if none.
const char *bl_adapter(void) {
	const char *best = NULL;
	for (size_t i = 0; i < bl.nobjects; i++)
		if (find_iface(&bl.objects[i], BL_IF_ADAPTER, false) && (!best || strcmp(bl.objects[i].path, best) < 0)) best = bl.objects[i].path;
	return best;
}

const char *bl_current(void) {
	if (bl.err) return bl.err;
	const char *a = bl_adapter();
	if (!a) return BT_UNSUPPORTED;
	bl_iface *p = bl_props(a, BL_IF_ADAPTER);
	if (!bl_bool(bl_prop(p, "Powered"))) return BT_OFF;
	// BlueZ 5.66 on: turning off ("on-disabling") is off already.
	const char *ps = bl_str(bl_prop(p, "PowerState"));
	if (ps && *ps && strcmp(ps, "on") != 0) return BT_OFF;
	return BT_ON;
}

typedef struct {
	char *state;
} told_state;

static void run_state(void *p) {
	told_state *t = p;
	bt_on_state(t->state);
	free(t->state);
	free(t);
}

// bl_update_state tells a change of state, and ends what it ends: the
// scans and the connections when it's no longer on; the advertisements
// and the servers too when BlueZ (or the adapter) went.
void bl_update_state(void) {
	const char *s = bl_current();
	if (bl.state && !strcmp(s, bl.state)) return;
	bool told = bl.state != NULL;
	free(bl.state);
	bl.state = asuka_strdup(s);
	if (told) {
		told_state *t = malloc(sizeof *t);
		t->state = asuka_strdup(s);
		bl_defer(run_state, t);
	}
	if (strcmp(s, BT_ON) != 0) bl_central_off(s);
	if (!strcmp(s, BT_UNSUPPORTED) || !strcmp(s, BT_DENIED)) bl_peripheral_gone(s);
}

// ---- Following BlueZ ----

static void added(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer d) {
	if (!bl.loaded || !bl_is(params, "(oa{sa{sv}})")) return;
	GVariant *p = G(g_variant_get_child_value)(params, 0), *ifs = G(g_variant_get_child_value)(params, 1);
	const char *at = G(g_variant_get_string)(p, NULL);
	add_ifaces(at, ifs);
	if (has_iface(ifs, BL_IF_ADAPTER)) {
		bl_update_state();
	} else if (has_iface(ifs, BL_IF_DEVICE)) {
		GVariant *dev = G(g_variant_lookup_value)(ifs, BL_IF_DEVICE, NULL);
		bl_heard(at, dev, true);
		if (bl_bool(bl_get(at, BL_IF_DEVICE, "Connected"))) bl_device_connected(at);
		G(g_variant_unref)(dev);
	} else if (has_iface(ifs, BL_IF_GATT_SERVICE) || has_iface(ifs, BL_IF_GATT_CHAR) || has_iface(ifs, BL_IF_GATT_DESC)) {
		bl_gatt_changed(at);
	}
	G(g_variant_unref)(ifs);
	G(g_variant_unref)(p);
	bl_flush();
}

static void removed(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer d) {
	if (!bl.loaded || !bl_is(params, "(oas)")) return;
	GVariant *p = G(g_variant_get_child_value)(params, 0), *ifs = G(g_variant_get_child_value)(params, 1);
	char *at = asuka_strdup(G(g_variant_get_string)(p, NULL));
	bl_object *o = find_object(at, false);
	bool gatt = false, adapter = false, device = false;
	bt_strings names = bl_strings(ifs);
	for (size_t i = 0; o && i < names.len; i++) {
		bl_iface *f = find_iface(o, names.data[i], false);
		if (!f) continue;
		const char *n = names.data[i];
		if (!strcmp(n, BL_IF_ADAPTER)) adapter = true;
		else if (!strcmp(n, BL_IF_DEVICE)) device = true;
		else if (!strcmp(n, BL_IF_GATT_SERVICE) || !strcmp(n, BL_IF_GATT_CHAR) || !strcmp(n, BL_IF_GATT_DESC)) gatt = true;
		free_iface(f);
		*f = o->ifaces[--o->nifaces];
	}
	bt_strings_free(&names);
	if (o && o->nifaces == 0) {
		free_object(o);
		*o = bl.objects[--bl.nobjects];
	}
	if (device) bl_device_disconnected(at);
	if (adapter) bl_update_state();
	if (gatt) bl_gatt_changed(at);
	free(at);
	G(g_variant_unref)(ifs);
	G(g_variant_unref)(p);
	bl_flush();
}

static void props_changed(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *signal_iface, const gchar *signal, GVariant *params,
                          gpointer d) {
	if (!bl.loaded || !bl_is(params, "(sa{sv}as)")) return;
	GVariant *i = G(g_variant_get_child_value)(params, 0), *changed = G(g_variant_get_child_value)(params, 1), *inv = G(g_variant_get_child_value)(params, 2);
	const char *iface = G(g_variant_get_string)(i, NULL);
	bl_iface *f = find_iface(find_object(path, true), iface, true);
	set_props(f, changed);
	bt_strings invalidated = bl_strings(inv);
	bool powered_gone = false;
	for (size_t k = 0; k < invalidated.len; k++) {
		if (!strcmp(invalidated.data[k], "Powered")) powered_gone = true;
		del_prop(f, invalidated.data[k]);
	}
	bt_strings_free(&invalidated);
	GVariant *v;
	if (!strcmp(iface, BL_IF_ADAPTER)) {
		GVariant *pw = G(g_variant_lookup_value)(changed, "Powered", NULL), *ps = G(g_variant_lookup_value)(changed, "PowerState", NULL);
		if (pw || ps || powered_gone) bl_update_state();
		if (pw) G(g_variant_unref)(pw);
		if (ps) G(g_variant_unref)(ps);
	} else if (!strcmp(iface, BL_IF_DEVICE)) {
		if ((v = G(g_variant_lookup_value)(changed, "Connected", NULL))) {
			if (bl_bool(v)) bl_device_connected(path);
			else bl_device_disconnected(path);
			G(g_variant_unref)(v);
		}
		if ((v = G(g_variant_lookup_value)(changed, "ServicesResolved", NULL))) {
			if (bl_bool(v)) bl_services_resolved(path);
			G(g_variant_unref)(v);
		}
		bl_heard(path, changed, false);
	} else if (!strcmp(iface, BL_IF_GATT_CHAR)) {
		if ((v = G(g_variant_lookup_value)(changed, "Value", NULL))) {
			asuka_bytes b = bl_bytes(v);
			bl_notified(path, b);
			free(b.data);
			G(g_variant_unref)(v);
		}
	}
	G(g_variant_unref)(inv);
	G(g_variant_unref)(changed);
	G(g_variant_unref)(i);
	bl_flush();
}

static void disconnected_signal(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params,
                                gpointer d) {
	if (!bl_is(params, "(ss)")) return;
	GVariant *r = G(g_variant_get_child_value)(params, 0), *m = G(g_variant_get_child_value)(params, 1);
	bl_disconnected_signal(path, G(g_variant_get_string)(r, NULL), G(g_variant_get_string)(m, NULL));
	G(g_variant_unref)(m);
	G(g_variant_unref)(r);
}

static void load(void);

// bluez_gone: bluetoothd left the bus; everything of it is gone.
static void bluez_gone(void) {
	if (!bl.loaded) return;
	clear_objects();
	free(bl.err_owned);
	bl.err = bl.err_owned = asuka_strdup(BT_UNSUPPORTED);
	bl_update_state();
}

static void owner_changed(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer d) {
	if (!bl_is(params, "(sss)")) return;
	GVariant *n = G(g_variant_get_child_value)(params, 0), *o = G(g_variant_get_child_value)(params, 2);
	if (!strcmp(G(g_variant_get_string)(n, NULL), BL_BLUEZ)) {
		if (!*G(g_variant_get_string)(o, NULL)) {
			bluez_gone();
		} else if (!bl.loading && bl.started) {  // bluetoothd started (again)
			bl.loaded = false;
			bl.loading = true;
			load();
		}
	}
	G(g_variant_unref)(o);
	G(g_variant_unref)(n);
	bl_flush();
}

typedef struct ready_fn {
	void (*fn)(void *);
	void *ctx;
	struct ready_fn *next;
} ready_fn;

static ready_fn *readies;

static void load_done(void) {
	bl.loading = false, bl.loaded = true;
	bl_update_state();
	ready_fn *r = readies;
	readies = NULL;
	// In the order asked.
	ready_fn *rev = NULL;
	while (r) {
		ready_fn *n = r->next;
		r->next = rev;
		rev = r;
		r = n;
	}
	while (rev) {
		ready_fn *n = rev->next;
		rev->fn(rev->ctx);
		free(rev);
		rev = n;
	}
}

static void loaded(void *ctx, GVariant *r, const char *name, const char *msg) {
	clear_objects();
	free(bl.err_owned);
	bl.err = bl.err_owned = NULL;
	if (!r) {
		bl.err = bl.err_owned = bl_bus_state(name, msg);
		if (!bl.err) bl.err = bl.err_owned = asuka_strdup(BT_UNSUPPORTED);
	} else if (bl_is(r, "(a{oa{sa{sv}}})")) {
		GVariant *objs = G(g_variant_get_child_value)(r, 0);
		GVariantIter it;
		G(g_variant_iter_init)(&it, objs);
		GVariant *e;
		while ((e = G(g_variant_iter_next_value)(&it))) {
			GVariant *p = G(g_variant_get_child_value)(e, 0), *ifs = G(g_variant_get_child_value)(e, 1);
			add_ifaces(G(g_variant_get_string)(p, NULL), ifs);
			G(g_variant_unref)(ifs);
			G(g_variant_unref)(p);
			G(g_variant_unref)(e);
		}
		G(g_variant_unref)(objs);
	}
	load_done();
}

static void load(void) {
	bl_call(BL_BLUEZ, "/", BL_IF_OBJECT_MANAGER, "GetManagedObjects", NULL, 10000, loaded, NULL);
}

// bus is the bus BlueZ is on: the system's (DBUS_SYSTEM_BUS_ADDRESS
// says where, as GLib reads it: a test's BlueZ's bus).
static GDBusConnection *bus(char **why) {
	GError *e = NULL;
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SYSTEM, &e);
	if (!c) {
		char *name = NULL, *msg = NULL;
		bl_error(e, &name, &msg);
		*why = bl_bus_state(name, msg);
		free(name);
		free(msg);
		if (e) G(g_error_free)(e);
	}
	return c;
}

// start connects to BlueZ, once, then calls then (each call does it
// first: nothing's asked of BlueZ before the app uses Bluetooth).
void bl_start(void (*then)(void *ctx), void *ctx) {
	if (bl.loaded) {
		then(ctx);
		return;
	}
	ready_fn *r = malloc(sizeof *r);
	*r = (ready_fn){then, ctx, readies};
	readies = r;
	if (bl.loading) return;
	bl.loading = true;
	if (!bl.started) {
		char *why = NULL;
		GDBusConnection *c = load_gx() ? bus(&why) : NULL;
		if (!c) {
			// No system bus: no BlueZ (a container, a sandbox without it).
			free(bl.err_owned);
			bl.err = bl.err_owned = why ? why : asuka_strdup(BT_UNSUPPORTED);
			load_done();
			return;
		}
		bl.conn = c;
		bl.started = true;
		G(g_dbus_connection_signal_subscribe)(c, BL_BLUEZ, BL_IF_OBJECT_MANAGER, "InterfacesAdded", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, added, NULL, NULL);
		G(g_dbus_connection_signal_subscribe)(c, BL_BLUEZ, BL_IF_OBJECT_MANAGER, "InterfacesRemoved", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, removed, NULL, NULL);
		G(g_dbus_connection_signal_subscribe)(c, BL_BLUEZ, BL_IF_PROPERTIES, "PropertiesChanged", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, props_changed, NULL, NULL);
		// Device1's (and Bearer.LE1's) Disconnected(reason, message),
		// BlueZ 5.82 on: sent before Connected turns false.
		G(g_dbus_connection_signal_subscribe)(c, BL_BLUEZ, NULL, "Disconnected", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, disconnected_signal, NULL, NULL);
		G(g_dbus_connection_signal_subscribe)(c, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", NULL,
		                                      G_DBUS_SIGNAL_FLAGS_NONE, owner_changed, NULL, NULL);
	}
	load();
}

// ---- The engine: the adapter ----

const char *bt_engine_state(void) {
	if (bl.loaded) return bl_current();
	bl_start_quietly();
	return NULL;
}

static void nothing(void *ctx) {}

void bl_start_quietly(void) {
	if (!bl.loaded && !bl.loading) bl_start(nothing, NULL);
}

typedef struct {
	void (*fn)(void *);
	void *ctx;
} then_ctx;

static void run_then(void *p) {
	then_ctx *t = p;
	t->fn(t->ctx);
	free(t);
}

static void ready_deferred(void *p) {
	bl_defer(run_then, p);
	bl_flush();
}

void bt_engine_ready(void (*ready)(void *ctx), void *ctx) {
	then_ctx *t = malloc(sizeof *t);
	*t = (then_ctx){ready, ctx};
	bl_start(ready_deferred, t);
}

typedef struct {
	void (*done)(void *ctx, const char *state, const char *err);
	void *ctx;
} request_ctx;

static void run_request(void *p) {
	request_ctx *r = p;
	r->done(r->ctx, bl_current(), NULL);
	free(r);
}

static void request_ready(void *p) {
	bl_defer(run_request, p);
	bl_flush();
}

void bt_engine_request(void (*done)(void *ctx, const char *state, const char *err), void *ctx) {
	request_ctx *r = malloc(sizeof *r);
	*r = (request_ctx){done, ctx};
	bl_start(request_ready, r);  // no permission to ask for
}

void bt_engine_choose(const bluetooth_list_Filter *filters, bt_strings optional, void (*done)(void *ctx, const char *id, const char *name, const char *err),
                      void *ctx) {
	done(ctx, NULL, NULL, "not available: Linux has no device chooser for apps (scan instead)");
}

// ---- Timers ----

typedef struct {
	void (*fn)(void *);
	void *ctx;
} timer_ctx;

static gboolean fire(gpointer p) {
	timer_ctx *t = p;
	t->fn(t->ctx);
	bl_flush();
	return false;
}

uintptr_t bt_after(unsigned ms, void (*fn)(void *ctx), void *ctx) {
	timer_ctx *t = malloc(sizeof *t);
	*t = (timer_ctx){fn, ctx};
	return G(g_timeout_add_full)(G_PRIORITY_DEFAULT, ms, fire, t, free);
}

void bt_after_stop(uintptr_t id) {
	if (id) G(g_source_remove)((guint)id);
}
