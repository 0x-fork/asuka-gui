// The standard library's builtins (core/src/std), called as the VM
// calls them: what the Asuka programs of lang/compile/testdata/run reach
// less easily (the local zone, hashes' block boundaries, programs a
// Regex was made with by hand, paths).
#define _POSIX_C_SOURCE 200809L  // setenv, tzset
#include "t.h"
#include "std/std.h"
#include "rt/sched.h"

#include <time.h>

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	for (const ak_lib_entry *e = ak_lib; e->name; e++)
		if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

static ak_val S(const char *s) { return (ak_val){.p = ak_str_c(s)}; }
static ak_val I(int64_t i) { return (ak_val){.i = i}; }

// call calls a builtin with args, releasing the references among them
// (refs: a bit per argument).
static ak_val call(const char *name, ak_val *args, uint32_t n, uint32_t refs) {
	ak_val r = fn(name)(args, n);
	for (uint32_t i = 0; i < n; i++)
		if (refs >> i & 1) ak_release(args[i].p);
	return r;
}

static void check_str(ak_val v, const char *want, int line) {
	ak_str *s = v.p;
	if (!s || strcmp(s->data, want) != 0) {
		fprintf(stderr, "std_test.c:%d: %s: got %s, want %s\n", line, t_current, s ? s->data : "(none)", want);
		t_failed++;
	}
	ak_release(s);
}
#define CHECK_STR(v, want) check_str(v, want, __LINE__)

static int64_t offset_at(int64_t unix) { return call("time.localOffset", (ak_val[]){I(unix)}, 1, 0).i; }

TEST(local_offsets) {
#if !defined(_WIN32)
	const char *old = getenv("TZ");
	char *saved = old ? strcpy(malloc(strlen(old) + 1), old) : NULL;
	setenv("TZ", "America/New_York", 1);
	tzset();
	CHECK_EQ_INT(offset_at(1767225600), -5 * 3600);  // 2026-01-01: EST
	CHECK_EQ_INT(offset_at(1782864000), -4 * 3600);  // 2026-07-01: EDT
	setenv("TZ", "Asia/Kolkata", 1);
	tzset();
	CHECK_EQ_INT(offset_at(1767225600), 5 * 3600 + 1800);
	CHECK_EQ_INT(offset_at(-86400 * 365), 5 * 3600 + 1800);
	setenv("TZ", "UTC", 1);
	tzset();
	CHECK_EQ_INT(offset_at(1767225600), 0);
	// The manual clock (tests): UTC whatever TZ says, at the test epoch.
	setenv("TZ", "America/New_York", 1);
	tzset();
	ak_sched_manual_clock(true);
	CHECK_EQ_INT(offset_at(1782864000), 0);
	CHECK_EQ_INT(call("time.wallNanos", NULL, 0, 0).i, AK_TEST_EPOCH);
	ak_sched_advance(1500000000);
	CHECK_EQ_INT(call("time.wallNanos", NULL, 0, 0).i, AK_TEST_EPOCH + 1500000000);
	ak_sched_manual_clock(false);
	if (saved) setenv("TZ", saved, 1);
	else unsetenv("TZ");
	tzset();
	free(saved);
#endif
}

static ak_val fmt(const char *pattern, int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s, int64_t ns, int64_t wd, int64_t off) {
	return call("time.formatFields", (ak_val[]){S(pattern), I(y), I(mo), I(d), I(h), I(mi), I(s), I(ns), I(wd), I(off)}, 10, 1);
}

TEST(time_format) {
	CHECK_STR(fmt("yyyy-MM-dd'T'HH:mm:ssXXX", 2026, 3, 7, 15, 4, 5, 0, 6, -9000), "2026-03-07T15:04:05-02:30");
	CHECK_STR(fmt("y yy yyyyy", 7, 1, 1, 0, 0, 0, 0, 0, 0), "7 07 00007");
	CHECK_STR(fmt("h a K k", 2026, 1, 1, 0, 0, 0, 0, 4, 0), "12 AM 0 24");
	CHECK_STR(fmt("h a", 2026, 1, 1, 12, 0, 0, 0, 4, 0), "12 PM");
	CHECK_STR(fmt("D", 2024, 12, 31, 0, 0, 0, 0, 2, 0), "366");
	CHECK_STR(fmt("x xx xxx X", 2026, 1, 1, 0, 0, 0, 0, 4, 0), "+00 +0000 +00:00 Z");
	CHECK_STR(fmt("X", 2026, 1, 1, 0, 0, 0, 0, 4, 19800), "+0530");
	CHECK_STR(fmt("SS", 2026, 1, 1, 0, 0, 0, 999999999, 4, 0), "99");
	CHECK_STR(fmt("'unterminated", 2026, 1, 1, 0, 0, 0, 0, 4, 0), "unterminated");
	CHECK_STR(fmt("", 2026, 1, 1, 0, 0, 0, 0, 4, 0), "");
	CHECK_STR(fmt("é-ç", 2026, 1, 1, 0, 0, 0, 0, 4, 0), "é-ç");
}

TEST(time_parse) {
	const char *good[] = {"2024-02-29T00:00:00Z", "1678-01-01T00:00:00Z", "2262-04-11T23:47:16.854775807Z"};
	for (size_t i = 0; i < AK_LEN(good); i++) {
		ak_val r = call("time.parseRFC3339", (ak_val[]){S(good[i])}, 1, 1);
		CHECK(!ak_error_pending() && r.p);
		ak_release(r.p);
	}
	ak_val r = call("time.parseRFC3339", (ak_val[]){S("1970-01-01T01:00:00.5+01:00")}, 1, 1);
	ak_arr *a = r.p;
	REQUIRE(a && a->len == 2);
	CHECK_EQ_INT(a->data[0].i, 500000000);
	CHECK_EQ_INT(a->data[1].i, 3600);
	ak_release(a);
	const char *bad[] = {"2023-02-29T00:00:00Z", "2026-01-01T24:00:00Z", "2026-01-01T00:00:60Z", "2026-01-01T00:00:00.Z", "2026-01-01T00:00:00", "2026-01-01T00:00:00+0100", "2026-01-01T00:00:00Zjunk", "", "0001-01-01T00:00:00Z", "2262-04-11T23:47:16.854775808Z"};
	for (size_t i = 0; i < AK_LEN(bad); i++) {
		call("time.parseRFC3339", (ak_val[]){S(bad[i])}, 1, 1);
		if (!ak_error_pending()) T_FAIL("%s parsed", bad[i]);
		ak_release(ak_error_take());
	}
}

static void hex(const uint8_t *p, size_t n, char *out) {
	for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]);
}

TEST(sha) {
	// Lengths around the padding's boundaries (55, 56, 64 bytes), and a
	// million: FIPS 180's vectors and sha256sum's.
	struct { size_t n; const char *sha256; } cases[] = {
		{55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
		{56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
		{64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
		{1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		uint8_t *p = malloc(cases[i].n), out[32];
		memset(p, 'a', cases[i].n);
		ak_sha256(p, cases[i].n, out);
		char h[65];
		hex(out, 32, h);
		CHECK_EQ_STR(h, cases[i].sha256);
		free(p);
	}
	uint8_t out[20];
	char h[41];
	ak_sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, out);
	hex(out, 20, h);
	CHECK_EQ_STR(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(base64_padding) {
	const char *ok[][2] = {{"Zg", "f"}, {"Zg==", "f"}, {"Zm8", "fo"}, {"Zm8=", "fo"}, {"", ""}, {"Z m 9 v", "foo"}};
	for (size_t i = 0; i < AK_LEN(ok); i++) {
		ak_val r = call("encoding.base64Decode", (ak_val[]){S(ok[i][0])}, 1, 1);
		ak_bytes *b = r.p;
		REQUIRE(!ak_error_pending() && b);
		if (b->len != strlen(ok[i][1]) || memcmp(b->data, ok[i][1], b->len) != 0) T_FAIL("%s", ok[i][0]);
		ak_release(b);
	}
	const char *bad[] = {"Z", "Zg=", "Zg===", "Zm8==", "=Zg"};
	for (size_t i = 0; i < AK_LEN(bad); i++) {
		call("encoding.base64Decode", (ak_val[]){S(bad[i])}, 1, 1);
		if (!ak_error_pending()) T_FAIL("%s decoded", bad[i]);
		ak_release(ak_error_take());
	}
}

TEST(regex_programs) {
	ak_val prog = call("regex.program", (ak_val[]){S("(a+)b")}, 1, 1);
	REQUIRE(prog.p);
	ak_val r = call("regex.test", (ak_val[]){prog, S("xaab")}, 2, 2);
	CHECK(r.i);
	// A program changed by hand: a jump out of range, a bad magic, a
	// truncation. Each throws instead of running.
	ak_bytes *b = prog.p;
	ak_bytes *bad[3];
	for (int k = 0; k < 3; k++) bad[k] = ak_bytes_new(b->data, b->len);
	for (uint32_t i = 20; i + 12 <= b->len; i += 12)
		if (bad[0]->data[i] == 5 /* I_SPLIT */) bad[0]->data[i + 4] = 0xff, bad[0]->data[i + 5] = 0xff;
	bad[1]->data[0] = 'X';
	bad[2]->len -= 3;
	for (int k = 0; k < 3; k++) {
		call("regex.test", (ak_val[]){{.p = bad[k]}, S("aab")}, 2, 3);
		if (!ak_error_pending()) T_FAIL("program %d ran", k);
		ak_release(ak_error_take());
	}
	call("regex.test", (ak_val[]){{.p = NULL}, S("aab")}, 2, 2);
	CHECK(ak_error_pending());
	ak_release(ak_error_take());
	ak_release(prog.p);
	// Too large: repeats of repeats in groups.
	call("regex.program", (ak_val[]){S("((a{1000}){1000}){1000}")}, 1, 1);
	CHECK(ak_error_pending());
	ak_release(ak_error_take());
	// Nested too deep.
	char deep[2 * 300 + 2];
	for (int i = 0; i < 300; i++) deep[i] = '(', deep[300 + 1 + i] = ')';
	deep[300] = 'a';
	deep[601] = 0;
	call("regex.program", (ak_val[]){S(deep)}, 1, 1);
	CHECK(ak_error_pending());
	ak_release(ak_error_take());
}

static ak_val search(const char *pattern, const char *text) {
	ak_val prog = call("regex.program", (ak_val[]){S(pattern)}, 1, 1);
	if (!prog.p) return prog;
	return call("regex.search", (ak_val[]){prog, S(text), I(-1)}, 3, 3);
}

TEST(regex_offsets) {
	// Invalid UTF-8 reads as U+FFFD, a byte at a time; \b at the text's
	// ends; anchors in the middle of a search.
	ak_val r = search(".", "a\xff" "b");
	ak_arr *a = r.p;
	REQUIRE(a);
	CHECK_EQ_INT(a->len, 3);
	ak_release(a);
	r = search("\\bx", "x x");
	a = r.p;
	REQUIRE(a && a->len == 2);
	CHECK_EQ_INT(((ak_arr *)a->data[1].p)->data[0].i, 2);
	ak_release(a);
	r = search("^a", "aaa");
	a = r.p;
	CHECK_EQ_INT(a->len, 1);
	ak_release(a);
	r = search("(?m)^a", "a\na\nb");
	a = r.p;
	CHECK_EQ_INT(a->len, 2);
	ak_release(a);
	// (?i) on classes and on Greek.
	r = search("(?i)[α-γ]+", "ΑΒΓδ");
	a = r.p;
	REQUIRE(a && a->len == 1);
	CHECK_EQ_INT(((ak_arr *)a->data[0].p)->data[1].i, 6);
	ak_release(a);
}

TEST(regex_alloc_bounded) {
	// Matching allocates per call, not per character: a longer text costs
	// no more allocations.
	ak_val prog = call("regex.program", (ak_val[]){S("(\\w+)@(\\w+)\\.com")}, 1, 1);
	char text[20001];
	memset(text, 'x', 20000);
	text[20000] = 0;
	ak_val t = S(text);
	uint64_t before = ak_alloc_count;
	call("regex.test", (ak_val[]){prog, t}, 2, 0);
	uint64_t one = ak_alloc_count - before;
	CHECK(one <= 4);
	ak_release(t.p);
	ak_release(prog.p);
}

TEST(url_parts) {
	ak_val r = call("url.parts", (ak_val[]){S("http://u@[::1]:/p")}, 1, 1);
	ak_arr *a = r.p;
	REQUIRE(a && a->len == 7);
	CHECK_EQ_STR(((ak_str *)a->data[2].p)->data, "::1");
	CHECK_EQ_STR(((ak_str *)a->data[3].p)->data, "");
	ak_release(a);
	r = call("url.parts", (ak_val[]){S("http://a@b@c:1/")}, 1, 1);
	a = r.p;
	REQUIRE(a && a->len == 7);
	CHECK_EQ_STR(((ak_str *)a->data[1].p)->data, "a@b");
	CHECK_EQ_STR(((ak_str *)a->data[2].p)->data, "c");
	ak_release(a);
	ak_str *nul = ak_str_new("a\0b", 3);
	r = call("url.escape", (ak_val[]){{.p = nul}}, 1, 1);
	CHECK_STR(r, "a%00b");
}

TEST(random_seeded) {
	call("random.seed", (ak_val[]){I(7)}, 1, 0);
	int64_t a = call("random.int", (ak_val[]){I(INT64_MIN), I(INT64_MAX)}, 2, 0).i;
	call("random.seed", (ak_val[]){I(7)}, 1, 0);
	int64_t b = call("random.int", (ak_val[]){I(INT64_MIN), I(INT64_MAX)}, 2, 0).i;
	CHECK_EQ_INT(a, b);
}

T_MAIN()
