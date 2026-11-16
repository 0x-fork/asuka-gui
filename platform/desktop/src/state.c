// state.c: where the windows were (asuka-window.json, its
// format) and where their routers were (asuka-routes.json, internal/
// routestate's) (desktop.h).
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L  // strdup, readlink under -std=c17
#endif
#include "desktop.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK lock_t;
#define LOCK_INIT SRWLOCK_INIT
static void lock(lock_t *l) { AcquireSRWLockExclusive(l); }
static void unlock(lock_t *l) { ReleaseSRWLockExclusive(l); }
#else
#include <pthread.h>
typedef pthread_mutex_t lock_t;
#define LOCK_INIT PTHREAD_MUTEX_INITIALIZER
static void lock(lock_t *l) { pthread_mutex_lock(l); }
static void unlock(lock_t *l) { pthread_mutex_unlock(l); }
#endif

// ---- asuka-window.json ----

struct dsk_winstate {
	char *path;
	dsk_json *all;  // an object: key → the window's state
	char *saved;    // the file as last read or written
};

static dsk_json *empty_object(void) { return dsk_json_parse("{}", 2); }

dsk_winstate *dsk_winstate_open(const char *path) {
	dsk_winstate *s = calloc(1, sizeof *s);
	if (!s) return NULL;
	s->path = path && *path ? strdup(path) : NULL;
	size_t n = 0;
	char *text = s->path ? dsk_read_file(s->path, &n) : NULL;
	dsk_json *j = dsk_json_parse(text, n);
	if (j && j->type == DSK_JOBJ) {
		bool old = false;
		for (size_t i = 0; i < j->n; i++)
			if (j->kids[i].type != DSK_JOBJ) old = true;
		if (old && j->n > 0) {
			// An older file: one window's fields, the main window's.
			dsk_json *all = empty_object();
			dsk_jw w = {0};
			dsk_jw_obj(&w);
			dsk_jw_key(&w, "main");
			dsk_jw_value(&w, j);
			dsk_jw_end(&w);
			size_t len;
			char *t = dsk_jw_take(&w, &len);
			dsk_json_free(all);
			all = dsk_json_parse(t, len);
			free(t);
			dsk_json_free(j);
			j = all;
		}
		s->all = j;
		s->saved = text;
		text = NULL;
	} else {
		dsk_json_free(j);
		s->all = empty_object();
	}
	free(text);
	return s;
}

void dsk_winstate_close(dsk_winstate *s) {
	if (!s) return;
	free(s->path);
	dsk_json_free(s->all);
	free(s->saved);
	free(s);
}

const dsk_json *dsk_winstate_load(dsk_winstate *s, const char *key) {
	if (!s || !key || !*key) return NULL;
	const dsk_json *v = dsk_json_get(s->all, key);
	return v && v->type == DSK_JOBJ ? v : NULL;
}

// rewrite writes the store with key's state replaced by json (NULL:
// dropped).
static void rewrite(dsk_winstate *s, const char *key, const char *json) {
	dsk_json *v = json ? dsk_json_parse(json, strlen(json)) : NULL;
	if (json && !v) return;
	dsk_jw w = {0};
	dsk_jw_obj(&w);
	bool done = false;
	for (size_t i = 0; i < s->all->n; i++) {
		if (strcmp(s->all->keys[i], key) == 0) {
			if (v && !done) {
				dsk_jw_key(&w, key);
				dsk_jw_value(&w, v);
			}
			done = true;
			continue;
		}
		dsk_jw_key(&w, s->all->keys[i]);
		dsk_jw_value(&w, &s->all->kids[i]);
	}
	if (v && !done) {
		dsk_jw_key(&w, key);
		dsk_jw_value(&w, v);
	}
	dsk_jw_end(&w);
	dsk_json_free(v);
	size_t len;
	char *text = dsk_jw_take(&w, &len);
	if (!text) return;
	dsk_json *all = dsk_json_parse(text, len);
	if (!all) {
		free(text);
		return;
	}
	dsk_json_free(s->all);
	s->all = all;
	if (s->saved && strcmp(s->saved, text) == 0) {
		free(text);
		return;
	}
	if (dsk_write_file(s->path, text, len)) {
		free(s->saved);
		s->saved = text;
	} else {
		free(text);
	}
}

void dsk_winstate_save(dsk_winstate *s, const char *key, const char *json) {
	if (!s || !s->path || !key || !*key || !json) return;
	rewrite(s, key, json);
}

void dsk_winstate_forget(dsk_winstate *s, const char *key) {
	if (!s || !s->path || !key || !dsk_json_get(s->all, key)) return;
	rewrite(s, key, NULL);
}

// ---- asuka-routes.json ----

typedef struct {
	char *key, *href, *state;
	size_t state_len;
} route;

struct dsk_routes {
	lock_t mu;
	char *path;
	route *r;
	size_t n, cap;
	bool pending;
};

char *dsk_routes_path(void) {
	char *keep = dsk_getenv("ASUKA_ROUTES");
	bool kept = keep && !strcmp(keep, "1");
	free(keep);
	const char *skip[] = {"ASUKA_SNAPSHOT", "ASUKA_SCRIPT", "ASUKA_TEST", "ASUKA_STATE"};
	for (size_t i = 0; i < sizeof skip / sizeof *skip && !kept; i++) {
		char *v = dsk_getenv(skip[i]);
		if (v) {
			free(v);
			return strdup("");
		}
	}
	char *dev = dsk_getenv("ASUKA_DEV");
	if (dev) {
		free(dev);
		return strdup("");
	}
	char *d = dsk_getenv("ASUKA_DATA");
	if (!d) return strdup("");
	char *p = dsk_join(d, "asuka-routes.json");
	free(d);
	return p;
}

static char *dupn(const char *s, size_t n) {
	char *d = malloc(n + 1);
	if (!d) return NULL;
	if (n) memcpy(d, s, n);
	d[n] = 0;
	return d;
}

static route *find(dsk_routes *s, const char *key) {
	for (size_t i = 0; i < s->n; i++)
		if (strcmp(s->r[i].key, key) == 0) return &s->r[i];
	return NULL;
}

static route *add(dsk_routes *s, const char *key) {
	if (s->n == s->cap) {
		size_t c = s->cap ? s->cap * 2 : 4;
		route *r = realloc(s->r, c * sizeof *r);
		if (!r) return NULL;
		s->r = r, s->cap = c;
	}
	route *r = &s->r[s->n++];
	memset(r, 0, sizeof *r);
	r->key = strdup(key);
	return r;
}

dsk_routes *dsk_routes_open(const char *path) {
	dsk_routes *s = calloc(1, sizeof *s);
	if (!s) return NULL;
	static const lock_t unlocked = LOCK_INIT;
	s->mu = unlocked;
	s->path = path && *path ? strdup(path) : NULL;
	size_t n = 0;
	char *text = s->path ? dsk_read_file(s->path, &n) : NULL;
	dsk_json *j = dsk_json_parse(text, n);
	free(text);
	if (j && j->type == DSK_JOBJ) {
		for (size_t i = 0; i < j->n; i++) {
			const dsk_json *e = &j->kids[i];
			const dsk_json *href = dsk_json_get(e, "href"), *state = dsk_json_get(e, "state");
			if (!href || href->type != DSK_JSTR) continue;
			route *r = find(s, j->keys[i]);
			if (!r) r = add(s, j->keys[i]);
			if (!r) break;
			free(r->href);
			free(r->state);
			r->href = dupn(href->str, href->len);
			r->state = state && state->type == DSK_JSTR ? dupn(state->str, state->len) : dupn("", 0);
			r->state_len = state && state->type == DSK_JSTR ? state->len : 0;
		}
	}
	dsk_json_free(j);
	return s;
}

void dsk_routes_close(dsk_routes *s) {
	if (!s) return;
	for (size_t i = 0; i < s->n; i++) {
		free(s->r[i].key);
		free(s->r[i].href);
		free(s->r[i].state);
	}
	free(s->r);
	free(s->path);
	free(s);
}

bool dsk_routes_location(dsk_routes *s, const char *key, const char **href, const char **state, size_t *state_len) {
	if (!s || !key || !*key) return false;
	lock(&s->mu);
	route *r = find(s, key);
	if (r) *href = r->href, *state = r->state, *state_len = r->state_len;
	unlock(&s->mu);
	return r != NULL;
}

bool dsk_routes_set(dsk_routes *s, const char *key, const char *href, size_t href_len, const char *state, size_t state_len) {
	if (!s || !key || !*key) return false;
	lock(&s->mu);
	route *r = find(s, key);
	bool same = r && strlen(r->href) == href_len && memcmp(r->href, href, href_len) == 0 && r->state_len == state_len &&
		(state_len == 0 || memcmp(r->state, state, state_len) == 0);
	if (!same) {
		if (!r) r = add(s, key);
		if (r) {
			free(r->href);
			free(r->state);
			r->href = dupn(href, href_len);
			r->state = dupn(state, state_len);
			r->state_len = state_len;
		}
		if (s->path) s->pending = true;
	}
	bool write = !same && s->path;
	unlock(&s->mu);
	return write;
}

void dsk_routes_flush(dsk_routes *s) {
	if (!s) return;
	lock(&s->mu);
	if (s->pending && s->path) {
		s->pending = false;
		dsk_jw w = {0};
		dsk_jw_obj(&w);
		for (size_t i = 0; i < s->n; i++) {
			dsk_jw_key(&w, s->r[i].key);
			dsk_jw_obj(&w);
			dsk_jw_key(&w, "href");
			dsk_jw_str(&w, s->r[i].href);
			if (s->r[i].state_len) {
				dsk_jw_key(&w, "state");
				dsk_jw_strn(&w, s->r[i].state, s->r[i].state_len);
			}
			dsk_jw_end(&w);
		}
		dsk_jw_end(&w);
		size_t len;
		char *text = dsk_jw_take(&w, &len);
		if (text) dsk_write_file(s->path, text, len);
		free(text);
	}
	unlock(&s->mu);
}
