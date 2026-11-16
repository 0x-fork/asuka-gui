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

static void test_exists(const char *dir) {
	CHECK(dsk_exists(dir));
	char *p = dsk_join(dir, "nothing-here");
	CHECK(!dsk_exists(p));
	dsk_write_file(p, "x", 1);
	CHECK(dsk_exists(p));
	free(p);
	CHECK(!dsk_exists(NULL) && !dsk_exists(""));
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

static bool same(dsk_rect a, dsk_rect b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

// internal/flyout's cases.
static void test_flyout(void) {
	dsk_rect work = {0, 25, 1440, 875}, icon = {1200, 0, 24, 25};
	struct {
		dsk_rect anchor;
		double w, h;
		int p;
		double gap;
		dsk_rect want;
	} cs[] = {
		{icon, 280, 300, 0, 0, {1072, 25, 280, 300}},
		{{1420, 0, 20, 25}, 280, 300, 0, 0, {1160, 25, 280, 300}},
		{{1300, 100, 100, 20}, 200, 100, 1, 4, {1200, 124, 200, 100}},
		{{100, 100, 100, 20}, 60, 50, 2, 0, {140, 120, 60, 50}},
		{{1300, 870, 24, 30}, 280, 300, 0, 8, {1160, 562, 280, 300}},
		{{600, 500, 40, 20}, 100, 100, 3, 4, {570, 396, 100, 100}},
		{{100, 300, 50, 20}, 100, 60, 7, 0, {150, 300, 100, 60}},
		{{1400, 300, 20, 20}, 100, 60, 6, 0, {1300, 280, 100, 60}},
		{icon, 200, 2000, 0, 0, {1112, 25, 200, 875}},
	};
	for (size_t i = 0; i < sizeof cs / sizeof *cs; i++) CHECK(same(dsk_flyout_place(cs[i].anchor, cs[i].w, cs[i].h, cs[i].p, cs[i].gap, work), cs[i].want));
	CHECK(same(dsk_flyout_center(600, 60, work), (dsk_rect){420, 25 + 815.0 / 3, 600, 60}));
}

int main(int argc, char **argv) {
	test_flyout();
	test_json();
	if (argc > 1) {
		test_state(argv[1]);
		test_exists(argv[1]);
	}
	fprintf(stderr, "%d failures\n", failed);
	return failed ? 1 : 0;
}
