// registry_pending.c: what the native packages' registry (G1's
// platform/desktop/src/native.c) answers the host until it lands: no
// package has native code, no Native view shows. Each is weak, so the
// registry's own, once it's linked in, is the one taken; this file goes
// when it lands.
#include "host.h"

#if defined(__GNUC__) || defined(__clang__)
#define PENDING __attribute__((weak))
#else
#define PENDING
#endif

// The registry's for the hosts, as G1's asuka_desktop.h has them.
bool asuka_native_view_measure(void *widget, float width, float height, float *out_width, float *out_height);
void asuka_native_view_prop(void *widget, const uint8_t *op, size_t len);

PENDING void asuka_native_callbacks(asuka_host_callbacks *cb) { (void)cb; }

PENDING void *asuka_native_view(const char *kind, uint32_t view, void *parent) {
	(void)kind, (void)view, (void)parent;
	return NULL;
}

PENDING void asuka_native_view_prop(void *widget, const uint8_t *op, size_t len) { (void)widget, (void)op, (void)len; }

PENDING bool asuka_native_view_measure(void *widget, float width, float height, float *out_width, float *out_height) {
	(void)widget, (void)width, (void)height, (void)out_width, (void)out_height;
	return false;
}

PENDING void asuka_native_view_destroy(const char *kind, void *widget) { (void)kind, (void)widget; }
