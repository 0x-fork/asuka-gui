// platform/vision on Windows and Linux: the image (a file an app names)
// decoded upright, then what's asked for: its text and faces (each
// platform's: vision_windows.c, vision_linux.c), and its codes
// (platform/desktop's reader, the same on both), boxes as
// fractions of the image from its top left. Its work is a thread of its
// own.
#include "asuka_barcode.h"
#include "vision.h"

#include <stdlib.h>
#include <string.h>

uint8_t *vision_gray(const uint8_t *px, int w, int h, bool bgr, bool premultiplied) {
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

vision_Box vision_box(double x0, double y0, double x1, double y1, int w, int h) {
	return (vision_Box){x0 / w, y0 / h, (x1 - x0) / w, (y1 - y0) / h};
}

vision_Box vision_union(vision_Box a, vision_Box b) {
	if (a.w <= 0 || a.h <= 0) return b;
	if (b.w <= 0 || b.h <= 0) return a;
	double x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
	double x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	return (vision_Box){x0, y0, x1 - x0, y1 - y0};
}

// runes counts UTF-8's characters in s's first n bytes.
static int64_t runes(const char *s, size_t n) {
	int64_t k = 0;
	for (size_t i = 0; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80) k++;
	return k;
}

bool vision_place(const char *line, const char *word, size_t *at, int64_t *start, int64_t *end) {
	if (!*word) return false;
	const char *f = strstr(line + *at, word);
	if (!f) return false;
	*start = runes(line, (size_t)(f - line));
	*end = *start + runes(word, strlen(word));
	*at = (size_t)(f - line) + strlen(word);
	return true;
}

void vision_lines_push(vision_list_Line *l, vision_Line v) {
	l->data = realloc(l->data, (l->len + 1) * sizeof *l->data);
	l->data[l->len++] = v;
}

void vision_words_push(vision_list_Word *l, vision_Word v) {
	l->data = realloc(l->data, (l->len + 1) * sizeof *l->data);
	l->data[l->len++] = v;
}

void vision_image_free(vision_image *img) {
	free(img->px);
	free(img->gray);
	*img = (vision_image){0};
}

typedef struct {
	const char *path;
	bool text, faces, codes;
	vision_list_string languages;
	asuka_call *call;
} job;

static void run(void *p) {
	job *j = p;
	asuka_error err = {0};
	vision_image img = {0};
	char *local = asuka_desktop_local_file(j->path, NULL, &err);
	bool ok = local && vision_load(local, &img);
	if (local && !ok) asuka_error_failed(&err, "vision", 0, "no image at %s", j->path);
	free(local);
	vision_Result r = {.width = img.w, .height = img.h};
	if (ok && j->text) ok = vision_text(&img, j->languages, &r.lines, &err);
	if (ok && j->faces) ok = vision_faces(&img, &r.faces, &err);
	if (ok && j->codes) {
		asuka_barcode *codes = NULL;
		size_t n = asuka_barcode_decode(img.gray, img.w, img.h, img.w, ASUKA_BARCODE_ALL, &codes);
		r.codes.data = calloc(n ? n : 1, sizeof *r.codes.data);
		for (size_t i = 0; i < n; i++)
			r.codes.data[r.codes.len++] = (vision_Code){asuka_strdup(asuka_barcode_name(codes[i].kind)), asuka_strdup(codes[i].text),
			                                            vision_box(codes[i].x0, codes[i].y0, codes[i].x1, codes[i].y1, img.w, img.h)};
		asuka_barcode_free(codes, n);
	}
	vision_image_free(&img);
	if (ok) {
		vision_Analyzer_analyze_done(j->call, r);
	} else {
		vision_Result_free(&r);
		asuka_call_fail(j->call, &err);
	}
	free(j);
}

static void analyze(vision_Analyzer *self, const char *path, bool text, bool faces, bool codes, vision_list_string languages, asuka_call *call) {
	job *j = malloc(sizeof *j);
	*j = (job){path, text, faces, codes, languages, call};
	if (!asuka_desktop_spawn(run, j)) {
		free(j);
		asuka_error e = {0};
		asuka_error_failed(&e, "vision", 0, "no thread to look on");
		asuka_call_fail(call, &e);
	}
}

static const vision_Analyzer_vtable vt = {.analyze = analyze};

vision_Analyzer *vision_make_Analyzer(asuka_error *err) {
	vision_Analyzer *a = malloc(sizeof *a);
	a->vt = &vt;
	return a;
}
