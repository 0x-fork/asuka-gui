// json.c: JSON read into a tree, and written (desktop.h).
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L  // strdup, readlink under -std=c17
#endif
#include "desktop.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const char *p, *end;
	int depth;
} reader;

static void ws(reader *r) {
	while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')) r->p++;
}

static bool value(reader *r, dsk_json *v);

static int hexv(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static bool hex4(reader *r, uint32_t *u) {
	if (r->end - r->p < 4) return false;
	*u = 0;
	for (int i = 0; i < 4; i++) {
		int h = hexv(r->p[i]);
		if (h < 0) return false;
		*u = *u << 4 | (uint32_t)h;
	}
	r->p += 4;
	return true;
}

static size_t utf8(char *o, uint32_t c) {
	if (c < 0x80) return o[0] = (char)c, 1;
	if (c < 0x800) return o[0] = (char)(0xC0 | c >> 6), o[1] = (char)(0x80 | (c & 63)), 2;
	if (c < 0x10000) return o[0] = (char)(0xE0 | c >> 12), o[1] = (char)(0x80 | (c >> 6 & 63)), o[2] = (char)(0x80 | (c & 63)), 3;
	o[0] = (char)(0xF0 | c >> 18), o[1] = (char)(0x80 | (c >> 12 & 63)), o[2] = (char)(0x80 | (c >> 6 & 63)), o[3] = (char)(0x80 | (c & 63));
	return 4;
}

// string reads a string after its quote into a new buffer.
static bool string(reader *r, char **out, size_t *outlen) {
	const char *s = r->p;
	size_t cap = 16;
	while (s < r->end && *s != '"') s += *s == '\\' ? 2 : 1;
	cap += (size_t)(s - r->p);
	char *b = malloc(cap);
	if (!b) return false;
	size_t n = 0;
	while (r->p < r->end && *r->p != '"') {
		char c = *r->p++;
		if ((unsigned char)c < 0x20) goto bad;
		if (c != '\\') {
			b[n++] = c;
			continue;
		}
		if (r->p >= r->end) goto bad;
		c = *r->p++;
		switch (c) {
		case '"': case '\\': case '/': b[n++] = c; break;
		case 'b': b[n++] = '\b'; break;
		case 'f': b[n++] = '\f'; break;
		case 'n': b[n++] = '\n'; break;
		case 'r': b[n++] = '\r'; break;
		case 't': b[n++] = '\t'; break;
		case 'u': {
			uint32_t u, lo;
			if (!hex4(r, &u)) goto bad;
			if (u >= 0xD800 && u < 0xDC00 && r->end - r->p >= 6 && r->p[0] == '\\' && r->p[1] == 'u') {
				reader save = *r;
				r->p += 2;
				if (hex4(r, &lo) && lo >= 0xDC00 && lo < 0xE000) u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				else *r = save, u = 0xFFFD;
			} else if (u >= 0xD800 && u < 0xE000) {
				u = 0xFFFD;
			}
			n += utf8(b + n, u);
			break;
		}
		default: goto bad;
		}
	}
	if (r->p >= r->end) goto bad;
	r->p++;
	b[n] = 0;
	*out = b;
	*outlen = n;
	return true;
bad:
	free(b);
	return false;
}

static bool push(dsk_json *v, dsk_json *kid, char *key, size_t *cap) {
	if (v->n == *cap) {
		size_t c = *cap ? *cap * 2 : 4;
		dsk_json *k = realloc(v->kids, c * sizeof *k);
		if (!k) return false;
		v->kids = k;
		if (v->type == DSK_JOBJ) {
			char **ks = realloc(v->keys, c * sizeof *ks);
			if (!ks) return false;
			v->keys = ks;
		}
		*cap = c;
	}
	v->kids[v->n] = *kid;
	if (v->type == DSK_JOBJ) v->keys[v->n] = key;
	v->n++;
	return true;
}

static void clear(dsk_json *v) {
	free(v->str);
	for (size_t i = 0; i < v->n; i++) {
		clear(&v->kids[i]);
		if (v->keys) free(v->keys[i]);
	}
	free(v->kids);
	free(v->keys);
	memset(v, 0, sizeof *v);
}

static bool value(reader *r, dsk_json *v) {
	memset(v, 0, sizeof *v);
	ws(r);
	if (r->p >= r->end || ++r->depth > 200) return false;
	char c = *r->p;
	bool ok = true;
	if (c == '{' || c == '[') {
		v->type = c == '{' ? DSK_JOBJ : DSK_JARR;
		char close = c == '{' ? '}' : ']';
		size_t cap = 0;
		r->p++;
		ws(r);
		if (r->p < r->end && *r->p == close) {
			r->p++;
		} else {
			for (;;) {
				char *key = NULL;
				size_t kl;
				if (v->type == DSK_JOBJ) {
					ws(r);
					if (r->p >= r->end || *r->p != '"') { ok = false; break; }
					r->p++;
					if (!string(r, &key, &kl)) { ok = false; break; }
					ws(r);
					if (r->p >= r->end || *r->p != ':') { free(key); ok = false; break; }
					r->p++;
				}
				dsk_json kid;
				if (!value(r, &kid) || !push(v, &kid, key, &cap)) {
					clear(&kid);
					free(key);
					ok = false;
					break;
				}
				ws(r);
				if (r->p < r->end && *r->p == ',') { r->p++; continue; }
				if (r->p < r->end && *r->p == close) { r->p++; break; }
				ok = false;
				break;
			}
		}
	} else if (c == '"') {
		r->p++;
		v->type = DSK_JSTR;
		ok = string(r, &v->str, &v->len);
	} else if (r->end - r->p >= 4 && memcmp(r->p, "true", 4) == 0) {
		v->type = DSK_JTRUE, r->p += 4;
	} else if (r->end - r->p >= 5 && memcmp(r->p, "false", 5) == 0) {
		v->type = DSK_JFALSE, r->p += 5;
	} else if (r->end - r->p >= 4 && memcmp(r->p, "null", 4) == 0) {
		v->type = DSK_JNULL, r->p += 4;
	} else if (c == '-' || (c >= '0' && c <= '9')) {
		char buf[64];
		size_t n = 0;
		while (r->p < r->end && n < sizeof buf - 1 && strchr("+-0123456789.eE", *r->p)) buf[n++] = *r->p++;
		buf[n] = 0;
		char *e;
		v->type = DSK_JNUM;
		v->num = strtod(buf, &e);
		ok = e == buf + n && n > 0;
	} else {
		ok = false;
	}
	r->depth--;
	if (!ok) clear(v);
	return ok;
}

dsk_json *dsk_json_parse(const char *text, size_t len) {
	if (!text) return NULL;
	reader r = {text, text + len, 0};
	dsk_json *v = malloc(sizeof *v);
	if (!v) return NULL;
	if (!value(&r, v)) {
		free(v);
		return NULL;
	}
	ws(&r);
	if (r.p != r.end) {
		clear(v);
		free(v);
		return NULL;
	}
	return v;
}

void dsk_json_free(dsk_json *v) {
	if (!v) return;
	clear(v);
	free(v);
}

const dsk_json *dsk_json_get(const dsk_json *v, const char *key) {
	if (!v || v->type != DSK_JOBJ) return NULL;
	for (size_t i = v->n; i-- > 0;)  // the last of a key, as Go's decoder keeps
		if (strcmp(v->keys[i], key) == 0) return &v->kids[i];
	return NULL;
}

const char *dsk_json_str(const dsk_json *v, const char *key, const char *def) {
	const dsk_json *m = dsk_json_get(v, key);
	return m && m->type == DSK_JSTR ? m->str : def;
}

double dsk_json_num(const dsk_json *v, const char *key, double def) {
	const dsk_json *m = dsk_json_get(v, key);
	return m && m->type == DSK_JNUM ? m->num : def;
}

bool dsk_json_bool(const dsk_json *v, const char *key, bool def) {
	const dsk_json *m = dsk_json_get(v, key);
	if (!m) return def;
	if (m->type == DSK_JTRUE) return true;
	if (m->type == DSK_JFALSE) return false;
	return def;
}

// ---- Writing ----

void dsk_jw_raw(dsk_jw *w, const char *s, size_t n) {
	if (w->err) return;
	if (w->len + n + 1 > w->cap) {
		size_t c = w->cap ? w->cap : 64;
		while (c < w->len + n + 1) c *= 2;
		char *p = realloc(w->p, c);
		if (!p) {
			w->err = true;
			return;
		}
		w->p = p, w->cap = c;
	}
	memcpy(w->p + w->len, s, n);
	w->len += n;
	w->p[w->len] = 0;
}

static void sep(dsk_jw *w) {
	if (w->comma) dsk_jw_raw(w, ",", 1);
	w->comma = true;
}

void dsk_jw_json(dsk_jw *w, const char *json, size_t n) {
	sep(w);
	dsk_jw_raw(w, json, n);
}

void dsk_jw_obj(dsk_jw *w) { sep(w), dsk_jw_raw(w, "{", 1), w->comma = false; }
void dsk_jw_end(dsk_jw *w) { dsk_jw_raw(w, "}", 1), w->comma = true; }
void dsk_jw_arr(dsk_jw *w) { sep(w), dsk_jw_raw(w, "[", 1), w->comma = false; }
void dsk_jw_aend(dsk_jw *w) { dsk_jw_raw(w, "]", 1), w->comma = true; }

static void quoted(dsk_jw *w, const char *s, size_t n) {
	static const char hexd[] = "0123456789abcdef";
	dsk_jw_raw(w, "\"", 1);
	size_t from = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		const char *esc = NULL;
		char u[6];
		switch (c) {
		case '"': esc = "\\\""; break;
		case '\\': esc = "\\\\"; break;
		case '\n': esc = "\\n"; break;
		case '\r': esc = "\\r"; break;
		case '\t': esc = "\\t"; break;
		case '<': case '>': case '&':  // as Go's encoder escapes them
			u[0] = '\\', u[1] = 'u', u[2] = '0', u[3] = '0', u[4] = hexd[c >> 4], u[5] = hexd[c & 15];
			break;
		default:
			if (c >= 0x20) continue;
			u[0] = '\\', u[1] = 'u', u[2] = '0', u[3] = '0', u[4] = hexd[c >> 4], u[5] = hexd[c & 15];
		}
		dsk_jw_raw(w, s + from, i - from);
		if (esc) dsk_jw_raw(w, esc, strlen(esc));
		else dsk_jw_raw(w, u, 6);
		from = i + 1;
	}
	dsk_jw_raw(w, s + from, n - from);
	dsk_jw_raw(w, "\"", 1);
}

void dsk_jw_key(dsk_jw *w, const char *key) {
	sep(w);
	quoted(w, key, strlen(key));
	dsk_jw_raw(w, ":", 1);
	w->comma = false;
}

void dsk_jw_strn(dsk_jw *w, const char *s, size_t n) {
	sep(w);
	quoted(w, s ? s : "", s ? n : 0);
}

void dsk_jw_str(dsk_jw *w, const char *s) { dsk_jw_strn(w, s, s ? strlen(s) : 0); }

void dsk_jw_num(dsk_jw *w, double v) {
	char b[40];
	if (!isfinite(v)) v = 0;
	if (v == (double)(int64_t)v && fabs(v) < 1e15) snprintf(b, sizeof b, "%lld", (long long)v);
	else snprintf(b, sizeof b, "%.17g", v);
	sep(w);
	dsk_jw_raw(w, b, strlen(b));
}

void dsk_jw_int(dsk_jw *w, int64_t v) {
	char b[24];
	snprintf(b, sizeof b, "%lld", (long long)v);
	sep(w);
	dsk_jw_raw(w, b, strlen(b));
}

void dsk_jw_bool(dsk_jw *w, bool v) {
	sep(w);
	dsk_jw_raw(w, v ? "true" : "false", v ? 4 : 5);
}

void dsk_jw_value(dsk_jw *w, const dsk_json *v) {
	switch (v->type) {
	case DSK_JNULL: sep(w), dsk_jw_raw(w, "null", 4); break;
	case DSK_JFALSE: dsk_jw_bool(w, false); break;
	case DSK_JTRUE: dsk_jw_bool(w, true); break;
	case DSK_JNUM: dsk_jw_num(w, v->num); break;
	case DSK_JSTR: dsk_jw_strn(w, v->str, v->len); break;
	case DSK_JARR:
		dsk_jw_arr(w);
		for (size_t i = 0; i < v->n; i++) dsk_jw_value(w, &v->kids[i]);
		dsk_jw_aend(w);
		break;
	case DSK_JOBJ:
		dsk_jw_obj(w);
		for (size_t i = 0; i < v->n; i++) {
			dsk_jw_key(w, v->keys[i]);
			dsk_jw_value(w, &v->kids[i]);
		}
		dsk_jw_end(w);
		break;
	}
}

char *dsk_jw_take(dsk_jw *w, size_t *len) {
	if (w->err) {
		dsk_jw_free(w);
		return NULL;
	}
	if (!w->p) dsk_jw_raw(w, "", 0);
	char *p = w->p;
	if (len) *len = w->len;
	memset(w, 0, sizeof *w);
	return p;
}

void dsk_jw_free(dsk_jw *w) {
	free(w->p);
	memset(w, 0, sizeof *w);
}
