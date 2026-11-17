// platform/bluetooth on Windows: Windows.Devices.Bluetooth (WinRT).
//
//   - The state is the default adapter's radio's (Windows.Devices.Radios):
//     no adapter, or one without LE: unsupported; a request asks for the
//     radio's access, which an unpackaged app has.
//   - One advertisement watcher (active, so scan responses come, with the
//     names) serves every scan; a device's advertisement and its scan
//     response, which Windows delivers apart, are put together (util.c),
//     and a scan without duplicates gets the device once it has both (or
//     once it's clear no response comes).
//   - A connection is the device's GattSession with MaintainConnection,
//     which is what holds one on Windows: discovering the services
//     (uncached) connects. Windows doesn't say why a link went: a
//     disconnection's reason is "off" with the radio off, else unknown.
//   - Handles are the attributes' ATT handles.
//   - The chooser is the system's device picker (DevicePicker), which
//     filters by name only.
//   - As a peripheral, each served service is a GattServiceProvider,
//     published (connectable, not discoverable) as it's served;
//     advertising a service makes its provider discoverable, with the
//     service's data. Windows lets an app's own advertisement (the
//     publisher) have only manufacturer data: no name, no service UUIDs.
//
// The IIDs and the vtables' slots are Windows SDK metadata's, as
// microsoft/windows-rs generates them: an interface's own slots start at
// 6, after IInspectable's. WinRT calls back on its own threads: what it
// says is read there, then posted to the main thread, where everything
// else is; objects are found again there by their ids.
#include "asuka_windows.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- IIDs and signatures ----

#define TYPED_HANDLER "{9de1c534-6ae1-11e0-84e1-18a905bcc53f}"
#define VECTOR_VIEW "{bbe1fa4c-b0e3-4583-baef-1f1b2e483e56}"
#define INSPECTABLE "cinterface(IInspectable)"
#define NS_BT "Windows.Devices.Bluetooth."
#define NS_ADV "Windows.Devices.Bluetooth.Advertisement."
#define NS_GATT "Windows.Devices.Bluetooth.GenericAttributeProfile."
#define RC(name, iid) "rc(" name ";{" iid "})"
#define COMPLETED(result) "pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" result ")"
#define HANDLER(sender, args) "pinterface(" TYPED_HANDLER ";" sender ";" args ")"
#define VECTOR_OF(of) "pinterface(" VECTOR_VIEW ";" of ")"

#define SIG_RADIO RC("Windows.Devices.Radios.Radio", "252118df-b33e-416a-875f-1cf38ae2d83e")
#define SIG_ADAPTER RC(NS_BT "BluetoothAdapter", "7974f04c-5f7a-4a34-9225-a855f84b1a8b")
#define SIG_LE_DEVICE RC(NS_BT "BluetoothLEDevice", "b5ee2f7b-4ad8-4642-ac48-80a0b500e887")
#define SIG_WATCHER RC(NS_ADV "BluetoothLEAdvertisementWatcher", "a6ac336f-f3d3-4297-8d6c-c81ea6623f40")
#define SIG_PUBLISHER RC(NS_ADV "BluetoothLEAdvertisementPublisher", "cde820f9-d9fa-43d6-a264-ddd8b7da8b78")
#define SIG_SESSION RC(NS_GATT "GattSession", "d23b5143-e04e-4c24-999c-9c256f9856b1")
#define SIG_CHAR RC(NS_GATT "GattCharacteristic", "59cb50c1-5934-4f68-a198-eb864fa44e6b")
#define SIG_PROVIDER RC(NS_GATT "GattServiceProvider", "7822b3cd-2889-4f86-a051-3f0aed1c2760")
#define SIG_LOCAL_CHAR RC(NS_GATT "GattLocalCharacteristic", "aede376d-5412-4d74-92a8-8deb8526829c")
#define SIG_DEVICE_INFO RC("Windows.Devices.Enumeration.DeviceInformation", "aba0fb95-4398-489d-8e44-e6130927011f")
#define SIG_NOTIFY_RESULT RC(NS_GATT "GattClientNotificationResult", "506d5599-0112-419a-8e3b-ae21afabd2c2")

static const char *const handler_sigs[] = {
#define H_RADIO_ACCESS 0
    COMPLETED("enum(Windows.Devices.Radios.RadioAccessStatus;i4)"),
#define H_ADAPTER 1
    COMPLETED(SIG_ADAPTER),
#define H_RADIO 2
    COMPLETED(SIG_RADIO),
#define H_LE_DEVICE 3
    COMPLETED(SIG_LE_DEVICE),
#define H_SESSION 4
    COMPLETED(SIG_SESSION),
#define H_SERVICES 5
    COMPLETED(RC(NS_GATT "GattDeviceServicesResult", "171dd3ee-016d-419d-838a-576cf475a3d8")),
#define H_CHARS 6
    COMPLETED(RC(NS_GATT "GattCharacteristicsResult", "1194945c-b257-4f3e-9db7-f68bc9a9aef2")),
#define H_DESCS 7
    COMPLETED(RC(NS_GATT "GattDescriptorsResult", "9bc091f3-95e7-4489-8d25-ff81955a57b9")),
#define H_READ 8
    COMPLETED(RC(NS_GATT "GattReadResult", "63a66f08-1aea-4c4c-a50f-97bae474b348")),
#define H_WRITE 9
    COMPLETED(RC(NS_GATT "GattWriteResult", "4991ddb1-cb2b-44f7-99fc-d29a2871dc9b")),
#define H_PROVIDER 10
    COMPLETED(RC(NS_GATT "GattServiceProviderResult", "764696d8-c53e-428c-8a48-67afe02c3ae6")),
#define H_LOCAL_CHAR 11
    COMPLETED(RC(NS_GATT "GattLocalCharacteristicResult", "7975de9b-0170-4397-9666-92f863f12ee6")),
#define H_READ_REQUEST 12
    COMPLETED(RC(NS_GATT "GattReadRequest", "f1dd6535-6acd-42a6-a4bb-d789dae0043e")),
#define H_WRITE_REQUEST 13
    COMPLETED(RC(NS_GATT "GattWriteRequest", "aeb6a9ed-de2f-4fc2-a9a8-94ea7844f13d")),
#define H_NOTIFY_ALL 14
    COMPLETED(VECTOR_OF(SIG_NOTIFY_RESULT)),
#define H_DEVICE_INFO 15
    COMPLETED(SIG_DEVICE_INFO),
#define E_RADIO_STATE 16
    HANDLER(SIG_RADIO, INSPECTABLE),
#define E_RECEIVED 17
    HANDLER(SIG_WATCHER, RC(NS_ADV "BluetoothLEAdvertisementReceivedEventArgs", "27987ddf-e596-41be-8d43-9e6731d4a913")),
#define E_WATCHER_STOPPED 18
    HANDLER(SIG_WATCHER, RC(NS_ADV "BluetoothLEAdvertisementWatcherStoppedEventArgs", "dd40f84d-e7b9-43e3-9c04-0685d085fd8c")),
#define E_LE_DEVICE 19
    HANDLER(SIG_LE_DEVICE, INSPECTABLE),
#define E_VALUE_CHANGED 20
    HANDLER(SIG_CHAR, RC(NS_GATT "GattValueChangedEventArgs", "d21bdb54-06e3-4ed8-a263-acfac8ba7313")),
#define E_PUBLISHER_STATUS 21
    HANDLER(SIG_PUBLISHER, RC(NS_ADV "BluetoothLEAdvertisementPublisherStatusChangedEventArgs", "09c2bd9f-2dff-4b23-86ee-0d14fb94aeae")),
#define E_PROVIDER_STATUS 22
    HANDLER(SIG_PROVIDER, RC(NS_GATT "GattServiceProviderAdvertisementStatusChangedEventArgs", "59a5aa65-fa21-4ffc-b155-04d928012686")),
#define E_READ_REQUESTED 23
    HANDLER(SIG_LOCAL_CHAR, RC(NS_GATT "GattReadRequestedEventArgs", "93497243-f39c-484b-8ab6-996ba486cfa3")),
#define E_WRITE_REQUESTED 24
    HANDLER(SIG_LOCAL_CHAR, RC(NS_GATT "GattWriteRequestedEventArgs", "2dec8bbe-a73a-471a-94d5-037deadd0806")),
#define E_SUBSCRIBERS 25
    HANDLER(SIG_LOCAL_CHAR, INSPECTABLE),
};
#define NHANDLERS (sizeof handler_sigs / sizeof *handler_sigs)

static GUID handlers[NHANDLERS];

static const char *const iid_strings[] = {
#define I_ADAPTER_STATICS 0
    "8B02FB6A-AC4C-4741-8661-8EAB7D17EA9F",
#define I_RADIO_STATICS 1
    "5FB6A12E-67CB-46AE-AAE9-65919F86EFF4",
#define I_LE_DEVICE3 2
    "AEE9E493-44AC-40DC-AF33-B2C13C01CA46",
#define I_LE_DEVICE4 3
    "2B605031-2248-4B2F-ACF0-7CEE36FC5870",
#define I_LE_DEVICE_STATICS 4
    "C8CF1A19-F0B6-4BF0-8689-41303DE2D9F4",
#define I_LE_DEVICE_STATICS2 5
    "5F12C06B-3BAC-43E8-AD16-563271BD41C2",
#define I_ADVERTISEMENT 6
    "066FB2B7-33D1-4E7D-8367-CF81D0F79653",
#define I_PUBLISHER2 7
    "FBDB545E-56F1-510F-A434-217FBD9E7BD2",
#define I_PUBLISHER_FACTORY 8
    "5C5F065E-B863-4981-A1AF-1C544D8B0C0D",
#define I_RECEIVED_ARGS2 9
    "12D9C87B-0399-5F0E-A348-53B02B6B162E",
#define I_WATCHER 10
    "A6AC336F-F3D3-4297-8D6C-C81EA6623F40",
#define I_WATCHER2 11
    "01BF26BC-B164-5805-90A3-E8A7997FF225",
#define I_MANUFACTURER_FACTORY 12
    "C09B39F8-319A-441E-8DE5-66A81E877A6C",
#define I_CHAR3 13
    "3F3C663E-93D4-406B-B817-DB81F8ED53B3",
#define I_SERVICE3 14
    "B293A950-0C53-437C-A9B3-5C3210C6E569",
#define I_LOCAL_CHAR_PARAMS 15
    "FAF73DB4-4CFF-44C7-8445-040E6EAD0063",
#define I_READ_RESULT2 16
    "A10F50A0-FB43-48AF-BAAA-638A5C6329FE",
#define I_PROVIDER2 17
    "9EF531A9-CF12-59A3-A81C-362F4AABAACF",
#define I_ADVERTISING_PARAMS 18
    "E2CE31AB-6315-4C22-9BD7-781DBC3D8D82",
#define I_ADVERTISING_PARAMS2 19
    "FF68468D-CA92-4434-9743-0E90988AD879",
#define I_PROVIDER_STATICS 20
    "31794063-5256-4054-A4F4-7BBE7755A57E",
#define I_SESSION_STATICS 21
    "2E65B95C-539F-4DB7-82A8-73BDBBF73EBF",
#define I_DEVICE_PICKER 22
    "84997AA2-034A-4440-8813-7D0BD479BF5A",
#define I_CLOSABLE 23
    "30D5A829-7FA4-4026-83BB-D75BAE4EA99E",
#define I_BUFFER_FACTORY 24
    "71AF914D-C10F-484B-BC50-14BC623B3A27",
#define I_BUFFER_BYTES 25
    "905A0FEF-BC53-11DF-8C49-001E4FC686DA",
#define I_INITIALIZE_WITH_WINDOW 26
    "3E68D4BD-7135-4D10-8018-9FB6D9F33FA1",
};
#define NIIDS (sizeof iid_strings / sizeof *iid_strings)

static GUID iids[NIIDS];

static void init_iids(void) {
	static bool done;
	if (done) return;
	done = true;
	for (size_t i = 0; i < NHANDLERS; i++) handlers[i] = asuka_winrt_iid(handler_sigs[i]);
	for (size_t i = 0; i < NIIDS; i++) iids[i] = asuka_guid(iid_strings[i]);
}

// ---- COM and WinRT ----

typedef HRESULT(STDMETHODCALLTYPE *get_obj)(void *, void **);
typedef HRESULT(STDMETHODCALLTYPE *get_i32)(void *, INT32 *);
typedef HRESULT(STDMETHODCALLTYPE *get_u32)(void *, UINT32 *);
typedef HRESULT(STDMETHODCALLTYPE *get_u16)(void *, UINT16 *);
typedef HRESULT(STDMETHODCALLTYPE *get_u8)(void *, BYTE *);
typedef HRESULT(STDMETHODCALLTYPE *get_u64)(void *, UINT64 *);
typedef HRESULT(STDMETHODCALLTYPE *get_guid)(void *, GUID *);
typedef HRESULT(STDMETHODCALLTYPE *get_hstring)(void *, HSTRING *);
typedef HRESULT(STDMETHODCALLTYPE *call0)(void *);
typedef HRESULT(STDMETHODCALLTYPE *set_i32)(void *, INT32);
typedef HRESULT(STDMETHODCALLTYPE *set_obj)(void *, void *);

static void *obj(void *o, int slot) {
	void *v = NULL;
	if (!o || FAILED(ASUKA_SLOT(o, slot, get_obj)(o, &v))) return NULL;
	return v;
}

static INT32 i32(void *o, int slot) {
	INT32 v = 0;
	if (o) ASUKA_SLOT(o, slot, get_i32)(o, &v);
	return v;
}

static UINT32 u32(void *o, int slot) {
	UINT32 v = 0;
	if (o) ASUKA_SLOT(o, slot, get_u32)(o, &v);
	return v;
}

static UINT16 u16(void *o, int slot) {
	UINT16 v = 0;
	if (o) ASUKA_SLOT(o, slot, get_u16)(o, &v);
	return v;
}

static bool flag(void *o, int slot) {
	BYTE v = 0;
	if (o) ASUKA_SLOT(o, slot, get_u8)(o, &v);
	return v != 0;
}

static char *string_of(void *o, int slot) {
	HSTRING s = NULL;
	if (!o || FAILED(ASUKA_SLOT(o, slot, get_hstring)(o, &s))) return asuka_strdup("");
	char *u = asuka_hstring_utf8(s);
	asuka_hstring_free(s);
	return u;
}

static char *uuid_of(void *o, int slot) {
	GUID g = {0};
	ASUKA_SLOT(o, slot, get_guid)(o, &g);
	char b[37];
	asuka_guid_string(&g, b);
	return asuka_strdup(b);
}

static void *query(void *o, int iid) { return o ? asuka_com_query(o, &iids[iid]) : NULL; }

static void *statics(const char *cls, int iid) {
	void *f = NULL;
	return SUCCEEDED(asuka_winrt_factory(cls, &iids[iid], &f)) ? f : NULL;
}

static void *activate(const char *cls, int iid) {
	void *o = NULL;
	if (FAILED(asuka_winrt_activate(cls, &o)) || !o) return NULL;
	void *x = query(o, iid);
	asuka_com_release(o);
	return x;
}

// close_ closes an object that's IClosable (a device, a session, a
// service: what holds the connection), and releases it.
static void close_(void *o) {
	if (!o) return;
	void *c = query(o, I_CLOSABLE);
	if (c) {
		ASUKA_SLOT(c, 6, call0)(c);
		asuka_com_release(c);
	}
	asuka_com_release(o);
}

static UINT32 vector_size(void *v) { return u32(v, 7); }

static void *vector_at(void *v, UINT32 i) {
	void *x = NULL;
	ASUKA_SLOT(v, 6, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32, void **))(v, i, &x);
	return x;
}

// buffer_bytes reads an IBuffer.
static asuka_bytes buffer_bytes(void *buf) {
	if (!buf) return (asuka_bytes){0};
	UINT32 n = u32(buf, 7);  // get_Length
	void *acc = query(buf, I_BUFFER_BYTES);
	uint8_t *p = NULL;
	if (acc) {
		ASUKA_SLOT(acc, 3, HRESULT(STDMETHODCALLTYPE *)(void *, uint8_t **))(acc, &p);
		asuka_com_release(acc);
	}
	return p ? asuka_bytes_copy(p, n) : asuka_bytes_copy("", 0);
}

static asuka_bytes buffer_of(void *o, int slot) {
	void *b = obj(o, slot);
	asuka_bytes v = buffer_bytes(b);
	asuka_com_release(b);
	return v;
}

// buffer makes an IBuffer of p (a reference), NULL if it can't.
static void *buffer(const void *p, size_t n) {
	void *bf = statics("Windows.Storage.Streams.Buffer", I_BUFFER_FACTORY), *buf = NULL;
	if (!bf) return NULL;
	ASUKA_SLOT(bf, 6, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32, void **))(bf, (UINT32)(n ? n : 1), &buf);
	asuka_com_release(bf);
	if (!buf) return NULL;
	void *acc = query(buf, I_BUFFER_BYTES);
	uint8_t *dst = NULL;
	if (acc) {
		ASUKA_SLOT(acc, 3, HRESULT(STDMETHODCALLTYPE *)(void *, uint8_t **))(acc, &dst);
		asuka_com_release(acc);
	}
	if (!dst) {
		asuka_com_release(buf);
		return NULL;
	}
	if (n) memcpy(dst, p, n);
	ASUKA_SLOT(buf, 8, set_i32)(buf, (INT32)n);  // put_Length
	return buf;
}

static GUID guid_of_uuid(const char *s) {
	uint8_t u[16] = {0};
	bt_parse_uuid(s, u);
	GUID g = {(DWORD)u[0] << 24 | (DWORD)u[1] << 16 | (DWORD)u[2] << 8 | u[3], (WORD)(u[4] << 8 | u[5]), (WORD)(u[6] << 8 | u[7])};
	memcpy(g.Data4, u + 8, 8);
	return g;
}

// ---- Posting to the main thread, awaiting operations, events ----

// An operation's result, on the main thread.
typedef struct {
	void *result;  // GetResults' object (a reference), NULL if none
	INT32 value;   // an enum result's
	HRESULT hr;    // why there's no result (E_ABORT: canceled)
} outcome;

typedef void (*await_fn)(void *ctx, outcome o);

typedef struct {
	await_fn fn;
	void *ctx;
	bool enumerated;
	outcome o;
} awaiting;

static void run_awaited(void *p) {
	awaiting *w = p;
	w->fn(w->ctx, w->o);
	free(w);
}

static void completed(void *p, void *op, void *status) {
	awaiting *w = p;
	awaiting *x = malloc(sizeof *x);
	*x = *w;
	int st = (int)(intptr_t)status;
	if (st == 1) {  // Completed
		HRESULT hr = w->enumerated ? ASUKA_SLOT(op, 8, get_i32)(op, &x->o.value) : ASUKA_SLOT(op, 8, get_obj)(op, &x->o.result);
		x->o.hr = FAILED(hr) ? hr : S_OK;
	} else if (st == 2) {  // Canceled
		x->o.hr = E_ABORT;
	} else {
		void *info = asuka_com_query(op, &asuka_IID_IAsyncInfo);
		HRESULT hr = E_FAIL;
		if (info) {
			ASUKA_SLOT(info, 8, HRESULT(STDMETHODCALLTYPE *)(void *, HRESULT *))(info, &hr);  // get_ErrorCode
			asuka_com_release(info);
		}
		x->o.hr = FAILED(hr) ? hr : E_FAIL;
	}
	asuka_desktop_post(run_awaited, x);
}

// await sets op's Completed handler and lets op go: fn gets its outcome
// on the main thread (later, never at once).
static void await(void *op, int handler, bool enumerated, await_fn fn, void *ctx) {
	awaiting *w = calloc(1, sizeof *w);
	*w = (awaiting){fn, ctx, enumerated};
	void *h = asuka_winrt_delegate(&handlers[handler], completed, w, free);
	HRESULT hr = h ? ASUKA_SLOT(op, 6, set_obj)(op, h) : E_OUTOFMEMORY;  // put_Completed
	if (h) asuka_com_release(h);
	if (FAILED(hr)) {
		awaiting *x = malloc(sizeof *x);
		*x = (awaiting){fn, ctx, enumerated, {NULL, 0, hr}};
		asuka_desktop_post(run_awaited, x);
		if (!h) free(w);
	}
	asuka_com_release(op);
}

// started is a method's operation (its out pointer last), awaited; a
// method that fails answers fn with its HRESULT.
static void started(HRESULT hr, void *op, int handler, await_fn fn, void *ctx) {
	if (FAILED(hr) || !op) {
		awaiting *x = malloc(sizeof *x);
		*x = (awaiting){fn, ctx, false, {NULL, 0, FAILED(hr) ? hr : E_FAIL}};
		asuka_desktop_post(run_awaited, x);
		return;
	}
	await(op, handler, false, fn, ctx);
}

// event adds a handler to obj's event whose add method is slot (remove:
// slot+1): fn(ctx, sender, args) on WinRT's threads; its token.
static INT64 event(void *o, int slot, int handler, void (*fn)(void *, void *, void *), void *ctx, void (*free_ctx)(void *)) {
	void *h = asuka_winrt_delegate(&handlers[handler], fn, ctx, free_ctx);
	INT64 token = 0;
	if (h) {
		if (FAILED(ASUKA_SLOT(o, slot, HRESULT(STDMETHODCALLTYPE *)(void *, void *, INT64 *))(o, h, &token))) token = 0;
		asuka_com_release(h);
	}
	return token;
}

static void unevent(void *o, int slot, INT64 *token) {
	if (o && *token) ASUKA_SLOT(o, slot + 1, HRESULT(STDMETHODCALLTYPE *)(void *, INT64))(o, *token);
	*token = 0;
}

// post_id runs fn(id) on the main thread: what WinRT's threads say of an
// object, found again there by its id.
typedef struct {
	void (*fn)(uint32_t id);
	uint32_t id;
} id_call;

static void run_id(void *p) {
	id_call *c = p;
	c->fn(c->id);
	free(c);
}

static void post_id(void (*fn)(uint32_t), uint32_t id) {
	id_call *c = malloc(sizeof *c);
	*c = (id_call){fn, id};
	asuka_desktop_post(run_id, c);
}

static void *as_id(uint32_t id) { return (void *)(uintptr_t)id; }
static uint32_t id_of(void *p) { return (uint32_t)(uintptr_t)p; }

// A GATT result's words: its status (slot), its ATT error (an
// IReference<UInt8> at proto, -1 for none).
static char *gatt_result(outcome o, int status_slot, int proto_slot) {
	if (!o.result) return btw_gatt_error(-1, -1, (uint32_t)(o.hr == E_ABORT ? 0 : o.hr));
	int proto = -1;
	if (proto_slot >= 0) {
		void *r = obj(o.result, proto_slot);
		if (r) {
			BYTE b = 0;
			ASUKA_SLOT(r, 6, get_u8)(r, &b);
			proto = b;
			asuka_com_release(r);
		}
	}
	return btw_gatt_error(i32(o.result, status_slot), proto, 0);
}

static char *why(HRESULT hr) {
	if (hr == E_ABORT) return asuka_strdup(BT_CANCELED);
	return btw_gatt_error(-1, -1, (uint32_t)hr);
}

// ---- The engine's state ----

typedef struct heard {
	uint64_t address;
	btw_heard h;
	struct heard *next;
} heard;

typedef struct scan {
	uint32_t id;
	bt_strings services;
	bool duplicates;
	uint64_t *seen;
	size_t nseen;
	struct scan *next;
} scan;

typedef struct ch {
	void *obj;
	uint32_t handle;
	int props;
	INT64 token;  // ValueChanged's
	struct ch *next;
} ch;

typedef struct pending {
	int id;
	int kind;  // 0 bt_done, 1 bytes, 2 services
	void *done, *ctx;
	struct pending *next;
} pending;

typedef struct conn {
	uint32_t id;
	void *dev, *session, *priority;
	INT64 status_token, changed_token;
	bool up, closed, auto_connect;
	bt_done connected;  // while connecting
	void *ctx;
	void **services;
	size_t nservices;
	ch *chars;
	bt_services tree;
	bool has_tree, stale, discovering;
	int changes;
	pending *pending;  // calls waiting on it
	int next_pending;
	uint32_t *subscribed;
	size_t nsubscribed;
	struct conn *next;
} conn;

typedef struct client {
	void *obj;
	char *central;
} client;

typedef struct local {
	void *obj;  // GattLocalCharacteristic
	uint32_t handle;
	INT64 read_token, write_token, subs_token;
	bt_strings subscribers;
	client *clients;
	size_t nclients;
	struct server *server;
	struct local *next;
} local;

typedef struct provider {
	void *p, *svc;
	INT64 token;
	char *uuid;
	struct server *server;
	uint32_t advert;  // the advertisement it's discoverable for (0: none)
	struct provider *next;
} provider;

typedef struct server {
	uint32_t id;
	provider *providers;
	local *chars;
	bool ready, closed;
	bt_done done;  // until it's served
	void *ctx;
	struct server *next;
} server;

typedef struct request {
	uint32_t id, server;
	void *req, *deferral;
	bool write;
	struct request *next;
} request;

typedef struct advert {
	uint32_t id;
	void *publisher;
	INT64 token;
	bt_strings services;
	bt_entries service_data;
	bool connectable;
	bt_done done;  // until it's advertising
	void *ctx;
	int waiting;  // the parts not started yet
	struct advert *next;
} advert;

static struct {
	void *adapter, *radio;
	bool low_energy, peripheral, known, loading, denied;
	INT64 radio_token;
	const char *state;  // the last told
	void *watcher;
	INT64 received_token, stopped_token;
	scan *scans;
	heard *heard;
	conn *conns;
	server *servers;
	request *requests;
	uint32_t next_request;
	advert *adverts;
	struct ready {
		void (*fn)(void *);
		void *ctx;
		struct ready *next;
	} * readies;
} bw;

static const char *current(void) {
	if (!bw.adapter || !bw.low_energy) return BT_UNSUPPORTED;
	if (bw.denied) return BT_DENIED;
	return btw_radio_state(bw.radio ? i32(bw.radio, 9) : 0);  // IRadio.get_State
}

// ---- The adapter ----

static void end_scans(const char *why_);
static void lost(conn *c, const char *reason);
static void end_conn(conn *c, const char *why_);

static void radio_changed(uint32_t unused) {
	const char *s = current();
	if (bw.state && !strcmp(s, bw.state)) return;
	bw.state = s;
	bt_on_state(s);
	if (strcmp(s, BT_ON) != 0) {
		// As the other platforms do: the scans and the connections end.
		end_scans(s);
		for (conn *c = bw.conns, *next; c; c = next) {
			next = c->next;
			if (c->up) lost(c, BT_REASON_OFF);
			else end_conn(c, s);
		}
	}
}

static void on_radio(void *ctx, void *sender, void *args) { post_id(radio_changed, 0); }

static void loaded(void) {
	bw.known = true, bw.loading = false;
	if (!bw.state) bw.state = current();
	struct ready *r = bw.readies, *rev = NULL;
	bw.readies = NULL;
	while (r) {
		struct ready *n = r->next;
		r->next = rev;
		rev = r;
		r = n;
	}
	while (rev) {
		struct ready *n = rev->next;
		rev->fn(rev->ctx);
		free(rev);
		rev = n;
	}
}

static void radio_got(void *ctx, outcome o) {
	bw.radio = o.result;
	if (bw.radio) bw.radio_token = event(bw.radio, 7, E_RADIO_STATE, on_radio, NULL, NULL);  // add_StateChanged
	loaded();
}

static void adapter_got(void *ctx, outcome o) {
	if (!o.result) {
		loaded();
		return;
	}
	bw.adapter = o.result;
	bw.low_energy = flag(bw.adapter, 9), bw.peripheral = flag(bw.adapter, 10);  // IsLowEnergySupported, IsPeripheralRoleSupported
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(bw.adapter, 13, get_obj)(bw.adapter, &op);  // GetRadioAsync
	started(hr, op, H_RADIO, radio_got, NULL);
}

// with_adapter calls fn once the adapter's known (bw.adapter NULL: none).
static void with_adapter(void (*fn)(void *), void *ctx) {
	if (bw.known) {
		fn(ctx);
		return;
	}
	struct ready *r = malloc(sizeof *r);
	*r = (struct ready){fn, ctx, bw.readies};
	bw.readies = r;
	if (bw.loading) return;
	bw.loading = true;
	init_iids();
	void *st = asuka_winrt() ? statics(NS_BT "BluetoothAdapter", I_ADAPTER_STATICS) : NULL, *op = NULL;
	if (!st) {
		loaded();
		return;
	}
	HRESULT hr = ASUKA_SLOT(st, 8, get_obj)(st, &op);  // GetDefaultAsync
	asuka_com_release(st);
	started(hr, op, H_ADAPTER, adapter_got, NULL);
}

static void nothing(void *ctx) {}

const char *bt_engine_state(void) {
	if (bw.known) return current();
	with_adapter(nothing, NULL);  // start knowing
	return NULL;
}

void bt_engine_ready(void (*ready)(void *ctx), void *ctx) { with_adapter(ready, ctx); }

typedef struct {
	void (*done)(void *ctx, const char *state, const char *err);
	void *ctx;
} request_ctx;

static void access_got(void *p, outcome o) {
	request_ctx *r = p;
	bw.denied = o.value == 2 || o.value == 3;  // DeniedByUser, DeniedBySystem
	radio_changed(0);
	r->done(r->ctx, current(), NULL);
	free(r);
}

static void request_now(void *p) {
	request_ctx *r = p;
	void *st = bw.adapter && bw.low_energy ? statics("Windows.Devices.Radios.Radio", I_RADIO_STATICS) : NULL, *op = NULL;
	if (!st) {
		r->done(r->ctx, current(), NULL);
		free(r);
		return;
	}
	HRESULT hr = ASUKA_SLOT(st, 9, get_obj)(st, &op);  // RequestAccessAsync
	asuka_com_release(st);
	if (FAILED(hr) || !op) {
		r->done(r->ctx, current(), NULL);
		free(r);
		return;
	}
	await(op, H_RADIO_ACCESS, true, access_got, r);
}

void bt_engine_request(void (*done)(void *ctx, const char *state, const char *err), void *ctx) {
	request_ctx *r = malloc(sizeof *r);
	*r = (request_ctx){done, ctx};
	with_adapter(request_now, r);
}

// ---- Timers ----

typedef struct {
	void (*fn)(void *);
	void *ctx;
	void (*free_ctx)(void *);
	HANDLE timer;
	volatile LONG stopped;
} timer_ctx;

static void run_timer(void *p) {
	timer_ctx *t = p;
	if (!t->stopped) t->fn(t->ctx);
	if (t->free_ctx) t->free_ctx(t->ctx);
	DeleteTimerQueueTimer(NULL, t->timer, NULL);
	free(t);
}

static VOID CALLBACK timer_fired(PVOID p, BOOLEAN fired) { asuka_desktop_post(run_timer, p); }

uintptr_t bt_after(unsigned ms, void (*fn)(void *ctx), void *ctx, void (*free_ctx)(void *ctx)) {
	timer_ctx *t = calloc(1, sizeof *t);
	t->fn = fn, t->ctx = ctx, t->free_ctx = free_ctx;
	if (!CreateTimerQueueTimer(&t->timer, NULL, timer_fired, t, ms, 0, WT_EXECUTEONLYONCE)) {
		if (free_ctx) free_ctx(ctx);
		free(t);
		return 0;
	}
	return (uintptr_t)t;
}

void bt_after_stop(uintptr_t id) {
	if (id) InterlockedExchange(&((timer_ctx *)id)->stopped, 1);  // it frees itself as it fires
}

// ---- Scanning ----

static void scan_free(scan *s) {
	bt_strings_free(&s->services);
	free(s->seen);
	free(s);
}

static void close_watcher(void) {
	if (!bw.watcher) return;
	unevent(bw.watcher, 19, &bw.received_token);
	unevent(bw.watcher, 21, &bw.stopped_token);
	ASUKA_SLOT(bw.watcher, 18, call0)(bw.watcher);  // Stop
	asuka_com_release(bw.watcher);
	bw.watcher = NULL;
	while (bw.heard) {
		heard *h = bw.heard;
		bw.heard = h->next;
		btw_heard_free(&h->h);
		free(h);
	}
}

static void end_scans(const char *why_) {
	while (bw.scans) {
		scan *s = bw.scans;
		bw.scans = s->next;
		bt_on_scan_stopped(s->id, why_);
		scan_free(s);
	}
	close_watcher();
}

// A packet heard, read on WinRT's thread.
typedef struct {
	uint64_t address;
	btw_packet p;
	void *watcher;
} received;

static void got_packet(void *p) {
	received *r = p;
	if (r->watcher != bw.watcher || !bw.watcher) {
		btw_ad_free(&r->p.ad);
		free(r);
		return;
	}
	heard *h = bw.heard;
	while (h && h->address != r->address) h = h->next;
	if (!h) {
		h = calloc(1, sizeof *h);
		h->address = r->address;
		h->h.connectable = -1;
		h->next = bw.heard;
		bw.heard = h;
	}
	btw_take(&h->h, &r->p);
	char *id = btw_address(r->address);
	bt_advertisement a = btw_advertisement(&h->h, id);
	free(id);
	for (scan *s = bw.scans; s; s = s->next) {
		bool seen = false;
		for (size_t i = 0; i < s->nseen; i++) seen = seen || s->seen[i] == r->address;
		if (!s->duplicates && (seen || !btw_complete(&h->h))) continue;
		if (!btw_matches(&a, &s->services)) continue;
		if (!seen) {
			s->seen = realloc(s->seen, (s->nseen + 1) * sizeof *s->seen);
			s->seen[s->nseen++] = r->address;
		}
		bt_on_advertisement(s->id, &a);
	}
	bt_advertisement_free(&a);
	free(r);
}

static void on_received(void *watcher, void *sender, void *args) {
	received *r = calloc(1, sizeof *r);
	r->watcher = watcher;
	r->p.type = i32(args, 8);
	INT16 rssi = 0;
	ASUKA_SLOT(args, 6, HRESULT(STDMETHODCALLTYPE *)(void *, INT16 *))(args, &rssi);  // RawSignalStrengthInDBm
	r->p.rssi = rssi;
	ASUKA_SLOT(args, 7, get_u64)(args, &r->address);  // BluetoothAddress
	INT64 ts = 0;
	ASUKA_SLOT(args, 9, HRESULT(STDMETHODCALLTYPE *)(void *, INT64 *))(args, &ts);  // Timestamp: 100 ns since 1601
	r->p.time_ms = ts / 10000 - 11644473600000LL;
	r->p.response = r->p.type == BTW_ADV_SCAN_RESPONSE;
	r->p.scannable = r->p.type == BTW_ADV_CONNECTABLE_UNDIRECTED || r->p.type == BTW_ADV_SCANNABLE_UNDIRECTED;
	r->p.connectable = -1;
	bool has_tx = false;
	int tx = 0;
	void *a2 = query(args, I_RECEIVED_ARGS2);
	if (a2) {
		void *ref = obj(a2, 7);  // TransmitPowerLevelInDBm: IReference<Int16>
		if (ref) {
			INT16 v = 0;
			ASUKA_SLOT(ref, 6, HRESULT(STDMETHODCALLTYPE *)(void *, INT16 *))(ref, &v);
			asuka_com_release(ref);
			has_tx = true, tx = v;
		}
		r->p.connectable = flag(a2, 9);   // IsConnectable
		r->p.scannable = flag(a2, 10);    // IsScannable
		r->p.response = flag(a2, 12);     // IsScanResponse
		asuka_com_release(a2);
	}
	char *local_name = NULL;
	btw_section *secs = NULL;
	asuka_bytes *datas = NULL;
	size_t nsecs = 0;
	void *ad = obj(args, 10);  // Advertisement
	if (ad) {
		local_name = string_of(ad, 8);  // LocalName
		void *list = obj(ad, 12);       // DataSections
		UINT32 n = list ? vector_size(list) : 0;
		secs = calloc(n ? n : 1, sizeof *secs);
		datas = calloc(n ? n : 1, sizeof *datas);
		for (UINT32 i = 0; i < n; i++) {
			void *s = vector_at(list, i);
			if (!s) continue;
			BYTE type = 0;
			ASUKA_SLOT(s, 6, get_u8)(s, &type);
			datas[nsecs] = buffer_of(s, 8);
			secs[nsecs] = (btw_section){type, datas[nsecs].data, datas[nsecs].len};
			nsecs++;
			asuka_com_release(s);
		}
		asuka_com_release(list);
		asuka_com_release(ad);
	}
	r->p.ad = btw_parse_sections(secs, nsecs);
	for (size_t i = 0; i < nsecs; i++) free(datas[i].data);
	free(secs);
	free(datas);
	if (!r->p.ad.name && local_name && *local_name) r->p.ad.name = asuka_strdup(local_name);
	free(local_name);
	if (!r->p.ad.has_tx_power && has_tx) r->p.ad.has_tx_power = true, r->p.ad.tx_power = tx;
	asuka_desktop_post(got_packet, r);
}

typedef struct {
	void *watcher;
	INT32 error;
} stopped_ctx;

static void watcher_stopped(void *p) {
	stopped_ctx *s = p;
	if (s->watcher == bw.watcher && bw.watcher) {
		const char *w = btw_error(s->error), *st = current();
		if (strcmp(st, BT_ON) != 0) w = st;
		else if (!w) w = "failed: Windows stopped the scan";
		end_scans(w);
	}
	free(s);
}

static void on_watcher_stopped(void *watcher, void *sender, void *args) {
	stopped_ctx *s = malloc(sizeof *s);
	*s = (stopped_ctx){watcher, i32(args, 6)};  // Error
	asuka_desktop_post(watcher_stopped, s);
}

typedef struct {
	uint32_t id;
	bt_strings services;
	bool duplicates;
	bt_done done;
	void *ctx;
} scan_args;

static void scan_now(void *p) {
	scan_args *a = p;
	const char *st = current();
	if (strcmp(st, BT_ON) != 0) {
		a->done(a->ctx, st);
		bt_strings_free(&a->services);
		free(a);
		return;
	}
	scan *s = calloc(1, sizeof *s);
	*s = (scan){.id = a->id, .services = a->services, .duplicates = a->duplicates, .next = bw.scans};
	bw.scans = s;
	char *err = NULL;
	if (!bw.watcher) {
		void *w = activate(NS_ADV "BluetoothLEAdvertisementWatcher", I_WATCHER);
		if (w) {
			ASUKA_SLOT(w, 12, set_i32)(w, 1);  // put_ScanningMode: Active
			void *w2 = query(w, I_WATCHER2);
			if (w2) {
				ASUKA_SLOT(w2, 7, set_i32)(w2, 1);  // put_AllowExtendedAdvertisements
				asuka_com_release(w2);
			}
			bw.watcher = w;
			bw.received_token = event(w, 19, E_RECEIVED, on_received, w, NULL);        // add_Received
			bw.stopped_token = event(w, 21, E_WATCHER_STOPPED, on_watcher_stopped, w, NULL);  // add_Stopped
			HRESULT hr = ASUKA_SLOT(w, 17, call0)(w);                                    // Start
			if (FAILED(hr)) {
				close_watcher();
				err = why(hr);
			}
		} else {
			err = asuka_strdup("not available: no advertisement watcher");
		}
	}
	if (err) {
		bw.scans = s->next;
		scan_free(s);
	}
	a->done(a->ctx, err);
	free(err);
	free(a);
}

void bt_engine_scan(uint32_t id, bt_strings services, bool duplicates, bt_done done, void *ctx) {
	scan_args *a = malloc(sizeof *a);
	*a = (scan_args){id, {0}, duplicates, done, ctx};
	for (size_t i = 0; i < services.len; i++) bt_strings_push(&a->services, bt_normalize(services.data[i]));
	with_adapter(scan_now, a);
}

void bt_engine_stop_scan(uint32_t id) {
	for (scan **p = &bw.scans; *p; p = &(*p)->next)
		if ((*p)->id == id) {
			scan *s = *p;
			*p = s->next;
			scan_free(s);
			break;
		}
	if (!bw.scans) close_watcher();
}

// ---- The chooser ----

typedef struct {
	void (*done)(void *ctx, const char *id, const char *name, const char *err);
	void *ctx;
	bluetooth_list_Filter filters;
	void *picker;
} choose_ctx;

static void choose_free(choose_ctx *c) {
	for (size_t i = 0; i < c->filters.len; i++) {
		free(c->filters.data[i].name);
		free(c->filters.data[i].namePrefix);
	}
	free(c->filters.data);
	asuka_com_release(c->picker);
	free(c);
}

static void chosen_device(void *p, outcome o) {
	choose_ctx *c = p;
	if (!o.result) {
		c->done(c->ctx, NULL, NULL, BT_NOT_FOUND);
	} else {
		UINT64 a = 0;
		ASUKA_SLOT(o.result, 10, get_u64)(o.result, &a);  // BluetoothAddress
		char *id = btw_address(a), *name = string_of(o.result, 7);
		close_(o.result);
		c->done(c->ctx, id, name, NULL);
		free(id);
		free(name);
	}
	choose_free(c);
}

static void picked(void *p, outcome o) {
	choose_ctx *c = p;
	if (!o.result) {
		char *w = o.hr == E_ABORT || o.hr == S_OK ? asuka_strdup(BT_CANCELED) : why(o.hr);
		c->done(c->ctx, NULL, NULL, w);
		free(w);
		choose_free(c);
		return;
	}
	char *id = string_of(o.result, 6);  // DeviceInformation.Id
	asuka_com_release(o.result);
	void *st = statics(NS_BT "BluetoothLEDevice", I_LE_DEVICE_STATICS), *op = NULL;
	HSTRING h = asuka_hstring(id);
	free(id);
	HRESULT hr = st ? ASUKA_SLOT(st, 6, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING, void **))(st, h, &op) : E_FAIL;  // FromIdAsync
	asuka_hstring_free(h);
	asuka_com_release(st);
	started(hr, op, H_LE_DEVICE, chosen_device, c);
}

static char *selector(int slot, INT32 arg) {
	void *st = statics(NS_BT "BluetoothLEDevice", I_LE_DEVICE_STATICS2);
	HSTRING s = NULL;
	if (!st) return NULL;
	HRESULT hr = ASUKA_SLOT(st, slot, HRESULT(STDMETHODCALLTYPE *)(void *, INT32, HSTRING *))(st, arg, &s);  // GetDeviceSelectorFromPairingState
	asuka_com_release(st);
	if (FAILED(hr)) return NULL;
	char *u = asuka_hstring_utf8(s);
	asuka_hstring_free(s);
	return u;
}

typedef struct {
	float x, y, w, h;
} rect;

static void choose_now(void *p) {
	choose_ctx *c = p;
	const char *st = current();
	if (strcmp(st, BT_ON) != 0) {
		c->done(c->ctx, NULL, NULL, st);
		choose_free(c);
		return;
	}
	bt_strings bases = {0};
	for (int paired = 0; paired < 2; paired++) {
		char *s = selector(6, paired);
		if (s && *s) bt_strings_push(&bases, s);
		else free(s);
	}
	void *picker = bases.len ? activate("Windows.Devices.Enumeration.DevicePicker", I_DEVICE_PICKER) : NULL;
	if (!picker) {
		c->done(c->ctx, NULL, NULL, "not available: this Windows has no Bluetooth LE device picker");
		bt_strings_free(&bases);
		choose_free(c);
		return;
	}
	c->picker = picker;
	// The window the user just used: the picker's owner.
	HWND hwnd = GetForegroundWindow();
	void *iw = query(picker, I_INITIALIZE_WITH_WINDOW);
	if (iw) {
		ASUKA_SLOT(iw, 3, HRESULT(STDMETHODCALLTYPE *)(void *, HWND))(iw, hwnd);
		asuka_com_release(iw);
	}
	bt_strings sels = btw_chooser_selectors(&bases, &c->filters);
	bt_strings_free(&bases);
	void *f = obj(picker, 6);       // Filter
	void *list = f ? obj(f, 7) : NULL;  // SupportedDeviceSelectors: IVector<String>
	for (size_t i = 0; list && i < sels.len; i++) {
		HSTRING h = asuka_hstring(sels.data[i]);
		ASUKA_SLOT(list, 13, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING))(list, h);  // Append
		asuka_hstring_free(h);
	}
	asuka_com_release(list);
	asuka_com_release(f);
	bt_strings_free(&sels);
	// Beside the window's center (its DIPs): the picker places itself.
	RECT r = {0};
	GetClientRect(hwnd, &r);
	UINT dpi = 96;
	UINT(WINAPI * for_window)(HWND) = NULL;
	*(FARPROC *)&for_window = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	if (for_window && hwnd) dpi = for_window(hwnd);
	float k = dpi ? (float)dpi / 96 : 1;
	rect at = {(float)r.right / k / 2, (float)r.bottom / k / 3, 1, 1};
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(picker, 17, HRESULT(STDMETHODCALLTYPE *)(void *, rect, void **))(picker, at, &op);  // PickSingleDeviceAsync
	started(hr, op, H_DEVICE_INFO, picked, c);
}

void bt_engine_choose(const bluetooth_list_Filter *filters, bt_strings optional, void (*done)(void *ctx, const char *id, const char *name, const char *err),
                      void *ctx) {
	choose_ctx *c = calloc(1, sizeof *c);
	c->done = done, c->ctx = ctx;
	c->filters = (bluetooth_list_Filter){calloc(filters->len ? filters->len : 1, sizeof(bluetooth_Filter)), filters->len};
	for (size_t i = 0; i < filters->len; i++) {
		c->filters.data[i].name = asuka_strdup(filters->data[i].name);
		c->filters.data[i].namePrefix = asuka_strdup(filters->data[i].namePrefix);
	}
	with_adapter(choose_now, c);
}

// ---- Connections ----

static conn *conn_of(uint32_t id) {
	for (conn *c = bw.conns; c; c = c->next)
		if (c->id == id && !c->closed) return c;
	return NULL;
}

static ch *char_of(conn *c, uint32_t handle) {
	for (ch *x = c->chars; x; x = x->next)
		if (x->handle == handle) return x;
	return NULL;
}

static void fail_pending(pending *p, const char *why_) {
	if (p->kind == 1) ((void (*)(void *, asuka_bytes, const char *))p->done)(p->ctx, (asuka_bytes){0}, why_);
	else if (p->kind == 2) ((void (*)(void *, const bt_services *, const char *))p->done)(p->ctx, NULL, why_);
	else ((bt_done)p->done)(p->ctx, why_);
}

static void release_attrs(conn *c) {
	while (c->chars) {
		ch *x = c->chars;
		c->chars = x->next;
		unevent(x->obj, 20, &x->token);
		asuka_com_release(x->obj);
		free(x);
	}
	for (size_t i = 0; i < c->nservices; i++) close_(c->services[i]);
	free(c->services);
	c->services = NULL;
	c->nservices = 0;
	if (c->has_tree) bt_services_free(&c->tree);
	c->has_tree = false;
}

// end_conn closes a connection: a connect waiting fails with why; every
// other call waiting, disconnected.
static void end_conn(conn *c, const char *why_) {
	if (c->closed) return;
	c->closed = true;
	for (conn **p = &bw.conns; *p; p = &(*p)->next)
		if (*p == c) {
			*p = c->next;
			break;
		}
	if (c->connected) c->connected(c->ctx, why_);
	c->connected = NULL;
	while (c->pending) {
		pending *p = c->pending;
		c->pending = p->next;
		fail_pending(p, BT_DISCONNECTED);
		free(p);
	}
	release_attrs(c);
	free(c->subscribed);
	if (c->dev) {
		unevent(c->dev, 16, &c->status_token);
		unevent(c->dev, 14, &c->changed_token);
		close_(c->priority);
		close_(c->session);
		close_(c->dev);
	}
	free(c);
}

// lost ends a connection the device (or the radio) ended.
static void lost(conn *c, const char *reason) {
	uint32_t id = c->id;
	end_conn(c, BT_DISCONNECTED);
	bt_on_disconnected(id, reason, "the device disconnected");
}

static void connected_now(conn *c) {
	if (c->up || c->closed) return;
	c->up = true;
	bt_done d = c->connected;
	c->connected = NULL;
	if (d) d(c->ctx, NULL);
}

static void connection_changed(uint32_t id) {
	conn *c = conn_of(id);
	if (!c || !c->dev) return;
	bool connected = i32(c->dev, 9) == 1;  // ConnectionStatus: Connected
	if (!c->up && connected) connected_now(c);
	else if (c->up && !connected) lost(c, strcmp(current(), BT_ON) == 0 ? BT_REASON_UNKNOWN : BT_REASON_OFF);  // Windows doesn't say
}

static void services_changed(uint32_t id) {
	conn *c = conn_of(id);
	if (!c) return;
	c->changes++;
	if (!c->has_tree) return;  // nothing discovered yet: nothing to say
	c->stale = true;
	bt_on_services_changed(id);
}

static void on_connection(void *id, void *sender, void *args) { post_id(connection_changed, id_of(id)); }
static void on_services(void *id, void *sender, void *args) { post_id(services_changed, id_of(id)); }

// read_services reads a GattDeviceServicesResult's services.
static void read_services(void *r, void ***out, size_t *n) {
	void *list = obj(r, 8);  // Services
	UINT32 k = list ? vector_size(list) : 0;
	*out = calloc(k ? k : 1, sizeof **out);
	*n = 0;
	for (UINT32 i = 0; i < k; i++) {
		void *s = vector_at(list, i);
		if (s) (*out)[(*n)++] = s;
	}
	asuka_com_release(list);
}

static void connect_discovered(void *p, outcome o) {
	conn *c = conn_of(id_of(p));
	char *e = gatt_result(o, 6, 7);
	void **svcs = NULL;
	size_t n = 0;
	if (o.result) {
		read_services(o.result, &svcs, &n);
		asuka_com_release(o.result);
	}
	if (!c) {
		for (size_t i = 0; i < n; i++) close_(svcs[i]);
		free(svcs);
		free(e);
		return;
	}
	if (!e) {
		if (!c->services) c->services = svcs, c->nservices = n, svcs = NULL, n = 0;
		if (i32(c->dev, 9) == 1) connected_now(c);  // else ConnectionStatusChanged says
	} else if (!strcmp(e, BT_DISCONNECTED) && c->auto_connect) {
		// MaintainConnection connects once it's in range.
	} else if (!strcmp(e, BT_DISCONNECTED)) {
		end_conn(c, "timeout: the device didn't answer");
	} else {
		end_conn(c, e);
	}
	for (size_t i = 0; i < n; i++) close_(svcs[i]);
	free(svcs);
	free(e);
}

static void session_opened(void *p, outcome o) {
	conn *c = conn_of(id_of(p));
	if (!c) {
		close_(o.result);
		return;
	}
	if (!o.result) {
		char *w = why(o.hr);
		end_conn(c, w);
		free(w);
		return;
	}
	if (flag(o.result, 7)) ASUKA_SLOT(o.result, 8, set_i32)(o.result, 1);  // CanMaintainConnection: put_MaintainConnection
	c->session = o.result;
	// Discovering from the device connects to it.
	void *d3 = query(c->dev, I_LE_DEVICE3), *op = NULL;
	HRESULT hr = d3 ? ASUKA_SLOT(d3, 9, HRESULT(STDMETHODCALLTYPE *)(void *, INT32, void **))(d3, 1, &op) : E_NOINTERFACE;  // GetGattServicesWithCacheModeAsync: Uncached
	asuka_com_release(d3);
	started(hr, op, H_SERVICES, connect_discovered, p);
}

static void device_got(void *p, outcome o) {
	conn *c = conn_of(id_of(p));
	if (!c) {
		close_(o.result);
		return;
	}
	if (!o.result) {
		end_conn(c, BT_NOT_FOUND);
		return;
	}
	c->dev = o.result;
	c->status_token = event(c->dev, 16, E_LE_DEVICE, on_connection, p, NULL);  // add_ConnectionStatusChanged
	c->changed_token = event(c->dev, 14, E_LE_DEVICE, on_services, p, NULL);   // add_GattServicesChanged
	void *d4 = query(c->dev, I_LE_DEVICE4), *id = d4 ? obj(d4, 6) : NULL;      // BluetoothDeviceId
	asuka_com_release(d4);
	void *st = id ? statics(NS_GATT "GattSession", I_SESSION_STATICS) : NULL, *op = NULL;
	if (!st) {
		asuka_com_release(id);
		end_conn(c, "not available: this Windows has no GattSession (Windows 10 1803 and later)");
		return;
	}
	HRESULT hr = ASUKA_SLOT(st, 6, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(st, id, &op);  // FromDeviceIdAsync
	asuka_com_release(st);
	asuka_com_release(id);
	started(hr, op, H_SESSION, session_opened, p);
}

typedef struct {
	uint32_t conn;
	char *device;
	bool auto_connect;
	bt_done done;
	void *ctx;
} connect_args;

static void connect_now(void *p) {
	connect_args *a = p;
	const char *st = current();
	uint64_t addr;
	if (strcmp(st, BT_ON) != 0) {
		a->done(a->ctx, st);
	} else if (!btw_parse_address(a->device, &addr)) {
		a->done(a->ctx, BT_NOT_FOUND);
	} else {
		conn *old = conn_of(a->conn);
		if (old) end_conn(old, BT_CANCELED);
		conn *c = calloc(1, sizeof *c);
		*c = (conn){.id = a->conn, .auto_connect = a->auto_connect, .connected = a->done, .ctx = a->ctx, .next = bw.conns};
		bw.conns = c;
		void *ls = statics(NS_BT "BluetoothLEDevice", I_LE_DEVICE_STATICS), *op = NULL;
		HRESULT hr = ls ? ASUKA_SLOT(ls, 7, HRESULT(STDMETHODCALLTYPE *)(void *, UINT64, void **))(ls, addr, &op) : E_FAIL;  // FromBluetoothAddressAsync
		asuka_com_release(ls);
		started(hr, op, H_LE_DEVICE, device_got, as_id(c->id));
	}
	free(a->device);
	free(a);
}

void bt_engine_connect(uint32_t id, const char *device, bool auto_connect, bt_done done, void *ctx) {
	connect_args *a = malloc(sizeof *a);
	*a = (connect_args){id, asuka_strdup(device), auto_connect, done, ctx};
	with_adapter(connect_now, a);
}

void bt_engine_disconnect(uint32_t id) {
	conn *c = conn_of(id);
	if (c) end_conn(c, BT_CANCELED);
}

// ---- Discovery: services, then each one's characteristics and their
// descriptors, one at a time ----

typedef struct {
	uint32_t conn;
	int changes;
	void **svcs;
	size_t nsvcs, at;
	bt_services tree;
	ch *chars;
	void **char_objs;  // the service's characteristics being read
	size_t nchars, char_at;
} discovery;

static void discovery_free(discovery *d, bool keep_attrs) {
	if (!keep_attrs) {
		for (size_t i = 0; i < d->nsvcs; i++) close_(d->svcs[i]);
		while (d->chars) {
			ch *x = d->chars;
			d->chars = x->next;
			asuka_com_release(x->obj);
			free(x);
		}
		bt_services_free(&d->tree);
	}
	for (size_t i = d->char_at; i < d->nchars; i++) asuka_com_release(d->char_objs[i]);
	free(d->char_objs);
	free(d);
}

static void discovered(conn *c, const char *why_) {
	c->discovering = false;
	// The calls waiting for the services answer.
	for (pending **pp = &c->pending; *pp;) {
		pending *p = *pp;
		if (p->kind != 2) {
			pp = &p->next;
			continue;
		}
		*pp = p->next;
		((void (*)(void *, const bt_services *, const char *))p->done)(p->ctx, why_ ? NULL : &c->tree, why_);
		free(p);
	}
}

static void follow(conn *c, ch *x);
static void next_service(discovery *d);
static void chars_got(void *p, outcome o);

static void descs_got(void *p, outcome o) {
	discovery *d = p;
	conn *c = conn_of(d->conn);
	if (!c) {
		asuka_com_release(o.result);
		discovery_free(d, false);
		return;
	}
	bt_service *s = &d->tree.data[d->tree.len - 1];
	bt_characteristic *cs = &s->characteristics[s->ncharacteristics - 1];
	void *list = o.result ? obj(o.result, 8) : NULL;
	UINT32 n = list ? vector_size(list) : 0;
	cs->descriptors = calloc(n ? n : 1, sizeof *cs->descriptors);
	for (UINT32 i = 0; i < n; i++) {
		void *x = vector_at(list, i);
		if (!x) continue;
		cs->descriptors[cs->ndescriptors++] = (bt_descriptor){u16(x, 9), uuid_of(x, 8)};  // AttributeHandle, Uuid
		asuka_com_release(x);
	}
	asuka_com_release(list);
	asuka_com_release(o.result);
	d->char_at++;
	next_service(d);
}

// next_service reads the next characteristic's descriptors, or the
// next service's characteristics, or ends.
static void next_service(discovery *d) {
	conn *c = conn_of(d->conn);
	if (!c) {
		discovery_free(d, false);
		return;
	}
	if (d->char_objs && d->char_at < d->nchars) {
		void *x = d->char_objs[d->char_at];
		bt_service *s = &d->tree.data[d->tree.len - 1];
		ch *k = calloc(1, sizeof *k);
		*k = (ch){x, u16(x, 12), (int)(u32(x, 7) & 0xff), 0, d->chars};  // AttributeHandle, CharacteristicProperties
		d->chars = k;
		s->characteristics = realloc(s->characteristics, (s->ncharacteristics + 1) * sizeof *s->characteristics);
		s->characteristics[s->ncharacteristics++] = (bt_characteristic){k->handle, uuid_of(x, 11), k->props};
		void *c3 = query(x, I_CHAR3), *op = NULL;
		HRESULT hr = c3 ? ASUKA_SLOT(c3, 7, HRESULT(STDMETHODCALLTYPE *)(void *, INT32, void **))(c3, 1, &op) : E_NOINTERFACE;  // GetDescriptorsWithCacheModeAsync
		asuka_com_release(c3);
		d->char_objs[d->char_at] = NULL;  // the discovery's chars have it
		started(hr, op, H_DESCS, descs_got, d);
		return;
	}
	free(d->char_objs);
	d->char_objs = NULL;
	d->nchars = d->char_at = 0;
	if (d->at == d->nsvcs) {
		// Done: the attributes are the connection's now.
		release_attrs(c);
		c->services = d->svcs, c->nservices = d->nsvcs, c->chars = d->chars, c->tree = d->tree, c->has_tree = true;
		c->stale = c->changes != d->changes;
		for (size_t i = 0; i < c->nsubscribed;) {
			ch *x = char_of(c, c->subscribed[i]);
			if (x) {
				follow(c, x);
				i++;
			} else {
				c->subscribed[i] = c->subscribed[--c->nsubscribed];
			}
		}
		free(d);
		discovered(c, NULL);
		return;
	}
	void *svc = d->svcs[d->at++];
	d->tree.data = realloc(d->tree.data, (d->tree.len + 1) * sizeof *d->tree.data);
	d->tree.data[d->tree.len++] = (bt_service){u16(svc, 10), uuid_of(svc, 9), true};  // AttributeHandle, Uuid
	void *s3 = query(svc, I_SERVICE3), *op = NULL;
	HRESULT hr = s3 ? ASUKA_SLOT(s3, 12, HRESULT(STDMETHODCALLTYPE *)(void *, INT32, void **))(s3, 1, &op) : E_NOINTERFACE;  // GetCharacteristicsWithCacheModeAsync
	asuka_com_release(s3);
	started(hr, op, H_CHARS, chars_got, d);
}

// chars_got: a service's characteristics; their descriptors next. A
// service Windows doesn't let the app use (HID's) comes without them.
static void chars_got(void *p, outcome o) {
	discovery *d = p;
	conn *c = conn_of(d->conn);
	char *e = gatt_result(o, 6, 7);
	void *list = o.result ? obj(o.result, 8) : NULL;  // Characteristics
	asuka_com_release(o.result);
	if (!c || (e && !strcmp(e, BT_DISCONNECTED))) {
		asuka_com_release(list);
		if (c) {
			discovery_free(d, false);
			c->discovering = false;
			discovered(c, e);
		} else {
			discovery_free(d, false);
		}
		free(e);
		return;
	}
	free(e);
	UINT32 n = list ? vector_size(list) : 0;
	d->char_objs = calloc(n ? n : 1, sizeof *d->char_objs);
	d->nchars = d->char_at = 0;
	for (UINT32 i = 0; i < n; i++) {
		void *x = vector_at(list, i);
		if (x) d->char_objs[d->nchars++] = x;
	}
	asuka_com_release(list);
	next_service(d);
}

static void discover(conn *c, void **svcs, size_t n) {
	discovery *d = calloc(1, sizeof *d);
	*d = (discovery){.conn = c->id, .changes = c->changes, .svcs = svcs, .nsvcs = n};
	next_service(d);
}

static void services_got(void *p, outcome o) {
	conn *c = conn_of(id_of(p));
	char *e = gatt_result(o, 6, 7);
	void **svcs = NULL;
	size_t n = 0;
	if (o.result) {
		read_services(o.result, &svcs, &n);
		asuka_com_release(o.result);
	}
	if (!c || e) {
		for (size_t i = 0; i < n; i++) close_(svcs[i]);
		free(svcs);
		if (c) {
			c->discovering = false;
			discovered(c, e);
		}
		free(e);
		return;
	}
	discover(c, svcs, n);
}

static void add_pending(conn *c, int kind, void *done, void *ctx, int *id) {
	pending *p = malloc(sizeof *p);
	*p = (pending){++c->next_pending, kind, done, ctx, c->pending};
	c->pending = p;
	if (id) *id = p->id;
}

// take_pending takes a connection's call waiting: NULL if the connection
// ended (which answered it).
static pending *take_pending(uint32_t conn_id, int id) {
	conn *c = conn_of(conn_id);
	for (pending **pp = c ? &c->pending : NULL; pp && *pp; pp = &(*pp)->next)
		if ((*pp)->id == id) {
			pending *p = *pp;
			*pp = p->next;
			return p;
		}
	return NULL;
}

void bt_engine_services(uint32_t id, void (*done)(void *ctx, const bt_services *s, const char *err), void *ctx) {
	conn *c = conn_of(id);
	if (!c || !c->up) {
		done(ctx, NULL, BT_DISCONNECTED);
		return;
	}
	if (c->has_tree && !c->stale) {
		done(ctx, &c->tree, NULL);
		return;
	}
	add_pending(c, 2, (void *)done, ctx, NULL);
	if (c->discovering) return;
	c->discovering = true;
	if (!c->has_tree && c->services) {  // connecting discovered them, uncached
		void **svcs = c->services;
		size_t n = c->nservices;
		c->services = NULL, c->nservices = 0;
		discover(c, svcs, n);
		return;
	}
	void *d3 = query(c->dev, I_LE_DEVICE3), *op = NULL;
	HRESULT hr = d3 ? ASUKA_SLOT(d3, 9, HRESULT(STDMETHODCALLTYPE *)(void *, INT32, void **))(d3, 1, &op) : E_NOINTERFACE;
	asuka_com_release(d3);
	started(hr, op, H_SERVICES, services_got, as_id(id));
}

// ---- Attributes ----

typedef struct {
	uint32_t conn;
	int pending;
} op_ctx;

static op_ctx *op_new(conn *c, int kind, void *done, void *ctx) {
	op_ctx *o = malloc(sizeof *o);
	o->conn = c->id;
	add_pending(c, kind, done, ctx, &o->pending);
	return o;
}

static void read_got(void *p, outcome o) {
	op_ctx *x = p;
	pending *w = take_pending(x->conn, x->pending);
	char *e = gatt_result(o, 6, -1);
	asuka_bytes v = {0};
	if (o.result) {
		v = buffer_of(o.result, 7);  // Value
		void *r2 = query(o.result, I_READ_RESULT2);
		if (r2 && !e) {
			void *ref = obj(r2, 6);  // ProtocolError
			if (ref) {
				BYTE b = 0;
				ASUKA_SLOT(ref, 6, get_u8)(ref, &b);
				asuka_com_release(ref);
				free(e);
				e = btw_att(b);
			}
		}
		asuka_com_release(r2);
		asuka_com_release(o.result);
	}
	if (w) ((void (*)(void *, asuka_bytes, const char *))w->done)(w->ctx, v, e);
	free(v.data);
	free(e);
	free(w);
	free(x);
}

static void written(void *p, outcome o) {
	op_ctx *x = p;
	pending *w = take_pending(x->conn, x->pending);
	char *e = gatt_result(o, 6, 7);
	asuka_com_release(o.result);
	if (w) ((bt_done)w->done)(w->ctx, e);
	free(e);
	free(w);
	free(x);
}

void bt_engine_read(uint32_t id, uint32_t handle, void (*done)(void *ctx, asuka_bytes value, const char *err), void *ctx) {
	conn *c = conn_of(id);
	ch *x = c && c->up ? char_of(c, handle) : NULL;
	if (!x) {
		done(ctx, (asuka_bytes){0}, c && c->up ? BT_NOT_FOUND : BT_DISCONNECTED);
		return;
	}
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(x->obj, 15, HRESULT(STDMETHODCALLTYPE *)(void *, INT32, void **))(x->obj, 1, &op);  // ReadValueWithCacheModeAsync: Uncached
	started(hr, op, H_READ, read_got, op_new(c, 1, (void *)done, ctx));
}

void bt_engine_write(uint32_t id, uint32_t handle, asuka_bytes value, bool response, bt_done done, void *ctx) {
	conn *c = conn_of(id);
	ch *x = c && c->up ? char_of(c, handle) : NULL;
	if (!x) {
		done(ctx, c && c->up ? BT_NOT_FOUND : BT_DISCONNECTED);
		return;
	}
	void *c3 = query(x->obj, I_CHAR3), *buf = buffer(value.data, value.len), *op = NULL;
	HRESULT hr = c3 && buf ? ASUKA_SLOT(c3, 11, HRESULT(STDMETHODCALLTYPE *)(void *, void *, INT32, void **))(c3, buf, response ? 0 : 1, &op) : E_FAIL;  // WriteValueWithResultAndOptionAsync
	asuka_com_release(c3);
	asuka_com_release(buf);
	started(hr, op, H_WRITE, written, op_new(c, 0, (void *)done, ctx));
}

typedef struct {
	uint32_t conn, handle;
	asuka_bytes v;
} value_ctx;

static void got_value(void *p) {
	value_ctx *v = p;
	conn *c = conn_of(v->conn);
	if (c && char_of(c, v->handle)) bt_on_value(v->conn, v->handle, v->v);
	free(v->v.data);
	free(v);
}

static void on_value(void *p, void *sender, void *args) {
	value_ctx *k = p, *v = malloc(sizeof *v);
	*v = (value_ctx){k->conn, k->handle, buffer_of(args, 6)};  // CharacteristicValue
	asuka_desktop_post(got_value, v);
}

// follow sends the characteristic's notifications as values.
static void follow(conn *c, ch *x) {
	if (x->token) return;
	value_ctx *k = calloc(1, sizeof *k);
	k->conn = c->id, k->handle = x->handle;
	x->token = event(x->obj, 20, E_VALUE_CHANGED, on_value, k, free);  // add_ValueChanged
}

static bool subscribed_(conn *c, uint32_t h) {
	for (size_t i = 0; i < c->nsubscribed; i++)
		if (c->subscribed[i] == h) return true;
	return false;
}

typedef struct {
	op_ctx op;
	uint32_t handle;
} sub_ctx;

static void cccd_written(void *p, outcome o) {
	sub_ctx *x = p;
	pending *w = take_pending(x->op.conn, x->op.pending);
	char *e = gatt_result(o, 6, 7);
	asuka_com_release(o.result);
	conn *c = conn_of(x->op.conn);
	ch *k = c ? char_of(c, x->handle) : NULL;
	if (c && !e && !subscribed_(c, x->handle)) {
		c->subscribed = realloc(c->subscribed, (c->nsubscribed + 1) * sizeof *c->subscribed);
		c->subscribed[c->nsubscribed++] = x->handle;
	} else if (c && e && k && !subscribed_(c, x->handle)) {
		unevent(k->obj, 20, &k->token);
	}
	if (w) ((bt_done)w->done)(w->ctx, e);
	free(e);
	free(w);
	free(x);
}

static HRESULT write_cccd(ch *x, INT32 v, void **op) {
	void *c3 = query(x->obj, I_CHAR3);
	HRESULT hr = c3 ? ASUKA_SLOT(c3, 12, HRESULT(STDMETHODCALLTYPE *)(void *, INT32, void **))(c3, v, op) : E_NOINTERFACE;  // WriteClientCharacteristicConfigurationDescriptorWithResultAsync
	asuka_com_release(c3);
	return hr;
}

void bt_engine_subscribe(uint32_t id, uint32_t handle, bt_done done, void *ctx) {
	conn *c = conn_of(id);
	ch *x = c && c->up ? char_of(c, handle) : NULL;
	int v;
	if (!x) {
		done(ctx, c && c->up ? BT_NOT_FOUND : BT_DISCONNECTED);
		return;
	}
	if (!btw_cccd(x->props, false, &v)) {
		char *e = btw_att(6);
		done(ctx, e);
		free(e);
		return;
	}
	follow(c, x);
	sub_ctx *s = malloc(sizeof *s);
	s->handle = handle;
	op_ctx *o = op_new(c, 0, (void *)done, ctx);
	s->op = *o;
	free(o);
	void *op = NULL;
	HRESULT hr = write_cccd(x, v, &op);
	started(hr, op, H_WRITE, cccd_written, s);
}

static void ignore(void *p, outcome o) { asuka_com_release(o.result); }

void bt_engine_unsubscribe(uint32_t id, uint32_t handle) {
	conn *c = conn_of(id);
	ch *x = c ? char_of(c, handle) : NULL;
	if (!x) return;
	unevent(x->obj, 20, &x->token);
	for (size_t i = 0; i < c->nsubscribed; i++)
		if (c->subscribed[i] == handle) c->subscribed[i] = c->subscribed[--c->nsubscribed];
	void *op = NULL;
	HRESULT hr = write_cccd(x, 0, &op);
	started(hr, op, H_WRITE, ignore, NULL);
}

// Windows negotiates the MTU itself: the session's MaxPduSize.
void bt_engine_mtu(uint32_t id, void (*done)(void *ctx, int mtu, const char *err), void *ctx) {
	conn *c = conn_of(id);
	if (!c || !c->up) {
		done(ctx, 0, BT_DISCONNECTED);
		return;
	}
	int m = c->session ? u16(c->session, 10) : 23;
	done(ctx, m > 23 ? m : 23, NULL);
}

void bt_engine_rssi(uint32_t id, void (*done)(void *ctx, int rssi, const char *err), void *ctx) {
	if (!conn_of(id)) done(ctx, 0, BT_DISCONNECTED);
	else done(ctx, 0, "not available: Windows doesn't read a connected device's signal strength");
}

// ---- The peripheral role: a GATT server ----

static server *server_of(uint32_t id) {
	for (server *s = bw.servers; s; s = s->next)
		if (s->id == id && !s->closed) return s;
	return NULL;
}

static local *local_of(server *s, uint32_t handle) {
	for (local *l = s->chars; l; l = l->next)
		if (l->handle == handle) return l;
	return NULL;
}

static void request_finish(request *r) {
	if (r->deferral) {
		ASUKA_SLOT(r->deferral, 6, call0)(r->deferral);  // IDeferral.Complete
		asuka_com_release(r->deferral);
	}
	asuka_com_release(r->req);
	free(r);
}

static void request_fail(request *r, int code) {
	ASUKA_SLOT(r->req, r->write ? 13 : 12, HRESULT(STDMETHODCALLTYPE *)(void *, BYTE))(r->req, (BYTE)code);  // RespondWithProtocolError
	request_finish(r);
}

static void release_clients(local *l) {
	for (size_t i = 0; i < l->nclients; i++) {
		asuka_com_release(l->clients[i].obj);
		free(l->clients[i].central);
	}
	free(l->clients);
	l->clients = NULL;
	l->nclients = 0;
}

static void advertise_provider(provider *p, bool connectable, bool discoverable, const asuka_bytes *data, char **err);
static void end_advert(advert *a, const char *why_);
static void bind(void);

// close_server closes a server: its call waiting fails with canceled;
// the centrals' requests waiting, as unlikely; its services go.
static void close_server(server *s) {
	if (s->closed) return;
	s->closed = true;
	for (server **p = &bw.servers; *p; p = &(*p)->next)
		if (*p == s) {
			*p = s->next;
			break;
		}
	if (s->done) s->done(s->ctx, BT_CANCELED);
	s->done = NULL;
	for (request **pp = &bw.requests; *pp;) {
		request *r = *pp;
		if (r->server != s->id) {
			pp = &r->next;
			continue;
		}
		*pp = r->next;
		request_fail(r, 0x0e);  // the server went
	}
	while (s->chars) {
		local *l = s->chars;
		s->chars = l->next;
		unevent(l->obj, 18, &l->read_token);
		unevent(l->obj, 20, &l->write_token);
		unevent(l->obj, 16, &l->subs_token);
		release_clients(l);
		bt_strings_free(&l->subscribers);
		asuka_com_release(l->obj);
		free(l);
	}
	while (s->providers) {
		provider *p = s->providers;
		s->providers = p->next;
		for (advert *a = bw.adverts; a; a = a->next)
			if (a->id == p->advert) p->advert = 0;
		unevent(p->p, 8, &p->token);
		ASUKA_SLOT(p->p, 12, call0)(p->p);  // StopAdvertising
		asuka_com_release(p->svc);
		asuka_com_release(p->p);
		free(p->uuid);
		free(p);
	}
	free(s);
}

// A central's request, taken on WinRT's thread, answered on the main.
typedef struct {
	uint32_t server, handle;
	bool write;
	char *central;
	void *deferral;
} taking;

static void request_got(void *p, outcome o) {
	taking *t = p;
	server *s = server_of(t->server);
	request *r = calloc(1, sizeof *r);
	*r = (request){0, t->server, o.result, t->deferral, t->write};
	if (!o.result) {
		request_finish(r);
	} else if (!s) {
		request_fail(r, 0x0e);
	} else {
		bt_central c = {t->central, (char *)""};
		if (t->write) {
			asuka_bytes v = buffer_of(r->req, 6);     // Value
			int offset = (int)u32(r->req, 7);         // Offset
			bool response = i32(r->req, 8) == 0;      // Option: WriteWithResponse
			uint32_t rid = 0;
			if (response) {
				r->id = rid = ++bw.next_request;
				r->next = bw.requests;
				bw.requests = r;
			} else {
				request_finish(r);  // no one answers a write without response
			}
			bt_on_write(t->server, rid, t->handle, &c, v, offset);
			free(v.data);
		} else {
			r->id = ++bw.next_request;
			r->next = bw.requests;
			bw.requests = r;
			bt_on_read(t->server, r->id, t->handle, &c, (int)u32(r->req, 6));  // Offset
		}
	}
	free(t->central);
	free(t);
}

// session_central is a GattSession's device's address.
static char *session_central(void *session) {
	void *id = obj(session, 6);  // DeviceId: BluetoothDeviceId
	char *s = id ? string_of(id, 6) : asuka_strdup("");
	asuka_com_release(id);
	char *c = btw_central_id(s);
	free(s);
	return c;
}

typedef struct {
	uint32_t server, handle;
} local_ctx;

static void take_request(local_ctx *k, void *args, bool write) {
	taking *t = calloc(1, sizeof *t);
	*t = (taking){k->server, k->handle, write};
	void *session = obj(args, 6);  // Session
	t->central = session ? session_central(session) : asuka_strdup("");
	asuka_com_release(session);
	t->deferral = obj(args, 7);  // GetDeferral
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(args, 8, get_obj)(args, &op);  // GetRequestAsync
	started(hr, op, write ? H_WRITE_REQUEST : H_READ_REQUEST, request_got, t);
}

static void on_read_requested(void *k, void *sender, void *args) { take_request(k, args, false); }
static void on_write_requested(void *k, void *sender, void *args) { take_request(k, args, true); }

static void subscribers_changed(void *p) {
	local_ctx *k = p;
	server *s = server_of(k->server);
	local *l = s ? local_of(s, k->handle) : NULL;
	free(k);
	if (!l) return;
	release_clients(l);
	bt_strings now = {0};
	void *list = obj(l->obj, 15);  // SubscribedClients
	UINT32 n = list ? vector_size(list) : 0;
	l->clients = calloc(n ? n : 1, sizeof *l->clients);
	for (UINT32 i = 0; i < n; i++) {
		void *x = vector_at(list, i);
		if (!x) continue;
		void *session = obj(x, 6);  // Session
		char *id = session ? session_central(session) : asuka_strdup("");
		asuka_com_release(session);
		l->clients[l->nclients++] = (client){x, asuka_strdup(id)};
		bt_strings_push(&now, id);
	}
	asuka_com_release(list);
	bt_strings on = {0}, off = {0};
	btw_diff(&l->subscribers, &now, &on, &off);
	bt_strings_free(&l->subscribers);
	l->subscribers = now;
	uint32_t sid = s->id, h = l->handle;
	for (size_t i = 0; i < on.len; i++) bt_on_subscription(sid, h, &(bt_central){on.data[i], (char *)""}, true);
	for (size_t i = 0; i < off.len; i++) bt_on_subscription(sid, h, &(bt_central){off.data[i], (char *)""}, false);
	bt_strings_free(&on);
	bt_strings_free(&off);
}

static void on_subscribers(void *k, void *sender, void *args) {
	local_ctx *x = malloc(sizeof *x);
	*x = *(local_ctx *)k;
	asuka_desktop_post(subscribers_changed, x);
}

typedef struct {
	uint32_t server;
	provider *p;
	INT32 status, error;
} status_ctx;

static void provider_status(void *p) {
	status_ctx *x = p;
	server *s = server_of(x->server);
	if (s && x->status == 3) {  // Aborted
		const char *w = btw_error(x->error);
		if (!w) w = "failed: Windows stopped the service";
		for (provider *pv = s->providers; pv; pv = pv->next)
			if (pv == x->p && pv->advert)
				for (advert *a = bw.adverts; a; a = a->next)
					if (a->id == pv->advert) {
						end_advert(a, w);
						break;
					}
		uint32_t id = s->id;
		close_server(s);
		bt_on_server_stopped(id, w);
		bind();
	}
	free(x);
}

typedef struct {
	uint32_t server;
	provider *p;
} provider_ctx;

static void on_provider_status(void *k, void *sender, void *args) {
	provider_ctx *c = k;
	status_ctx *x = malloc(sizeof *x);
	*x = (status_ctx){c->server, c->p, i32(args, 7), i32(args, 6)};  // Status, Error
	asuka_desktop_post(provider_status, x);
}

// The services a serve adds, one at a time.
typedef struct {
	uint32_t server;
	bt_local_service *services;
	size_t n, at, char_at;
	provider *p;  // the service being added
} serving;

static void serving_free(serving *v) {
	for (size_t i = 0; i < v->n; i++) {
		for (size_t j = 0; j < v->services[i].ncharacteristics; j++) {
			free(v->services[i].characteristics[j].uuid);
			free(v->services[i].characteristics[j].value.data);
		}
		free(v->services[i].characteristics);
		free(v->services[i].uuid);
	}
	free(v->services);
	free(v);
}

static void serve_fail(serving *v, const char *why_) {
	server *s = server_of(v->server);
	if (s) {
		bt_done done = s->done;
		void *ctx = s->ctx;
		s->done = NULL;
		close_server(s);
		if (done) done(ctx, why_);
	}
	serving_free(v);
}

static void serve_next(serving *v);

static void char_added(void *p, outcome o) {
	serving *v = p;
	server *s = server_of(v->server);
	void *c = o.result ? obj(o.result, 6) : NULL;  // Characteristic
	INT32 e = o.result ? i32(o.result, 7) : 4;     // Error
	asuka_com_release(o.result);
	if (!s) {
		asuka_com_release(c);
		serving_free(v);
		return;
	}
	if (!c || e) {
		asuka_com_release(c);
		char *w = btw_error(e) ? asuka_strdup(btw_error(e)) : why(o.hr);
		serve_fail(v, w);
		free(w);
		return;
	}
	bt_local_characteristic *lc = &v->services[v->at].characteristics[v->char_at];
	local *l = calloc(1, sizeof *l);
	*l = (local){.obj = c, .handle = lc->handle, .server = s, .next = s->chars};
	s->chars = l;
	local_ctx k = {s->id, lc->handle};
	local_ctx *k1 = malloc(sizeof k), *k2 = malloc(sizeof k), *k3 = malloc(sizeof k);
	*k1 = *k2 = *k3 = k;
	l->read_token = event(c, 18, E_READ_REQUESTED, on_read_requested, k1, free);    // add_ReadRequested
	l->write_token = event(c, 20, E_WRITE_REQUESTED, on_write_requested, k2, free);  // add_WriteRequested
	l->subs_token = event(c, 16, E_SUBSCRIBERS, on_subscribers, k3, free);           // add_SubscribedClientsChanged
	v->char_at++;
	serve_next(v);
}

static void provider_made(void *p, outcome o) {
	serving *v = p;
	server *s = server_of(v->server);
	INT32 e = o.result ? i32(o.result, 6) : 4;  // Error
	void *pv = o.result && !e ? obj(o.result, 7) : NULL;  // ServiceProvider
	asuka_com_release(o.result);
	if (!s) {
		asuka_com_release(pv);
		serving_free(v);
		return;
	}
	if (!pv) {
		char *w = btw_error(e) ? asuka_strdup(btw_error(e)) : why(o.hr);
		serve_fail(v, w);
		free(w);
		return;
	}
	provider *x = calloc(1, sizeof *x);
	*x = (provider){.p = pv, .svc = obj(pv, 6), .uuid = bt_normalize(v->services[v->at].uuid), .server = s, .next = s->providers};  // Service
	s->providers = x;
	v->p = x;
	v->char_at = 0;
	serve_next(v);
}

// serve_next adds the service's next characteristic, or the next
// service, or publishes them all.
static void serve_next(serving *v) {
	server *s = server_of(v->server);
	if (!s) {
		serving_free(v);
		return;
	}
	if (v->p && v->char_at < v->services[v->at].ncharacteristics) {
		bt_local_characteristic *lc = &v->services[v->at].characteristics[v->char_at];
		void *params = activate(NS_GATT "GattLocalCharacteristicParameters", I_LOCAL_CHAR_PARAMS), *op = NULL;
		if (!params) {
			serve_fail(v, "failed: no characteristic parameters");
			return;
		}
		ASUKA_SLOT(params, 8, set_i32)(params, lc->properties & 0xff);                                // put_CharacteristicProperties
		ASUKA_SLOT(params, 10, set_i32)(params, lc->permissions & BT_PERM_READ_ENCRYPTED ? 2 : 0);    // put_ReadProtectionLevel
		ASUKA_SLOT(params, 12, set_i32)(params, lc->permissions & BT_PERM_WRITE_ENCRYPTED ? 2 : 0);   // put_WriteProtectionLevel
		if (lc->has_value) {
			void *buf = buffer(lc->value.data, lc->value.len);
			if (buf) ASUKA_SLOT(params, 6, set_obj)(params, buf);  // put_StaticValue
			asuka_com_release(buf);
		}
		GUID g = guid_of_uuid(lc->uuid);
		HRESULT hr = ASUKA_SLOT(v->p->svc, 7, HRESULT(STDMETHODCALLTYPE *)(void *, GUID, void *, void **))(v->p->svc, g, params, &op);  // CreateCharacteristicAsync
		asuka_com_release(params);
		started(hr, op, H_LOCAL_CHAR, char_added, v);
		return;
	}
	if (v->p) v->at++, v->p = NULL;
	if (v->at < v->n) {
		void *st = statics(NS_GATT "GattServiceProvider", I_PROVIDER_STATICS), *op = NULL;
		GUID g = guid_of_uuid(v->services[v->at].uuid);
		HRESULT hr = st ? ASUKA_SLOT(st, 6, HRESULT(STDMETHODCALLTYPE *)(void *, GUID, void **))(st, g, &op) : E_FAIL;  // CreateAsync
		asuka_com_release(st);
		started(hr, op, H_PROVIDER, provider_made, v);
		return;
	}
	// Published: centrals connected to the computer find them.
	for (provider *p = s->providers; p; p = p->next) {
		provider_ctx *k = malloc(sizeof *k);
		*k = (provider_ctx){s->id, p};
		p->token = event(p->p, 8, E_PROVIDER_STATUS, on_provider_status, k, free);  // add_AdvertisementStatusChanged
		char *err = NULL;
		advertise_provider(p, true, false, NULL, &err);
		if (err) {
			serve_fail(v, err);
			free(err);
			return;
		}
	}
	s->ready = true;
	bt_done done = s->done;
	void *ctx = s->ctx;
	s->done = NULL;
	serving_free(v);
	if (done) done(ctx, NULL);
	bind();
}

typedef struct {
	uint32_t id;
	bt_local_service *services;
	size_t n;
	bt_done done;
	void *ctx;
} serve_args;

static const char *peripheral(void) {
	if (!bw.adapter || !bw.low_energy) return BT_UNSUPPORTED;
	if (!bw.peripheral) return "not available: this Bluetooth adapter has no peripheral role";
	const char *st = current();
	return strcmp(st, BT_ON) ? st : NULL;
}

static void serve_now(void *p) {
	serve_args *a = p;
	const char *no = peripheral();
	serving *v = calloc(1, sizeof *v);
	*v = (serving){a->id, a->services, a->n};
	if (no) {
		a->done(a->ctx, no);
		serving_free(v);
		free(a);
		return;
	}
	server *old = server_of(a->id);
	if (old) close_server(old);
	server *s = calloc(1, sizeof *s);
	*s = (server){.id = a->id, .done = a->done, .ctx = a->ctx, .next = bw.servers};
	bw.servers = s;
	free(a);
	serve_next(v);
}

void bt_engine_serve(uint32_t id, const bt_local_service *services, size_t n, bt_done done, void *ctx) {
	serve_args *a = malloc(sizeof *a);
	*a = (serve_args){id, calloc(n ? n : 1, sizeof(bt_local_service)), n, done, ctx};
	for (size_t i = 0; i < n; i++) {
		const bt_local_service *s = &services[i];
		a->services[i] = (bt_local_service){asuka_strdup(s->uuid), s->primary, calloc(s->ncharacteristics ? s->ncharacteristics : 1, sizeof(bt_local_characteristic)),
		                                    s->ncharacteristics};
		for (size_t j = 0; j < s->ncharacteristics; j++) {
			const bt_local_characteristic *c = &s->characteristics[j];
			a->services[i].characteristics[j] = (bt_local_characteristic){c->handle, asuka_strdup(c->uuid), c->properties, c->permissions, c->has_value,
			                                                              asuka_bytes_copy(c->value.data, c->value.len)};
		}
	}
	with_adapter(serve_now, a);
}

void bt_engine_unserve(uint32_t id) {
	server *s = server_of(id);
	if (!s) return;
	close_server(s);
	bind();
}

void bt_engine_respond(uint32_t server_id, uint32_t request_id, asuka_bytes value, int error) {
	for (request **pp = &bw.requests; *pp; pp = &(*pp)->next) {
		request *r = *pp;
		if (r->id != request_id || r->server != server_id) continue;
		*pp = r->next;
		if (error) {
			request_fail(r, error);
		} else if (r->write) {
			ASUKA_SLOT(r->req, 12, call0)(r->req);  // Respond
			request_finish(r);
		} else {
			void *buf = buffer(value.data, value.len);
			if (buf) ASUKA_SLOT(r->req, 11, set_obj)(r->req, buf);  // RespondWithValue
			else ASUKA_SLOT(r->req, 12, HRESULT(STDMETHODCALLTYPE *)(void *, BYTE))(r->req, 0x0e);
			asuka_com_release(buf);
			request_finish(r);
		}
		return;
	}
}

void bt_engine_notify(uint32_t server_id, uint32_t handle, asuka_bytes value) {
	server *s = server_of(server_id);
	local *l = s && s->ready ? local_of(s, handle) : NULL;
	if (!l) return;
	void *buf = buffer(value.data, value.len), *op = NULL;
	HRESULT hr = buf ? ASUKA_SLOT(l->obj, 22, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(l->obj, buf, &op) : E_FAIL;  // NotifyValueAsync
	asuka_com_release(buf);
	started(hr, op, H_NOTIFY_ALL, ignore, NULL);
}

// advertise_provider publishes the service: connectable (centrals
// connected to the computer use it), discoverable (its UUID, and the
// service data if any, advertised). A provider advertising already
// changes its parameters.
static void advertise_provider(provider *p, bool connectable, bool discoverable, const asuka_bytes *data, char **err) {
	void *params = activate(NS_GATT "GattServiceProviderAdvertisingParameters", I_ADVERTISING_PARAMS);
	if (!params) {
		*err = asuka_strdup("failed: no advertising parameters");
		return;
	}
	ASUKA_SLOT(params, 6, HRESULT(STDMETHODCALLTYPE *)(void *, BYTE))(params, connectable);   // put_IsConnectable
	ASUKA_SLOT(params, 8, HRESULT(STDMETHODCALLTYPE *)(void *, BYTE))(params, discoverable);  // put_IsDiscoverable
	if (data) {
		void *p2 = query(params, I_ADVERTISING_PARAMS2), *buf = p2 ? buffer(data->data, data->len) : NULL;
		if (buf) ASUKA_SLOT(p2, 6, set_obj)(p2, buf);  // put_ServiceData
		asuka_com_release(buf);
		asuka_com_release(p2);
	}
	INT32 st = i32(p->p, 7);  // AdvertisementStatus
	bool updated = false;
	if (st == 2 || st == 4) {  // Started, StartedWithoutData
		void *p2 = query(p->p, I_PROVIDER2);
		updated = p2 && SUCCEEDED(ASUKA_SLOT(p2, 6, set_obj)(p2, params));  // UpdateAdvertisingParameters (Windows 11)
		asuka_com_release(p2);
		if (!updated) ASUKA_SLOT(p->p, 12, call0)(p->p);  // StopAdvertising
	}
	if (!updated) {
		HRESULT hr = ASUKA_SLOT(p->p, 11, set_obj)(p->p, params);  // StartAdvertisingWithParameters
		if (FAILED(hr)) *err = why(hr);
	}
	asuka_com_release(params);
}

// ---- Advertising ----

static advert *advert_of(uint32_t id) {
	for (advert *a = bw.adverts; a; a = a->next)
		if (a->id == id) return a;
	return NULL;
}

// provider_for is a ready server's provider of the service, not
// advertising for another advertisement.
static provider *provider_for(const char *uuid) {
	for (server *s = bw.servers; s; s = s->next) {
		if (!s->ready || s->closed) continue;
		for (provider *p = s->providers; p; p = p->next)
			if (!strcmp(p->uuid, uuid) && !p->advert) return p;
	}
	return NULL;
}

static bool bound(advert *a, const char *uuid) {
	for (server *s = bw.servers; s; s = s->next)
		for (provider *p = s->providers; p; p = p->next)
			if (p->advert == a->id && !strcmp(p->uuid, uuid)) return true;
	return false;
}

// bind makes the providers of the services the advertisements name
// advertise discoverable, as their servers are ready.
static void bind(void) {
	for (advert *a = bw.adverts, *next; a; a = next) {
		next = a->next;
		for (size_t i = 0; i < a->services.len; i++) {
			if (bound(a, a->services.data[i])) continue;
			provider *p = provider_for(a->services.data[i]);
			if (!p) continue;
			const asuka_bytes *data = NULL;
			for (size_t k = 0; k < a->service_data.len; k++)
				if (!strcmp(a->service_data.data[k].key, a->services.data[i])) data = &a->service_data.data[k].value;
			char *err = NULL;
			advertise_provider(p, a->connectable, true, data, &err);
			if (err) {
				end_advert(a, err);
				free(err);
				break;
			}
			p->advert = a->id;
		}
	}
}

// end_advert stops an advertisement: its call waiting fails with why (or
// canceled), else (why set) it's told it stopped.
static void end_advert(advert *a, const char *why_) {
	for (advert **p = &bw.adverts; *p; p = &(*p)->next)
		if (*p == a) {
			*p = a->next;
			break;
		}
	if (a->publisher) {
		unevent(a->publisher, 10, &a->token);
		ASUKA_SLOT(a->publisher, 9, call0)(a->publisher);  // Stop
		asuka_com_release(a->publisher);
	}
	for (server *s = bw.servers; s; s = s->next)
		for (provider *p = s->providers; p; p = p->next)
			if (p->advert == a->id) {
				char *err = NULL;
				p->advert = 0;
				advertise_provider(p, true, false, NULL, &err);  // served still, not discoverable
				free(err);
			}
	uint32_t id = a->id;
	bt_done done = a->done;
	void *ctx = a->ctx;
	bt_strings_free(&a->services);
	bt_entries_free(&a->service_data);
	free(a);
	if (done) done(ctx, why_ ? why_ : BT_CANCELED);
	else if (why_) bt_on_advertise_stopped(id, why_);
	bind();  // another advertisement may want those providers
}

typedef struct {
	uint32_t id;
	INT32 status, error;
} publisher_ctx;

static void publisher_status(void *p) {
	publisher_ctx *x = p;
	advert *a = advert_of(x->id);
	if (a && x->status == 2 && a->waiting > 0) {  // Started
		if (--a->waiting == 0 && a->done) {
			bt_done done = a->done;
			a->done = NULL;
			done(a->ctx, NULL);
		}
	} else if (a && x->status == 5) {  // Aborted
		const char *w = btw_error(x->error), *st = current();
		if (strcmp(st, BT_ON) != 0) w = st;
		else if (!w) w = "failed: Windows stopped advertising";
		end_advert(a, w);
	}
	free(x);
}

static void on_publisher_status(void *id, void *sender, void *args) {
	publisher_ctx *x = malloc(sizeof *x);
	*x = (publisher_ctx){id_of(id), i32(args, 6), i32(args, 7)};  // Status, Error
	asuka_desktop_post(publisher_status, x);
}

// publisher makes a publisher of the manufacturer data (all Windows lets
// an app's own advertisement have).
static void *publisher(const bt_entries *md, bool tx_power, uint32_t id, INT64 *token) {
	void *ad = activate(NS_ADV "BluetoothLEAdvertisement", I_ADVERTISEMENT), *list = obj(ad, 11);  // ManufacturerData
	void *fact = statics(NS_ADV "BluetoothLEManufacturerData", I_MANUFACTURER_FACTORY), *p = NULL;
	bool ok = ad && list && fact;
	for (size_t i = 0; ok && i < md->len; i++) {
		void *buf = buffer(md->data[i].value.data, md->data[i].value.len), *m = NULL;
		UINT16 company = (UINT16)strtoul(md->data[i].key, NULL, 16);
		ok = buf && SUCCEEDED(ASUKA_SLOT(fact, 6, HRESULT(STDMETHODCALLTYPE *)(void *, UINT16, void *, void **))(fact, company, buf, &m)) && m &&
		     SUCCEEDED(ASUKA_SLOT(list, 13, set_obj)(list, m));  // Create, IVector.Append
		asuka_com_release(m);
		asuka_com_release(buf);
	}
	void *pf = ok ? statics(NS_ADV "BluetoothLEAdvertisementPublisher", I_PUBLISHER_FACTORY) : NULL;
	if (pf) ASUKA_SLOT(pf, 6, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(pf, ad, &p);  // Create(advertisement)
	asuka_com_release(pf);
	asuka_com_release(fact);
	asuka_com_release(list);
	asuka_com_release(ad);
	if (!p) return NULL;
	if (tx_power) {
		void *p2 = query(p, I_PUBLISHER2);
		if (p2) ASUKA_SLOT(p2, 13, HRESULT(STDMETHODCALLTYPE *)(void *, BYTE))(p2, 1);  // put_IncludeTransmitPowerLevel (Windows 10 2004)
		asuka_com_release(p2);
	}
	*token = event(p, 10, E_PUBLISHER_STATUS, on_publisher_status, as_id(id), NULL);  // add_StatusChanged
	return p;
}

typedef struct {
	uint32_t id;
	bt_advertising a;
	bt_done done;
	void *ctx;
} advertise_args;

static void advertise_args_free(advertise_args *x) {
	free(x->a.local_name);
	bt_strings_free(&x->a.services);
	bt_entries_free(&x->a.service_data);
	bt_entries_free(&x->a.manufacturer_data);
	free(x);
}

static void advertise_now(void *p) {
	advertise_args *x = p;
	const char *no = peripheral();
	if (no) {
		x->done(x->ctx, no);
		advertise_args_free(x);
		return;
	}
	bt_strings services = {0};
	for (size_t i = 0; i < x->a.services.len + x->a.service_data.len; i++) {
		const char *u = i < x->a.services.len ? x->a.services.data[i] : x->a.service_data.data[i - x->a.services.len].key;
		bool seen = false;
		for (size_t k = 0; k < services.len; k++) seen = seen || !strcmp(services.data[k], u);
		if (!seen) bt_strings_push(&services, asuka_strdup(u));
	}
	if (x->a.manufacturer_data.len == 0 && services.len == 0) {
		x->done(x->ctx, "not available: Windows advertises an app's manufacturer data and the services it serves, not a name alone");
		bt_strings_free(&services);
		advertise_args_free(x);
		return;
	}
	advert *old = advert_of(x->id);
	if (old) end_advert(old, NULL);
	advert *a = calloc(1, sizeof *a);
	*a = (advert){.id = x->id, .services = services, .connectable = x->a.connectable, .done = x->done, .ctx = x->ctx, .next = bw.adverts};
	a->service_data = x->a.service_data;
	x->a.service_data = (bt_entries){0};
	bw.adverts = a;
	if (x->a.manufacturer_data.len) {
		a->publisher = publisher(&x->a.manufacturer_data, x->a.include_tx_power, a->id, &a->token);
		HRESULT hr = a->publisher ? ASUKA_SLOT(a->publisher, 8, call0)(a->publisher) : E_FAIL;  // Start
		if (FAILED(hr)) {
			char *w = a->publisher ? why(hr) : asuka_strdup("failed: no advertisement publisher");
			end_advert(a, w);
			free(w);
			advertise_args_free(x);
			return;
		}
		a->waiting++;
	}
	advertise_args_free(x);
	bind();
	a = advert_of(a->id);  // bind may have ended it
	if (a && a->waiting == 0 && a->done) {
		bt_done done = a->done;
		a->done = NULL;
		done(a->ctx, NULL);
	}
}

void bt_engine_advertise(uint32_t id, const bt_advertising *args, bt_done done, void *ctx) {
	advertise_args *x = calloc(1, sizeof *x);
	x->id = id, x->done = done, x->ctx = ctx;
	x->a.local_name = asuka_strdup(args->local_name);
	x->a.connectable = args->connectable, x->a.include_tx_power = args->include_tx_power;
	for (size_t i = 0; i < args->services.len; i++) bt_strings_push(&x->a.services, bt_normalize(args->services.data[i]));
	for (size_t i = 0; i < args->service_data.len; i++)
		bt_entries_push(&x->a.service_data, bt_normalize(args->service_data.data[i].key), args->service_data.data[i].value.data,
		                args->service_data.data[i].value.len);
	for (size_t i = 0; i < args->manufacturer_data.len; i++)
		bt_entries_push(&x->a.manufacturer_data, asuka_strdup(args->manufacturer_data.data[i].key), args->manufacturer_data.data[i].value.data,
		                args->manufacturer_data.data[i].value.len);
	with_adapter(advertise_now, x);
}

void bt_engine_stop_advertise(uint32_t id) {
	advert *a = advert_of(id);
	if (a) end_advert(a, NULL);
}
