// asuka_barcode.h: barcodes and QR codes read from an image's gray
// levels, for packages' C where the platform has no reader (Windows; a
// Linux without ZBar): asuka/scan, asuka/vision, asuka/camera's frames.
// QR codes by quirc, Data Matrix by libdmtx (both vendored in
// platform/desktop/third_party), and EAN-13, EAN-8, UPC-A, UPC-E, Code
// 128 and Code 39 along scan lines in every direction (src/barcode1d.c),
// upright or turned. Implemented by src/barcode.c; any thread.
#ifndef ASUKA_BARCODE_H
#define ASUKA_BARCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The kinds, as asuka/scan names them, as bits.
enum {
	ASUKA_BARCODE_QR = 1 << 0,
	ASUKA_BARCODE_DATAMATRIX = 1 << 1,
	ASUKA_BARCODE_EAN13 = 1 << 2,
	ASUKA_BARCODE_EAN8 = 1 << 3,
	ASUKA_BARCODE_UPCA = 1 << 4,
	ASUKA_BARCODE_UPCE = 1 << 5,
	ASUKA_BARCODE_CODE128 = 1 << 6,
	ASUKA_BARCODE_CODE39 = 1 << 7,
	ASUKA_BARCODE_ALL = (1 << 8) - 1,
};

// A code found: its kind ("qr", "ean13"...), what it says (malloc's) and
// where, in pixels of the image, [x0, x1) × [y0, y1).
typedef struct {
	const char *kind;
	char *text;
	int x0, y0, x1, y1;
} asuka_barcode;

// asuka_barcode_kind is a kind's bit, by its name (0: none of these).
unsigned asuka_barcode_kind(const char *name);

// asuka_barcode_decode finds the codes of the kinds given (a mask of
// ASUKA_BARCODE_*) in an image of w×h gray levels, a byte a pixel, rows
// stride bytes apart: how many, in *out (malloc's: asuka_barcodes_free),
// top to bottom, then left to right; a code found twice in one place
// once.
size_t asuka_barcode_decode(const uint8_t *gray, int w, int h, int stride, unsigned kinds, asuka_barcode **out);
void asuka_barcodes_free(asuka_barcode *codes, size_t n);

// asuka_barcode_zbar reads the codes ZBar reads (Linux, where libzbar
// is installed: QR codes, EAN-13, EAN-8, UPC-A, UPC-E, Code 128, Code 39,
// Code 93, Codabar, interleaved 2 of 5 ("itf"), PDF417; ISBNs as
// EAN-13s) in w×h gray levels (rows w bytes apart): how many, in *out as
// asuka_barcode_decode's, or -1 when there's no ZBar (not installed; not
// Linux).
long asuka_barcode_zbar(const uint8_t *gray, int w, int h, asuka_barcode **out);

// asuka_barcode_gray is an image's gray levels (BT.601's luma) from its
// pixels, 4 bytes each (bgr: B, G, R, A; else R, G, B, A), premultiplied
// or not, transparent ones over white: w×h bytes of malloc's.
uint8_t *asuka_barcode_gray(const uint8_t *pixels, int w, int h, int stride, bool bgr, bool premultiplied);

#ifdef __cplusplus
}
#endif

#endif
