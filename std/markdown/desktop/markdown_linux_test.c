// markdown_linux_test.c: the Linux view (markdown_linux.c, real GTK) given
// a reply streamed in frame by frame: each frame replaces only the blocks
// from the first that changed (the widgets before stay, counted in the
// box), a selection in a block before them survives, and what it shows
// at the end is what a view given the whole reply at once shows.
// platform/desktop's TestStdUnits runs it on Linux (under Xvfb).
#include "markdown_linux.c"

#include "markdown_stream_test.h"

static size_t children(GtkWidget *box, GtkWidget **out, size_t max) {
	static GtkWidget *(*first)(GtkWidget *), *(*next)(GtkWidget *);
	if (!first) {
		*(void **)&first = asuka_linux_sym("gtk_widget_get_first_child");
		*(void **)&next = asuka_linux_sym("gtk_widget_get_next_sibling");
	}
	size_t n = 0;
	for (GtkWidget *c = first(box); c; c = next(c))
		if (n < max) out[n++] = c;
	return n;
}

static const char *label_text(GtkWidget *l) {
	static const char *(*get)(GtkWidget *);
	if (!get) *(void **)&get = asuka_linux_sym("gtk_label_get_label");
	return get(l);
}

int main(void) {
	if (!asuka_linux() || !G(gtk_init)) {
		printf("markdown (linux): no GTK, skipped\n");
		return 0;
	}
	G(gtk_init)();
	bool (*selection)(GtkWidget *, int *, int *);
	void (*select)(GtkWidget *, int, int);
	*(void **)&selection = asuka_linux_sym("gtk_label_get_selection_bounds");
	*(void **)&select = asuka_linux_sym("gtk_label_select_region");

	md_stream s = stream_load();
	CHECK(s.n > 20, "%zu frames", s.n);
	markdown_Document *d = markdown_make_Document((markdown_Document_events){0});
	CHECK(d != NULL, "no view");
	document *v = (document *)d;
	enum { MAX = 256 };
	GtkWidget *before[MAX], *after[MAX];
	size_t nbefore = 0, replaced = 0, shown = 0;
	GtkWidget *selected = NULL;
	markdown_list_Paragraph none = {0};
	for (size_t f = 0; f < s.n; f++) {
		const markdown_list_Paragraph *prev = f > 0 ? &s.frames[f - 1] : &none;
		d->vt->set_paragraphs(d, s.frames[f]);
		size_t n = children(d->widget, after, MAX);
		CHECK(n == s.frames[f].len, "frame %zu: %zu widgets for %zu paragraphs", f, n, s.frames[f].len);
		size_t kept = 0;
		while (kept < n && kept < nbefore && after[kept] == before[kept]) kept++;
		size_t want = first_change(prev, &s.frames[f]);
		CHECK(kept == want, "frame %zu: kept %zu widgets, want the %zu before the first change", f, kept, want);
		for (size_t i = kept; i < nbefore; i++)
			for (size_t j = kept; j < n; j++) CHECK(after[j] != before[i], "frame %zu: a widget replaced is back", f);
		replaced += n - kept;
		shown += n;
		memcpy(before, after, n * sizeof *after);
		nbefore = n;
		// Once the heading is whole and the next block has begun, part of it
		// is selected, as a reader would while the rest streams in.
		if (!selected && n >= 2 && v->blocks[0].heading == 1) {
			selected = v->blocks[0].widget;
			select(selected, 2, 6);
		}
	}
	CHECK(selected != NULL, "nothing selected");
	int a = -1, b = -1;
	CHECK(selected && before[0] == selected && selection(selected, &a, &b) && a == 2 && b == 6,
	      "the selection in the first block: [%d, %d), the same widget: %d", a, b, before[0] == selected);
	CHECK(replaced * 4 < shown, "replaced %zu widgets of the %zu shown frame by frame", replaced, shown);

	// At the end, what a view given it all at once shows.
	markdown_Document *whole = markdown_make_Document((markdown_Document_events){0});
	whole->vt->set_paragraphs(whole, s.frames[s.n - 1]);
	document *w = (document *)whole;
	CHECK(w->n == v->n, "%zu blocks, streamed %zu", w->n, v->n);
	for (size_t i = 0; i < w->n && i < v->n; i++) CHECK(same(&w->blocks[i], &v->blocks[i]), "block %zu: %s, streamed %s", i, w->blocks[i].markup, v->blocks[i].markup);
	CHECK(v->blocks[0].heading == 1 && strstr(label_text(v->blocks[0].widget), "Moving the cache"), "the heading: %s", label_text(v->blocks[0].widget));
	float mw, mh;
	d->vt->measure(d, 400, NAN, &mw, &mh);
	CHECK(mw == 400 && mh > 200, "measured %gx%g", mw, mh);
	printf("markdown (linux): %zu frames, %zu widgets replaced of %zu shown\n", s.n, replaced, shown);

	whole->vt->destroy(whole);
	d->vt->destroy(d);
	stream_free(&s);
	return failed ? 1 : 0;
}
