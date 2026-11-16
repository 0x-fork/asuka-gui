// std/store on Windows and Linux: a JSON object of strings in the app's
// data directory (store.json), written whole a moment after a change, so
// a run of changes is one write (small values only). Flush writes now.
#ifdef _WIN32
#include "asuka_windows.h"
#else
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif
#include "store.asuka.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// delay_ms is how long a change waits for the next before it's written.
enum { delay_ms = 200 };

typedef struct {
	store_Prefs base;
	char *path;
	char **keys, **values;
	size_t n, cap;
	bool dirty, timer;
#ifdef _WIN32
	SRWLOCK mu;
#else
	pthread_mutex_t mu;
#endif
} prefs;

#ifdef _WIN32
static void lock(prefs *p) { AcquireSRWLockExclusive(&p->mu); }
static void unlock(prefs *p) { ReleaseSRWLockExclusive(&p->mu); }
static void nap(int ms) { Sleep((DWORD)ms); }
#else
static void lock(prefs *p) { pthread_mutex_lock(&p->mu); }
static void unlock(prefs *p) { pthread_mutex_unlock(&p->mu); }
static void nap(int ms) {
	struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
	nanosleep(&ts, NULL);
}
#endif

// ---- JSON: an object of strings ----

static void put_string(asuka_writer *w, const char *s) {
	asuka_writer_u8(w, '"');
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		char esc[8];
		switch (*p) {
		case '"': asuka_writer_u8(w, '\\'), asuka_writer_u8(w, '"'); break;
		case '\\': asuka_writer_u8(w, '\\'), asuka_writer_u8(w, '\\'); break;
		case '\n': asuka_writer_u8(w, '\\'), asuka_writer_u8(w, 'n'); break;
		case '\r': asuka_writer_u8(w, '\\'), asuka_writer_u8(w, 'r'); break;
		case '\t': asuka_writer_u8(w, '\\'), asuka_writer_u8(w, 't'); break;
		default:
			if (*p < 0x20) {
				snprintf(esc, sizeof esc, "\\u%04x", *p);
				for (char *e = esc; *e; e++) asuka_writer_u8(w, (uint8_t)*e);
			} else {
				asuka_writer_u8(w, *p);
			}
		}
	}
	asuka_writer_u8(w, '"');
}

static void skip_ws(const char **s) {
	while (**s == ' ' || **s == '\n' || **s == '\r' || **s == '\t') (*s)++;
}

static int hex4(const char *s) {
	int v = 0;
	for (int i = 0; i < 4; i++) {
		char c = s[i];
		v <<= 4;
		if (c >= '0' && c <= '9') v |= c - '0';
		else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
		else return -1;
	}
	return v;
}

static void utf8(asuka_writer *w, unsigned c) {
	if (c < 0x80) asuka_writer_u8(w, (uint8_t)c);
	else if (c < 0x800) asuka_writer_u8(w, (uint8_t)(0xC0 | c >> 6)), asuka_writer_u8(w, (uint8_t)(0x80 | (c & 0x3F)));
	else if (c < 0x10000)
		asuka_writer_u8(w, (uint8_t)(0xE0 | c >> 12)), asuka_writer_u8(w, (uint8_t)(0x80 | (c >> 6 & 0x3F))), asuka_writer_u8(w, (uint8_t)(0x80 | (c & 0x3F)));
	else
		asuka_writer_u8(w, (uint8_t)(0xF0 | c >> 18)), asuka_writer_u8(w, (uint8_t)(0x80 | (c >> 12 & 0x3F))),
		    asuka_writer_u8(w, (uint8_t)(0x80 | (c >> 6 & 0x3F))), asuka_writer_u8(w, (uint8_t)(0x80 | (c & 0x3F)));
}

// get_string reads a JSON string at *s: NULL if it isn't one.
static char *get_string(const char **s) {
	skip_ws(s);
	if (**s != '"') return NULL;
	(*s)++;
	asuka_writer w = {0};
	while (**s && **s != '"') {
		if (**s != '\\') {
			asuka_writer_u8(&w, (uint8_t)*(*s)++);
			continue;
		}
		(*s)++;
		char c = *(*s)++;
		switch (c) {
		case 'n': asuka_writer_u8(&w, '\n'); break;
		case 'r': asuka_writer_u8(&w, '\r'); break;
		case 't': asuka_writer_u8(&w, '\t'); break;
		case 'b': asuka_writer_u8(&w, '\b'); break;
		case 'f': asuka_writer_u8(&w, '\f'); break;
		case 'u': {
			int u = hex4(*s);
			if (u < 0) goto bad;
			*s += 4;
			unsigned cp = (unsigned)u;
			if (cp >= 0xD800 && cp < 0xDC00 && (*s)[0] == '\\' && (*s)[1] == 'u') {
				int lo = hex4(*s + 2);
				if (lo >= 0xDC00 && lo < 0xE000) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)lo - 0xDC00);
					*s += 6;
				}
			}
			utf8(&w, cp);
			break;
		}
		case 0: goto bad;
		default: asuka_writer_u8(&w, (uint8_t)c);
		}
	}
	if (**s != '"') goto bad;
	(*s)++;
	asuka_writer_u8(&w, 0);
	return (char *)w.data;
bad:
	asuka_writer_free(&w);
	return NULL;
}

static ptrdiff_t find(prefs *p, const char *key) {
	for (size_t i = 0; i < p->n; i++)
		if (strcmp(p->keys[i], key) == 0) return (ptrdiff_t)i;
	return -1;
}

static void put(prefs *p, char *key, char *value) {
	ptrdiff_t i = find(p, key);
	if (i >= 0) {
		free(key);
		free(p->values[i]);
		p->values[i] = value;
		return;
	}
	if (p->n == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 16;
		p->keys = realloc(p->keys, p->cap * sizeof *p->keys);
		p->values = realloc(p->values, p->cap * sizeof *p->values);
	}
	p->keys[p->n] = key;
	p->values[p->n++] = value;
}

static void load(prefs *p) {
	FILE *f = NULL;
#ifdef _WIN32
	wchar_t *w = asuka_wide(p->path);
	f = _wfopen(w, L"rb");
	free(w);
#else
	f = fopen(p->path, "rb");
#endif
	if (!f) return;
	asuka_writer b = {0};
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) asuka_writer_put(&b, buf, n);
	fclose(f);
	asuka_writer_u8(&b, 0);
	const char *s = (const char *)b.data;
	skip_ws(&s);
	if (*s == '{') {
		s++;
		for (;;) {
			skip_ws(&s);
			if (*s == '}') break;
			char *k = get_string(&s);
			skip_ws(&s);
			if (!k || *s != ':') {
				free(k);
				break;
			}
			s++;
			char *v = get_string(&s);
			if (!v) {
				free(k);
				break;
			}
			put(p, k, v);
			skip_ws(&s);
			if (*s == ',') s++;
		}
	}
	asuka_writer_free(&b);
}

// write writes the file when something changed (p->mu held).
static bool write_file(prefs *p) {
	if (!p->dirty) return true;
	asuka_writer w = {0};
	asuka_writer_u8(&w, '{');
	for (size_t i = 0; i < p->n; i++) {
		if (i) asuka_writer_u8(&w, ',');
		put_string(&w, p->keys[i]);
		asuka_writer_u8(&w, ':');
		put_string(&w, p->values[i]);
	}
	asuka_writer_u8(&w, '}');
	char *tmp = asuka_sprintf("%s.tmp", p->path);
	bool ok = false;
#ifdef _WIN32
	wchar_t *wt = asuka_wide(tmp), *wp = asuka_wide(p->path);
	FILE *f = _wfopen(wt, L"wb");
	if (f) {
		ok = fwrite(w.data, 1, w.len, f) == w.len;
		ok = fclose(f) == 0 && ok;
		ok = ok && MoveFileExW(wt, wp, MOVEFILE_REPLACE_EXISTING);
	}
	free(wt);
	free(wp);
#else
	FILE *f = fopen(tmp, "wb");
	if (f) {
		ok = fwrite(w.data, 1, w.len, f) == w.len;
		ok = fclose(f) == 0 && ok;
		ok = ok && rename(tmp, p->path) == 0;
	}
#endif
	free(tmp);
	asuka_writer_free(&w);
	if (ok) p->dirty = false;
	return ok;
}

static void later(void *ctx) {
	prefs *p = ctx;
	nap(delay_ms);
	lock(p);
	p->timer = false;
	write_file(p);
	unlock(p);
}

// changed schedules the write (p->mu held).
static void changed(prefs *p) {
	p->dirty = true;
	if (p->timer) return;
	p->timer = asuka_desktop_spawn(later, p);
	if (!p->timer) write_file(p);
}

static char *s_get(store_Prefs *self, const char *key, asuka_error *err) {
	prefs *p = (prefs *)self;
	lock(p);
	ptrdiff_t i = find(p, key);
	char *v = i >= 0 ? asuka_strdup(p->values[i]) : NULL;
	unlock(p);
	return v;
}

static void s_set(store_Prefs *self, const char *key, const char *value, asuka_error *err) {
	prefs *p = (prefs *)self;
	lock(p);
	ptrdiff_t i = find(p, key);
	if (i < 0 || strcmp(p->values[i], value) != 0) {
		put(p, asuka_strdup(key), asuka_strdup(value));
		changed(p);
	}
	unlock(p);
}

static void s_remove(store_Prefs *self, const char *key, asuka_error *err) {
	prefs *p = (prefs *)self;
	lock(p);
	ptrdiff_t i = find(p, key);
	if (i >= 0) {
		free(p->keys[i]);
		free(p->values[i]);
		p->n--;
		p->keys[i] = p->keys[p->n];
		p->values[i] = p->values[p->n];
		changed(p);
	}
	unlock(p);
}

static void s_flush(store_Prefs *self, asuka_error *err) {
	prefs *p = (prefs *)self;
	lock(p);
	if (!write_file(p)) asuka_error_failed(err, "store", 0, "couldn't write %s", p->path);
	unlock(p);
}

static char *s_dir(store_Prefs *self, asuka_error *err) { return asuka_strdup(asuka_desktop_data_dir()); }

// The object lives as long as the app: a write it scheduled may come
// after the core let it go.
static void s_release(store_Prefs *self) {}

static const store_Prefs_vtable vt = {.release = s_release, .get = s_get, .set = s_set, .remove = s_remove, .flush = s_flush, .dir = s_dir};

store_Prefs *store_make_Prefs(asuka_error *err) {
	static prefs *one;
	if (one) return &one->base;
	prefs *p = calloc(1, sizeof *p);
	p->base.vt = &vt;
#ifdef _WIN32
	InitializeSRWLock(&p->mu);
	p->path = asuka_sprintf("%s\\store.json", asuka_desktop_data_dir());
#else
	pthread_mutex_init(&p->mu, NULL);
	p->path = asuka_sprintf("%s/store.json", asuka_desktop_data_dir());
#endif
	load(p);
	one = p;
	return &p->base;
}
