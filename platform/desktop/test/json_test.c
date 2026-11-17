// json_test.c: asuka_desktop.h's JSON helpers (src/json.c), what packages'
// C reads a platform API's JSON with (platform/desktop's TestJSONHelpers,
// under ASan and UBSan): a key's raw value, a string decoded, a string
// quoted, malformed input refused without reading past its end.
#include "asuka_desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

static bool is(char *got, const char *want) {
	bool ok = got ? want && strcmp(got, want) == 0 : !want;
	if (!ok) fprintf(stderr, "got %s, want %s\n", got ? got : "NULL", want ? want : "NULL");
	free(got);
	return ok;
}

// heap is s copied to a buffer of its own size, so reading past its
// terminator is ASan's to see.
static char *heap(const char *s) {
	size_t n = strlen(s) + 1;
	char *b = malloc(n);
	memcpy(b, s, n);
	return b;
}

static char *string_of(const char *s) {
	char *b = heap(s);
	char *out = asuka_json_string(b);
	free(b);
	return out;
}

static void decoded(void) {
	CHECK(is(string_of("\"a\\n\\\"b\\u00e9\""), "a\n\"b\xc3\xa9"));
	CHECK(is(string_of("\"\\ud83d\\ude00\""), "\xf0\x9f\x98\x80"));
	CHECK(is(string_of("1"), NULL));
	// A \u with fewer than four digits, at the end: its letter, and nothing
	// read past the string.
	CHECK(is(string_of("\"\\u1"), "u1"));
	CHECK(is(string_of("\"\\u1\""), "u1"));
	CHECK(is(string_of("\"\\u"), "u"));
	CHECK(is(string_of("\"\\ud83d\\u"), "\xef\xbf\xbdu"));
	// sscanf's %x took a sign, spaces and 0x; JSON's four digits don't.
	CHECK(is(string_of("\"\\u +41\""), "u +41"));
	// A lone surrogate is U+FFFD, not CESU-8.
	CHECK(is(string_of("\"\\udc00x\""), "\xef\xbf\xbdx"));
	CHECK(is(string_of("\"\\ud800\""), "\xef\xbf\xbd"));
	CHECK(is(string_of("\"a\\"), "a"));
}

static char *get(const char *json, const char *key) {
	char *b = heap(json);
	char *out = asuka_json_get(b, key);
	free(b);
	return out;
}

static void keys(void) {
	CHECK(is(get("{\"a\": 1, \"b\": {\"c\": [1, \"]\"]}, \"d\": \"x\"}", "b"), "{\"c\": [1, \"]\"]}"));
	CHECK(is(get("{\"a\": 1, \"d\": \"x\"}", "d"), "\"x\""));
	CHECK(is(get("{\"a\": 1}", "z"), NULL));
	CHECK(is(get("{\"a\": [1, 2", "a"), NULL));
	// Nested past any platform's JSON: refused, not a stack overflow.
	size_t depth = 1 << 20;
	char *deep = malloc(depth + 16);
	strcpy(deep, "{\"a\":");
	memset(deep + 5, '[', depth);
	deep[5 + depth] = 0;
	CHECK(is(asuka_json_get(deep, "a"), NULL));
	free(deep);
}

static void quotes(void) {
	asuka_writer w = {0};
	asuka_json_quote(&w, "a\"\\\n\x01");
	asuka_writer_u8(&w, 0);
	CHECK(strcmp((char *)w.data, "\"a\\\"\\\\\\n\\u0001\"") == 0);
	asuka_writer_free(&w);
}

int main(void) {
	decoded();
	keys();
	quotes();
	fprintf(stderr, "%d failures\n", failed);
	return failed ? 1 : 0;
}
