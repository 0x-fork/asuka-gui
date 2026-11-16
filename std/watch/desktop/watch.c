// std/watch on Windows and Linux: what both share. A watch keeps what
// it saw of the files and, a moment after the system says something
// changed (inotify, ReadDirectoryChangesW: watch_linux.c,
// watch_windows.c), looks again and compares: the events are the
// differences, a batch at a time. A file written many times in a row is
// one write; a file replaced at its path (an editor's atomic save) is a
// write; a file that moved within the directory is a rename with its old
// path (the same file system id).
#ifdef _WIN32
#include "asuka_windows.h"
#else
#include <pthread.h>
#include <time.h>
#endif
#include "watch.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// debounce is how long the watch waits for changes to stop, max_delay the
// longest it waits from the first.
enum { debounce = 50, max_delay = 500 };

// ---- Platform helpers ----

#ifdef _WIN32
uint64_t watch_now_ms(void) { return GetTickCount64(); }
void *watch_mutex_new(void) {
	SRWLOCK *l = malloc(sizeof *l);
	InitializeSRWLock(l);
	return l;
}
void watch_lock(void *m) { AcquireSRWLockExclusive(m); }
void watch_unlock(void *m) { ReleaseSRWLockExclusive(m); }
#else
uint64_t watch_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
void *watch_mutex_new(void) {
	pthread_mutex_t *m = malloc(sizeof *m);
	pthread_mutex_init(m, NULL);
	return m;
}
void watch_lock(void *m) { pthread_mutex_lock(m); }
void watch_unlock(void *m) { pthread_mutex_unlock(m); }
#endif

// ---- The snapshot ----

static size_t hash(const char *s) {
	size_t h = 1469598103934665603u;
	for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211u;
	return h;
}

static void reindex(wsnap *s) {
	free(s->slots);
	s->nslots = s->cap * 2;
	s->slots = malloc(s->nslots * sizeof *s->slots);
	for (size_t i = 0; i < s->nslots; i++) s->slots[i] = SIZE_MAX;
	for (size_t i = 0; i < s->n; i++) {
		size_t k = hash(s->e[i].rel) & (s->nslots - 1);
		while (s->slots[k] != SIZE_MAX) k = (k + 1) & (s->nslots - 1);
		s->slots[k] = i;
	}
}

void wsnap_add(wsnap *s, wentry e) {
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 64;
		s->e = realloc(s->e, s->cap * sizeof *s->e);
		s->e[s->n++] = e;
		reindex(s);
		return;
	}
	s->e[s->n] = e;
	size_t k = hash(e.rel) & (s->nslots - 1);
	while (s->slots[k] != SIZE_MAX) k = (k + 1) & (s->nslots - 1);
	s->slots[k] = s->n++;
}

const wentry *wsnap_find(const wsnap *s, const char *rel) {
	if (!s->nslots) return NULL;
	for (size_t k = hash(rel) & (s->nslots - 1); s->slots[k] != SIZE_MAX; k = (k + 1) & (s->nslots - 1))
		if (strcmp(s->e[s->slots[k]].rel, rel) == 0) return &s->e[s->slots[k]];
	return NULL;
}

void wsnap_free(wsnap *s) {
	for (size_t i = 0; i < s->n; i++) free(s->e[i].rel);
	free(s->e);
	free(s->slots);
	*s = (wsnap){0};
}

// ---- Patterns: a .gitignore's lines ----

// match is path.Match's: *, ?, [a-z] (^ negates), \ escapes; no '/'.
static bool match(const char *p, const char *s) {
	for (; *p; p++) {
		switch (*p) {
		case '*':
			while (p[1] == '*') p++;
			if (!p[1]) return !strchr(s, '/');
			for (const char *t = s; *t && *t != '/'; t++)
				if (match(p + 1, t + 1) || match(p + 1, t)) return true;
			return match(p + 1, s);
		case '?':
			if (!*s || *s == '/') return false;
			s++;
			break;
		case '[': {
			if (!*s) return false;
			const char *q = p + 1;
			bool neg = *q == '^';
			if (neg) q++;
			bool ok = false;
			for (; *q && *q != ']'; q++) {
				char lo = *q == '\\' && q[1] ? *++q : *q, hi = lo;
				if (q[1] == '-' && q[2] && q[2] != ']') {
					q += 2;
					hi = *q == '\\' && q[1] ? *++q : *q;
				}
				if (*s >= lo && *s <= hi) ok = true;
			}
			if (!*q || ok == neg) return false;
			p = q;
			s++;
			break;
		}
		case '\\':
			if (p[1]) p++;
			/* fallthrough */
		default:
			if (*s != *p) return false;
			s++;
		}
	}
	return !*s;
}

// parts_match reports whether the parts of a path match those of a
// pattern, where "**" matches any number of them.
static bool parts_match(char **pat, size_t np, char **parts, size_t n) {
	if (np == 0) return n == 0;
	if (strcmp(pat[0], "**") == 0) {
		if (np == 1) return n > 0;  // "logs/**": what is in logs
		for (size_t i = 0; i <= n; i++)
			if (parts_match(pat + 1, np - 1, parts + i, n - i)) return true;
		return false;
	}
	return n > 0 && match(pat[0], parts[0]) && parts_match(pat + 1, np - 1, parts + 1, n - 1);
}

static size_t split(char *s, char ***out) {
	size_t n = 1;
	for (char *p = s; *p; p++) n += *p == '/';
	*out = malloc(n * sizeof **out);
	size_t i = 0;
	(*out)[i++] = s;
	for (char *p = s; *p; p++)
		if (*p == '/') *p = 0, (*out)[i++] = p + 1;
	return n;
}

bool watch_ignored(const watch_list_string *patterns, const char *rel, bool dir) {
	if (strcmp(rel, ".") == 0) return false;
	const char *base = strrchr(rel, '/');
	base = base ? base + 1 : rel;
	bool hit = false;
	for (size_t i = 0; i < patterns->len && !hit; i++) {
		char *p = asuka_strdup(patterns->data[i]);
		size_t n = strlen(p);
		bool dir_only = n > 0 && p[n - 1] == '/';
		if (dir_only) p[--n] = 0;
		if (dir_only && !dir) {
			free(p);
			continue;
		}
		bool any_name = !strchr(p, '/');
		char *pp = p[0] == '/' ? p + 1 : p;
		if (!*pp) {
			free(p);
			continue;
		}
		if (any_name) {
			hit = match(pp, base);
		} else {
			char *path = asuka_strdup(rel);
			char **pat, **parts;
			size_t np = split(pp, &pat), nparts = split(path, &parts);
			hit = parts_match(pat, np, parts, nparts);
			free(pat);
			free(parts);
			free(path);
		}
		free(p);
	}
	return hit;
}

// ---- Differences ----

static int by_path(const void *a, const void *b) { return strcmp((*(const wentry *const *)a)->rel, (*(const wentry *const *)b)->rel); }

static char *parent(const char *rel) {
	const char *s = strrchr(rel, '/');
	return s ? asuka_strndup(rel, (size_t)(s - rel)) : NULL;
}

// top keeps the entries whose parent isn't among them: what's in a
// directory that came or went comes or goes with it.
static size_t top(const wentry **es, size_t n, const wsnap *in_set_of, const wsnap *not_in) {
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		char *p = parent(es[i]->rel);
		bool keep = !p || !wsnap_find(in_set_of, p) || wsnap_find(not_in, p);
		free(p);
		if (keep) es[k++] = es[i];
	}
	return k;
}

static void push(watch_list_Event *l, size_t *cap, watch_Op op, const char *path, const char *old, bool dir) {
	if (l->len == *cap) {
		*cap = *cap ? *cap * 2 : 16;
		l->data = realloc(l->data, *cap * sizeof *l->data);
	}
	l->data[l->len++] = (watch_Event){op, asuka_strdup(path), asuka_strdup(old ? old : ""), dir};
}

static watch_list_Event diff(const wsnap *old, const wsnap *now, bool attributes) {
	watch_list_Event out = {0};
	size_t cap = 0;
	const wentry **gone = malloc((old->n + 1) * sizeof *gone), **came = malloc((now->n + 1) * sizeof *came);
	size_t ng = 0, nc = 0;
	for (size_t i = 0; i < old->n; i++)
		if (!wsnap_find(now, old->e[i].rel)) gone[ng++] = &old->e[i];
	for (size_t i = 0; i < now->n; i++)
		if (!wsnap_find(old, now->e[i].rel)) came[nc++] = &now->e[i];
	qsort(gone, ng, sizeof *gone, by_path);
	qsort(came, nc, sizeof *came, by_path);
	ng = top(gone, ng, old, now);
	nc = top(came, nc, now, old);
	bool *paired = calloc(nc + 1, 1);
	for (size_t i = 0; i < ng; i++) {
		const wentry *g = gone[i];
		ptrdiff_t to = -1;
		for (size_t k = 0; g->id && k < nc && to < 0; k++)
			if (!paired[k] && came[k]->id == g->id && came[k]->dir == g->dir) to = (ptrdiff_t)k;
		if (to < 0) {
			push(&out, &cap, watch_Op_remove, g->rel, NULL, g->dir);
		} else {
			paired[to] = true;
			push(&out, &cap, watch_Op_rename, came[to]->rel, g->rel, g->dir);
		}
	}
	for (size_t k = 0; k < nc; k++)
		if (!paired[k]) push(&out, &cap, watch_Op_create, came[k]->rel, NULL, came[k]->dir);
	for (size_t i = 0; i < now->n; i++) {
		const wentry *n = &now->e[i], *o = wsnap_find(old, n->rel);
		if (!o) continue;
		if (!n->dir && (n->size != o->size || n->mtime != o->mtime || (n->id && o->id && n->id != o->id)))
			push(&out, &cap, watch_Op_write, n->rel, NULL, false);
		else if (attributes && n->mode != o->mode) push(&out, &cap, watch_Op_attrib, n->rel, NULL, n->dir);
	}
	free(gone);
	free(came);
	free(paired);
	if (!out.data) out.data = malloc(sizeof *out.data);
	return out;
}

// ---- The watcher ----

static void unref(watcher *w) {
	watch_lock(w->mu);
	bool last = --w->refs == 0;
	watch_unlock(w->mu);
	if (!last) return;
	watch_native_free(w);
	wsnap_free(&w->snap);
	watch_list_string_free(&w->ignore);
	for (size_t i = 0; i < w->npending; i++) watch_list_Event_free(&w->pending[i]);
	free(w->pending);
	free(w->root);
	free(w->mu);
	free(w);
}

// deliver sends a batch to follow, or keeps it for follow (w->mu held).
static void deliver(watcher *w, watch_list_Event b) {
	if (b.len == 0) {
		watch_list_Event_free(&b);
		return;
	}
	if (w->call) {
		watch_fn_list_Event_call(w->changed, b);
		watch_list_Event_free(&b);
		return;
	}
	w->pending = realloc(w->pending, (w->npending + 1) * sizeof *w->pending);
	w->pending[w->npending++] = b;
}

// end ends follow, answering its call (w->mu held): the call's ref goes
// after, returned.
static asuka_call *end(watcher *w) {
	w->ended = true;
	asuka_call *c = w->call;
	w->call = NULL;
	return c;
}

void watch_signal(watcher *w, bool overflow) {
	uint64_t now = watch_now_ms();
	watch_lock(w->mu);
	if (!w->dirty) w->first = now;
	w->dirty = true;
	w->last = now;
	w->overflow = w->overflow || overflow;
	watch_unlock(w->mu);
}

bool watch_stopping(watcher *w) {
	watch_lock(w->mu);
	bool s = w->stop;
	watch_unlock(w->mu);
	return s;
}

int watch_timeout(watcher *w) {
	watch_lock(w->mu);
	int t = -1;
	if (w->dirty) {
		uint64_t due = w->last + debounce, cap = w->first + max_delay, now = watch_now_ms();
		if (cap < due) due = cap;
		t = due > now ? (int)(due - now) : 0;
	}
	watch_unlock(w->mu);
	return t;
}

bool watch_settle(watcher *w) {
	if (watch_timeout(w) != 0) return true;
	watch_lock(w->mu);
	bool overflow = w->overflow;
	w->dirty = w->overflow = false;
	watch_unlock(w->mu);
	wsnap now = {0};
	bool there = watch_scan(w, &now);
	watch_list_Event b;
	if (!there) {
		// The watched directory (or file) went: a remove of ".", the last.
		b = (watch_list_Event){calloc(1, sizeof(watch_Event)), 0};
		size_t cap = 1;
		push(&b, &cap, watch_Op_remove, ".", NULL, w->root_dir);
	} else {
		b = diff(&w->snap, &now, w->attributes);
		if (overflow) {
			// The system dropped changes: the watch looked at everything.
			b.data = realloc(b.data, (b.len + 1) * sizeof *b.data);
			memmove(b.data + 1, b.data, b.len * sizeof *b.data);
			b.data[0] = (watch_Event){watch_Op_overflow, asuka_strdup("."), asuka_strdup(""), w->root_dir};
			b.len++;
		}
	}
	wsnap_free(&w->snap);
	w->snap = now;
	watch_lock(w->mu);
	deliver(w, b);
	watch_unlock(w->mu);
	return there;
}

void watch_exit(watcher *w) {
	watch_lock(w->mu);
	asuka_call *c = end(w);
	watch_unlock(w->mu);
	if (c) {
		watch_Watcher_follow_done(c);
		unref(w);  // the call's
	}
	unref(w);  // the thread's
}

static char *w_root(watch_Watcher *self, asuka_error *err) { return asuka_strdup(((watcher *)self)->root); }

static void stop_follow(void *p) {
	watcher *w = p;
	watch_lock(w->mu);
	asuka_call *c = w->call;
	w->call = NULL;
	watch_unlock(w->mu);
	if (!c) return;
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(c, &e);
	unref(w);
}

static void w_follow(watch_Watcher *self, watch_fn_list_Event changed, asuka_call *call) {
	watcher *w = (watcher *)self;
	watch_lock(w->mu);
	if (w->ended || w->call) {
		watch_unlock(w->mu);
		if (w->call) {
			asuka_error e = {0};
			asuka_error_failed(&e, "watch", 0, "the watch is followed already");
			asuka_call_fail(call, &e);
		} else {
			watch_Watcher_follow_done(call);  // closed, or its directory went
		}
		return;
	}
	w->refs++;
	w->changed = changed;
	w->call = call;
	for (size_t i = 0; i < w->npending; i++) deliver(w, w->pending[i]);
	w->npending = 0;
	watch_unlock(w->mu);
	asuka_call_on_cancel(call, stop_follow, w);
}

static void w_close(watch_Watcher *self, asuka_error *err) {
	watcher *w = (watcher *)self;
	watch_lock(w->mu);
	bool was = w->stop;
	w->stop = true;
	watch_unlock(w->mu);
	if (!was) watch_native_wake(w);
}

static void w_release(watch_Watcher *self) {
	w_close(self, NULL);
	unref((watcher *)self);
}

static const watch_Watcher_vtable watcher_vt = {.release = w_release, .root = w_root, .follow = w_follow, .close = w_close};

// A Watcher is made by Watching's open, not on its own.
watch_Watcher *watch_make_Watcher(asuka_error *err) {
	asuka_error_unsupported(err, "a watcher comes from Watching.open");
	return NULL;
}

// ---- Opening ----

typedef struct {
	char *path;
	watch_list_string ignore;
	bool recursive, attributes;
	asuka_call *call;
} opening;

#ifdef _WIN32
static char *resolve(const char *path, bool *dir) {
	wchar_t *w = asuka_wide(path);
	HANDLE h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	free(w);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	BY_HANDLE_FILE_INFORMATION fi;
	*dir = GetFileInformationByHandle(h, &fi) && (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	wchar_t buf[32768];
	DWORD n = GetFinalPathNameByHandleW(h, buf, 32768, FILE_NAME_NORMALIZED);
	CloseHandle(h);
	if (!n || n >= 32768) return NULL;
	const wchar_t *p = buf;
	if (wcsncmp(p, L"\\\\?\\UNC\\", 8) == 0) {
		p += 6;
		buf[6] = L'\\';
	} else if (wcsncmp(p, L"\\\\?\\", 4) == 0) {
		p += 4;
	}
	return asuka_narrow(p, -1);
}
#else
#include <limits.h>
#include <sys/stat.h>
static char *resolve(const char *path, bool *dir) {
	char buf[PATH_MAX];
	struct stat st;
	if (!realpath(path, buf) || stat(buf, &st) != 0) return NULL;
	*dir = S_ISDIR(st.st_mode);
	return asuka_strdup(buf);
}
#endif

static void open_run(void *p) {
	opening *o = p;
	asuka_error e = {0};
	bool dir = false;
	char *root = resolve(o->path, &dir);
	if (!root) {
		asuka_error_failed(&e, "watch", 0, "watch: no file or directory at %s", o->path);
		asuka_call_fail(o->call, &e);
		watch_list_string_free(&o->ignore);
	} else {
		watcher *w = calloc(1, sizeof *w);
		w->base.vt = &watcher_vt;
		w->root = root;
		w->root_dir = dir;
		w->recursive = o->recursive;
		w->attributes = o->attributes;
		w->ignore = o->ignore;
		w->mu = watch_mutex_new();
		w->refs = 2;  // the object's, the thread's
		if (!watch_native_start(w, &e)) {
			w->refs = 1;
			unref(w);
			asuka_call_fail(o->call, &e);
		} else {
			watch_Watching_open_done(o->call, &w->base);
		}
	}
	free(o->path);
	free(o);
}

static void open_(watch_Watching *self, const char *path, watch_list_string ignore, bool recursive, bool attributes, asuka_call *call) {
	opening *o = calloc(1, sizeof *o);
	o->path = asuka_strdup(path);
	o->ignore = (watch_list_string){calloc(ignore.len ? ignore.len : 1, sizeof(char *)), ignore.len};
	for (size_t i = 0; i < ignore.len; i++) o->ignore.data[i] = asuka_strdup(ignore.data[i]);
	o->recursive = recursive;
	o->attributes = attributes;
	o->call = call;
	// Listing a large tree takes a while: off the main thread.
	if (!asuka_desktop_spawn(open_run, o)) {
		asuka_error e = {0};
		asuka_error_failed(&e, "watch", 0, "no thread");
		asuka_call_fail(call, &e);
		watch_list_string_free(&o->ignore);
		free(o->path);
		free(o);
	}
}

static const watch_Watching_vtable watching_vt = {.open = open_};

watch_Watching *watch_make_Watching(asuka_error *err) {
	watch_Watching *w = malloc(sizeof *w);
	w->vt = &watching_vt;
	return w;
}
