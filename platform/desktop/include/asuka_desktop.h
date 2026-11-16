// asuka_desktop.h: the contract between a C desktop host (platform/windows,
// platform/linux) and the C its app links in: native packages' Windows and
// Linux code (RFC 0008, "Native packages on the desktop"), and the C an app
// calls from Asuka (extern). Both hosts implement the host's half; the
// package registry (platform/desktop/native.c) implements the providers'
// half, which the hosts route asuka.h's native_* callbacks to.
//
// Everything here is called on the main thread unless it says otherwise.
#ifndef ASUKA_DESKTOP_H
#define ASUKA_DESKTOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asuka.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- The host's half: what a package's C may use ----

// asuka_desktop_post runs fn(ctx) on the main thread soon. Any thread.
void asuka_desktop_post(void (*fn)(void *ctx), void *ctx);
// asuka_desktop_on_main reports whether this is the main thread. Any thread.
bool asuka_desktop_on_main(void);

// asuka_desktop_window is the platform's window a view is in (Windows: an
// HWND; Linux: a GtkWindow *), or the main window's for view 0; NULL when
// there is none (the view isn't in a window, or the app has none).
void *asuka_desktop_window(uint32_t view);

// asuka_desktop_color resolves a Color as the wire has it (ops/ops.go: 0
// the platform's default, a semantic color, RGBA) for the appearance
// shown now: 0xRRGGBBAA.
uint32_t asuka_desktop_color(uint64_t color);
// asuka_desktop_dark reports whether the app shows dark now.
bool asuka_desktop_dark(void);

// The app, as asuka.toml and the platform name it. Strings are the host's,
// valid for the process's life, UTF-8.
const char *asuka_desktop_app_name(void);
const char *asuka_desktop_app_id(void);
// Its directories (internal/appdir's rules), made if missing.
const char *asuka_desktop_data_dir(void);
const char *asuka_desktop_config_dir(void);
const char *asuka_desktop_cache_dir(void);
const char *asuka_desktop_logs_dir(void);
// Where its assets are (assets/ beside the executable, or asuka dev's).
const char *asuka_desktop_assets_dir(void);

// ---- The providers' half: native packages' C ----
//
// nativegen's C target writes each package's <pkg>.asuka.h and .c: its
// types, a vtable per native interface, the codec over the wire format
// (core/src/native/native.h), and a provider registered under the
// package's path (nativegen's Package.Path: what the core sends as the
// class). The package writes its make<Interface> functions. The hosts
// never see a package's types: they hand native_* calls to the registry.

// A native object: what a provider's make returned, which it calls and
// releases.
typedef struct asuka_native_object asuka_native_object;

// asuka_native_answer is the buffer an answer is written into (a status
// byte, then the value in the wire format): out, cap bytes; a longer
// one is allocated, *big set, and the core frees it with native_free.
typedef struct {
	uint8_t *out;
	size_t cap;
	uint8_t **big;
} asuka_native_answer;

// A view a native component shows: the provider makes the platform's
// widget (Windows: an HWND, a child of parent; Linux: a GtkWidget *),
// sets its props by name (each its encoded bytes), and raises events
// back through asuka_native_emit.
typedef struct {
	void *(*make)(const char *name, uint32_t view, void *parent);
	void (*set)(void *widget, const char *prop, const uint8_t *value, size_t len);
	void (*destroy)(void *widget);
} asuka_native_views;

typedef struct {
	const char *path;  // the package's path, as the core names its class
	// make makes interface iface's object: NULL with the error (status
	// and message) written into ans.
	asuka_native_object *(*make)(uint32_t iface, asuka_native_answer ans, size_t *len);
	// call runs method synchronously: the answer's length.
	size_t (*call)(asuka_native_object *o, uint32_t method, const uint8_t *in, size_t n, asuka_native_answer ans);
	// start runs an async method; it answers with asuka_native_done(call,
	// …) once, on any thread.
	void (*start)(asuka_native_object *o, uint32_t method, const uint8_t *in, size_t n, uint64_t call);
	void (*cancel)(uint64_t call);
	void (*release)(asuka_native_object *o);
	const asuka_native_views *views;  // NULL: no native components
} asuka_native_provider;

// asuka_native_register registers a package's provider; the generated
// code calls it as the program starts (before asuka_start).
void asuka_native_register(const asuka_native_provider *p);

// asuka_native_callbacks fills the native_* entries of a host's callbacks
// with the registry's, which find each call's provider by its class.
void asuka_native_callbacks(asuka_host_callbacks *cb);

// asuka_native_view makes the widget of a native component of kind
// ("<path>#<Name>", the Native view's Type prop) in parent; NULL when no
// provider shows it (the host shows nothing and logs it).
void *asuka_native_view(const char *kind, uint32_t view, void *parent);
void asuka_native_view_set(const char *kind, void *widget, const char *prop, const uint8_t *value, size_t len);
void asuka_native_view_destroy(const char *kind, void *widget);
// asuka_native_emit raises a native component's event (its name, its
// values encoded) on view: the host sends it to the core. Main thread.
void asuka_native_emit(uint32_t view, const char *event, const uint8_t *values, size_t len);

#ifdef __cplusplus
}
#endif

#endif
