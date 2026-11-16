// platform/scan on Windows and Linux: the image (a file an app names: a
// path, an asset, a file: or http(s) URL) decoded upright (WIC on
// Windows, GdkPixbuf on Linux), its gray levels read for codes by
// platform/desktop's reader (asuka_barcode.h: QR, Data Matrix, EAN-13,
// EAN-8, UPC-A, UPC-E, Code 128, Code 39), the same on both. A call's
// work is a thread of its own.
#ifdef _WIN32
#include "asuka_windows.h"
#else
#include "asuka_linux.h"
#endif
#include "asuka_barcode.h"
#include "scan.asuka.h"

#include <stdlib.h>
#include <string.h>

// gray is an image's gray levels (BT.601's luma) from its pixels, 4 bytes
// each (bgr: B, G, R, A; else R, G, B, A), premultiplied or not,
// transparent ones over white: w×h bytes of malloc's.
static uint8_t *gray_of(const uint8_t *px, int w, int h, bool bgr, bool premultiplied) {
	uint8_t *g = malloc((size_t)(w > 0 ? w : 1) * (h > 0 ? h : 1));
	for (size_t i = 0; i < (size_t)w * h; i++) {
		const uint8_t *p = px + 4 * i;
		unsigned r = bgr ? p[2] : p[0], gg = p[1], b = bgr ? p[0] : p[2], a = p[3];
		unsigned l = (299 * r + 587 * gg + 114 * b + 500) / 1000;
		unsigned v = premultiplied ? l + 255 - a : (l * a + 255 * (255 - a)) / 255;
		g[i] = (uint8_t)(v > 255 ? 255 : v);
	}
	return g;
}

// scan_gray is the image at path's gray levels, upright: NULL if it
// isn't an image this computer reads.
static uint8_t *scan_gray(const char *path, int *w, int *h) {
#ifdef _WIN32
	UINT uw, uh;
	uint8_t *px;
	if (!asuka_win_image_load(path, &uw, &uh, &px)) return NULL;
	*w = (int)uw, *h = (int)uh;
	uint8_t *g = gray_of(px, *w, *h, true, true);
#else
	uint8_t *px;
	if (!asuka_linux_image_load(path, w, h, &px)) return NULL;
	uint8_t *g = gray_of(px, *w, *h, false, false);
#endif
	free(px);
	return g;
}

typedef struct {
	const char *path;
	scan_list_string kinds;  // the generated code's until the call answers
	asuka_call *call;
} job;

static bool wants(const scan_list_string *k, const char *kind) {
	if (k->len == 0) return true;
	for (size_t i = 0; i < k->len; i++)
		if (strcmp(k->data[i], kind) == 0) return true;
	return false;
}

static void run(void *p) {
	job *j = p;
	asuka_error err = {0};
	char *local = asuka_desktop_local_file(j->path, NULL, &err);
	int w = 0, h = 0;
	uint8_t *gray = local ? scan_gray(local, &w, &h) : NULL;
	if (local && !gray) asuka_error_failed(&err, "scan", 0, "no image at %s", j->path);
	free(local);
	if (!gray) {
		asuka_call_fail(j->call, &err);
		free(j);
		return;
	}
	unsigned mask = j->kinds.len ? 0 : ASUKA_BARCODE_ALL;
	for (size_t i = 0; i < j->kinds.len; i++) mask |= asuka_barcode_kind(j->kinds.data[i]);
	asuka_barcode *codes = NULL;
	size_t n = mask ? asuka_barcode_decode(gray, w, h, w, mask, &codes) : 0;  // none of the kinds asked for is one it reads: none
	free(gray);
	scan_list_Code out = {calloc(n ? n : 1, sizeof(scan_Code)), 0};
	for (size_t i = 0; i < n; i++) {
		const char *kind = asuka_barcode_name(codes[i].kind);
		char *value = asuka_strdup(codes[i].text);
		// An app that asked for EAN-13s but not UPC-As gets a UPC-A as an
		// EAN-13 with a leading 0, as Apple's readers give it.
		if (strcmp(kind, "upca") == 0 && !wants(&j->kinds, "upca") && wants(&j->kinds, "ean13") && strlen(value) == 12) {
			char *v = asuka_sprintf("0%s", value);
			free(value);
			kind = "ean13", value = v;
		}
		bool dup = !wants(&j->kinds, kind);
		for (size_t k = 0; k < out.len && !dup; k++) dup = strcmp(out.data[k].kind, kind) == 0 && strcmp(out.data[k].value, value) == 0;
		if (dup) {
			free(value);
			continue;
		}
		out.data[out.len++] = (scan_Code){asuka_strdup(kind), value};
	}
	asuka_barcode_free(codes, n);
	scan_Reader_file_done(j->call, out);
	free(j);
}

static void file(scan_Reader *self, const char *path, scan_list_string kinds, asuka_call *call) {
	job *j = malloc(sizeof *j);
	*j = (job){path, kinds, call};
	if (!asuka_desktop_spawn(run, j)) {
		free(j);
		asuka_error e = {0};
		asuka_error_failed(&e, "scan", 0, "no thread to read on");
		asuka_call_fail(call, &e);
	}
}

static const scan_Reader_vtable vt = {.file = file};

scan_Reader *scan_make_Reader(asuka_error *err) {
	scan_Reader *r = malloc(sizeof *r);
	r->vt = &vt;
	return r;
}
