// str_test.c: strings' fast paths (rt/str.c) against the plain ways
// they replace: searching, replacing, splitting, counting characters,
// the builder, hashing, printing numbers.
#include "t.h"
#include "rt/rt.h"
#include "rt/lib.h"
#include "rt/unicode.h"

#include <float.h>
#include <stdint.h>

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint64_t next(void) {
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

static int64_t naive_find(const char *h, size_t hn, const char *n, size_t nn) {
	if (nn == 0) return 0;
	for (size_t i = 0; i + nn <= hn; i++)
		if (memcmp(h + i, n, nn) == 0) return (int64_t)i;
	return -1;
}

static int64_t naive_find_last(const char *h, size_t hn, const char *n, size_t nn) {
	if (nn > hn) return -1;
	for (size_t i = hn - nn + 1; i-- > 0;)
		if (memcmp(h + i, n, nn) == 0) return (int64_t)i;
	return -1;
}

// A haystack of a few letters, so needles of them are found often (and
// falsely started often).
static void fill(char *s, size_t n, const char *alphabet) {
	size_t k = strlen(alphabet);
	for (size_t i = 0; i < n; i++) s[i] = alphabet[next() % k];
}

TEST(find_as_naive) {
	static const char *alphabets[] = {"ab", "abc", "aaaaab", "etaoin ", "\x01\xff", "a"};
	char h[3000], n[80];
	for (int round = 0; round < 4000; round++) {
		const char *al = alphabets[round % 6];
		size_t hn = next() % sizeof h, nn = 1 + next() % (round % 3 == 0 ? 60 : 8);
		fill(h, hn, al);
		if (round % 2 && hn >= nn) {
			memcpy(n, h + next() % (hn - nn + 1), nn);  // one that's there
		} else {
			fill(n, nn, al);
		}
		int64_t want = naive_find(h, hn, n, nn), got = ak_mem_find(h, hn, n, nn);
		if (got != want) {
			T_FAIL("round %d: find of %zu bytes in %zu: %lld, want %lld", round, nn, hn, (long long)got, (long long)want);
			return;
		}
		CHECK_EQ_INT(ak_mem_find_last(h, hn, n, nn), naive_find_last(h, hn, n, nn));
	}
}

// What makes a rare byte's memchr start falsely at every byte: the
// two-way algorithm takes over, and finds what's there.
TEST(find_falls_back_to_two_way) {
	size_t hn = 1 << 20;
	char *h = malloc(hn + 1);
	memset(h, 'a', hn);
	const char *n = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab";
	CHECK_EQ_INT(ak_mem_find(h, hn, n, strlen(n)), -1);
	memcpy(h + hn - strlen(n), n, strlen(n));
	CHECK_EQ_INT(ak_mem_find(h, hn, n, strlen(n)), (int64_t)(hn - strlen(n)));
	// Periodic needles, and one at the very start.
	const char *p = "abababababababababababababababac";
	for (size_t i = 0; i < hn; i++) h[i] = i % 2 ? 'b' : 'a';
	memcpy(h + 777777, p, strlen(p));
	CHECK_EQ_INT(ak_mem_find(h, hn, p, strlen(p)), naive_find(h, hn, p, strlen(p)));
	CHECK_EQ_INT(ak_mem_find(h, hn, "ab", 2), 0);
	free(h);
}

static ak_str *S(const char *s) { return ak_str_c(s); }

static bool arr_is(ak_arr *a, const char *const *want, uint32_t n) {
	if (a->len != n) return false;
	for (uint32_t i = 0; i < n; i++) {
		ak_str *s = ak_arr_vals(a)[i].p;
		if (s->len != strlen(want[i]) || memcmp(s->data, want[i], s->len)) return false;
	}
	return true;
}

TEST(replace_and_split) {
	ak_str *s = S("a,b,,c,"), *comma = S(","), *dash = S("--"), *empty = ak_str_empty();
	ak_str *r = ak_str_replace(s, comma, dash);
	CHECK_EQ_STR(r->data, "a--b----c--");
	ak_release(r);
	r = ak_str_replace_n(s, comma, dash, 2);
	CHECK_EQ_STR(r->data, "a--b--,c,");
	ak_release(r);
	r = ak_str_replace(s, dash, comma);  // none: the same string
	CHECK(r == s);
	ak_release(r);
	r = ak_str_replace(s, comma, empty);
	CHECK_EQ_STR(r->data, "abc");
	ak_release(r);
	ak_arr *a = ak_str_split(s, comma);
	CHECK(arr_is(a, (const char *[]){"a", "b", "", "c", ""}, 5));
	ak_release(a);
	a = ak_str_split_n(s, comma, 3);
	CHECK(arr_is(a, (const char *[]){"a", "b", ",c,"}, 3));
	ak_release(a);
	a = ak_str_split_n(s, comma, 0);
	CHECK_EQ_INT(a->len, 0);
	ak_release(a);
	ak_str *u = S("añ€😀");
	a = ak_str_split(u, empty);
	CHECK(arr_is(a, (const char *[]){"a", "ñ", "€", "😀"}, 4));
	ak_release(a);
	// Replacing what overlaps itself: left to right, as Go's.
	ak_str *aaa = S("aaaa"), *aa = S("aa"), *b = S("b");
	r = ak_str_replace(aaa, aa, b);
	CHECK_EQ_STR(r->data, "bb");
	ak_release(r);
	// Random ones against a byte-by-byte replace.
	char h[400], o[6], w[6];
	for (int round = 0; round < 2000; round++) {
		size_t hn = next() % sizeof h, on = 1 + next() % 3, wn = next() % 4;
		fill(h, hn, "ab");
		fill(o, on, "ab");
		fill(w, wn, "xy");
		ak_str *hs = ak_str_new(h, hn), *os = ak_str_new(o, on), *ws = ak_str_new(w, wn);
		int64_t count = (int64_t)(next() % 5) - 1;
		ak_strb want = {0};
		int64_t left = count;
		for (size_t i = 0; i < hn;) {
			if (left != 0 && i + on <= hn && memcmp(h + i, o, on) == 0) {
				ak_strb_cstr(&want, w, wn);
				i += on;
				left--;
			} else {
				ak_strb_cstr(&want, h + i, 1);
				i++;
			}
		}
		ak_str *ws2 = ak_strb_finish(&want), *got = ak_str_replace_n(hs, os, ws, count);
		CHECK(ak_str_eq(got, ws2));
		ak_release(got), ak_release(ws2), ak_release(hs), ak_release(os), ak_release(ws);
	}
	ak_release(s), ak_release(comma), ak_release(dash), ak_release(u), ak_release(aaa), ak_release(aa), ak_release(b);
}

static int64_t naive_count(const char *s, size_t n) {
	int64_t k = 0;
	for (size_t i = 0; i < n; i++) k += ((uint8_t)s[i] & 0xc0) != 0x80;
	return k;
}

static int64_t naive_offset(const char *s, size_t n, int64_t c) {
	if (c <= 0) return 0;
	int64_t k = 0;
	for (size_t i = 0; i < n; i++)
		if (((uint8_t)s[i] & 0xc0) != 0x80) {
			if (k == c) return (int64_t)i;
			k++;
		}
	return (int64_t)n;
}

TEST(characters_counted_and_found) {
	// Text of every kind of byte, UTF-8 and not, at every alignment.
	static const char *pieces[] = {"a", "é", "€", "😀", "\x80", "\xc3", "\xff", "bcdefgh"};
	char s[600];
	for (int round = 0; round < 3000; round++) {
		size_t n = 0, want = next() % 500;
		while (n < want) {
			const char *p = pieces[next() % 8];
			size_t l = strlen(p);
			if (n + l > sizeof s) break;
			memcpy(s + n, p, l);
			n += l;
		}
		ak_str *str = ak_str_new(s, n);
		int64_t chars = naive_count(s, n);
		CHECK_EQ_INT(ak_str_count(str), chars);
		CHECK_EQ_INT(ak_str_count(str), chars);  // cached
		for (int k = 0; k < 8; k++) {
			int64_t c = (int64_t)(next() % (uint64_t)(chars + 3)) - 1;
			CHECK_EQ_INT(ak_str_char_offset(str, c), naive_offset(s, n, c));
		}
		ak_release(str);
	}
	ak_str *a = S("plain ASCII text, long enough for words");
	CHECK(ak_str_ascii(a));
	CHECK_EQ_INT(ak_str_count(a), a->len);
	CHECK_EQ_INT(ak_str_char_offset(a, 5), 5);
	CHECK_EQ_INT(ak_str_char_offset(a, 500), a->len);
	ak_str *u = S("caf\xc3\xa9 au lait, encore");
	CHECK(!ak_str_ascii(u));
	CHECK_EQ_INT(ak_str_count(u), 20);
	ak_release(a), ak_release(u);
}

// An ASCII string's flags say so before it's frozen, so threads read
// them, and read them right.
TEST(constants_know_their_bytes) {
	ak_str *a = S("constant"), *u = S("cönstant");
	ak_frozen made = {0};
	ak_freeze((ak_val){.p = a}, AK_K_REF, &made);
	ak_freeze((ak_val){.p = u}, AK_K_REF, &made);
	CHECK(a->h.flags & AK_STR_KNOWN && a->h.flags & AK_STR_ASCII);
	CHECK(u->h.flags & AK_STR_KNOWN && !(u->h.flags & AK_STR_ASCII));
	CHECK_EQ_INT(ak_str_count(u), 8);
	ak_frozen_free(&made);
}

TEST(builder_grows_from_small) {
	for (size_t total = 0; total < 1000; total += 37) {
		ak_strb b = {0};
		char want[1100];
		size_t n = 0;
		while (n < total) {
			size_t k = 1 + next() % 20;
			if (n + k > total) k = total - n;
			for (size_t i = 0; i < k; i++) want[n + i] = (char)('a' + (n + i) % 26);
			ak_strb_cstr(&b, want + n, k);
			n += k;
		}
		ak_strb_int(&b, INT64_MIN);
		ak_strb_float(&b, -1.5e-7);
		ak_strb_bool(&b, false);
		n += (size_t)snprintf(want + n, sizeof want - n, "%lld-1.5e-7false", (long long)INT64_MIN);
		ak_str *s = ak_strb_finish(&b);
		CHECK_EQ_INT(s->len, n);
		CHECK(memcmp(s->data, want, n) == 0 && s->data[n] == 0);
		ak_release(s);
	}
	// A short one allocates once: the string.
	int64_t before = ak_alloc_count;
	ak_strb b = {0};
	ak_strb_cstr(&b, "item ", 5);
	ak_strb_int(&b, 42);
	ak_str *s = ak_strb_finish(&b);
	CHECK_EQ_INT(ak_alloc_count - before, 1);
	CHECK_EQ_STR(s->data, "item 42");
	ak_release(s);
}

TEST(ints_printed) {
	static const int64_t cases[] = {0, 1, -1, 9, 10, 99, 100, -100, 12345678901234, INT64_MAX, INT64_MIN, -9223372036854775807LL};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		char want[32];
		snprintf(want, sizeof want, "%lld", (long long)cases[i]);
		ak_str *s = ak_str_from_int(cases[i]);
		CHECK_EQ_STR(s->data, want);
		ak_release(s);
	}
	for (int i = 0; i < 20000; i++) {
		int64_t v = (int64_t)next() >> (next() % 64);
		char want[32];
		snprintf(want, sizeof want, "%lld", (long long)v);
		ak_str *s = ak_str_from_int(v);
		if (strcmp(s->data, want)) T_FAIL("%s, want %s", s->data, want);
		ak_release(s);
	}
}

// How floats were printed before Ryu: the fewest of printf's correctly
// rounded digits that read back as the float, laid out as JavaScript's.
static void ref_float(double v, char *out) {
	if (v != v) { strcpy(out, "NaN"); return; }
	if (v == INFINITY) { strcpy(out, "Infinity"); return; }
	if (v == -INFINITY) { strcpy(out, "-Infinity"); return; }
	if (v == 0) { strcpy(out, "0"); return; }
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
	int n = atoi(c + 1) + 1;
	size_t o = 0;
	if (v < 0) out[o++] = '-';
	if (k <= n && n <= 21) {
		memcpy(out + o, digits, (size_t)k), o += (size_t)k;
		for (int i = k; i < n; i++) out[o++] = '0';
	} else if (0 < n && n <= 21) {
		memcpy(out + o, digits, (size_t)n), o += (size_t)n;
		out[o++] = '.';
		memcpy(out + o, digits + n, (size_t)(k - n)), o += (size_t)(k - n);
	} else if (-6 < n && n <= 0) {
		out[o++] = '0', out[o++] = '.';
		for (int i = n; i < 0; i++) out[o++] = '0';
		memcpy(out + o, digits, (size_t)k), o += (size_t)k;
	} else {
		out[o++] = digits[0];
		if (k > 1) {
			out[o++] = '.';
			memcpy(out + o, digits + 1, (size_t)(k - 1)), o += (size_t)(k - 1);
		}
		o += (size_t)snprintf(out + o, 64 - o, "e%c%d", n - 1 < 0 ? '-' : '+', abs(n - 1));
	}
	out[o] = 0;
}

static bool float_ok(double v) {
	char want[64];
	ref_float(v, want);
	ak_str *s = ak_str_from_float(v);
	bool ok = strcmp(s->data, want) == 0;
	if (!ok) T_FAIL("%a: %s, want %s", v, s->data, want);
	ak_release(s);
	return ok;
}

static double from_bits(uint64_t b) {
	double d;
	memcpy(&d, &b, 8);
	return d;
}

TEST(floats_printed_as_before) {
	static const double cases[] = {
		0.1, 0.2, 0.3, 1.0 / 3, 2.0 / 3, 1e21, 1e21 * (1 - DBL_EPSILON), 1e-6, 1e-7, 123456789012345680000.0,
		DBL_MAX, DBL_MIN, DBL_TRUE_MIN, 5e-324, 1.7976931348623157e308, 2.2250738585072014e-308,
		2.2250738585072009e-308, 9007199254740992.0, 9007199254740993.0, 4503599627370496.5, 0.5, 1.5,
		100, 1e15, 1e16, 1e17, 123e18, 5e-7, 1.25, -0.0001, 299792458, 3.141592653589793, 2.718281828459045,
		1e23, 8.41e21, 5.0e-324 * 3, 1.0e-323, 9.5e-5, 0.000001, 4.35, 0.07, 1e300 * 1e-10,
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		float_ok(cases[i]);
		float_ok(-cases[i]);
	}
	// Every power of 2 (the slow path) and its neighbors (Ryu's).
	for (int e = -1074; e <= 1023; e++) {
		double p = ldexp(1, e);
		float_ok(p);
		float_ok(nextafter(p, 0));
		float_ok(nextafter(p, INFINITY));
	}
	// Random bits, of every exponent; ints; the decimals people write.
	int bad = 0;
	for (int i = 0; i < 300000 && bad < 10; i++) {
		double v = from_bits(next());
		if (v != v) continue;
		bad += !float_ok(v);
	}
	for (int i = 0; i < 20000 && bad < 10; i++) {
		bad += !float_ok((double)(int64_t)(next() >> (next() % 64)));
		bad += !float_ok((double)(next() % 100000) / 100);
		bad += !float_ok((double)(next() % 1000000) / 7.0 + 0.1);
		bad += !float_ok((double)(next() % 1000) * 1e-9);
	}
	// Its digits read back as the float.
	for (int i = 0; i < 20000; i++) {
		double v = from_bits(next() & ~(0x7ffull << 52)) * 1e10;
		char buf[48];
		int n = ak_float_text(buf, v);
		buf[n] = 0;
		CHECK(strtod(buf, NULL) == v);
	}
}

static ak_lib_fn lib(const char *name) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	abort();
}

// findAny's characters, as each character of s compared with each of
// them.
static int64_t naive_find_any(const char *s, size_t n, const char *cs, size_t cn) {
	for (size_t i = 0; i < n;) {
		size_t j = i + 1;
		while (j < n && ((uint8_t)s[j] & 0xc0) == 0x80) j++;
		for (size_t k = 0; k < cn;) {
			size_t l = k + 1;
			while (l < cn && ((uint8_t)cs[l] & 0xc0) == 0x80) l++;
			if (l - k == j - i && memcmp(s + i, cs + k, j - i) == 0) return (int64_t)i;
			k = l;
		}
		i = j;
	}
	return -1;
}

TEST(find_any_as_naive) {
	static const char *pieces[] = {"a", "z", "V", "é", "\x80", "\xc3", "€", " "};
	static const char *sets[] = {"zV", "é€", ("\x80" "a"), "", " \xc3", "q"};
	char s[100];
	for (int round = 0; round < 3000; round++) {
		size_t n = 0, want = next() % 60;
		while (n < want) {
			const char *p = pieces[next() % 8];
			memcpy(s + n, p, strlen(p));
			n += strlen(p);
		}
		const char *cs = sets[round % 6];
		ak_str *a = ak_str_new(s, n), *b = ak_str_c(cs);
		ak_box *got = lib("str.find_any")((ak_val[]){{.p = a}, {.p = b}}, 2).p;
		int64_t want_at = naive_find_any(s, n, cs, strlen(cs));
		CHECK_EQ_INT(got ? got->v.i : -1, want_at);
		if (got) ak_release(got);
		ak_release(a), ak_release(b);
	}
}

// bytes' string(): what's UTF-8 as it is, each byte that isn't one
// U+FFFD, as ak_utf8_decode reads it a code point at a time.
TEST(bytes_string_as_decoded) {
	static const char *pieces[] = {"abcdefgh", "a", "é", "€", "😀", "\x80", "\xc3", "\xe2\x82", "\xff", "\xed\xa0\x80", "\xc0\xaf"};
	char s[300];
	for (int round = 0; round < 3000; round++) {
		size_t n = 0, want = next() % 200;
		while (n < want) {
			const char *p = pieces[next() % AK_LEN(pieces)];
			memcpy(s + n, p, strlen(p));
			n += strlen(p);
		}
		ak_strb ref = {0};
		for (size_t i = 0; i < n;) {
			size_t from = i;
			uint32_t c = ak_utf8_decode(s, n, &i);
			if (c == 0xfffd && !(i - from == 3 && memcmp(s + from, "\xef\xbf\xbd", 3) == 0)) ak_strb_cstr(&ref, "\xef\xbf\xbd", 3);
			else ak_strb_cstr(&ref, s + from, i - from);
		}
		ak_str *want_s = ak_strb_finish(&ref);
		ak_bytes *b = ak_bytes_new(s, n);
		ak_str *got = lib("bytes.str")((ak_val[]){{.p = b}}, 1).p;
		CHECK(ak_str_eq(got, want_s));
		ak_release(got), ak_release(want_s), ak_release(b);
	}
}

TEST(hash_bytes) {
	// Equal bytes hash the same wherever they are; different ones (of
	// every length) almost never do.
	char a[200], b[264];
	for (size_t n = 0; n <= sizeof a; n++) {
		fill(a, n, "abcdefgh");
		memcpy(b + 64 - n % 8, a, n);
		CHECK(ak_hash_bytes(a, n) == ak_hash_bytes(b + 64 - n % 8, n));
		if (n) {
			a[next() % n] ^= 1;
			CHECK(ak_hash_bytes(a, n) != ak_hash_bytes(b + 64 - n % 8, n));
		}
	}
	CHECK(ak_hash_bytes("", 0) != ak_hash_bytes("\0", 1));
	// Its low bits, which index a map, spread: 64K short keys in 4096
	// buckets, none much fuller than the 16 each would have.
	static int buckets[4096];
	int most = 0;
	for (int i = 0; i < 65536; i++) {
		char k[16];
		int n = snprintf(k, sizeof k, "key%d", i);
		int *c = &buckets[ak_hash_bytes(k, (size_t)n) & 4095];
		if (++*c > most) most = *c;
	}
	CHECK(most < 40);
}

T_MAIN()
