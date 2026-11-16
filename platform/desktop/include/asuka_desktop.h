// asuka_desktop.h: the contract between a C desktop host (platform/windows,
// platform/linux) and the C its app links in: native packages' Windows and
// Linux code (RFC 0008, "Native packages on the desktop"), and the C an app
// calls from Asuka (extern). Both hosts implement the host's half; the
// package registry (platform/desktop/src/native.c) implements the providers'
// half, which the hosts route asuka.h's native_* callbacks to.
//
// Everything here is called on the main thread unless it says otherwise.
//
// Changes since the first version (G1, for the host workers):
//   - asuka_native_provider is the registry's contract with nativegen's
//     generated code only (no host sees it): it takes the interface with
//     each call, its answers go through asuka_writer, and async calls get
//     an asuka_call; asuka_native_views makes a view object and its widget,
//     and measures.
//   - asuka_native_view_measure: a native view's size, when its package
//     measures it (else the host measures the widget as it would its own).
//   - asuka_native_view_prop: a Native view's Prop op (ops/schema.go: its
//     bytes as sent) handed over as is, for a host that doesn't decode it.
//   - asuka_native_emit is the registry's: it encodes the Native event
//     and gives it to the core (asuka_event); a host needn't implement it.
//   - The codec (asuka_writer, asuka_reader), errors (asuka_error), started
//     calls (asuka_call), callbacks and objects: what generated code uses,
//     and a package's C may.
//   - asuka_desktop_spawn: work on a thread of its own (the host's half
//     doesn't change: the registry implements it).
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
// What's posted runs in the order it was, in one queue with the core's
// own posts (asuka.h's post callback): a package's notification posted
// before its async call answers runs before the answer.
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

// ---- The registry's: for the hosts ----

// asuka_native_callbacks fills the native_* entries of a host's callbacks
// with the registry's, which find each call's provider by its class.
void asuka_native_callbacks(asuka_host_callbacks *cb);

// asuka_native_view makes the widget of a native component of kind
// ("<path>#<Name>", the Native view's Type prop) for view in parent (the
// host's widget the view goes in: an HWND, a GtkWidget *); NULL when no
// provider shows it (the host shows nothing and logs it). The widget is
// the host's from then on: it destroys it as it destroys the view, after
// asuka_native_view_destroy.
void *asuka_native_view(const char *kind, uint32_t view, void *parent);
// asuka_native_view_set sets one of the widget's props: its name as the
// core sends it ("Selected"), its value encoded (the Prop op's bytes).
void asuka_native_view_set(const char *kind, void *widget, const char *prop, const uint8_t *value, size_t len);
// asuka_native_view_prop is asuka_native_view_set from a Native view's
// Prop op as it came (string name, u8 type 5, u32 length, the value).
void asuka_native_view_prop(void *widget, const uint8_t *op, size_t len);
// asuka_native_view_measure measures the widget with its package's code,
// width and height a size the layout knows or NaN: false when the package
// doesn't measure it (the host measures the widget as its own).
bool asuka_native_view_measure(void *widget, float width, float height, float *out_width, float *out_height);
// asuka_native_view_destroy tells the widget's package the view goes.
void asuka_native_view_destroy(const char *kind, void *widget);
// asuka_native_emit raises a native component's event (its name, its
// values encoded) on view: the registry hands it to the core. Any thread
// (from another, it's posted to the main thread).
void asuka_native_emit(uint32_t view, const char *event, const uint8_t *values, size_t len);

// asuka_desktop_spawn runs fn(ctx) on a thread of its own, which ends as
// fn returns: for work that blocks (a device, a file, a request) off the
// main thread. Any thread; false if no thread could start.
bool asuka_desktop_spawn(void (*fn)(void *ctx), void *ctx);

// ---- Values, as native packages' C has them ----
//
// What a native declaration's types are in C (nativegen's C target):
//   bool bool; int int64_t; float double; Duration int64_t nanoseconds;
//   Color uint64_t (the wire's: asuka_desktop_color resolves it);
//   string char * (UTF-8, NUL-terminated; NULL reads as ""); bytes
//   asuka_bytes; an enum of the package an int64_t of its cases'
//   constants; a struct of the package a struct; [T] a <pkg>_list_T, a
//   data pointer and a length; [string: T] a <pkg>_map_T, keys, values
//   and a length; T? a pointer to T (NULL: none), a string? a char *, an
//   object? an object's pointer; a native interface its object's pointer;
//   fn(...) a <pkg>_fn_... (a callback's id), called with its _call.
//
// Who owns what (all of it malloc's):
//   - a method's arguments are the generated code's: valid during a
//     synchronous call, and until an async one answers (copy what's kept
//     longer);
//   - what a method returns (or an async one answers with) is given to
//     the generated code, which frees it: asuka_strdup a literal;
//   - a callback's arguments stay the caller's; what a question returns
//     is the caller's to free (<pkg>_<T>_free);
//   - a prop's value is the setter's for the call only; an event's
//     values stay the caller's.

typedef struct {
	uint8_t *data;
	size_t len;
} asuka_bytes;

// asuka_strdup is s, copied with malloc (NULL: "").
char *asuka_strdup(const char *s);
char *asuka_strndup(const char *s, size_t n);
// asuka_sprintf formats into a string of malloc's.
char *asuka_sprintf(const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 1, 2)))
#endif
	;
asuka_bytes asuka_bytes_copy(const void *p, size_t n);

// ---- The codec (the wire: core/src/native/native.h) ----
//
// Little-endian; int i64, float f64, bool u8, a string or bytes a u32
// length and its bytes, a list or a map a u32 count then its items, an
// optional a u8 (0: none) then the value, a struct its fields in order,
// an object a u64 handle (0: none), a callback a u64 id.

typedef struct {
	uint8_t *data;
	size_t len, cap;
} asuka_writer;

void asuka_writer_u8(asuka_writer *w, uint8_t v);
void asuka_writer_u32(asuka_writer *w, uint32_t v);
void asuka_writer_u64(asuka_writer *w, uint64_t v);
void asuka_writer_i64(asuka_writer *w, int64_t v);
void asuka_writer_f64(asuka_writer *w, double v);
void asuka_writer_bool(asuka_writer *w, bool v);
// asuka_writer_string writes s (NULL: "").
void asuka_writer_string(asuka_writer *w, const char *s);
void asuka_writer_bytes(asuka_writer *w, const void *p, size_t n);
void asuka_writer_free(asuka_writer *w);

typedef struct {
	const uint8_t *p;
	size_t len, off;
	bool failed;  // read past the end: every read after gives 0
} asuka_reader;

uint8_t asuka_reader_u8(asuka_reader *r);
uint32_t asuka_reader_u32(asuka_reader *r);
uint64_t asuka_reader_u64(asuka_reader *r);
int64_t asuka_reader_i64(asuka_reader *r);
double asuka_reader_f64(asuka_reader *r);
bool asuka_reader_bool(asuka_reader *r);
// asuka_reader_string is a string read into malloc's, NUL-terminated.
char *asuka_reader_string(asuka_reader *r);
asuka_bytes asuka_reader_bytes(asuka_reader *r);
// asuka_reader_count is a list's or a map's count: 0, and failed, when
// more than the bytes left could hold.
uint32_t asuka_reader_count(asuka_reader *r);

// ---- Errors ----

enum {
	ASUKA_OK = 0,
	ASUKA_DECLARED = 1,     // one of the package's errors: index
	ASUKA_FAILED = 2,       // the platform's: domain, code, message
	ASUKA_UNSUPPORTED = 3,  // not on this platform: message
	ASUKA_CANCELLED = 4,    // the task that called went
};

// asuka_error is how a method fails: set it (one of the functions below)
// and return anything. Its strings are malloc's; the generated code
// frees them.
typedef struct {
	uint8_t status;
	uint32_t index;
	char *domain;
	int64_t code;
	char *message;
} asuka_error;

// asuka_error_declared fails with the package's declared error index
// (its <pkg>_<Error>_<case> constant).
void asuka_error_declared(asuka_error *e, uint32_t index);
// asuka_error_failed fails with the platform's error: its domain ("win32",
// "HRESULT", "gio"), its code, a message.
void asuka_error_failed(asuka_error *e, const char *domain, int64_t code, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 4, 5)))
#endif
	;
// asuka_error_unsupported fails as Asuka's unsupported.
void asuka_error_unsupported(asuka_error *e, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 2, 3)))
#endif
	;
void asuka_error_cancelled(asuka_error *e);
// asuka_error_free frees e's strings and makes it ASUKA_OK.
void asuka_error_free(asuka_error *e);

// ---- Started calls: an async method's ----

typedef struct asuka_call asuka_call;

// asuka_call_cancelled reports whether the task that started the call
// went. Any thread.
bool asuka_call_cancelled(asuka_call *c);
// asuka_call_on_cancel has fn(ctx) run on the main thread when the task
// goes (at once if it has), unless the call has answered by then. ctx
// must stay valid until the call answers.
void asuka_call_on_cancel(asuka_call *c, void (*fn)(void *ctx), void *ctx);
// asuka_call_fail answers the call with an error (taking e's strings).
// Once, any thread; the call is gone after.
void asuka_call_fail(asuka_call *c, asuka_error *e);
// A method's answer is its generated <pkg>_<Interface>_<method>_done(c,
// value): once, any thread.

// ---- Callbacks: fn parameters ----
//
// A notification (a fn that returns nothing) may be called from any
// thread, any number of times, as long as the object it was passed to
// lives: it's posted to the main thread. A question (a fn that returns a
// value) runs at once on the main thread, and from another thread waits
// for it. Its generated <pkg>_fn_..._call encodes and calls these.
void asuka_native_notify(uint64_t callback, asuka_writer *args);
// asuka_native_ask's answer is into *answer (the result encoded; empty if
// it failed), which the caller frees (asuka_writer_free).
void asuka_native_ask(uint64_t callback, asuka_writer *args, asuka_writer *answer);

// ---- The providers' half: native packages' C ----
//
// nativegen's C target writes each package's desktop/<pkg>.asuka.h and
// .c: its types, a vtable per native interface, the codec, and a provider
// registered under the package's path (what the core sends as the class)
// as the program starts. The package writes its <pkg>_make_<Interface>
// functions (and <pkg>_make_<View> for its views). The hosts never see a
// package's types: they hand native_* calls to the registry.

// asuka_native_provider is a package's, as its generated code makes it.
typedef struct asuka_native_provider asuka_native_provider;

// A native component's views, by name.
typedef struct {
	// make makes view's object (and *widget, its platform widget, made
	// in parent): NULL if the package has no such view here. *index is
	// the view's, which the other calls get.
	void *(*make)(const char *name, uint32_t view, void *parent, void **widget, uint32_t *index);
	// set sets prop (its name as sent) from its value.
	void (*set)(void *obj, uint32_t index, const char *prop, asuka_reader *value);
	// measure: false when the package doesn't measure the view.
	bool (*measure)(void *obj, uint32_t index, float width, float height, float *out_width, float *out_height);
	void (*destroy)(void *obj, uint32_t index);
} asuka_native_views;

struct asuka_native_provider {
	const char *path;  // the package's path, as the core names its class
	// make makes interface iface's object: NULL with err set.
	void *(*make)(uint32_t iface, asuka_error *err);
	// call runs a synchronous method: its answer into out (after the
	// registry's status byte), or err set.
	void (*call)(void *obj, uint32_t iface, uint32_t method, asuka_reader *in, asuka_writer *out, asuka_error *err);
	// start runs an async method, which answers call once.
	void (*start)(void *obj, uint32_t iface, uint32_t method, asuka_reader *in, asuka_call *call);
	// release lets an object go (the core no longer holds it).
	void (*release)(void *obj, uint32_t iface);
	const asuka_native_views *views;  // NULL: no native components
};

// asuka_native_register registers a package's provider; the generated
// code calls it as the program starts (before asuka_start: a constructor).
void asuka_native_register(const asuka_native_provider *p);

// asuka_native_handle gives obj, an object of p's interface iface, a
// handle the core holds (0 for NULL). The same object gets the same
// handle; it's released once the core let go of every one it was given.
// Any thread.
uint64_t asuka_native_handle(const asuka_native_provider *p, uint32_t iface, void *obj);
// asuka_native_object is a handle's object, if it's of p's interface
// iface (NULL for 0, another's, or one let go). Any thread.
void *asuka_native_object(const asuka_native_provider *p, uint32_t iface, uint64_t handle);

// For the generated code: a started call's arguments, freed with
// free_args(args) as it answers; and its answer (out's status and value).
void asuka_call_set_args(asuka_call *c, void *args, void (*free_args)(void *args));
void asuka_call_done(asuka_call *c, asuka_writer *out);
// asuka_call_begin is the writer of a call's answer: its status byte,
// ready for the value.
asuka_writer asuka_call_begin(void);

// ASUKA_NATIVE_CONSTRUCTOR declares a function the program runs as it
// starts (the generated code's registration).
#if defined(_MSC_VER) && !defined(__clang__)
#pragma section(".CRT$XCU", read)
#define ASUKA_NATIVE_CONSTRUCTOR(f)                                       \
	static void f(void);                                              \
	__declspec(allocate(".CRT$XCU")) void (*f##_ptr)(void) = f;       \
	__pragma(comment(linker, "/include:" #f "_ptr")) static void f(void)
#else
#define ASUKA_NATIVE_CONSTRUCTOR(f) __attribute__((constructor, used)) static void f(void)
#endif

#ifdef __cplusplus
}
#endif

#endif
