// url.c: asuka/url's builtins (RFC 3986):
//
//	url.parts(s) -> [string]?     scheme (lower-cased), user, host
//	                              (IPv6 without its brackets), port ("" for
//	                              none), path, query, fragment; none when
//	                              s isn't a URL
//	url.resolveRef(base, ref) -> string   §5.2: ref against base
//	url.escape(s) / url.pathEscape(s) -> string
//	url.unescapeText(s, plus) -> string?  %XX (and + for a space when
//	                                      plus); none for a bad escape
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

static ak_str *escape(const ak_str *s, bool query) {
	static const char hex[] = "0123456789ABCDEF";
	ak_buf b = {0};
	for (uint32_t i = 0; i < s->len; i++) {
		char c = s->data[i];
		if (c && (is_alpha(c) || is_digit(c) || strchr("-_.~", c))) {
			ak_buf_u8(&b, (uint8_t)c);
		} else if (c == ' ' && query) {
			ak_buf_u8(&b, '+');
		} else if (!query && c != 0 && strchr("!$&'()*+,;=:@/", c)) {
			ak_buf_u8(&b, (uint8_t)c);  // a path's sub-delimiters and '/' stay
		} else {
			uint8_t u = (uint8_t)c;
			ak_buf_u8(&b, '%');
			ak_buf_u8(&b, (uint8_t)hex[u >> 4]);
			ak_buf_u8(&b, (uint8_t)hex[u & 15]);
		}
	}
	ak_str *r = ak_str_new((const char *)b.data, b.len);
	ak_buf_free(&b);
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

const ak_lib_entry ak_std_url[] = {
	{"url.parts", parts},
	{"url.resolveRef", resolve},
	{"url.escape", escape_fn},
	{"url.pathEscape", path_escape_fn},
	{"url.unescapeText", unescape_fn},
	{NULL, NULL},
};
