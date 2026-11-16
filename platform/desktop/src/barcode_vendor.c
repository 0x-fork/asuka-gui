// barcode_vendor.c (platform/desktop/src): the vendored readers
// (platform/desktop/third_party, unchanged, each with its license):
// quirc 1.2 (ISC) for QR codes and libdmtx 0.7.8 (BSD 2-clause) for Data
// Matrix, compiled here with the platform's C; and barcode.c's use of
// them. They build with the repository's flags but for the warnings
// they'd raise (sign comparisons, what's set but not used, GCC's guesses at what
// may be read before it's set, its style warnings), turned off
// for them alone.
#include "barcode_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wempty-body"
#endif

// quirc copies its old image as it resizes, which the first time is
// memcpy(image, NULL, 0): undefined (UBSan says so), if harmless. Its
// memcpy of nothing copies nothing here.
static void *bc_memcpy(void *d, const void *s, size_t n) { return n ? memcpy(d, s, n) : d; }
#define memcpy bc_memcpy
#include "../third_party/quirc/decode.c"
#include "../third_party/quirc/identify.c"
#include "../third_party/quirc/quirc.c"
#include "../third_party/quirc/version_db.c"
#undef memcpy

#define VERSION "0.7.8"
#define HAVE_SYS_TIME_H 1
#define HAVE_GETTIMEOFDAY 1
#include "../third_party/libdmtx/dmtx.c"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

// sjis_to_utf8 is a QR code's Kanji (Shift JIS) as UTF-8, or NULL where
// the system can't convert it.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static char *sjis_to_utf8(const uint8_t *p, size_t n) {
	int wn = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, (const char *)p, (int)n, NULL, 0);
	if (wn <= 0) return NULL;
	wchar_t *w = malloc(((size_t)wn + 1) * sizeof *w);
	MultiByteToWideChar(932, 0, (const char *)p, (int)n, w, wn);
	int un = WideCharToMultiByte(CP_UTF8, 0, w, wn, NULL, 0, NULL, NULL);
	char *u = malloc((size_t)un + 1);
	WideCharToMultiByte(CP_UTF8, 0, w, wn, u, un, NULL, NULL);
	u[un] = 0;
	free(w);
	return u;
}
#elif defined(__linux__)
#include <iconv.h>
static char *sjis_to_utf8(const uint8_t *p, size_t n) {
	iconv_t cd = iconv_open("UTF-8", "SHIFT_JIS");
	if (cd == (iconv_t)-1) return NULL;
	size_t cap = n * 3 + 1, left = cap - 1;
	char *u = malloc(cap), *out = u;
	char *in = (char *)p;
	size_t inleft = n;
	size_t r = iconv(cd, &in, &inleft, &out, &left);
	iconv_close(cd);
	if (r == (size_t)-1) {
		free(u);
		return NULL;
	}
	*out = 0;
	return u;
}
#else
static char *sjis_to_utf8(const uint8_t *p, size_t n) { return NULL; }
#endif

void bc_decode_qr(const uint8_t *gray, int w, int h, int stride, bc_found **found, size_t *n, size_t *cap) {
	struct quirc *q = quirc_new();
	if (!q || quirc_resize(q, w, h) < 0) {
		if (q) quirc_destroy(q);
		return;
	}
	uint8_t *buf = quirc_begin(q, NULL, NULL);
	for (int y = 0; y < h; y++) memcpy(buf + (size_t)y * w, gray + (size_t)y * stride, (size_t)w);
	quirc_end(q);
	int count = quirc_count(q);
	struct quirc_code *code = malloc(sizeof *code);
	struct quirc_data *data = malloc(sizeof *data);
	for (int i = 0; i < count; i++) {
		quirc_extract(q, i, code);
		quirc_decode_error_t err = quirc_decode(code, data);
		if (err == QUIRC_ERROR_DATA_ECC) {  // mirrored
			quirc_flip(code);
			err = quirc_decode(code, data);
		}
		if (err) continue;
		char *text = NULL;
		if (data->data_type == QUIRC_DATA_TYPE_KANJI) text = sjis_to_utf8(data->payload, (size_t)data->payload_len);
		if (!text) text = bc_text(data->payload, (size_t)data->payload_len);
		bc_found f = {ASUKA_BARCODE_QR, text, w, h, 0, 0};
		for (int c = 0; c < 4; c++) {
			int x = code->corners[c].x, y = code->corners[c].y;
			if (x < f.x0) f.x0 = x;
			if (y < f.y0) f.y0 = y;
			if (x + 1 > f.x1) f.x1 = x + 1;
			if (y + 1 > f.y1) f.y1 = y + 1;
		}
		bc_add(found, n, cap, f);
	}
	free(code);
	free(data);
	quirc_destroy(q);
}

// The side a Data Matrix search reads at most: a larger image is read
// scaled down (libdmtx's search takes time with the pixels).
#define DM_SIDE 1024
// How long it searches, at most, in milliseconds.
#define DM_BUDGET 1500

void bc_decode_datamatrix(const uint8_t *gray, int w, int h, int stride, bc_found **found, size_t *n, size_t *cap) {
	int k = 1;
	while ((w + k - 1) / k > DM_SIDE || (h + k - 1) / k > DM_SIDE) k++;
	int sw = w / k, sh = h / k;
	if (sw < 8 || sh < 8) return;
	unsigned char *px = malloc((size_t)sw * sh);
	for (int y = 0; y < sh; y++)
		for (int x = 0; x < sw; x++) {
			unsigned sum = 0;
			for (int j = 0; j < k; j++)
				for (int i = 0; i < k; i++) sum += gray[(size_t)(y * k + j) * stride + x * k + i];
			px[(size_t)y * sw + x] = (unsigned char)(sum / (unsigned)(k * k));
		}
	DmtxImage *img = dmtxImageCreate(px, sw, sh, DmtxPack8bppK);
	DmtxDecode *dec = img ? dmtxDecodeCreate(img, 1) : NULL;
	if (dec) {
		DmtxTime until = dmtxTimeAdd(dmtxTimeNow(), DM_BUDGET);
		DmtxRegion *reg;
		while ((reg = dmtxRegionFindNext(dec, &until))) {
			DmtxMessage *msg = dmtxDecodeMatrixRegion(dec, reg, DmtxUndefined);
			if (msg) {
				bc_found f = {ASUKA_BARCODE_DATAMATRIX, bc_text(msg->output, msg->outputIdx), w, h, 0, 0};
				// Its corners, from its fitted grid to the image (whose rows
				// libdmtx counts from the bottom).
				static const double cs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
				for (int c = 0; c < 4; c++) {
					DmtxVector2 p = {cs[c][0], cs[c][1]};
					dmtxMatrix3VMultiplyBy(&p, reg->fit2raw);
					int x = (int)(p.X * k), y = (int)((sh - 1 - p.Y) * k);
					if (x < f.x0) f.x0 = x < 0 ? 0 : x;
					if (y < f.y0) f.y0 = y < 0 ? 0 : y;
					if (x + 1 > f.x1) f.x1 = x + 1 > w ? w : x + 1;
					if (y + 1 > f.y1) f.y1 = y + 1 > h ? h : y + 1;
				}
				bc_add(found, n, cap, f);
				dmtxMessageDestroy(&msg);
			}
			dmtxRegionDestroy(&reg);
		}
		dmtxDecodeDestroy(&dec);
	}
	if (img) dmtxImageDestroy(&img);
	free(px);
}
