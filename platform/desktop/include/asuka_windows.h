// asuka_windows.h: what native packages' Windows C shares (RFC 0008),
// over Win32, COM and WinRT through their C ABI: UTF-16, COM calls by
// vtable slot, HSTRINGs, activation factories, the IIDs of parameterized
// interfaces, delegates, and waiting for an async operation. WinRT's
// interfaces have no C headers in MinGW, so a package declares the slots
// it calls, as Microsoft's documentation orders them (IInspectable's
// methods are 3 to 5: an interface's own start at 6).
// Implemented by platform/desktop/src/windows.c (Windows only).
#ifndef ASUKA_WINDOWS_H
#define ASUKA_WINDOWS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <windows.h>
#include <objbase.h>

#include "asuka_desktop.h"

#ifdef __cplusplus
extern "C" {
#endif

// asuka_wide is s as UTF-16, of malloc's (NULL: "").
wchar_t *asuka_wide(const char *s);
// asuka_narrow is w as UTF-8, of malloc's (NULL: ""); n characters, or
// up to its NUL with n < 0.
char *asuka_narrow(const wchar_t *w, int n);

// asuka_win_fail fails err with a Windows error (an HRESULT, or a Win32
// error code as HRESULT_FROM_WIN32 makes it): the system's message for it.
void asuka_win_fail(asuka_error *err, HRESULT hr, const char *what);

// ASUKA_SLOT is slot i of obj's vtable as a function of type T:
// ASUKA_SLOT(obj, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))(obj, &s).
#define ASUKA_SLOT(obj, i, T) ((T)((*(void ***)(obj))[i]))
// asuka_com_release releases obj if it isn't NULL.
void asuka_com_release(void *obj);
// asuka_com_query is obj's interface iid (a reference), or NULL.
void *asuka_com_query(void *obj, const GUID *iid);

// asuka_win_reg reads a value under HKEY_CURRENT_USER into buf (its
// size in *n, in and out): false when it isn't there or isn't of type.
bool asuka_win_reg(const wchar_t *path, const wchar_t *name, DWORD type, void *buf, DWORD *n);

// asuka_guid parses "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" (braces allowed).
GUID asuka_guid(const char *s);
// asuka_guid_string is g as "XXXXXXXX-XXXX-…", lower case, into buf (37).
void asuka_guid_string(const GUID *g, char buf[37]);

// asuka_win_system_message calls fn(ctx, msg, wp, lp) with each message
// the system window gets, making it the first time: a top-level window
// that's never shown, which hears what Windows broadcasts
// (WM_DISPLAYCHANGE, WM_SETTINGCHANGE, WM_SYSCOLORCHANGE,
// WM_POWERBROADCAST: a message-only window hears none), registered for
// the power source's, the battery's and battery saver's changes, sleep
// and waking (modern standby's too), and the session's WM_WTSSESSION_CHANGE.
// It returns the handler's id, for asuka_win_system_message_stop. Main
// thread.
int asuka_win_system_message(void (*fn)(void *ctx, UINT msg, WPARAM wp, LPARAM lp), void *ctx);
void asuka_win_system_message_stop(int id);

// asuka_win_window makes a window of native packages' own class, whose
// messages go to handle first (true: handled, *result its answer), the
// rest to DefWindowProc; handle gets WM_NCDESTROY last, and ctx is free
// then. Main thread.
HWND asuka_win_window(DWORD ex, const wchar_t *title, DWORD style, int x, int y, int w, int h, HWND parent,
                      bool (*handle)(void *ctx, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result), void *ctx);

// asuka_win_image_load decodes an image file with WIC (PNG, JPEG, GIF,
// BMP, TIFF, HEIC and WebP where their codecs are), upright (its EXIF
// orientation applied): *pixels its rows of BGRA, premultiplied, of
// malloc's. false when it isn't an image WIC reads. Any thread (COM is
// started on it).
bool asuka_win_image_load(const char *path, UINT *width, UINT *height, uint8_t **pixels);
// asuka_win_png encodes BGRA pixels (premultiplied, rows of width*4) as a
// PNG, with WIC: false if it can't.
bool asuka_win_png(UINT width, UINT height, const uint8_t *pixels, asuka_bytes *out);

// asuka_win_handler makes a COM handler: IUnknown (any interface asked
// for is it) and Invoke(a, b), which calls fn(ctx, a, b); one
// reference, the caller's. WebView2's completed and event handlers are
// such.
void *asuka_win_handler(void (*fn)(void *ctx, void *a, void *b), void *ctx, void (*free_ctx)(void *ctx));

// ---- WebView2 (src/webview2.c) ----

// asuka_win_webview2 calls done(ctx, env, hr) on the main thread with the
// process's WebView2 environment (ICoreWebView2Environment, the
// process's: don't release it) once it's made, at once if it is, or why
// there's none. One for the process, its data in
// %LOCALAPPDATA%\<app>\WebView2: the Windows host's WebViews should
// make theirs here too, so that they share its browser process. Main
// thread.
void asuka_win_webview2(void (*done)(void *ctx, void *env, HRESULT hr), void *ctx);

// asuka_win_webview2_at makes a WebView2 environment of its own, its
// data in folder (an ephemeral profile's): done gets it (a reference the
// caller releases) on the main thread. Main thread.
void asuka_win_webview2_at(const wchar_t *folder, void (*done)(void *ctx, void *env, HRESULT hr), void *ctx);

// A web page that's never shown: markup in a WebView2, in a hidden window
// of its own, a Letter page wide.
typedef struct asuka_web_page asuka_web_page;

// asuka_win_web_page loads html, then calls done on the main thread with
// the page (NULL with err set). Main thread.
void asuka_win_web_page(const char *html, void (*done)(void *ctx, asuka_web_page *page, asuka_error *err), void *ctx);
// asuka_win_web_page_evaluate runs script in the page as a script of its
// own (its last statement's value; a promise's, awaited) and calls done
// on a later turn of the main thread with the value as JSON (null for
// undefined), or what it threw in err. Main thread.
void asuka_win_web_page_evaluate(asuka_web_page *p, const char *script, void (*done)(void *ctx, char *json, asuka_error *err), void *ctx);
// asuka_win_web_page_devtools calls a DevTools protocol method with its
// parameters (a JSON object) and calls done on a later turn of the main
// thread with its result (JSON), or why it failed in err. Main thread.
void asuka_win_web_page_devtools(asuka_web_page *p, const char *method, const char *params,
                                 void (*done)(void *ctx, char *json, asuka_error *err), void *ctx);
// asuka_win_web_page_view is its ICoreWebView2.
void *asuka_win_web_page_view(asuka_web_page *p);
// asuka_win_web_page_print shows a print dialog for the page: the
// browser's print preview (0) or the system's (1); window.print() where
// WebView2 is too old for it.
void asuka_win_web_page_print(asuka_web_page *p, int kind);
// asuka_win_webview_print is the same for any WebView2's page (an
// ICoreWebView2 *: asuka_desktop_webview's, a WebView the app shows).
void asuka_win_webview_print(void *view, int kind);
// asuka_win_web_page_close lets the page go.
void asuka_win_web_page_close(asuka_web_page *p);

// ---- WinRT ----

typedef struct HSTRING__ *HSTRING;

// asuka_hstring makes an HSTRING of s (NULL for ""), which the caller
// frees with asuka_hstring_free.
HSTRING asuka_hstring(const char *s);
// asuka_hstring_w is the same of UTF-16 (NULL for "").
HSTRING asuka_hstring_w(const wchar_t *s);
// asuka_hstring_utf8 is h as UTF-8, of malloc's.
char *asuka_hstring_utf8(HSTRING h);
void asuka_hstring_free(HSTRING h);

// asuka_winrt reports whether this Windows has WinRT (8 and later), and
// makes sure the calling thread has an apartment (a worker's: the MTA).
bool asuka_winrt(void);
// asuka_winrt_factory is class's activation factory as iid (a reference).
HRESULT asuka_winrt_factory(const char *cls, const GUID *iid, void **out);
// asuka_winrt_activate makes an instance of cls with its default
// constructor: its IInspectable (a reference).
HRESULT asuka_winrt_activate(const char *cls, void **out);

// asuka_winrt_iid is the IID of a parameterized interface from its
// signature, as WinRT's type system writes it: "pinterface({9fc2…};
// rc(Windows.Devices.Geolocation.Geoposition;{…}))".
GUID asuka_winrt_iid(const char *signature);

// asuka_winrt_delegate makes a WinRT delegate of interface iid (an event's
// TypedEventHandler, an async operation's completed handler), agile,
// holding one reference: its Invoke calls fn(ctx, a, b) with its two
// arguments, on whatever thread WinRT calls it; free_ctx(ctx) runs as the
// last reference goes.
void *asuka_winrt_delegate(const GUID *iid, void (*fn)(void *ctx, void *a, void *b), void *ctx, void (*free_ctx)(void *ctx));

// asuka_winrt_wait waits for an async operation or action (its
// IAsyncInfo's status), set Completed with a delegate of handler_iid
// (its handler's parameterized IID), blocking the calling thread (never
// the main one): S_OK once completed, the operation's error, or
// E_ABORT when cancel is set and becomes true (it's cancelled then).
HRESULT asuka_winrt_wait(void *op, const GUID *handler_iid, asuka_call *cancel);

// The IIDs the helpers use.
extern const GUID asuka_IID_IAsyncInfo;

// Signatures for asuka_winrt_iid: IAsyncOperation<T>'s and its completed
// handler's (pinterface(<this>;<T's signature>)); IAsyncAction's handler
// is a plain IID.
#define ASUKA_WINRT_ASYNC_OPERATION "{9fc2b0bb-e446-44e2-aa61-9cab8f636af2}"
#define ASUKA_WINRT_ASYNC_HANDLER "{fcdcf02c-e5d8-4478-915a-4d90b74b83a5}"
extern const GUID asuka_IID_AsyncActionCompletedHandler;

#ifdef __cplusplus
}
#endif

#endif
