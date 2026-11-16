// popover.c: popovers and keyboard accessories.
//
// A Popover is a GtkPopover pointing at its anchor's view, below it (GTK
// moves it where there's room), its content the popover node's view at
// the size the core gives it (Width, Height), a layout root of its own.
// It closes on a click outside or Escape, reported as Dismissed; while
// ui.OnBack in it intercepts, Escape asks the core (Back) instead.
//
// An Accessory (a field's bar over an on-screen keyboard) is made and
// never shown: a desktop has no on-screen keyboard.
#include "host.h"

#include <stdlib.h>

void lx_key_controller(GtkWidget *native, win *w, node *pop);

typedef struct {
	GtkWidget *pop;
	ak_id anchor;
	float w, h;
	bool closing;  // the core closes it: no Dismissed
	gulong map_id;
	GtkWidget *waiting;  // the anchor whose map it waits for
} popover;

static popover *pop_of(node *n) { return n->kind == AK_KIND_POPOVER ? n->ext : NULL; }

static void stop_waiting(popover *po) {
	if (po->waiting) {
		g_signal_handler_disconnect(po->waiting, po->map_id);
		g_object_unref(po->waiting);
		po->waiting = NULL;
	}
}

static void free_popover(node *n) {
	stop_waiting(n->ext);
	free(n->ext);
	n->ext = NULL;
}

static bool pop_create(node *n) {
	switch (n->kind) {
	case AK_KIND_POPOVER: {
		if (!n->nav) {
			n->nav = calloc(1, sizeof *n->nav);
			if (!n->nav) abort();
		}
		popover *po = calloc(1, sizeof *po);
		if (!po) abort();
		n->ext = po;
		n->ext_free = free_popover;
		return true;
	}
	case AK_KIND_ACCESSORY:
		return true;
	default:
		return false;
	}
}

static void size_it(node *n, popover *po) {
	if (po->w > 0 && po->h > 0 && n->widget) gtk_widget_set_size_request(n->widget, lx_window_size(po->w), lx_window_size(po->h));
}

static bool pop_prop(node *n, ak_prop p, const ak_value *v) {
	popover *po = pop_of(n);
	if (!po) return false;
	switch (p) {
	case AK_PROP_POPOVER_ANCHOR: po->anchor = (ak_id)v->i; break;
	case AK_PROP_POPOVER_WIDTH: po->w = (float)v->f; break;
	case AK_PROP_POPOVER_HEIGHT: po->h = (float)v->f; break;
	default: return false;
	}
	size_it(n, po);
	return true;
}

// lx_popover_anchor is the node a popover points at (NULL: none).
node *lx_popover_anchor(node *n) {
	popover *po = pop_of(n);
	return po ? node_get(po->anchor) : NULL;
}

static gboolean on_pop_key(GtkEventControllerKey *k, guint keyval, guint keycode, GdkModifierType state, node *n) {
	(void)k;
	(void)keycode;
	(void)state;
	popover *po = pop_of(n);
	if (keyval == 0xff1b && n->intercept_back && po && !po->closing) {  // Escape
		lx_enter_begin();
		lx_emit(n, (ak_event){.kind = AK_EVENT_BACK});
		lx_enter_end();
		return 1;
	}
	return 0;
}

static void on_pop_closed(GtkPopover *pop, node *n) {
	(void)pop;
	popover *po = pop_of(n);
	if (!po || po->closing) return;
	po->closing = true;
	lx_enter_begin();
	lx_emit(n, (ak_event){.kind = AK_EVENT_DISMISSED});
	lx_enter_end();
}

static void show_popover(node *n);

static gboolean show_later(gpointer data) {
	node *n = node_get((ak_id)(uintptr_t)data);
	if (n && pop_of(n)) show_popover(n);
	return 0;
}

static void on_anchor_mapped(GtkWidget *w, gpointer data) {
	(void)w;
	node *n = node_get((ak_id)(uintptr_t)data);
	if (!n || !pop_of(n)) return;
	stop_waiting(pop_of(n));
	g_idle_add_full(G_PRIORITY_DEFAULT, show_later, data, NULL);
}

static void show_popover(node *n) {
	popover *po = pop_of(n);
	node *a = node_get(po->anchor);
	if (po->pop || !a || !a->widget || !n->widget || n->dead) return;
	if (!gtk_widget_get_mapped(a->widget)) {
		// Not on screen yet (the window shows after the first batch): once
		// it is.
		stop_waiting(po);
		po->waiting = g_object_ref(a->widget);
		po->map_id = g_signal_connect(a->widget, "map", G_CALLBACK(on_anchor_mapped), (gpointer)(uintptr_t)n->id);
		return;
	}
	GtkWidget *pop = gtk_popover_new();
	g_object_ref_sink(pop);  // the host's, until it's unparented
	po->pop = pop;
	size_it(n, po);
	lx_detach(n->widget);
	gtk_popover_set_child((GtkPopover *)pop, n->widget);
	gtk_popover_set_position((GtkPopover *)pop, GTK_POS_BOTTOM);
	gtk_widget_set_parent(pop, a->widget);
	lx_key_controller(pop, NULL, n);
	GtkEventController *k = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
	lx_connect(n, k, "key-pressed", G_CALLBACK(on_pop_key));
	gtk_widget_add_controller(pop, k);
	lx_connect(n, pop, "closed", G_CALLBACK(on_pop_closed));
	gtk_popover_popup((GtkPopover *)pop);
}

static void present_later(void *ctx) {
	node *n = node_get((ak_id)(uintptr_t)ctx);
	if (n && pop_of(n)) show_popover(n);
}

// lx_present_popover shows a popover after the batch: its anchor, made
// with it, isn't laid out yet.
void lx_present_popover(node *n) {
	if (pop_of(n)) lx_after_core(present_later, (void *)(uintptr_t)n->id);
}

static gboolean unparent_later(gpointer data) {
	GtkWidget *pop = data;
	if (gtk_widget_get_parent(pop)) gtk_widget_unparent(pop);
	g_object_unref(pop);
	return 0;
}

// release closes a popover the core destroyed (closing it) or the user
// dismissed; it's unparented once GTK is done with it.
static void pop_release(node *n) {
	popover *po = pop_of(n);
	if (!po || !po->pop) return;
	GtkWidget *pop = po->pop;
	po->pop = NULL;
	g_signal_handlers_disconnect_matched(pop, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, n);
	if (!po->closing) {
		po->closing = true;
		gtk_popover_popdown((GtkPopover *)pop);
	}
	gtk_popover_set_child((GtkPopover *)pop, NULL);
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, unparent_later, pop, NULL);
}

const area lx_area_popover = {
	.name = "popover",
	.create = pop_create,
	.prop = pop_prop,
	.release = pop_release,
};
