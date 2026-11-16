// asuka/markdown on Linux: the paragraphs the core read, a widget a block
// in a vertical GtkBox, as GNOME apps show rich text: a GtkLabel of Pango
// markup that wraps and is selectable (links GTK opens, the theme's font
// and its text scaling), a heading's with the heading role and its level;
// a list item's marker in a label of its own beside it; a table's row
// its cells' labels, as wide as each other; a rule a GtkSeparator.
//
// A block's widget stays while what it shows does: a change (a reply
// streaming in) replaces the blocks from the first that differs, so the
// ones before keep their widgets, their layout and their selection.
#include "asuka_linux.h"
#include "markdown.asuka.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct {
	bool tried, ok;
	GType (*label_type)(void);
	GtkWidget *(*box_new)(int orientation, int spacing);
	void (*box_append)(GtkWidget *box, GtkWidget *child);
	void (*box_remove)(GtkWidget *box, GtkWidget *child);
	void (*box_set_homogeneous)(GtkWidget *box, gboolean);
	GtkWidget *(*separator_new)(int orientation);
	void (*set_wrap)(GtkWidget *, gboolean);
	void (*set_wrap_mode)(GtkWidget *, int);
	void (*set_selectable)(GtkWidget *, gboolean);
	void (*set_xalign)(GtkWidget *, float);
	void (*set_yalign)(GtkWidget *, float);
	void (*set_markup)(GtkWidget *, const char *);
	void (*set_margin_top)(GtkWidget *, int);
	void (*set_margin_start)(GtkWidget *, int);
	void (*set_size_request)(GtkWidget *, int, int);
	void (*set_hexpand)(GtkWidget *, gboolean);
	void (*update_property)(GtkWidget *, ...);
	void (*measure)(GtkWidget *, int orientation, int for_size, int *min, int *nat, int *min_base, int *nat_base);
	gpointer (*type_class_ref)(GType);
	struct GEnumValue_ {
		int value;
		const char *name, *nick;
	} *(*enum_by_nick)(gpointer cls, const char *nick);
	GType (*role_type)(void);
	GType (*property_type)(void);
	int heading_role, level_property;
} gk;

static int enum_value(GType (*type)(void), const char *nick, int def) {
	if (!type) return def;
	struct GEnumValue_ *v = gk.enum_by_nick(gk.type_class_ref(type()), nick);
	return v ? v->value : def;
}

static bool load(void) {
	if (gk.tried) return gk.ok;
	gk.tried = true;
	if (!asuka_linux()) return false;
	*(void **)&gk.label_type = asuka_linux_sym("gtk_label_get_type");
	*(void **)&gk.box_new = asuka_linux_sym("gtk_box_new");
	*(void **)&gk.box_append = asuka_linux_sym("gtk_box_append");
	*(void **)&gk.box_remove = asuka_linux_sym("gtk_box_remove");
	*(void **)&gk.box_set_homogeneous = asuka_linux_sym("gtk_box_set_homogeneous");
	*(void **)&gk.separator_new = asuka_linux_sym("gtk_separator_new");
	*(void **)&gk.set_wrap = asuka_linux_sym("gtk_label_set_wrap");
	*(void **)&gk.set_wrap_mode = asuka_linux_sym("gtk_label_set_wrap_mode");
	*(void **)&gk.set_selectable = asuka_linux_sym("gtk_label_set_selectable");
	*(void **)&gk.set_xalign = asuka_linux_sym("gtk_label_set_xalign");
	*(void **)&gk.set_yalign = asuka_linux_sym("gtk_label_set_yalign");
	*(void **)&gk.set_markup = asuka_linux_sym("gtk_label_set_markup");
	*(void **)&gk.set_margin_top = asuka_linux_sym("gtk_widget_set_margin_top");
	*(void **)&gk.set_margin_start = asuka_linux_sym("gtk_widget_set_margin_start");
	*(void **)&gk.set_size_request = asuka_linux_sym("gtk_widget_set_size_request");
	*(void **)&gk.set_hexpand = asuka_linux_sym("gtk_widget_set_hexpand");
	*(void **)&gk.update_property = asuka_linux_sym("gtk_accessible_update_property");
	*(void **)&gk.measure = asuka_linux_sym("gtk_widget_measure");
	*(void **)&gk.type_class_ref = asuka_linux_sym("g_type_class_ref");
	*(void **)&gk.enum_by_nick = asuka_linux_sym("g_enum_get_value_by_nick");
	*(void **)&gk.role_type = asuka_linux_sym("gtk_accessible_role_get_type");
	*(void **)&gk.property_type = asuka_linux_sym("gtk_accessible_property_get_type");
	gk.ok = gk.label_type && gk.box_new && gk.box_append && gk.box_remove && gk.box_set_homogeneous && gk.separator_new && gk.set_wrap &&
	        gk.set_wrap_mode && gk.set_selectable && gk.set_xalign && gk.set_yalign && gk.set_markup && gk.set_margin_top &&
	        gk.set_margin_start && gk.set_size_request && gk.set_hexpand && gk.measure && gk.type_class_ref && gk.enum_by_nick;
	if (gk.ok) {
		gk.heading_role = enum_value(gk.role_type, "heading", -1);
		gk.level_property = enum_value(gk.property_type, "level", -1);
	}
	return gk.ok;
}

enum { VERTICAL = 1, HORIZONTAL = 0 };

// What a block shows: its markup (a row's cells' apart by tabs, a rule's
// none), its kind and level, its marker and indent, its space above. Two
// blocks that say the same show the same.
typedef struct {
	char *markup;
	char *marker;  // "": none
	int heading;   // 1 to 6 (0: not a heading)
	int indent;    // in steps
	int top;       // the space above it, in pixels
	bool rule, row;
	GtkWidget *widget;  // ours (a reference)
} block;

typedef struct {
	markdown_Document base;
	block *blocks;
	size_t n, cap;
} document;

static void put(asuka_writer *w, const char *s) { asuka_writer_put(w, s, strlen(s)); }

// escape writes s[0:n] with Pango markup's specials escaped.
static void escape(asuka_writer *w, const char *s, size_t n) {
	for (size_t i = 0; i < n; i++) {
		switch (s[i]) {
		case '&': put(w, "&amp;"); break;
		case '<': put(w, "&lt;"); break;
		case '>': put(w, "&gt;"); break;
		case '"': put(w, "&quot;"); break;
		case '\'': put(w, "&#39;"); break;
		default: asuka_writer_put(w, s + i, 1);
		}
	}
}

static const char *mono_span = "<span font_family=\"monospace\" bgalpha=\"12%\" background=\"#808080\">";

// run writes a run's text in its style.
static void run(asuka_writer *w, const markdown_Run *r, bool mono) {
	if (*r->link) {
		put(w, "<a href=\"");
		escape(w, r->link, strlen(r->link));
		put(w, "\">");
	}
	if (r->quoted) put(w, "<span fgalpha=\"65%\">");
	if (r->struck) put(w, "<s>");
	if (r->italic) put(w, "<i>");
	if (r->bold) put(w, "<b>");
	if (mono) put(w, mono_span);
	escape(w, r->text, strlen(r->text));
	if (mono) put(w, "</span>");
	if (r->bold) put(w, "</b>");
	if (r->italic) put(w, "</i>");
	if (r->struck) put(w, "</s>");
	if (r->quoted) put(w, "</span>");
	if (*r->link) put(w, "</a>");
}

static const char *heading_sizes[] = {"xx-large", "x-large", "large", "medium", "medium", "medium"};

// make is what paragraph i shows (prev: the one before it, or NULL).
static block make(const markdown_Paragraph *p, const markdown_Paragraph *prev) {
	block b = {.marker = asuka_strdup(p->marker), .indent = (int)p->indent, .rule = p->rule, .row = p->row};
	b.heading = p->heading < 0 ? 0 : p->heading > 6 ? 6 : (int)p->heading;
	b.top = !prev ? 0 : prev->tight && p->tight ? (p->row ? 0 : 4) : 12;
	asuka_writer w = {0};
	if (b.heading > 0) {
		put(&w, "<span size=\"");
		put(&w, heading_sizes[b.heading - 1]);
		put(&w, "\" weight=\"bold\">");
	}
	if (p->code) put(&w, mono_span);
	for (size_t j = 0; j < p->runs.len; j++) run(&w, &p->runs.data[j], p->runs.data[j].code && !p->code);  // a code block's span is the whole of it
	if (p->code) put(&w, "</span>");
	if (b.heading > 0) put(&w, "</span>");
	asuka_writer_u8(&w, 0);
	b.markup = (char *)w.data;
	return b;
}

static bool same(const block *a, const block *b) {
	return strcmp(a->markup, b->markup) == 0 && strcmp(a->marker, b->marker) == 0 && a->heading == b->heading && a->indent == b->indent &&
	       a->top == b->top && a->rule == b->rule && a->row == b->row;
}

static GtkWidget *label(const char *markup, int heading) {
	GtkWidget *l;
	if (heading > 0 && gk.heading_role >= 0) {
		l = G(g_object_new)(gk.label_type(), "accessible-role", gk.heading_role, NULL);
		if (gk.update_property && gk.level_property >= 0) gk.update_property(l, gk.level_property, heading, -1);
	} else {
		l = G(g_object_new)(gk.label_type(), NULL);
	}
	gk.set_wrap(l, true);
	gk.set_wrap_mode(l, 2);  // PANGO_WRAP_WORD_CHAR
	gk.set_selectable(l, true);
	gk.set_xalign(l, 0);
	gk.set_yalign(l, 0);
	gk.set_markup(l, markup);
	gk.set_hexpand(l, true);
	return l;
}

// step is a list's indent, in pixels.
enum { STEP = 24 };

// widget makes a block's widget.
static GtkWidget *widget(const block *b) {
	GtkWidget *w;
	if (b->rule) {
		w = gk.separator_new(HORIZONTAL);
		gk.set_margin_top(w, b->top + 6);
	} else if (b->row) {
		// Its cells: the markup's parts between tabs.
		w = gk.box_new(HORIZONTAL, 12);
		gk.box_set_homogeneous(w, true);
		const char *s = b->markup;
		for (;;) {
			const char *tab = strchr(s, '\t');
			char *cell = asuka_strndup(s, tab ? (size_t)(tab - s) : strlen(s));
			gk.box_append(w, label(cell, 0));
			free(cell);
			if (!tab) break;
			s = tab + 1;
		}
	} else if (*b->marker) {
		w = gk.box_new(HORIZONTAL, 0);
		asuka_writer m = {0};
		put(&m, "<span fgalpha=\"65%\">");
		escape(&m, b->marker, strlen(b->marker));
		put(&m, "</span>");
		asuka_writer_u8(&m, 0);
		GtkWidget *ml = label((const char *)m.data, 0);
		asuka_writer_free(&m);
		gk.set_selectable(ml, false);
		gk.set_hexpand(ml, false);
		gk.set_size_request(ml, STEP, -1);
		gk.box_append(w, ml);
		gk.box_append(w, label(b->markup, 0));
	} else {
		w = label(b->markup, b->heading);
	}
	// A marker sits in the step before its text; other blocks in lists and
	// quotes line up with that text.
	int steps = *b->marker ? b->indent - 1 : b->indent;
	gk.set_margin_start(w, STEP * (steps > 0 ? steps : 0));
	if (!b->rule) gk.set_margin_top(w, b->top);
	G(g_object_ref_sink)(w);  // ours until it goes; the box holds its own
	return w;
}

static void drop(document *v, block *b) {
	gk.box_remove(v->base.widget, b->widget);
	G(g_object_unref)(b->widget);
	free(b->markup);
	free(b->marker);
}

static void set_paragraphs(markdown_Document *self, markdown_list_Paragraph d) {
	document *v = (document *)self;
	block *next = calloc(d.len ? d.len : 1, sizeof *next);
	for (size_t i = 0; i < d.len; i++) next[i] = make(&d.data[i], i > 0 ? &d.data[i - 1] : NULL);
	// The blocks from the first that differs go; the rest stay as they are.
	size_t keep = 0;
	while (keep < v->n && keep < d.len && same(&v->blocks[keep], &next[keep])) keep++;
	for (size_t i = keep; i < v->n; i++) drop(v, &v->blocks[i]);
	if (d.len > v->cap) {
		v->cap = d.len;
		v->blocks = realloc(v->blocks, v->cap * sizeof *v->blocks);
	}
	for (size_t i = 0; i < keep; i++) {
		free(next[i].markup);
		free(next[i].marker);
	}
	for (size_t i = keep; i < d.len; i++) {
		v->blocks[i] = next[i];
		v->blocks[i].widget = widget(&next[i]);
		gk.box_append(v->base.widget, v->blocks[i].widget);
	}
	v->n = d.len;
	free(next);
}

// measure is its height at the width it's given (its natural width when
// it's given none).
static bool measure(markdown_Document *self, float width, float height, float *ow, float *oh) {
	int mn = 0, nat = 0;
	if (isnan(width)) {
		gk.measure(self->widget, HORIZONTAL, -1, &mn, &nat, NULL, NULL);
		width = (float)nat;
	}
	gk.measure(self->widget, VERTICAL, (int)width, &mn, &nat, NULL, NULL);
	*ow = width;
	*oh = (float)nat;
	return true;
}

static void destroy(markdown_Document *self) {
	document *v = (document *)self;
	for (size_t i = 0; i < v->n; i++) drop(v, &v->blocks[i]);
	free(v->blocks);
	G(g_object_unref)(v->base.widget);
	free(v);
}

static const markdown_Document_vtable vt = {.destroy = destroy, .set_paragraphs = set_paragraphs, .measure = measure};

markdown_Document *markdown_make_Document(markdown_Document_events events) {
	if (!load()) return NULL;
	document *v = calloc(1, sizeof *v);
	v->base.vt = &vt;
	v->base.widget = gk.box_new(VERTICAL, 0);
	G(g_object_ref_sink)(v->base.widget);  // ours until destroy; the host's parent holds its own
	return &v->base;
}
