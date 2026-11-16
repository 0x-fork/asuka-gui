// util.c: memory, strings, the maps (host.h).
#include "host.h"

#include <stdarg.h>

void *wh_alloc(size_t n) {
	void *p = calloc(1, n ? n : 1);
	if (!p) {
		fputs("asuka: out of memory\n", stderr);
		abort();
	}
	return p;
}

void *wh_realloc(void *p, size_t n) {
	void *q = realloc(p, n ? n : 1);
	if (!q) {
		fputs("asuka: out of memory\n", stderr);
		abort();
	}
	return q;
}

char *wh_strdup(const char *s) { return s ? wh_strndup(s, strlen(s)) : NULL; }

char *wh_strndup(const char *s, size_t n) {
	char *d = wh_alloc(n + 1);
	if (n) memcpy(d, s, n);
	return d;
}

wchar_t *wh_wide(const char *utf8, int n) {
	if (!utf8) n = 0;
	int len = n > 0 ? MultiByteToWideChar(CP_UTF8, 0, utf8, n, NULL, 0) : 0;
	wchar_t *w = wh_alloc(((size_t)len + 1) * sizeof *w);
	if (len > 0) MultiByteToWideChar(CP_UTF8, 0, utf8, n, w, len);
	w[len] = 0;
	return w;
}

char *wh_utf8(const wchar_t *w, int n, int *outlen) {
	if (!w) n = 0;
	if (n < 0) n = (int)wcslen(w);
	int len = n > 0 ? WideCharToMultiByte(CP_UTF8, 0, w, n, NULL, 0, NULL, NULL) : 0;
	char *s = wh_alloc((size_t)len + 1);
	if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, s, len, NULL, NULL);
	s[len] = 0;
	if (outlen) *outlen = len;
	return s;
}

static void wstr_grow(wstr *s, int n) {
	if (n + 1 > s->cap || !s->p) {
		int c = s->cap ? s->cap : 16;
		while (c < n + 1) c *= 2;
		s->p = wh_realloc(s->p, (size_t)c * sizeof(wchar_t));
		s->cap = c;
	}
}

void wstr_set(wstr *s, ak_span u) {
	int n = u.len ? MultiByteToWideChar(CP_UTF8, 0, u.p, (int)u.len, NULL, 0) : 0;
	wstr_grow(s, n);
	if (n) MultiByteToWideChar(CP_UTF8, 0, u.p, (int)u.len, s->p, n);
	s->p[n] = 0;
	s->n = n;
}

void wstr_setw(wstr *s, const wchar_t *w, int n) {
	if (n < 0) n = w ? (int)wcslen(w) : 0;
	wstr_grow(s, n);
	if (n) memcpy(s->p, w, (size_t)n * sizeof(wchar_t));
	s->p[n] = 0;
	s->n = n;
}

void wstr_free(wstr *s) {
	free(s->p);
	memset(s, 0, sizeof *s);
}

// ---- The map ----

static uint32_t slot(const wh_map *m, uint64_t k) {
	uint64_t h = k * 0x9E3779B97F4A7C15ULL;
	return (uint32_t)(h >> 32) & (m->cap - 1);
}

// Keys are never 0 (no node has ID 0, no window handle is NULL); a slot
// whose key is 0 is empty, ~0 a deleted one's.
#define TOMB UINT64_MAX

void *wh_map_get(const wh_map *m, uint64_t k) {
	if (!m->cap || k == 0) return NULL;
	for (uint32_t i = slot(m, k);; i = (i + 1) & (m->cap - 1)) {
		if (m->keys[i] == k) return m->vals[i];
		if (m->keys[i] == 0) return NULL;
	}
}

static void rehash(wh_map *m, uint32_t cap) {
	uint64_t *ok = m->keys;
	void **ov = m->vals;
	uint32_t oc = m->cap;
	m->keys = wh_alloc(cap * sizeof *m->keys);
	m->vals = wh_alloc(cap * sizeof *m->vals);
	m->cap = cap;
	m->len = 0;
	for (uint32_t i = 0; i < oc; i++)
		if (ok[i] && ok[i] != TOMB) wh_map_put(m, ok[i], ov[i]);
	free(ok);
	free(ov);
}

void wh_map_put(wh_map *m, uint64_t k, void *v) {
	if (k == 0 || k == TOMB) return;
	if ((m->len + 1) * 4 >= m->cap * 3) rehash(m, m->cap ? m->cap * 2 : 64);
	uint32_t tomb = UINT32_MAX;
	for (uint32_t i = slot(m, k);; i = (i + 1) & (m->cap - 1)) {
		if (m->keys[i] == k) {
			m->vals[i] = v;
			return;
		}
		if (m->keys[i] == TOMB && tomb == UINT32_MAX) tomb = i;
		if (m->keys[i] == 0) {
			if (tomb != UINT32_MAX) i = tomb;
			m->keys[i] = k;
			m->vals[i] = v;
			m->len++;
			return;
		}
	}
}

void wh_map_del(wh_map *m, uint64_t k) {
	if (!m->cap || k == 0) return;
	for (uint32_t i = slot(m, k);; i = (i + 1) & (m->cap - 1)) {
		if (m->keys[i] == k) {
			m->keys[i] = TOMB;
			m->vals[i] = NULL;
			m->len--;
			return;
		}
		if (m->keys[i] == 0) return;
	}
}

bool wh_map_next(const wh_map *m, uint32_t *i, uint64_t *k, void **v) {
	for (; *i < m->cap; (*i)++) {
		if (m->keys[*i] && m->keys[*i] != TOMB) {
			if (k) *k = m->keys[*i];
			if (v) *v = m->vals[*i];
			(*i)++;
			return true;
		}
	}
	return false;
}

// ---- Logging ----

void wh_log(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fputs("asuka: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	fflush(stderr);
	va_end(ap);
}

void wh_fail(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fputs("asuka: the Windows host ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	fflush(stderr);
	va_end(ap);
	abort();
}

// ---- The wire's little-endian values ----

float wh_f32(const uint8_t *b) {
	uint32_t u = wh_u32(b);
	float f;
	memcpy(&f, &u, 4);
	return f;
}

uint32_t wh_u32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

uint64_t wh_u64(const uint8_t *b) { return (uint64_t)wh_u32(b) | (uint64_t)wh_u32(b + 4) << 32; }

uint32_t wh_decode_strings(wchar_t ***out, const uint8_t *b, size_t n) {
	*out = NULL;
	if (n < 4) return 0;
	uint32_t count = wh_u32(b);
	size_t at = 4;
	if (count > n / 4) count = (uint32_t)(n / 4);
	wchar_t **v = wh_alloc((count ? count : 1) * sizeof *v);
	uint32_t got = 0;
	for (; got < count; got++) {
		if (at + 4 > n) break;
		uint32_t l = wh_u32(b + at);
		at += 4;
		if (at + l > n) break;
		v[got] = wh_wide((const char *)b + at, (int)l);
		at += l;
	}
	*out = v;
	return got;
}

void wh_free_strings(wchar_t **s, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) free(s[i]);
	free(s);
}

uint32_t wh_decode_cstrings(char ***out, const uint8_t *b, size_t n) {
	*out = NULL;
	if (n < 4) return 0;
	uint32_t count = wh_u32(b);
	size_t at = 4;
	if (count > n / 4) count = (uint32_t)(n / 4);
	char **v = wh_alloc((count ? count : 1) * sizeof *v);
	uint32_t got = 0;
	for (; got < count; got++) {
		if (at + 4 > n) break;
		uint32_t l = wh_u32(b + at);
		at += 4;
		if (at + l > n) break;
		v[got] = wh_strndup((const char *)b + at, l);
		at += l;
	}
	*out = v;
	return got;
}

void wh_free_cstrings(char **s, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) free(s[i]);
	free(s);
}

// ---- Actions ----

static uint32_t decode_level(action **out, const ak_action *items, uint32_t n, uint32_t at, uint32_t count) {
	action *v = wh_alloc((count ? count : 1) * sizeof *v);
	uint32_t i = at;
	for (uint32_t k = 0; k < count && i < n; k++) {
		const ak_action *a = &items[i];
		v[k].role = a->role;
		v[k].title = wh_wspan(a->title);
		v[k].icon = a->icon.len ? wh_strndup(a->icon.p, a->icon.len) : NULL;
		v[k].index = a->index;
		v[k].flags = a->flags;
		i++;
		if (a->flags & AK_MENU_SUBMENU) {
			v[k].count = a->count;
			i = decode_level(&v[k].items, items, n, i, a->count);
		}
	}
	*out = v;
	return i;
}

void actions_decode(actions *a, const uint8_t *b, size_t n) {
	actions_free(a);
	ak_action_vec v = {0};
	ak_decode_actions(b, n, &v);
	uint32_t top = 0;
	for (uint32_t i = 0; i < v.len; i++)
		if (v.data[i].depth == 0) top++;
	if (top) decode_level(&a->v, v.data, v.len, 0, top);
	a->n = top;
	ak_vec_free(&v);
}

static void free_level(action *v, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) {
		free(v[i].title);
		free(v[i].icon);
		free_level(v[i].items, v[i].count);
	}
	free(v);
}

void actions_free(actions *a) {
	if (a->v) free_level(a->v, a->n);
	a->v = NULL;
	a->n = 0;
}

actions actions_flat(const actions *a) {
	actions out = {0};
	if (!a->n) return out;
	out.v = wh_alloc(a->n * sizeof *out.v);
	out.n = a->n;
	for (uint32_t i = 0; i < a->n; i++) {
		out.v[i].role = a->v[i].role;
		size_t l = wcslen(a->v[i].title);
		out.v[i].title = wh_alloc((l + 1) * sizeof(wchar_t));
		memcpy(out.v[i].title, a->v[i].title, l * sizeof(wchar_t));
		out.v[i].icon = wh_strdup(a->v[i].icon);
		out.v[i].index = i;
	}
	return out;
}
