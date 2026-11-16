// native_stub.c: the providers' half of asuka_desktop.h until the
// registry (platform/desktop's native.c) is linked in: weak, so the
// registry's take their place. Without it, no native package's C answers:
// the core's calls throw "unsupported", and a Native view shows nothing.
//
// asuka_native_emit is the host's: a native view's event goes to the core
// as a Native event (its name, then its values) on the view.
#include "host.h"
#include "asuka_desktop.h"

#define WEAK __attribute__((weak))

WEAK void asuka_native_register(const asuka_native_provider *p) { (void)p; }

WEAK void asuka_native_callbacks(asuka_host_callbacks *cb) { (void)cb; }

WEAK void *asuka_native_view(const char *kind, uint32_t view, void *parent) {
	(void)view;
	(void)parent;
	lx_log("no native package shows %s on Linux", kind);
	return NULL;
}

WEAK void asuka_native_view_set(const char *kind, void *widget, const char *prop, const uint8_t *value, size_t len) {
	(void)kind;
	(void)widget;
	(void)prop;
	(void)value;
	(void)len;
}

WEAK void asuka_native_view_destroy(const char *kind, void *widget) {
	(void)kind;
	(void)widget;
}

// asuka_native_emit raises a native component's event on view: the
// Native event's payload is the event's name and its values, u32-length
// each.
void asuka_native_emit(uint32_t view, const char *event, const uint8_t *values, size_t len) {
	node *n = node_get(view);
	if (!n) return;
	ak_buf b = {0};
	ak_buf_str(&b, event, strlen(event));
	ak_buf_str(&b, (const char *)values, len);
	lx_emit(n, (ak_event){.kind = AK_EVENT_NATIVE, .s = ak_strn(b.data, b.len)});
	ak_buf_free(&b);
}
