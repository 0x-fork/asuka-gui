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

// asuka_guid parses "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" (braces allowed).
GUID asuka_guid(const char *s);
// asuka_guid_string is g as "XXXXXXXX-XXXX-…", lower case, into buf (37).
void asuka_guid_string(const GUID *g, char buf[37]);

// ---- WinRT ----

typedef struct HSTRING__ *HSTRING;

// asuka_hstring makes an HSTRING of s (NULL for ""), which the caller
// frees with asuka_hstring_free.
HSTRING asuka_hstring(const char *s);
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
