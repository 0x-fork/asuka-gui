// windows.c (platform/desktop/src): asuka_windows.h, what native
// packages' Windows C shares. Built on Windows only.
#ifdef _WIN32
#include "asuka_windows.h"
#include <wincodec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Strings ----

wchar_t *asuka_wide(const char *s) {
	if (!s) s = "";
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t *w = malloc((size_t)(n > 0 ? n : 1) * sizeof(wchar_t));
	if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0;
	return w;
}

char *asuka_narrow(const wchar_t *w, int n) {
	if (!w) return asuka_strdup("");
	int m = WideCharToMultiByte(CP_UTF8, 0, w, n, NULL, 0, NULL, NULL);
	char *s = malloc((size_t)(m > 0 ? m : 0) + 1);
	if (m > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, s, m, NULL, NULL);
	s[m > 0 ? m : 0] = 0;
	if (n < 0 && m > 0) s[m - 1] = 0;  // the NUL it counted
	return s;
}

void asuka_win_fail(asuka_error *err, HRESULT hr, const char *what) {
	wchar_t *msg = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)hr, 0, (LPWSTR)&msg, 0, NULL);
	char *m = msg ? asuka_narrow(msg, -1) : asuka_sprintf("error %#lx", (unsigned long)hr);
	if (msg) LocalFree(msg);
	size_t n = strlen(m);
	while (n > 0 && (m[n - 1] == '\n' || m[n - 1] == '\r' || m[n - 1] == ' ' || m[n - 1] == '.')) m[--n] = 0;
	if (what) asuka_error_failed(err, "HRESULT", (int64_t)(uint32_t)hr, "%s: %s", what, m);
	else asuka_error_failed(err, "HRESULT", (int64_t)(uint32_t)hr, "%s", m);
	free(m);
}

// ---- COM ----

void asuka_com_release(void *obj) {
	if (obj) ASUKA_SLOT(obj, 2, ULONG(STDMETHODCALLTYPE *)(void *))(obj);
}

void *asuka_com_query(void *obj, const GUID *iid) {
	void *out = NULL;
	if (!obj) return NULL;
	HRESULT hr = ASUKA_SLOT(obj, 0, HRESULT(STDMETHODCALLTYPE *)(void *, const GUID *, void **))(obj, iid, &out);
	return SUCCEEDED(hr) ? out : NULL;
}

bool asuka_win_reg(const wchar_t *path, const wchar_t *name, DWORD type, void *buf, DWORD *n) {
	DWORD t = 0;
	LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_ANY, &t, buf, n);
	return r == ERROR_SUCCESS && t == type;
}

static int hexv(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

GUID asuka_guid(const char *s) {
	uint8_t b[16] = {0};
	int n = 0;
	for (const char *p = s; *p && n < 32; p++) {
		int v = hexv(*p);
		if (v < 0) continue;
		b[n / 2] = (uint8_t)(b[n / 2] << 4 | v);
		n++;
	}
	GUID g;
	g.Data1 = (DWORD)b[0] << 24 | (DWORD)b[1] << 16 | (DWORD)b[2] << 8 | b[3];
	g.Data2 = (WORD)(b[4] << 8 | b[5]);
	g.Data3 = (WORD)(b[6] << 8 | b[7]);
	memcpy(g.Data4, b + 8, 8);
	return g;
}

void asuka_guid_string(const GUID *g, char buf[37]) {
	snprintf(buf, 37, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0],
	         g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

// ---- The system window, and packages' windows ----

typedef struct {
	int id;
	void (*fn)(void *, UINT, WPARAM, LPARAM);
	void *ctx;
} sys_handler;

static HWND sys_hwnd;
static sys_handler *sys_handlers;
static int nsys, capsys, sys_next;

static LRESULT CALLBACK sys_proc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp) {
	for (int i = 0; i < nsys; i++) {
		sys_handler h = sys_handlers[i];
		h.fn(h.ctx, m, wp, lp);
	}
	return DefWindowProcW(hwnd, m, wp, lp);
}

int asuka_win_system_message(void (*fn)(void *ctx, UINT msg, WPARAM wp, LPARAM lp), void *ctx) {
	if (!sys_hwnd) {
		WNDCLASSEXW wc;
		memset(&wc, 0, sizeof wc);
		wc.cbSize = sizeof wc;
		wc.lpfnWndProc = sys_proc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.lpszClassName = L"AsukaSystemWindow";
		RegisterClassExW(&wc);
		sys_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"AsukaSystemWindow", L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
		// GUID_ACDC_POWER_SOURCE, GUID_BATTERY_PERCENTAGE_REMAINING,
		// GUID_POWER_SAVING_STATUS (battery saver) and
		// GUID_ENERGY_SAVER_STATUS (Windows 11's energy saver).
		static const char *settings[] = {"5D3E9A59-E9D5-4B00-A6BD-FF34FF516548", "A7AD8041-B45A-4CAE-87A3-EECBB468A9E1",
		                                 "E00958C0-C213-4ACE-AC77-FECCED2EEEA5", "550E8400-E29B-41D4-A716-446655440000"};
		HMODULE user = GetModuleHandleW(L"user32.dll");
		HPOWERNOTIFY(WINAPI * reg_setting)(HANDLE, LPCGUID, DWORD) = NULL;
		HPOWERNOTIFY(WINAPI * reg_suspend)(HANDLE, DWORD) = NULL;
		if (user) {
			*(FARPROC *)&reg_setting = GetProcAddress(user, "RegisterPowerSettingNotification");
			*(FARPROC *)&reg_suspend = GetProcAddress(user, "RegisterSuspendResumeNotification");
		}
		for (size_t i = 0; reg_setting && i < sizeof settings / sizeof *settings; i++) {
			GUID g = asuka_guid(settings[i]);
			reg_setting(sys_hwnd, &g, 0);  // DEVICE_NOTIFY_WINDOW_HANDLE
		}
		if (reg_suspend) reg_suspend(sys_hwnd, 0);
		HMODULE wts = LoadLibraryExW(L"wtsapi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		BOOL(WINAPI * reg_session)(HWND, DWORD) = NULL;
		if (wts) *(FARPROC *)&reg_session = GetProcAddress(wts, "WTSRegisterSessionNotification");
		if (reg_session) reg_session(sys_hwnd, 0);  // NOTIFY_FOR_THIS_SESSION
	}
	if (nsys == capsys) {
		capsys = capsys ? capsys * 2 : 8;
		sys_handlers = realloc(sys_handlers, (size_t)capsys * sizeof *sys_handlers);
	}
	sys_handlers[nsys++] = (sys_handler){++sys_next, fn, ctx};
	return sys_next;
}

void asuka_win_system_message_stop(int id) {
	for (int i = 0; i < nsys; i++) {
		if (sys_handlers[i].id == id) {
			sys_handlers[i] = sys_handlers[--nsys];
			return;
		}
	}
}

typedef struct {
	bool (*handle)(void *, HWND, UINT, WPARAM, LPARAM, LRESULT *);
	void *ctx;
} own_window;

static LRESULT CALLBACK own_proc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp) {
	own_window *o = (own_window *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (m == WM_NCCREATE) {
		CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
		o = cs->lpCreateParams;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)o);
	}
	LRESULT r = 0;
	if (o && o->handle && o->handle(o->ctx, hwnd, m, wp, lp, &r)) {
		if (m == WM_NCDESTROY) free(o);
		return r;
	}
	if (m == WM_NCDESTROY) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		free(o);
	}
	return DefWindowProcW(hwnd, m, wp, lp);
}

HWND asuka_win_window(DWORD ex, const wchar_t *title, DWORD style, int x, int y, int w, int h, HWND parent,
                      bool (*handle)(void *ctx, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result), void *ctx) {
	static bool registered;
	HINSTANCE inst = GetModuleHandleW(NULL);
	if (!registered) {
		WNDCLASSEXW wc;
		memset(&wc, 0, sizeof wc);
		wc.cbSize = sizeof wc;
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = own_proc;
		wc.hInstance = inst;
		wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
		wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
		wc.lpszClassName = L"AsukaPackageWindow";
		RegisterClassExW(&wc);
		registered = true;
	}
	own_window *o = malloc(sizeof *o);
	o->handle = handle;
	o->ctx = ctx;
	HWND hwnd = CreateWindowExW(ex, L"AsukaPackageWindow", title ? title : L"", style, x, y, w, h, parent, NULL, inst, o);
	if (!hwnd) free(o);
	return hwnd;
}

// ---- Images (WIC) ----

static IWICImagingFactory *wic(void) {
	CoInitializeEx(NULL, asuka_desktop_on_main() ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
	IWICImagingFactory *f = NULL;
	CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f);
	return f;
}

bool asuka_win_image_load(const char *path, UINT *width, UINT *height, uint8_t **pixels) {
	IWICImagingFactory *f = wic();
	if (!f) return false;
	wchar_t *w = asuka_wide(path);
	IWICBitmapDecoder *dec = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICFormatConverter *conv = NULL;
	IWICBitmapFlipRotator *rot = NULL;
	bool ok = false;
	HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(f, w, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
	free(w);
	if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(dec, 0, &frame);
	IWICBitmapSource *src = (IWICBitmapSource *)frame;
	if (SUCCEEDED(hr)) {
		// The EXIF orientation (1 upright), applied.
		IWICMetadataQueryReader *q = NULL;
		USHORT orient = 1;
		if (SUCCEEDED(IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &q))) {
			PROPVARIANT v;
			PropVariantInit(&v);
			if (SUCCEEDED(IWICMetadataQueryReader_GetMetadataByName(q, L"/app1/ifd/{ushort=274}", &v)) && v.vt == VT_UI2) orient = v.uiVal;
			PropVariantClear(&v);
			IWICMetadataQueryReader_Release(q);
		}
		static const WICBitmapTransformOptions transforms[9] = {
		    WICBitmapTransformRotate0, WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate180,
		    WICBitmapTransformFlipVertical, (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
		    WICBitmapTransformRotate90, (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal), WICBitmapTransformRotate270};
		if (orient > 1 && orient <= 8 && SUCCEEDED(IWICImagingFactory_CreateBitmapFlipRotator(f, &rot)) &&
		    SUCCEEDED(IWICBitmapFlipRotator_Initialize(rot, src, transforms[orient])))
			src = (IWICBitmapSource *)rot;
		hr = IWICImagingFactory_CreateFormatConverter(f, &conv);
	}
	if (SUCCEEDED(hr)) hr = IWICFormatConverter_Initialize(conv, src, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
	if (SUCCEEDED(hr)) hr = IWICFormatConverter_GetSize(conv, width, height);
	if (SUCCEEDED(hr) && *width && *height) {
		size_t n = (size_t)*width * *height * 4;
		*pixels = malloc(n);
		hr = IWICFormatConverter_CopyPixels(conv, NULL, *width * 4, (UINT)n, *pixels);
		ok = SUCCEEDED(hr);
		if (!ok) free(*pixels), *pixels = NULL;
	}
	if (conv) IWICFormatConverter_Release(conv);
	if (rot) IWICBitmapFlipRotator_Release(rot);
	if (frame) IWICBitmapFrameDecode_Release(frame);
	if (dec) IWICBitmapDecoder_Release(dec);
	IWICImagingFactory_Release(f);
	return ok;
}

bool asuka_win_png(UINT width, UINT height, const uint8_t *pixels, asuka_bytes *out) {
	IWICImagingFactory *f = wic();
	if (!f) return false;
	IStream *st = NULL;
	IWICBitmapEncoder *enc = NULL;
	IWICBitmapFrameEncode *frame = NULL;
	HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &st);
	if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(f, &GUID_ContainerFormatPng, NULL, &enc);
	if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(enc, st, WICBitmapEncoderNoCache);
	if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(enc, &frame, NULL);
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(frame, NULL);
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize(frame, width, height);
	WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
	// The encoder may want another format (straight alpha): convert.
	IWICBitmap *bmp = NULL;
	IWICFormatConverter *conv = NULL;
	if (SUCCEEDED(hr) && !IsEqualGUID(&fmt, &GUID_WICPixelFormat32bppPBGRA)) {
		hr = IWICImagingFactory_CreateBitmapFromMemory(f, width, height, &GUID_WICPixelFormat32bppPBGRA, width * 4, width * height * 4, (BYTE *)pixels, &bmp);
		if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateFormatConverter(f, &conv);
		if (SUCCEEDED(hr)) hr = IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)bmp, &fmt, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
		if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource *)conv, NULL);
	} else if (SUCCEEDED(hr)) {
		hr = IWICBitmapFrameEncode_WritePixels(frame, height, width * 4, width * height * 4, (BYTE *)pixels);
	}
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(frame);
	if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(enc);
	bool ok = false;
	if (SUCCEEDED(hr)) {
		HGLOBAL g = NULL;
		if (SUCCEEDED(GetHGlobalFromStream(st, &g))) {
			STATSTG stat;
			IStream_Stat(st, &stat, STATFLAG_NONAME);
			void *p = GlobalLock(g);
			*out = asuka_bytes_copy(p, (size_t)stat.cbSize.QuadPart);
			GlobalUnlock(g);
			ok = true;
		}
	}
	if (conv) IWICFormatConverter_Release(conv);
	if (bmp) IWICBitmap_Release(bmp);
	if (frame) IWICBitmapFrameEncode_Release(frame);
	if (enc) IWICBitmapEncoder_Release(enc);
	if (st) IStream_Release(st);
	IWICImagingFactory_Release(f);
	return ok;
}

// ---- WinRT ----

static HRESULT(WINAPI *pWindowsCreateString)(const wchar_t *, UINT32, HSTRING *);
static HRESULT(WINAPI *pWindowsDeleteString)(HSTRING);
static const wchar_t *(WINAPI *pWindowsGetStringRawBuffer)(HSTRING, UINT32 *);
static HRESULT(WINAPI *pRoGetActivationFactory)(HSTRING, const GUID *, void **);
static HRESULT(WINAPI *pRoActivateInstance)(HSTRING, void **);
static INIT_ONCE combase_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load_combase(PINIT_ONCE o, PVOID p, PVOID *c) {
	HMODULE m = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!m) return TRUE;
	*(FARPROC *)&pWindowsCreateString = GetProcAddress(m, "WindowsCreateString");
	*(FARPROC *)&pWindowsDeleteString = GetProcAddress(m, "WindowsDeleteString");
	*(FARPROC *)&pWindowsGetStringRawBuffer = GetProcAddress(m, "WindowsGetStringRawBuffer");
	*(FARPROC *)&pRoGetActivationFactory = GetProcAddress(m, "RoGetActivationFactory");
	*(FARPROC *)&pRoActivateInstance = GetProcAddress(m, "RoActivateInstance");
	return TRUE;
}

bool asuka_winrt(void) {
	InitOnceExecuteOnce(&combase_once, load_combase, NULL, NULL);
	if (!pRoGetActivationFactory || !pWindowsCreateString) return false;
	// A worker's first WinRT call: the multithreaded apartment (S_FALSE or
	// RPC_E_CHANGED_MODE when the thread has one already, which is fine).
	if (!asuka_desktop_on_main()) CoInitializeEx(NULL, COINIT_MULTITHREADED);
	return true;
}

HSTRING asuka_hstring(const char *s) {
	if (!asuka_winrt() || !s || !*s) return NULL;
	wchar_t *w = asuka_wide(s);
	HSTRING h = NULL;
	pWindowsCreateString(w, (UINT32)wcslen(w), &h);
	free(w);
	return h;
}

char *asuka_hstring_utf8(HSTRING h) {
	if (!h || !asuka_winrt()) return asuka_strdup("");
	UINT32 n = 0;
	const wchar_t *w = pWindowsGetStringRawBuffer(h, &n);
	return w && n ? asuka_narrow(w, (int)n) : asuka_strdup("");
}

void asuka_hstring_free(HSTRING h) {
	if (h && asuka_winrt()) pWindowsDeleteString(h);
}

HRESULT asuka_winrt_factory(const char *cls, const GUID *iid, void **out) {
	*out = NULL;
	if (!asuka_winrt()) return E_NOTIMPL;
	HSTRING h = asuka_hstring(cls);
	HRESULT hr = pRoGetActivationFactory(h, iid, out);
	asuka_hstring_free(h);
	if (SUCCEEDED(hr) && !*out) hr = E_NOINTERFACE;
	return hr;
}

HRESULT asuka_winrt_activate(const char *cls, void **out) {
	*out = NULL;
	if (!asuka_winrt()) return E_NOTIMPL;
	HSTRING h = asuka_hstring(cls);
	HRESULT hr = pRoActivateInstance(h, out);
	asuka_hstring_free(h);
	if (SUCCEEDED(hr) && !*out) hr = E_NOINTERFACE;
	return hr;
}

// ---- Parameterized IIDs: a version 5 UUID (SHA-1) of the signature ----

typedef struct {
	uint32_t h[5];
	uint64_t len;
	uint8_t buf[64];
	size_t n;
} sha1;

static uint32_t rol(uint32_t x, int k) { return x << k | x >> (32 - k); }

static void sha1_block(sha1 *s, const uint8_t *p) {
	uint32_t w[80];
	for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
	for (int i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
		else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
		else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
		else f = b ^ c ^ d, k = 0xCA62C1D6;
		uint32_t t = rol(a, 5) + f + e + k + w[i];
		e = d, d = c, c = rol(b, 30), b = a, a = t;
	}
	s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

static void sha1_put(sha1 *s, const void *data, size_t n) {
	const uint8_t *p = data;
	s->len += n;
	while (n--) {
		s->buf[s->n++] = *p++;
		if (s->n == 64) sha1_block(s, s->buf), s->n = 0;
	}
}

GUID asuka_winrt_iid(const char *signature) {
	static const uint8_t ns[16] = {0x11, 0xf4, 0x7a, 0xd5, 0x7b, 0x73, 0x42, 0xc0, 0xab, 0xae, 0x87, 0x8b, 0x1e, 0x16, 0xad, 0xee};
	sha1 s = {{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, 0, {0}, 0};
	sha1_put(&s, ns, 16);
	sha1_put(&s, signature, strlen(signature));
	uint64_t bits = s.len * 8;
	uint8_t pad = 0x80;
	sha1_put(&s, &pad, 1);
	pad = 0;
	while (s.n != 56) sha1_put(&s, &pad, 1);
	uint8_t l[8];
	for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
	sha1_put(&s, l, 8);
	uint8_t b[20];
	for (int i = 0; i < 5; i++) b[4 * i] = (uint8_t)(s.h[i] >> 24), b[4 * i + 1] = (uint8_t)(s.h[i] >> 16), b[4 * i + 2] = (uint8_t)(s.h[i] >> 8), b[4 * i + 3] = (uint8_t)s.h[i];
	b[6] = (uint8_t)((b[6] & 0x0f) | 0x50);
	b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);
	GUID g;
	g.Data1 = (DWORD)b[0] << 24 | (DWORD)b[1] << 16 | (DWORD)b[2] << 8 | b[3];
	g.Data2 = (WORD)(b[4] << 8 | b[5]);
	g.Data3 = (WORD)(b[6] << 8 | b[7]);
	memcpy(g.Data4, b + 8, 8);
	return g;
}

// ---- Delegates ----

static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
const GUID asuka_IID_IAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID asuka_IID_AsyncActionCompletedHandler = {0xa4ed5c81, 0x76c9, 0x40bd, {0x8b, 0xe6, 0xb1, 0xd9, 0x0f, 0xb2, 0x0a, 0xe7}};

typedef struct {
	const void *const *vtbl;
	LONG refs;
	GUID iid;
	void (*fn)(void *, void *, void *);
	void *ctx;
	void (*free_ctx)(void *);
} delegate;

static HRESULT STDMETHODCALLTYPE d_query(delegate *d, const GUID *riid, void **out) {
	if (IsEqualGUID(riid, &d->iid) || IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_agile)) {
		InterlockedIncrement(&d->refs);
		*out = d;
		return S_OK;
	}
	*out = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d_addref(delegate *d) { return (ULONG)InterlockedIncrement(&d->refs); }

static ULONG STDMETHODCALLTYPE d_release(delegate *d) {
	LONG n = InterlockedDecrement(&d->refs);
	if (n == 0) {
		if (d->free_ctx) d->free_ctx(d->ctx);
		free(d);
	}
	return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE d_invoke(delegate *d, void *a, void *b) {
	d->fn(d->ctx, a, b);
	return S_OK;
}

static const void *const delegate_vtbl[] = {(void *)d_query, (void *)d_addref, (void *)d_release, (void *)d_invoke};

void *asuka_winrt_delegate(const GUID *iid, void (*fn)(void *ctx, void *a, void *b), void *ctx, void (*free_ctx)(void *ctx)) {
	delegate *d = calloc(1, sizeof *d);
	d->vtbl = delegate_vtbl;
	d->refs = 1;
	d->iid = *iid;
	d->fn = fn;
	d->ctx = ctx;
	d->free_ctx = free_ctx;
	return d;
}

// ---- Waiting ----

static void signal_done(void *ev, void *a, void *b) { SetEvent((HANDLE)ev); }

HRESULT asuka_winrt_wait(void *op, const GUID *handler_iid, asuka_call *cancel) {
	HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!ev) return HRESULT_FROM_WIN32(GetLastError());
	void *h = asuka_winrt_delegate(handler_iid, signal_done, ev, NULL);
	HRESULT hr = ASUKA_SLOT(op, 6, HRESULT(STDMETHODCALLTYPE *)(void *, void *))(op, h);  // put_Completed
	asuka_com_release(h);
	void *info = asuka_com_query(op, &asuka_IID_IAsyncInfo);
	if (FAILED(hr) || !info) {
		CloseHandle(ev);
		asuka_com_release(info);
		return FAILED(hr) ? hr : E_NOINTERFACE;
	}
	bool cancelled = false;
	// Woken when it completes; a cancel is seen within a tenth of a second.
	while (WaitForSingleObject(ev, 100) == WAIT_TIMEOUT) {
		if (cancel && asuka_call_cancelled(cancel) && !cancelled) {
			cancelled = true;
			ASUKA_SLOT(info, 9, HRESULT(STDMETHODCALLTYPE *)(void *))(info);  // Cancel
		}
	}
	int status = 0;
	ASUKA_SLOT(info, 7, HRESULT(STDMETHODCALLTYPE *)(void *, int *))(info, &status);  // get_Status
	if (status == 3) {                                                                  // Error
		HRESULT code = E_FAIL;
		ASUKA_SLOT(info, 8, HRESULT(STDMETHODCALLTYPE *)(void *, HRESULT *))(info, &code);  // get_ErrorCode
		hr = code;
	} else if (status == 2 || cancelled) {  // Canceled
		hr = E_ABORT;
	} else {
		hr = S_OK;
	}
	asuka_com_release(info);
	CloseHandle(ev);  // signaled: Completed is called once
	return hr;
}
#endif
