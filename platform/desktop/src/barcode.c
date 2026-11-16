// barcode.c (platform/desktop/src): asuka_barcode.h. Each reader wanted
// reads the gray levels (barcode_vendor.c's QR and Data Matrix,
// barcode1d.c's 1D codes); a code found twice in one place counts once,
// and the codes come top to bottom, then left to right.
#include "barcode_internal.h"

#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <dlfcn.h>
#include <pthread.h>
#endif

static const struct {
	unsigned bit;
	const char *name;
} kinds[] = {
    {ASUKA_BARCODE_QR, "qr"},           {ASUKA_BARCODE_DATAMATRIX, "datamatrix"}, {ASUKA_BARCODE_EAN13, "ean13"},
    {ASUKA_BARCODE_EAN8, "ean8"},       {ASUKA_BARCODE_UPCA, "upca"},             {ASUKA_BARCODE_UPCE, "upce"},
    {ASUKA_BARCODE_CODE128, "code128"}, {ASUKA_BARCODE_CODE39, "code39"},
};

unsigned asuka_barcode_kind(const char *name) {
	for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++)
		if (name && strcmp(kinds[i].name, name) == 0) return kinds[i].bit;
	return 0;
}

static const char *kind_name(unsigned bit) {
	for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++)
		if (kinds[i].bit == bit) return kinds[i].name;
	return "";
}

char *asuka_barcode_strdup(const char *s) {
	size_t n = strlen(s);
	char *p = malloc(n + 1);
	memcpy(p, s, n + 1);
	return p;
}

// utf8_valid reports whether p is UTF-8.
static bool utf8_valid(const uint8_t *p, size_t n) {
	for (size_t i = 0; i < n;) {
		uint8_t c = p[i];
		size_t k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
		if (k == 0 || i + k > n || (k == 2 && c < 0xC2)) return false;
		for (size_t j = 1; j < k; j++)
			if ((p[i + j] & 0xC0) != 0x80) return false;
		i += k;
	}
	return true;
}

char *bc_text(const uint8_t *p, size_t n) {
	if (utf8_valid(p, n)) {
		char *s = malloc(n + 1);
		memcpy(s, p, n);
		s[n] = 0;
		return s;
	}
	char *s = malloc(2 * n + 1), *o = s;
	for (size_t i = 0; i < n; i++) {
		if (p[i] < 0x80) *o++ = (char)p[i];
		else *o++ = (char)(0xC0 | p[i] >> 6), *o++ = (char)(0x80 | (p[i] & 0x3F));
	}
	*o = 0;
	return s;
}

void bc_add(bc_found **found, size_t *n, size_t *cap, bc_found f) {
	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 8;
		*found = realloc(*found, *cap * sizeof **found);
	}
	(*found)[(*n)++] = f;
}

static int area(int x0, int y0, int x1, int y1) { return x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) : 0; }

// overlaps reports whether a and b share at least half the smaller's
// area.
static bool overlaps(const bc_found *a, const bc_found *b) {
	int x0 = a->x0 > b->x0 ? a->x0 : b->x0, y0 = a->y0 > b->y0 ? a->y0 : b->y0;
	int x1 = a->x1 < b->x1 ? a->x1 : b->x1, y1 = a->y1 < b->y1 ? a->y1 : b->y1;
	int in = area(x0, y0, x1, y1);
	if (in == 0) return false;
	int m = area(a->x0, a->y0, a->x1, a->y1), mb = area(b->x0, b->y0, b->x1, b->y1);
	if (mb < m) m = mb;
	return 2 * in >= m;
}

static int by_place(const void *pa, const void *pb) {
	const asuka_barcode *a = pa, *b = pb;
	if (a->y0 != b->y0) return a->y0 - b->y0;
	return a->x0 - b->x0;
}

size_t asuka_barcode_decode(const uint8_t *gray, int w, int h, int stride, unsigned want, asuka_barcode **out) {
	*out = NULL;
	want &= ASUKA_BARCODE_ALL;
	if (!want || w <= 0 || h <= 0 || !gray) return 0;
	bc_found *found = NULL;
	size_t n = 0, cap = 0;
	if (want & ASUKA_BARCODE_QR) bc_decode_qr(gray, w, h, stride, &found, &n, &cap);
	if (want & ASUKA_BARCODE_DATAMATRIX) bc_decode_datamatrix(gray, w, h, stride, &found, &n, &cap);
	unsigned oned = want & ~(unsigned)(ASUKA_BARCODE_QR | ASUKA_BARCODE_DATAMATRIX);
	if (oned) {
		bc_found *f1;
		size_t n1 = bc_decode_1d(gray, w, h, stride, oned, &f1);
		for (size_t i = 0; i < n1; i++) bc_add(&found, &n, &cap, f1[i]);
		free(f1);
	}
	// A code found again in the same place counts once.
	asuka_barcode *codes = calloc(n ? n : 1, sizeof *codes);
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		bc_found *c = &found[i];
		bool dup = false;
		for (size_t j = 0; j < k && !dup; j++) {
			asuka_barcode *o = &codes[j];
			bc_found of = {asuka_barcode_kind(o->kind), o->text, o->x0, o->y0, o->x1, o->y1};
			if (of.kind == c->kind && strcmp(o->text, c->text) == 0 && overlaps(&of, c)) {
				if (c->x0 < o->x0) o->x0 = c->x0;
				if (c->y0 < o->y0) o->y0 = c->y0;
				if (c->x1 > o->x1) o->x1 = c->x1;
				if (c->y1 > o->y1) o->y1 = c->y1;
				dup = true;
			}
		}
		if (dup) {
			free(c->text);
			continue;
		}
		codes[k++] = (asuka_barcode){kind_name(c->kind), c->text, c->x0, c->y0, c->x1, c->y1};
	}
	free(found);
	// Stable: equal places keep their readers' order.
	for (size_t i = 1; i < k; i++)
		for (size_t j = i; j > 0 && by_place(&codes[j - 1], &codes[j]) > 0; j--) {
			asuka_barcode t = codes[j];
			codes[j] = codes[j - 1], codes[j - 1] = t;
		}
	*out = codes;
	return k;
}

void asuka_barcodes_free(asuka_barcode *codes, size_t n) {
	for (size_t i = 0; i < n; i++) free(codes[i].text);
	free(codes);
}

uint8_t *asuka_barcode_gray(const uint8_t *pixels, int w, int h, int stride, bool bgr, bool premultiplied) {
	uint8_t *g = malloc((size_t)(w > 0 ? w : 1) * (h > 0 ? h : 1));
	for (int y = 0; y < h; y++) {
		const uint8_t *p = pixels + (size_t)y * stride;
		for (int x = 0; x < w; x++, p += 4) {
			unsigned r = bgr ? p[2] : p[0], gg = p[1], b = bgr ? p[0] : p[2], a = p[3];
			unsigned l = (299 * r + 587 * gg + 114 * b + 500) / 1000;
			// Transparent pixels are composed over white.
			unsigned v = premultiplied ? l + 255 - a : (l * a + 255 * (255 - a)) / 255;
			g[(size_t)y * w + x] = (uint8_t)(v > 255 ? 255 : v);
		}
	}
	return g;
}

// ---- ZBar (Linux) ----

#ifdef __linux__
static struct {
	pthread_once_t once;
	bool ok;
	void *(*scanner_create)(void);
	void (*scanner_destroy)(void *);
	int (*scanner_set_config)(void *, int sym, int cfg, int val);
	void *(*image_create)(void);
	void (*image_destroy)(void *);
	void (*image_set_format)(void *, unsigned long fourcc);
	void (*image_set_size)(void *, unsigned w, unsigned h);
	void (*image_set_data)(void *, const void *, unsigned long n, void (*cleanup)(void *));
	int (*scan_image)(void *, void *);
	const void *(*first_symbol)(const void *);
	const void *(*symbol_next)(const void *);
	int (*symbol_type)(const void *);
	const char *(*symbol_name)(int);
	const char *(*symbol_data)(const void *);
	unsigned (*symbol_data_length)(const void *);
	unsigned (*symbol_loc_size)(const void *);
	int (*symbol_loc_x)(const void *, unsigned);
	int (*symbol_loc_y)(const void *, unsigned);
} zb = {.once = PTHREAD_ONCE_INIT};

static void load_zbar(void) {
	void *l = dlopen("libzbar.so.0", RTLD_NOW);
	if (!l) l = dlopen("libzbar.so", RTLD_NOW);
	if (!l) return;
	bool ok = true;
#define Z(f, name) ok = (*(void **)&zb.f = dlsym(l, name)) && ok
	Z(scanner_create, "zbar_image_scanner_create");
	Z(scanner_destroy, "zbar_image_scanner_destroy");
	Z(scanner_set_config, "zbar_image_scanner_set_config");
	Z(image_create, "zbar_image_create");
	Z(image_destroy, "zbar_image_destroy");
	Z(image_set_format, "zbar_image_set_format");
	Z(image_set_size, "zbar_image_set_size");
	Z(image_set_data, "zbar_image_set_data");
	Z(scan_image, "zbar_scan_image");
	Z(first_symbol, "zbar_image_first_symbol");
	Z(symbol_next, "zbar_symbol_next");
	Z(symbol_type, "zbar_symbol_get_type");
	Z(symbol_name, "zbar_get_symbol_name");
	Z(symbol_data, "zbar_symbol_get_data");
	Z(symbol_data_length, "zbar_symbol_get_data_length");
	Z(symbol_loc_size, "zbar_symbol_get_loc_size");
	Z(symbol_loc_x, "zbar_symbol_get_loc_x");
	Z(symbol_loc_y, "zbar_symbol_get_loc_y");
#undef Z
	zb.ok = ok;
}

// zbar_kind is asuka/scan's name of a ZBar symbology (NULL: none of its).
static const char *zbar_kind(const char *t) {
	static const char *const names[][2] = {
	    {"QR-Code", "qr"},     {"EAN-13", "ean13"},     {"EAN-8", "ean8"},       {"UPC-A", "upca"},     {"UPC-E", "upce"}, {"CODE-128", "code128"},
	    {"CODE-39", "code39"}, {"CODE-93", "code93"},   {"Codabar", "codabar"}, {"CODABAR", "codabar"}, {"I2/5", "itf"},  {"PDF417", "pdf417"},
	};
	for (size_t i = 0; t && i < sizeof names / sizeof *names; i++)
		if (strcmp(names[i][0], t) == 0) return names[i][1];
	return NULL;
}

long asuka_barcode_zbar(const uint8_t *gray, int w, int h, asuka_barcode **out) {
	*out = NULL;
	pthread_once(&zb.once, load_zbar);
	if (!zb.ok) return -1;
	void *s = zb.scanner_create();
	zb.scanner_set_config(s, 0, 0, 1);   // every symbology (ZBAR_CFG_ENABLE)...
	zb.scanner_set_config(s, 10, 0, 0);  // ...but ISBN-10's
	zb.scanner_set_config(s, 14, 0, 0);  // and ISBN-13's: EAN-13s
	void *img = zb.image_create();
	zb.image_set_format(img, 'Y' | '8' << 8 | '0' << 16 | (unsigned long)'0' << 24);
	zb.image_set_size(img, (unsigned)w, (unsigned)h);
	zb.image_set_data(img, gray, (unsigned long)w * h, NULL);
	asuka_barcode *codes = NULL;
	size_t n = 0, cap = 0;
	if (zb.scan_image(s, img) > 0) {
		for (const void *sym = zb.first_symbol(img); sym; sym = zb.symbol_next(sym)) {
			const char *kind = zbar_kind(zb.symbol_name(zb.symbol_type(sym)));
			if (!kind) continue;
			asuka_barcode c = {kind, bc_text((const uint8_t *)zb.symbol_data(sym), zb.symbol_data_length(sym)), w, h, 0, 0};
			unsigned k = zb.symbol_loc_size(sym);
			for (unsigned i = 0; i < k; i++) {
				int x = zb.symbol_loc_x(sym, i), y = zb.symbol_loc_y(sym, i);
				if (x < c.x0) c.x0 = x;
				if (y < c.y0) c.y0 = y;
				if (x + 1 > c.x1) c.x1 = x + 1;
				if (y + 1 > c.y1) c.y1 = y + 1;
			}
			if (k == 0) c.x0 = c.y0 = 0, c.x1 = w, c.y1 = h;
			if (n == cap) {
				cap = cap ? cap * 2 : 8;
				codes = realloc(codes, cap * sizeof *codes);
			}
			codes[n++] = c;
		}
	}
	zb.image_destroy(img);
	zb.scanner_destroy(s);
	*out = codes;
	return (long)n;
}
#else
long asuka_barcode_zbar(const uint8_t *gray, int w, int h, asuka_barcode **out) {
	*out = NULL;
	return -1;
}
#endif
