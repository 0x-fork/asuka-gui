// url.c: std/url's builtins (RFC 3986):
//
//	url.parts(s) -> [string]?     scheme (lower-cased), user, host
//	                              (IPv6 without its brackets), port ("" for
//	                              none), path, query, fragment; none when
//	                              s isn't a URL
//	url.resolveRef(base, ref) -> string   §5.2: ref against base
//	url.escape(s) / url.pathEscape(s) -> string
//	url.unescapeText(s, plus) -> string?  %XX (and + for a space when
//	                                      plus); none for a bad escape
//	url.queryValue(raw, name) -> string?  name's first value in a query
//	url.queryValues(raw, name) -> [string], url.queryPairs(raw) -> [string]
#include "std/std.h"

#define STR(i) ((ak_str *)args[i].p)

typedef struct {
	const char *p;
	size_t n;
	bool set;
} span;

// The parts of a reference (§3), as spans of the text.
typedef struct {
	span scheme, authority, path, query, fragment;
} ref;

static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static int hexval(char c) { return is_digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// split splits s by Appendix B's expression; false when its scheme isn't
// one (a first segment with a colon), or it has control characters,
// spaces or a bad %-escape.
static bool split(const char *s, size_t n, ref *r) {
	memset(r, 0, sizeof *r);
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c <= ' ' || c == 0x7f) return false;
		if (c == '%' && (i + 2 >= n || hexval(s[i + 1]) < 0 || hexval(s[i + 2]) < 0)) return false;
	}
	size_t i = 0;
	while (i < n && !strchr(":/?#", s[i])) i++;
	if (i < n && s[i] == ':') {
		if (i == 0 || !is_alpha(s[0])) return false;
		for (size_t k = 1; k < i; k++)
			if (!is_alpha(s[k]) && !is_digit(s[k]) && !strchr("+-.", s[k])) return false;
		r->scheme = (span){s, i, true};
		i++;
	} else {
		i = 0;
	}
	if (i + 1 < n && s[i] == '/' && s[i + 1] == '/') {
		size_t j = i + 2;
		while (j < n && !strchr("/?#", s[j])) j++;
		r->authority = (span){s + i + 2, j - i - 2, true};
		i = j;
	}
	size_t j = i;
	while (j < n && s[j] != '?' && s[j] != '#') j++;
	r->path = (span){s + i, j - i, true};
	i = j;
	if (i < n && s[i] == '?') {
		j = i + 1;
		while (j < n && s[j] != '#') j++;
		r->query = (span){s + i + 1, j - i - 1, true};
		i = j;
	}
	if (i < n && s[i] == '#') r->fragment = (span){s + i + 1, n - i - 1, true};
	return true;
}

static void push(ak_arr **a, span s) { ak_str_arr_push(a, s.p ? s.p : "", s.n); }

static ak_val parts(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *s = STR(0);
	ref r;
	if (!split(s->data, s->len, &r)) return (ak_val){.p = NULL};
	span user = {0}, host = {0}, port = {0};
	if (r.authority.set) {
		const char *a = r.authority.p;
		size_t an = r.authority.n, i = 0;
		const char *at = NULL;  // the last '@': a user's unescaped '@' stays in it
		for (size_t k = an; k > 0; k--)
			if (a[k - 1] == '@') {
				at = a + k - 1;
				break;
			}
		if (at) {
			user = (span){a, (size_t)(at - a), true};
			i = (size_t)(at - a) + 1;
		}
		if (i < an && a[i] == '[') {
			const char *close = memchr(a + i, ']', an - i);
			if (!close) return (ak_val){.p = NULL};
			host = (span){a + i + 1, (size_t)(close - a - i - 1), true};
			i = (size_t)(close - a) + 1;
			if (i < an && a[i] != ':') return (ak_val){.p = NULL};
		} else {
			size_t j = i;
			while (j < an && a[j] != ':') j++;
			host = (span){a + i, j - i, true};
			i = j;
		}
		if (i < an) {  // ':' port
			port = (span){a + i + 1, an - i - 1, true};
			if (port.n > 5) return (ak_val){.p = NULL};
			long v = 0;
			for (size_t k = 0; k < port.n; k++) {
				if (!is_digit(port.p[k])) return (ak_val){.p = NULL};
				v = v * 10 + (port.p[k] - '0');
			}
			if (v > 65535) return (ak_val){.p = NULL};
		}
	}
	ak_arr *out = ak_str_arr(7);
	ak_str *scheme = ak_str_new(r.scheme.p, r.scheme.n);
	for (uint32_t k = 0; k < scheme->len; k++)
		if (scheme->data[k] >= 'A' && scheme->data[k] <= 'Z') scheme->data[k] += 'a' - 'A';
	ak_arr_push(&out, (ak_val){.p = scheme});
	push(&out, user);
	push(&out, host);
	push(&out, port);
	push(&out, r.path);
	push(&out, r.query);
	push(&out, r.fragment);
	return (ak_val){.p = out};
}

// ---- Resolution (§5.2) ----

// remove_dots appends path to out without its "." and ".." segments
// (§5.2.4).
static void remove_dots(ak_buf *out, const char *p, size_t n) {
	size_t base = out->len;
	size_t i = 0;
	while (i < n) {
		const char *s = p + i;
		size_t left = n - i;
		if (left >= 3 && memcmp(s, "../", 3) == 0) i += 3;
		else if (left >= 2 && memcmp(s, "./", 2) == 0) i += 2;
		else if (left >= 3 && memcmp(s, "/./", 3) == 0) i += 2;
		else if (left == 2 && memcmp(s, "/.", 2) == 0) {
			ak_buf_u8(out, '/');
			i += 2;
		} else if ((left >= 4 && memcmp(s, "/../", 4) == 0) || (left == 3 && memcmp(s, "/..", 3) == 0)) {
			// Drop the last segment written, and its '/'.
			while (out->len > base && out->data[out->len - 1] != '/') out->len--;
			if (out->len > base) out->len--;
			if (left == 3) {
				ak_buf_u8(out, '/');
				i += 3;
			} else {
				i += 3;  // leaves "/" to start the next segment
			}
		} else if ((left == 1 && s[0] == '.') || (left == 2 && memcmp(s, "..", 2) == 0)) {
			i = n;
		} else {
			// The first segment, with its '/'.
			size_t j = s[0] == '/' ? 1 : 0;
			while (j < left && s[j] != '/') j++;
			ak_buf_put(out, s, j);
			i += j;
		}
	}
}

static void put(ak_buf *b, span s) { ak_buf_put(b, s.p, s.n); }

static ak_val resolve(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ref b, r;
	ak_str *bs = STR(0), *rs = STR(1);
	if (!split(rs->data, rs->len, &r)) return (ak_val){.p = ak_retain(rs)};
	if (!split(bs->data, bs->len, &b)) memset(&b, 0, sizeof b);
	ak_buf out = {0}, path = {0};
	span scheme, authority, query;
	if (r.scheme.set) {
		scheme = r.scheme, authority = r.authority, query = r.query;
		remove_dots(&path, r.path.p, r.path.n);
	} else {
		scheme = b.scheme;
		if (r.authority.set) {
			authority = r.authority, query = r.query;
			remove_dots(&path, r.path.p, r.path.n);
		} else {
			authority = b.authority;
			if (r.path.n == 0) {
				ak_buf_put(&path, b.path.p, b.path.n);
				query = r.query.set ? r.query : b.query;
			} else {
				ak_buf merged = {0};
				if (r.path.p[0] == '/') {
					ak_buf_put(&merged, r.path.p, r.path.n);
				} else {
					// §5.2.3: the base's path up to its last '/', then ref's.
					if (b.authority.set && b.path.n == 0) {
						ak_buf_u8(&merged, '/');
					} else {
						size_t k = b.path.n;
						while (k > 0 && b.path.p[k - 1] != '/') k--;
						ak_buf_put(&merged, b.path.p, k);
					}
					ak_buf_put(&merged, r.path.p, r.path.n);
				}
				remove_dots(&path, (const char *)merged.data, merged.len);
				ak_buf_free(&merged);
				query = r.query;
			}
		}
	}
	// §5.3: recomposed.
	if (scheme.set) {
		put(&out, scheme);
		ak_buf_u8(&out, ':');
	}
	if (authority.set) {
		ak_buf_put(&out, "//", 2);
		put(&out, authority);
	}
	ak_buf_put(&out, path.data, path.len);
	if (query.set) {
		ak_buf_u8(&out, '?');
		put(&out, query);
	}
	if (r.fragment.set) {
		ak_buf_u8(&out, '#');
		put(&out, r.fragment);
	}
	ak_str *s = ak_str_new((const char *)out.data, out.len);
	ak_buf_free(&out);
	ak_buf_free(&path);
	return (ak_val){.p = s};
}

// ---- Escaping ----

// What escaping leaves as it is: unreserved characters, and in a path
// its sub-delimiters and '/' (1: both; 2: a path only).
static const uint8_t keep[256] = {
	['-'] = 1, ['_'] = 1, ['.'] = 1, ['~'] = 1,
	['!'] = 2, ['$'] = 2, ['&'] = 2, ['\''] = 2, ['('] = 2, [')'] = 2, ['*'] = 2, ['+'] = 2, [','] = 2, [';'] = 2, ['='] = 2, [':'] = 2, ['@'] = 2, ['/'] = 2,
	['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,
	['A'] = 1, ['B'] = 1, ['C'] = 1, ['D'] = 1, ['E'] = 1, ['F'] = 1, ['G'] = 1, ['H'] = 1, ['I'] = 1, ['J'] = 1, ['K'] = 1, ['L'] = 1, ['M'] = 1,
	['N'] = 1, ['O'] = 1, ['P'] = 1, ['Q'] = 1, ['R'] = 1, ['S'] = 1, ['T'] = 1, ['U'] = 1, ['V'] = 1, ['W'] = 1, ['X'] = 1, ['Y'] = 1, ['Z'] = 1,
	['a'] = 1, ['b'] = 1, ['c'] = 1, ['d'] = 1, ['e'] = 1, ['f'] = 1, ['g'] = 1, ['h'] = 1, ['i'] = 1, ['j'] = 1, ['k'] = 1, ['l'] = 1, ['m'] = 1,
	['n'] = 1, ['o'] = 1, ['p'] = 1, ['q'] = 1, ['r'] = 1, ['s'] = 1, ['t'] = 1, ['u'] = 1, ['v'] = 1, ['w'] = 1, ['x'] = 1, ['y'] = 1, ['z'] = 1,
};

// escape: s with what isn't kept as %XX (a space as + in a query),
// sized first, then written.
static ak_str *escape(const ak_str *s, bool query) {
	static const char hex[] = "0123456789ABCDEF";
	const uint8_t *p = (const uint8_t *)s->data;
	uint8_t mask = query ? 1 : 3;
	size_t len = 0;
	for (uint32_t i = 0; i < s->len; i++) len += (keep[p[i]] & mask) || (p[i] == ' ' && query) ? 1 : 3;
	if (len == s->len) return ak_retain((ak_str *)s);
	ak_str *r = ak_str_new(NULL, len);
	char *o = r->data;
	for (uint32_t i = 0; i < s->len; i++) {
		uint8_t c = p[i];
		if (keep[c] & mask) {
			*o++ = (char)c;
		} else if (c == ' ' && query) {
			*o++ = '+';
		} else {
			o[0] = '%', o[1] = hex[c >> 4], o[2] = hex[c & 15];
			o += 3;
		}
	}
	return r;
}

static ak_val escape_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = escape(STR(0), true)};
}

static ak_val path_escape_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = escape(STR(0), false)};
}

static ak_val unescape_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *s = STR(0);
	bool plus = args[1].i != 0;
	ak_str *r = ak_str_new(NULL, s->len);
	uint32_t o = 0;
	for (uint32_t i = 0; i < s->len; i++) {
		char c = s->data[i];
		if (c == '%') {
			int hi = i + 2 < s->len ? hexval(s->data[i + 1]) : -1, lo = i + 2 < s->len ? hexval(s->data[i + 2]) : -1;
			if (hi < 0 || lo < 0) {
				ak_release(r);
				return (ak_val){.p = NULL};
			}
			r->data[o++] = (char)(hi << 4 | lo);
			i += 2;
		} else if (c == '+' && plus) {
			r->data[o++] = ' ';
		} else {
			r->data[o++] = c;
		}
	}
	r->len = o;
	r->data[o] = 0;
	return (ak_val){.p = r};
}

// ---- Queries ----

// unesc writes s (n bytes) unescaped (%XX, + as a space) into o, which
// has room for n; its length, or -1 for a bad escape.
static int64_t unesc(const char *s, size_t n, char *o) {
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (c == '%') {
			int hi = i + 2 < n ? hexval(s[i + 1]) : -1, lo = i + 2 < n ? hexval(s[i + 2]) : -1;
			if (hi < 0 || lo < 0) return -1;
			o[k++] = (char)(hi << 4 | lo);
			i += 2;
		} else {
			o[k++] = c == '+' ? ' ' : c;
		}
	}
	return (int64_t)k;
}

// next_pair: raw's next name=value pair from *at ('&' between them,
// empty ones skipped; a pair with no '=' has an empty value).
static bool next_pair(const char *raw, size_t n, size_t *at, span *k, span *v) {
	while (*at < n) {
		size_t j = *at;
		const char *amp = memchr(raw + j, '&', n - j);
		size_t end = amp ? (size_t)(amp - raw) : n;
		*at = end + 1;
		if (end == j) continue;
		const char *eq = memchr(raw + j, '=', end - j);
		*k = (span){raw + j, eq ? (size_t)(eq - raw) - j : end - j, true};
		*v = eq ? (span){eq + 1, end - (size_t)(eq + 1 - raw), true} : (span){"", 0, true};
		return true;
	}
	return false;
}

// unesc_str: s unescaped, a string; NULL for a bad escape.
static ak_str *unesc_str(span s) {
	ak_str *r = ak_str_new(NULL, s.n);
	int64_t k = unesc(s.p, s.n, r->data);
	if (k < 0) {
		ak_release(r);
		return NULL;
	}
	r->len = (uint32_t)k;
	r->data[k] = 0;
	return r;
}

// key_is: whether a pair's name, unescaped, is name (false when it has a
// bad escape).
static bool key_is(span k, const ak_str *name) {
	if (!memchr(k.p, '%', k.n) && !memchr(k.p, '+', k.n)) return k.n == name->len && memcmp(k.p, name->data, k.n) == 0;
	if (k.n < name->len) return false;
	char small[256], *buf = k.n <= sizeof small ? small : ak_alloc(k.n);
	int64_t len = unesc(k.p, k.n, buf);
	bool eq = len == (int64_t)name->len && memcmp(buf, name->data, (size_t)len) == 0;
	if (buf != small) ak_free(buf);
	return eq;
}

// query_value_fn(raw, name): name's first value, unescaped; none when
// it has none (a pair with a bad escape is left out, as parseQuery
// leaves it).
static ak_val query_value_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *raw = STR(0), *name = STR(1);
	size_t at = 0;
	span k, v;
	while (next_pair(raw->data, raw->len, &at, &k, &v)) {
		if (!key_is(k, name)) continue;
		ak_str *r = unesc_str(v);
		if (r) return (ak_val){.p = r};
	}
	return (ak_val){.p = NULL};
}

// query_values_fn(raw, name): every value of name's, in order.
static ak_val query_values_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *raw = STR(0), *name = STR(1);
	ak_arr *out = ak_str_arr(0);
	size_t at = 0;
	span k, v;
	while (next_pair(raw->data, raw->len, &at, &k, &v)) {
		if (!key_is(k, name)) continue;
		ak_str *r = unesc_str(v);
		if (r) ak_arr_push(&out, (ak_val){.p = r});
	}
	return (ak_val){.p = out};
}

// query_pairs_fn(raw): every pair's name and value, unescaped, one
// after the other.
static ak_val query_pairs_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *raw = STR(0);
	ak_arr *out = ak_str_arr(0);
	size_t at = 0;
	span k, v;
	while (next_pair(raw->data, raw->len, &at, &k, &v)) {
		ak_str *ks = unesc_str(k), *vs = ks ? unesc_str(v) : NULL;
		if (!vs) {
			if (ks) ak_release(ks);
			continue;
		}
		ak_arr_push(&out, (ak_val){.p = ks});
		ak_arr_push(&out, (ak_val){.p = vs});
	}
	return (ak_val){.p = out};
}

const ak_lib_entry ak_std_url[] = {
	{"url.queryValue", query_value_fn},
	{"url.queryValues", query_values_fn},
	{"url.queryPairs", query_pairs_fn},
	{"url.parts", parts},
	{"url.resolveRef", resolve},
	{"url.escape", escape_fn},
	{"url.pathEscape", path_escape_fn},
	{"url.unescapeText", unescape_fn},
	{NULL, NULL},
};
