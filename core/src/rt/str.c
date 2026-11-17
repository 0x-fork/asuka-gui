#include "rt/rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

ak_str *ak_str_new(const char *s, size_t n) {
	ak_str *o = ak_obj_new(AK_TAG_STR, sizeof(ak_str) + n + 1);
	o->len = (uint32_t)n;
	if (n && s) memcpy(o->data, s, n);  // s NULL: the caller fills them
	o->data[n] = 0;
	return o;
}

ak_str *ak_str_empty(void) {
	static void *e;
	ak_str *s = ak_constant_get(&e);
	return s ? s : ak_constant_put(&e, ak_str_new("", 0));
}

ak_str *ak_str_concat(ak_str *a, ak_str *b) {
	if (a->len == 0) return ak_retain(b);
	if (b->len == 0) return ak_retain(a);
	ak_str *o = ak_obj_new(AK_TAG_STR, sizeof(ak_str) + a->len + b->len + 1);
	o->len = a->len + b->len;
	memcpy(o->data, a->data, a->len);
	memcpy(o->data + a->len, b->data, b->len);
	o->data[o->len] = 0;
	return o;
}

ak_str *ak_str_from_int(int64_t v) {
	char buf[24];
	int n = snprintf(buf, sizeof buf, "%lld", (long long)v);
	return ak_str_new(buf, (size_t)n);
}

// fmt_float writes the shortest form of v that reads back as v, as
// JavaScript prints numbers (ECMAScript's Number::toString): the fewest
// significant digits that round-trip, then plain digits for magnitudes
// from 1e-6 to below 1e21 (an integer without a fraction), an exponent
// otherwise.
static int fmt_float(char *buf, size_t cap, double v) {
	if (v != v) return snprintf(buf, cap, "NaN");
	if (v == INFINITY) return snprintf(buf, cap, "Infinity");
	if (v == -INFINITY) return snprintf(buf, cap, "-Infinity");
	if (v == 0) return snprintf(buf, cap, "0");
	// The shortest digits: d.ddde±x with as few as read back as v.
	char e[40];
	for (int p = 0; p < 17; p++) {
		snprintf(e, sizeof e, "%.*e", p, fabs(v));
		if (strtod(e, NULL) == fabs(v)) break;
	}
	char digits[24];
	int k = 0;
	const char *c = e;
	for (; *c && *c != 'e'; c++)
		if (*c >= '0' && *c <= '9') digits[k++] = *c;
	while (k > 1 && digits[k - 1] == '0') k--;
	// v is 0.d1..dk × 10^n.
	int n = atoi(c + 1) + 1;
	char out[48];
	size_t o = 0;
	if (v < 0) out[o++] = '-';
	if (k <= n && n <= 21) {
		memcpy(out + o, digits, (size_t)k);
		o += (size_t)k;
		for (int i = k; i < n; i++) out[o++] = '0';
	} else if (0 < n && n <= 21) {
		memcpy(out + o, digits, (size_t)n);
		o += (size_t)n;
		out[o++] = '.';
		memcpy(out + o, digits + n, (size_t)(k - n));
		o += (size_t)(k - n);
	} else if (-6 < n && n <= 0) {
		out[o++] = '0';
		out[o++] = '.';
		for (int i = n; i < 0; i++) out[o++] = '0';
		memcpy(out + o, digits, (size_t)k);
		o += (size_t)k;
	} else {
		out[o++] = digits[0];
		if (k > 1) {
			out[o++] = '.';
			memcpy(out + o, digits + 1, (size_t)(k - 1));
			o += (size_t)(k - 1);
		}
		o += (size_t)snprintf(out + o, sizeof out - o, "e%c%d", n - 1 < 0 ? '-' : '+', abs(n - 1));
	}
	out[o] = 0;
	return snprintf(buf, cap, "%s", out);
}

ak_str *ak_str_from_float(double v) {
	char buf[48];
	int n = fmt_float(buf, sizeof buf, v);
	return ak_str_new(buf, (size_t)n);
}

ak_str *ak_str_from_bool(bool v) {
	static void *t, *f;
	void **slot = v ? &t : &f;
	ak_str *s = ak_constant_get(slot);
	return s ? s : ak_constant_put(slot, ak_str_c(v ? "true" : "false"));
}

bool ak_str_eq(const ak_str *a, const ak_str *b) {
	if (a == b) return true;
	if (!a || !b || a->len != b->len) return false;
	if (a->hash && b->hash && a->hash != b->hash) return false;
	return memcmp(a->data, b->data, a->len) == 0;
}

int ak_str_cmp(const ak_str *a, const ak_str *b) {
	uint32_t n = a->len < b->len ? a->len : b->len;
	int c = memcmp(a->data, b->data, n);
	if (c) return c < 0 ? -1 : 1;
	return a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
}

uint32_t ak_str_hash(ak_str *s) {
	if (s->hash) return s->hash;
	uint32_t h = 2166136261u;  // FNV-1a
	for (uint32_t i = 0; i < s->len; i++) {
		h ^= (uint8_t)s->data[i];
		h *= 16777619u;
	}
	if (h == 0) h = 1;
	// Immortal strings (constants) may be read from worker threads: their
	// hash is computed once, as they become immortal.
	if (s->h.rc != AK_RC_IMMORTAL) s->hash = h;
	return h;
}

ak_str *ak_str_slice(ak_str *s, int64_t from, int64_t to) {
	if (from < 0) from = 0;
	if (to > s->len) to = s->len;
	if (from >= to) return ak_str_empty();
	if (from == 0 && to == s->len) return ak_retain(s);
	return ak_str_new(s->data + from, (size_t)(to - from));
}

int64_t ak_str_count(const ak_str *s) {
	int64_t n = 0;
	for (uint32_t i = 0; i < s->len; i++)
		if (((uint8_t)s->data[i] & 0xc0) != 0x80) n++;
	return n;
}

int64_t ak_str_find(const ak_str *s, const ak_str *sub) {
	if (sub->len == 0) return 0;
	if (sub->len > s->len) return -1;
	for (uint32_t i = 0; i + sub->len <= s->len; i++)
		if (s->data[i] == sub->data[0] && memcmp(s->data + i, sub->data, sub->len) == 0) return i;
	return -1;
}

bool ak_str_has_prefix(const ak_str *s, const ak_str *p) {
	return p->len <= s->len && memcmp(s->data, p->data, p->len) == 0;
}

bool ak_str_has_suffix(const ak_str *s, const ak_str *p) {
	return p->len <= s->len && memcmp(s->data + s->len - p->len, p->data, p->len) == 0;
}

bool ak_str_contains(const ak_str *s, const ak_str *sub) { return ak_str_find(s, sub) >= 0; }

static ak_str *map_ascii(ak_str *s, bool upper) {
	bool change = false;
	for (uint32_t i = 0; i < s->len && !change; i++) {
		char c = s->data[i];
		change = upper ? (c >= 'a' && c <= 'z') : (c >= 'A' && c <= 'Z');
	}
	if (!change) return ak_retain(s);
	ak_str *o = ak_str_new(s->data, s->len);
	for (uint32_t i = 0; i < o->len; i++) {
		char c = o->data[i];
		if (upper && c >= 'a' && c <= 'z') o->data[i] = (char)(c - 32);
		if (!upper && c >= 'A' && c <= 'Z') o->data[i] = (char)(c + 32);
	}
	return o;
}

// Case mapping beyond ASCII needs the locale's rules (Turkish i, German
// ß), which the platform has: the i18n runtime overrides these.
ak_str *ak_str_lower(ak_str *s) { return map_ascii(s, false); }
ak_str *ak_str_upper(ak_str *s) { return map_ascii(s, true); }

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

ak_str *ak_str_trim(ak_str *s) {
	uint32_t a = 0, b = s->len;
	while (a < b && is_space(s->data[a])) a++;
	while (b > a && is_space(s->data[b - 1])) b--;
	return ak_str_slice(s, a, b);
}

ak_str *ak_str_replace(ak_str *s, const ak_str *old, const ak_str *new_) {
	if (old->len == 0 || ak_str_find(s, old) < 0) return ak_retain(s);
	ak_strb b = {0};
	uint32_t i = 0;
	while (i < s->len) {
		if (i + old->len <= s->len && memcmp(s->data + i, old->data, old->len) == 0) {
			ak_strb_str(&b, new_);
			i += old->len;
		} else {
			ak_strb_cstr(&b, s->data + i, 1);
			i++;
		}
	}
	return ak_strb_finish(&b);
}

ak_arr *ak_str_split(ak_str *s, const ak_str *sep) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 4);
	a->h.flags = 1;
	if (sep->len == 0) {
		// Split into characters.
		uint32_t i = 0;
		while (i < s->len) {
			uint32_t j = i + 1;
			while (j < s->len && ((uint8_t)s->data[j] & 0xc0) == 0x80) j++;
			ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + i, j - i)});
			i = j;
		}
		return a;
	}
	uint32_t start = 0;
	for (uint32_t i = 0; i + sep->len <= s->len;) {
		if (memcmp(s->data + i, sep->data, sep->len) == 0) {
			ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + start, i - start)});
			i += sep->len;
			start = i;
		} else {
			i++;
		}
	}
	ak_arr_push(&a, (ak_val){.p = ak_str_new(s->data + start, s->len - start)});
	return a;
}

bool ak_str_to_int(const ak_str *s, int64_t *out) {
	if (s->len == 0 || s->len > 30) return false;
	char *end;
	errno = 0;
	long long v = strtoll(s->data, &end, 10);
	if (errno || end != s->data + s->len) return false;
	*out = v;
	return true;
}

bool ak_str_to_float(const ak_str *s, double *out) {
	if (s->len == 0) return false;
	char *end;
	double v = strtod(s->data, &end);
	if (end != s->data + s->len) return false;
	*out = v;
	return true;
}

void ak_strb_str(ak_strb *b, const ak_str *s) { ak_buf_put(&b->b, s->data, s->len); }
void ak_strb_cstr(ak_strb *b, const char *s, size_t n) { ak_buf_put(&b->b, s, n); }

void ak_strb_int(ak_strb *b, int64_t v) {
	char buf[24];
	int n = snprintf(buf, sizeof buf, "%lld", (long long)v);
	ak_buf_put(&b->b, buf, (size_t)n);
}

void ak_strb_float(ak_strb *b, double v) {
	char buf[48];
	int n = fmt_float(buf, sizeof buf, v);
	ak_buf_put(&b->b, buf, (size_t)n);
}

void ak_strb_bool(ak_strb *b, bool v) {
	if (v) ak_buf_put(&b->b, "true", 4);
	else ak_buf_put(&b->b, "false", 5);
}

ak_str *ak_strb_finish(ak_strb *b) {
	ak_str *s = b->b.len ? ak_str_new((const char *)b->b.data, b->b.len) : ak_str_empty();
	ak_buf_free(&b->b);
	return s;
}

ak_bytes *ak_bytes_new(const void *p, size_t n) {
	ak_bytes *o = ak_obj_new(AK_TAG_BYTES, sizeof(ak_bytes) + n);
	o->len = (uint32_t)n;
	if (n && p) memcpy(o->data, p, n);  // p NULL: the caller fills them
	return o;
}
