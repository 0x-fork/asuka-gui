// barcode_test.c: asuka_barcode.h's readers on images of codes (testdata/
// barcode/*.pgm, which the Go host's barcode package made and read), run
// by platform/desktop's TestBarcode with the fixtures' directory.
#include "asuka_barcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *pgm(const char *dir, const char *name, int *w, int *h) {
	char path[1024];
	snprintf(path, sizeof path, "%s/%s.pgm", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	int max = 0;
	if (fscanf(f, "P5 %d %d %d", w, h, &max) != 3 || max != 255) {
		fclose(f);
		return NULL;
	}
	fgetc(f);
	uint8_t *p = malloc((size_t)*w * *h);
	size_t got = fread(p, 1, (size_t)*w * *h, f);
	fclose(f);
	if (got != (size_t)*w * *h) {
		free(p);
		return NULL;
	}
	return p;
}

static int failed;

// expect reads an image's codes of the kinds given and checks them
// against want: "kind text" each, in order.
static void expect(const char *dir, const char *name, unsigned kinds, int n, const char *const *want) {
	int w, h;
	uint8_t *p = pgm(dir, name, &w, &h);
	if (!p) {
		printf("%s: no fixture\n", name);
		failed = 1;
		return;
	}
	asuka_barcode *codes;
	size_t got = asuka_barcode_decode(p, w, h, w, kinds, &codes);
	bool ok = got == (size_t)n;
	for (size_t i = 0; ok && i < got; i++) {
		char s[512];
		snprintf(s, sizeof s, "%s %s", codes[i].kind, codes[i].text);
		ok = strcmp(s, want[i]) == 0 && codes[i].x0 >= 0 && codes[i].y0 >= 0 && codes[i].x1 <= w && codes[i].y1 <= h &&
		     codes[i].x1 - codes[i].x0 > 20 && codes[i].y1 - codes[i].y0 > 20;
	}
	if (!ok) {
		failed = 1;
		printf("%s: got %zu:", name, got);
		for (size_t i = 0; i < got; i++) printf(" [%s %s (%d,%d)-(%d,%d)]", codes[i].kind, codes[i].text, codes[i].x0, codes[i].y0, codes[i].x1, codes[i].y1);
		printf("\n");
	} else {
		printf("%s: ok\n", name);
	}
	asuka_barcodes_free(codes, got);
	free(p);
}

#define ONE(name, kinds, w) expect(dir, name, kinds, 1, (const char *const[]){w})

int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : "testdata/barcode";
	unsigned all = ASUKA_BARCODE_ALL;
	ONE("qr", all, "qr https://asuka.dev/scan?q=1");
	ONE("qr_turned", ASUKA_BARCODE_QR, "qr https://asuka.dev/scan?q=1");
	ONE("datamatrix", all, "datamatrix ASUKA DM 1234");
	ONE("datamatrix_turned", ASUKA_BARCODE_DATAMATRIX, "datamatrix ASUKA DM 1234");
	ONE("ean13", all, "ean13 5901234123457");
	ONE("ean13_turned", all, "ean13 5901234123457");
	ONE("upca", all, "upca 036000291452");
	// Asked for EAN-13s but not UPC-As, a UPC-A is an EAN-13 with a 0.
	ONE("upca", ASUKA_BARCODE_EAN13, "ean13 0036000291452");
	ONE("ean8", all, "ean8 96385074");
	ONE("upce", all, "upce 01234565");
	ONE("code128-b", all, "code128 Hello, World!");
	ONE("code128-fnc4", all, "code128 aéb");
	ONE("code39", all, "code39 CODE 39-$/+%.");
	expect(dir, "mixed", all, 2, (const char *const[]){"ean13 5901234123457", "qr https://asuka.dev/scan?q=1"});
	expect(dir, "mixed", ASUKA_BARCODE_QR, 1, (const char *const[]){"qr https://asuka.dev/scan?q=1"});
	expect(dir, "none", all, 0, NULL);
	if (asuka_barcode_kind("code39") != ASUKA_BARCODE_CODE39 || asuka_barcode_kind("pdf417") != 0) {
		printf("kinds by name\n");
		failed = 1;
	}
	return failed;
}
