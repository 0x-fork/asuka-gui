// platform/purchase on Windows: the Microsoft Store (Windows.Services.Store),
// for an app installed from it; one that isn't (no package identity:
// asuka dev, a build installed by hand) has no store. Products are the
// app's add-ons, by the product ID Partner Center gave each (its
// InAppOfferToken) or their Store ID. A consumable's purchase is a
// transaction until the app finishes it (the Store's fulfillment: the
// user can't buy it again until then, and those left unfulfilled come
// again at the next start); a durable's or a subscription's is its
// license. The Store says when the user's licenses change (bought on
// another PC, renewed, refunded): new ones come through events(), a
// durable's gone as revoked. The Store's calls wait, so each runs on a
// worker in COM's multithreaded apartment; a desktop app's context shows
// its dialogs over the window it's initialized with
// (IInitializeWithWindow).
#include "asuka_windows.h"
#include "purchase.asuka.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- WinRT's Windows.Services.Store, by vtable slot ----

#define STORE_RC(name, iid) "rc(Windows.Services.Store." name ";" iid ")"
#define RC_CONTEXT STORE_RC("StoreContext", "{ac98b6be-f4fd-4912-babd-5035e5e8bcab}")
#define RC_QUERY STORE_RC("StoreProductQueryResult", "{d805e6c5-d456-4ff6-8049-9076d5165f73}")
#define RC_PRODUCT STORE_RC("StoreProduct", "{320e2c52-d760-450a-a42b-67d1e901ac90}")
#define RC_PURCHASE STORE_RC("StorePurchaseResult", "{add28552-f96a-463d-a7bb-c20b4fca6952}")
#define RC_CONSUMABLE STORE_RC("StoreConsumableResult", "{ea5dab72-6a00-4052-be5b-bfdab4433352}")
#define RC_APP_LICENSE STORE_RC("StoreAppLicense", "{f389f9de-73c0-45ce-9bab-b2fe3e5eafd3}")
#define RC_LICENSE STORE_RC("StoreLicense", "{26dc9579-4c4f-4f30-bc89-649f60e36055}")
#define ITERABLE "{faa585ea-6214-4217-afda-7f46de5869b3}"
#define ITERATOR "{6a79e863-4300-459a-9966-cbb660963ee1}"
#define KV_PAIR "{02b51929-c1c4-4a7e-8940-0381e2b86a5f}"
#define TYPED_HANDLER "{9de1c534-6ae1-11e0-84e1-18a905bcc53f}"

static const GUID iid_statics = {0x9C06EE5F, 0x15C0, 0x4E72, {0x93, 0x30, 0xD6, 0x19, 0x1C, 0xEB, 0xD1, 0x9C}};
static const GUID iid_init_window = {0x3E68D4BD, 0x7135, 0x4D10, {0x80, 0x18, 0x9F, 0xB6, 0xD9, 0xF3, 0x3F, 0xA1}};
static const GUID iid_inspectable = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

enum {
	statics_get_default = 6,
	ctx_add_licenses_changed = 7,
	ctx_get_app_license = 11,
	ctx_get_products = 14,
	ctx_report_fulfillment = 18,
	ctx_request_purchase = 21,
	query_products = 6,
	query_error = 7,
	product_store_id = 6,
	product_title = 8,
	product_description = 9,
	product_kind = 10,
	product_skus = 15,
	product_owned = 16,
	product_price = 17,
	product_offer_token = 23,
	price_formatted = 7,
	price_currency = 10,
	vector_get_at = 6,
	vector_size = 7,
	sku_json = 16,
	sku_is_subscription = 23,
	sku_subscription = 24,
	sub_period = 6,
	sub_period_unit = 7,
	sub_has_trial = 8,
	sub_trial = 9,
	sub_trial_unit = 10,
	purchase_status = 6,
	purchase_error = 7,
	consumable_status = 6,
	consumable_error = 9,
	app_license_add_ons = 11,
	license_sku = 6,
	license_active = 7,
	license_expires = 8,
	license_offer_token = 10,
	iterable_first = 6,
	iterator_current = 6,
	iterator_has_current = 7,
	iterator_move_next = 8,
	pair_key = 6,
	pair_value = 7,
	op_get_results = 8,
};

enum { st_purchased = 0, st_already = 1, st_not_purchased = 2, st_network = 3, st_server = 4 };  // StorePurchaseStatus

typedef HRESULT(STDMETHODCALLTYPE *get_ptr)(void *, void **);
typedef HRESULT(STDMETHODCALLTYPE *get_hs)(void *, HSTRING *);
typedef HRESULT(STDMETHODCALLTYPE *get_u8)(void *, uint8_t *);
typedef HRESULT(STDMETHODCALLTYPE *get_u32)(void *, uint32_t *);
typedef HRESULT(STDMETHODCALLTYPE *get_i32)(void *, int32_t *);
typedef HRESULT(STDMETHODCALLTYPE *get_i64)(void *, int64_t *);

// str is the string at a getter's slot, of malloc's ("" for none).
static char *str(void *obj, int slot) {
	HSTRING h = NULL;
	if (!obj || FAILED(ASUKA_SLOT(obj, slot, get_hs)(obj, &h))) return asuka_strdup("");
	char *s = asuka_hstring_utf8(h);
	asuka_hstring_free(h);
	return s;
}

static void *ptr(void *obj, int slot) {
	void *p = NULL;
	if (obj && FAILED(ASUKA_SLOT(obj, slot, get_ptr)(obj, &p))) p = NULL;
	return p;
}

static bool flag(void *obj, int slot) {
	uint8_t b = 0;
	ASUKA_SLOT(obj, slot, get_u8)(obj, &b);
	return b != 0;
}

// await waits for an async operation (a worker's) and is its results (a
// reference), or NULL.
static void *await(void *op, const char *result_sig) {
	if (!op) return NULL;
	char *sig = asuka_sprintf("pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";%s)", result_sig);
	GUID h = asuka_winrt_iid(sig);
	free(sig);
	void *r = NULL;
	if (asuka_winrt_wait(op, &h, NULL) == S_OK) ASUKA_SLOT(op, op_get_results, get_ptr)(op, &r);
	asuka_com_release(op);
	return r;
}

// extended fails err with a result's ExtendedError (an HRESULT at slot),
// if it's one.
static bool extended(void *res, int slot, asuka_error *err) {
	int32_t hr = 0;
	ASUKA_SLOT(res, slot, get_i32)(res, &hr);
	if (hr >= 0) return false;
	asuka_win_fail(err, (HRESULT)hr, "the Microsoft Store");
	return true;
}

// ---- IIterable<String>: the kinds asked for ----

typedef struct {
	const void *const *vtbl;
	LONG refs;
	GUID iid;
	const char *const *list;
	int n, at;
	bool iterator;
} strings;

static strings *new_strings(const char *const *list, int n, int at, bool iterator);

static HRESULT STDMETHODCALLTYPE s_query(strings *s, const GUID *riid, void **out) {
	if (IsEqualGUID(riid, &s->iid) || IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_inspectable) || IsEqualGUID(riid, &iid_agile)) {
		InterlockedIncrement(&s->refs);
		*out = s;
		return S_OK;
	}
	*out = NULL;
	return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE s_addref(strings *s) { return (ULONG)InterlockedIncrement(&s->refs); }
static ULONG STDMETHODCALLTYPE s_release(strings *s) {
	LONG n = InterlockedDecrement(&s->refs);
	if (n == 0) free(s);
	return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE s_iids(strings *s, ULONG *n, GUID **iids) {
	*n = 0;
	*iids = NULL;
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_class(strings *s, HSTRING *name) {
	*name = NULL;
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_trust(strings *s, int *level) {
	*level = 0;
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_first(strings *s, void **out) {
	*out = new_strings(s->list, s->n, 0, true);
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_current(strings *s, HSTRING *out) {
	if (s->at >= s->n) return E_BOUNDS;
	*out = asuka_hstring(s->list[s->at]);
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_has_current(strings *s, uint8_t *out) {
	*out = s->at < s->n;
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_move_next(strings *s, uint8_t *out) {
	if (s->at < s->n) s->at++;
	*out = s->at < s->n;
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_get_many(strings *s, UINT32 capacity, HSTRING *out, UINT32 *actual) {
	UINT32 k = 0;
	while (k < capacity && s->at < s->n) out[k++] = asuka_hstring(s->list[s->at++]);
	*actual = k;
	return S_OK;
}

static const void *const iterable_vtbl[] = {(void *)s_query, (void *)s_addref, (void *)s_release, (void *)s_iids,
                                            (void *)s_class, (void *)s_trust, (void *)s_first};
static const void *const iterator_vtbl[] = {(void *)s_query,   (void *)s_addref,      (void *)s_release,   (void *)s_iids,     (void *)s_class,
                                            (void *)s_trust,   (void *)s_current,     (void *)s_has_current, (void *)s_move_next, (void *)s_get_many};

static strings *new_strings(const char *const *list, int n, int at, bool iterator) {
	strings *s = calloc(1, sizeof *s);
	s->vtbl = iterator ? iterator_vtbl : iterable_vtbl;
	s->refs = 1;
	s->iid = asuka_winrt_iid(iterator ? "pinterface(" ITERATOR ";string)" : "pinterface(" ITERABLE ";string)");
	s->list = list;
	s->n = n;
	s->at = at;
	s->iterator = iterator;
	return s;
}

// ---- The Store's objects, read ----

typedef struct {
	char *store_id, *offer_token, *title, *description, *kind, *price, *currency;
	int64_t micros;
	bool subscription, owned;
	char *period_unit, *trial_unit;
	int period_count, trial_count;
} add_on;

typedef struct {
	char *sku, *offer_token;
	bool active;
	int64_t expires;  // ns since 1970; 0: none
} license;

static void add_on_free(add_on *a) {
	free(a->store_id), free(a->offer_token), free(a->title), free(a->description), free(a->kind), free(a->price), free(a->currency);
	free(a->period_unit), free(a->trial_unit);
}

static add_on add_on_copy(const add_on *a) {
	add_on c = *a;
	c.store_id = asuka_strdup(a->store_id), c.offer_token = asuka_strdup(a->offer_token), c.title = asuka_strdup(a->title);
	c.description = asuka_strdup(a->description), c.kind = asuka_strdup(a->kind), c.price = asuka_strdup(a->price);
	c.currency = asuka_strdup(a->currency), c.period_unit = asuka_strdup(a->period_unit), c.trial_unit = asuka_strdup(a->trial_unit);
	return c;
}

// pair is a map view's pairs (IIterable<IKeyValuePair<String, T>>):
// fn(ctx, value) with each value.
static void pairs(void *map, const char *value_sig, void (*fn)(void *ctx, void *value), void *ctx) {
	char *sig = asuka_sprintf("pinterface(" ITERABLE ";pinterface(" KV_PAIR ";string;%s))", value_sig);
	GUID iid = asuka_winrt_iid(sig);
	free(sig);
	void *it = asuka_com_query(map, &iid);
	void *iter = it ? ptr(it, iterable_first) : NULL;
	asuka_com_release(it);
	if (!iter) return;
	for (;;) {
		if (!flag(iter, iterator_has_current)) break;
		void *kv = ptr(iter, iterator_current);
		if (kv) {
			void *v = ptr(kv, pair_value);
			if (v) fn(ctx, v), asuka_com_release(v);
			asuka_com_release(kv);
		}
		uint8_t more = 0;
		ASUKA_SLOT(iter, iterator_move_next, get_u8)(iter, &more);
	}
	asuka_com_release(iter);
}

// duration_unit is StoreDurationUnit's name.
static const char *duration_unit(int32_t u) {
	switch (u) {
	case 0:
	case 1:
	case 2:
		return "Day";  // Minute, Hour, Day
	case 3:
		return "Week";
	case 4:
		return "Month";
	}
	return "Year";
}

// list_price_micros reads a SKU's list price from its listing's JSON
// (Availabilities[0].OrderManagementData.Price.ListPrice): StorePrice has
// only formatted strings.
static int64_t list_price_micros(const char *js) {
	char *av = asuka_json_get(js, "Availabilities");
	const char *first = av ? strchr(av, '{') : NULL;
	char *omd = first ? asuka_json_get(first, "OrderManagementData") : NULL;
	char *price = omd ? asuka_json_get(omd, "Price") : NULL;
	char *list = price ? asuka_json_get(price, "ListPrice") : NULL;
	int64_t micros = list ? (int64_t)(strtod(list, NULL) * 1e6 + 0.5) : 0;
	free(av), free(omd), free(price), free(list);
	return micros;
}

static add_on read_add_on(void *p) {
	add_on a = {str(p, product_store_id), str(p, product_offer_token), str(p, product_title), str(p, product_description), str(p, product_kind)};
	a.owned = flag(p, product_owned);
	void *price = ptr(p, product_price);
	a.price = str(price, price_formatted);
	a.currency = str(price, price_currency);
	asuka_com_release(price);
	a.period_unit = asuka_strdup("");
	a.trial_unit = asuka_strdup("");
	void *skus = ptr(p, product_skus);
	uint32_t n = 0;
	if (skus) ASUKA_SLOT(skus, vector_size, get_u32)(skus, &n);
	void *sku = NULL;
	if (n > 0) ASUKA_SLOT(skus, vector_get_at, HRESULT(STDMETHODCALLTYPE *)(void *, uint32_t, void **))(skus, 0, &sku);
	asuka_com_release(skus);
	if (sku) {
		char *js = str(sku, sku_json);
		a.micros = list_price_micros(js);
		free(js);
		void *info = flag(sku, sku_is_subscription) ? ptr(sku, sku_subscription) : NULL;
		if (info) {
			a.subscription = true;
			uint32_t count = 0;
			int32_t unit = 0;
			ASUKA_SLOT(info, sub_period, get_u32)(info, &count);
			ASUKA_SLOT(info, sub_period_unit, get_i32)(info, &unit);
			a.period_count = (int)count;
			free(a.period_unit);
			a.period_unit = asuka_strdup(duration_unit(unit));
			if (flag(info, sub_has_trial)) {
				ASUKA_SLOT(info, sub_trial, get_u32)(info, &count);
				ASUKA_SLOT(info, sub_trial_unit, get_i32)(info, &unit);
				a.trial_count = (int)count;
				free(a.trial_unit);
				a.trial_unit = asuka_strdup(duration_unit(unit));
			}
			asuka_com_release(info);
		}
		asuka_com_release(sku);
	}
	return a;
}

// ---- The store ----

static struct {
	CRITICAL_SECTION mu;
	INIT_ONCE once;
	void *ctx;  // StoreContext
	asuka_error failed;
	add_on *add_ons;  // by Store ID
	size_t nadd_ons;
	license *active;  // the licenses last seen active
	size_t nactive;
	char **bought;  // Store IDs bought here, whose license isn't news
	size_t nbought;
	purchase_StoreEvent *kept;  // for the first events()
	size_t nkept;
	struct reader {
		purchase_fn_StoreEvent fn;
		asuka_call *call;
	} *readers;
	size_t nreaders;
} s;

static void lock(void) { EnterCriticalSection(&s.mu); }
static void unlock(void) { LeaveCriticalSection(&s.mu); }

// store_id_of is an add-on's Store ID from a SKU's (9NBLGGH4R315/0010).
static char *store_id_of(const char *sku) { return asuka_strndup(sku, strcspn(sku, "/")); }

// find_add_on is the add-on of a Store ID or a product ID (its offer
// token), a copy; s.mu held.
static add_on *find_locked(const char *id) {
	for (size_t i = 0; i < s.nadd_ons; i++)
		if (!strcmp(s.add_ons[i].store_id, id)) return &s.add_ons[i];
	for (size_t i = 0; i < s.nadd_ons; i++)
		if (*s.add_ons[i].offer_token && !strcmp(s.add_ons[i].offer_token, id)) return &s.add_ons[i];
	return NULL;
}

static bool find(const char *id, add_on *out) {
	lock();
	add_on *a = find_locked(id);
	if (a) *out = add_on_copy(a);
	unlock();
	return a != NULL;
}

static const char *product_id_of(const add_on *a) { return *a->offer_token ? a->offer_token : a->store_id; }

static bool ends_with(const char *s, const char *suffix) {
	size_t n = strlen(s), m = strlen(suffix);
	return n >= m && !strcmp(s + n - m, suffix);
}

static purchase_Kind kind_of(const add_on *a) {
	if (ends_with(a->kind, "Consumable")) return purchase_Kind_consumable;
	if (a->subscription) return purchase_Kind_autoRenewable;
	return purchase_Kind_nonConsumable;
}

static purchase_Period period(const char *unit, int n) {
	purchase_PeriodUnit u = !_stricmp(unit, "Day") ? purchase_PeriodUnit_day : !_stricmp(unit, "Week") ? purchase_PeriodUnit_week
	                        : !_stricmp(unit, "Year")                         ? purchase_PeriodUnit_year
	                                                                           : purchase_PeriodUnit_month;
	return (purchase_Period){u, n};
}

// user_country is the user's country or region, as Settings' Region has
// it (ISO 3166-1 alpha-2: what the Store's prices follow), "" if Windows
// doesn't say. Of malloc's.
static char *user_country(void) {
	static int(WINAPI * geo_name)(wchar_t *, int);
	static bool tried;
	if (!tried) {
		tried = true;
		*(FARPROC *)&geo_name = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetUserDefaultGeoName");
	}
	wchar_t buf[16];
	if (!geo_name || geo_name(buf, 16) == 0) return asuka_strdup("");
	return asuka_narrow(buf, -1);
}

// tracking_id is a fulfillment's ID: a GUID, as "{...}".
static char *tracking_id(void) {
	GUID g;
	CoCreateGuid(&g);
	char b[37];
	asuka_guid_string(&g, b);
	return asuka_sprintf("{%s}", b);
}

static int64_t now_ns(void) {
	FILETIME ft;
	GetSystemTimePreciseAsFileTime(&ft);
	int64_t t = (int64_t)((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime);
	return (t - 116444736000000000LL) * 100;
}

// transaction is a purchase of a: a consumable's under a new tracking ID
// (what its fulfillment reports), the others' under the add-on's Store
// ID. id is the product ID asked for ("": the add-on's).
static purchase_StoreTransaction transaction(const add_on *a, const char *id) {
	purchase_StoreTransaction t = {0};
	t.kind = kind_of(a);
	t.id = t.kind == purchase_Kind_consumable ? tracking_id() : asuka_strdup(a->store_id);
	t.productId = asuka_strdup(id && *id ? id : product_id_of(a));
	t.quantity = 1;
	t.state = purchase_State_purchased;
	t.autoRenews = a->subscription;
	t.storefront = user_country();
	t.verification.token = asuka_strdup(a->store_id);
	return t;
}

static purchase_Product product(const add_on *a, const char *id) {
	purchase_Product p = {asuka_strdup(id), kind_of(a), asuka_strdup(a->title), asuka_strdup(a->description), asuka_strdup(a->price), a->micros,
	                      asuka_strdup(a->currency)};
	if (a->subscription) {
		purchase_Subscription *sub = calloc(1, sizeof *sub);
		sub->group = asuka_strdup(product_id_of(a));
		sub->period = period(a->period_unit, a->period_count);
		if (a->trial_count > 0) {
			sub->introEligible = !a->owned;
			purchase_Phase *ph = calloc(1, sizeof *ph);
			*ph = (purchase_Phase){asuka_strdup("Free"), 0, asuka_strdup(a->currency), period(a->trial_unit, a->trial_count), 1, purchase_Mode_freeTrial};
			purchase_Offer *o = calloc(1, sizeof *o);
			o->type = purchase_OfferType_introductory;
			o->phases = (purchase_list_Phase){ph, 1};
			sub->offers = (purchase_list_Offer){o, 1};
		}
		p.subscription = sub;
	}
	return p;
}

// give gives a transaction to events(), or keeps it for the first. s.mu
// held; t is taken.
static void give_locked(purchase_StoreTransaction t) {
	purchase_StoreTransaction *tp = malloc(sizeof *tp);
	*tp = t;
	purchase_StoreEvent e = {purchase_EventType_transactionUpdated, tp, {asuka_strdup(""), asuka_strdup("")}};
	if (!s.nreaders) {
		s.kept = realloc(s.kept, (s.nkept + 1) * sizeof *s.kept);
		s.kept[s.nkept++] = e;
		return;
	}
	for (size_t i = 0; i < s.nreaders; i++) purchase_fn_StoreEvent_call(s.readers[i].fn, &e);
	purchase_StoreEvent_free(&e);
}

static void add_on_got(void *ctx, void *p) {
	add_on a = read_add_on(p);
	lock();
	add_on *had = find_locked(a.store_id);
	if (had && !strcmp(had->store_id, a.store_id)) {
		add_on_free(had);
		*had = a;
	} else {
		s.add_ons = realloc(s.add_ons, (s.nadd_ons + 1) * sizeof *s.add_ons);
		s.add_ons[s.nadd_ons++] = a;
	}
	unlock();
}

// refresh reads the add-ons the Store sells with the app (durables,
// consumables, subscriptions). A worker's.
static bool refresh(asuka_error *err) {
	static const char *const kinds[] = {"Durable", "Consumable", "UnmanagedConsumable"};
	strings *ks = new_strings(kinds, 3, 0, false);
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(s.ctx, ctx_get_products, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(s.ctx, ks, &op);
	s_release(ks);
	if (FAILED(hr) || !op) {
		asuka_win_fail(err, FAILED(hr) ? hr : E_FAIL, "the Microsoft Store");
		return false;
	}
	void *res = await(op, RC_QUERY);
	if (!res) {
		asuka_error_failed(err, "purchase", 0, "the Microsoft Store didn't answer");
		return false;
	}
	bool ok = !extended(res, query_error, err);
	void *m = ok ? ptr(res, query_products) : NULL;
	if (m) pairs(m, RC_PRODUCT, add_on_got, NULL), asuka_com_release(m);
	asuka_com_release(res);
	return ok;
}

typedef struct {
	license *list;
	size_t n;
} licenses;

static void license_got(void *ctx, void *l) {
	licenses *ls = ctx;
	license x = {str(l, license_sku), str(l, license_offer_token), flag(l, license_active), 0};
	int64_t ticks = 0;  // 100 ns since 1601
	ASUKA_SLOT(l, license_expires, get_i64)(l, &ticks);
	const int64_t epoch = 116444736000000000LL, max = epoch + INT64_MAX / 100;  // a year past 2262 (9999: none) doesn't end
	if (ticks > 0 && ticks < max) x.expires = (ticks - epoch) * 100;
	ls->list = realloc(ls->list, (ls->n + 1) * sizeof *ls->list);
	ls->list[ls->n++] = x;
}

static void licenses_free(licenses *ls) {
	for (size_t i = 0; i < ls->n; i++) free(ls->list[i].sku), free(ls->list[i].offer_token);
	free(ls->list);
	*ls = (licenses){0};
}

// read_licenses are the user's licenses for the app's add-ons. A worker's.
static bool read_licenses(licenses *out, asuka_error *err) {
	*out = (licenses){0};
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(s.ctx, ctx_get_app_license, get_ptr)(s.ctx, &op);
	if (FAILED(hr) || !op) {
		asuka_win_fail(err, FAILED(hr) ? hr : E_FAIL, "the Microsoft Store");
		return false;
	}
	void *lic = await(op, RC_APP_LICENSE);
	if (!lic) {
		asuka_error_failed(err, "purchase", 0, "the Microsoft Store didn't answer");
		return false;
	}
	void *m = ptr(lic, app_license_add_ons);
	if (m) pairs(m, RC_LICENSE, license_got, out), asuka_com_release(m);
	asuka_com_release(lic);
	return true;
}

static const license *active_of(const licenses *ls, const char *id) {
	for (size_t i = 0; i < ls->n; i++) {
		if (!ls->list[i].active) continue;
		char *sid = store_id_of(ls->list[i].sku);
		bool same = !strcmp(sid, id);
		free(sid);
		if (same) return &ls->list[i];
	}
	return NULL;
}

// keep_active keeps the active licenses as those seen. s.mu held.
static void keep_active_locked(const licenses *ls) {
	for (size_t i = 0; i < s.nactive; i++) free(s.active[i].sku), free(s.active[i].offer_token);
	s.nactive = 0;
	for (size_t i = 0; i < ls->n; i++) {
		if (!ls->list[i].active) continue;
		s.active = realloc(s.active, (s.nactive + 1) * sizeof *s.active);
		license l = ls->list[i];
		l.sku = store_id_of(l.sku);  // kept by Store ID
		l.offer_token = asuka_strdup(l.offer_token);
		s.active[s.nactive++] = l;
	}
}

static bool bought_take_locked(const char *id) {
	for (size_t i = 0; i < s.nbought; i++)
		if (!strcmp(s.bought[i], id)) {
			free(s.bought[i]);
			s.bought[i] = s.bought[--s.nbought];
			return true;
		}
	return false;
}

// licenses_changed compares the licenses with those seen: one new is a
// purchase outside a buy (another PC, a renewal's new period), a
// durable's gone is revoked (refunded). A worker's.
static void licenses_changed(void) {
	licenses ls;
	asuka_error e = {0};
	if (!read_licenses(&ls, &e)) {
		asuka_error_free(&e);
		return;
	}
	refresh(&e);
	asuka_error_free(&e);
	lock();
	for (size_t i = 0; i < ls.n; i++) {
		const license *l = &ls.list[i];
		if (!l->active) continue;
		char *id = store_id_of(l->sku);
		add_on *a = find_locked(id);
		if (a && strcmp(a->store_id, id)) a = NULL;
		if (a && !bought_take_locked(id)) {
			const license *old = NULL;
			for (size_t k = 0; k < s.nactive; k++)
				if (!strcmp(s.active[k].sku, id)) old = &s.active[k];
			if (!old || old->expires != l->expires) {
				purchase_StoreTransaction t = transaction(a, "");
				t.date = now_ns();
				t.expires = l->expires;
				give_locked(t);
			}
		}
		free(id);
	}
	for (size_t k = 0; k < s.nactive; k++) {
		if (active_of(&ls, s.active[k].sku)) continue;
		add_on *a = find_locked(s.active[k].sku);
		if (a && !strcmp(a->store_id, s.active[k].sku) && kind_of(a) == purchase_Kind_nonConsumable) {
			purchase_StoreTransaction t = transaction(a, "");
			t.state = purchase_State_revoked;
			t.revoked = now_ns();
			give_locked(t);
		}
	}
	keep_active_locked(&ls);
	unlock();
	licenses_free(&ls);
}

static void changed_run(void *p) {
	asuka_winrt();
	licenses_changed();
}

static void on_licenses_changed(void *ctx, void *a, void *b) { asuka_desktop_spawn(changed_run, NULL); }

// open_context makes the Store's context once (over the foreground
// window, for its dialogs), with its add-ons and licenses read and their
// changes followed. A worker's.
static BOOL CALLBACK open_context(INIT_ONCE *once, void *param, void **ctx) {
	void *st = NULL;
	HRESULT hr = asuka_winrt_factory("Windows.Services.Store.StoreContext", &iid_statics, &st);
	if (SUCCEEDED(hr)) hr = ASUKA_SLOT(st, statics_get_default, get_ptr)(st, &s.ctx);
	asuka_com_release(st);
	if (FAILED(hr) || !s.ctx) {
		asuka_win_fail(&s.failed, FAILED(hr) ? hr : E_FAIL, "StoreContext");
		s.ctx = NULL;
		return TRUE;
	}
	HWND hwnd = GetForegroundWindow();
	void *iw = hwnd ? asuka_com_query(s.ctx, &iid_init_window) : NULL;
	if (iw) {
		ASUKA_SLOT(iw, 3, HRESULT(STDMETHODCALLTYPE *)(void *, HWND))(iw, hwnd);  // Initialize
		asuka_com_release(iw);
	}
	asuka_error e = {0};
	refresh(&e);
	asuka_error_free(&e);
	// The licenses as the app starts: a consumable's not yet fulfilled is
	// a transaction left unfinished.
	licenses ls;
	if (read_licenses(&ls, &e)) {
		lock();
		keep_active_locked(&ls);
		for (size_t i = 0; i < s.nactive; i++) {
			add_on *a = find_locked(s.active[i].sku);
			if (a && !strcmp(a->store_id, s.active[i].sku) && ends_with(a->kind, "Consumable")) give_locked(transaction(a, ""));
		}
		unlock();
		licenses_free(&ls);
	}
	asuka_error_free(&e);
	GUID iid = asuka_winrt_iid("pinterface(" TYPED_HANDLER ";" RC_CONTEXT ";cinterface(IInspectable))");
	void *h = asuka_winrt_delegate(&iid, on_licenses_changed, NULL, NULL);
	int64_t token;
	ASUKA_SLOT(s.ctx, ctx_add_licenses_changed, HRESULT(STDMETHODCALLTYPE *)(void *, void *, int64_t *))(s.ctx, h, &token);
	asuka_com_release(h);
	return TRUE;
}

// context is the Store's context (a worker's, in the MTA): false with
// err set when there's none.
static bool context(asuka_error *err) {
	asuka_winrt();
	InitOnceExecuteOnce(&s.once, open_context, NULL, NULL);
	if (s.ctx) return true;
	asuka_error_failed(err, s.failed.domain ? s.failed.domain : "purchase", s.failed.code, "%s", s.failed.message ? s.failed.message : "no Store");
	return false;
}

// ---- The calls: each a worker's ----

typedef struct job job;
struct job {
	void (*run)(job *j, asuka_error *err);  // false: failed
	asuka_call *call;
	const void *a, *b;
	purchase_list_Item items;  // products', the call's
};

static void run(void *p) {
	job *j = p;
	asuka_error err = {0};
	if (context(&err)) j->run(j, &err);
	if (err.status) asuka_call_fail(j->call, &err);
	free(j);
}

static void start_job(job *j) {
	asuka_call *call = j->call;
	if (asuka_desktop_spawn(run, j)) return;
	asuka_error e = {0};
	asuka_error_failed(&e, "purchase", 0, "no thread");
	asuka_call_fail(call, &e);
	free(j);
}

static void start(asuka_call *call, void (*fn)(job *, asuka_error *), const void *a, const void *b) {
	job *j = calloc(1, sizeof *j);
	*j = (job){fn, call, a, b};
	start_job(j);
}

static void products_run(job *j, asuka_error *err) {
	const purchase_list_Item *items = &j->items;
	if (!refresh(err)) return;
	purchase_list_Product out = {calloc(items->len + 1, sizeof(purchase_Product)), 0};
	for (size_t i = 0; i < items->len; i++) {
		add_on a;
		if (!find(items->data[i].id, &a)) continue;
		out.data[out.len++] = product(&a, items->data[i].id);
		add_on_free(&a);
	}
	purchase_Store_products_done(j->call, out);
}

static void buy_run(job *j, asuka_error *err) {
	const purchase_Item *it = j->a;
	add_on a;
	if (!find(it->id, &a)) {
		if (!refresh(err)) return;
		if (!find(it->id, &a)) {
			asuka_error_declared(err, purchase_PurchaseError_unknownProduct);
			return;
		}
	}
	HSTRING id = asuka_hstring(a.store_id);
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(s.ctx, ctx_request_purchase, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING, void **))(s.ctx, id, &op);
	asuka_hstring_free(id);
	void *res = SUCCEEDED(hr) ? await(op, RC_PURCHASE) : NULL;
	int32_t status = st_server;
	if (res) ASUKA_SLOT(res, purchase_status, get_i32)(res, &status);
	purchase_StoreResult r = {purchase_Status_cancelled, NULL};
	switch (status) {
	case st_purchased: {
		lock();
		s.bought = realloc(s.bought, (s.nbought + 1) * sizeof *s.bought);
		s.bought[s.nbought++] = asuka_strdup(a.store_id);
		unlock();
		r.status = purchase_Status_bought;
		r.transaction = malloc(sizeof *r.transaction);
		*r.transaction = transaction(&a, it->id);
		r.transaction->date = now_ns();
		break;
	}
	case st_already:
		asuka_error_declared(err, purchase_PurchaseError_alreadyOwned);
		break;
	case st_not_purchased:
		break;
	case st_network:
		asuka_error_failed(err, "purchase", 0, "purchase: the Microsoft Store can't be reached");
		break;
	default:
		if (FAILED(hr)) asuka_win_fail(err, hr, "the Microsoft Store");
		else if (!res || !extended(res, purchase_error, err)) asuka_error_failed(err, "purchase", 0, "purchase: the Microsoft Store failed");
	}
	asuka_com_release(res);
	add_on_free(&a);
	if (!err->status) purchase_Store_buy_done(j->call, r);
	else purchase_StoreResult_free(&r);
}

static void finish_run(job *j, asuka_error *err) {
	const purchase_StoreTransaction *t = j->a;
	if (t->kind != purchase_Kind_consumable) {  // a durable's license is its own
		purchase_Store_finish_done(j->call);
		return;
	}
	char *tracking = t->id && t->id[0] == '{' ? asuka_strdup(t->id) : tracking_id();
	size_t n = strlen(tracking);
	tracking[n - 1] = 0;
	GUID g = asuka_guid(tracking + 1);
	free(tracking);
	char *sid = store_id_of(t->verification.token ? t->verification.token : "");
	HSTRING id = asuka_hstring(sid);
	free(sid);
	void *op = NULL;
	// ReportConsumableFulfillmentAsync(HSTRING, UINT32, GUID, out).
	HRESULT hr = ASUKA_SLOT(s.ctx, ctx_report_fulfillment, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING, UINT32, GUID, void **))(
	    s.ctx, id, (UINT32)(t->quantity > 1 ? t->quantity : 1), g, &op);
	asuka_hstring_free(id);
	if (FAILED(hr) || !op) {
		asuka_win_fail(err, FAILED(hr) ? hr : E_FAIL, "the Microsoft Store");
		return;
	}
	void *res = await(op, RC_CONSUMABLE);
	if (!res) {
		asuka_error_failed(err, "purchase", 0, "the Microsoft Store didn't answer");
		return;
	}
	int32_t status = 3;
	ASUKA_SLOT(res, consumable_status, get_i32)(res, &status);
	if (!extended(res, consumable_error, err) && status != 0)
		asuka_error_failed(err, "purchase", 0, "purchase: the Microsoft Store didn't take the fulfillment");
	asuka_com_release(res);
	if (!err->status) purchase_Store_finish_done(j->call);
}

static bool entitlements(purchase_list_StoreTransaction *out, asuka_error *err) {
	licenses ls;
	if (!read_licenses(&ls, err)) return false;
	*out = (purchase_list_StoreTransaction){calloc(ls.n + 1, sizeof(purchase_StoreTransaction)), 0};
	lock();
	for (size_t i = 0; i < ls.n; i++) {
		char *id = store_id_of(ls.list[i].sku);
		add_on *a = find_locked(id);
		if (a && !strcmp(a->store_id, id) && ls.list[i].active && kind_of(a) != purchase_Kind_consumable) {
			purchase_StoreTransaction t = transaction(a, "");
			t.expires = ls.list[i].expires;
			out->data[out->len++] = t;
		}
		free(id);
	}
	unlock();
	licenses_free(&ls);
	return true;
}

static void entitlements_run(job *j, asuka_error *err) {
	purchase_list_StoreTransaction out;
	if (entitlements(&out, err)) purchase_Store_entitlements_done(j->call, out);
}

static void restore_run(job *j, asuka_error *err) {
	licenses_changed();
	purchase_list_StoreTransaction out;
	if (entitlements(&out, err)) purchase_Store_restore_done(j->call, out);
}

static void statuses_run(job *j, asuka_error *err) {
	const char *group = j->a;
	licenses ls;
	if (!read_licenses(&ls, err)) return;
	purchase_list_StoreStatus out = {calloc(ls.n + 1, sizeof(purchase_StoreStatus)), 0};
	lock();
	for (size_t i = 0; i < ls.n; i++) {
		const license *l = &ls.list[i];
		char *id = store_id_of(l->sku);
		add_on *a = find_locked(id);
		free(id);
		if (!a || !a->subscription || (strcmp(l->offer_token, group) && strcmp(a->store_id, group))) continue;
		purchase_StoreStatus st = {asuka_strdup(product_id_of(a)), l->active ? purchase_SubscriptionState_subscribed : purchase_SubscriptionState_expired,
		                                  l->active};
		st.transaction = transaction(a, "");
		if (l->expires) st.renewal = st.transaction.expires = l->expires;
		out.data[out.len++] = st;
	}
	unlock();
	licenses_free(&ls);
	purchase_Store_statuses_done(j->call, out);
}

static void open_url(void *url) {
	ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

static bool s_available(purchase_Store *self, asuka_error *err) { return true; }

static void s_declare(purchase_Store *self, purchase_list_Item items, asuka_error *err) {}  // the Store says what each add-on is

static void s_products(purchase_Store *self, purchase_list_Item items, asuka_call *call) {
	job *j = calloc(1, sizeof *j);
	*j = (job){products_run, call};
	j->items = items;  // the call's, which live until it answers
	start_job(j);
}

static void s_buy(purchase_Store *self, const purchase_Item *item, const purchase_StoreOptions *options, asuka_call *call) {
	start(call, buy_run, item, options);
}

static void s_finish(purchase_Store *self, const purchase_StoreTransaction *t, asuka_call *call) { start(call, finish_run, t, NULL); }
static void s_entitlements(purchase_Store *self, asuka_call *call) { start(call, entitlements_run, NULL, NULL); }
static void s_restore(purchase_Store *self, asuka_call *call) { start(call, restore_run, NULL, NULL); }
static void s_statuses(purchase_Store *self, const char *group, asuka_call *call) { start(call, statuses_run, group, NULL); }

static void s_manage(purchase_Store *self, const char *product, asuka_call *call) {
	open_url(L"https://account.microsoft.com/services");
	purchase_Store_manageSubscriptions_done(call);
}

static void s_refund(purchase_Store *self, const char *id, asuka_call *call) {
	asuka_error e = {0};
	asuka_error_unsupported(&e, "the Microsoft Store's refunds are asked for on its website");
	asuka_call_fail(call, &e);
}

static void s_redeem(purchase_Store *self, asuka_call *call) {
	open_url(L"https://www.microsoft.com/redeem");
	purchase_Store_redeemCode_done(call);
}

static void s_storefront(purchase_Store *self, asuka_call *call) {
	purchase_Store_storefront_done(call, (purchase_Storefront){user_country(), asuka_strdup("")});
}

static void events_cancelled(void *p) {
	asuka_call *call = p;
	lock();
	for (size_t i = 0; i < s.nreaders; i++)
		if (s.readers[i].call == call) s.readers[i] = s.readers[--s.nreaders];
	unlock();
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(call, &e);
}

static void open_run(void *p) {
	asuka_error e = {0};
	context(&e);
	asuka_error_free(&e);
}

static void s_events(purchase_Store *self, purchase_fn_StoreEvent fn, asuka_call *call) {
	lock();
	s.readers = realloc(s.readers, (s.nreaders + 1) * sizeof *s.readers);
	s.readers[s.nreaders++] = (struct reader){fn, call};
	purchase_StoreEvent *kept = s.kept;
	size_t n = s.nkept;
	s.kept = NULL, s.nkept = 0;
	for (size_t i = 0; i < n; i++) {
		purchase_fn_StoreEvent_call(fn, &kept[i]);
		purchase_StoreEvent_free(&kept[i]);
	}
	unlock();
	free(kept);
	asuka_call_on_cancel(call, events_cancelled, call);
	asuka_desktop_spawn(open_run, NULL);  // listening from the start
}

static void store_release(purchase_Store *self) {}

static const purchase_Store_vtable vt = {
    .release = store_release,
    .available = s_available,
    .declare = s_declare,
    .products = s_products,
    .buy = s_buy,
    .finish = s_finish,
    .entitlements = s_entitlements,
    .restore = s_restore,
    .statuses = s_statuses,
    .manageSubscriptions = s_manage,
    .requestRefund = s_refund,
    .redeemCode = s_redeem,
    .storefront = s_storefront,
    .events_ = s_events,
};

// packaged reports whether the app has a package identity (installed
// from the Store, or an MSIX).
static bool packaged(void) {
	LONG(WINAPI * name)(UINT32 *, wchar_t *) = NULL;
	*(FARPROC *)&name = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFullName");
	UINT32 n = 0;
	return name && name(&n, NULL) != 15700;  // APPMODEL_ERROR_NO_PACKAGE
}

purchase_Store *purchase_make_Store(asuka_error *err) {
	static purchase_Store store = {&vt};
	static bool made;
	if (!packaged()) {
		asuka_error_declared(err, purchase_PurchaseError_notAvailable);
		return NULL;
	}
	if (!made) {
		made = true;
		InitializeCriticalSection(&s.mu);
		InitOnceInitialize(&s.once);
		// Listening from the start: what the user bought while the app
		// didn't read comes to the first events().
		asuka_desktop_spawn(open_run, NULL);
	}
	return &store;
}
