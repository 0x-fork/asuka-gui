// rtf_test.c: RTF written and read back, and what a rich edit control
// streams out read (rtf.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtf.h"

static int failed;

#define CHECK(c, ...) \
	do { \
		if (!(c)) { \
			fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__); \
			fputc('\n', stderr); \
			failed = 1; \
		} \
	} while (0)

static const rtf_doc doc = {"Segoe UI", "Consolas", 10.5f, {0x1b, 0x1b, 0x1b}};

typedef struct {
	const char *text;
	unsigned flags;  // 1 bold, 2 italic, 4 underline, 8 strike, 16 mono
	const char *link;
	int color, bg;  // 0x1RRGGBB when set
} want_run;

static void dump(const rtf_runs *r) {
	for (uint32_t i = 0; i < r->n; i++) {
		const rtf_run *x = &r->v[i];
		fprintf(stderr, "  %u: \"%s\" b%d i%d u%d s%d m%d link=%s color=%d:%02x%02x%02x bg=%d:%02x%02x%02x\n", i, x->text, x->bold, x->italic,
			x->underline, x->strike, x->mono, x->link ? x->link : "-", x->has_color, x->color.r, x->color.g, x->color.b, x->has_bg, x->bg.r,
			x->bg.g, x->bg.b);
	}
}

static void expect(const char *name, const rtf_runs *got, const want_run *want, uint32_t n) {
	bool ok = got->n == n;
	for (uint32_t i = 0; ok && i < n; i++) {
		const rtf_run *g = &got->v[i];
		const want_run *w = &want[i];
		unsigned f = g->bold | g->italic << 1 | g->underline << 2 | g->strike << 3 | g->mono << 4;
		int color = g->has_color ? 0x1000000 | g->color.r << 16 | g->color.g << 8 | g->color.b : 0;
		int bg = g->has_bg ? 0x1000000 | g->bg.r << 16 | g->bg.g << 8 | g->bg.b : 0;
		ok = strlen(w->text) == g->len && memcmp(w->text, g->text, g->len) == 0 && f == w->flags && color == w->color && bg == w->bg &&
			strcmp(w->link ? w->link : "", g->link ? g->link : "") == 0;
	}
	CHECK(ok, "%s: the runs differ", name);
	if (!ok) dump(got);
}

static void round_trip(void) {
	rtf_run runs[] = {
		{.text = "Hi,\n\nThe "},
		{.text = "1.0", .bold = true},
		{.text = " build is "},
		{.text = "ready", .italic = true, .underline = true},
		{.text = ". Notes: "},
		{.text = "here", .link = "https://example.com/a?b=\"c\""},
		{.text = " \xe2\x80\x94 ", .strike = true},
		{.text = "code{}\\", .mono = true},
		{.text = " \xf0\x9f\x91\x8b colored", .has_color = true, .color = {0, 0, 255}},
		{.text = "\nend"},
	};
	uint32_t n = sizeof runs / sizeof *runs;
	for (uint32_t i = 0; i < n; i++) runs[i].len = strlen(runs[i].text);
	rtf_buf b = {0};
	rtf_write(runs, n, &doc, &b);
	CHECK(b.p && strlen(b.p) == b.n, "NUL terminated");
	rtf_runs got = {0};
	rtf_parse(b.p, b.n, &doc, &got);
	want_run want[] = {
		{"Hi,\n\nThe ", 0, 0, 0, 0},
		{"1.0", 1, 0, 0, 0},
		{" build is ", 0, 0, 0, 0},
		{"ready", 2 | 4, 0, 0, 0},
		{". Notes: ", 0, 0, 0, 0},
		{"here", 0, "https://example.com/a?b=%22c%22", 0, 0},
		{" \xe2\x80\x94 ", 8, 0, 0, 0},
		{"code{}\\", 16, 0, 0, 0},
		{" \xf0\x9f\x91\x8b colored", 0, 0, 0x10000ff, 0},
		{"\nend", 0, 0, 0, 0},
	};
	expect("round trip", &got, want, sizeof want / sizeof *want);
	if (failed) fprintf(stderr, "%s\n", b.p);
	rtf_runs_free(&got);
	free(b.p);
}

// What a rich edit control streams out: its header, a font and color
// table, friendly links, \'hh and \u escapes, a final \par.
static void rich_edit_output(void) {
	const char *src =
		"{\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat\\deflang1033{\\fonttbl{\\f0\\fnil\\fcharset0 Segoe UI;}{\\f1\\fmodern\\fprq1\\fcharset0 Consolas;}}\n"
		"{\\colortbl ;\\red27\\green27\\blue27;\\red0\\green102\\blue204;\\red255\\green255\\blue0;}\n"
		"{\\*\\generator Riched20 10.0.22621}\\viewkind4\\uc1\n"
		"\\pard\\cf1\\f0\\fs21\\lang9 Caf\\'e9 \\b bold\\b0  and \\i it\\i0 , \\ul under\\ulnone  \\strike gone\\strike0  \\f1 mono\\f0  \\cf2 "
		"blue\\cf1  \\highlight3 hi\\highlight0\\par\n"
		"{\\field{\\*\\fldinst{HYPERLINK \"https://go.dev\" }}{\\fldrslt{go}}}\\f0\\fs21  \\u20320?\\u22909?\\par\n"
		"}";
	rtf_runs got = {0};
	rtf_parse(src, strlen(src), &doc, &got);
	want_run want[] = {
		{"Caf\xc3\xa9 ", 0, 0, 0, 0},
		{"bold", 1, 0, 0, 0},
		{" and ", 0, 0, 0, 0},
		{"it", 2, 0, 0, 0},
		{", ", 0, 0, 0, 0},
		{"under", 4, 0, 0, 0},
		{" ", 0, 0, 0, 0},
		{"gone", 8, 0, 0, 0},
		{" ", 0, 0, 0, 0},
		{"mono", 16, 0, 0, 0},
		{" ", 0, 0, 0, 0},
		{"blue", 0, 0, 0x10066cc, 0},
		{" ", 0, 0, 0, 0},
		{"hi", 0, 0, 0, 0x1ffff00},
		{"\n", 0, 0, 0, 0},
		{"go", 0, "https://go.dev", 0, 0},
		{" \xe4\xbd\xa0\xe5\xa5\xbd", 0, 0, 0, 0},
	};
	expect("rich edit output", &got, want, sizeof want / sizeof *want);
	rtf_runs_free(&got);
}

// A font named as the document's monospaced face is monospaced, with or
// without \fmodern.
static void mono_by_name(void) {
	const char *src = "{\\rtf1{\\fonttbl{\\f0\\fnil Segoe UI;}{\\f1\\fnil Consolas;}}\\f0 a \\f1 b}";
	rtf_runs got = {0};
	rtf_parse(src, strlen(src), &doc, &got);
	want_run want[] = {{"a ", 0, 0, 0, 0}, {"b", 16, 0, 0, 0}};
	expect("mono by name", &got, want, 2);
	rtf_runs_free(&got);
}

// Trimming to UTF-16 units: a surrogate pair isn't split, empty runs at
// the end go.
static void trim(void) {
	rtf_runs r = {0};
	const char *src = "{\\rtf1 ab\\b \\u-10179?\\u-8820?\\b0 cd\\par}";  // ab, then 👋 bold, then cd
	rtf_parse(src, strlen(src), &doc, &r);
	CHECK(rtf_utf16_len("\xf0\x9f\x91\x8b", 4) == 2, "a pair is two units");
	rtf_trim(&r, 3);
	want_run want[] = {{"ab", 0, 0, 0, 0}};
	expect("trim", &r, want, 1);
	rtf_runs_free(&r);
}

// A link beyond ASCII is written a character at a time, not a byte at a
// time: it comes back as it went.
static void link_beyond_ascii(void) {
	const char *url = "https://\xe4\xbe\x8b\xe3\x81\x88.jp/\xf0\x9f\x91\x8b?q=\"x\"";
	rtf_run runs[] = {{.text = "see", .len = 3, .link = (char *)url}};
	rtf_buf b = {0};
	rtf_write(runs, 1, &doc, &b);
	rtf_runs got = {0};
	rtf_parse(b.p, b.n, &doc, &got);
	want_run want[] = {{"see", 0, "https://\xe4\xbe\x8b\xe3\x81\x88.jp/\xf0\x9f\x91\x8b?q=%22x%22", 0, 0}};
	expect("link beyond ASCII", &got, want, 1);
	if (failed) fprintf(stderr, "%s\n", b.p);
	rtf_runs_free(&got);
	free(b.p);
}

// A control word's parameter past 32 bits (a long, on Windows) stops at
// its largest, as a reader of 32-bit parameters has it: it doesn't wrap.
static void long_parameters(void) {
	const char *src = "{\\rtf1{\\colortbl ;\\red0\\green0\\blue255;}"
		"{\\b4294967296 big}{\\cf99999999999999999999 none}{\\i-99999999999999999999 neg}}";
	rtf_runs got = {0};
	rtf_parse(src, strlen(src), &doc, &got);
	want_run want[] = {{"big", 1, 0, 0, 0}, {"none", 0, 0, 0, 0}, {"neg", 2, 0, 0, 0}};
	expect("long parameters", &got, want, 3);
	rtf_runs_free(&got);
}

int main(void) {
	round_trip();
	link_beyond_ascii();
	long_parameters();
	rich_edit_output();
	mono_by_name();
	trim();
	if (failed) return 1;
	puts("ok");
	return 0;
}
