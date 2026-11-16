// platform/location on Windows and Linux (macOS runs the package's Swift):
// the methods, over each platform's location.h. Windows: WinRT's
// Geolocator and GeofenceMonitor; Linux: the location portal, else
// GeoClue2 (no geofences: the package computes the regions from the
// positions). A desktop app runs out of sight, so its background updates
// are positions it watches while it runs, each a batch. Everything runs
// on the main thread, where the callbacks are called.
#include "location.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

// A started call that may be cancelled before the platform answers: the
// platform's answer comes through it once, whichever is first.
typedef struct {
	asuka_call *call;
	bool answered;  // cancelled, and answered so
} pending;

static void pending_cancel(void *p) {
	pending *q = p;
	q->answered = true;
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(q->call, &e);
}

static pending *pend(asuka_call *call) {
	pending *p = calloc(1, sizeof *p);
	p->call = call;
	asuka_call_on_cancel(call, pending_cancel, p);
	return p;
}

// settle is the call to answer (NULL: it's answered), freeing p.
static asuka_call *settle(pending *p) {
	asuka_call *c = p->answered ? NULL : p->call;
	free(p);
	return c;
}

static void fail_declared(asuka_call *c, uint32_t index) {
	asuka_error e = {0};
	asuka_error_declared(&e, index);
	asuka_call_fail(c, &e);
}

static void fail_cancelled(asuka_call *c) {
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(c, &e);
}

static int64_t now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

location_Region location_copy_region(const location_Region *r) {
	location_Region c = *r;
	c.id = asuka_strdup(r->id);
	c.events_.data = r->events_.len ? malloc(r->events_.len * sizeof *r->events_.data) : NULL;
	if (r->events_.len) memcpy(c.events_.data, r->events_.data, r->events_.len * sizeof *r->events_.data);
	return c;
}

// ---- Followers: the authorization's, the events' ----

typedef struct follower {
	union {
		location_fn_Authorization auth;
		location_fn_Event event;
	} fn;
	asuka_call *call;
	struct follower **list;
	struct follower *next;
} follower;

static follower *auth_followers, *event_followers;

static void unfollow(void *p) {
	follower *f = p;
	for (follower **q = f->list; *q; q = &(*q)->next)
		if (*q == f) {
			*q = f->next;
			break;
		}
	fail_cancelled(f->call);
	free(f);
}

static follower *follow(follower **list, asuka_call *call) {
	follower *f = calloc(1, sizeof *f);
	f->call = call;
	f->list = list;
	f->next = *list;
	*list = f;
	asuka_call_on_cancel(call, unfollow, f);
	return f;
}

void location_authorization_changed(void) {
	location_Authorization a = location_access();
	for (follower *f = auth_followers; f; f = f->next) location_fn_Authorization_call(f->fn.auth, &a);
}

static int64_t next_event;

// event gives the followers an event (its id the next).
static void event(location_Event *e) {
	e->id = ++next_event;
	for (follower *f = event_followers; f; f = f->next) location_fn_Event_call(f->fn.event, e);
}

// ---- The methods ----

static location_Authorization m_authorization(location_Locator *self, asuka_error *err) { return location_access(); }

static void m_watchAuthorization(location_Locator *self, location_fn_Authorization changed, asuka_call *call) {
	follower *f = follow(&auth_followers, call);
	f->fn.auth = changed;
	location_Authorization a = location_access();
	location_fn_Authorization_call(changed, &a);
}

static void requested(void *p) {
	asuka_call *c = settle(p);
	if (c) location_Locator_request_done(c, location_access());
}

static void m_request(location_Locator *self, bool always, asuka_call *call) { location_request_access(requested, pend(call)); }

static void m_requestPrecise(location_Locator *self, const char *purpose, asuka_call *call) {
	location_Locator_requestPrecise_done(call, location_access());  // the platforms have no approximate choice to undo
}

static void fixed(void *p, const location_Fix *pos, asuka_error *e) {
	asuka_call *c = settle(p);
	if (!c) {
		if (e) asuka_error_free(e);
	} else if (e && e->status) asuka_call_fail(c, e);
	else location_Locator_current_done(c, *pos);
}

static void m_current(location_Locator *self, asuka_call *call) { location_fix(fixed, pend(call)); }

// A watch: positions to its callback until the task goes, or they fail.
typedef struct {
	location_fn_Fix moved;
	asuka_call *call;
	void *source;
	bool starting;
	asuka_error failed;  // while starting
	bool over;
} watch;

static void watch_moved(void *p, const location_Fix *pos) {
	watch *w = p;
	if (!w->over) location_fn_Fix_call(w->moved, pos);
}

static void watch_failed(void *p, asuka_error *e) {
	watch *w = p;
	if (w->over) {
		asuka_error_free(e);
		return;
	}
	if (w->starting) {
		asuka_error_free(&w->failed);
		w->failed = *e;
		return;
	}
	w->over = true;
	location_stop(w->source);
	asuka_call_fail(w->call, e);
	free(w);
}

static void watch_cancel(void *p) {
	watch *w = p;
	w->over = true;
	location_stop(w->source);
	fail_cancelled(w->call);
	free(w);
}

static void m_watch(location_Locator *self, location_fn_Fix moved, asuka_call *call) {
	watch *w = calloc(1, sizeof *w);
	w->moved = moved;
	w->call = call;
	w->starting = true;
	w->source = location_start((location_sink){watch_moved, watch_failed, w});
	w->starting = false;
	if (w->failed.status) {
		w->over = true;
		location_stop(w->source);
		asuka_call_fail(call, &w->failed);
		free(w);
		return;
	}
	asuka_call_on_cancel(call, watch_cancel, w);
}

static void named(void *p, char *name, asuka_error *e) {
	asuka_call *c = settle(p);
	if (!c) {
		free(name);
		if (e) asuka_error_free(e);
	} else if (e && e->status) {
		free(name);
		asuka_call_fail(c, e);
	} else {
		location_Locator_placeName_done(c, name);
	}
}

static void m_placeName(location_Locator *self, double latitude, double longitude, asuka_call *call) {
	location_geocode(latitude, longitude, named, pend(call));
}

static void m_openSettings(location_Locator *self, asuka_error *err) { location_open_settings(err); }

static location_Support m_support(location_Locator *self, asuka_error *err) { return location_support(); }

// The background updates: a source whose positions are batches.
static void *updates;

static void update_moved(void *ctx, const location_Fix *p) {
	location_Event e = {.positions = {(location_Fix *)p, 1}};
	event(&e);
}

static void update_failed(void *ctx, asuka_error *e) { asuka_error_free(e); }

static void start_updates(void *p) {
	asuka_call *c = settle(p);
	if (!c) return;
	location_Level l = location_access().level;
	if (l != location_Level_whenInUse && l != location_Level_always) {
		fail_declared(c, location_LocationError_denied);
		return;
	}
	if (updates) location_stop(updates);
	updates = location_start((location_sink){update_moved, update_failed, NULL});
	location_Locator_startUpdates_done(c);
}

static void m_startUpdates(location_Locator *self, const location_Updates *options, asuka_call *call) {
	location_request_access(start_updates, pend(call));
}

static void m_stopUpdates(location_Locator *self, asuka_error *err) {
	if (updates) location_stop(updates);
	updates = NULL;
}

static void m_updating(location_Locator *self, asuka_call *call) { location_Locator_updating_done(call, updates != NULL); }

static void m_monitor(location_Locator *self, const location_Region *region, asuka_call *call) {
	asuka_error e = {0};
	location_add_fence(region, &e);
	if (e.status) asuka_call_fail(call, &e);
	else location_Locator_monitor_done(call);
}

static void m_stopMonitoring(location_Locator *self, const char *id, asuka_call *call) {
	if (id && *id) location_remove_fence(id);
	location_Locator_stopMonitoring_done(call);
}

static void m_stopMonitoringAll(location_Locator *self, asuka_call *call) {
	location_remove_fence("");
	location_Locator_stopMonitoringAll_done(call);
}

static void m_monitored(location_Locator *self, asuka_call *call) { location_Locator_monitored_done(call, location_fences()); }

static void crossed(const char *region, location_Transition kind, const location_Fix *at) {
	location_Event e = {.crossing = true, .region = (char *)region, .kind = kind, .time = now_ns()};
	if (at) e.at = *at;
	event(&e);
}

static void m_events(location_Locator *self, location_fn_Event fn, asuka_call *call) {
	follower *f = follow(&event_followers, call);
	f->fn.event = fn;
	location_watch_fences(crossed);
}

// A desktop app isn't put to sleep for its events.
static void m_hold(location_Locator *self, int64_t id, asuka_error *err) {}
static void m_done(location_Locator *self, int64_t id, asuka_error *err) {}

static void m_release(location_Locator *self) {}  // the one locator lives with the app

static const location_Locator_vtable vt = {
	.release = m_release,
	.authorization = m_authorization,
	.watchAuthorization = m_watchAuthorization,
	.request = m_request,
	.requestPrecise = m_requestPrecise,
	.current = m_current,
	.watch = m_watch,
	.placeName = m_placeName,
	.openSettings = m_openSettings,
	.support = m_support,
	.startUpdates = m_startUpdates,
	.stopUpdates = m_stopUpdates,
	.updating = m_updating,
	.monitor = m_monitor,
	.stopMonitoring = m_stopMonitoring,
	.stopMonitoringAll = m_stopMonitoringAll,
	.monitored = m_monitored,
	.events_ = m_events,
	.hold = m_hold,
	.done = m_done,
};

location_Locator *location_make_Locator(asuka_error *err) {
	static location_Locator locator = {&vt};
	return &locator;
}
