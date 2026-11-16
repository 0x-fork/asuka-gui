// Segmented on Linux: GNOME's segmented control, toggle buttons in one
// group, linked in a box (the "linked" style class) and sharing its width
// equally. GTK's functions are asuka_linux.def's, as the host's are.
#include "asuka_linux.h"
#include "segmented.asuka.h"

#include <stdlib.h>

typedef struct view view;

// A button's handler data: its view and its index.
typedef struct {
	view *v;
	int64_t index;
} segment;

struct view {
	segmented_Segmented base;
	segmented_Segmented_events events;
	int refs;  // the view's, and each button's handler's
	GtkWidget **buttons;
	size_t n;
	int64_t selected;
	bool setting;  // GTK emits toggled for what the code sets too
	bool destroyed;
};

static void unref(view *v) {
	if (--v->refs == 0) {
		free(v->buttons);
		free(v);
	}
}

static void segment_free(gpointer data, GClosure *closure) {
	(void)closure;
	segment *s = data;
	unref(s->v);
	free(s);
}

static void toggled(GtkToggleButton *b, gpointer data) {
	segment *s = data;
	view *v = s->v;
	if (v->destroyed || v->setting || !G(gtk_toggle_button_get_active)(b)) return;
	v->selected = s->index;
	segmented_Segmented_changed(v->events, s->index);
}

static void select_now(view *v) {
	if (v->selected >= 0 && (size_t)v->selected < v->n) {
		v->setting = true;
		G(gtk_toggle_button_set_active)((GtkToggleButton *)v->buttons[v->selected], true);
		v->setting = false;
	}
}

static void set_items(segmented_Segmented *self, segmented_list_string items) {
	view *v = (view *)self;
	GtkWidget *box = v->base.widget;
	for (size_t i = 0; i < v->n; i++) G(gtk_box_remove)((GtkBox *)box, v->buttons[i]);
	free(v->buttons);
	v->n = 0;
	v->buttons = calloc(items.len ? items.len : 1, sizeof *v->buttons);
	if (!v->buttons) return;
	for (size_t i = 0; i < items.len; i++) {
		GtkWidget *b = G(gtk_toggle_button_new)();
		G(gtk_button_set_label)((GtkButton *)b, items.data[i]);
		if (i > 0) G(gtk_toggle_button_set_group)((GtkToggleButton *)b, (GtkToggleButton *)v->buttons[0]);
		segment *s = malloc(sizeof *s);
		if (s) {
			s->v = v;
			s->index = (int64_t)i;
			v->refs++;
			G(g_signal_connect_data)(b, "toggled", (GCallback)toggled, s, segment_free, 0);
		}
		G(gtk_box_append)((GtkBox *)box, b);
		v->buttons[v->n++] = b;
	}
	select_now(v);
}

static void set_selected(segmented_Segmented *self, int64_t selected) {
	view *v = (view *)self;
	v->selected = selected;
	select_now(v);
}

static void destroy(segmented_Segmented *self) {
	view *v = (view *)self;
	v->destroyed = true;
	unref(v);
}

static const segmented_Segmented_vtable vtable = {
	.destroy = destroy,
	.set_items = set_items,
	.set_selected = set_selected,
};

segmented_Segmented *segmented_make_Segmented(segmented_Segmented_events events) {
	if (!asuka_linux()) return NULL;
	view *v = calloc(1, sizeof *v);
	if (!v) return NULL;
	v->base.vt = &vtable;
	v->events = events;
	v->refs = 1;
	GtkWidget *box = G(gtk_box_new)(GTK_ORIENTATION_HORIZONTAL, 0);
	G(gtk_widget_add_css_class)(box, "linked");
	G(gtk_box_set_homogeneous)((GtkBox *)box, true);
	v->base.widget = box;
	return &v->base;
}
