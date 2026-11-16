// asuka/vision on Windows: the image decoded upright (WIC), its text read
// by Windows's OCR (Windows.Media.Ocr: line by line, word by word, in the
// first language asked for whose OCR is installed, else the user's), its
// faces found by Windows.Media.FaceAnalysis, from its pixels as a
// SoftwareBitmap; WinRT's operations waited for on the call's thread.
#include "asuka_windows.h"
#include "vision.h"

#include <stdlib.h>
#include <string.h>

static const GUID iid_ocr_statics = {0x5BFFA85A, 0x3384, 0x3540, {0x99, 0x40, 0x69, 0x91, 0x20, 0xD4, 0x28, 0xA8}};
static const GUID iid_language_factory = {0x9B0252AC, 0x0C27, 0x44F8, {0xB7, 0x92, 0x97, 0x93, 0xFB, 0x66, 0xC6, 0x3E}};
static const GUID iid_bitmap_statics = {0xDF0385DB, 0x672F, 0x4A9D, {0x80, 0x6E, 0xC2, 0x44, 0x2F, 0x34, 0x3E, 0x86}};
static const GUID iid_face_statics = {0xBC042D67, 0x9047, 0x33F6, {0x88, 0x1B, 0x67, 0x46, 0xC1, 0xB2, 0x18, 0xB8}};
static const GUID iid_buffer_factory = {0x71AF914D, 0xC10F, 0x484B, {0xBC, 0x50, 0x14, 0xBC, 0x62, 0x3B, 0x3A, 0x27}};
static const GUID iid_buffer_bytes = {0x905A0FEF, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};

#define OCR_RESULT "rc(Windows.Media.Ocr.OcrResult;{9bd235b2-175b-3d6a-92e2-388c206e2f63})"
#define FACE_DETECTOR "rc(Windows.Media.FaceAnalysis.FaceDetector;{16b672dc-fe6f-3117-8d95-c3f04d51630c})"
#define DETECTED_FACES \
	"pinterface({913337e9-11a1-4345-a3a2-4e7f956e222d};rc(Windows.Media.FaceAnalysis.DetectedFace;{8200d454-66bc-34df-9410-e89400195414}))"

typedef HRESULT(STDMETHODCALLTYPE *get_fn)(void *, void **);

bool vision_load(const char *path, vision_image *img) {
	UINT w, h;
	if (!asuka_win_image_load(path, &w, &h, &img->px)) return false;
	img->w = (int)w, img->h = (int)h;
	img->gray = vision_gray(img->px, img->w, img->h, true, true);
	return true;
}

// await waits for an IAsyncOperation whose completed handler has the
// signature given, and gives its result (a reference), or NULL.
static void *await(void *op, const char *handler) {
	GUID h = asuka_winrt_iid(handler);
	void *r = NULL;
	if (asuka_winrt_wait(op, &h, NULL) == S_OK) ASUKA_SLOT(op, 8, get_fn)(op, &r);  // GetResults
	asuka_com_release(op);
	return r;
}

static UINT32 vector_size(void *v) {
	UINT32 n = 0;
	ASUKA_SLOT(v, 7, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32 *))(v, &n);  // get_Size
	return n;
}

static void *vector_at(void *v, UINT32 i) {
	void *x = NULL;
	ASUKA_SLOT(v, 6, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32, void **))(v, i, &x);  // GetAt
	return x;
}

// string_of calls a getter of an HSTRING: UTF-8, of malloc's.
static char *string_of(void *obj, int slot) {
	HSTRING s = NULL;
	if (FAILED(ASUKA_SLOT(obj, slot, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING *))(obj, &s))) return asuka_strdup("");
	char *u = asuka_hstring_utf8(s);
	asuka_hstring_free(s);
	return u;
}

// bitmap makes a SoftwareBitmap of pixels (format: 87 BGRA8, 62 Gray8).
static void *bitmap(const uint8_t *px, UINT32 n, int format, int w, int h) {
	void *bf = NULL, *buf = NULL, *acc = NULL, *st = NULL, *bmp = NULL;
	if (FAILED(asuka_winrt_factory("Windows.Storage.Streams.Buffer", &iid_buffer_factory, &bf))) return NULL;
	ASUKA_SLOT(bf, 6, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32, void **))(bf, n, &buf);  // Create
	asuka_com_release(bf);
	if (!buf) return NULL;
	if ((acc = asuka_com_query(buf, &iid_buffer_bytes))) {
		uint8_t *p = NULL;
		ASUKA_SLOT(acc, 3, HRESULT(STDMETHODCALLTYPE *)(void *, uint8_t **))(acc, &p);  // Buffer
		if (p) memcpy(p, px, n);
		asuka_com_release(acc);
		ASUKA_SLOT(buf, 8, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32))(buf, n);  // put_Length
		if (SUCCEEDED(asuka_winrt_factory("Windows.Graphics.Imaging.SoftwareBitmap", &iid_bitmap_statics, &st))) {
			ASUKA_SLOT(st, 9, HRESULT(STDMETHODCALLTYPE *)(void *, void *, int, INT32, INT32, void **))(st, buf, format, w, h, &bmp);  // CreateCopyFromBuffer
			asuka_com_release(st);
		}
	}
	asuka_com_release(buf);
	return bmp;
}

// engine is an OCR engine of the first language given that's installed,
// else the user's: NULL if there's none.
static void *engine(void *st, vision_list_string tags) {
	void *e = NULL, *lf = NULL;
	if (tags.len && SUCCEEDED(asuka_winrt_factory("Windows.Globalization.Language", &iid_language_factory, &lf))) {
		for (size_t i = 0; i < tags.len && !e; i++) {
			HSTRING t = asuka_hstring(tags.data[i]);
			void *lang = NULL;
			ASUKA_SLOT(lf, 6, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING, void **))(lf, t, &lang);  // CreateLanguage
			asuka_hstring_free(t);
			if (!lang) continue;
			ASUKA_SLOT(st, 9, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(st, lang, &e);  // TryCreateFromLanguage
			asuka_com_release(lang);
		}
		asuka_com_release(lf);
	}
	if (!e) ASUKA_SLOT(st, 10, get_fn)(st, &e);  // TryCreateFromUserProfileLanguages
	return e;
}

// scaled is a BGRA image no larger than max on its longer side: the
// pixels averaged over each new one's area, opaque (as OCR reads them).
static uint8_t *scaled(const vision_image *img, UINT32 max, int *sw, int *sh) {
	int k = 1;
	while (max > 0 && ((UINT32)((img->w + k - 1) / k) > max || (UINT32)((img->h + k - 1) / k) > max)) k++;
	*sw = img->w / k, *sh = img->h / k;
	uint8_t *out = malloc((size_t)*sw * *sh * 4);
	for (int y = 0; y < *sh; y++)
		for (int x = 0; x < *sw; x++) {
			unsigned s[3] = {0};
			for (int j = 0; j < k; j++)
				for (int i = 0; i < k; i++) {
					const uint8_t *p = img->px + ((size_t)(y * k + j) * img->w + x * k + i) * 4;
					for (int c = 0; c < 3; c++) s[c] += p[c] + 255u - p[3];  // over white
				}
			uint8_t *d = out + ((size_t)y * *sw + x) * 4;
			for (int c = 0; c < 3; c++) {
				unsigned v = s[c] / (unsigned)(k * k);
				d[c] = (uint8_t)(v > 255 ? 255 : v);
			}
			d[3] = 255;
		}
	return out;
}

bool vision_text(const vision_image *img, vision_list_string tags, vision_list_Line *out, asuka_error *err) {
	asuka_winrt();
	void *st = NULL;
	HRESULT hr = asuka_winrt_factory("Windows.Media.Ocr.OcrEngine", &iid_ocr_statics, &st);
	if (FAILED(hr)) {
		asuka_win_fail(err, hr, "Windows's OCR");
		return false;
	}
	void *e = engine(st, tags);
	UINT32 max = 0;
	ASUKA_SLOT(st, 6, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32 *))(st, &max);  // get_MaxImageDimension
	asuka_com_release(st);
	if (!e) {
		asuka_error_unsupported(err, "no OCR language installed");
		return false;
	}
	// The image scaled down to what the OCR takes, if it's larger (the
	// boxes are fractions: the same).
	int sw, sh;
	uint8_t *px = scaled(img, max, &sw, &sh);
	void *bmp = bitmap(px, (UINT32)sw * sh * 4, 87, sw, sh);
	free(px);
	void *op = NULL, *res = NULL;
	if (bmp) ASUKA_SLOT(e, 6, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(e, bmp, &op);  // RecognizeAsync
	if (op) res = await(op, "pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" OCR_RESULT ")");
	asuka_com_release(bmp);
	asuka_com_release(e);
	if (!res) {
		asuka_error_failed(err, "vision", 0, "Windows's OCR failed");
		return false;
	}
	void *lines = NULL;
	ASUKA_SLOT(res, 6, get_fn)(res, &lines);  // get_Lines
	UINT32 n = lines ? vector_size(lines) : 0;
	for (UINT32 i = 0; i < n; i++) {
		void *l = vector_at(lines, i);
		if (!l) continue;
		vision_Line line = {.text = string_of(l, 7)};  // get_Text
		void *words = NULL;
		ASUKA_SLOT(l, 6, get_fn)(l, &words);  // get_Words
		UINT32 nw = words ? vector_size(words) : 0;
		size_t at = 0;  // bytes into the line's text
		for (UINT32 k = 0; k < nw; k++) {
			void *wd = vector_at(words, k);
			if (!wd) continue;
			float r[4] = {0};  // Rect: X, Y, Width, Height
			ASUKA_SLOT(wd, 6, HRESULT(STDMETHODCALLTYPE *)(void *, float *))(wd, r);  // get_BoundingRect
			vision_Word w = {.text = string_of(wd, 7), .box = {r[0] / sw, r[1] / sh, r[2] / sw, r[3] / sh}};
			vision_place(line.text, w.text, &at, &w.start, &w.end);
			line.box = vision_union(line.box, w.box);
			vision_words_push(&line.words, w);
			asuka_com_release(wd);
		}
		asuka_com_release(words);
		asuka_com_release(l);
		vision_lines_push(out, line);
	}
	asuka_com_release(lines);
	asuka_com_release(res);
	return true;
}

bool vision_faces(const vision_image *img, vision_list_Box *out, asuka_error *err) {
	asuka_winrt();
	void *st = NULL;
	if (FAILED(asuka_winrt_factory("Windows.Media.FaceAnalysis.FaceDetector", &iid_face_statics, &st))) return true;  // no face detector: none
	boolean supported = 0;
	ASUKA_SLOT(st, 9, HRESULT(STDMETHODCALLTYPE *)(void *, boolean *))(st, &supported);  // get_IsSupported
	void *op = NULL, *det = NULL;
	if (supported) ASUKA_SLOT(st, 6, get_fn)(st, &op);  // CreateAsync
	asuka_com_release(st);
	if (!supported) return true;
	if (op) det = await(op, "pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" FACE_DETECTOR ")");
	if (!det) {
		asuka_error_failed(err, "vision", 0, "no face detector");
		return false;
	}
	void *bmp = bitmap(img->gray, (UINT32)img->w * img->h, 62, img->w, img->h), *faces = NULL;
	op = NULL;
	if (bmp) ASUKA_SLOT(det, 6, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(det, bmp, &op);  // DetectFacesAsync
	if (op) faces = await(op, "pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" DETECTED_FACES ")");
	asuka_com_release(bmp);
	asuka_com_release(det);
	if (!faces) {
		asuka_error_failed(err, "vision", 0, "face detection failed");
		return false;
	}
	UINT32 n = vector_size(faces);
	out->data = calloc(n ? n : 1, sizeof *out->data);
	for (UINT32 i = 0; i < n; i++) {
		void *f = vector_at(faces, i);
		if (!f) continue;
		UINT32 b[4] = {0};  // BitmapBounds: X, Y, Width, Height
		ASUKA_SLOT(f, 6, HRESULT(STDMETHODCALLTYPE *)(void *, UINT32 *))(f, b);  // get_FaceBox
		out->data[out->len++] = vision_box(b[0], b[1], (double)b[0] + b[2], (double)b[1] + b[3], img->w, img->h);
		asuka_com_release(f);
	}
	asuka_com_release(faces);
	return true;
}
