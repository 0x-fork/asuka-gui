// bluetooth_linux.h: what the BlueZ engine's files share
// (bluetooth_linux.c: GIO, BlueZ's objects, the state; central_linux.c:
// scans, connections, attributes; peripheral_linux.c: advertisements
// and servers). Main thread.
#ifndef BLUETOOTH_LINUX_H
#define BLUETOOTH_LINUX_H

#include "asuka_linux.h"
#include "bluetooth.h"

#define BL_BLUEZ "org.bluez"
#define BL_IF_ADAPTER "org.bluez.Adapter1"
#define BL_IF_DEVICE "org.bluez.Device1"
#define BL_IF_GATT_SERVICE "org.bluez.GattService1"
#define BL_IF_GATT_CHAR "org.bluez.GattCharacteristic1"
#define BL_IF_GATT_DESC "org.bluez.GattDescriptor1"
#define BL_IF_ADV_MANAGER "org.bluez.LEAdvertisingManager1"
#define BL_IF_GATT_MANAGER "org.bluez.GattManager1"
#define BL_IF_ADVERT "org.bluez.LEAdvertisement1"
#define BL_IF_PROPERTIES "org.freedesktop.DBus.Properties"
#define BL_IF_OBJECT_MANAGER "org.freedesktop.DBus.ObjectManager"

#define BL_ERR_SERVICE_UNKNOWN "org.freedesktop.DBus.Error.ServiceUnknown"
#define BL_ERR_NAME_HAS_NO_OWNER "org.freedesktop.DBus.Error.NameHasNoOwner"
#define BL_ERR_ACCESS_DENIED "org.freedesktop.DBus.Error.AccessDenied"
#define BL_ERR_UNKNOWN_OBJECT "org.freedesktop.DBus.Error.UnknownObject"
#define BL_ERR_UNKNOWN_METHOD "org.freedesktop.DBus.Error.UnknownMethod"
#define BL_ERR_NO_REPLY "org.freedesktop.DBus.Error.NoReply"
#define BL_ERR_TIMEOUT "org.freedesktop.DBus.Error.Timeout"
#define BL_ERR_TIMED_OUT "org.freedesktop.DBus.Error.TimedOut"

// A call's timeout outlasts BlueZ's own (ATT's 30 seconds), which
// answers first; a connection's has none but BlueZ's (G_MAXINT).
#define BL_CALL_TIMEOUT 35000
#define BL_NO_TIMEOUT 0x7fffffff

// GIO's functions the package calls besides asuka_linux.h's.
typedef struct _GDBusNodeInfo GDBusNodeInfo;
typedef struct _GDBusInterfaceInfo GDBusInterfaceInfo;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct {
	void (*method_call)(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params,
	                    GDBusMethodInvocation *inv, gpointer data);
	GVariant *(*get_property)(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GError **err,
	                          gpointer data);
	gboolean (*set_property)(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GVariant *value,
	                         GError **err, gpointer data);
	gpointer padding[8];
} GDBusInterfaceVTable;

struct gx {
	GDBusNodeInfo *(*node_info_new_for_xml)(const gchar *xml, GError **e);
	GDBusInterfaceInfo *(*node_info_lookup_interface)(GDBusNodeInfo *n, const gchar *name);
	guint (*register_object)(GDBusConnection *c, const gchar *path, GDBusInterfaceInfo *iface, const GDBusInterfaceVTable *vt, gpointer data,
	                         GDestroyNotify free, GError **e);
	gboolean (*unregister_object)(GDBusConnection *c, guint id);
	gboolean (*emit_signal)(GDBusConnection *c, const gchar *dest, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, GError **e);
	void (*return_value)(GDBusMethodInvocation *inv, GVariant *v);
	void (*return_dbus_error)(GDBusMethodInvocation *inv, const gchar *name, const gchar *msg);
	GVariant *(*variant_ref)(GVariant *v);
	GVariant *(*variant_new_uint16)(guint16 v);
	GVariant *(*variant_new_int16)(gint16 v);
	GVariant *(*variant_new_byte)(guchar v);
};
extern struct gx gx;

// BlueZ's objects: their interfaces' properties (references).
typedef struct {
	char *name;
	GVariant *value;
} bl_prop_t;

typedef struct {
	char *name;
	bl_prop_t *props;
	size_t nprops;
} bl_iface;

typedef struct {
	char *path;
	bl_iface *ifaces;
	size_t nifaces;
} bl_object;

// The engine's state.
struct bl {
	bool started, loading, loaded;
	const char *err;  // why BlueZ can't be used: unsupported, denied (NULL: it can)
	char *err_owned;
	GDBusConnection *conn;
	bl_object *objects;
	size_t nobjects;
	char *state;  // the state last told (NULL: none yet)
};
extern struct bl bl;

// The queue of what bluetooth.c is told (bluetooth_linux.c).
void bl_defer(void (*run)(void *ctx), void *ctx);
void bl_flush(void);
void bl_answer(bt_done done, void *ctx, const char *err);

void bl_start(void (*then)(void *ctx), void *ctx);
void bl_start_quietly(void);
const char *bl_current(void);
const char *bl_adapter(void);
void bl_update_state(void);

// Values.
GVariant *bl_ref(GVariant *v);
bool bl_is(GVariant *v, const char *type);
const char *bl_str(GVariant *v);
bool bl_bool(GVariant *v);
bool bl_int(GVariant *v, int *out);
asuka_bytes bl_bytes(GVariant *v);
bt_strings bl_strings(GVariant *v);
GVariant *bl_byte_array(const void *p, size_t n);
GVariant *bl_string_array(const char *const *s, size_t n);
GVariant *bl_empty_dict(void);
GVariant *bl_option(GVariant *dict, const char *key);

// BlueZ's objects.
bl_iface *bl_props(const char *path, const char *iface);
GVariant *bl_prop(bl_iface *f, const char *name);
GVariant *bl_get(const char *path, const char *iface, const char *name);
bt_strings bl_paths(const char *iface, const char *prefix);

// D-Bus calls: done gets the reply (NULL on failure: the error's name,
// if it has one, and its message).
typedef void (*bl_reply)(void *ctx, GVariant *reply, const char *name, const char *msg);
void bl_call(const char *dest, const char *path, const char *iface, const char *method, GVariant *params, int timeout, bl_reply done, void *ctx);
void bl_call_bluez(const char *path, const char *iface, const char *method, GVariant *params, bl_reply done, void *ctx);
void bl_error(const GError *e, char **name, char **msg);

// BlueZ's errors, flags and reasons as the engine's words (of malloc's).
char *bl_bus_state(const char *name, const char *msg);
char *bl_client_error(const char *name, const char *msg, bool write);
char *bl_connect_error(const char *name, const char *msg);
bool bl_retry(const char *name, const char *msg);
const char *bl_reason(const char *name);
int bl_properties(GVariant *flags);
char *bl_device_path(const char *path);
char *bl_address_from_path(const char *path);

// A device, as BlueZ has it (its strings its properties').
typedef struct {
	const char *adapter, *address, *name, *alias;
	bool has_rssi, has_tx_power;
	int rssi, tx_power;
	bt_strings uuids;  // normalized
	bt_entries service_data, manufacturer_data;
	bool connected, services_resolved;
} bl_device;

bl_device bl_parse_device(bl_iface *p);
void bl_device_free(bl_device *d);
char *bl_device_name(const bl_device *d);
bool bl_matches(const bl_device *d, const bt_strings *services);
bt_advertisement bl_advertisement_of(const bl_device *d);
int64_t bl_now_ms(void);

// The central's (central_linux.c), as BlueZ tells.
void bl_central_off(const char *state);
void bl_heard(const char *path, GVariant *changed, bool added);
void bl_device_connected(const char *path);
void bl_device_disconnected(const char *path);
void bl_services_resolved(const char *path);
void bl_gatt_changed(const char *path);
void bl_notified(const char *path, asuka_bytes value);
void bl_disconnected_signal(const char *path, const char *reason, const char *words);

// The peripheral's (peripheral_linux.c).
void bl_peripheral_gone(const char *state);

#endif
