// What location.c (the package's methods, the same on Windows and Linux)
// asks of each platform's C (location_windows.c, location_linux.c), and
// what it gives them. All on the main thread.
#ifndef LOCATION_H
#define LOCATION_H

#include "location.asuka.h"

// ---- The platform's ----

// location_access is what the user allowed, as the platform knows now.
location_Authorization location_access(void);
// location_request_access asks the user (once: then the platform
// remembers), then calls done(ctx).
void location_request_access(void (*done)(void *ctx), void *ctx);

// A position's taker: moved with each, failed (taking e's strings) when
// the positions can't come; neither after the source stops.
typedef struct {
	void (*moved)(void *ctx, const location_Position *p);
	void (*failed)(void *ctx, asuka_error *e);
	void *ctx;
} location_sink;
// location_start starts positions coming to s (asking first): a source,
// for location_stop. s.failed may be called before it returns.
void *location_start(location_sink s);
// location_stop stops a source; from its sink's callbacks too.
void location_stop(void *source);
// location_fix calls done once with one position, or why there's none
// (done takes e's strings).
void location_fix(void (*done)(void *ctx, const location_Position *p, asuka_error *e), void *ctx);
// location_geocode calls done once with a place's name (of malloc's,
// done's), or why there's none.
void location_geocode(double latitude, double longitude, void (*done)(void *ctx, char *name, asuka_error *e), void *ctx);
void location_open_settings(asuka_error *err);
location_Support location_support(void);
// The regions the system monitors (where location_support says so).
void location_add_fence(const location_Region *r, asuka_error *err);
// location_remove_fence stops monitoring id ("": all of them).
void location_remove_fence(const char *id);
location_list_Region location_fences(void);
// location_watch_fences has crossed called with each crossing reported
// (at: where the device was, or NULL).
void location_watch_fences(void (*crossed)(const char *region, location_Transition kind, const location_Position *at));

// ---- location.c's, for the platform ----

// location_authorization_changed tells the followers what's allowed now.
void location_authorization_changed(void);
// location_copy_region is a deep copy of r.
location_Region location_copy_region(const location_Region *r);

#endif
