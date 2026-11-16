// Windows: WinRT's Geolocator (the user's permission through
// RequestAccessAsync; a desktop app that's allowed has it always, as it
// runs out of sight) and the app's GeofenceMonitor, while the app runs.
// WinRT answers on its own threads: everything is posted back to the
// main thread. Windows has no approximate choice for an app, and no
// geocoder without a map service key: placeName is unsupported.
#include "asuka_windows.h"
#include "location.h"

#include <math.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

static const GUID iid_geolocator = {0xA9C3BF62, 0x4524, 0x4989, {0x8A, 0xA9, 0xDE, 0x01, 0x9D, 0x2E, 0x55, 0x1F}};
static const GUID iid_geolocator_statics = {0x9A8E7571, 0x2DF5, 0x4591, {0x9F, 0x87, 0xEB, 0x5F, 0xD8, 0x94, 0xE9, 0xB7}};
static const GUID iid_monitor_statics = {0x2DD32FCF, 0x7E75, 0x4899, {0xAC, 0xE3, 0x2B, 0xD0, 0xA6, 0x5C, 0xCE, 0x06}};
static const GUID iid_geofence_factory = {0x841F624B, 0x325F, 0x4B90, {0xBC, 0xA7, 0x2B, 0x80, 0x22, 0xA9, 0x37, 0x96}};
static const GUID iid_geocircle_factory = {0xAFD6531F, 0x72B1, 0x4F7D, {0x87, 0xCC, 0x4E, 0xD4, 0xC9, 0x84, 0x9C, 0x05}};
static const GUID iid_geoshape = {0xC99CA2AF, 0xC729, 0x43C1, {0x8F, 0xAB, 0xD6, 0xDE, 0xC9, 0x14, 0xDF, 0x7E}};

#define TYPED_HANDLER "{9de1c534-6ae1-11e0-84e1-18a905bcc53f}"
#define GEOLOCATOR "rc(Windows.Devices.Geolocation.Geolocator;{a9c3bf62-4524-4989-8aa9-de019d2e551f})"
#define GEOPOSITION "rc(Windows.Devices.Geolocation.Geoposition;{c18d0454-7d41-4ff7-a957-9dffb4ef7f5b})"
#define POSITION_ARGS "rc(Windows.Devices.Geolocation.PositionChangedEventArgs;{37859ce5-9d1e-46c5-bf3b-6ad8cac1a093})"
#define STATUS_ARGS "rc(Windows.Devices.Geolocation.StatusChangedEventArgs;{3453d2da-8c93-4111-a205-9aecfc9be5c0})"
#define MONITOR "rc(Windows.Devices.Geolocation.Geofencing.GeofenceMonitor;{4c0f5f78-1c1f-4621-bbbd-833b92247226})"
#define ACCESS "enum(Windows.Devices.Geolocation.GeolocationAccessStatus;i4)"

// Geolocator's LocationStatus.
enum { ready, initializing, no_data, disabled, not_initialized, not_available };
// Geofence states (a report's, and those a geofence reports).
enum { entered = 1, exited = 2, removed = 4 };

typedef HRESULT(STDMETHODCALLTYPE *get_ptr)(void *, void **);
typedef HRESULT(STDMETHODCALLTYPE *get_u32)(void *, UINT32 *);
typedef HRESULT(STDMETHODCALLTYPE *get_i32)(void *, INT32 *);
typedef HRESULT(STDMETHODCALLTYPE *get_f64)(void *, double *);
typedef HRESULT(STDMETHODCALLTYPE *get_i64)(void *, INT64 *);
typedef HRESULT(STDMETHODCALLTYPE *put_ptr)(void *, void *);
typedef HRESULT(STDMETHODCALLTYPE *add_handler)(void *, void *, INT64 *);
typedef HRESULT(STDMETHODCALLTYPE *remove_handler)(void *, INT64);

typedef struct mover {
	int id;
	location_sink sink;
	struct mover *next;
} mover;

typedef struct {
	char *id;
	int state;
	bool has_at;
	location_Fix at;
} report;

static struct {
	bool tried;
	void *geo;  // IGeolocator
	bool asked, allowed;
	mover *movers;
	int next;
	bool watching;
	INT64 token;
	bool fences_tried;
	void *fences;  // IGeofenceMonitor
	bool followed;
	location_list_Region regions;
	void (*crossed)(const char *, location_Transition, const location_Fix *);
} win;

static void authorization_changed(void *ctx) { location_authorization_changed(); }

static void status_changed(void *ctx, void *sender, void *args) { asuka_desktop_post(authorization_changed, NULL); }

static void *locator(void) {
	if (win.tried) return win.geo;
	win.tried = true;
	if (!asuka_winrt()) return NULL;
	void *g = NULL;
	if (FAILED(asuka_winrt_activate("Windows.Devices.Geolocation.Geolocator", &g)) || !g) return NULL;
	win.geo = asuka_com_query(g, &iid_geolocator);
	asuka_com_release(g);
	if (!win.geo) return NULL;
	ASUKA_SLOT(win.geo, 7, HRESULT(STDMETHODCALLTYPE *)(void *, INT32))(win.geo, 1);  // put_DesiredAccuracy: High
	GUID h = asuka_winrt_iid("pinterface(" TYPED_HANDLER ";" GEOLOCATOR ";" STATUS_ARGS ")");
	void *d = asuka_winrt_delegate(&h, status_changed, NULL, NULL);
	INT64 token;
	ASUKA_SLOT(win.geo, 17, add_handler)(win.geo, d, &token);  // add_StatusChanged
	asuka_com_release(d);
	return win.geo;
}

static int status(void) {
	INT32 s = not_available;
	ASUKA_SLOT(win.geo, 12, get_i32)(win.geo, &s);  // get_LocationStatus
	return s;
}

location_Authorization location_access(void) {
	if (!locator()) return (location_Authorization){.level = location_Level_denied, .off = true};
	switch (status()) {
	case disabled: return (location_Authorization){.level = location_Level_denied};
	case not_available: return (location_Authorization){.level = location_Level_denied, .off = true};
	case not_initialized:
		if (!win.asked) return (location_Authorization){.level = location_Level_notAsked};
		if (!win.allowed) return (location_Authorization){.level = location_Level_denied};
	}
	return (location_Authorization){.level = location_Level_always, .precise = true};
}

// ---- The user's permission ----

typedef struct {
	void (*done)(void *ctx);
	void *ctx;
	bool answered;  // the operation answered (else it failed)
	INT32 access;
} asking;

static void asked(void *p) {
	asking *a = p;
	if (a->answered) {
		win.asked = true;
		win.allowed = a->access == 1;  // Allowed
	}
	location_authorization_changed();
	a->done(a->ctx);
	free(a);
}

static void access_completed(void *ctx, void *op, void *st) {
	asking *a = ctx;
	if ((int)(intptr_t)st == 1) ASUKA_SLOT(op, 8, get_i32)(op, &a->access);  // Completed: GetResults
	a->answered = true;
	asuka_desktop_post(asked, a);
}

void location_request_access(void (*done)(void *ctx), void *ctx) {
	asking *a = calloc(1, sizeof *a);
	a->done = done;
	a->ctx = ctx;
	void *st = NULL, *op = NULL;
	if (!asuka_winrt() || FAILED(asuka_winrt_factory("Windows.Devices.Geolocation.Geolocator", &iid_geolocator_statics, &st)) ||
	    FAILED(ASUKA_SLOT(st, 6, get_ptr)(st, &op)) || !op) {  // RequestAccessAsync
		asuka_com_release(st);
		asked(a);
		return;
	}
	asuka_com_release(st);
	GUID h = asuka_winrt_iid("pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" ACCESS ")");
	void *d = asuka_winrt_delegate(&h, access_completed, a, NULL);
	ASUKA_SLOT(op, 6, put_ptr)(op, d);  // put_Completed
	asuka_com_release(d);
	asuka_com_release(op);
}

// ---- Positions ----

// optional is an IReference<double>'s value, or -1 (none, or NaN).
static double optional(void *coord, int slot) {
	void *ref = NULL;
	double v = -1;
	if (SUCCEEDED(ASUKA_SLOT(coord, slot, get_ptr)(coord, &ref)) && ref) {
		double x = 0;
		if (SUCCEEDED(ASUKA_SLOT(ref, 6, get_f64)(ref, &x)) && !isnan(x)) v = x;
		asuka_com_release(ref);
	}
	return v;
}

// read_fix reads an IGeoposition. Any thread.
static bool read_fix(void *pos, location_Fix *out) {
	void *c = NULL;
	if (FAILED(ASUKA_SLOT(pos, 6, get_ptr)(pos, &c)) || !c) return false;  // get_Coordinate
	location_Fix p = {0};
	INT64 ts = 0;
	ASUKA_SLOT(c, 6, get_f64)(c, &p.latitude);
	ASUKA_SLOT(c, 7, get_f64)(c, &p.longitude);
	ASUKA_SLOT(c, 9, get_f64)(c, &p.accuracy);
	ASUKA_SLOT(c, 13, get_i64)(c, &ts);  // DateTime: 100 ns since 1601
	{
		void *ref = NULL;  // get_Altitude: IReference<double>, null if not known
		if (SUCCEEDED(ASUKA_SLOT(c, 8, get_ptr)(c, &ref)) && ref) {
			double x = 0;
			if (SUCCEEDED(ASUKA_SLOT(ref, 6, get_f64)(ref, &x)) && !isnan(x)) p.altitude = x;
			asuka_com_release(ref);
		}
	}
	p.course = optional(c, 11);  // get_Heading
	p.speed = optional(c, 12);   // get_Speed
	p.time = (ts - 116444736000000000LL) * 100;
	asuka_com_release(c);
	if (isnan(p.latitude) || isnan(p.longitude)) return false;
	*out = p;
	return true;
}

typedef struct {
	void (*done)(void *ctx, const location_Fix *p, asuka_error *e);
	void *ctx;
	location_Fix p;
	int failure;  // 0 none, 1 denied, 2 unavailable, 3 unavailable (denied, or off)
} fixing;

static void fixed(void *p) {
	fixing *f = p;
	asuka_error e = {0};
	if (f->failure == 1) asuka_error_declared(&e, location_LocationError_denied);
	else if (f->failure == 2) asuka_error_failed(&e, "location", 0, "location: unavailable");
	else if (f->failure == 3) asuka_error_failed(&e, "location", 0, "location: unavailable (denied, or off)");
	if (e.status) f->done(f->ctx, NULL, &e);
	else f->done(f->ctx, &f->p, NULL);
	free(f);
}

static void position_completed(void *ctx, void *op, void *st) {
	fixing *f = ctx;
	void *pos = NULL;
	if ((int)(intptr_t)st == 1) ASUKA_SLOT(op, 8, get_ptr)(op, &pos);
	if (!pos) f->failure = (int)(intptr_t)st == 3 ? 3 : 2;  // Error
	else if (!read_fix(pos, &f->p)) f->failure = 2;
	asuka_com_release(pos);
	asuka_desktop_post(fixed, f);
}

static void fix_asked(void *p) {
	fixing *f = p;
	if (!win.allowed || !locator()) {
		f->failure = 1;
		fixed(f);
		return;
	}
	void *op = NULL;
	HRESULT hr = ASUKA_SLOT(win.geo, 13, get_ptr)(win.geo, &op);  // GetGeopositionAsync
	if (FAILED(hr) || !op) {
		f->failure = hr == E_ACCESSDENIED ? 1 : 2;
		fixed(f);
		return;
	}
	GUID h = asuka_winrt_iid("pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" GEOPOSITION ")");
	void *d = asuka_winrt_delegate(&h, position_completed, f, NULL);
	ASUKA_SLOT(op, 6, put_ptr)(op, d);
	asuka_com_release(d);
	asuka_com_release(op);
}

void location_fix(void (*done)(void *ctx, const location_Fix *p, asuka_error *e), void *ctx) {
	fixing *f = calloc(1, sizeof *f);
	f->done = done;
	f->ctx = ctx;
	location_request_access(fix_asked, f);
}

static void moved_main(void *p) {
	location_Fix *pos = p;
	for (mover *m = win.movers, *next; m; m = next) {
		next = m->next;  // a mover may stop
		m->sink.moved(m->sink.ctx, pos);
	}
	free(pos);
}

static void position_changed(void *ctx, void *sender, void *args) {
	void *pos = NULL;
	ASUKA_SLOT(args, 6, get_ptr)(args, &pos);  // get_Position
	if (!pos) return;
	location_Fix *p = malloc(sizeof *p);
	if (read_fix(pos, p)) asuka_desktop_post(moved_main, p);
	else free(p);
	asuka_com_release(pos);
}

static mover *find(int id) {
	for (mover *m = win.movers; m; m = m->next)
		if (m->id == id) return m;
	return NULL;
}

static void watch_asked(void *p) {
	mover *m = find((int)(intptr_t)p);
	if (!m) return;
	if (!win.allowed || !locator()) {
		asuka_error e = {0};
		asuka_error_declared(&e, location_LocationError_denied);
		m->sink.failed(m->sink.ctx, &e);
		return;
	}
	if (!win.watching) {
		win.watching = true;
		GUID h = asuka_winrt_iid("pinterface(" TYPED_HANDLER ";" GEOLOCATOR ";" POSITION_ARGS ")");
		void *d = asuka_winrt_delegate(&h, position_changed, NULL, NULL);
		ASUKA_SLOT(win.geo, 15, add_handler)(win.geo, d, &win.token);  // add_PositionChanged
		asuka_com_release(d);
	}
}

void *location_start(location_sink sink) {
	mover *m = calloc(1, sizeof *m);
	m->id = ++win.next;
	m->sink = sink;
	m->next = win.movers;
	win.movers = m;
	location_request_access(watch_asked, (void *)(intptr_t)m->id);
	return (void *)(intptr_t)m->id;
}

void location_stop(void *source) {
	int id = (int)(intptr_t)source;
	for (mover **q = &win.movers; *q; q = &(*q)->next)
		if ((*q)->id == id) {
			mover *m = *q;
			*q = m->next;
			free(m);
			break;
		}
	if (!win.movers && win.watching && win.geo) {
		ASUKA_SLOT(win.geo, 16, remove_handler)(win.geo, win.token);  // remove_PositionChanged
		win.watching = false;
		win.token = 0;
	}
}

void location_geocode(double latitude, double longitude, void (*done)(void *ctx, char *name, asuka_error *e), void *ctx) {
	asuka_error e = {0};
	asuka_error_unsupported(&e, "Windows has no geocoder an app reaches without a map service key");
	done(ctx, NULL, &e);
}

void location_open_settings(asuka_error *err) {
	HINSTANCE r = ShellExecuteW(NULL, NULL, L"ms-settings:privacy-location", NULL, NULL, SW_SHOWNORMAL);
	if ((INT_PTR)r <= 32) asuka_win_fail(err, HRESULT_FROM_WIN32(GetLastError()), "location: the settings");
}

// ---- Geofences: the app's GeofenceMonitor, its geofences circles ----

static void *monitor(void) {
	if (win.fences_tried) return win.fences;
	win.fences_tried = true;
	void *st = NULL;
	if (!asuka_winrt() || FAILED(asuka_winrt_factory("Windows.Devices.Geolocation.Geofencing.GeofenceMonitor", &iid_monitor_statics, &st))) return NULL;
	ASUKA_SLOT(st, 6, get_ptr)(st, &win.fences);  // get_Current
	asuka_com_release(st);
	return win.fences;
}

location_Support location_support(void) { return (location_Support){.regions = monitor() != NULL}; }

// vector is the monitor's IVector<Geofence> (a reference).
static void *vector(void) {
	void *v = NULL;
	ASUKA_SLOT(win.fences, 7, get_ptr)(win.fences, &v);  // get_Geofences
	return v;
}

static char *fence_id(void *f) {
	HSTRING h = NULL;
	ASUKA_SLOT(f, 9, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING *))(f, &h);  // get_Id
	char *s = asuka_hstring_utf8(h);
	asuka_hstring_free(h);
	return s;
}

typedef HRESULT(STDMETHODCALLTYPE *get_at)(void *, UINT32, void **);

// remove_monitored stops the monitor's geofence id ("": all).
static void remove_monitored(const char *id) {
	void *v = vector();
	if (!v) return;
	if (!*id) {
		ASUKA_SLOT(v, 15, HRESULT(STDMETHODCALLTYPE *)(void *))(v);  // Clear
	} else {
		UINT32 n = 0;
		ASUKA_SLOT(v, 7, get_u32)(v, &n);  // get_Size
		for (INT64 i = (INT64)n - 1; i >= 0; i--) {
			void *f = NULL;
			if (FAILED(ASUKA_SLOT(v, 6, get_at)(v, (UINT32)i, &f)) || !f) continue;  // GetAt
			char *fid = fence_id(f);
			if (!strcmp(fid, id)) ASUKA_SLOT(v, 12, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32))(v, (UINT32)i);  // RemoveAt
			free(fid);
			asuka_com_release(f);
		}
	}
	asuka_com_release(v);
}

static void forget(const char *id) {
	size_t k = 0;
	for (size_t i = 0; i < win.regions.len; i++) {
		if (!*id || !strcmp(win.regions.data[i].id, id)) location_Region_free(&win.regions.data[i]);
		else win.regions.data[k++] = win.regions.data[i];
	}
	win.regions.len = k;
}

typedef struct {
	double latitude, longitude, altitude;
} basic_geoposition;

void location_add_fence(const location_Region *r, asuka_error *err) {
	if (!monitor()) {
		asuka_error_declared(err, location_LocationError_unavailable);
		return;
	}
	UINT32 states = 0;
	for (size_t i = 0; i < r->events_.len; i++) {
		if (r->events_.data[i] == location_Transition_enter) states |= entered;
		if (r->events_.data[i] == location_Transition_exit) states |= exited;
	}
	void *cf = NULL, *circle = NULL, *shape = NULL, *ff = NULL, *fence = NULL;
	HRESULT hr = asuka_winrt_factory("Windows.Devices.Geolocation.Geocircle", &iid_geocircle_factory, &cf);
	if (SUCCEEDED(hr)) {
		basic_geoposition at = {r->latitude, r->longitude, 0};
		// Create(BasicGeoposition, radius): the compiler passes the struct as
		// the platform's ABI has it.
		hr = ASUKA_SLOT(cf, 6, HRESULT(STDMETHODCALLTYPE *)(void *, basic_geoposition, double, void **))(cf, at, r->radius, &circle);
		if (SUCCEEDED(hr) && !circle) hr = E_FAIL;
	}
	if (SUCCEEDED(hr)) {
		shape = asuka_com_query(circle, &iid_geoshape);
		if (!shape) hr = E_NOINTERFACE;
	}
	if (SUCCEEDED(hr)) hr = asuka_winrt_factory("Windows.Devices.Geolocation.Geofencing.Geofence", &iid_geofence_factory, &ff);
	if (SUCCEEDED(hr)) {
		HSTRING name = asuka_hstring(r->id);
		// CreateWithMonitorStatesAndDwellTime(id, shape, states, singleUse, dwellTime: 100 ns).
		hr = ASUKA_SLOT(ff, 8, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING, void *, UINT32, boolean, INT64, void **))(ff, name, shape, states, 0, 0, &fence);
		asuka_hstring_free(name);
		if (SUCCEEDED(hr) && !fence) hr = E_FAIL;
	}
	if (SUCCEEDED(hr)) {
		remove_monitored(r->id);
		void *v = vector();
		hr = v ? ASUKA_SLOT(v, 13, put_ptr)(v, fence) : E_FAIL;  // Append
		asuka_com_release(v);
	}
	asuka_com_release(fence);
	asuka_com_release(ff);
	asuka_com_release(shape);
	asuka_com_release(circle);
	asuka_com_release(cf);
	if (FAILED(hr)) {
		asuka_win_fail(err, hr, "location: Windows didn't take the geofence");
		return;
	}
	forget(r->id);
	win.regions.data = realloc(win.regions.data, (win.regions.len + 1) * sizeof *win.regions.data);
	win.regions.data[win.regions.len++] = location_copy_region(r);
}

void location_remove_fence(const char *id) {
	if (monitor()) remove_monitored(id);
	forget(id);
}

location_list_Region location_fences(void) {
	location_list_Region out = {0};
	if (!monitor()) return out;
	void *v = vector();
	if (!v) return out;
	UINT32 n = 0;
	ASUKA_SLOT(v, 7, get_u32)(v, &n);
	char **ids = calloc(n ? n : 1, sizeof *ids);
	for (UINT32 i = 0; i < n; i++) {
		void *f = NULL;
		if (SUCCEEDED(ASUKA_SLOT(v, 6, get_at)(v, i, &f)) && f) {
			ids[i] = fence_id(f);
			asuka_com_release(f);
		}
	}
	asuka_com_release(v);
	out.data = calloc(win.regions.len ? win.regions.len : 1, sizeof *out.data);
	for (size_t k = 0; k < win.regions.len; k++)
		for (UINT32 i = 0; i < n; i++)
			if (ids[i] && !strcmp(ids[i], win.regions.data[k].id)) {
				out.data[out.len++] = location_copy_region(&win.regions.data[k]);
				break;
			}
	for (UINT32 i = 0; i < n; i++) free(ids[i]);
	free(ids);
	return out;
}

typedef struct {
	report *r;
	size_t n;
} reports;

static void reported(void *p) {
	reports *rs = p;
	for (size_t i = 0; i < rs->n; i++) {
		report *r = &rs->r[i];
		const location_Fix *at = r->has_at ? &r->at : NULL;
		if (r->state == entered && win.crossed) win.crossed(r->id, location_Transition_enter, at);
		else if (r->state == exited && win.crossed) win.crossed(r->id, location_Transition_exit, at);
		else if (r->state == removed) forget(r->id);
		free(r->id);
	}
	free(rs->r);
	free(rs);
}

static void state_changed(void *ctx, void *sender, void *args) {
	void *view = NULL;
	if (FAILED(ASUKA_SLOT(win.fences, 11, get_ptr)(win.fences, &view)) || !view) return;  // ReadReports
	UINT32 n = 0;
	ASUKA_SLOT(view, 7, get_u32)(view, &n);
	reports *rs = calloc(1, sizeof *rs);
	rs->r = calloc(n ? n : 1, sizeof *rs->r);
	for (UINT32 i = 0; i < n; i++) {
		void *r = NULL;
		if (FAILED(ASUKA_SLOT(view, 6, get_at)(view, i, &r)) || !r) continue;
		report *rep = &rs->r[rs->n++];
		INT32 state = 0;
		ASUKA_SLOT(r, 6, get_i32)(r, &state);  // get_NewState
		rep->state = state;
		void *f = NULL;
		ASUKA_SLOT(r, 7, get_ptr)(r, &f);  // get_Geofence
		rep->id = f ? fence_id(f) : asuka_strdup("");
		asuka_com_release(f);
		void *pos = NULL;
		ASUKA_SLOT(r, 8, get_ptr)(r, &pos);  // get_Geoposition
		if (pos) rep->has_at = read_fix(pos, &rep->at);
		asuka_com_release(pos);
		asuka_com_release(r);
	}
	asuka_com_release(view);
	if (rs->n) {
		asuka_desktop_post(reported, rs);
	} else {
		free(rs->r);
		free(rs);
	}
}

void location_watch_fences(void (*crossed)(const char *, location_Transition, const location_Fix *)) {
	win.crossed = crossed;
	if (!monitor() || win.followed) return;
	win.followed = true;
	GUID h = asuka_winrt_iid("pinterface(" TYPED_HANDLER ";" MONITOR ";cinterface(IInspectable))");
	void *d = asuka_winrt_delegate(&h, state_changed, NULL, NULL);
	INT64 token;
	ASUKA_SLOT(win.fences, 9, add_handler)(win.fences, d, &token);  // add_GeofenceStateChanged
	asuka_com_release(d);
}
