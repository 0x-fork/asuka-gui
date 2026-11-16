// asuka/imageedit on Windows: Windows's codecs (WIC) decode what the
// system reads (HEIC, WebP where their extensions are installed), upright
// as their EXIF orientation says, and write JPEG and PNG; Media
// Foundation takes a video's frame (its source reader, decoding to RGB32,
// reading a remote video as far as the frame needs).
#include "asuka_windows.h"
#include "imageedit.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propidl.h>
#include <stdlib.h>
#include <string.h>
#include <wincodec.h>

static IWICImagingFactory *wic(void) {
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	IWICImagingFactory *f = NULL;
	CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f);
	return f;
}

// swap turns BGRA into RGBA, or back.
static void swap(uint8_t *p, size_t n) {
	for (size_t i = 0; i < n; i += 4) {
		uint8_t t = p[i];
		p[i] = p[i + 2], p[i + 2] = t;
	}
}

bool ie_decode(const char *path, ie_image *img, asuka_error *err) {
	UINT w = 0, h = 0;
	uint8_t *p = NULL;
	if (!asuka_win_image_load(path, &w, &h, &p)) {
		ie_no_image(err);
		return false;
	}
	swap(p, (size_t)w * h * 4);
	*img = (ie_image){(int)w, (int)h, p};
	return true;
}

bool ie_size(const char *path, int *w, int *h, asuka_error *err) {
	IWICImagingFactory *f = wic();
	IWICBitmapDecoder *dec = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	bool ok = false;
	if (f) {
		wchar_t *wp = asuka_wide(path);
		HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(f, wp, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
		free(wp);
		if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(dec, 0, &frame);
		UINT fw = 0, fh = 0;
		if (SUCCEEDED(hr)) hr = IWICBitmapFrameDecode_GetSize(frame, &fw, &fh);
		if (SUCCEEDED(hr) && fw && fh) {
			USHORT orient = 1;
			IWICMetadataQueryReader *q = NULL;
			if (SUCCEEDED(IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &q))) {
				PROPVARIANT v;
				PropVariantInit(&v);
				if (SUCCEEDED(IWICMetadataQueryReader_GetMetadataByName(q, L"/app1/ifd/{ushort=274}", &v)) && v.vt == VT_UI2) orient = v.uiVal;
				PropVariantClear(&v);
				IWICMetadataQueryReader_Release(q);
			}
			bool quarter = orient >= 5 && orient <= 8;
			*w = (int)(quarter ? fh : fw), *h = (int)(quarter ? fw : fh);
			ok = true;
		}
	}
	if (frame) IWICBitmapFrameDecode_Release(frame);
	if (dec) IWICBitmapDecoder_Release(dec);
	if (f) IWICImagingFactory_Release(f);
	if (ok) return true;
	ie_image img;
	if (!ie_decode(path, &img, err)) return false;
	*w = img.w, *h = img.h;
	free(img.px);
	return true;
}

// jpeg encodes opaque BGRA as a JPEG of quality 0 to 1, with WIC.
static bool jpeg(UINT width, UINT height, const uint8_t *pixels, float quality, asuka_bytes *out) {
	IWICImagingFactory *f = wic();
	if (!f) return false;
	IStream *st = NULL;
	IWICBitmapEncoder *enc = NULL;
	IWICBitmapFrameEncode *frame = NULL;
	IPropertyBag2 *props = NULL;
	IWICBitmap *bmp = NULL;
	IWICFormatConverter *conv = NULL;
	HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &st);
	if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(f, &GUID_ContainerFormatJpeg, NULL, &enc);
	if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(enc, st, WICBitmapEncoderNoCache);
	if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(enc, &frame, &props);
	if (SUCCEEDED(hr) && props) {
		PROPBAG2 opt;
		memset(&opt, 0, sizeof opt);
		opt.pstrName = L"ImageQuality";
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_R4;
		v.fltVal = quality;
		IPropertyBag2_Write(props, 1, &opt, &v);
	}
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(frame, props);
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize(frame, width, height);
	WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
	if (SUCCEEDED(hr))
		hr = IWICImagingFactory_CreateBitmapFromMemory(f, width, height, &GUID_WICPixelFormat32bppBGRA, width * 4, width * height * 4, (BYTE *)pixels, &bmp);
	if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateFormatConverter(f, &conv);
	if (SUCCEEDED(hr)) hr = IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)bmp, &fmt, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource *)conv, NULL);
	if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(frame);
	if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(enc);
	bool ok = false;
	HGLOBAL g = NULL;
	if (SUCCEEDED(hr) && SUCCEEDED(GetHGlobalFromStream(st, &g))) {
		STATSTG stat;
		IStream_Stat(st, &stat, STATFLAG_NONAME);
		void *p = GlobalLock(g);
		*out = asuka_bytes_copy(p, (size_t)stat.cbSize.QuadPart);
		GlobalUnlock(g);
		ok = true;
	}
	if (conv) IWICFormatConverter_Release(conv);
	if (bmp) IWICBitmap_Release(bmp);
	if (props) IPropertyBag2_Release(props);
	if (frame) IWICBitmapFrameEncode_Release(frame);
	if (enc) IWICBitmapEncoder_Release(enc);
	if (st) IStream_Release(st);
	IWICImagingFactory_Release(f);
	return ok;
}

bool ie_encode(const ie_image *img, bool png, double quality, asuka_bytes *out, asuka_error *err) {
	size_t n = (size_t)img->w * img->h * 4;
	uint8_t *bgra = malloc(n);
	memcpy(bgra, img->px, n);
	swap(bgra, n);
	bool ok = png ? asuka_win_png((UINT)img->w, (UINT)img->h, bgra, out) : jpeg((UINT)img->w, (UINT)img->h, bgra, (float)quality, out);
	free(bgra);
	if (!ok) asuka_error_failed(err, "imageedit", 0, "WIC couldn't write the %s", png ? "PNG" : "JPEG");
	return ok;
}

// ---- Media Foundation ----

static const GUID mf_video_processing = {0xFB394F3D, 0xCCF1, 0x42EE, {0xBB, 0xB3, 0xF9, 0xB8, 0x45, 0xD5, 0x68, 0x1D}};
static const GUID mf_major_type = {0x48EBA18E, 0xF8C9, 0x4687, {0xBF, 0x11, 0x0A, 0x74, 0xC9, 0xF9, 0x6A, 0x8F}};
static const GUID mf_subtype = {0xF7E34C9A, 0x42E8, 0x4714, {0xB7, 0x4B, 0xCB, 0x29, 0xD7, 0x2C, 0x35, 0xE5}};
static const GUID mf_video = {0x73646976, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID mf_rgb32 = {0x00000016, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID mf_frame_size = {0x1652C33D, 0xD6B2, 0x4012, {0xB8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xA3, 0x7D}};
static const GUID mf_default_stride = {0x644B4E48, 0x1E02, 0x4516, {0xB0, 0xEB, 0xC0, 0x1C, 0xA9, 0xD4, 0x9A, 0xC6}};

static struct {
	bool tried, started;
	HRESULT(WINAPI *startup)(ULONG, DWORD);
	HRESULT(WINAPI *attributes)(IMFAttributes **, UINT32);
	HRESULT(WINAPI *media_type)(IMFMediaType **);
	HRESULT(WINAPI *reader)(LPCWSTR, IMFAttributes *, IMFSourceReader **);
} mf;

// mf_start loads and starts Media Foundation once (Windows N has none).
static bool mf_start(void) {
	static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
	BOOL pending;
	InitOnceBeginInitialize(&once, 0, &pending, NULL);
	if (pending) {
		HMODULE plat = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (plat && rw) {
			*(FARPROC *)&mf.startup = GetProcAddress(plat, "MFStartup");
			*(FARPROC *)&mf.attributes = GetProcAddress(plat, "MFCreateAttributes");
			*(FARPROC *)&mf.media_type = GetProcAddress(plat, "MFCreateMediaType");
			*(FARPROC *)&mf.reader = GetProcAddress(rw, "MFCreateSourceReaderFromURL");
			mf.started = mf.startup && mf.attributes && mf.media_type && mf.reader && SUCCEEDED(mf.startup(0x00020070, 0));  // MF_VERSION, MFSTARTUP_FULL
		}
		InitOnceComplete(&once, 0, NULL);
	}
	return mf.started;
}

bool ie_video_frame(const char *source, bool local, double seconds, ie_image *img, asuka_error *err) {
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (!mf_start()) {
		asuka_error_unsupported(err, "video frames need Media Foundation (Windows N: the Media Feature Pack)");
		return false;
	}
	IMFAttributes *attrs = NULL;
	IMFSourceReader *r = NULL;
	IMFMediaType *t = NULL, *cur = NULL;
	IMFSample *last = NULL;
	IMFMediaBuffer *buf = NULL;
	bool ok = false;
	if (FAILED(mf.attributes(&attrs, 1))) goto done;
	IMFAttributes_SetUINT32(attrs, &mf_video_processing, 1);  // decode to RGB32, whatever the codec gives
	wchar_t *url = asuka_wide(source);
	HRESULT hr = mf.reader(url, attrs, &r);
	free(url);
	if (FAILED(hr) || !r) goto done;
	const DWORD first_video = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, all = (DWORD)MF_SOURCE_READER_ALL_STREAMS;
	IMFSourceReader_SetStreamSelection(r, all, FALSE);
	IMFSourceReader_SetStreamSelection(r, first_video, TRUE);
	if (FAILED(mf.media_type(&t))) goto done;
	IMFMediaType_SetGUID(t, &mf_major_type, &mf_video);
	IMFMediaType_SetGUID(t, &mf_subtype, &mf_rgb32);
	if (FAILED(IMFSourceReader_SetCurrentMediaType(r, first_video, NULL, t))) goto done;
	if (FAILED(IMFSourceReader_GetCurrentMediaType(r, first_video, &cur))) goto done;
	UINT64 size = 0;
	UINT32 stride = 0;
	IMFMediaType_GetUINT64(cur, &mf_frame_size, &size);
	bool has_stride = SUCCEEDED(IMFMediaType_GetUINT32(cur, &mf_default_stride, &stride));
	int w = (int)(size >> 32), h = (int)(UINT32)size;
	if (w <= 0 || h <= 0) goto done;
	if (!has_stride) stride = (UINT32)w * 4;
	LONGLONG at = (LONGLONG)(seconds * 1e7);  // 100 ns
	if (at > 0) {
		PROPVARIANT pv;
		PropVariantInit(&pv);
		pv.vt = VT_I8;
		pv.hVal.QuadPart = at;
		IMFSourceReader_SetCurrentPosition(r, &GUID_NULL, &pv);
	}
	// Read until the frame at the time (seeking lands on the key frame
	// before it); past the end, the last one read.
	for (int i = 0; i < 600; i++) {
		DWORD stream = 0, flags = 0;
		LONGLONG ts = 0;
		IMFSample *s = NULL;
		if (FAILED(IMFSourceReader_ReadSample(r, first_video, 0, &stream, &flags, &ts, &s))) break;
		if (s) {
			if (last) IMFSample_Release(last);
			last = s;
		}
		if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) || (s && ts >= at - 10000)) break;
	}
	if (!last || FAILED(IMFSample_ConvertToContiguousBuffer(last, &buf))) goto done;
	BYTE *p = NULL;
	DWORD n = 0;
	if (FAILED(IMFMediaBuffer_Lock(buf, &p, NULL, &n))) goto done;
	int st = (int)(INT32)stride;
	bool bottom_up = st < 0;
	if (bottom_up) st = -st;
	if (st >= w * 4 && (size_t)n >= (size_t)st * h) {
		*img = (ie_image){w, h, malloc((size_t)w * h * 4)};
		for (int y = 0; y < h; y++) {
			const BYTE *row = p + (size_t)(bottom_up ? h - 1 - y : y) * st;
			uint8_t *d = img->px + (size_t)y * w * 4;
			for (int x = 0; x < w; x++) d[x * 4] = row[x * 4 + 2], d[x * 4 + 1] = row[x * 4 + 1], d[x * 4 + 2] = row[x * 4], d[x * 4 + 3] = 255;
		}
		ok = true;
	}
	IMFMediaBuffer_Unlock(buf);
done:
	if (buf) IMFMediaBuffer_Release(buf);
	if (last) IMFSample_Release(last);
	if (cur) IMFMediaType_Release(cur);
	if (t) IMFMediaType_Release(t);
	if (r) IMFSourceReader_Release(r);
	if (attrs) IMFAttributes_Release(attrs);
	if (!ok) ie_no_image(err);
	return ok;
}
