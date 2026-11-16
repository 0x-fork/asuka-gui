// split.c: a SplitView is libadwaita's AdwNavigationSplitView, as GNOME's
// split apps are: a sidebar page (its own header bar, with the sidebar's
// title, over a .navigation-sidebar GtkListBox of the destinations, their
// sections' titles over them) beside a content page holding the detail
// Stack under the window's header bar (moved there, so the detail's back
// button, title and bar buttons are over the detail). Under 500 points
// wide it collapses into one page at a time. Choosing a destination sends
// Selected.
#include "host.h"

#include <stdlib.h>

#define SPLIT_COLLAPSE_WIDTH 500
#define A11Y_PROP_LABEL 4

void lx_set_a11y_string(gpointer w, int prop, const char *s);
const char *lx_badged(const char *title, const char *badge);

// A section's title or a destination (its index among all of them).
typedef struct {
	char *title, *icon, *badge;
	bool group;
	int index;
} sidebar_row;

// A row's widgets, which change in place.
typedef struct {
	GtkWidget *row, *box, *icon, *title, *badge;
} sidebar_parts;

typedef struct {
	AdwNavigationSplitView *view;
	AdwNavigationPage *side_page, *content_page;
	AdwToolbarView *content;
	AdwWindowTitle *title;
	GtkWidget *box;
	AK_VEC(sidebar_row) rows;
	AK_VEC(sidebar_parts) parts;
	int selected;
	bool updating;
	node *detail;
} split_t;

// Where each window's header bar is: in its own toolbar view, or a split
// view's content page's.
typedef struct {
	win *w;
	AdwToolbarView *home;
} header_home;
static AK_VEC(header_home) homes;

static split_t *split_of(node *n) { return n->kind == AK_KIND_SPLIT ? n->ext : NULL; }

static void rows_free(split_t *s) {
	for (uint32_t i = 0; i < s->rows.len; i++) {
		free(s->rows.data[i].title);
		free(s->rows.data[i].icon);
		free(s->rows.data[i].badge);
	}
	s->rows.len = 0;
}

static void show_selection(split_t *s);
static void sidebar_chose(node *n, GtkWidget *row);

static void on_row_selected(GtkListBox *b, GtkWidget *row, node *n) {
	(void)b;
	split_t *s = n->ext;
	if (s->updating || !row) return;
	sidebar_chose(n, row);
}

static void on_row_activated(GtkListBox *b, GtkWidget *row, node *n) {
	(void)b;
	(void)row;
	split_t *s = n->ext;
	// Collapsed, the chosen destination (even the one shown) shows.
	if (adw_navigation_split_view_get_collapsed(s->view)) adw_navigation_split_view_set_show_content(s->view, 1);
}

static void free_split(node *n);

static bool make_split(node *n) {
	if (n->kind != AK_KIND_SPLIT) return false;
	split_t *s = calloc(1, sizeof *s);
	if (!s) abort();
	s->selected = -1;
	n->ext = s;
	n->ext_free = free_split;
	s->view = (AdwNavigationSplitView *)adw_navigation_split_view_new();
	adw_navigation_split_view_set_min_sidebar_width(s->view, 200);
	adw_navigation_split_view_set_max_sidebar_width(s->view, 280);
	AdwToolbarView *side = (AdwToolbarView *)adw_toolbar_view_new();
	GtkWidget *bar = adw_header_bar_new();
	s->title = (AdwWindowTitle *)adw_window_title_new("", "");
	adw_header_bar_set_title_widget((AdwHeaderBar *)bar, (GtkWidget *)s->title);
	adw_toolbar_view_add_top_bar(side, bar);
	s->box = gtk_list_box_new();
	gtk_widget_add_css_class(s->box, "navigation-sidebar");
	gtk_list_box_set_selection_mode((GtkListBox *)s->box, 1 /* GTK_SELECTION_SINGLE */);
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy((GtkScrolledWindow *)scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child((GtkScrolledWindow *)scroll, s->box);
	adw_toolbar_view_set_content(side, scroll);
	s->side_page = adw_navigation_page_new((GtkWidget *)side, "");
	s->content = (AdwToolbarView *)adw_toolbar_view_new();
	s->content_page = adw_navigation_page_new((GtkWidget *)s->content, "");
	adw_navigation_split_view_set_sidebar(s->view, s->side_page);
	adw_navigation_split_view_set_content(s->view, s->content_page);
	lx_set_inner(n, (GtkWidget *)s->view);
	lx_connect(n, s->box, "row-selected", G_CALLBACK(on_row_selected));
	lx_connect(n, s->box, "row-activated", G_CALLBACK(on_row_activated));
	return true;
}

static void show_badge(split_t *s, uint32_t i, const sidebar_row *r) {
	sidebar_parts *p = &s->parts.data[i];
	if (str_empty(r->badge) && p->badge) {
		gtk_box_remove((GtkBox *)p->box, p->badge);
		p->badge = NULL;
	} else if (!str_empty(r->badge) && !p->badge) {
		p->badge = gtk_label_new(r->badge);
		gtk_widget_add_css_class(p->badge, "dim-label");
		gtk_widget_add_css_class(p->badge, "numeric");
		gtk_box_append((GtkBox *)p->box, p->badge);
	} else if (!str_empty(r->badge)) {
		gtk_label_set_text((GtkLabel *)p->badge, r->badge);
	}
	lx_set_a11y_string(p->row, A11Y_PROP_LABEL, lx_badged(r->title, r->badge));
}

// fill makes the list box's rows again: a section's title (not
// selectable), a destination's icon, title and badge.
static void fill(split_t *s) {
	s->updating = true;
	for (uint32_t i = 0; i < s->parts.len; i++) gtk_list_box_remove((GtkListBox *)s->box, s->parts.data[i].row);
	s->parts.len = 0;
	for (uint32_t i = 0; i < s->rows.len; i++) {
		sidebar_row *r = &s->rows.data[i];
		GtkWidget *row = gtk_list_box_row_new();
		sidebar_parts parts = {.row = row};
		if (r->group) {
			GtkWidget *l = gtk_label_new(r->title);
			gtk_label_set_xalign((GtkLabel *)l, 0);
			gtk_widget_add_css_class(l, "caption-heading");
			gtk_widget_add_css_class(l, "dim-label");
			if (i > 0) gtk_widget_set_margin_top(l, 12);
			gtk_widget_set_margin_start(l, 6);
			gtk_list_box_row_set_child((GtkListBoxRow *)row, l);
			gtk_list_box_row_set_selectable((GtkListBoxRow *)row, 0);
			gtk_list_box_row_set_activatable((GtkListBoxRow *)row, 0);
			gtk_widget_set_focusable(row, 0);
			parts.title = l;
		} else {
			GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
			parts.box = box;
			const char *name = lx_icon_name(S(r->icon));
			if (*name) {
				parts.icon = gtk_image_new_from_icon_name(name);
				gtk_box_append((GtkBox *)box, parts.icon);
			}
			GtkWidget *l = gtk_label_new(r->title);
			gtk_label_set_xalign((GtkLabel *)l, 0);
			gtk_label_set_ellipsize((GtkLabel *)l, PANGO_ELLIPSIZE_END);
			gtk_widget_set_hexpand(l, 1);
			gtk_box_append((GtkBox *)box, l);
			parts.title = l;
			gtk_list_box_row_set_child((GtkListBoxRow *)row, box);
		}
		gtk_list_box_append((GtkListBox *)s->box, row);
		ak_vec_push(&s->parts, parts);
		if (!r->group) show_badge(s, i, r);
	}
	s->updating = false;
	show_selection(s);
}

// show_selection selects the chosen destination's row (none: no row),
// and titles the content page with it.
static void show_selection(split_t *s) {
	s->updating = true;
	bool found = false;
	for (uint32_t r = 0; r < s->rows.len; r++) {
		sidebar_row *row = &s->rows.data[r];
		if (!row->group && row->index == s->selected && r < s->parts.len) {
			if ((GtkWidget *)gtk_list_box_get_selected_row((GtkListBox *)s->box) != s->parts.data[r].row)
				gtk_list_box_select_row((GtkListBox *)s->box, (GtkListBoxRow *)s->parts.data[r].row);
			adw_navigation_page_set_title(s->content_page, S(row->title));
			found = true;
			break;
		}
	}
	if (!found) gtk_list_box_unselect_all((GtkListBox *)s->box);
	s->updating = false;
}

static void sidebar_chose(node *n, GtkWidget *row) {
	split_t *s = n->ext;
	for (uint32_t r = 0; r < s->parts.len; r++) {
		if (s->parts.data[r].row != row || r >= s->rows.len || s->rows.data[r].group) continue;
		adw_navigation_page_set_title(s->content_page, S(s->rows.data[r].title));
		if (adw_navigation_split_view_get_collapsed(s->view)) adw_navigation_split_view_set_show_content(s->view, 1);
		int i = s->rows.data[r].index;
		if (i != s->selected) {
			s->selected = i;
			lx_emit(n, (ak_event){.kind = AK_EVENT_SELECTED, .i = i});
		}
		return;
	}
}

static bool split_prop(node *n, ak_prop p, const ak_value *v) {
	split_t *s = split_of(n);
	if (!s) return false;
	switch (p) {
	case AK_PROP_SPLIT_ITEMS: {
		ak_sidebar_heading_vec secs = {0};
		ak_sidebar_item_vec items = {0};
		ak_decode_sidebar((const uint8_t *)v->s.p, v->s.len, &secs, &items);
		AK_VEC(sidebar_row) rows = {0};
		int flat = 0;
		for (uint32_t i = 0; i < secs.len; i++) {
			if (secs.data[i].title.len) {
				sidebar_row r = {str_dup(secs.data[i].title), NULL, NULL, true, 0};
				ak_vec_push(&rows, r);
			}
			for (uint32_t k = 0; k < secs.data[i].count; k++) {
				ak_sidebar_item *it = &items.data[secs.data[i].first + k];
				sidebar_row r = {str_dup(it->title), str_dup(it->icon), str_dup(it->badge), false, flat++};
				ak_vec_push(&rows, r);
			}
		}
		ak_vec_free(&secs);
		ak_vec_free(&items);
		bool same = rows.len == s->rows.len && s->parts.len == rows.len;
		for (uint32_t i = 0; same && i < rows.len; i++)
			same = rows.data[i].group == s->rows.data[i].group && rows.data[i].index == s->rows.data[i].index;
		if (same) {
			// As many sections and items: the rows that changed change in
			// place, keeping the selection, the focus and the scroll.
			for (uint32_t i = 0; i < rows.len; i++) {
				sidebar_row *a = &s->rows.data[i], *b = &rows.data[i];
				sidebar_parts *pt = &s->parts.data[i];
				gtk_label_set_text((GtkLabel *)pt->title, b->title);
				if (!b->group) {
					if (!str_eq(a->icon, b->icon)) {
						if (pt->icon) {
							gtk_box_remove((GtkBox *)pt->box, pt->icon);
							pt->icon = NULL;
						}
						const char *name = lx_icon_name(S(b->icon));
						if (*name) {
							pt->icon = gtk_image_new_from_icon_name(name);
							gtk_box_prepend((GtkBox *)pt->box, pt->icon);
						}
					}
					show_badge(s, i, b);
				}
			}
			rows_free(s);
			for (uint32_t i = 0; i < rows.len; i++) ak_vec_push(&s->rows, rows.data[i]);
			ak_vec_free(&rows);
			show_selection(s);
			return true;
		}
		rows_free(s);
		for (uint32_t i = 0; i < rows.len; i++) ak_vec_push(&s->rows, rows.data[i]);
		ak_vec_free(&rows);
		fill(s);
		return true;
	}
	case AK_PROP_SPLIT_SELECTED:
		s->selected = (int)v->i;
		show_selection(s);
		return true;
	case AK_PROP_SPLIT_TITLE: {
		char *t = str_dup(v->s);
		adw_window_title_set_title(s->title, t);
		adw_navigation_page_set_title(s->side_page, t);
		free(t);
		return true;
	}
	}
	return false;
}

static void badges_of(node *n, void *ctx) {
	(void)ctx;
	split_t *s = split_of(n);
	if (!s) return;
	for (uint32_t i = 0; i < s->rows.len && i < s->parts.len; i++)
		if (!s->rows.data[i].group) show_badge(s, i, &s->rows.data[i]);
}

void lx_split_badges_all(void);
void lx_split_badges_all(void) { lx_each_node(badges_of, NULL); }

// lx_insert_split puts the detail Stack in the content page, under the
// window's header bar.
void lx_insert_split(node *p, node *c, uint32_t index) {
	split_t *s = p->ext;
	lx_add_child(p, c, index);
	if (!c->widget) return;
	s->detail = c;
	lx_detach(c->widget);
	adw_toolbar_view_set_content(s->content, c->widget);
	lx_queue_chrome();
}

void lx_remove_split(node *p, node *c) {
	split_t *s = p->ext;
	if (s->detail == c) {
		s->detail = NULL;
		adw_toolbar_view_set_content(s->content, NULL);
	}
	lx_queue_chrome();
}

// split_frame: the detail is sized by its page, not the core; a split
// view narrower than 500 points collapses.
static bool split_frame(node *n, const float f[4]) {
	if (n->parent && n->parent->kind == AK_KIND_SPLIT) return true;
	split_t *s = split_of(n);
	if (s) {
		bool collapsed = f[2] < SPLIT_COLLAPSE_WIDTH;
		if (collapsed != (bool)adw_navigation_split_view_get_collapsed(s->view)) adw_navigation_split_view_set_collapsed(s->view, collapsed);
	}
	return false;
}

// lx_place_header puts a window's header bar over the split view's
// content on screen in it (sp), or back at its top (NULL).
void lx_place_header(win *w, node *sp) {
	AdwToolbarView *home = w->toolbar;
	uint32_t at = UINT32_MAX;
	for (uint32_t i = 0; i < homes.len; i++)
		if (homes.data[i].w == w) {
			home = homes.data[i].home;
			at = i;
		}
	AdwToolbarView *target = w->toolbar;
	if (sp && !sp->dead && node_get(sp->id) == sp && split_of(sp)) target = ((split_t *)sp->ext)->content;
	if (home == target) return;
	g_object_ref(w->header);
	adw_toolbar_view_remove(home, (GtkWidget *)w->header);
	adw_toolbar_view_add_top_bar(target, (GtkWidget *)w->header);
	g_object_unref(w->header);
	if (target == w->toolbar) {
		if (at != UINT32_MAX) homes.data[at] = homes.data[--homes.len];
	} else if (at != UINT32_MAX) {
		homes.data[at].home = target;
	} else {
		header_home h = {w, target};
		ak_vec_push(&homes, h);
	}
}

// free_split gives the window its header bar back, and lets go of the
// detail, as the split view goes.
static void free_split(node *n) {
	split_t *s = n->ext;
	for (uint32_t i = homes.len; i-- > 0;)
		if (homes.data[i].home == s->content) lx_place_header(homes.data[i].w, NULL);
	if (s->detail) {
		adw_toolbar_view_set_content(s->content, NULL);
		s->detail = NULL;
	}
	rows_free(s);
	ak_vec_free(&s->rows);
	ak_vec_free(&s->parts);
	free(s);
	n->ext = NULL;
}

const area lx_area_split = {.name = "split", .create = make_split, .prop = split_prop, .frame = split_frame};
