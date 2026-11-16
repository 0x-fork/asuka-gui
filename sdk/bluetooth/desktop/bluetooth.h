// bluetooth.h: what platform/bluetooth's Windows and Linux C share. The
// platform's work is an engine's (bluetooth_linux.c: BlueZ over D-Bus;
// bluetooth_windows.c: WinRT's Windows.Devices.Bluetooth): calls that
// answer once, through a function they're given, and events, which it
// tells bluetooth.c (the package's Adapter) as they come. Everything is
// on the main thread. The engine names attributes by handles; UUIDs
// cross normalized (128 bits, lower case: bt_normalize).
#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "bluetooth.asuka.h"

// The engine's words: an adapter's state; a failure's (NULL: none) is
// one of these, or "not available: why", or the platform's words.
#define BT_ON "on"
#define BT_OFF "off"
#define BT_DENIED "denied"
#define BT_UNSUPPORTED "unsupported"
#define BT_UNKNOWN "unknown"
#define BT_CANCELED "canceled"
#define BT_DISCONNECTED "disconnected"
#define BT_NOT_FOUND "not found"
#define BT_TIMEOUT "timeout"

// A disconnection's reason.
#define BT_REASON_REMOTE "remote"
#define BT_REASON_TIMEOUT "timeout"
#define BT_REASON_FAILED "failed"
#define BT_REASON_OFF "off"
#define BT_REASON_LOCAL "local"
#define BT_REASON_UNKNOWN "unknown"

// A characteristic's properties, as ATT's bits.
enum {
	BT_PROP_BROADCAST = 0x01,
	BT_PROP_READ = 0x02,
	BT_PROP_WRITE_NO_RESPONSE = 0x04,
	BT_PROP_WRITE = 0x08,
	BT_PROP_NOTIFY = 0x10,
	BT_PROP_INDICATE = 0x20,
	BT_PROP_SIGNED_WRITE = 0x40,
	BT_PROP_EXTENDED = 0x80,
};

// A served characteristic's permissions.
enum { BT_PERM_READ = 0x01, BT_PERM_WRITE = 0x02, BT_PERM_READ_ENCRYPTED = 0x04, BT_PERM_WRITE_ENCRYPTED = 0x08 };

typedef struct {
	char *key;  // a service data's UUID (normalized), a manufacturer's company ID (four hex digits)
	asuka_bytes value;
} bt_entry;

typedef struct {
	bt_entry *data;
	size_t len;
} bt_entries;

typedef struct {
	char **data;
	size_t len;
} bt_strings;

// An advertisement heard: its device (its id an address, "AA:BB:...",
// or the platform's), what it said.
typedef struct {
	char *id, *name;
	int rssi;
	char *local_name;
	bt_strings services, solicited;
	bt_entries service_data, manufacturer_data;
	bool has_tx_power;
	int tx_power;
	bool connectable;
	int64_t time_ms;  // since 1970
} bt_advertisement;

typedef struct {
	uint32_t handle;
	char *uuid;
} bt_descriptor;

typedef struct {
	uint32_t handle;
	char *uuid;
	int properties;
	bt_descriptor *descriptors;
	size_t ndescriptors;
} bt_characteristic;

typedef struct {
	uint32_t handle;
	char *uuid;
	bool primary;
	bt_characteristic *characteristics;
	size_t ncharacteristics;
} bt_service;

typedef struct {
	bt_service *data;
	size_t len;
} bt_services;

// A served characteristic: its handle (the package's), a fixed value
// (has_value) or one each read asks the app for.
typedef struct {
	uint32_t handle;
	char *uuid;
	int properties, permissions;
	bool has_value;
	asuka_bytes value;
} bt_local_characteristic;

typedef struct {
	char *uuid;
	bool primary;
	bt_local_characteristic *characteristics;
	size_t ncharacteristics;
} bt_local_service;

typedef struct {
	char *local_name;
	bt_strings services;
	bt_entries service_data, manufacturer_data;
	bool connectable, include_tx_power;
} bt_advertising;

// A central's device: an address (00:00:00:00:00:00 when the platform
// doesn't say).
typedef struct {
	char *id, *name;
} bt_central;

// ---- The engine (each platform's) ----

typedef void (*bt_done)(void *ctx, const char *err);

// bt_engine_state is the adapter's state now (a BT_ word), NULL while
// it's not known yet: then ready runs once it is (and the events follow
// it), else at once.
const char *bt_engine_state(void);
void bt_engine_ready(void (*ready)(void *ctx), void *ctx);
// bt_engine_request asks for the app's Bluetooth access (the first
// time): the state then.
void bt_engine_request(void (*done)(void *ctx, const char *state, const char *err), void *ctx);

void bt_engine_scan(uint32_t id, bt_strings services, bool duplicates, bt_done done, void *ctx);
void bt_engine_stop_scan(uint32_t id);
void bt_engine_choose(const bluetooth_list_Filter *filters, bt_strings optional, void (*done)(void *ctx, const char *id, const char *name, const char *err),
                      void *ctx);
void bt_engine_connect(uint32_t conn, const char *device, bool auto_connect, bt_done done, void *ctx);
void bt_engine_disconnect(uint32_t conn);
void bt_engine_services(uint32_t conn, void (*done)(void *ctx, const bt_services *s, const char *err), void *ctx);
void bt_engine_mtu(uint32_t conn, void (*done)(void *ctx, int mtu, const char *err), void *ctx);
void bt_engine_rssi(uint32_t conn, void (*done)(void *ctx, int rssi, const char *err), void *ctx);
void bt_engine_read(uint32_t conn, uint32_t handle, void (*done)(void *ctx, asuka_bytes value, const char *err), void *ctx);
void bt_engine_write(uint32_t conn, uint32_t handle, asuka_bytes value, bool response, bt_done done, void *ctx);
void bt_engine_subscribe(uint32_t conn, uint32_t handle, bt_done done, void *ctx);
void bt_engine_unsubscribe(uint32_t conn, uint32_t handle);
void bt_engine_serve(uint32_t id, const bt_local_service *services, size_t n, bt_done done, void *ctx);
void bt_engine_unserve(uint32_t id);
// bt_engine_respond answers a read or a write a server got: the value
// read, or an ATT error (0: none).
void bt_engine_respond(uint32_t server, uint32_t request, asuka_bytes value, int error);
void bt_engine_notify(uint32_t server, uint32_t handle, asuka_bytes value);
void bt_engine_advertise(uint32_t id, const bt_advertising *a, bt_done done, void *ctx);
void bt_engine_stop_advertise(uint32_t id);

// bt_after runs fn(ctx) in ms milliseconds on the main thread: its id,
// for bt_after_stop (the engine's main loop's timers).
uintptr_t bt_after(unsigned ms, void (*fn)(void *ctx), void *ctx);
void bt_after_stop(uintptr_t id);

// ---- The events (bluetooth.c's), as the engine tells them ----

void bt_on_state(const char *state);
void bt_on_advertisement(uint32_t scan, const bt_advertisement *a);
void bt_on_scan_stopped(uint32_t scan, const char *err);
// bt_on_disconnected: an established connection went (reason a
// BT_REASON_, err the platform's words or NULL).
void bt_on_disconnected(uint32_t conn, const char *reason, const char *err);
void bt_on_value(uint32_t conn, uint32_t handle, asuka_bytes value);
void bt_on_services_changed(uint32_t conn);
// bt_on_read and bt_on_write: a central's; request 0 is a write nobody
// answers (without response).
void bt_on_read(uint32_t server, uint32_t request, uint32_t handle, const bt_central *central, int offset);
void bt_on_write(uint32_t server, uint32_t request, uint32_t handle, const bt_central *central, asuka_bytes value, int offset);
void bt_on_subscription(uint32_t server, uint32_t handle, const bt_central *central, bool on);
void bt_on_advertise_stopped(uint32_t id, const char *err);
void bt_on_server_stopped(uint32_t server, const char *err);

// ---- Shared ----

// bt_normalize is a UUID as the engine has it (128 bits, lower case:
// "180d" is "0000180d-0000-1000-8000-00805f9b34fb"), of malloc's; what
// isn't one is a copy.
char *bt_normalize(const char *s);
// bt_short is a UUID as the package writes it: a SIG one's four hex
// digits, another's 128 bits; of malloc's.
char *bt_short(const char *s);
// bt_parse_uuid reads one into 16 bytes (big-endian): false if it isn't.
bool bt_parse_uuid(const char *s, uint8_t u[16]);

void bt_strings_free(bt_strings *s);
void bt_entries_free(bt_entries *e);
void bt_advertisement_free(bt_advertisement *a);
void bt_services_free(bt_services *s);
void bt_strings_push(bt_strings *s, char *v);
void bt_entries_push(bt_entries *e, char *key, const void *p, size_t n);

#endif
