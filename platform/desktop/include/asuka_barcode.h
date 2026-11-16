// asuka_barcode.h: barcodes and QR codes read in images, in C alone (a
// port of the Go host's package barcode): QR codes, Data Matrix, EAN-13,
// EAN-8, UPC-A, UPC-E, Code 128 and Code 39, upright or turned by any
// angle. Packages' Windows C reads codes with it (Windows has no reader
// a desktop app can use); Linux's has ZBar. Implemented by
// platform/desktop/src/barcode.c (every platform). Any thread.
#ifndef ASUKA_BARCODE_H
#define ASUKA_BARCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The kinds, as asuka/scan names them: "qr", "datamatrix", "ean13",
// "ean8", "upca", "upce", "code128", "code39".
enum {
	ASUKA_BARCODE_QR = 1 << 0,
	ASUKA_BARCODE_DATAMATRIX = 1 << 1,
	ASUKA_BARCODE_EAN13 = 1 << 2,
	ASUKA_BARCODE_EAN8 = 1 << 3,
	ASUKA_BARCODE_UPCA = 1 << 4,
	ASUKA_BARCODE_UPCE = 1 << 5,
	ASUKA_BARCODE_CODE128 = 1 << 6,
	ASUKA_BARCODE_CODE39 = 1 << 7,
	ASUKA_BARCODE_ALL = 0xff,
};

// asuka_barcode_kind is a kind's bit (0: not one this reads);
// asuka_barcode_name is a bit's name.
unsigned asuka_barcode_kind(const char *name);
const char *asuka_barcode_name(unsigned kind);

// A code found: its kind (one bit), what it says (UTF-8, malloc's), and
// where, in pixels: [x0, x1) × [y0, y1).
typedef struct {
	unsigned kind;
	char *text;
	int x0, y0, x1, y1;
} asuka_barcode;

// asuka_barcode_decode finds the codes of kinds (bits; 0 is none) in an
// image of gray levels (one byte a pixel, rows stride bytes apart), top
// to bottom, then left to right: their count, *out an array of malloc's
// (asuka_barcode_free).
size_t asuka_barcode_decode(const uint8_t *gray, int width, int height, int stride, unsigned kinds, asuka_barcode **out);
void asuka_barcode_free(asuka_barcode *codes, size_t n);

#ifdef __cplusplus
}
#endif

#endif
