// a11y.c: screen readers (Orca) read GTK's accessibility tree over
// AT-SPI. A node's view takes its role (a pressable view a button, a
// header a heading), its label (A11yLabel; a text's text) and description
// (the hint), and is hidden with A11yHidden.
//
// A screen reader's actions on a view (A11yActions) and adjustable views
// (A11yAdjustable): GTK has no accessible actions of an app's own, but its
// AT-SPI bridge gives a widget that has action groups of its own the
// Action interface, listing their enabled actions without a parameter by
// their names. So a view's actions are an action group of its own,
// "action", each named after its title as GAction names may be ("action.
// Mark-as-read"): Orca lists them and does them. The view says it has a
// popup and its key ("Shift+F10"), and its actions are its context menu
// (a secondary click, a long press on a touchscreen, the Menu key,
// Shift+F10); the view is a tab stop. An adjustable view is a slider to
// screen readers and takes the arrow keys (up and the reading direction's
// forward one step up, the others down), each sending Adjust.
#include "host.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

enum {
	A11Y_PROP_DESCRIPTION = 1,
	A11Y_PROP_HAS_POPUP = 2,
	A11Y_PROP_KEY_SHORTCUTS = 3,
	A11Y_PROP_LABEL = 4,
	A11Y_STATE_HIDDEN = 4,
};

const char *lx_symbol_label(const char *symbol);
GtkWidget *lx_field_widget(node *n);
bool lx_view_is_pressable(node *n);

struct a11y_state {
	AK_VEC(char *) actions;
	AK_VEC(char *) names;  // the actions' GAction names
	bool adjustable;
};

// lx_set_role sets a view's accessible role by its nick, while GTK still
// lets it (until the accessibility context is realized).
void lx_set_role(gpointer w, const char *nick) {
	GType t = gtk_accessible_role_get_type();
	GEnumClass *c = g_type_class_ref(t);
	GEnumValue *v = c ? g_enum_get_value_by_nick(c, nick) : NULL;
	if (v) {
		GValue val = {0};
		g_value_init(&val, t);
		g_value_set_enum(&val, v->value);
		g_object_set_property(w, "accessible-role", &val);
		g_value_unset(&val);
	}
	if (c) g_type_class_unref(c);
}

void lx_set_a11y_string(gpointer w, int prop, const char *s) {
	GValue val = {0};
	g_value_init(&val, G_TYPE_STRING);
	g_value_set_string(&val, s);
	GtkAccessibleProperty p = (GtkAccessibleProperty)prop;
	gtk_accessible_update_property_value((GtkAccessible *)w, 1, &p, &val);
	g_value_unset(&val);
}

void lx_set_a11y(node *n, ak_prop p, const ak_value *v) {
	if (!n->widget) return;
	switch (p) {
	case AK_PROP_A11Y_HINT: {
		char *s = str_dup(v->s);
		lx_set_a11y_string(n->widget, A11Y_PROP_DESCRIPTION, s);
		free(s);
		break;
	}
	case AK_PROP_A11Y_ROLE: {
		const char *nick = NULL;
		switch (v->i) {
		case AK_ROLE_BUTTON: nick = "button"; break;
		case AK_ROLE_HEADER: nick = "heading"; break;
		case AK_ROLE_IMAGE: nick = "img"; break;
		case AK_ROLE_LINK: nick = "link"; break;
		case AK_ROLE_TEXT: nick = "label"; break;
		case AK_ROLE_SEARCH: nick = "search"; break;
		}
		if (nick) lx_set_role(n->widget, nick);
		break;
	}
	case AK_PROP_A11Y_HIDDEN: {
		GValue val = {0};
		g_value_init(&val, G_TYPE_BOOLEAN);
		g_value_set_boolean(&val, v->i != 0);
		GtkAccessibleState st = (GtkAccessibleState)A11Y_STATE_HIDDEN;
		gtk_accessible_update_state_value((GtkAccessible *)n->widget, 1, &st, &val);
		g_value_unset(&val);
		break;
	}
	}
}

// lx_update_a11y labels the native control, or the view for content the
// host draws.
void lx_update_a11y(node *n) {
	if (!n->widget) return;
	if (n->kind == AK_KIND_TEXT_FIELD || n->kind == AK_KIND_TOGGLE) {
		GtkWidget *w = n->kind == AK_KIND_TEXT_FIELD ? lx_field_widget(n) : n->inner;
		if (w) {
			if (str_empty(n->a11y_label))
				gtk_accessible_reset_property((GtkAccessible *)w, (GtkAccessibleProperty)A11Y_PROP_LABEL);
			else
				lx_set_a11y_string(w, A11Y_PROP_LABEL, n->a11y_label);
		}
		return;  // keep the control's native role; the wrapper has no label
	}
	const char *label = n->a11y_label;
	if (str_empty(label) && n->kind == AK_KIND_TEXT) return;  // the label inside says it
	if (str_empty(label) && n->kind == AK_KIND_SYMBOL) label = lx_symbol_label(n->symbol);
	if (!str_empty(label)) lx_set_a11y_string(n->widget, A11Y_PROP_LABEL, label);
	if (lx_view_is_pressable(n) && n->kind == AK_KIND_VIEW) lx_set_role(n->widget, "button");
}

// lx_announce has a screen reader read text out.
void lx_announce(ak_span text) {
	if (!gtk_accessible_announce || !H.main) return;
	char *t = str_dup(text);
	gtk_accessible_announce((GtkAccessible *)H.main->window, t, GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
	free(t);
}

// ---- Actions and adjustable views ----

static void free_names(a11y_state *a) {
	for (uint32_t i = 0; i < a->actions.len; i++) free(a->actions.data[i]);
	for (uint32_t i = 0; i < a->names.len; i++) free(a->names.data[i]);
	a->actions.len = a->names.len = 0;
}

void lx_release_a11y(node *n) {
	if (!n->a11y) return;
	free_names(n->a11y);
	ak_vec_free(&n->a11y->actions);
	ak_vec_free(&n->a11y->names);
	free(n->a11y);
	n->a11y = NULL;
}

static bool actions_menu(node *n, double x, double y);

static gboolean on_a11y_key(GtkEventControllerKey *k, guint keyval, guint keycode, GdkModifierType state, node *n) {
	(void)k;
	(void)keycode;
	a11y_state *a = n->a11y;
	if (a->adjustable && !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))) {
		int64_t by = 0;
		switch (keyval) {
		case 0xff52: case 0xff97: by = 1; break;   // up
		case 0xff54: case 0xff99: by = -1; break;  // down
		case 0xff53: case 0xff98: by = n->rtl ? -1 : 1; break;  // right
		case 0xff51: case 0xff96: by = n->rtl ? 1 : -1; break;  // left
		}
		if (by) {
			lx_emit(n, (ak_event){.kind = AK_EVENT_ADJUST, .i = by});
			return 1;
		}
	}
	if (keyval == 0xff67 || (keyval == 0xffc7 && (state & GDK_SHIFT_MASK))) return actions_menu(n, -1, -1);  // Menu, Shift+F10
	return 0;
}

static void on_a11y_secondary(GtkGestureClick *g, int count, double x, double y, node *n) {
	(void)count;
	if (actions_menu(n, x, y)) gtk_gesture_set_state((GtkGesture *)g, GTK_EVENT_SEQUENCE_CLAIMED);
}

static void on_a11y_long(GtkGestureLongPress *g, double x, double y, node *n) {
	(void)g;
	actions_menu(n, x, y);
}

// a11y_of is n's state, its controllers added.
static a11y_state *a11y_of(node *n) {
	if (n->a11y || !n->widget) {
		if (!n->a11y) n->a11y = calloc(1, sizeof *n->a11y);
		return n->a11y;
	}
	a11y_state *a = calloc(1, sizeof *a);
	if (!a) abort();
	n->a11y = a;
	GtkWidget *w = n->widget;
	gtk_widget_set_focusable(w, 1);
	gtk_widget_set_focus_on_click(w, 0);
	gtk_widget_add_css_class(w, "asuka-focus-ring");
	GtkEventController *keys = gtk_event_controller_key_new();
	lx_connect(n, keys, "key-pressed", G_CALLBACK(on_a11y_key));
	gtk_widget_add_controller(w, keys);
	GtkGesture *secondary = gtk_gesture_click_new();
	gtk_gesture_single_set_button((GtkGestureSingle *)secondary, 3);
	lx_connect(n, secondary, "pressed", G_CALLBACK(on_a11y_secondary));
	gtk_widget_add_controller(w, (GtkEventController *)secondary);
	GtkGesture *lng = gtk_gesture_long_press_new();
	gtk_gesture_single_set_touch_only((GtkGestureSingle *)lng, 1);
	lx_connect(n, lng, "pressed", G_CALLBACK(on_a11y_long));
	gtk_widget_add_controller(w, (GtkEventController *)lng);
	return a;
}

static gboolean unparent_later(gpointer w) {
	gtk_widget_unparent(w);
	return 0;
}

static void on_popup_closed(GtkPopover *pop, gpointer d) {
	(void)d;
	// Unparented once its action (which runs after it closes) has.
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, unparent_later, pop, NULL);
}

// lx_popup_at shows a popover menu of model on n's view: at x, y in it,
// or (x < 0, from the keyboard) under it.
void lx_popup_at(node *n, GMenuModel *model, double x, double y);
void lx_popup_at(node *n, GMenuModel *model, double x, double y) {
	GtkWidget *pop = gtk_popover_menu_new_from_model(model);
	gtk_widget_set_parent(pop, n->widget);
	if (x >= 0) {
		GdkRectangle r = {(int)x, (int)y, 1, 1};
		gtk_popover_set_pointing_to((GtkPopover *)pop, &r);
		gtk_popover_set_has_arrow((GtkPopover *)pop, 0);
		gtk_widget_set_halign(pop, n->rtl ? GTK_ALIGN_END : GTK_ALIGN_START);
	} else {
		gtk_popover_set_position((GtkPopover *)pop, GTK_POS_BOTTOM);
	}
	g_signal_connect(pop, "closed", G_CALLBACK(on_popup_closed), NULL);
	gtk_popover_popup((GtkPopover *)pop);
}

// actions_menu shows a view's actions as a popover menu. The action
// chosen sends A11yAction. A view with a context menu of its own shows
// that.
static bool actions_menu(node *n, double x, double y) {
	a11y_state *a = n->a11y;
	if (!a || !a->actions.len || !n->widget || n->has_menu) return false;
	GMenu *model = g_menu_new();
	for (uint32_t i = 0; i < a->actions.len && i < a->names.len; i++) {
		char detailed[256];
		snprintf(detailed, sizeof detailed, "action.%s", a->names.data[i]);
		GMenuItem *item = g_menu_item_new(a->actions.data[i], detailed);
		g_menu_append_item(model, item);
		g_object_unref(item);
	}
	lx_popup_at(n, (GMenuModel *)model, x, y);
	g_object_unref(model);
	return true;
}

// action_names are titles as GAction names (ASCII letters, digits, '-'
// and '.'), each its own: a space or a dot is a hyphen, other characters
// go; a title left with none its number.
static void action_names(a11y_state *a) {
	for (uint32_t i = 0; i < a->actions.len; i++) {
		const char *t = a->actions.data[i];
		while (*t == ' ') t++;
		char name[128];
		size_t k = 0;
		bool hyphen = false;
		for (; *t && k < sizeof name - 8; t++) {
			unsigned char c = (unsigned char)*t;
			if (c < 128 && isalnum(c)) {
				name[k++] = (char)c;
				hyphen = false;
			} else if ((c == ' ' || c == '.' || c == '-' || c == '_' || c == '/') && k > 0 && !hyphen) {
				name[k++] = '-';
				hyphen = true;
			}
		}
		while (k > 0 && name[k - 1] == '-') k--;
		name[k] = 0;
		if (!k) snprintf(name, sizeof name, "%u", i + 1);
		char unique[160];
		snprintf(unique, sizeof unique, "%s", name);
		for (int n = 2;; n++) {
			bool seen = false;
			for (uint32_t j = 0; j < a->names.len; j++) seen |= !strcmp(a->names.data[j], unique);
			if (!seen) break;
			snprintf(unique, sizeof unique, "%s-%d", name, n);
		}
		ak_vec_push(&a->names, strdup(unique));
	}
}

typedef struct {
	ak_id id;
	int index;
} a11y_act;

static void on_a11y_action(GSimpleAction *act, GVariant *param, gpointer data) {
	(void)act;
	(void)param;
	a11y_act *a = data;
	node *n = node_get(a->id);
	if (n) lx_emit(n, (ak_event){.kind = AK_EVENT_A11Y_ACTION, .i = a->index});
}

// expose_actions makes a view's actions its "action" group, and says it
// has them.
static void expose_actions(node *n, a11y_state *a) {
	if (!n->widget) return;
	for (uint32_t i = 0; i < a->names.len; i++) free(a->names.data[i]);
	a->names.len = 0;
	action_names(a);
	if (!a->actions.len) {
		gtk_widget_insert_action_group(n->widget, "action", NULL);
		gtk_accessible_reset_property((GtkAccessible *)n->widget, (GtkAccessibleProperty)A11Y_PROP_HAS_POPUP);
		gtk_accessible_reset_property((GtkAccessible *)n->widget, (GtkAccessibleProperty)A11Y_PROP_KEY_SHORTCUTS);
		return;
	}
	// GTK names no generic view: a group, so its label is read with its
	// actions.
	if (n->kind == AK_KIND_VIEW && !a->adjustable) lx_set_role(n->widget, "group");
	GSimpleActionGroup *group = g_simple_action_group_new();
	for (uint32_t i = 0; i < a->names.len; i++) {
		GSimpleAction *act = g_simple_action_new(a->names.data[i], NULL);
		a11y_act *d = malloc(sizeof *d);
		if (!d) abort();
		d->id = n->id;
		d->index = (int)i;
		g_signal_connect_data(act, "activate", G_CALLBACK(on_a11y_action), d, (GClosureNotify)(void (*)(void))free, (GConnectFlags)0);
		g_action_map_add_action((GActionMap *)group, (GAction *)act);
		g_object_unref(act);
	}
	gtk_widget_insert_action_group(n->widget, "action", (GActionGroup *)group);  // the view holds it
	g_object_unref(group);
	GValue val = {0};
	g_value_init(&val, G_TYPE_BOOLEAN);
	g_value_set_boolean(&val, 1);
	GtkAccessibleProperty p = (GtkAccessibleProperty)A11Y_PROP_HAS_POPUP;
	gtk_accessible_update_property_value((GtkAccessible *)n->widget, 1, &p, &val);
	g_value_unset(&val);
	lx_set_a11y_string(n->widget, A11Y_PROP_KEY_SHORTCUTS, "Shift+F10");
}

void lx_set_a11y_actions(node *n, ak_span b) {
	a11y_state *a = a11y_of(n);
	for (uint32_t i = 0; i < a->actions.len; i++) free(a->actions.data[i]);
	a->actions.len = 0;
	ak_str_vec ss = {0};
	ak_decode_strings((const uint8_t *)b.p, b.len, &ss);
	for (uint32_t i = 0; i < ss.len; i++) ak_vec_push(&a->actions, str_dup(ss.data[i]));
	ak_vec_free(&ss);
	expose_actions(n, a);
}

void lx_set_adjustable(node *n, bool on) {
	a11y_state *a = a11y_of(n);
	a->adjustable = on;
	if (on && n->widget) lx_set_role(n->widget, "slider");
}
