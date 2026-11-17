// central_linux.c: the central role on BlueZ: scans through the
// adapter's discovery, connections through the devices' Connect, their
// GATT attributes through BlueZ's objects of them.
#include "bluetooth_linux.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ---- Deferred answers of each kind ----

typedef struct {
	void (*done)(void *ctx, const bt_services *s, const char *err);
	void *ctx;
	bt_services tree;
	char *err;
} services_answer;

static void run_services(void *p) {
	services_answer *a = p;
	a->done(a->ctx, &a->tree, a->err);
	bt_services_free(&a->tree);
	free(a->err);
	free(a);
}

typedef struct {
	void (*done)(void *ctx, asuka_bytes v, const char *err);
	void *ctx;
	asuka_bytes v;
	char *err;
} bytes_answer;

static void run_bytes(void *p) {
	bytes_answer *a = p;
	a->done(a->ctx, a->v, a->err);
	free(a->v.data);
	free(a->err);
	free(a);
}

static void answer_bytes(void (*done)(void *, asuka_bytes, const char *), void *ctx, asuka_bytes v, const char *err) {
	bytes_answer *a = malloc(sizeof *a);
	*a = (bytes_answer){done, ctx, asuka_bytes_copy(v.data, v.len), err ? asuka_strdup(err) : NULL};
	bl_defer(run_bytes, a);
}

typedef struct {
	void (*done)(void *ctx, int v, const char *err);
	void *ctx;
	int v;
	char *err;
} int_answer;

static void run_int(void *p) {
	int_answer *a = p;
	a->done(a->ctx, a->v, a->err);
	free(a->err);
	free(a);
}

static void answer_int(void (*done)(void *, int, const char *), void *ctx, int v, const char *err) {
	int_answer *a = malloc(sizeof *a);
	*a = (int_answer){done, ctx, v, err ? asuka_strdup(err) : NULL};
	bl_defer(run_int, a);
}

// ---- Sets of strings ----

typedef struct {
	char **v;
	size_t n;
} set;

static bool set_has(const set *s, const char *x) {
	for (size_t i = 0; i < s->n; i++)
		if (!strcmp(s->v[i], x)) return true;
	return false;
}

static void set_add(set *s, const char *x) {
	if (set_has(s, x)) return;
	s->v = realloc(s->v, (s->n + 1) * sizeof *s->v);
	s->v[s->n++] = asuka_strdup(x);
}

static void set_del(set *s, const char *x) {
	for (size_t i = 0; i < s->n; i++)
		if (!strcmp(s->v[i], x)) {
			free(s->v[i]);
			s->v[i] = s->v[--s->n];
			return;
		}
}

static void set_clear(set *s) {
	for (size_t i = 0; i < s->n; i++) free(s->v[i]);
	free(s->v);
	*s = (set){0};
}

// ---- Scans ----

typedef struct scan {
	uint32_t id;
	bt_strings services;  // normalized
	bool duplicates;
	bt_done done;  // while starting
	void *ctx;
	bool running;  // answered: discovery runs with a filter that has it
	set seen;      // the devices it heard (by path)
	struct scan *next;
} scan;

static scan *scans;

// The adapter's discovery, as the app's scans want it: one call to
// BlueZ at a time.
static struct {
	bool busy, on;
	char *filter;  // the filter set (NULL: none)
	char *adapter;
} disc;

static void reconcile(void);

static scan *scan_of(uint32_t id) {
	for (scan *s = scans; s; s = s->next)
		if (s->id == id) return s;
	return NULL;
}

static void scan_free(scan *s) {
	for (scan **p = &scans; *p; p = &(*p)->next)
		if (*p == s) {
			*p = s->next;
			break;
		}
	bt_strings_free(&s->services);
	set_clear(&s->seen);
	free(s);
}

typedef struct {
	uint32_t id;
	bt_strings services;
	bool duplicates;
	bt_done done;
	void *ctx;
} scan_args;

static void start_scan(void *p) {
	scan_args *a = p;
	const char *st = bl_current();
	if (strcmp(st, BT_ON) != 0) {
		bl_answer(a->done, a->ctx, st);
		bt_strings_free(&a->services);
	} else {
		scan *s = calloc(1, sizeof *s);
		*s = (scan){.id = a->id, .services = a->services, .duplicates = a->duplicates, .done = a->done, .ctx = a->ctx};
		// In id order (the order they came).
		scan **at = &scans;
		while (*at) at = &(*at)->next;
		*at = s;
		reconcile();
	}
	free(a);
	bl_flush();
}

void bt_engine_scan(uint32_t id, bt_strings services, bool duplicates, bt_done done, void *ctx) {
	scan_args *a = malloc(sizeof *a);
	*a = (scan_args){id, {0}, duplicates, done, ctx};
	for (size_t i = 0; i < services.len; i++) bt_strings_push(&a->services, bt_normalize(services.data[i]));
	bl_start(start_scan, a);
}

void bt_engine_stop_scan(uint32_t id) {
	scan *s = scan_of(id);
	if (!s) return;
	if (s->done) bl_answer(s->done, s->ctx, NULL);
	scan_free(s);
	reconcile();
	bl_flush();
}

typedef struct {
	uint32_t id;
	char *err;
} stopped_ctx;

static void run_scan_stopped(void *p) {
	stopped_ctx *c = p;
	bt_on_scan_stopped(c->id, c->err);
	free(c->err);
	free(c);
}

// end_scan ends a scan the platform stopped, with why.
static void end_scan(scan *s, const char *why) {
	if (s->done) {
		bl_answer(s->done, s->ctx, why);
	} else {
		stopped_ctx *c = malloc(sizeof *c);
		*c = (stopped_ctx){s->id, asuka_strdup(why)};
		bl_defer(run_scan_stopped, c);
	}
	scan_free(s);
}

static void fail_pending_scans(const char *why) {
	for (scan *s = scans, *next; s; s = next) {
		next = s->next;
		if (!s->running) end_scan(s, why);
	}
}

typedef struct {
	uint32_t scan;
	bt_advertisement a;
} heard_ctx;

static void run_heard(void *p) {
	heard_ctx *h = p;
	bt_on_advertisement(h->scan, &h->a);
	bt_advertisement_free(&h->a);
	free(h);
}

static void tell_heard(uint32_t scan_id, const bl_device *d) {
	heard_ctx *h = malloc(sizeof *h);
	h->scan = scan_id;
	h->a = bl_advertisement_of(d);
	bl_defer(run_heard, h);
}

// heard_now tells a scan starting the devices the adapter hears now: a
// device with an RSSI was heard in the discovery running (BlueZ drops
// the RSSIs as discovery stops).
static void heard_now(scan *s) {
	bt_strings paths = bl_paths(BL_IF_DEVICE, NULL);
	for (size_t i = 0; i < paths.len; i++) {
		bl_iface *p = bl_props(paths.data[i], BL_IF_DEVICE);
		if (!bl_prop(p, "RSSI")) continue;
		bl_device d = bl_parse_device(p);
		if (d.adapter && disc.adapter && !strcmp(d.adapter, disc.adapter) && bl_matches(&d, &s->services)) {
			set_add(&s->seen, paths.data[i]);
			tell_heard(s->id, &d);
		}
		bl_device_free(&d);
	}
	bt_strings_free(&paths);
}

static void discovery_stopped(void *ctx, GVariant *r, const char *name, const char *msg) {
	disc.busy = disc.on = false;
	reconcile();
}

static void filter_set(void *ctx, GVariant *r, const char *name, const char *msg) {
	disc.busy = false;
	// A filter refused is the attempt's still: the scans running keep the
	// one before, and each scan matches its own advertisements anyway.
	free(disc.filter);
	disc.filter = ctx;
	if (!r) {
		char *why = bl_client_error(name, msg, false);
		fail_pending_scans(why);
		free(why);
	}
	reconcile();
}

static void discovery_started(void *ctx, GVariant *r, const char *name, const char *msg) {
	disc.busy = false;
	if (!r && !(name && !strcmp(name, "org.bluez.Error.InProgress"))) {  // already discovering: ours
		char *why = bl_client_error(name, msg, false);
		fail_pending_scans(why);
		free(why);
		reconcile();
		return;
	}
	disc.on = true;
	reconcile();
}

// reconcile brings the adapter's discovery to what the scans want: the
// union of their filters set, discovery started while any runs, stopped
// once none does; then the scans waiting start.
static void reconcile(void) {
	if (disc.busy) return;
	if (!scans) {
		if (disc.on) {
			disc.busy = true;
			bl_call_bluez(disc.adapter, BL_IF_ADAPTER, "StopDiscovery", NULL, discovery_stopped, NULL);
		}
		return;
	}
	const char *adapter = bl_adapter();
	if (!adapter) {
		fail_pending_scans(BT_UNSUPPORTED);
		return;
	}
	if (!disc.adapter || strcmp(disc.adapter, adapter) != 0) {
		free(disc.adapter);
		free(disc.filter);
		disc.adapter = asuka_strdup(adapter);
		disc.filter = NULL;
		disc.on = false;
	}
	// The union of the scans' services (none if one wants every device).
	set uuids = {0};
	bool all = false, dup = false;
	for (scan *s = scans; s; s = s->next) {
		dup = dup || s->duplicates;
		if (s->services.len == 0) all = true;
		for (size_t i = 0; i < s->services.len; i++) set_add(&uuids, s->services.data[i]);
	}
	if (all) set_clear(&uuids);
	for (size_t i = 1; i < uuids.n; i++)  // sorted
		for (size_t j = i; j > 0 && strcmp(uuids.v[j - 1], uuids.v[j]) > 0; j--) {
			char *t = uuids.v[j];
			uuids.v[j] = uuids.v[j - 1], uuids.v[j - 1] = t;
		}
	asuka_writer key = {0};
	for (size_t i = 0; i < uuids.n; i++) {
		if (i) asuka_writer_put(&key, ",", 1);
		asuka_writer_put(&key, uuids.v[i], strlen(uuids.v[i]));
	}
	if (dup) asuka_writer_put(&key, "+dup", 4);
	asuka_writer_u8(&key, 0);
	if (!disc.filter || strcmp((char *)key.data, disc.filter) != 0) {
		GVariantBuilder b;
		G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
		G(g_variant_builder_add)(&b, "{sv}", "Transport", G(g_variant_new_string)("le"));
		G(g_variant_builder_add)(&b, "{sv}", "DuplicateData", G(g_variant_new_boolean)(dup));
		if (uuids.n) G(g_variant_builder_add)(&b, "{sv}", "UUIDs", bl_string_array((const char *const *)uuids.v, uuids.n));
		GVariant *params = G(g_variant_new)("(@a{sv})", G(g_variant_builder_end)(&b));
		disc.busy = true;
		bl_call_bluez(adapter, BL_IF_ADAPTER, "SetDiscoveryFilter", params, filter_set, key.data);
		set_clear(&uuids);
		return;
	}
	asuka_writer_free(&key);
	set_clear(&uuids);
	if (!disc.on) {
		disc.busy = true;
		bl_call_bluez(adapter, BL_IF_ADAPTER, "StartDiscovery", NULL, discovery_started, NULL);
		return;
	}
	for (scan *s = scans; s; s = s->next) {
		if (s->running) continue;
		s->running = true;
		bl_answer(s->done, s->ctx, NULL);
		s->done = NULL;
		// The devices heard in this discovery (another app's, another
		// scan's) are heard: BlueZ tells only what changes.
		heard_now(s);
	}
}

// bl_heard tells the scans an advertisement of the device at path, if
// what changed is one: BlueZ caches the devices it ever heard, so a
// device is heard when its RSSI comes (or changes), or, for a scan that
// wants duplicates, what it advertises changes.
void bl_heard(const char *path, GVariant *changed, bool added) {
	if (!scans || !disc.on) return;
	bl_iface *p = bl_props(path, BL_IF_DEVICE);
	if (!p || !bl_prop(p, "RSSI")) return;
	static const char *data_keys[] = {"ManufacturerData", "ServiceData", "UUIDs", "TxPower", "Name"};
	GVariant *v = G(g_variant_lookup_value)(changed, "RSSI", NULL);
	bool rssi = v != NULL, data = false;
	if (v) G(g_variant_unref)(v);
	for (size_t i = 0; i < 5 && !data; i++)
		if ((v = G(g_variant_lookup_value)(changed, data_keys[i], NULL))) data = true, G(g_variant_unref)(v);
	if (!added && !rssi && !data) return;
	bl_device d = bl_parse_device(p);
	if (d.adapter && disc.adapter && !strcmp(d.adapter, disc.adapter)) {
		for (scan *s = scans; s; s = s->next) {
			if (!s->running || !bl_matches(&d, &s->services)) continue;
			if (set_has(&s->seen, path) && !s->duplicates) continue;
			set_add(&s->seen, path);
			tell_heard(s->id, &d);
		}
	}
	bl_device_free(&d);
}

// ---- Connections ----

typedef struct conn conn;

// A call waiting on a connection's attribute.
enum { CALL_READ, CALL_WRITE, CALL_SUBSCRIBE };

typedef struct pending {
	uint32_t id;
	int kind;
	void *done;
	void *ctx;
	struct pending *next;
} pending;

typedef struct dev {
	char *path;
	bool calling;  // Device1.Connect asked
	bool ours;     // the app connected it (Disconnect once its last connection goes)
	// The Disconnected signal's reason and words, for the disconnection
	// (Connected false) that follows.
	char *reason, *words;
	set notifying;  // characteristics StartNotify'd (by path)
	uintptr_t retry;
	struct dev *next;
} dev;

typedef struct svc_wait {
	void (*done)(void *ctx, const bt_services *s, const char *err);
	void *ctx;
	struct svc_wait *next;
} svc_wait;

struct conn {
	uint32_t id;
	dev *d;
	bool up;
	bt_done done;  // "connect"'s, while connecting
	void *ctx;
	bool auto_connect;
	bool asked;  // its services told since the last change
	svc_wait *svc_waits;
	// Its attributes' handles, by path.
	set paths;  // index + 1 is the handle
	pending *calls;
	uint32_t next_call;
	set subs;  // characteristics subscribed (by path)
	conn *next;
};

static conn *conns;
static dev *devs;

static conn *conn_of(uint32_t id) {
	for (conn *c = conns; c; c = c->next)
		if (c->id == id) return c;
	return NULL;
}

static dev *dev_of(const char *path) {
	for (dev *d = devs; d; d = d->next)
		if (!strcmp(d->path, path)) return d;
	return NULL;
}

static size_t dev_conns(dev *d) {
	size_t n = 0;
	for (conn *c = conns; c; c = c->next) n += c->d == d;
	return n;
}

static void dev_forget(dev *d) {
	for (dev **p = &devs; *p; p = &(*p)->next)
		if (*p == d) {
			*p = d->next;
			break;
		}
	bt_after_stop(d->retry);
	free(d->path);
	free(d->reason);
	free(d->words);
	set_clear(&d->notifying);
	free(d);
}

static uint32_t handle_of(conn *c, const char *path) {
	for (size_t i = 0; i < c->paths.n; i++)
		if (!strcmp(c->paths.v[i], path)) return (uint32_t)i + 1;
	set_add(&c->paths, path);
	return (uint32_t)c->paths.n;
}

static const char *path_of(conn *c, uint32_t handle) { return handle >= 1 && handle <= c->paths.n ? c->paths.v[handle - 1] : NULL; }

static void fail_call(pending *p, const char *why) {
	if (p->kind == CALL_READ) answer_bytes((void (*)(void *, asuka_bytes, const char *))p->done, p->ctx, (asuka_bytes){0}, why);
	else bl_answer((bt_done)p->done, p->ctx, why);
}

static void drop_notify(dev *d, const char *path);

// end_conn forgets a connection: its calls waiting fail with why, as
// "connect" does if it was connecting; its subscriptions end.
static void end_conn(conn *c, const char *why) {
	for (conn **p = &conns; *p; p = &(*p)->next)
		if (*p == c) {
			*p = c->next;
			break;
		}
	dev *d = c->d;
	if (c->done) bl_answer(c->done, c->ctx, why);
	const char *f = strcmp(why, BT_DISCONNECTED) != 0 && c->up ? BT_DISCONNECTED : why;
	while (c->calls) {
		pending *p = c->calls;
		c->calls = p->next;
		fail_call(p, f);
		free(p);
	}
	while (c->svc_waits) {
		svc_wait *w = c->svc_waits;
		c->svc_waits = w->next;
		services_answer *a = calloc(1, sizeof *a);
		*a = (services_answer){w->done, w->ctx, {0}, asuka_strdup(f)};
		bl_defer(run_services, a);
		free(w);
	}
	set subs = c->subs;
	c->subs = (set){0};
	set_clear(&c->paths);
	free(c);
	for (size_t i = 0; i < subs.n; i++) drop_notify(d, subs.v[i]);
	set_clear(&subs);
	if (dev_conns(d) == 0 && !d->calling) dev_forget(d);
}

// device_path is the object of the device with the address, on the
// adapter in use (another's if that one hasn't it), NULL if none.
static char *device_path(const char *addr) {
	const char *adapter = bl_adapter();
	char *found = NULL;
	bt_strings paths = bl_paths(BL_IF_DEVICE, NULL);
	for (size_t i = 0; i < paths.len; i++) {
		bl_iface *p = bl_props(paths.data[i], BL_IF_DEVICE);
		const char *a = bl_str(bl_prop(p, "Address"));
		if (!a || strcasecmp(a, addr) != 0) continue;
		const char *ad = bl_str(bl_prop(p, "Adapter"));
		if (adapter && ad && !strcmp(ad, adapter)) {
			free(found);
			found = asuka_strdup(paths.data[i]);
			break;
		}
		if (!found) found = asuka_strdup(paths.data[i]);  // sorted: the first
	}
	bt_strings_free(&paths);
	return found;
}

static void conn_up(conn *c) {
	if (c->up) return;
	c->up = true;
	if (c->done) {
		bl_answer(c->done, c->ctx, NULL);
		c->done = NULL;
	}
}

void bl_device_connected(const char *path) {
	dev *d = dev_of(path);
	if (!d) return;
	free(d->reason), free(d->words);
	d->reason = d->words = NULL;
	bt_after_stop(d->retry);
	d->retry = 0;
	for (conn *c = conns; c; c = c->next)
		if (c->d == d) conn_up(c);
}

static void connect_device(dev *d);

static void retry_now(void *p) {
	dev *d = dev_of(p);  // its path: bt_after frees it
	if (!d) return;
	d->retry = 0;
	bool waiting = false;
	for (conn *c = conns; c; c = c->next) waiting = waiting || (c->d == d && !c->up);
	if (waiting && !bl_bool(bl_get(d->path, BL_IF_DEVICE, "Connected"))) connect_device(d);
}

// retry_connect connects the device again in a second, for its
// connections still connecting then.
static void retry_connect(dev *d) {
	bt_after_stop(d->retry);
	d->retry = bt_after(1000, retry_now, asuka_strdup(d->path), free);
}

static void disconnect_device(dev *d, bool quiet);

static void connected(void *ctx, GVariant *r, const char *name, const char *msg) {
	char *path = ctx;
	dev *d = dev_of(path);
	free(path);
	if (!d) return;
	d->calling = false;
	bool already = false, failed = !r;
	if (!r && name && !strcmp(name, "org.bluez.Error.AlreadyConnected")) {
		failed = false, already = true;  // by someone else, meanwhile
	} else if (!r && name && !strcmp(name, "org.bluez.Error.InProgress")) {
		// Another's connect (another app's): Connected says if it
		// succeeds; if not, this one tries again.
		retry_connect(d);
		return;
	}
	if (failed) {
		// The connections waiting fail, but those that wait for the
		// device (AutoConnect), which try again.
		char *why = bl_connect_error(name, msg);
		bool again = false;
		for (conn *c = conns, *next; c; c = next) {
			next = c->next;
			if (c->d != d || c->up) continue;
			if (c->auto_connect && bl_retry(name, msg) && !strcmp(bl_current(), BT_ON)) {
				again = true;
				continue;
			}
			end_conn(c, why);
		}
		free(why);
		// Not in range (or not answering): try again in a moment, as
		// Android's autoConnect waits for the device.
		if (again) retry_connect(d);
		else if (dev_conns(d) == 0 && dev_of(d->path) == d) dev_forget(d);
		return;
	}
	d->ours = !already;
	if (dev_conns(d) == 0 && d->ours) {
		disconnect_device(d, true);  // every connection gave up as it connected
		dev_forget(d);
		return;
	}
	bl_device_connected(d->path);
}

// connect_device asks BlueZ to connect the device, once for all its
// connections waiting.
static void connect_device(dev *d) {
	if (d->calling) return;
	d->calling = true;
	bl_call(BL_BLUEZ, d->path, BL_IF_DEVICE, "Connect", NULL, BL_NO_TIMEOUT, connected, asuka_strdup(d->path));
}

typedef struct {
	uint32_t conn;
	char *device;
	bool auto_connect;
	bt_done done;
	void *ctx;
} connect_args;

static void start_connect(void *p) {
	connect_args *a = p;
	const char *st = bl_current();
	char *path = strcmp(st, BT_ON) == 0 ? device_path(a->device) : NULL;
	if (strcmp(st, BT_ON) != 0) {
		bl_answer(a->done, a->ctx, st);
	} else if (!path) {
		bl_answer(a->done, a->ctx, BT_NOT_FOUND);
	} else {
		conn *old = conn_of(a->conn);
		if (old) end_conn(old, BT_DISCONNECTED);
		dev *d = dev_of(path);
		if (!d) {
			d = calloc(1, sizeof *d);
			d->path = asuka_strdup(path);
			d->next = devs;
			devs = d;
		}
		conn *c = calloc(1, sizeof *c);
		*c = (conn){.id = a->conn, .d = d, .done = a->done, .ctx = a->ctx, .auto_connect = a->auto_connect, .next = conns};
		conns = c;
		if (bl_bool(bl_get(path, BL_IF_DEVICE, "Connected"))) conn_up(c);  // by the system, another app, or another connection
		else connect_device(d);
	}
	free(path);
	free(a->device);
	free(a);
	bl_flush();
}

void bt_engine_connect(uint32_t conn_id, const char *device, bool auto_connect, bt_done done, void *ctx) {
	connect_args *a = malloc(sizeof *a);
	*a = (connect_args){conn_id, asuka_strdup(device), auto_connect, done, ctx};
	bl_start(start_connect, a);
}

static void disconnect_done(void *ctx, GVariant *r, const char *name, const char *msg) {}

static void disconnect_device(dev *d, bool quiet) { bl_call_bluez(d->path, BL_IF_DEVICE, "Disconnect", NULL, disconnect_done, NULL); }

void bt_engine_disconnect(uint32_t id) {
	conn *c = conn_of(id);
	if (!c) return;
	dev *d = c->d;
	if (c->done) {
		bl_answer(c->done, c->ctx, BT_CANCELED);
		c->done = NULL;
	}
	bool last = dev_conns(d) == 1, ours = d->ours || d->calling;
	char *path = asuka_strdup(d->path);
	end_conn(c, BT_DISCONNECTED);
	// The app's connection (or its attempt, which Disconnect cancels):
	// the system's own, another app's, stay.
	if (last && ours) bl_call_bluez(path, BL_IF_DEVICE, "Disconnect", NULL, disconnect_done, NULL);
	free(path);
	bl_flush();
}

typedef struct {
	uint32_t conn;
	char *reason, *err;
} disconnected_ctx;

static void run_disconnected(void *p) {
	disconnected_ctx *c = p;
	bt_on_disconnected(c->conn, c->reason, c->err);
	free(c->reason);
	free(c->err);
	free(c);
}

static void tell_disconnected(uint32_t conn_id, const char *reason, const char *err) {
	disconnected_ctx *c = malloc(sizeof *c);
	*c = (disconnected_ctx){conn_id, asuka_strdup(reason), err ? asuka_strdup(err) : NULL};
	bl_defer(run_disconnected, c);
}

// bl_device_disconnected: Connected turned false (or the device went):
// its connections end, with the reason the Disconnected signal gave.
void bl_device_disconnected(const char *path) {
	dev *d = dev_of(path);
	if (!d) return;
	const char *reason = bl_reason(d->reason);
	if (!strcmp(bl_current(), BT_OFF)) reason = BT_REASON_OFF;
	char *words = asuka_strdup(d->words && *d->words ? d->words : "the device disconnected");
	free(d->reason), free(d->words);
	d->reason = d->words = NULL;
	d->ours = false;
	set_clear(&d->notifying);
	bool gone = bl_props(path, BL_IF_DEVICE) == NULL;
	for (conn *c = conns, *next; c; c = next) {
		next = c->next;
		if (c->d != d) continue;
		if (!c->up) {
			if (!d->calling && gone) end_conn(c, BT_NOT_FOUND);  // else Connect answers
			continue;
		}
		tell_disconnected(c->id, reason, words);
		end_conn(c, BT_DISCONNECTED);
	}
	free(words);
}

void bl_disconnected_signal(const char *path, const char *reason, const char *words) {
	char *dp = bl_device_path(path);
	dev *d = dev_of(dp);
	free(dp);
	if (!d) return;
	free(d->reason), free(d->words);
	d->reason = asuka_strdup(reason);
	d->words = asuka_strdup(words);
}

// bl_central_off: the adapter's no longer on: the scans end, as do the
// connections.
void bl_central_off(const char *s) {
	while (scans) end_scan(scans, s);
	disc.on = false;  // BlueZ forgets the discovery
	free(disc.filter);
	disc.filter = NULL;
	const char *reason = !strcmp(s, BT_OFF) ? BT_REASON_OFF : BT_REASON_UNKNOWN;
	char *words = asuka_sprintf("Bluetooth is %s", s);
	while (conns) {
		conn *c = conns;
		if (c->up) tell_disconnected(c->id, reason, words);
		end_conn(c, s);
	}
	free(words);
}

// ---- Services ----

typedef struct {
	char *path;
	size_t svc, chr;
} at;

// service_tree is the device's attributes, in their order on the device
// (BlueZ's paths name their ATT handles, in hex of a fixed width).
static bt_services service_tree(conn *c) {
	c->asked = true;
	char *prefix = asuka_sprintf("%s/", c->d->path);
	bt_strings svcs = bl_paths(BL_IF_GATT_SERVICE, prefix), chars = bl_paths(BL_IF_GATT_CHAR, prefix), descs = bl_paths(BL_IF_GATT_DESC, prefix);
	free(prefix);
	bt_services out = {calloc(svcs.len ? svcs.len : 1, sizeof(bt_service)), svcs.len};
	for (size_t i = 0; i < svcs.len; i++) {
		bl_iface *p = bl_props(svcs.data[i], BL_IF_GATT_SERVICE);
		out.data[i] = (bt_service){handle_of(c, svcs.data[i]), bt_normalize(bl_str(bl_prop(p, "UUID"))), bl_bool(bl_prop(p, "Primary"))};
	}
	at *chars_at = calloc(chars.len ? chars.len : 1, sizeof *chars_at);
	size_t nat = 0;
	for (size_t i = 0; i < chars.len; i++) {
		bl_iface *p = bl_props(chars.data[i], BL_IF_GATT_CHAR);
		const char *svc = bl_str(bl_prop(p, "Service"));
		size_t k = 0;
		while (k < svcs.len && (!svc || strcmp(svcs.data[k], svc) != 0)) k++;
		if (k == svcs.len) continue;
		bt_service *s = &out.data[k];
		s->characteristics = realloc(s->characteristics, (s->ncharacteristics + 1) * sizeof *s->characteristics);
		s->characteristics[s->ncharacteristics] = (bt_characteristic){handle_of(c, chars.data[i]), bt_normalize(bl_str(bl_prop(p, "UUID"))),
		                                                              bl_properties(bl_prop(p, "Flags"))};
		chars_at[nat++] = (at){chars.data[i], k, s->ncharacteristics++};
	}
	for (size_t i = 0; i < descs.len; i++) {
		bl_iface *p = bl_props(descs.data[i], BL_IF_GATT_DESC);
		const char *ch = bl_str(bl_prop(p, "Characteristic"));
		size_t k = 0;
		while (k < nat && (!ch || strcmp(chars_at[k].path, ch) != 0)) k++;
		if (k == nat) continue;
		bt_characteristic *cs = &out.data[chars_at[k].svc].characteristics[chars_at[k].chr];
		cs->descriptors = realloc(cs->descriptors, (cs->ndescriptors + 1) * sizeof *cs->descriptors);
		cs->descriptors[cs->ndescriptors++] = (bt_descriptor){handle_of(c, descs.data[i]), bt_normalize(bl_str(bl_prop(p, "UUID")))};
	}
	free(chars_at);
	bt_strings_free(&svcs);
	bt_strings_free(&chars);
	bt_strings_free(&descs);
	return out;
}

static void answer_tree(conn *c, void (*done)(void *, const bt_services *, const char *), void *ctx) {
	services_answer *a = calloc(1, sizeof *a);
	*a = (services_answer){done, ctx, service_tree(c), NULL};
	bl_defer(run_services, a);
}

typedef struct {
	uint32_t conn;
	void (*done)(void *ctx, const bt_services *s, const char *err);
	void *ctx;
} services_args;

static void start_services(void *p) {
	services_args *a = p;
	conn *c = conn_of(a->conn);
	if (!c || !c->up) {
		services_answer *x = calloc(1, sizeof *x);
		*x = (services_answer){a->done, a->ctx, {0}, asuka_strdup(BT_DISCONNECTED)};
		bl_defer(run_services, x);
	} else if (!bl_bool(bl_get(c->d->path, BL_IF_DEVICE, "ServicesResolved"))) {
		svc_wait *w = malloc(sizeof *w);
		*w = (svc_wait){a->done, a->ctx, c->svc_waits};
		c->svc_waits = w;
	} else {
		answer_tree(c, a->done, a->ctx);
	}
	free(a);
	bl_flush();
}

void bt_engine_services(uint32_t conn_id, void (*done)(void *ctx, const bt_services *s, const char *err), void *ctx) {
	services_args *a = malloc(sizeof *a);
	*a = (services_args){conn_id, done, ctx};
	bl_start(start_services, a);
}

typedef struct {
	uint32_t conn;
} changed_ctx;

static void run_services_changed(void *p) {
	changed_ctx *c = p;
	bt_on_services_changed(c->conn);
	free(c);
}

// services_changed tells the connection its handles are gone.
static void services_changed(conn *c) {
	c->asked = false;
	set_clear(&c->paths);
	set subs = c->subs;
	c->subs = (set){0};
	for (size_t i = 0; i < subs.n; i++) drop_notify(c->d, subs.v[i]);
	set_clear(&subs);
	changed_ctx *x = malloc(sizeof *x);
	x->conn = c->id;
	bl_defer(run_services_changed, x);
}

// bl_services_resolved: BlueZ has the device's attributes; "services"
// waiting answer, and a connection that had them is told they changed.
void bl_services_resolved(const char *path) {
	dev *d = dev_of(path);
	if (!d) return;
	for (conn *c = conns; c; c = c->next) {
		if (c->d != d || !c->up) continue;
		if (c->asked && !c->svc_waits) {
			services_changed(c);
			continue;
		}
		svc_wait *w = c->svc_waits;
		c->svc_waits = NULL;
		while (w) {
			svc_wait *next = w->next;
			answer_tree(c, w->done, w->ctx);
			free(w);
			w = next;
		}
	}
}

static set changed_devs;

static bool has_services(const char *dev_path) {
	char *prefix = asuka_sprintf("%s/", dev_path);
	bt_strings s = bl_paths(BL_IF_GATT_SERVICE, prefix);
	free(prefix);
	bool has = s.len > 0;
	bt_strings_free(&s);
	return has;
}

static gboolean gatt_settled(gpointer p) {
	char *path = p;
	set_del(&changed_devs, path);
	dev *d = dev_of(path);
	// Not a disconnection's (BlueZ takes the objects down before it says
	// Connected is false), nor a re-discovery's first half.
	if (d && bl_bool(bl_get(path, BL_IF_DEVICE, "Connected")) && bl_bool(bl_get(path, BL_IF_DEVICE, "ServicesResolved")) && has_services(path))
		for (conn *c = conns; c; c = c->next)
			if (c->d == d && c->up && c->asked) services_changed(c);
	free(path);
	bl_flush();
	return false;
}

// bl_gatt_changed: a GATT object under a device came or went; once the
// signals of it are in, a connection that has its services hears that
// they changed (BlueZ's Service Changed handling, re-discovery).
void bl_gatt_changed(const char *path) {
	char *dp = bl_device_path(path);
	if (!dev_of(dp) || set_has(&changed_devs, dp) || !bl_bool(bl_get(dp, BL_IF_DEVICE, "ServicesResolved"))) {
		free(dp);  // the first discovery's objects: ServicesResolved tells
		return;
	}
	set_add(&changed_devs, dp);
	G(g_idle_add_full)(G_PRIORITY_DEFAULT, gatt_settled, dp, NULL);
}

// ---- Attributes: calls queued per attribute ----
//
// BlueZ refuses a second read (or write) of an attribute while one
// runs: each attribute's calls run one after another.

typedef struct job {
	int kind;  // JOB_*
	uint32_t conn, call;
	char *path;
	asuka_bytes value;
	bool response;
	dev *d;  // a StopNotify's
	struct job *next;
} job;

enum { JOB_READ, JOB_WRITE, JOB_START_NOTIFY, JOB_STOP_NOTIFY };

typedef struct queue {
	char *path;
	job *head;
	struct queue *next;
} queue;

static queue *queues;

static void run_queue(queue *q);

static queue *queue_of(const char *path, bool make) {
	for (queue *q = queues; q; q = q->next)
		if (!strcmp(q->path, path)) return q;
	if (!make) return NULL;
	queue *q = calloc(1, sizeof *q);
	q->path = asuka_strdup(path);
	q->next = queues;
	queues = q;
	return q;
}

static void job_free(job *j) {
	free(j->path);
	free(j->value.data);
	free(j);
}

static void enqueue(job *j) {
	queue *q = queue_of(j->path, true);
	job **at = &q->head;
	while (*at) at = &(*at)->next;
	*at = j;
	if (q->head == j) run_queue(q);
}

// next_job: the queue's first job is done; the next runs.
static void next_job(const char *path) {
	queue *q = queue_of(path, false);
	if (!q || !q->head) return;
	job *j = q->head;
	q->head = j->next;
	job_free(j);
	if (q->head) {
		run_queue(q);
		return;
	}
	for (queue **p = &queues; *p; p = &(*p)->next)
		if (*p == q) {
			*p = q->next;
			break;
		}
	free(q->path);
	free(q);
}

// take_call takes the connection's call waiting, NULL if it was answered
// already (the connection ended).
static pending *take_call(uint32_t conn_id, uint32_t call) {
	conn *c = conn_of(conn_id);
	if (!c) return NULL;
	for (pending **p = &c->calls; *p; p = &(*p)->next)
		if ((*p)->id == call) {
			pending *x = *p;
			*p = x->next;
			return x;
		}
	return NULL;
}

static bool call_waits(uint32_t conn_id, uint32_t call) {
	conn *c = conn_of(conn_id);
	for (pending *p = c ? c->calls : NULL; p; p = p->next)
		if (p->id == call) return true;
	return false;
}

static void job_done(void *ctx, GVariant *r, const char *name, const char *msg) {
	char *path = ctx;
	queue *q = queue_of(path, false);
	job *j = q ? q->head : NULL;
	if (j) {
		if (j->kind == JOB_STOP_NOTIFY) {
			// nothing waits
		} else {
			pending *p = take_call(j->conn, j->call);
			if (p) {
				char *why = r ? NULL : bl_client_error(name, msg, j->kind != JOB_READ);
				if (j->kind == JOB_READ) {
					asuka_bytes v = {0};
					if (r) {
						GVariant *c = G(g_variant_get_child_value)(r, 0);
						v = bl_bytes(c);
						G(g_variant_unref)(c);
					}
					answer_bytes((void (*)(void *, asuka_bytes, const char *))p->done, p->ctx, v, why);
					free(v.data);
				} else {
					if (r && j->kind == JOB_START_NOTIFY) {
						conn *c = conn_of(j->conn);
						if (c) {
							set_add(&c->d->notifying, j->path);
							set_add(&c->subs, j->path);
						}
					}
					bl_answer((bt_done)p->done, p->ctx, why);
				}
				free(why);
				free(p);
			}
		}
	}
	next_job(path);
	free(path);
}

static void run_queue(queue *q) {
	job *j = q->head;
	if (j->kind == JOB_STOP_NOTIFY) {
		if (set_has(&j->d->notifying, j->path) || !bl_props(j->path, BL_IF_GATT_CHAR)) {
			next_job(j->path);  // subscribed again meanwhile, or gone
			return;
		}
		bl_call_bluez(j->path, BL_IF_GATT_CHAR, "StopNotify", NULL, job_done, asuka_strdup(j->path));
		return;
	}
	if (!call_waits(j->conn, j->call)) {
		next_job(j->path);  // its connection ended: it was answered
		return;
	}
	switch (j->kind) {
	case JOB_READ:
		bl_call_bluez(j->path, BL_IF_GATT_CHAR, "ReadValue", G(g_variant_new)("(@a{sv})", bl_empty_dict()), job_done, asuka_strdup(j->path));
		break;
	case JOB_WRITE: {
		GVariantBuilder b;
		G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
		G(g_variant_builder_add)(&b, "{sv}", "type", G(g_variant_new_string)(j->response ? "request" : "command"));
		GVariant *params = G(g_variant_new)("(@ay@a{sv})", bl_byte_array(j->value.data, j->value.len), G(g_variant_builder_end)(&b));
		bl_call_bluez(j->path, BL_IF_GATT_CHAR, "WriteValue", params, job_done, asuka_strdup(j->path));
		break;
	}
	case JOB_START_NOTIFY: {
		conn *c = conn_of(j->conn);
		if (c && set_has(&c->d->notifying, j->path)) {  // the device's session of it is on already
			set_add(&c->subs, j->path);
			pending *p = take_call(j->conn, j->call);
			if (p) bl_answer((bt_done)p->done, p->ctx, NULL);
			free(p);
			next_job(j->path);
			return;
		}
		bl_call_bluez(j->path, BL_IF_GATT_CHAR, "StartNotify", NULL, job_done, asuka_strdup(j->path));
		break;
	}
	}
}

// drop_notify ends BlueZ's notification session of the characteristic
// once no connection to the device is subscribed.
static void drop_notify(dev *d, const char *path) {
	if (!set_has(&d->notifying, path)) return;
	for (conn *c = conns; c; c = c->next)
		if (c->d == d && set_has(&c->subs, path)) return;
	set_del(&d->notifying, path);
	if (!bl_props(path, BL_IF_GATT_CHAR)) return;  // gone with the connection
	job *j = calloc(1, sizeof *j);
	*j = (job){.kind = JOB_STOP_NOTIFY, .path = asuka_strdup(path), .d = d};
	enqueue(j);
}

typedef struct {
	int kind;
	uint32_t conn, handle;
	asuka_bytes value;
	bool response;
	void *done, *ctx;
} attr_args;

static void start_attr(void *p) {
	attr_args *a = p;
	conn *c = conn_of(a->conn);
	const char *path = c && c->up ? path_of(c, a->handle) : NULL;
	pending x = {0, a->kind, a->done, a->ctx, NULL};
	if (!c || !c->up) {
		fail_call(&x, BT_DISCONNECTED);
	} else if (!path || !bl_props(path, BL_IF_GATT_CHAR)) {
		fail_call(&x, BT_NOT_FOUND);
	} else if (a->kind == CALL_SUBSCRIBE && !(bl_properties(bl_get(path, BL_IF_GATT_CHAR, "Flags")) & (BT_PROP_NOTIFY | BT_PROP_INDICATE))) {
		fail_call(&x, "att 6: the characteristic doesn't notify");
	} else {
		pending *w = malloc(sizeof *w);
		*w = (pending){++c->next_call, a->kind, a->done, a->ctx, c->calls};
		c->calls = w;
		job *j = calloc(1, sizeof *j);
		*j = (job){.kind = a->kind == CALL_READ ? JOB_READ : a->kind == CALL_WRITE ? JOB_WRITE : JOB_START_NOTIFY, .conn = c->id, .call = w->id,
		           .path = asuka_strdup(path), .value = a->value, .response = a->response};
		a->value = (asuka_bytes){0};
		enqueue(j);
	}
	free(a->value.data);
	free(a);
	bl_flush();
}

static void attr_call(int kind, uint32_t conn_id, uint32_t handle, asuka_bytes value, bool response, void *done, void *ctx) {
	attr_args *a = malloc(sizeof *a);
	*a = (attr_args){kind, conn_id, handle, asuka_bytes_copy(value.data, value.len), response, done, ctx};
	bl_start(start_attr, a);
}

void bt_engine_read(uint32_t conn_id, uint32_t handle, void (*done)(void *ctx, asuka_bytes value, const char *err), void *ctx) {
	attr_call(CALL_READ, conn_id, handle, (asuka_bytes){0}, false, (void *)done, ctx);
}

void bt_engine_write(uint32_t conn_id, uint32_t handle, asuka_bytes value, bool response, bt_done done, void *ctx) {
	attr_call(CALL_WRITE, conn_id, handle, value, response, (void *)done, ctx);
}

// bt_engine_subscribe turns notifications on: StartNotify (BlueZ writes
// the CCCD; it enables notifications where the characteristic has both).
// The device's connections share BlueZ's one session of it.
void bt_engine_subscribe(uint32_t conn_id, uint32_t handle, bt_done done, void *ctx) {
	attr_call(CALL_SUBSCRIBE, conn_id, handle, (asuka_bytes){0}, false, (void *)done, ctx);
}

void bt_engine_unsubscribe(uint32_t conn_id, uint32_t handle) {
	conn *c = conn_of(conn_id);
	const char *path = c ? path_of(c, handle) : NULL;
	if (!path || !set_has(&c->subs, path)) return;
	char *p = asuka_strdup(path);
	set_del(&c->subs, p);
	drop_notify(c->d, p);
	free(p);
	bl_flush();
}

typedef struct {
	uint32_t conn, handle;
	asuka_bytes v;
} value_ctx;

static void run_value(void *p) {
	value_ctx *v = p;
	bt_on_value(v->conn, v->handle, v->v);
	free(v->v.data);
	free(v);
}

// bl_notified is a characteristic's new value (a notification, an
// indication, or a read's): the connections subscribed to it get it.
void bl_notified(const char *path, asuka_bytes value) {
	char *dp = bl_device_path(path);
	dev *d = dev_of(dp);
	free(dp);
	if (!d || !set_has(&d->notifying, path)) return;
	for (conn *c = conns; c; c = c->next) {
		if (c->d != d || !c->up || !set_has(&c->subs, path)) continue;
		for (size_t i = 0; i < c->paths.n; i++)
			if (!strcmp(c->paths.v[i], path)) {
				value_ctx *v = malloc(sizeof *v);
				*v = (value_ctx){c->id, (uint32_t)i + 1, asuka_bytes_copy(value.data, value.len)};
				bl_defer(run_value, v);
			}
	}
}

// ---- The link ----

typedef struct {
	uint32_t conn;
	void (*done)(void *ctx, int v, const char *err);
	void *ctx;
} link_args;

// The connection's ATT MTU: BlueZ's characteristics have it (MTU, BlueZ
// 5.62 on); 23 (the least) where they don't say.
static void start_mtu(void *p) {
	link_args *a = p;
	conn *c = conn_of(a->conn);
	if (!c || !c->up) {
		answer_int(a->done, a->ctx, 0, BT_DISCONNECTED);
	} else {
		char *prefix = asuka_sprintf("%s/", c->d->path);
		bt_strings chars = bl_paths(BL_IF_GATT_CHAR, prefix);
		free(prefix);
		int mtu = 23, n;
		for (size_t i = 0; i < chars.len; i++)
			if (bl_int(bl_get(chars.data[i], BL_IF_GATT_CHAR, "MTU"), &n) && n > mtu) mtu = n;
		bt_strings_free(&chars);
		answer_int(a->done, a->ctx, mtu, NULL);
	}
	free(a);
	bl_flush();
}

void bt_engine_mtu(uint32_t conn_id, void (*done)(void *ctx, int mtu, const char *err), void *ctx) {
	link_args *a = malloc(sizeof *a);
	*a = (link_args){conn_id, done, ctx};
	bl_start(start_mtu, a);
}

// BlueZ has a device's RSSI only while discovering (from its
// advertisements), none for a connection's link.
static void start_rssi(void *p) {
	link_args *a = p;
	conn *c = conn_of(a->conn);
	int n;
	if (!c || !c->up) answer_int(a->done, a->ctx, 0, BT_DISCONNECTED);
	else if (bl_int(bl_get(c->d->path, BL_IF_DEVICE, "RSSI"), &n)) answer_int(a->done, a->ctx, n, NULL);
	else answer_int(a->done, a->ctx, 0, "not available: BlueZ reads no connected device's signal strength");
	free(a);
	bl_flush();
}

void bt_engine_rssi(uint32_t conn_id, void (*done)(void *ctx, int rssi, const char *err), void *ctx) {
	link_args *a = malloc(sizeof *a);
	*a = (link_args){conn_id, done, ctx};
	bl_start(start_rssi, a);
}
