// modutil.c: what the modules share: HTTP downloads (WinHTTP), the files
// a call names (internal/localfile's rules), images through WIC, icons
// drawn from pixels and glyphs.
#include "modules.h"
#include "http.h"

#include <wincodec.h>
#include <winhttp.h>

// ---- HTTP ----

// open_session is a WinHTTP session with the system's proxy settings.
static HINTERNET open_session(const wchar_t *agent) {
	HINTERNET s = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s) s = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	return s;
}

static void set_error(char **err, const char *what, DWORD e) {
	if (!err) return;
	free(*err);
	if (e == ERROR_WINHTTP_OPERATION_CANCELLED) {
		*err = wh_strdup("canceled");
		return;
	}
	HMODULE m = GetModuleHandleW(L"winhttp.dll");
	wchar_t *buf = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, m,
		e, 0, (LPWSTR)&buf, 0, NULL);
	char *msg = buf ? wh_utf8(buf, -1, NULL) : NULL;
	if (buf) LocalFree(buf);
	size_t n = msg ? strlen(msg) : 0;
	while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r' || msg[n - 1] == '.')) msg[--n] = 0;
	*err = wh_alloc(strlen(what) + n + 48);
	if (n) sprintf(*err, "%s: %s", what, msg);
	else sprintf(*err, "%s: error %lu", what, e);
	free(msg);
}

bool wh_http_get(wh_http *h) {
	h->status = 0, h->total = 0, h->downloaded = 0;
	wchar_t *url = wh_wide(h->url, -1);
	URL_COMPONENTS u = {sizeof u};
	wchar_t host[256], path[4096];
	u.lpszHostName = host, u.dwHostNameLength = WH_LEN(host);
	u.lpszUrlPath = path, u.dwUrlPathLength = WH_LEN(path);
	wchar_t extra[4096];
	u.lpszExtraInfo = extra, u.dwExtraInfoLength = WH_LEN(extra);
	if (!WinHttpCrackUrl(url, 0, 0, &u) || (u.nScheme != INTERNET_SCHEME_HTTP && u.nScheme != INTERNET_SCHEME_HTTPS)) {
		free(url);
		if (h->err) free(*h->err), *h->err = wh_strdup("not an http or https URL");
		return false;
	}
	free(url);
	wchar_t *agent = wh_wide(h->agent ? h->agent : "Asuka", -1);
	HINTERNET s = open_session(agent);
	free(agent);
	HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : NULL;
	wchar_t *target = wh_alloc((wcslen(path) + wcslen(extra) + 1) * sizeof(wchar_t));
	wcscpy(target, path);
	wcscat(target, extra);
	HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", target, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
	free(target);
	bool ok = false;
	HANDLE f = INVALID_HANDLE_VALUE;
	if (!r) {
		set_error(h->err, "connecting", GetLastError());
		goto done;
	}
	if (h->lock) {
		AcquireSRWLockExclusive(h->lock);
		if (h->cancel && *h->cancel) {
			ReleaseSRWLockExclusive(h->lock);
			set_error(h->err, "downloading", ERROR_WINHTTP_OPERATION_CANCELLED);
			goto done;
		}
		*h->request = r;  // a cancel closes it, which ends what waits on it
		ReleaseSRWLockExclusive(h->lock);
	}
	DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
	if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) {
		set_error(h->err, "downloading", GetLastError());
		goto done;
	}
	DWORD status = 0, sl = sizeof status;
	WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sl, WINHTTP_NO_HEADER_INDEX);
	h->status = (int)status;
	wchar_t len[32];
	DWORD ll = sizeof len;
	if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &ll, WINHTTP_NO_HEADER_INDEX))
		h->total = _wcstoui64(len, NULL, 10);
	if (h->disposition) {
		wchar_t cd[1024];
		DWORD cl = sizeof cd;
		if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_DISPOSITION, WINHTTP_HEADER_NAME_BY_INDEX, cd, &cl, WINHTTP_NO_HEADER_INDEX))
			*h->disposition = wh_utf8(cd, -1, NULL);
	}
	if (h->content_type) {
		wchar_t ct[256];
		DWORD cl = sizeof ct;
		if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ct, &cl, WINHTTP_NO_HEADER_INDEX))
			*h->content_type = wh_utf8(ct, -1, NULL);
	}
	if (h->progress) h->progress(h);
	// The body is kept for 200 (and the 2xx a file wants) only.
	bool keep = h->status == 200 || (h->any_2xx && h->status / 100 == 2);
	if (!keep) {
		ok = true;
		goto done;
	}
	wchar_t *to = wh_wide(h->to, -1);
	f = CreateFileW(to, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	free(to);
	if (f == INVALID_HANDLE_VALUE) {
		set_error(h->err, "writing the download", GetLastError());
		goto done;
	}
	uint8_t *buf = wh_alloc(64 * 1024);
	for (;;) {
		DWORD got = 0;
		if (!WinHttpReadData(r, buf, 64 * 1024, &got)) {
			set_error(h->err, "downloading", GetLastError());
			break;
		}
		if (!got) {
			ok = true;
			break;
		}
		DWORD wrote = 0;
		if (!WriteFile(f, buf, got, &wrote, NULL) || wrote != got) {
			set_error(h->err, "writing the download", GetLastError());
			break;
		}
		h->downloaded += got;
		if (h->progress) h->progress(h);
		if (h->cancel && *h->cancel) {
			set_error(h->err, "downloading", ERROR_WINHTTP_OPERATION_CANCELLED);
			break;
		}
	}
	free(buf);
done:
	if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
	if (h->lock) {
		AcquireSRWLockExclusive(h->lock);
		if (*h->request == r) *h->request = NULL;
		else r = NULL;  // a cancel closed it
		ReleaseSRWLockExclusive(h->lock);
	}
	if (r) WinHttpCloseHandle(r);
	if (c) WinHttpCloseHandle(c);
	if (s) WinHttpCloseHandle(s);
	return ok;
}

// ---- The files a call names ----

static bool has_prefix_ci(const char *s, const char *p) { return _strnicmp(s, p, strlen(p)) == 0; }

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// unescape undoes a URL's %XX escapes.
static char *unescape(const char *s) {
	char *o = wh_alloc(strlen(s) + 1);
	size_t k = 0;
	for (const char *p = s; *p; p++) {
		if (*p == '%' && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) {
			o[k++] = (char)(hexv(p[1]) * 16 + hexv(p[2]));
			p += 2;
		} else o[k++] = *p;
	}
	o[k] = 0;
	return o;
}

static bool is_file(const char *path) {
	wchar_t *w = wh_wide(path, -1);
	DWORD a = GetFileAttributesW(w);
	free(w);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// download_name is what a downloaded file is called: its server's
// filename, else the URL path's last element, with an extension.
static char *download_name(const char *url, const char *disposition) {
	char *name = NULL;
	if (disposition) {
		const char *f = strstr(disposition, "filename=");
		if (f) {
			f += 9;
			bool q = *f == '"';
			if (q) f++;
			size_t n = 0;
			while (f[n] && (q ? f[n] != '"' : f[n] != ';' && f[n] != ' ')) n++;
			name = wh_strndup(f, n);
		}
	}
	if (!name || !*name) {
		free(name);
		const char *p = strstr(url, "://");
		p = p ? strchr(p + 3, '/') : NULL;
		if (p) {
			size_t n = strcspn(p, "?#");
			const char *last = p;
			for (size_t i = 0; i < n; i++)
				if (p[i] == '/') last = p + i + 1;
			char *raw = wh_strndup(last, (size_t)(p + n - last));
			name = unescape(raw);
			free(raw);
		}
	}
	char *safe = wh_safe_file_name(name ? name : "", "file");
	free(name);
	return safe;
}

char *wh_local_file(const char *source, char **err) {
	*err = NULL;
	if (has_prefix_ci(source, "http://") || has_prefix_ci(source, "https://")) {
		char *dir = wh_cache_path("asuka-files");
		// A folder of its own, so its name stays its own.
		char sub[32];
		snprintf(sub, sizeof sub, "%llu", (unsigned long long)wh_now_ns());
		char *into = dsk_join(dir, sub);
		free(dir);
		dsk_mkdirs(into);
		char *tmp = dsk_join(into, ".download");
		char *disposition = NULL;
		wh_http h = {.url = source, .to = tmp, .err = err, .disposition = &disposition, .any_2xx = true};
		bool ok = wh_http_get(&h);
		if (ok && h.status / 100 != 2) {
			*err = wh_alloc(strlen(source) + 64);
			sprintf(*err, "couldn't download %s: %d", source, h.status);
			ok = false;
		}
		char *out = NULL;
		if (ok) {
			char *name = download_name(source, disposition);
			out = dsk_join(into, name);
			free(name);
			wchar_t *a = wh_wide(tmp, -1), *b = wh_wide(out, -1);
			if (!MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING)) {
				free(out);
				out = NULL;
				*err = wh_strdup("couldn't keep the download");
			}
			free(a);
			free(b);
		}
		free(disposition);
		free(tmp);
		free(into);
		return out;
	}
	char *p;
	if (has_prefix_ci(source, "file://")) {
		const char *rest = source + 7;
		if (has_prefix_ci(rest, "localhost/")) rest += 9;
		p = unescape(rest);
		for (char *c = p; *c; c++)
			if (*c == '/') *c = '\\';
		if (p[0] == '\\' && p[1] && p[2] == ':') memmove(p, p + 1, strlen(p));  // file:///C:/...
	} else if ((source[0] && source[1] == ':') || source[0] == '\\' || source[0] == '/') {
		p = wh_strdup(source);
	} else {
		char *rel = wh_strdup(source);
		for (char *c = rel; *c; c++)
			if (*c == '/') *c = '\\';
		p = dsk_join(H.assets ? H.assets : "assets", rel);
		free(rel);
	}
	if (!is_file(p)) {
		free(p);
		*err = wh_alloc(strlen(source) + 16);
		sprintf(*err, "no file at %s", source);
		return NULL;
	}
	return p;
}

// ---- Images (WIC) ----

static IWICImagingFactory *wic(void) {
	IWICImagingFactory *f = NULL;
	CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f);
	return f;
}

// orientation is a frame's EXIF orientation (1 to 8; 1 if none).
static int orientation(IWICBitmapFrameDecode *frame) {
	IWICMetadataQueryReader *q = NULL;
	if (FAILED(IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &q)) || !q) return 1;
	const wchar_t *names[] = {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}", L"System.Photo.Orientation"};
	int o = 1;
	for (size_t i = 0; i < WH_LEN(names); i++) {
		PROPVARIANT v;
		PropVariantInit(&v);
		if (SUCCEEDED(IWICMetadataQueryReader_GetMetadataByName(q, names[i], &v))) {
			if (v.vt == VT_UI2) o = v.uiVal;
			PropVariantClear(&v);
			if (o >= 1 && o <= 8) break;
		}
	}
	IWICMetadataQueryReader_Release(q);
	return o >= 1 && o <= 8 ? o : 1;
}

static IWICBitmapFrameDecode *open_frame(IWICImagingFactory *f, const wchar_t *path, int *o) {
	IWICBitmapDecoder *d = NULL;
	if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &d)) || !d) return NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICBitmapDecoder_GetFrame(d, 0, &frame);
	IWICBitmapDecoder_Release(d);
	if (frame && o) *o = orientation(frame);
	return frame;
}

bool wh_image_file_size(const wchar_t *path, int *w, int *h) {
	IWICImagingFactory *f = wic();
	if (!f) return false;
	int o = 1;
	IWICBitmapFrameDecode *frame = open_frame(f, path, &o);
	bool ok = false;
	if (frame) {
		UINT fw = 0, fh = 0;
		IWICBitmapFrameDecode_GetSize(frame, &fw, &fh);
		if (o >= 5) *w = (int)fh, *h = (int)fw;
		else *w = (int)fw, *h = (int)fh;
		ok = true;
		IWICBitmapFrameDecode_Release(frame);
	}
	IWICImagingFactory_Release(f);
	return ok;
}

bool wh_decode_image_file(const wchar_t *path, int max, uint8_t **px, int *w, int *h) {
	*px = NULL;
	IWICImagingFactory *f = wic();
	if (!f) return false;
	int o = 1;
	IWICBitmapFrameDecode *frame = open_frame(f, path, &o);
	bool ok = false;
	if (frame) {
		IWICBitmapSource *src = (IWICBitmapSource *)frame;
		IWICBitmapSource_AddRef(src);
		UINT fw = 0, fh = 0;
		IWICBitmapSource_GetSize(src, &fw, &fh);
		if (max > 0 && (fw > (UINT)max || fh > (UINT)max)) {
			double s = (double)max / (double)(fw > fh ? fw : fh);
			UINT nw = (UINT)(fw * s + 0.5), nh = (UINT)(fh * s + 0.5);
			IWICBitmapScaler *sc = NULL;
			if (SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(f, &sc)) &&
				SUCCEEDED(IWICBitmapScaler_Initialize(sc, src, nw ? nw : 1, nh ? nh : 1, WICBitmapInterpolationModeFant))) {
				IWICBitmapSource_Release(src);
				src = (IWICBitmapSource *)sc;
			} else if (sc) IWICBitmapScaler_Release(sc);
		}
		if (o != 1) {
			static const WICBitmapTransformOptions t[9] = {0, WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal,
				WICBitmapTransformRotate180, WICBitmapTransformFlipVertical,
				WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate90,
				WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate270};
			IWICBitmapFlipRotator *r = NULL;
			if (SUCCEEDED(IWICImagingFactory_CreateBitmapFlipRotator(f, &r)) && SUCCEEDED(IWICBitmapFlipRotator_Initialize(r, src, t[o]))) {
				IWICBitmapSource_Release(src);
				src = (IWICBitmapSource *)r;
			} else if (r) IWICBitmapFlipRotator_Release(r);
		}
		IWICFormatConverter *cv = NULL;
		if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(f, &cv)) &&
			SUCCEEDED(IWICFormatConverter_Initialize(cv, src, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
			UINT cw = 0, ch = 0;
			IWICFormatConverter_GetSize(cv, &cw, &ch);
			uint8_t *buf = malloc((size_t)cw * ch * 4 + 1);
			if (buf && SUCCEEDED(IWICFormatConverter_CopyPixels(cv, NULL, cw * 4, cw * ch * 4, buf))) {
				*px = buf, *w = (int)cw, *h = (int)ch;
				ok = true;
			} else free(buf);
		}
		if (cv) IWICFormatConverter_Release(cv);
		IWICBitmapSource_Release(src);
		IWICBitmapFrameDecode_Release(frame);
	}
	IWICImagingFactory_Release(f);
	return ok;
}

bool wh_encode_image_file(const wchar_t *path, bool jpeg, const uint8_t *px, int w, int h) {
	IWICImagingFactory *f = wic();
	if (!f) return false;
	bool ok = false;
	IWICStream *st = NULL;
	IWICBitmapEncoder *enc = NULL;
	IWICBitmapFrameEncode *frame = NULL;
	IPropertyBag2 *props = NULL;
	if (FAILED(IWICImagingFactory_CreateStream(f, &st)) || FAILED(IWICStream_InitializeFromFilename(st, path, GENERIC_WRITE))) goto out;
	if (FAILED(IWICImagingFactory_CreateEncoder(f, jpeg ? &GUID_ContainerFormatJpeg : &GUID_ContainerFormatPng, NULL, &enc))) goto out;
	if (FAILED(IWICBitmapEncoder_Initialize(enc, (IStream *)st, WICBitmapEncoderNoCache))) goto out;
	if (FAILED(IWICBitmapEncoder_CreateNewFrame(enc, &frame, &props))) goto out;
	if (jpeg && props) {
		PROPBAG2 opt = {0};
		opt.pstrName = (LPOLESTR)L"ImageQuality";
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_R4, v.fltVal = 0.8f;
		IPropertyBag2_Write(props, 1, &opt, &v);
	}
	if (FAILED(IWICBitmapFrameEncode_Initialize(frame, props))) goto out;
	IWICBitmapFrameEncode_SetSize(frame, (UINT)w, (UINT)h);
	WICPixelFormatGUID fmt = jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
	IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
	if (jpeg) {
		// On white: a JPEG has no alpha.
		uint8_t *rgb = malloc((size_t)w * h * 3 + 1);
		if (!rgb) goto out;
		for (int i = 0; i < w * h; i++) {
			unsigned a = px[i * 4 + 3];
			for (int c = 0; c < 3; c++) rgb[i * 3 + c] = (uint8_t)((px[i * 4 + c] * a + 255 * (255 - a)) / 255);
		}
		ok = SUCCEEDED(IWICBitmapFrameEncode_WritePixels(frame, (UINT)h, (UINT)w * 3, (UINT)(w * h * 3), rgb));
		free(rgb);
	} else {
		ok = SUCCEEDED(IWICBitmapFrameEncode_WritePixels(frame, (UINT)h, (UINT)w * 4, (UINT)(w * h * 4), (BYTE *)px));
	}
	ok = ok && SUCCEEDED(IWICBitmapFrameEncode_Commit(frame)) && SUCCEEDED(IWICBitmapEncoder_Commit(enc));
out:
	if (props) IPropertyBag2_Release(props);
	if (frame) IWICBitmapFrameEncode_Release(frame);
	if (enc) IWICBitmapEncoder_Release(enc);
	if (st) IWICStream_Release(st);
	IWICImagingFactory_Release(f);
	if (!ok) DeleteFileW(path);
	return ok;
}

// ---- Icons ----

// dib is a top-down 32-bit DIB section of w×h, its bits in *bits.
static HBITMAP dib(int w, int h, void **bits) {
	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	return CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, bits, NULL, 0);
}

HICON wh_icon_from_pixels(const uint8_t *px, int w, int h) {
	void *bits = NULL;
	HBITMAP bmp = dib(w, h, &bits);
	if (!bmp) return NULL;
	memcpy(bits, px, (size_t)w * h * 4);
	HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
	ICONINFO ii = {TRUE, 0, 0, mask, bmp};
	HICON icon = CreateIconIndirect(&ii);
	DeleteObject(mask);
	DeleteObject(bmp);
	return icon;
}

uint8_t *wh_glyph_coverage(uint16_t glyph, const wchar_t *face, int size) {
	uint8_t *out = wh_alloc((size_t)size * size);
	HDC dc = CreateCompatibleDC(NULL);
	void *bits = NULL;
	HBITMAP bmp = dc ? dib(size, size, &bits) : NULL;
	if (!bmp) {
		if (dc) DeleteDC(dc);
		return out;
	}
	HGDIOBJ old = SelectObject(dc, bmp);
	LOGFONTW lf = {0};
	lf.lfHeight = -size, lf.lfWeight = FW_NORMAL, lf.lfQuality = ANTIALIASED_QUALITY;  // gray, not ClearType's colors
	wcsncpy(lf.lfFaceName, face, 31);
	HFONT font = CreateFontIndirectW(&lf);
	HGDIOBJ of = SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	RECT r = {0, 0, size, size};
	wchar_t g[2] = {glyph, 0};
	DrawTextW(dc, g, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	GdiFlush();
	SelectObject(dc, of);
	DeleteObject(font);
	const uint8_t *p = bits;
	for (int i = 0; i < size * size; i++) out[i] = p[i * 4 + 1];
	SelectObject(dc, old);
	DeleteObject(bmp);
	DeleteDC(dc);
	return out;
}

// wh_app_icon is the app's icon (its executable's first icon resource).
HICON wh_app_icon(void) {
	static HICON icon;
	static bool tried;
	if (!tried) {
		tried = true;
		icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
	}
	return icon;
}
