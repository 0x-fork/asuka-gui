// snap.c: snapshots (docs/testing.md#snapshots). expectSnapshot(x)
// compares what x prints as (test.show) with the snapshot stored for it:
// a test's nth, keyed "<test> <n>", in the stored snapshots asuka test
// gives (-snapshots: "key\0value\0" records, from the package's
// testdata/snapshots). expectInlineSnapshot(x, "…") compares it with the
// string in the call. Each says what it saw as a JSON line, which asuka
// test reads to write the snapshots (new ones, and with -update the ones
// that changed) and to show how a changed one differs.
#include "testing.h"

#include <stdlib.h>
#include <string.h>

static ak_snap_options opts;
static AK_VEC(char *) keys;
static AK_VEC(char *) vals;
static AK_VEC(size_t) lens;
static char *stored;
static uint32_t nth;  // the test's snapshots so far
static bool in_property;

void ak_snap_init(const ak_snap_options *o) {
	opts = *o;
	if (!o->file) return;
	FILE *f = fopen(o->file, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	stored = ak_alloc((size_t)n + 1);
	size_t got = fread(stored, 1, (size_t)n, f);
	fclose(f);
	for (char *p = stored; p < stored + got;) {
		char *k = p;
		char *v = k + strlen(k) + 1;
		if (v >= stored + got) break;
		size_t vl = strlen(v);
		ak_vec_push(&keys, k);
		ak_vec_push(&vals, v);
		ak_vec_push(&lens, vl);
		p = v + vl + 1;
	}
}

void ak_snap_begin(const char *test, bool property) {
	AK_UNUSED(test);
	nth = 0;
	in_property = property;
}

// normalize: what's compared is the text without the line breaks it
// ends with, and with \r\n as \n.
static size_t normalize(char *s, size_t n) {
	size_t o = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') continue;
		s[o++] = s[i];
	}
	while (o && s[o - 1] == '\n') o--;
	s[o] = 0;
	return o;
}

static void line(const char *kind, const char *key, const char *where, const char *status, const char *value, size_t vn, const char *was, size_t wn, int64_t ln, int64_t col) {
	ak_strb b = {0};
	ak_strb_cstr(&b, "{\"", 2);
	ak_strb_cstr(&b, kind, strlen(kind));
	ak_strb_cstr(&b, "\":", 2);
	ak_jstr(&b, key, strlen(key));
	if (current) {
		ak_strb_cstr(&b, ",\"test\":", 8);
		ak_jstr(&b, current, strlen(current));
	}
	if (ln) {
		ak_strb_cstr(&b, ",\"line\":", 8);
		ak_strb_int(&b, ln);
		ak_strb_cstr(&b, ",\"col\":", 7);
		ak_strb_int(&b, col);
	}
	ak_strb_cstr(&b, ",\"where\":", 9);
	ak_jstr(&b, where, strlen(where));
	ak_strb_cstr(&b, ",\"status\":", 10);
	ak_jstr(&b, status, strlen(status));
	if (value) {
		ak_strb_cstr(&b, ",\"value\":", 9);
		ak_jstr(&b, value, vn);
	}
	if (was) {
		ak_strb_cstr(&b, ",\"stored\":", 10);
		ak_jstr(&b, was, wn);
	}
	ak_strb_cstr(&b, "}", 1);
	ak_jline(&b);
}

// compare compares the text with the snapshot (want, NULL: none), says
// so, and throws when the test fails on it.
static void compare(const char *what, const char *kind, const char *key, const char *where, ak_str *text, const char *want, size_t wn, int64_t ln, int64_t col) {
	char *got = ak_alloc(text->len + 1);
	memcpy(got, text->data, text->len);
	size_t gn = normalize(got, text->len);
	char *w = NULL;
	if (want) {
		w = ak_alloc(wn + 1);
		memcpy(w, want, wn);
		wn = normalize(w, wn);
	}
	if (w && gn == wn && memcmp(got, w, gn) == 0) {
		line(kind, key, where, "ok", NULL, 0, NULL, 0, ln, col);
	} else if (!w) {
		// None stored: written, unless in CI.
		line(kind, key, where, "new", got, gn, NULL, 0, ln, col);
		if (opts.ci && !opts.update) ak_throw_msg("%s (%s): no snapshot is stored for it, and in CI asuka test writes none: run asuka test --update, and commit what it writes", what, where);
	} else {
		line(kind, key, where, opts.update ? "updated" : "changed", got, gn, w, wn, ln, col);
		if (!opts.update) ak_throw_msg("%s (%s): what it shows isn't its snapshot (asuka test --update writes what it shows)", what, where);
	}
	ak_free(got);
	ak_free(w);
}

static bool check_property(const char *what, const char *where) {
	if (!in_property) return true;
	ak_throw_msg("%s (%s): a property test runs it with every value it makes: snapshot a case in a test of its own", what, where);
	return false;
}

// test.snapshot(text, where)
static ak_val test_snapshot(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *text = args[0].p, *where = args[1].p;
	if (!current) {
		ak_throw_msg("expectSnapshot (%s): a snapshot is a test's", where->data);
		return (ak_val){0};
	}
	if (!check_property("expectSnapshot", where->data)) return (ak_val){0};
	nth++;
	char key[1200];
	snprintf(key, sizeof key, "%s %u", current, nth);
	const char *want = NULL;
	size_t wn = 0;
	for (uint32_t i = 0; i < keys.len; i++) {
		if (strcmp(keys.data[i], key) == 0) {
			want = vals.data[i];
			wn = lens.data[i];
		}
	}
	compare("expectSnapshot", "snapshot", key, where->data, text, want, wn, 0, 0);
	return (ak_val){0};
}

// test.inline_snapshot(text, want, has, file, line, col)
static ak_val test_inline_snapshot(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *text = args[0].p, *want = args[1].p, *file = args[3].p;
	bool has = args[2].i != 0;
	int64_t ln = args[4].i, col = args[5].i;
	const char *base = strrchr(file->data, '/');
	base = base ? base + 1 : file->data;
	char where[600];
	snprintf(where, sizeof where, "%s:%lld", base, (long long)ln);
	if (!check_property("expectInlineSnapshot", where)) return (ak_val){0};
	compare("expectInlineSnapshot", "inline", file->data, where, text, has ? want->data : NULL, has ? want->len : 0, ln, col);
	return (ak_val){0};
}

void ak_snap_builtins_init(void) {
	ak_builtin_register("test.snapshot", test_snapshot);
	ak_builtin_register("test.inline_snapshot", test_inline_snapshot);
}
