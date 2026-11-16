// The shared desktop C's tests (platform/desktop/desktop_test.go builds
// and runs them on this computer).
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L  // strdup, readlink under -std=c17
#endif
#include "desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

static void test_json(void) {
	const char *t = "{\"a\": \"x\\u00e9\\n\", \"b\": [1, 2.5, true, null], \"c\": {\"d\": false}, \"a\": \"last\"}";
	dsk_json *j = dsk_json_parse(t, strlen(t));
	CHECK(j && j->type == DSK_JOBJ);
	CHECK(strcmp(dsk_json_str(j, "a", ""), "last") == 0);
	const dsk_json *b = dsk_json_get(j, "b");
	CHECK(b && b->type == DSK_JARR && b->n == 4 && b->kids[1].num == 2.5 && b->kids[2].type == DSK_JTRUE);
	CHECK(dsk_json_bool(dsk_json_get(j, "c"), "d", true) == false);
	dsk_jw w = {0};
	dsk_jw_value(&w, j);
	char *out = dsk_jw_take(&w, NULL);
	CHECK(out && strcmp(out, "{\"a\":\"x\xc3\xa9\\n\",\"b\":[1,2.5,true,null],\"c\":{\"d\":false},\"a\":\"last\"}") == 0);
	free(out);
	dsk_json_free(j);
	CHECK(dsk_json_parse("{\"a\":}", 6) == NULL);
	CHECK(dsk_json_parse("[1,2] x", 7) == NULL);
	const char *s = "\"\\ud83d\\ude00<\"";
	j = dsk_json_parse(s, strlen(s));
	CHECK(j && strcmp(j->str, "\xf0\x9f\x98\x80<") == 0);
	dsk_jw_value(&w, j);
	out = dsk_jw_take(&w, NULL);
	CHECK(out && strcmp(out, "\"\xf0\x9f\x98\x80\\u003c\"") == 0);
	free(out);
	dsk_json_free(j);
}

static void test_state(const char *dir) {
	char *path = dsk_join(dir, "asuka-window.json");
	dsk_write_file(path, "{\"x\":10,\"y\":20}", 15);  // an older file: the main window's
	dsk_winstate *s = dsk_winstate_open(path);
	const dsk_json *m = dsk_winstate_load(s, "main");
	CHECK(m && dsk_json_num(m, "x", 0) == 10);
	dsk_winstate_save(s, "prefs", "{\"w\":3}");
	dsk_winstate_close(s);
	s = dsk_winstate_open(path);
	CHECK(dsk_json_num(dsk_winstate_load(s, "prefs"), "w", 0) == 3);
	CHECK(dsk_json_num(dsk_winstate_load(s, "main"), "y", 0) == 20);
	dsk_winstate_forget(s, "prefs");
	CHECK(dsk_winstate_load(s, "prefs") == NULL);
	dsk_winstate_close(s);
	free(path);

	path = dsk_join(dir, "asuka-routes.json");
	dsk_routes *r = dsk_routes_open(path);
	CHECK(dsk_routes_set(r, "main", "/inbox/3", 8, "{}", 2));
	CHECK(!dsk_routes_set(r, "main", "/inbox/3", 8, "{}", 2));
	dsk_routes_flush(r);
	dsk_routes_close(r);
	r = dsk_routes_open(path);
	const char *href, *state;
	size_t sl;
	CHECK(dsk_routes_location(r, "main", &href, &state, &sl) && strcmp(href, "/inbox/3") == 0 && sl == 2);
	dsk_routes_close(r);
	free(path);
}

int main(int argc, char **argv) {
	test_json();
	if (argc > 1) test_state(argv[1]);
	fprintf(stderr, "%d failures\n", failed);
	return failed ? 1 : 0;
}
