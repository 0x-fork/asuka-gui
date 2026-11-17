// bluetooth_test.c: the package's portable C (platform/desktop's
// TestStdUnits): UUIDs, and the Windows engine's work without WinRT
// (util.c). The engine and the generated glue the adapter calls are
// stand-ins here, never called.
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;

#define CHECK(c)                                           \
	do {                                                   \
		if (!(c)) {                                        \
			printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
			failed = 1;                                    \
		}                                                  \
	} while (0)

static bool is(char *got, const char *want) {
	bool ok = got && !strcmp(got, want);
	if (!ok) printf("got %s, want %s\n", got ? got : "NULL", want);
	free(got);
	return ok;
}

static void uuids(void) {
	CHECK(is(bt_normalize("180D"), "0000180d-0000-1000-8000-00805f9b34fb"));
	CHECK(is(bt_normalize("0x2a37"), "00002a37-0000-1000-8000-00805f9b34fb"));
	CHECK(is(bt_normalize("{6E400001-B5A3-F393-E0A9-E50E24DCCA9E}"), "6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
	CHECK(is(bt_normalize("12345678"), "12345678-0000-1000-8000-00805f9b34fb"));
	CHECK(is(bt_normalize("not one"), "not one"));
	CHECK(is(bt_short("0000180d-0000-1000-8000-00805f9b34fb"), "180d"));
	CHECK(is(bt_short("6e400001-b5a3-f393-e0a9-e50e24dcca9e"), "6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
	CHECK(is(bt_short("12345678-0000-1000-8000-00805f9b34fb"), "12345678-0000-1000-8000-00805f9b34fb"));
}

static void addresses(void) {
	CHECK(is(btw_address(0xC47C8D6A1F20), "C4:7C:8D:6A:1F:20"));
	CHECK(is(btw_address(0x01), "00:00:00:00:00:01"));
	const char *good[] = {"C4:7C:8D:6A:1F:20", "c4:7c:8d:6a:1f:20", "c4-7c-8d-6a-1f-20", "c47c8d6a1f20"};
	for (size_t i = 0; i < 4; i++) {
		uint64_t a = 0;
		CHECK(btw_parse_address(good[i], &a) && a == 0xC47C8D6A1F20);
	}
	const char *bad[] = {"", "C4:7C:8D:6A:1F", "C4:7C-8D:6A:1F:20", "C4.7C.8D.6A.1F.20", "zz:7c:8d:6a:1f:20", "0000180d-0000-1000-8000-00805f9b34fb"};
	for (size_t i = 0; i < 6; i++) {
		uint64_t a;
		CHECK(!btw_parse_address(bad[i], &a));
	}
	CHECK(is(btw_central_id("BluetoothLE#BluetoothLE00:1a:7d:da:71:13-c4:7c:8d:6a:1f:20"), "C4:7C:8D:6A:1F:20"));
	CHECK(is(btw_central_id("weird"), "weird"));
}

static bool entry(const bt_entries *e, const char *key, const uint8_t *v, size_t n) {
	for (size_t i = 0; i < e->len; i++)
		if (!strcmp(e->data[i].key, key)) return e->data[i].value.len == n && !memcmp(e->data[i].value.data, v, n);
	return false;
}

static void sections(void) {
	static const uint8_t flags[] = {0x06}, svcs[] = {0x0d, 0x18, 0x0f, 0x18},
	                     u128[] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0, 0, 0x80, 0, 0x10, 0, 0, 0x78, 0x56, 0x34, 0x12}, tx[] = {0xf4}, sol[] = {0x02, 0x18},
	                     sd[] = {0x0f, 0x18, 0x64}, apple[] = {0x4c, 0x00, 0x02, 0x15}, shorty[] = {0x01};
	btw_section s[] = {
	    {0x01, flags, 1}, {0x03, svcs, 4},     {0x07, u128, 16},     {0x08, (const uint8_t *)"Pol", 3}, {0x09, (const uint8_t *)"Polar H10", 9},
	    {0x0a, tx, 1},    {0x14, sol, 2},      {0x16, sd, 3},        {0xff, apple, 4},                   {0xff, shorty, 1},
	};
	btw_ad a = btw_parse_sections(s, sizeof s / sizeof *s);
	CHECK(a.name && !strcmp(a.name, "Polar H10"));
	CHECK(a.services.len == 3 && !strcmp(a.services.data[0], "0000180d-0000-1000-8000-00805f9b34fb") &&
	      !strcmp(a.services.data[1], "0000180f-0000-1000-8000-00805f9b34fb") && !strcmp(a.services.data[2], "12345678-0000-1000-8000-00805f9b34fb"));
	CHECK(a.solicited.len == 1 && !strcmp(a.solicited.data[0], "00001802-0000-1000-8000-00805f9b34fb"));
	CHECK(a.has_tx_power && a.tx_power == -12);
	CHECK(entry(&a.service_data, "0000180f-0000-1000-8000-00805f9b34fb", (const uint8_t[]){0x64}, 1));
	CHECK(a.manufacturer.len == 1 && entry(&a.manufacturer, "004c", (const uint8_t[]){0x02, 0x15}, 2));
	btw_ad_free(&a);
	CHECK(is(btw_uuid_le((const uint8_t[]){0x78, 0x56, 0x34, 0x12}, 4), "12345678-0000-1000-8000-00805f9b34fb"));
}

static btw_ad parse(const btw_section *s, size_t n) { return btw_parse_sections(s, n); }

static void heard(void) {
	btw_section adv_s[] = {{0x03, (const uint8_t[]){0x0d, 0x18}, 2}};
	btw_section rsp_s[] = {{0x09, (const uint8_t *)"HR", 2}, {0xff, (const uint8_t[]){0x59, 0x00, 1}, 3}};
	btw_heard h = {.connectable = -1};
	btw_packet p = {.ad = parse(adv_s, 1), .rssi = -60, .type = BTW_ADV_CONNECTABLE_UNDIRECTED, .scannable = true, .connectable = -1};
	btw_take(&h, &p);
	CHECK(!btw_complete(&h));  // before its scan response
	p = (btw_packet){.ad = parse(rsp_s, 2), .rssi = -61, .response = true, .type = BTW_ADV_SCAN_RESPONSE, .connectable = -1};
	btw_take(&h, &p);
	CHECK(btw_complete(&h));
	bt_advertisement a = btw_advertisement(&h, "AA:BB:CC:DD:EE:FF");
	CHECK(!strcmp(a.local_name, "HR") && !strcmp(a.name, "HR") && !strcmp(a.id, "AA:BB:CC:DD:EE:FF") && a.rssi == -61);
	CHECK(a.services.len == 1 && !strcmp(a.services.data[0], "0000180d-0000-1000-8000-00805f9b34fb"));
	CHECK(entry(&a.manufacturer_data, "0059", (const uint8_t[]){1}, 1));
	CHECK(a.connectable);
	bt_strings want = {0}, none = {0}, other = {0};
	bt_strings_push(&want, asuka_strdup("180d"));
	bt_strings_push(&other, asuka_strdup("180f"));
	CHECK(btw_matches(&a, &want) && !btw_matches(&a, &other) && btw_matches(&a, &none));
	bt_strings_free(&want);
	bt_strings_free(&other);
	bt_advertisement_free(&a);
	btw_heard_free(&h);

	// A device that doesn't answer is complete after two advertisements.
	btw_heard quiet = {.connectable = -1};
	for (int i = 0; i < 2; i++) {
		p = (btw_packet){.ad = parse(adv_s, 1), .type = BTW_ADV_CONNECTABLE_UNDIRECTED, .scannable = true, .connectable = -1};
		btw_take(&quiet, &p);
	}
	CHECK(btw_complete(&quiet));
	btw_heard_free(&quiet);
	btw_heard beacon = {.connectable = -1};
	p = (btw_packet){.ad = parse(adv_s, 1), .type = BTW_ADV_CONNECTABLE_UNDIRECTED, .connectable = 0};
	btw_take(&beacon, &p);
	a = btw_advertisement(&beacon, "x");
	CHECK(btw_complete(&beacon) && !a.connectable);
	bt_advertisement_free(&a);
	btw_heard_free(&beacon);
	btw_heard nonconn = {.connectable = -1};
	p = (btw_packet){.ad = parse(adv_s, 1), .type = BTW_ADV_NON_CONNECTABLE_UNDIRECTED, .connectable = -1};
	btw_take(&nonconn, &p);
	a = btw_advertisement(&nonconn, "x");
	CHECK(!a.connectable);
	bt_advertisement_free(&a);
	btw_heard_free(&nonconn);
}

static void errors(void) {
	CHECK(btw_gatt_error(0, -1, 0) == NULL);
	CHECK(is(btw_gatt_error(1, -1, 0), "disconnected"));
	CHECK(is(btw_gatt_error(2, 3, 0), "att 3: write not permitted"));
	CHECK(is(btw_gatt_error(2, -1, 0), "att 14: unlikely error"));
	CHECK(is(btw_gatt_error(3, -1, 0), "denied"));
	CHECK(is(btw_gatt_error(-1, -1, 0x80000013), "disconnected"));
	CHECK(is(btw_gatt_error(-1, -1, 0x80070005), "denied"));
	CHECK(is(btw_gatt_error(-1, -1, 0x80004005), "failed: 0x80004005"));
	CHECK(btw_error(0) == NULL && !strcmp(btw_error(1), "off") && !strcmp(btw_error(7), "denied") && !strncmp(btw_error(6), "not available", 13));
	CHECK(!strcmp(btw_radio_state(1), "on") && !strcmp(btw_radio_state(2), "off") && !strcmp(btw_radio_state(3), "off") &&
	      !strcmp(btw_radio_state(0), "unknown"));
	int v;
	CHECK(btw_cccd(BT_PROP_NOTIFY | BT_PROP_INDICATE, false, &v) && v == 1);
	CHECK(btw_cccd(BT_PROP_NOTIFY | BT_PROP_INDICATE, true, &v) && v == 2);
	CHECK(btw_cccd(BT_PROP_INDICATE, false, &v) && v == 2);
	CHECK(!btw_cccd(BT_PROP_READ, false, &v));
}

static void chooser(void) {
	bt_strings bases = {0};
	bt_strings_push(&bases, asuka_strdup("A"));
	bt_strings_push(&bases, asuka_strdup("B"));
	bt_strings got = btw_chooser_selectors(&bases, NULL);
	CHECK(got.len == 2 && !strcmp(got.data[0], "A") && !strcmp(got.data[1], "B"));
	bt_strings_free(&got);
	bluetooth_Filter nameless[] = {{.name = "x"}, {.name = ""}};
	got = btw_chooser_selectors(&bases, &(bluetooth_list_Filter){nameless, 2});
	CHECK(got.len == 2 && !strcmp(got.data[0], "A"));
	bt_strings_free(&got);
	bluetooth_Filter named[] = {{.name = "Po\"lar"}, {.name = "", .namePrefix = "HR"}};
	got = btw_chooser_selectors(&bases, &(bluetooth_list_Filter){named, 2});
	CHECK(got.len == 4 && !strcmp(got.data[0], "(A) AND System.ItemNameDisplay:=\"Polar\"") && !strcmp(got.data[1], "(B) AND System.ItemNameDisplay:=\"Polar\"") &&
	      !strcmp(got.data[2], "(A) AND System.ItemNameDisplay:~<\"HR\"") && !strcmp(got.data[3], "(B) AND System.ItemNameDisplay:~<\"HR\""));
	bt_strings_free(&got);
	bt_strings_free(&bases);
	bt_strings old = {0}, now = {0}, on = {0}, off = {0};
	bt_strings_push(&old, asuka_strdup("a")), bt_strings_push(&old, asuka_strdup("b"));
	bt_strings_push(&now, asuka_strdup("b")), bt_strings_push(&now, asuka_strdup("c")), bt_strings_push(&now, asuka_strdup("c"));
	btw_diff(&old, &now, &on, &off);
	CHECK(on.len == 1 && !strcmp(on.data[0], "c") && off.len == 1 && !strcmp(off.data[0], "a"));
	bt_strings_free(&old), bt_strings_free(&now), bt_strings_free(&on), bt_strings_free(&off);
}

int main(void) {
	uuids();
	addresses();
	sections();
	heard();
	errors();
	chooser();
	if (!failed) printf("bluetooth: ok\n");
	return failed;
}

// ---- Stand-ins: the engine, the generated glue (never called) ----

const char *bt_engine_state(void) { return NULL; }
void bt_engine_ready(void (*ready)(void *), void *ctx) {}
void bt_engine_request(void (*done)(void *, const char *, const char *), void *ctx) {}
void bt_engine_scan(uint32_t id, bt_strings services, bool duplicates, bt_done done, void *ctx) {}
void bt_engine_stop_scan(uint32_t id) {}
void bt_engine_choose(const bluetooth_list_Filter *f, bt_strings o, void (*done)(void *, const char *, const char *, const char *), void *ctx) {}
void bt_engine_connect(uint32_t conn, const char *device, bool auto_connect, bt_done done, void *ctx) {}
void bt_engine_disconnect(uint32_t conn) {}
void bt_engine_services(uint32_t conn, void (*done)(void *, const bt_services *, const char *), void *ctx) {}
void bt_engine_mtu(uint32_t conn, void (*done)(void *, int, const char *), void *ctx) {}
void bt_engine_rssi(uint32_t conn, void (*done)(void *, int, const char *), void *ctx) {}
void bt_engine_read(uint32_t conn, uint32_t handle, void (*done)(void *, asuka_bytes, const char *), void *ctx) {}
void bt_engine_write(uint32_t conn, uint32_t handle, asuka_bytes value, bool response, bt_done done, void *ctx) {}
void bt_engine_subscribe(uint32_t conn, uint32_t handle, bt_done done, void *ctx) {}
void bt_engine_unsubscribe(uint32_t conn, uint32_t handle) {}
void bt_engine_serve(uint32_t id, const bt_local_service *s, size_t n, bt_done done, void *ctx) {}
void bt_engine_unserve(uint32_t id) {}
void bt_engine_respond(uint32_t server, uint32_t request, asuka_bytes value, int error) {}
void bt_engine_notify(uint32_t server, uint32_t handle, asuka_bytes value) {}
void bt_engine_advertise(uint32_t id, const bt_advertising *a, bt_done done, void *ctx) {}
void bt_engine_stop_advertise(uint32_t id) {}
uintptr_t bt_after(unsigned ms, void (*fn)(void *), void *ctx) { return 0; }
void bt_after_stop(uintptr_t id) {}
void bluetooth_fn_State_call(bluetooth_fn_State f, bluetooth_State p0) {}
void bluetooth_fn_Heard_call(bluetooth_fn_Heard f, const bluetooth_Heard *p0) {}
void bluetooth_fn_Link_call(bluetooth_fn_Link f, const bluetooth_Link *p0) {}
void bluetooth_fn_bytes_call(bluetooth_fn_bytes f, asuka_bytes p0) {}
void bluetooth_fn_Request_call(bluetooth_fn_Request f, const bluetooth_Request *p0) {}
void bluetooth_Adapter_request_done(asuka_call *call, bluetooth_State value) {}
void bluetooth_Adapter_choose_done(asuka_call *call, bluetooth_Device value) {}
void bluetooth_Adapter_connect_done(asuka_call *call) {}
void bluetooth_Adapter_services_done(asuka_call *call, bluetooth_list_FoundService value) {}
void bluetooth_Adapter_mtu_done(asuka_call *call, int64_t value) {}
void bluetooth_Adapter_rssi_done(asuka_call *call, int64_t value) {}
void bluetooth_Adapter_read_done(asuka_call *call, asuka_bytes value) {}
void bluetooth_Adapter_write_done(asuka_call *call) {}
void bluetooth_Adapter_watchRestored_done(asuka_call *call) {}
void bluetooth_Heard_free(bluetooth_Heard *v) {}
