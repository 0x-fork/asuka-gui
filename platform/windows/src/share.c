// share.c: Windows's share UI (the Share flyout, Windows 10 and later)
// for text, the Share command's: the DataTransferManager for the window
// in use, through IDataTransferManagerInterop, fills a DataPackage when
// asked (DataRequested) and shows it. Where there's none (before Windows
// 10), the text is copied instead. WinRT's objects are called through
// their vtables, with asuka_windows.h's helpers.
#include "nav.h"
#include "asuka_windows.h"

// A COM object's vtable, its methods called by their places in it.
#define VTBL(o) (*(void ***)(o))

static const GUID iid_interop = {0x3A3DCD6C, 0x3EAB, 0x43DC, {0xBC, 0xDE, 0x45, 0x67, 0x1C, 0xE8, 0x00, 0xC8}};
static const GUID iid_manager = {0xA5CAEE9B, 0x8708, 0x49D1, {0x8D, 0x36, 0x67, 0xD2, 0x5A, 0x8D, 0xA0, 0x0C}};
static const GUID iid_uri_factory = {0x44A9796F, 0x723E, 0x4FDF, {0xA2, 0x18, 0x03, 0x3E, 0x75, 0xB0, 0xC0, 0x84}};
// TypedEventHandler<DataTransferManager, DataRequestedEventArgs>: WinRT's
// IID of the parameterized interface, from its signature.
static const GUID iid_requested = {0xec6f9cc8, 0x46d0, 0x5e0e, {0xb4, 0xd2, 0x7d, 0x77, 0x73, 0xae, 0x37, 0xa0}};

// The share in progress: one at a time, per process.
static struct {
	void *manager;  // the window's DataTransferManager
	int64_t token;  // its DataRequested handler's
	wchar_t *title, *text, *link;
} sharing;

// fill fills the DataPackage the share UI asks for.
static void fill(void *args) {
	void *req = NULL, *pkg = NULL, *props = NULL;
	((HRESULT(STDMETHODCALLTYPE *)(void *, void **))VTBL(args)[6])(args, &req);  // get_Request
	if (!req) return;
	((HRESULT(STDMETHODCALLTYPE *)(void *, void **))VTBL(req)[6])(req, &pkg);  // get_Data
	if (!pkg) {
		asuka_com_release(req);
		return;
	}
	((HRESULT(STDMETHODCALLTYPE *)(void *, void **))VTBL(pkg)[7])(pkg, &props);  // get_Properties
	if (props) {
		HSTRING t = asuka_hstring_w(sharing.title);
		((HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING))VTBL(props)[7])(props, t);  // put_Title
		asuka_hstring_free(t);
		asuka_com_release(props);
	}
	if (sharing.text) {
		HSTRING t = asuka_hstring_w(sharing.text);
		((HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING))VTBL(pkg)[16])(pkg, t);  // SetText
		asuka_hstring_free(t);
	}
	if (sharing.link) {
		void *f;
		asuka_winrt_factory("Windows.Foundation.Uri", &iid_uri_factory, &f);
		if (f) {
			HSTRING t = asuka_hstring_w(sharing.link);
			void *uri = NULL;
			((HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING, void **))VTBL(f)[6])(f, t, &uri);  // CreateUri
			asuka_hstring_free(t);
			asuka_com_release(f);
			if (uri) {
				((HRESULT(STDMETHODCALLTYPE *)(void *, void *))VTBL(pkg)[17])(pkg, uri);  // SetUri
				asuka_com_release(uri);
			}
		}
	}
	asuka_com_release(pkg);
	asuka_com_release(req);
}

// The DataRequested handler's: fill the package the share UI asks for.
static void requested(void *ctx, void *sender, void *args) {
	if (args) fill(args);
}

// show shows the share UI over hwnd; false: there's none.
static bool show(HWND hwnd) {
	void *interop;
	asuka_winrt_factory("Windows.ApplicationModel.DataTransfer.DataTransferManager", &iid_interop, &interop);
	if (!interop) return false;
	if (sharing.manager) {
		((HRESULT(STDMETHODCALLTYPE *)(void *, int64_t))VTBL(sharing.manager)[7])(sharing.manager, sharing.token);  // remove_DataRequested
		asuka_com_release(sharing.manager);
		sharing.manager = NULL;
	}
	void *m = NULL;
	HRESULT hr = ((HRESULT(STDMETHODCALLTYPE *)(void *, HWND, REFIID, void **))VTBL(interop)[3])(interop, hwnd, &iid_manager, &m);  // GetForWindow
	if (FAILED(hr) || !m) {
		asuka_com_release(interop);
		return false;
	}
	sharing.manager = m;
	void *d = asuka_winrt_delegate(&iid_requested, requested, NULL, NULL);
	hr = ((HRESULT(STDMETHODCALLTYPE *)(void *, void *, int64_t *))VTBL(m)[6])(m, d, &sharing.token);  // add_DataRequested
	asuka_com_release(d);
	if (SUCCEEDED(hr)) hr = ((HRESULT(STDMETHODCALLTYPE *)(void *, HWND))VTBL(interop)[4])(interop, hwnd);  // ShowShareUIForWindow
	asuka_com_release(interop);
	return SUCCEEDED(hr);
}

static void copy_text(const wchar_t *w) {
	if (!OpenClipboard(H.window)) return;
	EmptyClipboard();
	size_t size = (wcslen(w) + 1) * sizeof *w;
	HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, size);
	if (g) {
		void *p = GlobalLock(g);
		if (p) {
			memcpy(p, w, size);
			GlobalUnlock(g);
			if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
		} else {
			GlobalFree(g);
		}
	}
	CloseClipboard();
}

// is_link reports whether text is a web link alone: http or https, a
// host, nothing around it.
static bool is_link(const wchar_t *s, size_t n) {
	size_t k;
	if (n > 8 && !_wcsnicmp(s, L"https://", 8)) k = 8;
	else if (n > 7 && !_wcsnicmp(s, L"http://", 7)) k = 7;
	else return false;
	if (s[k] == L'/' || s[k] == L'?' || s[k] == L'#' || s[k] == L':' || s[k] == L'@') return false;
	for (size_t i = 0; i < n; i++)
		if (iswspace(s[i])) return false;
	return true;
}

// wh_share_text shows Windows's share UI for text, a link if it's one;
// where there's none the text is copied.
void wh_share_text(const char *s, size_t n) {
	free(sharing.title);
	free(sharing.text);
	free(sharing.link);
	sharing.title = sharing.text = sharing.link = NULL;
	wchar_t *w = wh_wide(s, (int)n);
	const wchar_t *t = w;
	while (*t && iswspace(*t)) t++;
	size_t l = wcslen(t);
	while (l && iswspace(t[l - 1])) l--;
	if (is_link(t, l)) {
		sharing.link = wh_alloc((l + 1) * sizeof(wchar_t));
		memcpy(sharing.link, t, l * sizeof(wchar_t));
	} else {
		sharing.text = wh_alloc((wcslen(w) + 1) * sizeof(wchar_t));
		wcscpy(sharing.text, w);
	}
	sharing.title = H.app_namew && *H.app_namew ? _wcsdup(H.app_namew) : _wcsdup(L" ");
	HWND win = wh_window_in_use();
	if (!show(win)) {
		wh_log("the share UI isn't there; the text is copied instead");
		copy_text(w);
	}
	free(w);
}
