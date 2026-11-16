// markdown_stream_test.h: what markdown's view tests share: the reply
// streamed in (../tests/stream.json: the paragraphs the core reads, frame
// by frame, as tokens come; core/test/std_test.c keeps it what the core
// reads) as the view is given them, and where two frames first differ.
#ifndef MARKDOWN_STREAM_TEST_H
#define MARKDOWN_STREAM_TEST_H

#include "desktop.h"
#include "markdown.asuka.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
#define CHECK(c, ...)                                                    \
	do {                                                                 \
		if (!(c)) {                                                      \
			fprintf(stderr, "%s:%d: %s: ", __FILE__, __LINE__, #c);      \
			fprintf(stderr, __VA_ARGS__);                                \
			fprintf(stderr, "\n");                                       \
			failed++;                                                    \
		}                                                                \
	} while (0)

typedef struct {
	markdown_list_Paragraph *frames;
	size_t n;
} md_stream;

static char *dup_str(const dsk_json *o, const char *key) { return asuka_strdup(dsk_json_str(o, key, "")); }

static md_stream stream_load(void) {
	md_stream s = {0};
	FILE *f = fopen("../tests/stream.json", "rb");
	if (!f) {
		fprintf(stderr, "no ../tests/stream.json\n");
		exit(1);
	}
	asuka_writer w = {0};
	char buf[4096];
	for (size_t k; (k = fread(buf, 1, sizeof buf, f)) > 0;) asuka_writer_put(&w, buf, k);
	fclose(f);
	dsk_json *doc = dsk_json_parse((const char *)w.data, w.len);
	asuka_writer_free(&w);
	const dsk_json *frames = dsk_json_get(doc, "frames");
	if (!frames || frames->type != DSK_JARR) {
		fprintf(stderr, "stream.json has no frames\n");
		exit(1);
	}
	s.n = frames->n;
	s.frames = calloc(s.n, sizeof *s.frames);
	for (size_t i = 0; i < s.n; i++) {
		const dsk_json *fr = &frames->kids[i];
		markdown_list_Paragraph *l = &s.frames[i];
		l->len = fr->n;
		l->data = calloc(fr->n ? fr->n : 1, sizeof *l->data);
		for (size_t j = 0; j < fr->n; j++) {
			const dsk_json *p = &fr->kids[j];
			markdown_Paragraph *q = &l->data[j];
			q->heading = (int64_t)dsk_json_num(p, "heading", 0);
			q->indent = (int64_t)dsk_json_num(p, "indent", 0);
			q->marker = dup_str(p, "marker");
			q->code = dsk_json_bool(p, "code", false);
			q->rule = dsk_json_bool(p, "rule", false);
			q->row = dsk_json_bool(p, "row", false);
			q->tight = dsk_json_bool(p, "tight", false);
			const dsk_json *runs = dsk_json_get(p, "runs");
			q->runs.len = runs ? runs->n : 0;
			q->runs.data = calloc(q->runs.len ? q->runs.len : 1, sizeof *q->runs.data);
			for (size_t k = 0; k < q->runs.len; k++) {
				const dsk_json *r = &runs->kids[k];
				markdown_Run *x = &q->runs.data[k];
				x->text = dup_str(r, "text");
				x->link = dup_str(r, "link");
				x->bold = dsk_json_bool(r, "bold", false);
				x->italic = dsk_json_bool(r, "italic", false);
				x->code = dsk_json_bool(r, "code", false);
				x->struck = dsk_json_bool(r, "struck", false);
				x->quoted = dsk_json_bool(r, "quoted", false);
			}
		}
	}
	dsk_json_free(doc);
	return s;
}

static void stream_free(md_stream *s) {
	for (size_t i = 0; i < s->n; i++) markdown_list_Paragraph_free(&s->frames[i]);
	free(s->frames);
}

static bool run_equal(const markdown_Run *a, const markdown_Run *b) {
	return strcmp(a->text, b->text) == 0 && strcmp(a->link, b->link) == 0 && a->bold == b->bold && a->italic == b->italic &&
	       a->code == b->code && a->struck == b->struck && a->quoted == b->quoted;
}

static bool para_equal(const markdown_Paragraph *a, const markdown_Paragraph *b) {
	if (a->heading != b->heading || a->indent != b->indent || strcmp(a->marker, b->marker) != 0 || a->code != b->code ||
	    a->rule != b->rule || a->row != b->row || a->tight != b->tight || a->runs.len != b->runs.len)
		return false;
	for (size_t k = 0; k < a->runs.len; k++)
		if (!run_equal(&a->runs.data[k], &b->runs.data[k])) return false;
	return true;
}

// first_change is the first paragraph of b that a doesn't have as it is:
// what a view showing a, given b, has to replace from.
static size_t first_change(const markdown_list_Paragraph *a, const markdown_list_Paragraph *b) {
	size_t i = 0;
	while (i < a->len && i < b->len && para_equal(&a->data[i], &b->data[i])) i++;
	return i;
}

#endif
