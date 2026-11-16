// asuka/markdown on Linux: the paragraphs the core read as Pango markup
// in a GtkLabel that wraps and is selectable: one selection, links GTK
// opens, the theme's font and its text scaling.
#include "asuka_linux.h"
#include "markdown.asuka.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct {
	bool tried, ok;
	GtkWidget *(*label_new)(const char *);
	void (*set_wrap)(GtkWidget *, gboolean);
	void (*set_wrap_mode)(GtkWidget *, int);
	void (*set_selectable)(GtkWidget *, gboolean);
	void (*set_xalign)(GtkWidget *, float);
	void (*set_yalign)(GtkWidget *, float);
	void (*set_markup)(GtkWidget *, const char *);
	void (*queue_resize)(GtkWidget *);
	void (*measure)(GtkWidget *, int orientation, int for_size, int *min, int *nat, int *min_base, int *nat_base);
} gk;

static bool load(void) {
	if (gk.tried) return gk.ok;
	gk.tried = true;
	if (!asuka_linux()) return false;
	*(void **)&gk.label_new = asuka_linux_sym("gtk_label_new");
	*(void **)&gk.set_wrap = asuka_linux_sym("gtk_label_set_wrap");
	*(void **)&gk.set_wrap_mode = asuka_linux_sym("gtk_label_set_wrap_mode");
	*(void **)&gk.set_selectable = asuka_linux_sym("gtk_label_set_selectable");
	*(void **)&gk.set_xalign = asuka_linux_sym("gtk_label_set_xalign");
	*(void **)&gk.set_yalign = asuka_linux_sym("gtk_label_set_yalign");
	*(void **)&gk.set_markup = asuka_linux_sym("gtk_label_set_markup");
	*(void **)&gk.queue_resize = asuka_linux_sym("gtk_widget_queue_resize");
	*(void **)&gk.measure = asuka_linux_sym("gtk_widget_measure");
	gk.ok = gk.label_new && gk.set_wrap && gk.set_wrap_mode && gk.set_selectable && gk.set_xalign && gk.set_yalign && gk.set_markup &&
	        gk.queue_resize && gk.measure;
	return gk.ok;
}

typedef struct {
	markdown_Document base;
	char *markup;  // what the label shows
} document;

// escape writes s with Pango markup's specials escaped.
static void escape(asuka_writer *w, const char *s) {
	for (; *s; s++) {
		switch (*s) {
		case '&': asuka_writer_put(w, "&amp;", 5); break;
		case '<': asuka_writer_put(w, "&lt;", 4); break;
		case '>': asuka_writer_put(w, "&gt;", 4); break;
		case '"': asuka_writer_put(w, "&quot;", 6); break;
		case '\'': asuka_writer_put(w, "&#39;", 5); break;
		default: asuka_writer_put(w, s, 1);
		}
	}
}

static void put(asuka_writer *w, const char *s) { asuka_writer_put(w, s, strlen(s)); }

static void indent(asuka_writer *w, int n) {
	for (int i = 0; i < n; i++) put(w, "    ");
}

static const char *mono_span = "<span font_family=\"monospace\" bgalpha=\"12%\" background=\"#808080\">";

// run writes a run's text in its style; in a code block, each of its
// lines indented as the block is.
static void run(asuka_writer *w, const markdown_Paragraph *p, const markdown_Run *r, bool mono) {
	if (*r->link) {
		put(w, "<a href=\"");
		escape(w, r->link);
		put(w, "\">");
	}
	if (r->quoted) put(w, "<span fgalpha=\"65%\">");
	if (r->struck) put(w, "<s>");
	if (r->italic) put(w, "<i>");
	if (r->bold) put(w, "<b>");
	if (mono) put(w, mono_span);
	if (p->code) {
		const char *s = r->text;
		for (const char *nl; (nl = strchr(s, '\n')); s = nl + 1) {
			char *line = asuka_strndup(s, (size_t)(nl - s));
			escape(w, line);
			free(line);
			put(w, "\n");
			indent(w, (int)p->indent);
		}
		escape(w, s);
	} else {
		escape(w, r->text);
	}
	if (mono) put(w, "</span>");
	if (r->bold) put(w, "</b>");
	if (r->italic) put(w, "</i>");
	if (r->struck) put(w, "</s>");
	if (r->quoted) put(w, "</span>");
	if (*r->link) put(w, "</a>");
}

static const char *heading_sizes[] = {"xx-large", "x-large", "large", "medium", "medium", "medium"};

// pango is the paragraphs as Pango markup, of malloc's.
static char *pango(markdown_list_Paragraph d) {
	asuka_writer w = {0};
	for (size_t i = 0; i < d.len; i++) {
		const markdown_Paragraph *p = &d.data[i];
		if (i > 0) {
			put(&w, "\n");
			if (!d.data[i - 1].tight || !p->tight) put(&w, "\n");
		}
		indent(&w, p->indent > 1 ? (int)p->indent - 1 : 0);
		if (*p->marker) {
			escape(&w, p->marker);
			put(&w, " ");
		} else if (p->indent > 0) {
			put(&w, "    ");
		}
		if (p->rule) {
			put(&w, "<span fgalpha=\"35%\">");
			for (int k = 0; k < 24; k++) put(&w, "―");
			put(&w, "</span>");
			continue;
		}
		if (p->heading > 0) {
			char *h = asuka_sprintf("<span size=\"%s\" weight=\"bold\">", heading_sizes[(p->heading > 6 ? 6 : p->heading) - 1]);
			put(&w, h);
			free(h);
		}
		if (p->code) put(&w, mono_span);
		for (size_t j = 0; j < p->runs.len; j++) {
			const markdown_Run *r = &p->runs.data[j];
			run(&w, p, r, r->code && !p->code);  // a code block's span is the whole of it
		}
		if (p->code) put(&w, "</span>");
		if (p->heading > 0) put(&w, "</span>");
	}
	asuka_writer_u8(&w, 0);
	return (char *)w.data;
}

static void set_paragraphs(markdown_Document *self, markdown_list_Paragraph d) {
	document *v = (document *)self;
	char *markup = pango(d);
	if (v->markup && strcmp(v->markup, markup) == 0) {  // as it is: its selection kept
		free(markup);
		return;
	}
	free(v->markup);
	v->markup = markup;
	gk.set_markup(v->base.widget, markup);
	gk.queue_resize(v->base.widget);
}

// measure is its height at the width it's given (its natural width when
// it's given none).
static bool measure(markdown_Document *self, float width, float height, float *ow, float *oh) {
	int mn = 0, nat = 0;
	if (isnan(width)) {
		gk.measure(self->widget, 0, -1, &mn, &nat, NULL, NULL);  // GTK_ORIENTATION_HORIZONTAL
		width = (float)nat;
	}
	gk.measure(self->widget, 1, (int)width, &mn, &nat, NULL, NULL);
	*ow = width;
	*oh = (float)nat;
	return true;
}

static void destroy(markdown_Document *self) {
	document *v = (document *)self;
	G(g_object_unref)(v->base.widget);
	free(v->markup);
	free(v);
}

static const markdown_Document_vtable vt = {.destroy = destroy, .set_paragraphs = set_paragraphs, .measure = measure};

markdown_Document *markdown_make_Document(markdown_Document_events events) {
	if (!load()) return NULL;
	document *v = calloc(1, sizeof *v);
	v->base.vt = &vt;
	GtkWidget *l = gk.label_new("");
	G(g_object_ref_sink)(l);  // ours until destroy; the host's parent holds its own
	gk.set_wrap(l, true);
	gk.set_wrap_mode(l, 2);  // PANGO_WRAP_WORD_CHAR
	gk.set_selectable(l, true);
	gk.set_xalign(l, 0);
	gk.set_yalign(l, 0);
	v->base.widget = l;
	return &v->base;
}
