// Unicode (rt/unicode.c, unicode_gen.c): grapheme clusters on every case
// of Unicode's GraphemeBreakTest.txt, every case mapping and folding the
// release has (core/test/testdata/unicode/case_cases.txt, internal/
// ucdgen's), terminal widths, escape sequences, and the strings' case
// methods over them (lower, upper, foldCase, the comparisons without
// case).
#include "t.h"
#include "rt/rt.h"
#include "rt/lib.h"
#include "rt/unicode.h"

static char *read_file(const char *rel) {
	const char *root = getenv("ASUKA_ROOT");
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", root ? root : "../..", rel);
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc((size_t)n + 1);
	size_t got = fread(b, 1, (size_t)n, f);
	fclose(f);
	b[got] = 0;
	return b;
}

TEST(utf8_round_trip) {
	uint32_t cps[] = {0, 0x41, 0x7f, 0x80, 0x7ff, 0x800, 0xffff, 0x10000, 0x10ffff};
	for (size_t k = 0; k < AK_LEN(cps); k++) {
		char b[4];
		size_t n = ak_utf8_encode(cps[k], b), i = 0;
		CHECK_EQ_INT(ak_utf8_decode(b, n, &i), cps[k]);
		CHECK_EQ_INT(i, n);
	}
	size_t i = 0;
	CHECK_EQ_INT(ak_utf8_decode("\xc0\xaf", 2, &i), 0xfffd);  // overlong
	CHECK_EQ_INT(i, 1);
}

// Every line of GraphemeBreakTest.txt: "÷ 0020 × 0308 ÷ 0020 ÷": the
// clusters ak_grapheme_end finds are the ones between the ÷s.
TEST(grapheme_break_test) {
	char *text = read_file("core/test/testdata/unicode/GraphemeBreakTest.txt");
	REQUIRE(text);
	int cases = 0;
	for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		char *hash = strchr(line, '#');
		if (hash) *hash = 0;
		char s[256];
		size_t n = 0, breaks[64];
		int nb = 0;
		const char *p = line;
		while (*p) {
			if (*p == ' ' || *p == '\t') {
				p++;
			} else if (strncmp(p, "\xc3\xb7", 2) == 0) {  // ÷
				breaks[nb++] = n;
				p += 2;
			} else if (strncmp(p, "\xc3\x97", 2) == 0) {  // ×
				p += 2;
			} else {
				char *end;
				uint32_t cp = (uint32_t)strtoul(p, &end, 16);
				n += ak_utf8_encode(cp, s + n);
				p = end;
			}
		}
		if (nb == 0) continue;
		cases++;
		size_t i = 0;
		int b = 1;
		bool ok = true;
		while (i < n) {
			size_t j = ak_grapheme_end(s, n, i);
			if (b >= nb || breaks[b] != j) ok = false;
			b++;
			i = j;
		}
		if (!ok || b != nb) T_FAIL("clusters of %s", line);
	}
	CHECK(cases > 700);
	free(text);
}

static void cps_of(const char *field, uint32_t *out, int *n) {
	*n = 0;
	const char *p = field;
	while (*p && *p != ';') {
		char *end;
		out[(*n)++] = (uint32_t)strtoul(p, &end, 16);
		p = end;
		while (*p == ' ') p++;
	}
}

// Every code point a mapping changes, as the release says.
TEST(case_mappings) {
	char *text = read_file("core/test/testdata/unicode/case_cases.txt");
	REQUIRE(text);
	int cases = 0;
	for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		if (line[0] == '#') continue;
		char *end;
		uint32_t cp = (uint32_t)strtoul(line, &end, 16);
		const char *f = end + 1;
		for (int k = 0; k < 3; k++) {
			uint32_t want[3], got[3];
			int nw, ng = ak_case_map(cp, (ak_case_kind)k, got);
			cps_of(f, want, &nw);
			if (nw != ng || memcmp(want, got, (size_t)nw * sizeof *got) != 0) T_FAIL("%04X: mapping %d", cp, k);
			f = strchr(f, ';');
			f = f ? f + 1 : "";
		}
		cases++;
	}
	CHECK(cases > 2800);
	// What maps to itself: digits, CJK, an unassigned code point.
	uint32_t o[3];
	CHECK_EQ_INT(ak_case_map('7', AK_CASE_UPPER, o), 1);
	CHECK_EQ_INT(o[0], '7');
	ak_case_map(0x4e2d, AK_CASE_FOLD, o);
	CHECK_EQ_INT(o[0], 0x4e2d);
	ak_case_map(0x10fffd, AK_CASE_LOWER, o);
	CHECK_EQ_INT(o[0], 0x10fffd);
	free(text);
}

static int64_t W(const char *s) { return ak_text_width(s, strlen(s)); }

TEST(terminal_widths) {
	CHECK_EQ_INT(W(""), 0);
	CHECK_EQ_INT(W("hello"), 5);
	CHECK_EQ_INT(W("日本語"), 6);
	CHECK_EQ_INT(W("한국어"), 6);
	CHECK_EQ_INT(W("ｶﾀｶﾅ"), 4);       // halfwidth katakana
	CHECK_EQ_INT(W("ＡＢ"), 4);         // fullwidth Latin
	CHECK_EQ_INT(W("e\xcc\x81"), 1);    // e and a combining acute
	CHECK_EQ_INT(W("\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8"), 2);  // Hangul jamo L V T: one syllable
	CHECK_EQ_INT(W("👍"), 2);
	CHECK_EQ_INT(W("👍🏽"), 2);                         // a modifier
	CHECK_EQ_INT(W("👨‍👩‍👧"), 2);                      // a ZWJ sequence
	CHECK_EQ_INT(W("🇯🇵"), 2);                          // a flag
	CHECK_EQ_INT(W("❤"), 1);                           // text presentation by default
	CHECK_EQ_INT(W("❤️"), 2);                          // VS16
	CHECK_EQ_INT(W("1️⃣"), 2);                         // a keycap
	CHECK_EQ_INT(W("⌚︎"), 1);                          // VS15 on a wide emoji
	CHECK_EQ_INT(W("🏳️‍🌈"), 2);
	CHECK_EQ_INT(W("a\tb\n"), 2);                       // controls take none
	CHECK_EQ_INT(W("\xe2\x80\x8b"), 0);                 // zero width space
	CHECK_EQ_INT(W("\xc2\xad"), 1);                     // soft hyphen, as wcwidth
	CHECK_EQ_INT(W("±"), 1);                            // Ambiguous: narrow
	CHECK_EQ_INT(W("\x1b[1;31mred\x1b[39;22m"), 3);     // SGR
	CHECK_EQ_INT(W("\x1b]8;;https://example.com\x1b\\link\x1b]8;;\x07"), 4);  // an OSC 8 link
	CHECK_EQ_INT(W("中\x1b[0m文"), 4);
	CHECK_EQ_INT(W("\xc2\x9b" "31mx"), 1);              // C1's CSI
	CHECK_EQ_INT(W("\x1b"), 0);
	CHECK_EQ_INT(W("\xff"), 1);                         // not UTF-8: U+FFFD
}

TEST(escape_lengths) {
	struct { const char *s; size_t want; } cases[] = {
		{"\x1b[31m", 5}, {"\x1b[38;2;255;0;0mx", 15}, {"\x1b]0;title\x07x", 10}, {"\x1b]8;;u\x1b\\x", 8},
		{"\x1b(B", 3}, {"\x1b" "7", 2}, {"\x1b[", 2}, {"x", 0}, {"\x1bPq\x1b\\", 5},
	};
	for (size_t k = 0; k < AK_LEN(cases); k++) {
		if (ak_escape_len(cases[k].s, strlen(cases[k].s), 0) != cases[k].want) T_FAIL("escape %zu", k);
	}
}

static void check_map(ak_str *(*f)(ak_str *), const char *in, const char *want) {
	ak_str *s = ak_str_c(in), *o = f(s);
	if (strcmp(o->data, want) != 0) T_FAIL("%s: got %s, want %s", in, o->data, want);
	ak_release(o);
	ak_release(s);
}

TEST(string_case) {
	check_map(ak_str_upper, "straße", "STRASSE");
	check_map(ak_str_upper, "ǆ ŉ ﬃ", "Ǆ ʼN FFI");
	check_map(ak_str_lower, "ÀÉÎ ΣΑΣ ΣΑΣ.", "àéî σας σας.");
	check_map(ak_str_lower, "Σ", "σ");      // not after a letter
	check_map(ak_str_lower, "AΣ'B", "aσ'b");  // a letter after it, past the apostrophe
	check_map(ak_str_lower, "İ", "i\xcc\x87"); // no Turkish rules: i and a combining dot
	check_map(ak_str_lower, "ABC", "abc");
	check_map(ak_str_upper, "日本", "日本");
	check_map(ak_str_fold, "Straße ΣΑΣ", "strasse σασ");
	check_map(ak_str_fold, "ﬁ ẞ", "fi ss");
	// Unchanged: the same string, not a copy.
	ak_str *s = ak_str_c("日本 abc");
	ak_str *l = ak_str_lower(s);
	CHECK(l == s);
	ak_release(l);
	ak_release(s);
}

static bool cmp(bool (*f)(const ak_str *, const ak_str *), const char *a, const char *b) {
	ak_str *x = ak_str_c(a), *y = ak_str_c(b);
	bool r = f(x, y);
	ak_release(x);
	ak_release(y);
	return r;
}

static int order(const char *a, const char *b) {
	ak_str *x = ak_str_c(a), *y = ak_str_c(b);
	int r = ak_str_cmp_fold(x, y);
	ak_release(x);
	ak_release(y);
	return r;
}

TEST(without_case) {
	CHECK(cmp(ak_str_eq_fold, "Go", "GO"));
	CHECK(cmp(ak_str_eq_fold, "Straße", "STRASSE"));
	CHECK(cmp(ak_str_eq_fold, "ΣΑΣ", "σας"));
	CHECK(cmp(ak_str_eq_fold, "", ""));
	CHECK(!cmp(ak_str_eq_fold, "a", "ab"));
	CHECK(!cmp(ak_str_eq_fold, "é", "e\xcc\x81"));  // no normalization
	CHECK_EQ_INT(order("apple", "Banana"), -1);
	CHECK_EQ_INT(order("B", "a"), 1);
	CHECK_EQ_INT(order("ß", "SS"), 0);
	CHECK_EQ_INT(order("ab", "AB"), 0);
	CHECK_EQ_INT(order("a", "AB"), -1);
	CHECK(cmp(ak_str_has_prefix_fold, "STRASSE", "straß"));
	CHECK(cmp(ak_str_has_prefix_fold, "Hello", ""));
	CHECK(!cmp(ak_str_has_prefix_fold, "ß", "s"));  // not inside a code point's folding
	CHECK(cmp(ak_str_has_prefix_fold, "ßa", "ss"));
	CHECK(cmp(ak_str_has_suffix_fold, "README.MD", ".md"));
	CHECK(!cmp(ak_str_has_suffix_fold, "ß", "s"));
	CHECK(!cmp(ak_str_has_suffix_fold, "a", "ab"));
	CHECK(cmp(ak_str_contains_fold, "Die Straße", "STRASSE"));
	CHECK(cmp(ak_str_contains_fold, "Die Straße", "e s"));
	CHECK(!cmp(ak_str_contains_fold, "Straße", "as"));
	CHECK(cmp(ak_str_contains_fold, "x", ""));
	// Past the stack's buffers.
	char big[2000];
	for (int i = 0; i < 1990; i++) big[i] = (char)('a' + i % 26);
	strcpy(big + 1990, "NEEDLE");
	CHECK(cmp(ak_str_contains_fold, big, "needle"));
	CHECK(cmp(ak_str_has_suffix_fold, big, "eedle"));
}

static ak_lib_fn builtin(const char *name) {
	for (const ak_lib_entry *e = ak_lib; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	abort();
}

TEST(graphemes_builtin) {
	ak_val args[1] = {{.p = ak_str_c("e\xcc\x81🇯🇵👨‍👩‍👧a\r\n")}};
	ak_arr *a = builtin("str.graphemes")(args, 1).p;
	REQUIRE(a->len == 5);
	CHECK_EQ_STR(((ak_str *)a->data[0].p)->data, "e\xcc\x81");
	CHECK_EQ_STR(((ak_str *)a->data[1].p)->data, "🇯🇵");
	CHECK_EQ_STR(((ak_str *)a->data[2].p)->data, "👨‍👩‍👧");
	CHECK_EQ_STR(((ak_str *)a->data[4].p)->data, "\r\n");
	ak_release(a);
	ak_release(args[0].p);
}

T_MAIN()
