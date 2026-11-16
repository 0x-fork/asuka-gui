// asuka/vision on Linux: the image decoded by GdkPixbuf; its text read by
// Tesseract where it's installed (libtesseract5 and its languages' data),
// line by line and word by word, in the languages asked for (else the
// user's) that it has data for, else English. No library on a Linux
// desktop finds faces: none are found.
#include "asuka_barcode.h"
#include "asuka_linux.h"
#include "vision.h"

#include <dlfcn.h>
#include <glob.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool vision_load(const char *path, vision_image *img) {
	if (!asuka_linux_image_load(path, &img->w, &img->h, &img->px)) return false;
	img->gray = asuka_barcode_gray(img->px, img->w, img->h, img->w * 4, false, false);
	return true;
}

bool vision_faces(const vision_image *img, vision_list_Box *out, asuka_error *err) { return true; }

// ---- Tesseract ----

static struct {
	pthread_once_t once;
	bool ok;
	void *(*create)(void);
	void (*delete_)(void *);
	void (*end)(void *);
	int (*init3)(void *, const char *datapath, const char *lang);
	void (*set_image)(void *, const unsigned char *, int w, int h, int bpp, int bpl);
	int (*recognize)(void *, void *monitor);
	void *(*iterator)(void *);
	void (*iter_delete)(void *);
	int (*iter_next)(void *, int level);
	char *(*iter_text)(void *, int level);
	void *(*iter_page)(void *);
	void (*delete_text)(char *);
	int (*page_box)(void *, int level, int *l, int *t, int *r, int *b);
	int (*page_at_beginning)(void *, int level);
} tess = {.once = PTHREAD_ONCE_INIT};

static void load_tess(void) {
	static const char *names[] = {"libtesseract.so.5", "libtesseract.so.4", "libtesseract.so"};
	void *l = NULL;
	for (size_t i = 0; i < 3 && !l; i++) l = dlopen(names[i], RTLD_NOW);
	if (!l) return;
	bool ok = true;
#define T(f, name) ok = (*(void **)&tess.f = dlsym(l, name)) && ok
	T(create, "TessBaseAPICreate");
	T(delete_, "TessBaseAPIDelete");
	T(end, "TessBaseAPIEnd");
	T(init3, "TessBaseAPIInit3");
	T(set_image, "TessBaseAPISetImage");
	T(recognize, "TessBaseAPIRecognize");
	T(iterator, "TessBaseAPIGetIterator");
	T(iter_delete, "TessResultIteratorDelete");
	T(iter_next, "TessResultIteratorNext");
	T(iter_text, "TessResultIteratorGetUTF8Text");
	T(iter_page, "TessResultIteratorGetPageIterator");
	T(delete_text, "TessDeleteText");
	T(page_box, "TessPageIteratorBoundingBox");
	T(page_at_beginning, "TessPageIteratorIsAtBeginningOf");
#undef T
	tess.ok = ok;
}

// Tesseract's names of languages, by BCP 47's.
static const char *const tess_codes[][2] = {
    {"en", "eng"},      {"de", "deu"},      {"fr", "fra"},      {"es", "spa"},      {"it", "ita"},           {"pt", "por"},     {"nl", "nld"},
    {"sv", "swe"},      {"da", "dan"},      {"nb", "nor"},      {"no", "nor"},      {"fi", "fin"},           {"pl", "pol"},     {"cs", "ces"},
    {"sk", "slk"},      {"hu", "hun"},      {"ro", "ron"},      {"tr", "tur"},      {"el", "ell"},           {"ru", "rus"},     {"uk", "ukr"},
    {"bg", "bul"},      {"ar", "ara"},      {"he", "heb"},      {"fa", "fas"},      {"hi", "hin"},           {"th", "tha"},     {"vi", "vie"},
    {"id", "ind"},      {"ms", "msa"},      {"ja", "jpn"},      {"ko", "kor"},      {"zh-Hans", "chi_sim"}, {"zh-Hant", "chi_tra"}, {"zh", "chi_sim"},
    {"zh-TW", "chi_tra"}, {"zh-HK", "chi_tra"}, {"ca", "cat"}, {"hr", "hrv"}, {"sr", "srp"},
};

static const char *tess_code(const char *tag) {
	for (size_t i = 0; i < sizeof tess_codes / sizeof *tess_codes; i++)
		if (strcmp(tess_codes[i][0], tag) == 0) return tess_codes[i][1];
	return NULL;
}

// has_data reports whether Tesseract has a language's data where it
// looks for it.
static bool has_data(const char *code) {
	char path[1024];
	struct stat st;
	const char *prefix = getenv("TESSDATA_PREFIX");
	if (prefix && *prefix) {
		snprintf(path, sizeof path, "%s/%s.traineddata", prefix, code);
		if (stat(path, &st) == 0) return true;
		snprintf(path, sizeof path, "%s/tessdata/%s.traineddata", prefix, code);
		if (stat(path, &st) == 0) return true;
	}
	char pattern[256];
	snprintf(pattern, sizeof pattern, "/usr/share/tesseract-ocr/*/tessdata/%s.traineddata", code);
	glob_t g;
	bool found = glob(pattern, 0, NULL, &g) == 0 && g.gl_pathc > 0;
	globfree(&g);
	if (found) return true;
	static const char *dirs[] = {"/usr/share/tessdata", "/usr/local/share/tessdata"};
	for (size_t i = 0; i < 2; i++) {
		snprintf(path, sizeof path, "%s/%s.traineddata", dirs[i], code);
		if (stat(path, &st) == 0) return true;
	}
	return false;
}

// user_language is the user's language (LANGUAGE, LC_ALL, LC_MESSAGES,
// LANG) as a BCP 47 tag, of malloc's (NULL: none).
static char *user_language(void) {
	static const char *vars[] = {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"};
	for (size_t i = 0; i < 4; i++) {
		const char *v = getenv(vars[i]);
		if (!v || !*v) continue;
		size_t n = strcspn(v, ":");
		char *s = asuka_strndup(v, n);
		if (!*s || strcmp(s, "C") == 0 || strcmp(s, "POSIX") == 0) {
			free(s);
			continue;
		}
		s[strcspn(s, ".@")] = 0;
		for (char *c = s; *c; c++)
			if (*c == '_') *c = '-';
		return s;
	}
	return NULL;
}

// languages is Tesseract's languages for the tags (none: the user's),
// those it has data for ("eng" if none), of malloc's.
static char *languages(vision_list_string tags) {
	char *user = tags.len == 0 ? user_language() : NULL;
	size_t n = tags.len ? tags.len : user ? 1 : 0;
	asuka_writer w = {0};
	for (size_t i = 0; i < n; i++) {
		const char *tag = tags.len ? tags.data[i] : user;
		const char *code = tess_code(tag);
		if (!code) {
			char *base = asuka_strndup(tag, strcspn(tag, "-"));
			code = tess_code(base);
			free(base);
		}
		if (!code || !has_data(code)) continue;
		asuka_writer_u8(&w, 0);  // what's there so far, as a string
		bool seen = strstr((const char *)w.data, code) != NULL;
		w.len--;
		if (seen) continue;
		if (w.len) asuka_writer_put(&w, "+", 1);
		asuka_writer_put(&w, code, strlen(code));
	}
	free(user);
	if (!w.len) asuka_writer_put(&w, "eng", 3);
	asuka_writer_u8(&w, 0);
	return (char *)w.data;
}

enum { LINE = 2, WORD = 3 };

static char *iter_text(void *it, int level) {
	char *t = tess.iter_text(it, level);
	char *s = asuka_strdup(t ? t : "");
	if (t) tess.delete_text(t);
	return s;
}

// read reads the gray levels in langs into lines: false when Tesseract
// has no data for them, or fails.
static bool read_text(const vision_image *img, const char *langs, vision_list_Line *out) {
	void *api = tess.create();
	if (tess.init3(api, NULL, langs) != 0) {
		tess.delete_(api);
		return false;
	}
	tess.set_image(api, img->gray, img->w, img->h, 1, img->w);
	bool ok = tess.recognize(api, NULL) == 0;
	void *it = ok ? tess.iterator(api) : NULL;
	if (it) {
		void *page = tess.iter_page(it);
		size_t at = 0;  // bytes into the line's text searched
		bool first = true, skipping = false;
		do {
			int l, t, r, b;
			if (first || tess.page_at_beginning(page, LINE)) {
				first = false;
				char *text = iter_text(it, LINE);
				size_t n = strlen(text);
				while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\n')) text[--n] = 0;
				skipping = strspn(text, " \t\n") == n;  // a line of nothing
				if (skipping) {
					free(text);
				} else {
					vision_Line line = {.text = text};
					if (tess.page_box(page, LINE, &l, &t, &r, &b)) line.box = vision_box(l, t, r, b, img->w, img->h);
					vision_lines_push(out, line);
					at = 0;
				}
			}
			if (skipping || out->len == 0) continue;
			char *word = iter_text(it, WORD);
			vision_Line *line = &out->data[out->len - 1];
			vision_Word wd = {.text = word};
			if (*word && vision_place(line->text, word, &at, &wd.start, &wd.end)) {
				if (tess.page_box(page, WORD, &l, &t, &r, &b)) wd.box = vision_box(l, t, r, b, img->w, img->h);
				vision_words_push(&line->words, wd);
			} else {
				free(word);
			}
		} while (tess.iter_next(it, WORD));
		tess.iter_delete(it);
	}
	tess.end(api);
	tess.delete_(api);
	return ok;
}

bool vision_text(const vision_image *img, vision_list_string tags, vision_list_Line *out, asuka_error *err) {
	pthread_once(&tess.once, load_tess);
	if (!tess.ok) return true;  // no Tesseract: no text
	char *langs = languages(tags);
	if (!read_text(img, langs, out) && strcmp(langs, "eng") != 0) {
		vision_list_Line_free(out);
		*out = (vision_list_Line){0};
		read_text(img, "eng", out);
	}
	free(langs);
	return true;
}
