// asuka/bluetooth on Windows and Linux (macOS runs the package's Swift):
// the package's Adapter on the platform's engine (bluetooth.h). Each
// call is the engine's; its events go to the calls waiting for them (a
// scan's advertisements, a connection's loss and its notifications, a
// server's requests). The engine names attributes by handles: a
// connection's are looked up by their service and characteristic (the
// services discovered first when they aren't), a server's are this
// file's. Everything here is on the main thread.
#include "bluetooth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- UUIDs ----

static const uint8_t sig_base[16] = {0, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0x80, 0x5f, 0x9b, 0x34, 0xfb};

static int hexval(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static bool hex_bytes(const char *s, size_t n, uint8_t *out) {
	for (size_t i = 0; i < n; i++) {
		int a = hexval(s[2 * i]), b = hexval(s[2 * i + 1]);
		if (a < 0 || b < 0) return false;
		out[i] = (uint8_t)(a << 4 | b);
	}
	return true;
}

bool bt_parse_uuid(const char *s, uint8_t u[16]) {
	if (!s) return false;
	while (*s == ' ' || *s == '\t') s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	if (*s == '{') s++;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
	if (n > 0 && s[n - 1] == '}') n--;
	char h[33];
	if (n == 4 || n == 8) {
		memcpy(u, sig_base, 16);
		char b[8] = {'0', '0', '0', '0'};
		memcpy(b + 8 - n, s, n);
		return hex_bytes(b, 4, u);
	}
	if (n == 36) {
		if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return false;
		memcpy(h, s, 8), memcpy(h + 8, s + 9, 4), memcpy(h + 12, s + 14, 4), memcpy(h + 16, s + 19, 4), memcpy(h + 20, s + 24, 12);
	} else if (n == 32) {
		memcpy(h, s, 32);
	} else {
		return false;
	}
	return hex_bytes(h, 16, u);
}

static char *format_uuid(const uint8_t u[16]) {
	return asuka_sprintf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9],
	                     u[10], u[11], u[12], u[13], u[14], u[15]);
}

char *bt_normalize(const char *s) {
	uint8_t u[16];
	return bt_parse_uuid(s, u) ? format_uuid(u) : asuka_strdup(s);
}

char *bt_short(const char *s) {
	uint8_t u[16];
	if (!bt_parse_uuid(s, u)) return asuka_strdup(s);
	if (u[0] == 0 && u[1] == 0 && memcmp(u + 4, sig_base + 4, 12) == 0) return asuka_sprintf("%02x%02x", u[2], u[3]);
	return format_uuid(u);
}

// ---- Lists ----

void bt_strings_push(bt_strings *s, char *v) {
	s->data = realloc(s->data, (s->len + 1) * sizeof *s->data);
	s->data[s->len++] = v;
}

void bt_entries_push(bt_entries *e, char *key, const void *p, size_t n) {
	e->data = realloc(e->data, (e->len + 1) * sizeof *e->data);
	e->data[e->len++] = (bt_entry){key, asuka_bytes_copy(p, n)};
}

void bt_strings_free(bt_strings *s) {
	for (size_t i = 0; i < s->len; i++) free(s->data[i]);
	free(s->data);
	*s = (bt_strings){0};
}

void bt_entries_free(bt_entries *e) {
	for (size_t i = 0; i < e->len; i++) {
		free(e->data[i].key);
		free(e->data[i].value.data);
	}
	free(e->data);
	*e = (bt_entries){0};
}

void bt_advertisement_free(bt_advertisement *a) {
	free(a->id);
	free(a->name);
	free(a->local_name);
	bt_strings_free(&a->services);
	bt_strings_free(&a->solicited);
	bt_entries_free(&a->service_data);
	bt_entries_free(&a->manufacturer_data);
}

void bt_services_free(bt_services *s) {
	for (size_t i = 0; i < s->len; i++) {
		bt_service *v = &s->data[i];
		for (size_t j = 0; j < v->ncharacteristics; j++) {
			bt_characteristic *c = &v->characteristics[j];
			for (size_t k = 0; k < c->ndescriptors; k++) free(c->descriptors[k].uuid);
			free(c->descriptors);
			free(c->uuid);
		}
		free(v->characteristics);
		free(v->uuid);
	}
	free(s->data);
	*s = (bt_services){0};
}

// ---- Errors ----

// fail answers a call with the engine's words as the package's error.
static void fail(asuka_call *call, const char *why) {
	asuka_error e = {0};
	static const struct {
		const char *word;
		uint32_t error;
	} words[] = {
	    {BT_OFF, bluetooth_BluetoothError_off},
	    {BT_DENIED, bluetooth_BluetoothError_denied},
	    {BT_UNSUPPORTED, bluetooth_BluetoothError_unsupported},
	    {BT_CANCELED, bluetooth_BluetoothError_canceled},
	    {BT_DISCONNECTED, bluetooth_BluetoothError_disconnected},
	    {BT_NOT_FOUND, bluetooth_BluetoothError_notFound},
	    {BT_TIMEOUT, bluetooth_BluetoothError_timeout},
	};
	if (!why) why = BT_OFF;
	for (size_t i = 0; i < sizeof words / sizeof *words; i++)
		if (strcmp(why, words[i].word) == 0) {
			asuka_error_declared(&e, words[i].error);
			asuka_call_fail(call, &e);
			return;
		}
	if (strncmp(why, "not available", 13) == 0) asuka_error_declared(&e, bluetooth_BluetoothError_notAvailable);
	else asuka_error_failed(&e, "bluetooth", 0, "bluetooth: %s", why);
	asuka_call_fail(call, &e);
}

static void cancelled(asuka_call *call) {
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(call, &e);
}

static bluetooth_State state_of(const char *s) {
	if (!s) return bluetooth_State_unknown;
	if (strcmp(s, BT_ON) == 0) return bluetooth_State_on;
	if (strcmp(s, BT_OFF) == 0) return bluetooth_State_off;
	if (strcmp(s, BT_DENIED) == 0) return bluetooth_State_unauthorized;
	if (strcmp(s, BT_UNSUPPORTED) == 0) return bluetooth_State_unsupported;
	return bluetooth_State_unknown;
}

// ---- What waits for the engine's events ----

typedef struct watcher {
	bluetooth_fn_State changed;
	asuka_call *call;
	struct watcher *next;
} watcher;

typedef struct scan {
	uint32_t id;
	bluetooth_fn_Advertisement found;
	asuka_call *call;
	bool done;  // answered
	struct scan *next;
} scan;

typedef struct {
	char *key;  // "service characteristic", short
	uint32_t handle;
} handle_of;

typedef struct sub {
	uint32_t handle;
	bluetooth_fn_bytes value;
	asuka_call *call;
	bool done;
	struct sub *next;
} sub;

typedef struct conn {
	uint32_t id;
	bluetooth_fn_Link changed;
	asuka_call *call;
	bool up, gone, done;
	uintptr_t timer;
	bool known;  // its handles discovered
	handle_of *handles;
	size_t nhandles;
	sub *subs;
	struct conn *next;
} conn;

typedef struct {
	uint32_t handle;
	char *service, *characteristic;  // short
	char **centrals;                 // subscribed
	size_t ncentrals;
} attr;

typedef struct server {
	uint32_t id;
	bluetooth_fn_Request request;
	asuka_call *call;
	bool done;
	attr *attrs;
	size_t nattrs;
	struct server *next;
} server;

typedef struct advert {
	uint32_t id;
	asuka_call *call;
	bool done;
	struct advert *next;
} advert;

typedef struct request {
	int64_t id;
	uint32_t server, request;
	int offset;
	bool read;
	struct request *next;
} request;

static struct {
	uint32_t next;
	bluetooth_State state;
	watcher *watchers;
	scan *scans;
	conn *conns;
	server *servers;
	advert *adverts;
	request *requests;
	int64_t next_request;
} br = {.state = bluetooth_State_unknown};

static uint32_t next_id(void) { return ++br.next; }

#define FIND(list, field, value, out)                         \
	do {                                                      \
		out = NULL;                                           \
		for (__typeof__(list) p_ = list; p_; p_ = p_->next)   \
			if (p_->field == (value)) {                       \
				out = p_;                                     \
				break;                                        \
			}                                                 \
	} while (0)

#define UNLINK(list, item)                                                   \
	do {                                                                     \
		for (__typeof__(list) *pp_ = &(list); *pp_; pp_ = &(*pp_)->next)     \
			if (*pp_ == (item)) {                                            \
				*pp_ = (item)->next;                                         \
				break;                                                       \
			}                                                                \
	} while (0)

// ---- The state ----

static bluetooth_State state(bluetooth_Adapter *self, asuka_error *err) {
	const char *s = bt_engine_state();
	if (s) br.state = state_of(s);
	return br.state;
}

void bt_on_state(const char *s) {
	br.state = state_of(s);
	for (watcher *w = br.watchers; w; w = w->next) bluetooth_fn_State_call(w->changed, br.state);
}

static void watch_ready(void *p) {
	watcher *w;
	FIND(br.watchers, call, (asuka_call *)p, w);
	if (!w) return;
	const char *s = bt_engine_state();
	if (s) br.state = state_of(s);
	bluetooth_fn_State_call(w->changed, br.state);
}

static void watch_cancel(void *p) {
	watcher *w = p;
	UNLINK(br.watchers, w);
	cancelled(w->call);
	free(w);
}

static void watchState(bluetooth_Adapter *self, bluetooth_fn_State changed, asuka_call *call) {
	watcher *w = calloc(1, sizeof *w);
	*w = (watcher){changed, call, br.watchers};
	br.watchers = w;
	asuka_call_on_cancel(call, watch_cancel, w);
	bt_engine_ready(watch_ready, call);
}

static void requested(void *ctx, const char *s, const char *err) {
	asuka_call *call = ctx;
	if (err) fail(call, err);
	else bluetooth_Adapter_request_done(call, state_of(s));
}

static void request_(bluetooth_Adapter *self, asuka_call *call) { bt_engine_request(requested, call); }

// ---- Scans ----

static void scan_end(scan *s, const char *err) {
	if (s->done) return;
	s->done = true;
	UNLINK(br.scans, s);
	if (err) fail(s->call, err);
	else cancelled(s->call);
	free(s);
}

static void scan_started(void *ctx, const char *err) {
	scan *s;
	FIND(br.scans, id, (uint32_t)(uintptr_t)ctx, s);
	if (s && err) scan_end(s, err);
}

static void scan_cancel(void *p) {
	scan *s = p;
	bt_engine_stop_scan(s->id);
	scan_end(s, NULL);
}

static bt_strings normalized(bluetooth_list_string l) {
	bt_strings out = {0};
	for (size_t i = 0; i < l.len; i++) bt_strings_push(&out, bt_normalize(l.data[i]));
	return out;
}

static void scan_(bluetooth_Adapter *self, const bluetooth_ScanOptions *o, bluetooth_fn_Advertisement found, asuka_call *call) {
	scan *s = calloc(1, sizeof *s);
	*s = (scan){.id = next_id(), .found = found, .call = call, .next = br.scans};
	br.scans = s;
	asuka_call_on_cancel(call, scan_cancel, s);
	bt_strings services = normalized(o->services);
	bt_engine_scan(s->id, services, o->duplicates, scan_started, (void *)(uintptr_t)s->id);
	bt_strings_free(&services);
}

void bt_on_scan_stopped(uint32_t id, const char *err) {
	scan *s;
	FIND(br.scans, id, id, s);
	if (s) scan_end(s, err ? err : BT_OFF);
}

static bluetooth_map_bytes map_of(const bt_entries *e, bool uuids) {
	bluetooth_map_bytes m = {calloc(e->len ? e->len : 1, sizeof(char *)), calloc(e->len ? e->len : 1, sizeof(asuka_bytes)), e->len};
	for (size_t i = 0; i < e->len; i++) {
		m.keys[i] = uuids ? bt_short(e->data[i].key) : asuka_strdup(e->data[i].key);
		m.values[i] = asuka_bytes_copy(e->data[i].value.data, e->data[i].value.len);
	}
	return m;
}

static bluetooth_list_string shorts(const bt_strings *s) {
	bluetooth_list_string l = {calloc(s->len ? s->len : 1, sizeof(char *)), s->len};
	for (size_t i = 0; i < s->len; i++) l.data[i] = bt_short(s->data[i]);
	return l;
}

void bt_on_advertisement(uint32_t id, const bt_advertisement *a) {
	scan *s;
	FIND(br.scans, id, id, s);
	if (!s) return;
	bluetooth_Advertisement ad = {
	    .device = {asuka_strdup(a->id), asuka_strdup(a->name)},
	    .rssi = a->rssi,
	    .localName = asuka_strdup(a->local_name),
	    .services = shorts(&a->services),
	    .solicited = shorts(&a->solicited),
	    .serviceData = map_of(&a->service_data, true),
	    .manufacturerData = map_of(&a->manufacturer_data, false),
	    .txPower = a->tx_power,
	    .hasTxPower = a->has_tx_power,
	    .connectable = a->connectable,
	    .time = a->time_ms * 1000000,
	};
	bluetooth_fn_Advertisement_call(s->found, &ad);
	bluetooth_Advertisement_free(&ad);
}

// ---- Choosing ----

static void chosen(void *ctx, const char *id, const char *name, const char *err) {
	asuka_call *call = ctx;
	if (err) fail(call, err);
	else bluetooth_Adapter_choose_done(call, (bluetooth_Device){asuka_strdup(id), asuka_strdup(name)});
}

static void choose(bluetooth_Adapter *self, bluetooth_list_Filter filters, bluetooth_list_string optional, asuka_call *call) {
	bt_strings o = normalized(optional);
	bt_engine_choose(&filters, o, chosen, call);
	bt_strings_free(&o);
}

// ---- Connections ----

static conn *conn_of(int64_t h) {
	conn *c;
	FIND(br.conns, id, (uint32_t)h, c);
	return c && !c->gone ? c : NULL;
}

static void link_changed(conn *c, bluetooth_ConnectionState st, const char *why) {
	bluetooth_Link l = {st, st == bluetooth_ConnectionState_connected ? c->id : 0, (char *)(why ? why : "")};
	bluetooth_fn_Link_call(c->changed, &l);
}

static void clear_handles(conn *c) {
	for (size_t i = 0; i < c->nhandles; i++) free(c->handles[i].key);
	free(c->handles);
	c->handles = NULL;
	c->nhandles = 0;
	c->known = false;
}

static void sub_end(conn *c, sub *s, const char *err);

// conn_free forgets a connection that ended, answered.
static void conn_free(conn *c) {
	UNLINK(br.conns, c);
	if (c->timer) bt_after_stop(c->timer);
	while (c->subs) sub_end(c, c->subs, BT_DISCONNECTED);
	clear_handles(c);
	free(c);
}

static void connected(void *ctx, const char *err) {
	conn *c;
	FIND(br.conns, id, (uint32_t)(uintptr_t)ctx, c);
	if (!c || c->done) return;
	if (c->timer) bt_after_stop(c->timer);
	c->timer = 0;
	if (err) {
		c->done = true;
		fail(c->call, err);
		conn_free(c);
		return;
	}
	c->up = true;
	link_changed(c, bluetooth_ConnectionState_connected, NULL);
}

static void connect_timeout(void *p) {
	conn *c;
	FIND(br.conns, id, (uint32_t)(uintptr_t)p, c);
	if (!c || c->done || c->up) return;
	c->timer = 0;
	c->done = true;
	bt_engine_disconnect(c->id);  // ends the attempt
	fail(c->call, BT_TIMEOUT);
	conn_free(c);
}

static void connect_cancel(void *p) {
	conn *c = p;
	if (c->done) return;
	c->done = true;
	bt_engine_disconnect(c->id);
	cancelled(c->call);
	conn_free(c);
}

static void connect_(bluetooth_Adapter *self, const bluetooth_Device *d, const bluetooth_ConnectOptions *o, bluetooth_fn_Link changed, asuka_call *call) {
	conn *c = calloc(1, sizeof *c);
	*c = (conn){.id = next_id(), .changed = changed, .call = call, .next = br.conns};
	br.conns = c;
	link_changed(c, bluetooth_ConnectionState_connecting, NULL);
	asuka_call_on_cancel(call, connect_cancel, c);
	if (o->timeout > 0 && !o->autoConnect) {
		int64_t ms = o->timeout / 1000000;
		c->timer = bt_after((unsigned)(ms > 0 ? ms : 1), connect_timeout, (void *)(uintptr_t)c->id);
	}
	bt_engine_connect(c->id, d->id, o->autoConnect, connected, (void *)(uintptr_t)c->id);
}

void bt_on_disconnected(uint32_t id, const char *reason, const char *err) {
	conn *c;
	FIND(br.conns, id, id, c);
	if (!c || c->done) return;
	char *why = reason && strcmp(reason, BT_REASON_OFF) == 0 ? asuka_strdup("bluetooth: off")
	            : err                                          ? asuka_sprintf("bluetooth: %s", err)
	                                                           : asuka_sprintf("bluetooth: disconnected (%s)", reason ? reason : BT_REASON_UNKNOWN);
	c->gone = true;
	c->done = true;
	while (c->subs) sub_end(c, c->subs, BT_DISCONNECTED);
	link_changed(c, bluetooth_ConnectionState_disconnected, why);
	free(why);
	bluetooth_Adapter_connect_done(c->call);
	conn_free(c);
}

void bt_on_services_changed(uint32_t id) {
	conn *c = conn_of(id);
	if (c) clear_handles(c);  // discovered again as they're next used
}

// ---- Services ----

static bluetooth_list_Property properties(int bits) {
	static const int order[] = {BT_PROP_BROADCAST, BT_PROP_READ, BT_PROP_WRITE_NO_RESPONSE, BT_PROP_WRITE, BT_PROP_NOTIFY, BT_PROP_INDICATE, BT_PROP_SIGNED_WRITE, BT_PROP_EXTENDED};
	bluetooth_list_Property l = {calloc(8, sizeof(bluetooth_Property)), 0};
	for (int i = 0; i < 8; i++)
		if (bits & order[i]) l.data[l.len++] = i;  // the package's Property is ATT's bit's place
	return l;
}

static void learn(conn *c, const bt_services *tree) {
	clear_handles(c);
	c->known = true;
	for (size_t i = 0; i < tree->len; i++) {
		char *svc = bt_short(tree->data[i].uuid);
		for (size_t j = 0; j < tree->data[i].ncharacteristics; j++) {
			const bt_characteristic *ch = &tree->data[i].characteristics[j];
			char *chr = bt_short(ch->uuid);
			char *key = asuka_sprintf("%s %s", svc, chr);
			free(chr);
			bool seen = false;
			for (size_t k = 0; k < c->nhandles && !seen; k++) seen = strcmp(c->handles[k].key, key) == 0;
			if (seen) {
				free(key);
				continue;
			}
			c->handles = realloc(c->handles, (c->nhandles + 1) * sizeof *c->handles);
			c->handles[c->nhandles++] = (handle_of){key, ch->handle};
		}
		free(svc);
	}
}

static void services_found(void *ctx, const bt_services *tree, const char *err) {
	asuka_call *call = ((void **)ctx)[0];
	uint32_t id = (uint32_t)(uintptr_t)((void **)ctx)[1];
	free(ctx);
	if (err) {
		fail(call, err);
		return;
	}
	bluetooth_list_FoundService out = {calloc(tree->len ? tree->len : 1, sizeof(bluetooth_FoundService)), tree->len};
	for (size_t i = 0; i < tree->len; i++) {
		const bt_service *s = &tree->data[i];
		bluetooth_FoundService *fs = &out.data[i];
		fs->uuid = bt_short(s->uuid);
		fs->characteristics = (bluetooth_list_FoundCharacteristic){calloc(s->ncharacteristics ? s->ncharacteristics : 1, sizeof(bluetooth_FoundCharacteristic)), s->ncharacteristics};
		for (size_t j = 0; j < s->ncharacteristics; j++) {
			const bt_characteristic *ch = &s->characteristics[j];
			bluetooth_FoundCharacteristic *fc = &fs->characteristics.data[j];
			fc->uuid = bt_short(ch->uuid);
			fc->properties = properties(ch->properties);
			fc->descriptors = (bluetooth_list_string){calloc(ch->ndescriptors ? ch->ndescriptors : 1, sizeof(char *)), ch->ndescriptors};
			for (size_t k = 0; k < ch->ndescriptors; k++) fc->descriptors.data[k] = bt_short(ch->descriptors[k].uuid);
		}
	}
	conn *c = conn_of(id);
	if (c) learn(c, tree);
	bluetooth_Adapter_services_done(call, out);
}

static void services(bluetooth_Adapter *self, int64_t h, asuka_call *call) {
	if (!conn_of(h)) {
		fail(call, BT_DISCONNECTED);
		return;
	}
	void **ctx = malloc(2 * sizeof *ctx);
	ctx[0] = call, ctx[1] = (void *)(uintptr_t)h;
	bt_engine_services((uint32_t)h, services_found, ctx);
}

static void mtu_got(void *ctx, int v, const char *err) {
	if (err) fail(ctx, err);
	else bluetooth_Adapter_mtu_done(ctx, v);
}

static void mtu(bluetooth_Adapter *self, int64_t h, asuka_call *call) {
	if (!conn_of(h)) fail(call, BT_DISCONNECTED);
	else bt_engine_mtu((uint32_t)h, mtu_got, call);
}

static void rssi_got(void *ctx, int v, const char *err) {
	if (err) fail(ctx, err);
	else bluetooth_Adapter_rssi_done(ctx, v);
}

static void rssi(bluetooth_Adapter *self, int64_t h, asuka_call *call) {
	if (!conn_of(h)) fail(call, BT_DISCONNECTED);
	else bt_engine_rssi((uint32_t)h, rssi_got, call);
}

// ---- Attributes: by their service and characteristic ----

enum { OP_READ, OP_WRITE, OP_SUBSCRIBE };

// An attribute's call: its handle looked up first (the services
// discovered first when they aren't).
typedef struct {
	int kind;
	uint32_t conn;
	char *key;
	asuka_call *call;
	asuka_bytes value;
	bool response;
	bluetooth_fn_bytes notified;
} op;

static void op_free(op *o) {
	free(o->key);
	free(o->value.data);
	free(o);
}

static void sub_cancel(void *p);

static void op_run(op *o, uint32_t handle);

static uint32_t lookup(conn *c, const char *key) {
	for (size_t i = 0; i < c->nhandles; i++)
		if (strcmp(c->handles[i].key, key) == 0) return c->handles[i].handle;
	return 0;
}

static void op_discovered(void *ctx, const bt_services *tree, const char *err) {
	op *o = ctx;
	conn *c = conn_of(o->conn);
	if (err || !c) {
		fail(o->call, err ? err : BT_DISCONNECTED);
		op_free(o);
		return;
	}
	learn(c, tree);
	uint32_t h = lookup(c, o->key);
	if (!h) {
		fail(o->call, BT_NOT_FOUND);
		op_free(o);
		return;
	}
	op_run(o, h);
}

static void op_start(int kind, int64_t conn_id, const char *service, const char *characteristic, asuka_call *call, op *o) {
	conn *c = conn_of(conn_id);
	if (!c) {
		fail(call, BT_DISCONNECTED);
		op_free(o);
		return;
	}
	char *s = bt_short(service), *ch = bt_short(characteristic);
	o->kind = kind, o->conn = (uint32_t)conn_id, o->call = call, o->key = asuka_sprintf("%s %s", s, ch);
	free(s);
	free(ch);
	if (!c->known) {
		bt_engine_services(o->conn, op_discovered, o);
		return;
	}
	uint32_t h = lookup(c, o->key);
	if (!h) {
		fail(call, BT_NOT_FOUND);
		op_free(o);
		return;
	}
	op_run(o, h);
}

static void read_got(void *ctx, asuka_bytes v, const char *err) {
	if (err) fail(ctx, err);
	else bluetooth_Adapter_read_done(ctx, asuka_bytes_copy(v.data, v.len));
}

static void written(void *ctx, const char *err) {
	if (err) fail(ctx, err);
	else bluetooth_Adapter_write_done(ctx);
}

static void subscribed(void *ctx, const char *err) {
	conn *c = NULL;
	sub *s = NULL;
	for (conn *cc = br.conns; cc && !s; cc = cc->next)
		for (sub *x = cc->subs; x; x = x->next)
			if (x == ctx) s = x, c = cc;
	if (s && err) sub_end(c, s, err);
}

static void op_run(op *o, uint32_t handle) {
	switch (o->kind) {
	case OP_READ:
		bt_engine_read(o->conn, handle, read_got, o->call);
		break;
	case OP_WRITE:
		bt_engine_write(o->conn, handle, o->value, o->response, written, o->call);
		break;
	case OP_SUBSCRIBE: {
		conn *c = conn_of(o->conn);
		sub *s = calloc(1, sizeof *s);
		*s = (sub){.handle = handle, .value = o->notified, .call = o->call, .next = c->subs};
		c->subs = s;
		asuka_call_on_cancel(o->call, sub_cancel, s);
		bt_engine_subscribe(o->conn, handle, subscribed, s);
		break;
	}
	}
	op_free(o);
}

static void read_(bluetooth_Adapter *self, int64_t h, const char *service, const char *characteristic, asuka_call *call) {
	op_start(OP_READ, h, service, characteristic, call, calloc(1, sizeof(op)));
}

static void write_(bluetooth_Adapter *self, int64_t h, const char *service, const char *characteristic, asuka_bytes value, bool response, asuka_call *call) {
	op *o = calloc(1, sizeof *o);
	o->value = asuka_bytes_copy(value.data, value.len);
	o->response = response;
	op_start(OP_WRITE, h, service, characteristic, call, o);
}

// sub_end ends a subscription: with err (NULL: cancelled).
static void sub_end(conn *c, sub *s, const char *err) {
	if (s->done) return;
	s->done = true;
	for (sub **p = &c->subs; *p; p = &(*p)->next)
		if (*p == s) {
			*p = s->next;
			break;
		}
	if (err) fail(s->call, err);
	else cancelled(s->call);
	free(s);
}

static void sub_cancel(void *p) {
	for (conn *c = br.conns; c; c = c->next)
		for (sub *s = c->subs; s; s = s->next)
			if (s == p) {
				bt_engine_unsubscribe(c->id, s->handle);
				sub_end(c, s, NULL);
				return;
			}
}

static void subscribe_(bluetooth_Adapter *self, int64_t h, const char *service, const char *characteristic, bluetooth_fn_bytes value, asuka_call *call) {
	op *o = calloc(1, sizeof *o);
	o->notified = value;
	op_start(OP_SUBSCRIBE, h, service, characteristic, call, o);
}

void bt_on_value(uint32_t id, uint32_t handle, asuka_bytes value) {
	conn *c = conn_of(id);
	if (!c) return;
	for (sub *s = c->subs; s; s = s->next)
		if (s->handle == handle) bluetooth_fn_bytes_call(s->value, value);
}

// ---- The peripheral ----

static int property_mask(bluetooth_list_Property ps) {
	int m = 0;
	for (size_t i = 0; i < ps.len; i++)
		if (ps.data[i] >= 0 && ps.data[i] < 8) m |= 1 << ps.data[i];
	return m;
}

static int permission_mask(bluetooth_list_Permission ps) {
	int m = 0;
	for (size_t i = 0; i < ps.len; i++)
		if (ps.data[i] >= 0 && ps.data[i] < 4) m |= 1 << ps.data[i];  // readable, writeable, readEncrypted, writeEncrypted
	return m;
}

static void requests_drop(uint32_t server_id) {
	for (request **p = &br.requests; *p;) {
		if ((*p)->server == server_id) {
			request *r = *p;
			*p = r->next;
			free(r);
		} else {
			p = &(*p)->next;
		}
	}
}

static void server_free(server *s) {
	UNLINK(br.servers, s);
	requests_drop(s->id);
	for (size_t i = 0; i < s->nattrs; i++) {
		free(s->attrs[i].service);
		free(s->attrs[i].characteristic);
		for (size_t k = 0; k < s->attrs[i].ncentrals; k++) free(s->attrs[i].centrals[k]);
		free(s->attrs[i].centrals);
	}
	free(s->attrs);
	free(s);
}

static void server_end(server *s, const char *err) {
	if (s->done) return;
	s->done = true;
	if (err) fail(s->call, err);
	else cancelled(s->call);
	server_free(s);
}

static void served(void *ctx, const char *err) {
	server *s;
	FIND(br.servers, id, (uint32_t)(uintptr_t)ctx, s);
	if (s && err) server_end(s, err);
}

static void serve_cancel(void *p) {
	server *s = p;
	bt_engine_unserve(s->id);
	server_end(s, NULL);
}

static void serve(bluetooth_Adapter *self, bluetooth_list_ServedService services, bluetooth_fn_Request request, asuka_call *call) {
	server *s = calloc(1, sizeof *s);
	*s = (server){.id = next_id(), .request = request, .call = call, .next = br.servers};
	br.servers = s;
	bt_local_service *ls = calloc(services.len ? services.len : 1, sizeof *ls);
	uint32_t handle = 0;
	for (size_t i = 0; i < services.len; i++) {
		const bluetooth_ServedService *ss = &services.data[i];
		ls[i] = (bt_local_service){bt_normalize(ss->uuid), ss->primary, calloc(ss->characteristics.len ? ss->characteristics.len : 1, sizeof(bt_local_characteristic)),
		                           ss->characteristics.len};
		for (size_t j = 0; j < ss->characteristics.len; j++) {
			const bluetooth_ServedCharacteristic *c = &ss->characteristics.data[j];
			handle++;
			s->attrs = realloc(s->attrs, (s->nattrs + 1) * sizeof *s->attrs);
			s->attrs[s->nattrs++] = (attr){handle, bt_short(ss->uuid), bt_short(c->uuid), NULL, 0};
			bt_local_characteristic *lc = &ls[i].characteristics[j];
			*lc = (bt_local_characteristic){handle, bt_normalize(c->uuid), property_mask(c->properties), permission_mask(c->permissions)};
			if (!c->dynamic && c->value.len > 0) {
				lc->has_value = true;
				lc->value = c->value;  // the call's, during it
			}
		}
	}
	asuka_call_on_cancel(call, serve_cancel, s);
	bt_engine_serve(s->id, ls, services.len, served, (void *)(uintptr_t)s->id);
	for (size_t i = 0; i < services.len; i++) {
		for (size_t j = 0; j < ls[i].ncharacteristics; j++) free(ls[i].characteristics[j].uuid);
		free(ls[i].characteristics);
		free(ls[i].uuid);
	}
	free(ls);
}

void bt_on_server_stopped(uint32_t id, const char *err) {
	server *s;
	FIND(br.servers, id, id, s);
	if (s) server_end(s, err ? err : BT_OFF);
}

static attr *attr_of(server *s, uint32_t handle) {
	for (size_t i = 0; i < s->nattrs; i++)
		if (s->attrs[i].handle == handle) return &s->attrs[i];
	return NULL;
}

static void raise_request(server *s, uint32_t request_id, bool read, uint32_t handle, const bt_central *central, asuka_bytes value, int offset,
                          bool answered) {
	attr *a = attr_of(s, handle);
	if (!a) {
		if (answered) bt_engine_respond(s->id, request_id, (asuka_bytes){0}, 0x0a);  // attribute not found
		return;
	}
	bluetooth_Request r = {.id = ++br.next_request, .kind = read ? bluetooth_RequestKind_read : bluetooth_RequestKind_write,
	                       .central = central ? central->id : (char *)"", .service = a->service, .characteristic = a->characteristic, .value = value,
	                       .offset = offset};
	if (answered) {  // a write without response is answered by no one
		request *q = calloc(1, sizeof *q);
		*q = (request){r.id, s->id, request_id, offset, read, br.requests};
		br.requests = q;
	}
	bluetooth_fn_Request_call(s->request, &r);
}

void bt_on_read(uint32_t server_id, uint32_t request_id, uint32_t handle, const bt_central *central, int offset) {
	server *s;
	FIND(br.servers, id, server_id, s);
	if (s) raise_request(s, request_id, true, handle, central, (asuka_bytes){0}, offset, true);
	else bt_engine_respond(server_id, request_id, (asuka_bytes){0}, 0x0e);
}

void bt_on_write(uint32_t server_id, uint32_t request_id, uint32_t handle, const bt_central *central, asuka_bytes value, int offset) {
	server *s;
	FIND(br.servers, id, server_id, s);
	if (s) raise_request(s, request_id, false, handle, central, value, offset, request_id != 0);
	else if (request_id) bt_engine_respond(server_id, request_id, (asuka_bytes){0}, 0x0e);
}

void bt_on_subscription(uint32_t server_id, uint32_t handle, const bt_central *central, bool on) {
	server *s;
	FIND(br.servers, id, server_id, s);
	attr *a = s ? attr_of(s, handle) : NULL;
	if (!a || !central) return;
	size_t k = 0;
	while (k < a->ncentrals && strcmp(a->centrals[k], central->id) != 0) k++;
	if (on && k == a->ncentrals) {
		a->centrals = realloc(a->centrals, (a->ncentrals + 1) * sizeof *a->centrals);
		a->centrals[a->ncentrals++] = asuka_strdup(central->id);
	} else if (!on && k < a->ncentrals) {
		free(a->centrals[k]);
		a->centrals[k] = a->centrals[--a->ncentrals];
	}
	bluetooth_Request r = {.id = ++br.next_request, .kind = on ? bluetooth_RequestKind_subscribe : bluetooth_RequestKind_unsubscribe,
	                       .central = central->id, .service = a->service, .characteristic = a->characteristic};
	bluetooth_fn_Request_call(s->request, &r);
}

static void respond(bluetooth_Adapter *self, int64_t id, asuka_bytes value, int64_t error, asuka_error *err) {
	request *r;
	FIND(br.requests, id, id, r);
	if (!r) return;
	UNLINK(br.requests, r);
	// A read's answer is the value from its offset on.
	if (r->read && error == 0 && r->offset > 0) {
		if ((size_t)r->offset > value.len) {
			error = 0x07;  // invalid offset
			value = (asuka_bytes){0};
		} else {
			value.data += r->offset;
			value.len -= (size_t)r->offset;
		}
	}
	bt_engine_respond(r->server, r->request, value, (int)error);
	free(r);
}

static int64_t notify(bluetooth_Adapter *self, const char *service, const char *characteristic, asuka_bytes value, asuka_error *err) {
	char *s = bt_short(service), *c = bt_short(characteristic);
	int64_t n = 0;
	for (server *sv = br.servers; sv; sv = sv->next)
		for (size_t i = 0; i < sv->nattrs; i++) {
			attr *a = &sv->attrs[i];
			if (strcmp(a->service, s) != 0 || strcmp(a->characteristic, c) != 0 || a->ncentrals == 0) continue;
			n += (int64_t)a->ncentrals;
			bt_engine_notify(sv->id, a->handle, value);
		}
	free(s);
	free(c);
	return n;
}

// ---- Advertising ----

static void advert_end(advert *a, const char *err) {
	if (a->done) return;
	a->done = true;
	UNLINK(br.adverts, a);
	if (err) fail(a->call, err);
	else cancelled(a->call);
	free(a);
}

static void advertising(void *ctx, const char *err) {
	advert *a;
	FIND(br.adverts, id, (uint32_t)(uintptr_t)ctx, a);
	if (a && err) advert_end(a, err);
}

static void advertise_cancel(void *p) {
	advert *a = p;
	bt_engine_stop_advertise(a->id);
	advert_end(a, NULL);
}

static bt_entries entries(const bluetooth_map_bytes *m, bool uuids) {
	bt_entries e = {0};
	for (size_t i = 0; i < m->len; i++) bt_entries_push(&e, uuids ? bt_normalize(m->keys[i]) : asuka_strdup(m->keys[i]), m->values[i].data, m->values[i].len);
	return e;
}

static void advertise(bluetooth_Adapter *self, const bluetooth_Advertising *o, asuka_call *call) {
	advert *a = calloc(1, sizeof *a);
	*a = (advert){.id = next_id(), .call = call, .next = br.adverts};
	br.adverts = a;
	bt_advertising args = {o->localName, normalized(o->services), entries(&o->serviceData, true), entries(&o->manufacturerData, false), o->connectable,
	                       o->includeTxPower};
	asuka_call_on_cancel(call, advertise_cancel, a);
	bt_engine_advertise(a->id, &args, advertising, (void *)(uintptr_t)a->id);
	bt_strings_free(&args.services);
	bt_entries_free(&args.service_data);
	bt_entries_free(&args.manufacturer_data);
}

void bt_on_advertise_stopped(uint32_t id, const char *err) {
	advert *a;
	FIND(br.adverts, id, id, a);
	if (a) advert_end(a, err ? err : BT_OFF);
}

// ---- The adapter ----

static const bluetooth_Adapter_vtable vt = {
    .state = state,
    .watchState = watchState,
    .request = request_,
    .scan = scan_,
    .choose = choose,
    .connect = connect_,
    .services = services,
    .mtu = mtu,
    .rssi = rssi,
    .read = read_,
    .write = write_,
    .subscribe = subscribe_,
    .serve = serve,
    .respond = respond,
    .notify = notify,
    .advertise = advertise,
};

static void release(bluetooth_Adapter *self) {}  // the one adapter, the process's

bluetooth_Adapter *bluetooth_make_Adapter(asuka_error *err) {
	static bluetooth_Adapter_vtable v;
	static bluetooth_Adapter a;
	v = vt;
	v.release = release;
	a.vt = &v;
	return &a;
}
