// asuka/camera on Windows (Media Foundation's capture engine,
// camera_windows.c) and Linux (GStreamer, camera_linux.c); macOS runs
// the package's Swift. What both share. Main thread, unless it says.
#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

struct camera_pending {
	asuka_call *call;
	void (*done)(asuka_call *call, char *value);
	camera_pending **slot;
	void (*cancel)(void *ctx);
	void *ctx;
};

static void cancelled(void *p) {
	camera_pending *w = p;
	if (w->slot) *w->slot = NULL;
	if (w->cancel) w->cancel(w->ctx);
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(w->call, &e);
	free(w);
}

void camera_wait(asuka_call *call, void (*done)(asuka_call *call, char *value), camera_pending **slot, void (*cancel)(void *ctx), void *ctx) {
	camera_pending *w = malloc(sizeof *w);
	*w = (camera_pending){call, done, slot, cancel, ctx};
	*slot = w;
	asuka_call_on_cancel(call, cancelled, w);
}

void camera_move(camera_pending **from, camera_pending **to) {
	camera_pending *w = *from;
	*from = NULL;
	*to = w;
	if (w) w->slot = to;
}

static camera_pending *take(camera_pending **slot) {
	camera_pending *w = *slot;
	*slot = NULL;
	return w;
}

void camera_answer(camera_pending **slot, const char *value) {
	camera_pending *w = take(slot);
	if (!w) return;
	w->done(w->call, asuka_strdup(value));
	free(w);
}

void camera_fail(camera_pending **slot, const char *message) {
	camera_pending *w = take(slot);
	if (!w) return;
	asuka_error e = {0};
	asuka_error_failed(&e, "camera", 0, "%s", message);
	asuka_call_fail(w->call, &e);
	free(w);
}

void camera_fail_declared(camera_pending **slot, uint32_t index) {
	camera_pending *w = take(slot);
	if (!w) return;
	asuka_error e = {0};
	asuka_error_declared(&e, index);
	asuka_call_fail(w->call, &e);
	free(w);
}

void camera_kinds_set(camera_kinds *k, camera_list_string list) {
	camera_kinds_free(k);
	k->kinds = calloc(list.len + 1, sizeof *k->kinds);
	for (size_t i = 0; i < list.len; i++)
		if (list.data[i] && *list.data[i]) k->kinds[k->n++] = asuka_strdup(list.data[i]);
}

bool camera_kinds_wants(const camera_kinds *k, const char *kind) {
	if (!k->n) return true;
	for (size_t i = 0; i < k->n; i++)
		if (!strcmp(k->kinds[i], kind)) return true;
	return false;
}

void camera_kinds_free(camera_kinds *k) {
	for (size_t i = 0; i < k->n; i++) free(k->kinds[i]);
	free(k->kinds);
	*k = (camera_kinds){0};
}

bool camera_seen_new(camera_seen *s, const char *kind, const char *value, int64_t now) {
	char *key = asuka_sprintf("%s%c%s", kind, 1, value);
	bool fresh = true;
	size_t i;
	for (i = 0; i < s->n; i++)
		if (!strcmp(s->seen[i].key, key)) break;
	if (i < s->n) {
		fresh = now - s->seen[i].at > 1000;
		s->seen[i].at = now;
		free(key);
	} else {
		if (s->n == s->cap) {
			s->cap = s->cap ? s->cap * 2 : 8;
			s->seen = realloc(s->seen, s->cap * sizeof *s->seen);
		}
		s->seen[s->n].key = key;
		s->seen[s->n++].at = now;
	}
	if (s->n > 32) {  // forget what's been out of view a while
		size_t j = 0;
		for (size_t k = 0; k < s->n; k++) {
			if (now - s->seen[k].at > 1000) free(s->seen[k].key);
			else s->seen[j++] = s->seen[k];
		}
		s->n = j;
	}
	return fresh;
}

void camera_seen_free(camera_seen *s) {
	for (size_t i = 0; i < s->n; i++) free(s->seen[i].key);
	free(s->seen);
	*s = (camera_seen){0};
}

int64_t camera_now_ms(void) {
#ifdef _WIN32
	return (int64_t)GetTickCount64();
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

// random_name is 24 hex digits, from the system's random source.
static void random_name(char out[25]) {
	unsigned char b[12] = {0};
#ifdef _WIN32
	for (int i = 0; i < 12; i += 4) {
		unsigned int v = 0;
		rand_s(&v);
		memcpy(b + i, &v, 4);
	}
#else
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0 || read(fd, b, sizeof b) != (ssize_t)sizeof b) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		uint64_t v = (uint64_t)ts.tv_nsec ^ ((uint64_t)ts.tv_sec << 20) ^ (uint64_t)getpid() << 40;
		memcpy(b, &v, 8);
	}
	if (fd >= 0) close(fd);
#endif
	for (int i = 0; i < 12; i++) snprintf(out + 2 * i, 3, "%02x", b[i]);
}

char *camera_file(const char *ext) {
	char *dir = asuka_sprintf("%s/asuka-camera", asuka_desktop_cache_dir());
#ifdef _WIN32
	_mkdir(dir);
#else
	mkdir(dir, 0755);
#endif
	char name[25];
	random_name(name);
	char *path = asuka_sprintf("%s/%s%s", dir, name, ext);
	free(dir);
	FILE *f = fopen(path, "wb");  // there, and writable
	if (!f) {
		free(path);
		return NULL;
	}
	fclose(f);
	return path;
}

// lang_is compares a language tag with one of the table's, ignoring case
// and '_' for '-'.
static bool lang_is(const char *a, size_t n, const char *b) {
	size_t i = 0;
	for (; i < n && b[i]; i++) {
		char x = a[i] == '_' ? '-' : a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return false;
	}
	return i == n && !b[i];
}

static const camera_word *word(const char *lang, size_t n) {
	for (const camera_word *w = camera_words; w->lang; w++)
		if (lang_is(lang, n, w->lang)) return w;
	return NULL;
}

static bool has_part(const char *tag, size_t n, const char *part) {
	size_t k = strlen(part);
	for (size_t i = 0; i + k <= n; i++)
		if ((i == 0 || tag[i - 1] == '-' || tag[i - 1] == '_') && (i + k == n || tag[i + k] == '-' || tag[i + k] == '_' || tag[i + k] == '.' || tag[i + k] == '@') &&
		    lang_is(tag + i, k, part))
			return true;
	return false;
}

const camera_word *camera_words_for(const char *const *locales, size_t count) {
	for (size_t i = 0; i < count; i++) {
		const char *l = locales[i];
		if (!l || !*l) continue;
		size_t n = strcspn(l, ".@");  // "de_DE.UTF-8", "sr@latin"
		const camera_word *w = word(l, n);
		if (w) return w;
		size_t base = strcspn(l, "-_.@");
		if (base > n) base = n;
		if (lang_is(l, base, "zh")) {
			bool hant = has_part(l, n, "Hant") || has_part(l, n, "TW") || has_part(l, n, "HK") || has_part(l, n, "MO");
			return word(hant ? "zh-Hant" : "zh-Hans", hant ? 7 : 7);
		}
		if (lang_is(l, base, "no") || lang_is(l, base, "nn")) w = word("nb", 2);
		else if (lang_is(l, base, "tl")) w = word("fil", 3);
		else w = word(l, base);
		if (w) return w;
	}
	const camera_word *en = word("en", 2);
	static const camera_word fallback = {"en", "Take Photo", "Cancel"};
	return en ? en : &fallback;
}
