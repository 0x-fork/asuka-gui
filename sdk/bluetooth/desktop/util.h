// util.h: what the Windows engine works out without WinRT (util.c,
// portable, so its unit test runs anywhere): advertisements' data
// sections, an advertisement and its scan response put together,
// addresses, GATT's errors in the engine's words, the device picker's
// selectors.
#ifndef BLUETOOTH_UTIL_H
#define BLUETOOTH_UTIL_H

#include "bluetooth.h"

// An advertisement's data section: its type and data.
typedef struct {
	uint8_t type;
	const uint8_t *data;
	size_t len;
} btw_section;

// What one advertising packet says.
typedef struct {
	char *name;
	bt_strings services, solicited;
	bt_entries service_data, manufacturer;  // UUIDs normalized; companies four hex digits
	bool has_tx_power;
	int tx_power;
} btw_ad;

btw_ad btw_parse_sections(const btw_section *s, size_t n);
void btw_ad_free(btw_ad *a);
// btw_uuid_le is a UUID of 2, 4 or 16 bytes, little endian, normalized
// (NULL for another length).
char *btw_uuid_le(const uint8_t *b, size_t n);

// BluetoothLEAdvertisementType.
enum { BTW_ADV_CONNECTABLE_UNDIRECTED, BTW_ADV_CONNECTABLE_DIRECTED, BTW_ADV_SCANNABLE_UNDIRECTED, BTW_ADV_NON_CONNECTABLE_UNDIRECTED, BTW_ADV_SCAN_RESPONSE };

// A packet heard: its data, signal, time, and what kind.
typedef struct {
	btw_ad ad;
	int rssi;
	int64_t time_ms;
	bool response, scannable;
	int type;
	int connectable;  // Windows 10 2004's IsConnectable: -1 unknown (from type), 0, 1
} btw_packet;

// A device heard: its advertisement and its scan response (Windows gives
// them apart), and whether it's connectable.
typedef struct {
	btw_ad adv, rsp;
	bool has_adv, has_rsp;
	int advs;  // advertisements heard without a scan response
	int rssi;
	int64_t time_ms;
	bool scannable;
	int connectable;  // -1 unknown
} btw_heard;

// btw_take takes a packet's (its ad is the heard's then).
void btw_take(btw_heard *h, btw_packet *p);
// btw_complete reports whether it's all there is: a scan response if one
// may come, or two advertisements without one.
bool btw_complete(const btw_heard *h);
bt_advertisement btw_advertisement(const btw_heard *h, const char *id);
void btw_heard_free(btw_heard *h);
bool btw_matches(const bt_advertisement *a, const bt_strings *services);

// Addresses: 48 bits as "C4:7C:8D:6A:1F:20" (malloc's), and back
// (colons, dashes or none).
char *btw_address(uint64_t a);
bool btw_parse_address(const char *s, uint64_t *out);
// btw_central_id is a central's address from its device ID
// ("BluetoothLE#BluetoothLE<ours>-<theirs>"), else the ID (malloc's).
char *btw_central_id(const char *device_id);

// btw_gatt_error is a GATT result in the engine's words (NULL: success;
// malloc's): its GattCommunicationStatus, ATT error (-1: none), or an
// operation's HRESULT.
char *btw_gatt_error(int status, int proto, uint32_t hr);
char *btw_att(int code);
// btw_error is a BluetoothError in the engine's words (NULL: success).
const char *btw_error(int e);
const char *btw_radio_state(int st);
// btw_cccd is the CCCD to write to subscribe: false when the
// characteristic neither notifies nor indicates.
bool btw_cccd(int props, bool indicate, int *v);
// btw_chooser_selectors are the picker's AQS selectors: bases narrowed
// to the filters' names (bases as they are when a filter has none).
bt_strings btw_chooser_selectors(const bt_strings *bases, const bluetooth_list_Filter *filters);
// btw_diff: what's in now and not old (on), and in old and not now (off).
void btw_diff(const bt_strings *old, const bt_strings *now, bt_strings *on, bt_strings *off);

#endif
