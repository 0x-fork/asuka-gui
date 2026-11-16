// barcode_internal.h (platform/desktop/src): what barcode.c and its
// readers share.
#ifndef BARCODE_INTERNAL_H
#define BARCODE_INTERNAL_H

#include "asuka_barcode.h"

// A code a reader found: its kind (a bit), its text (malloc's) and where.
typedef struct {
	unsigned kind;
	char *text;
	int x0, y0, x1, y1;
} bc_found;

// bc_decode_1d reads the 1D codes of the kinds wanted (barcode1d.c).
size_t bc_decode_1d(const uint8_t *gray, int w, int h, int stride, unsigned want, bc_found **out);

// The vendored readers (barcode_vendor.c): QR codes by quirc, Data
// Matrix by libdmtx, appended to *found (*n of them, *cap room).
void bc_decode_qr(const uint8_t *gray, int w, int h, int stride, bc_found **found, size_t *n, size_t *cap);
void bc_decode_datamatrix(const uint8_t *gray, int w, int h, int stride, bc_found **found, size_t *n, size_t *cap);

void bc_add(bc_found **found, size_t *n, size_t *cap, bc_found f);
char *asuka_barcode_strdup(const char *s);
// bc_text is a payload as UTF-8 (malloc's): as it is when it's UTF-8,
// else Latin-1's characters, as a code's bytes default to.
char *bc_text(const uint8_t *p, size_t n);

#endif
