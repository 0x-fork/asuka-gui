// share.c: Windows's share UI (the Share flyout, Windows 10 and later)
// for text, the Share command's: the DataTransferManager for the window
// in use, through IDataTransferManagerInterop, fills a DataPackage when
// asked (DataRequested) and shows it. Where there's none (before Windows
// 10), the text is copied instead. WinRT's objects are called through
// their vtables; its functions are looked up in combase.dll.
#include "nav.h"

typedef void *hstr;  // HSTRING
typedef HRESULT(WINAPI *create_string_fn)(const wchar_t *, UINT32, hstr *);
typedef HRESULT(WINAPI *delete_string_fn)(hstr);
typedef HRESULT(WINAPI *activation_factory_fn)(hstr, REFIID, void **);

static create_string_fn create_string;
static delete_string_fn delete_string;
static activation_factory_fn activation_factory;

static bool winrt(void) {
	static bool looked;
	if (!looked) {
		looked = true;
		HMODULE m = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (m) {
			create_string = (create_string_fn)(void (*)(void))GetProcAddress(m, "WindowsCreateString");
			delete_string = (delete_string_fn)(void (*)(void))GetProcAddress(m, "WindowsDeleteString");
			activation_factory = (activation_factory_fn)(void (*)(void))GetProcAddress(m, "RoGetActivationFactory");
		}
	}
	return create_string && delete_string && activation_factory;
}

static hstr hstring(const wchar_t *s) {
	hstr h = NULL;
	if (FAILED(create_string(s, (UINT32)wcslen(s), &h))) return NULL;
	return h;
}

// factory is a runtime class's activation factory as interface iid (a
// reference to release), or NULL.
static void *factory(const wchar_t *cls, const GUID *iid) {
	if (!winrt()) return NULL;
	hstr h = hstring(cls);
	void *f = NULL;
	if (FAILED(activation_factory(h, iid, &f))) f = NULL;
	delete_string(h);
	return f;
}

// A COM object's vtable, its methods called by their places in it.
#define VTBL(o) (*(void ***)(o))

static void release(void *o) {
	if (o) ((ULONG(STDMETHODCALLTYPE *)(void *))VTBL(o)[2])(o);
}

static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_agile = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
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
		release(req);
		return;
	}
	((HRESULT(STDMETHODCALLTYPE *)(void *, void **))VTBL(pkg)[7])(pkg, &props);  // get_Properties
	if (props) {
		hstr t = hstring(sharing.title);
		((HRESULT(STDMETHODCALLTYPE *)(void *, hstr))VTBL(props)[7])(props, t);  // put_Title
		delete_string(t);
		release(props);
	}
	if (sharing.text) {
		hstr t = hstring(sharing.text);
		((HRESULT(STDMETHODCALLTYPE *)(void *, hstr))VTBL(pkg)[16])(pkg, t);  // SetText
		delete_string(t);
	}
	if (sharing.link) {
		void *f = factory(L"Windows.Foundation.Uri", &iid_uri_factory);
		if (f) {
			hstr t = hstring(sharing.link);
			void *uri = NULL;
			((HRESULT(STDMETHODCALLTYPE *)(void *, hstr, void **))VTBL(f)[6])(f, t, &uri);  // CreateUri
			delete_string(t);
			release(f);
			if (uri) {
				((HRESULT(STDMETHODCALLTYPE *)(void *, void *))VTBL(pkg)[17])(pkg, uri);  // SetUri
				release(uri);
			}
		}
	}
	release(pkg);
	release(req);
}

// The DataRequested handler: a WinRT delegate of the host's.
typedef struct delegate delegate;

typedef struct {
	HRESULT(STDMETHODCALLTYPE *query)(delegate *, REFIID, void **);
	ULONG(STDMETHODCALLTYPE *add_ref)(delegate *);
	ULONG(STDMETHODCALLTYPE *release)(delegate *);
	HRESULT(STDMETHODCALLTYPE *invoke)(delegate *, void *, void *);
} delegate_vtbl;

struct delegate {
	const delegate_vtbl *vtbl;
	LONG refs;
};

static HRESULT STDMETHODCALLTYPE d_query(delegate *d, REFIID riid, void **out) {
	if (!out) return E_POINTER;
	*out = NULL;
	if (!IsEqualIID(riid, &iid_unknown) && !IsEqualIID(riid, &iid_agile) && !IsEqualIID(riid, &iid_requested)) return E_NOINTERFACE;
	*out = d;
	InterlockedIncrement(&d->refs);
	return S_OK;
}

static ULONG STDMETHODCALLTYPE d_add_ref(delegate *d) { return (ULONG)InterlockedIncrement(&d->refs); }

static ULONG STDMETHODCALLTYPE d_release(delegate *d) {
	LONG r = InterlockedDecrement(&d->refs);
	if (r == 0) free(d);
	return (ULONG)r;
}

static HRESULT STDMETHODCALLTYPE d_invoke(delegate *d, void *sender, void *args) {
	(void)d, (void)sender;
	if (args) fill(args);
	return S_OK;
}

static const delegate_vtbl requested_vtbl = {d_query, d_add_ref, d_release, d_invoke};

// show shows the share UI over hwnd; false: there's none.
static bool show(HWND hwnd) {
	void *interop = factory(L"Windows.ApplicationModel.DataTransfer.DataTransferManager", &iid_interop);
	if (!interop) return false;
	if (sharing.manager) {
		((HRESULT(STDMETHODCALLTYPE *)(void *, int64_t))VTBL(sharing.manager)[7])(sharing.manager, sharing.token);  // remove_DataRequested
		release(sharing.manager);
		sharing.manager = NULL;
	}
	void *m = NULL;
	HRESULT hr = ((HRESULT(STDMETHODCALLTYPE *)(void *, HWND, REFIID, void **))VTBL(interop)[3])(interop, hwnd, &iid_manager, &m);  // GetForWindow
	if (FAILED(hr) || !m) {
		release(interop);
		return false;
	}
	sharing.manager = m;
	delegate *d = wh_alloc(sizeof *d);
	d->vtbl = &requested_vtbl;
	d->refs = 1;
	hr = ((HRESULT(STDMETHODCALLTYPE *)(void *, void *, int64_t *))VTBL(m)[6])(m, d, &sharing.token);  // add_DataRequested
	d_release(d);
	if (SUCCEEDED(hr)) hr = ((HRESULT(STDMETHODCALLTYPE *)(void *, HWND))VTBL(interop)[4])(interop, hwnd);  // ShowShareUIForWindow
	release(interop);
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
