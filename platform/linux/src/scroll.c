// scroll.c: scroll views, pull to refresh, page controls and paging.
//
// A Scroll is a GtkScrolledWindow (kinetic scrolling, overlay scroll bars,
// overshoot) around a viewport whose child is the content node's view, as
// big as the content's frame.
//
// Pull to refresh (OnRefresh), the desktop's way, as on Windows: F5 or
// Ctrl+R refreshes the refreshable Scroll or List the focus is in, or
// else the innermost one on screen in the window. While it refreshes, a
// spinner floats over the top of its content, on a card, until Refreshing
// goes false.
//
// GTK has no page control that stands alone: a PageControl is drawn by
// its view as libadwaita's carousel dots are (the current one larger and
// darker, in the text color), measured, and clicks, the arrow keys and a
// screen reader's slider change the page.
//
// A paging Scroll hides its scroll bars and settles on whole pages: a
// wheel's notch moves a page; a touchpad's or a touchscreen's swipe
// scrolls with the fingers, then settles on the page it was going to (the
// next one past a quick flick, else the nearest), one page a swipe; the
// arrow keys, Page Up and Page Down move a page, Home and End to the
// first and last. Each settling sends Paged.
#include "host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void lx_set_role(gpointer w, const char *nick);
void lx_set_a11y_string(gpointer w, int prop, const char *s);
bool lx_list_refreshable(node *n);
bool lx_list_refresh(node *n);

enum {
	KEY_LEFT = 0xff51, KEY_UP = 0xff52, KEY_RIGHT = 0xff53, KEY_DOWN = 0xff54,
	KEY_PAGE_UP = 0xff55, KEY_PAGE_DOWN = 0xff56, KEY_HOME = 0xff50, KEY_END = 0xff57,
	KEY_KP_LEFT = 0xff96, KEY_KP_UP = 0xff97, KEY_KP_RIGHT = 0xff98, KEY_KP_DOWN = 0xff99,
};

typedef struct {
	node *n;
	GtkEventController *wheel, *keys;
	double sum;          // a wheel's notches, as they add up
	bool touchpad;       // a touchpad's scroll is under way
	double velocity;     // points a second along the axis
	double last_pos, last_t;
	int start_page;
	bool moving;
	guint settle_t, tick;
	double anim_to, anim_from, anim_start;
	bool animating, self_moving;
	int paged;  // the page last reported
} paging;

// ---- Scroll views ----

static void scrolled(GtkAdjustment *a, node *n) {
	(void)a;
	scroll_view *s = n->scroll;
	double x = gtk_adjustment_get_value(s->hadj), y = gtk_adjustment_get_value(s->vadj);
	if (s->reported[0] == x && s->reported[1] == y) return;
	s->reported[0] = x;
	s->reported[1] = y;
	lx_emit(n, (ak_event){.kind = AK_EVENT_SCROLLED, .x = x, .y = y});
	lx_scroll_moved(n);
}

void lx_make_scroll(node *n, bool horizontal) {
	scroll_view *s = n->scroll;
	if (!s) {
		s = calloc(1, sizeof *s);
		if (!s) abort();
		n->scroll = s;
	}
	s->horizontal = horizontal;
	if (!s->sw) {
		s->sw = gtk_scrolled_window_new();
		s->viewport = gtk_viewport_new(NULL, NULL);
		gtk_viewport_set_scroll_to_focus((GtkViewport *)s->viewport, 1);
		gtk_scrolled_window_set_child((GtkScrolledWindow *)s->sw, s->viewport);
		lx_set_inner(n, s->sw);
		s->hadj = gtk_scrolled_window_get_hadjustment((GtkScrolledWindow *)s->sw);
		s->vadj = gtk_scrolled_window_get_vadjustment((GtkScrolledWindow *)s->sw);
		lx_connect(n, s->hadj, "value-changed", G_CALLBACK(scrolled));
		lx_connect(n, s->vadj, "value-changed", G_CALLBACK(scrolled));
	}
	if (s->pager) return;  // paging keeps its policy
	if (horizontal)
		gtk_scrolled_window_set_policy((GtkScrolledWindow *)s->sw, GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	else
		gtk_scrolled_window_set_policy((GtkScrolledWindow *)s->sw, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
}

// lx_scroll_set_content makes c's view the scrolled content (NULL: none).
void lx_scroll_set_content(node *n, node *c) {
	scroll_view *s = n->scroll;
	if (s->content == c) return;
	s->content = c;
	if (!c) {
		gtk_viewport_set_child((GtkViewport *)s->viewport, NULL);
		return;
	}
	lx_detach(c->widget);
	gtk_viewport_set_child((GtkViewport *)s->viewport, c->widget);
}

static double clamp_adj(GtkAdjustment *adj, double v) {
	double hi = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
	if (hi < 0) hi = 0;
	double lo = gtk_adjustment_get_lower(adj);
	return fmin(fmax(v, lo), hi);
}

// lx_scroll_to scrolls to an offset, clamped to the content.
void lx_scroll_to(node *n, double x, double y, bool animated) {
	(void)animated;
	scroll_view *s = n->scroll;
	if (!s) return;
	gtk_adjustment_set_value(s->hadj, clamp_adj(s->hadj, x));
	gtk_adjustment_set_value(s->vadj, clamp_adj(s->vadj, y));
}

// ---- Pull to refresh ----

typedef struct {
	bool refreshing;
	GtkEventController *shortcut;
	GtkWidget *spinner;  // the card with the spinner, over the scroll view
} refresher;

static node_vec refreshers;  // the refreshable Scrolls

static bool refresh_key(node *from);

static gboolean on_refresh_key(GtkWidget *w, GVariant *args, gpointer data) {
	(void)w;
	(void)args;
	node *n = node_get((ak_id)(uintptr_t)data);
	return n && refresh_key(n);
}

static refresher *refresher_of(node *n) { return n->scroll ? (refresher *)n->scroll->spinner_state : NULL; }

void lx_set_refreshing(node *n, bool on) {
	refresher *r = refresher_of(n);
	if (!r || r->refreshing == on || !n->widget) return;
	r->refreshing = on;
	if (on && !r->spinner) {
		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_add_css_class(box, "card");
		GtkWidget *sp;
		if (adw_spinner_new) {
			sp = adw_spinner_new();
		} else {
			sp = gtk_spinner_new();
			gtk_spinner_set_spinning((GtkSpinner *)sp, 1);
		}
		gtk_widget_set_size_request(sp, 24, 24);
		gtk_widget_set_margin_top(sp, 8);
		gtk_widget_set_margin_bottom(sp, 8);
		gtk_widget_set_margin_start(sp, 8);
		gtk_widget_set_margin_end(sp, 8);
		gtk_box_append((GtkBox *)box, sp);
		gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(box, GTK_ALIGN_START);
		gtk_widget_set_margin_top(box, 12);
		gtk_widget_set_can_target(box, 0);
		gtk_widget_set_parent(box, n->widget);  // after the scrolled window: over it
		r->spinner = box;
	}
	if (r->spinner) {
		gtk_widget_set_visible(r->spinner, on);
		if (!adw_spinner_new) gtk_spinner_set_spinning((GtkSpinner *)gtk_widget_get_first_child(r->spinner), on);
	}
}

static void set_refreshable(node *n, bool on) {
	refresher *r = refresher_of(n);
	if (!on) {
		if (r) {
			lx_set_refreshing(n, false);
			if (r->shortcut && n->widget) gtk_widget_remove_controller(n->widget, r->shortcut);
			if (r->spinner) gtk_widget_unparent(r->spinner);
			free(r);
			n->scroll->spinner_state = NULL;
			for (uint32_t i = 0; i < refreshers.len; i++)
				if (refreshers.data[i] == n) refreshers.data[i] = refreshers.data[--refreshers.len];
		}
		return;
	}
	if (r || !n->widget) return;
	r = calloc(1, sizeof *r);
	if (!r) abort();
	n->scroll->spinner_state = r;
	ak_vec_push(&refreshers, n);
	GtkEventController *sc = gtk_shortcut_controller_new();
	gtk_shortcut_controller_set_scope((GtkShortcutController *)sc, GTK_SHORTCUT_SCOPE_GLOBAL);  // anywhere in its window
	static const char *const keys[] = {"F5", "<Control>r"};
	for (size_t i = 0; i < AK_LEN(keys); i++) {
		GtkShortcutAction *act = gtk_callback_action_new(on_refresh_key, (gpointer)(uintptr_t)n->id, NULL);
		gtk_shortcut_controller_add_shortcut((GtkShortcutController *)sc, gtk_shortcut_new(gtk_shortcut_trigger_parse_string(keys[i]), act));
	}
	gtk_widget_add_controller(n->widget, sc);
	r->shortcut = sc;
}

static bool is_refreshable(node *n) { return refresher_of(n) != NULL || lx_list_refreshable(n); }

// start_refresh refreshes n, as the user asked: Refresh, the spinner shown
// until the core says it's done.
static bool start_refresh(node *n) {
	if (n->kind == AK_KIND_LIST) return lx_list_refresh(n);
	refresher *r = refresher_of(n);
	if (!r || r->refreshing || !listening(n, AK_EVENT_REFRESH)) return false;
	lx_set_refreshing(n, true);
	lx_emit(n, (ak_event){.kind = AK_EVENT_REFRESH});
	return true;
}

typedef struct {
	GtkRoot *root;
	node *best;
	int depth;
} refresh_ctx;

static void deepest(node *n, void *data) {
	refresh_ctx *c = data;
	if (!is_refreshable(n) || !n->widget || !gtk_widget_get_mapped(n->widget) || gtk_widget_get_root(n->widget) != c->root) return;
	int d = 0;
	for (GtkWidget *w = gtk_widget_get_parent(n->widget); w; w = gtk_widget_get_parent(w)) d++;
	if (d > c->depth) {
		c->best = n;
		c->depth = d;
	}
}

// refresh_key is F5 or Ctrl+R in from's window: it refreshes the
// refreshable view the focus is in, or the innermost one on screen.
static bool refresh_key(node *from) {
	if (!from->widget) return false;
	GtkRoot *root = gtk_widget_get_root(from->widget);
	if (!root) return false;
	for (GtkWidget *f = gtk_window_get_focus((GtkWindow *)root); f; f = gtk_widget_get_parent(f)) {
		node *n = lx_view_node(f);
		if (n && is_refreshable(n)) return start_refresh(n);
	}
	refresh_ctx c = {root, NULL, -1};
	lx_each_node(deepest, &c);
	return c.best && start_refresh(c.best);
}

// lx_refresh_key lets a List's own shortcut take F5 the same way.
bool lx_refresh_from(node *from);
bool lx_refresh_from(node *from) { return refresh_key(from); }

// ---- Paging scrolls ----

static void page_metrics(node *n, double *pos, double *size, int *pages) {
	scroll_view *s = n->scroll;
	GtkAdjustment *adj = s->horizontal ? s->hadj : s->vadj;
	*pos = gtk_adjustment_get_value(adj);
	*size = gtk_adjustment_get_page_size(adj);
	*pages = 0;
	if (*size <= 0) return;
	int p = (int)ceil(gtk_adjustment_get_upper(adj) / *size - 1e-3);
	*pages = p > 1 ? p : 1;
}

static int current_page(node *n, paging *p) {
	double pos, size;
	int pages;
	page_metrics(n, &pos, &size, &pages);
	if (size <= 0) return 0;
	if (p->animating) pos = p->anim_to;
	int page = (int)lround(pos / size);
	return page < 0 ? 0 : page > pages - 1 ? pages - 1 : page;
}

static void stop_animation(paging *p) {
	if (p->tick && p->n->scroll) gtk_widget_remove_tick_callback(p->n->scroll->sw, p->tick);
	p->tick = 0;
	p->animating = false;
}

static void paging_stop(paging *p) {
	if (p->settle_t) g_source_remove(p->settle_t);
	p->settle_t = 0;
	stop_animation(p);
}

static gboolean scroll_tick(GtkWidget *w, GdkFrameClock *clock, gpointer data) {
	(void)w;
	(void)clock;
	paging *p = data;
	if (!p->animating) {
		p->tick = 0;
		return 0;
	}
	scroll_view *s = p->n->scroll;
	GtkAdjustment *adj = s->horizontal ? s->hadj : s->vadj;
	double t = fmin((lx_now() - p->anim_start) / 0.25, 1);
	double e = 1 - pow(1 - t, 3);
	p->self_moving = true;
	gtk_adjustment_set_value(adj, p->anim_from + (p->anim_to - p->anim_from) * e);
	p->self_moving = false;
	if (t >= 1) {
		p->animating = false;
		p->tick = 0;
		return 0;
	}
	return 1;
}

// animate_scroll moves the scroll to an offset along its axis, easing out
// over a quarter second on its frame clock (at once with Reduce Motion).
static void animate_scroll(paging *p, double to) {
	scroll_view *s = p->n->scroll;
	GtkAdjustment *adj = s->horizontal ? s->hadj : s->vadj;
	double from = gtk_adjustment_get_value(adj);
	stop_animation(p);
	if (from == to || H.env.reduce_motion || !gtk_widget_get_mapped(s->sw)) {
		p->self_moving = true;
		gtk_adjustment_set_value(adj, to);
		p->self_moving = false;
		return;
	}
	p->animating = true;
	p->anim_to = to;
	p->anim_from = from;
	p->anim_start = lx_now();
	p->tick = gtk_widget_add_tick_callback(s->sw, scroll_tick, p, NULL);
}

static void go_to_page(paging *p, int page) {
	double pos, size;
	int pages;
	page_metrics(p->n, &pos, &size, &pages);
	if (size <= 0) return;
	if (page > pages - 1) page = pages - 1;
	if (page < 0) page = 0;
	p->moving = false;
	animate_scroll(p, page * size);
	if (page != p->paged) {
		p->paged = page;
		lx_emit(p->n, (ak_event){.kind = AK_EVENT_PAGED, .i = page});
	}
}

static void begin_swipe(paging *p) {
	stop_animation(p);
	p->start_page = current_page(p->n, p);
	p->moving = true;
	p->velocity = 0;
	p->last_t = 0;
}

// settle moves a scroll that came to rest to the page it was going to:
// past a quick flick the next one, else the nearest; one page a swipe.
static void settle(paging *p) {
	if (!p->moving) return;
	p->moving = p->touchpad = false;
	double pos, size;
	int pages;
	page_metrics(p->n, &pos, &size, &pages);
	if (size <= 0) return;
	double at = pos / size;
	int page = (int)lround(at);
	const double flick = 300;  // points a second
	if (p->velocity > flick)
		page = (int)ceil(at - 1e-3);
	else if (p->velocity < -flick)
		page = (int)floor(at + 1e-3);
	int lo = p->start_page - 1 > 0 ? p->start_page - 1 : 0;
	int hi = p->start_page + 1 < pages - 1 ? p->start_page + 1 : pages - 1;
	if (page < lo) page = lo;
	if (page > hi) page = hi;
	go_to_page(p, page);
}

static gboolean settle_now(gpointer data) {
	paging *p = data;
	p->settle_t = 0;
	settle(p);
	return 0;
}

static void settle_soon(paging *p, guint ms) {
	if (p->settle_t) g_source_remove(p->settle_t);
	p->settle_t = ms ? g_timeout_add(ms, settle_now, p) : g_idle_add_full(0, settle_now, p, NULL);
}

static gboolean paging_scroll(GtkEventControllerScroll *c, double dx, double dy, gpointer data) {
	paging *p = data;
	scroll_view *s = p->n->scroll;
	if (gtk_event_controller_scroll_get_unit(c) == GDK_SCROLL_UNIT_WHEEL) {
		// Along its axis: a horizontal one takes a tilted wheel, or Shift
		// and the wheel; the plain wheel goes on to a scroll around it.
		double d = dy;
		if (s->horizontal) {
			d = dx;
			if (d == 0 && (gtk_event_controller_get_current_event_state((GtkEventController *)c) & GDK_SHIFT_MASK)) d = dy;
		}
		if (d == 0) return 0;
		p->sum += d;
		if (fabs(p->sum) < 1) return 1;
		int step = p->sum < 0 ? -1 : 1;
		p->sum = 0;
		go_to_page(p, current_page(p->n, p) + step);
		return 1;
	}
	double along = s->horizontal ? dx : dy;
	if (along == 0) return 0;  // across it: for a scroll around it
	if (!p->touchpad) {
		p->touchpad = true;
		begin_swipe(p);
	}
	settle_soon(p, 150);  // X11 sends no end of a touchpad's scroll
	return 0;             // GTK scrolls it with the fingers
}

static void paging_scroll_end(GtkEventControllerScroll *c, gpointer data) {
	(void)c;
	paging *p = data;
	if (p->touchpad) settle_soon(p, 0);
}

static void paging_decelerate(GtkEventControllerScroll *c, double vx, double vy, gpointer data) {
	(void)c;
	paging *p = data;
	p->velocity = p->n->scroll->horizontal ? vx : vy;
	settle_soon(p, 0);
}

// focus_edits reports whether the focus in n's window is in a text it
// edits, which keeps the keys.
static bool focus_edits(node *n) {
	GtkRoot *root = gtk_widget_get_root(n->widget);
	GtkWidget *f = root ? gtk_window_get_focus((GtkWindow *)root) : NULL;
	return f && (AK_IS(f, gtk_editable_get_type) || AK_IS(f, gtk_text_view_get_type));
}

static gboolean paging_key(GtkEventControllerKey *k, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
	(void)k;
	(void)keycode;
	paging *p = data;
	node *n = p->n;
	if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) || focus_edits(n)) return 0;
	bool h = n->scroll->horizontal;
	int step = 0;
	switch (keyval) {
	case KEY_RIGHT: case KEY_KP_RIGHT: step = h ? 1 : 0; break;
	case KEY_LEFT: case KEY_KP_LEFT: step = h ? -1 : 0; break;
	case KEY_DOWN: case KEY_KP_DOWN: step = h ? 0 : 1; break;
	case KEY_UP: case KEY_KP_UP: step = h ? 0 : -1; break;
	case KEY_PAGE_DOWN: step = 1; break;
	case KEY_PAGE_UP: step = -1; break;
	case KEY_HOME:
		go_to_page(p, 0);
		return 1;
	case KEY_END: {
		double pos, size;
		int pages;
		page_metrics(n, &pos, &size, &pages);
		go_to_page(p, pages - 1);
		return 1;
	}
	}
	if (!step) return 0;
	go_to_page(p, current_page(n, p) + step);
	return 1;
}

// paging_moved follows the scroll as it moves (a touchscreen's drag, a
// scroll bar's, a key's): it settles once it rests.
static void paging_moved(paging *p) {
	if (p->self_moving) return;
	double pos, size;
	int pages;
	page_metrics(p->n, &pos, &size, &pages);
	double now = lx_now();
	if (!p->moving) {
		p->animating = false;
		begin_swipe(p);
	} else if (p->last_t > 0 && now - p->last_t > 0) {
		double dt = now - p->last_t;
		double v = dt > 0.1 ? 0 : (pos - p->last_pos) / dt;
		p->velocity = 0.6 * v + 0.4 * p->velocity;
	}
	p->last_pos = pos;
	p->last_t = now;
	if (!p->touchpad) settle_soon(p, 120);
}

void lx_set_paging(node *n, bool on) {
	scroll_view *s = n->scroll;
	paging *p = s->pager;
	if (!on) {
		if (p) {
			paging_stop(p);
			gtk_widget_remove_controller(s->sw, p->wheel);
			gtk_widget_remove_controller(s->sw, p->keys);
			free(p);
			s->pager = NULL;
		}
		gtk_scrolled_window_set_kinetic_scrolling((GtkScrolledWindow *)s->sw, 1);
		lx_make_scroll(n, s->horizontal);
		return;
	}
	if (p) return;
	p = calloc(1, sizeof *p);
	if (!p) abort();
	p->n = n;
	p->paged = -1;
	s->pager = p;
	// Scrollable, without a scroll bar; no momentum of its own: the host
	// settles each swipe.
	if (s->horizontal)
		gtk_scrolled_window_set_policy((GtkScrolledWindow *)s->sw, GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
	else
		gtk_scrolled_window_set_policy((GtkScrolledWindow *)s->sw, GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);
	gtk_scrolled_window_set_kinetic_scrolling((GtkScrolledWindow *)s->sw, 0);
	GtkEventController *c = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES | GTK_EVENT_CONTROLLER_SCROLL_KINETIC);
	p->wheel = c;
	gtk_event_controller_set_propagation_phase(c, GTK_PHASE_CAPTURE);
	g_signal_connect(c, "scroll", G_CALLBACK(paging_scroll), p);
	g_signal_connect(c, "scroll-end", G_CALLBACK(paging_scroll_end), p);
	g_signal_connect(c, "decelerate", G_CALLBACK(paging_decelerate), p);
	gtk_widget_add_controller(s->sw, c);
	GtkEventController *k = gtk_event_controller_key_new();
	p->keys = k;
	gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
	g_signal_connect(k, "key-pressed", G_CALLBACK(paging_key), p);
	gtk_widget_add_controller(s->sw, k);
}

bool lx_set_scroll_prop(node *n, ak_prop p, const ak_value *v) {
	switch (p) {
	case AK_PROP_SCROLL_HORIZONTAL:
		lx_make_scroll(n, v->i != 0);
		if (n->scroll->pager) {
			lx_set_paging(n, false);
			lx_set_paging(n, true);
		}
		return true;
	case AK_PROP_SCROLL_REFRESHABLE:
		set_refreshable(n, v->i != 0);
		return true;
	case AK_PROP_SCROLL_REFRESHING:
		lx_set_refreshing(n, v->i != 0);
		return true;
	case AK_PROP_SCROLL_PAGING:
		lx_set_paging(n, v->i != 0);
		return true;
	}
	return lx_set_own_more(n, p, v);
}

void lx_release_scroll(node *n) {
	scroll_view *s = n->scroll;
	if (!s) return;
	if (s->pager) {
		paging_stop(s->pager);
		free(s->pager);
	}
	if (s->spinner_state) {
		free(s->spinner_state);
		for (uint32_t i = 0; i < refreshers.len; i++)
			if (refreshers.data[i] == n) refreshers.data[i] = refreshers.data[--refreshers.len];
	}
	free(s);
	n->scroll = NULL;
}

// ---- Page controls ----

#define DOT_SIZE 6.0f  // a dot's diameter
#define DOT_ON 8.0f    // the current one's
#define DOT_CELL 13.0f  // a dot and the gap to the next
#define DOTS_HIGH 24.0f
#define DOTS_ALPHA 0.3f  // the others' opacity, the current one's 0.9

typedef struct {
	int count, page;
} page_dots;

static page_dots *dots_of(node *n) { return n->kind == AK_KIND_PAGE_CONTROL ? n->ext : NULL; }

static void dots_changed(node *n, page_dots *d) {
	if (d->count > 0 && n->widget) {
		// A screen reader reads it as a slider: "page 2 of 4".
		char t[32];
		snprintf(t, sizeof t, "%d / %d", d->page + 1, d->count);
		lx_set_a11y_string(n->widget, 18, t);  // GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT
	}
	lx_queue_draw(n);
}

static int dot_at(node *n, page_dots *d, float x) {
	if (!d->count) return -1;
	float w, h;
	lx_size(n, n->widget, &w, &h);
	int i = (int)floorf((x - (w - d->count * DOT_CELL) / 2) / DOT_CELL);
	if (n->rtl) i = d->count - 1 - i;
	return i < 0 ? 0 : i > d->count - 1 ? d->count - 1 : i;
}

static void choose_dot(node *n, int i) {
	page_dots *d = dots_of(n);
	if (!d || i < 0 || i >= d->count || i == d->page) return;
	d->page = i;
	dots_changed(n, d);
	lx_emit(n, (ak_event){.kind = AK_EVENT_SELECTED, .i = i});
}

static void on_dot_click(GtkGestureClick *g, int count, double x, double y, node *n) {
	(void)g;
	(void)count;
	(void)y;
	page_dots *d = dots_of(n);
	if (d) choose_dot(n, dot_at(n, d, (float)x));
}

static gboolean on_dot_key(GtkEventControllerKey *k, guint keyval, guint keycode, GdkModifierType state, node *n) {
	(void)k;
	(void)keycode;
	(void)state;
	page_dots *d = dots_of(n);
	if (!d) return 0;
	int next = n->rtl ? -1 : 1, prev = -next;
	switch (keyval) {
	case KEY_RIGHT: case KEY_KP_RIGHT: choose_dot(n, d->page + next); return 1;
	case KEY_LEFT: case KEY_KP_LEFT: choose_dot(n, d->page + prev); return 1;
	case KEY_UP: case KEY_KP_UP: case KEY_PAGE_UP: choose_dot(n, d->page + 1); return 1;
	case KEY_DOWN: case KEY_KP_DOWN: case KEY_PAGE_DOWN: choose_dot(n, d->page - 1); return 1;
	case KEY_HOME: choose_dot(n, 0); return 1;
	case KEY_END: choose_dot(n, d->count - 1); return 1;
	}
	return 0;
}

static void free_dots(node *n) {
	free(n->ext);
	n->ext = NULL;
}

static bool dots_create(node *n) {
	if (n->kind != AK_KIND_PAGE_CONTROL) return false;
	page_dots *d = calloc(1, sizeof *d);
	if (!d) abort();
	n->ext = d;
	n->ext_free = free_dots;
	GtkWidget *w = n->widget;
	lx_set_role(w, "slider");
	gtk_widget_set_focusable(w, 1);
	gtk_widget_set_focus_on_click(w, 0);
	gtk_widget_add_css_class(w, "asuka-focus-ring");
	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button((GtkGestureSingle *)click, 1);
	lx_connect(n, click, "pressed", G_CALLBACK(on_dot_click));
	gtk_widget_add_controller(w, (GtkEventController *)click);
	GtkEventController *keys = gtk_event_controller_key_new();
	lx_connect(n, keys, "key-pressed", G_CALLBACK(on_dot_key));
	gtk_widget_add_controller(w, keys);
	return true;
}

static bool dots_prop(node *n, ak_prop p, const ak_value *v) {
	page_dots *d = dots_of(n);
	if (!d) return false;
	switch (p) {
	case AK_PROP_PAGE_CONTROL_COUNT:
		d->count = v->i > 0 ? (int)v->i : 0;
		break;
	case AK_PROP_PAGE_CONTROL_PAGE:
		d->page = (int)v->i;
		break;
	default:
		return false;
	}
	dots_changed(n, d);
	return true;
}

static bool dots_measure(node *n, float cw, float ch, float aw, float ah, float *ow, float *oh) {
	(void)cw;
	(void)ch;
	(void)aw;
	(void)ah;
	page_dots *d = dots_of(n);
	if (!d) return false;
	*ow = d->count * DOT_CELL + DOT_CELL;
	*oh = DOTS_HIGH;
	return true;
}

static bool dots_draw(node *n, GtkSnapshot *snap, const graphene_rect_t *b) {
	page_dots *d = dots_of(n);
	if (!d) return false;
	GdkRGBA c = H.theme.fg, t;
	if (n->color && lx_resolve(n->color, &t)) c = t;
	float x0 = (b->size.width - d->count * DOT_CELL) / 2;
	for (int i = 0; i < d->count; i++) {
		int slot = n->rtl ? d->count - 1 - i : i;
		float size = DOT_SIZE;
		GdkRGBA col = c;
		col.alpha *= DOTS_ALPHA;
		if (i == d->page) {
			size = DOT_ON;
			col = c;
			col.alpha *= 0.9f;
		}
		float cx = x0 + (slot + 0.5f) * DOT_CELL;
		graphene_rect_t r = {{cx - size / 2, b->size.height / 2 - size / 2}, {size, size}};
		GskRoundedRect rr = {.bounds = r};
		for (int k = 0; k < 4; k++) rr.corner[k] = (graphene_size_t){size / 2, size / 2};
		gtk_snapshot_push_rounded_clip(snap, &rr);
		gtk_snapshot_append_color(snap, &col, &r);
		gtk_snapshot_pop(snap);
	}
	return true;
}

static void scroll_scrolled(node *n) {
	if (n->scroll && n->scroll->pager) paging_moved(n->scroll->pager);
}

static void scroll_release(node *n) { lx_release_scroll(n); }

const area lx_area_scroll = {
	.name = "scroll",
	.create = dots_create,
	.prop = dots_prop,
	.measure = dots_measure,
	.draw = dots_draw,
	.scrolled = scroll_scrolled,
	.release = scroll_release,
};
