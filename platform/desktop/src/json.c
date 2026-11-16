// json.c (platform/desktop/src): asuka_desktop.h's JSON helpers, for
// packages' C that reads and writes the JSON a platform API speaks
// (WebView2's DevTools, WinRT's, a portal's): a key's raw value, a
// string decoded, a string quoted.
#include "asuka_desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ws(const char *p) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

// skip is past the value at p (NULL: malformed).
static const char *skip(const char *p) {
	p = ws(p);
	if (*p == '"') {
		for (p++; *p && *p != '"'; p++)
			if (*p == '\\' && p[1]) p++;
		return *p ? p + 1 : NULL;
	}
	if (*p == '{' || *p == '[') {
		char close = *p == '{' ? '}' : ']';
		p = ws(p + 1);
		if (*p == close) return p + 1;
		for (;;) {
			if (close == '}') {
				p = skip(p);  // the key
				if (!p) return NULL;
				p = ws(p);
				if (*p != ':') return NULL;
				p++;
			}
			p = skip(p);
			if (!p) return NULL;
			p = ws(p);
			if (*p == ',') {
				p = ws(p + 1);
				continue;
			}
			return *p == close ? p + 1 : NULL;
		}
	}
	const char *s = p;
	while (*p && !strchr(",}] \t\r\n", *p)) p++;
	return p > s ? p : NULL;
}

char *asuka_json_get(const char *json, const char *key) {
	if (!json) return NULL;
	const char *p = ws(json);
	if (*p != '{') return NULL;
	p = ws(p + 1);
	size_t kn = strlen(key);
	while (*p == '"') {
		const char *k = p + 1;
		const char *ke = skip(p);
		if (!ke) return NULL;
		bool match = (size_t)(ke - 1 - k) == kn && strncmp(k, key, kn) == 0;
		p = ws(ke);
		if (*p != ':') return NULL;
		const char *v = ws(p + 1), *ve = skip(v);
		if (!ve) return NULL;
		if (match) return asuka_strndup(v, (size_t)(ve - v));
		p = ws(ve);
		if (*p != ',') return NULL;
		p = ws(p + 1);
	}
	return NULL;
}

static void utf8(asuka_writer *w, unsigned c) {
	uint8_t b[4];
	size_t n;
	if (c < 0x80) b[0] = (uint8_t)c, n = 1;
	else if (c < 0x800) b[0] = (uint8_t)(0xC0 | c >> 6), b[1] = (uint8_t)(0x80 | (c & 0x3F)), n = 2;
	else if (c < 0x10000) b[0] = (uint8_t)(0xE0 | c >> 12), b[1] = (uint8_t)(0x80 | (c >> 6 & 0x3F)), b[2] = (uint8_t)(0x80 | (c & 0x3F)), n = 3;
	else b[0] = (uint8_t)(0xF0 | c >> 18), b[1] = (uint8_t)(0x80 | (c >> 12 & 0x3F)), b[2] = (uint8_t)(0x80 | (c >> 6 & 0x3F)), b[3] = (uint8_t)(0x80 | (c & 0x3F)), n = 4;
	asuka_writer_put(w, b, n);
}

char *asuka_json_string(const char *raw) {
	if (!raw) return NULL;
	const char *p = ws(raw);
	if (*p != '"') return NULL;
	asuka_writer w = {0};
	for (p++; *p && *p != '"'; p++) {
		if (*p != '\\') {
			asuka_writer_u8(&w, (uint8_t)*p);
			continue;
		}
		p++;
		switch (*p) {
		case 'n': asuka_writer_u8(&w, '\n'); break;
		case 'r': asuka_writer_u8(&w, '\r'); break;
		case 't': asuka_writer_u8(&w, '\t'); break;
		case 'b': asuka_writer_u8(&w, '\b'); break;
		case 'f': asuka_writer_u8(&w, '\f'); break;
		case 'u': {
			unsigned c = 0;
			if (sscanf(p + 1, "%4x", &c) != 1) break;
			p += 4;
			if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
				unsigned lo = 0;
				if (sscanf(p + 3, "%4x", &lo) == 1 && lo >= 0xDC00 && lo < 0xE000) {
					c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
					p += 6;
				}
			}
			utf8(&w, c);
			break;
		}
		case 0: p--; break;
		default: asuka_writer_u8(&w, (uint8_t)*p);
		}
	}
	asuka_writer_u8(&w, 0);
	return (char *)w.data;
}

void asuka_json_quote(asuka_writer *w, const char *s) {
	asuka_writer_u8(w, '"');
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		switch (*p) {
		case '"': asuka_writer_put(w, "\\\"", 2); break;
		case '\\': asuka_writer_put(w, "\\\\", 2); break;
		case '\n': asuka_writer_put(w, "\\n", 2); break;
		case '\r': asuka_writer_put(w, "\\r", 2); break;
		case '\t': asuka_writer_put(w, "\\t", 2); break;
		default:
			if (*p < 0x20) {
				char e[8];
				snprintf(e, sizeof e, "\\u%04x", *p);
				asuka_writer_put(w, e, 6);
			} else {
				asuka_writer_u8(w, *p);
			}
		}
	}
	asuka_writer_u8(w, '"');
}
