// winctl.c: controlling windows: the main window's and each Window's
// options as it opens, the app's WindowAction commands, and their state
// reported as it changes.
//
// What GTK 4 does: the content's size (the window's default size, the
// header bar's height on top) and its least (the content's size request);
// not resizable, no close button, no minimize or maximize button (the
// header bar's decoration layout without them), maximized and full
// screen, hidden until a Show, a background color or none, a hidden
// title bar (the content under a flat, title-less header bar whose window
// buttons stay; SafeTop is its height), a child (transient for its
// parent, destroyed with it) and a modal one.
//
// What GTK 4 can't, the host does on X11 through Xlib (deskwin.c): a
// window's place, always on top, skipping the taskbar, as do panels and
// flyouts. Wayland leaves those to the compositor. Neither has a most
// size, and the position reported is 0, 0; Wayland doesn't tell an app
// its window is minimized either.
#include "host.h"

#include <stdio.h>
#include <stdlib.h>

win *lx_win_for(node *n) {
	if (!n) return NULL;
	if (n->kind == AK_KIND_WINDOW) return n->win;
	if (n == H.root) return H.main;
	return NULL;
}

node *lx_window_node(win *w) {
	if (w->node) return w->node;
	if (w == H.main) return H.root;
	return NULL;
}

// lx_header_room is how much of the window's height its header bar takes
// above the content: none with a hidden title bar, the content going
// under it.
int lx_header_room(win *w) {
	if (w->cfg.title_bar == AK_TITLE_BAR_HIDDEN || lx_frameless(w)) return 0;
	return AK_HEADER_HEIGHT;
}

// decoration_layout is the desktop's layout of a header bar's window
// buttons less minimize or maximize (malloc'd).
static char *decoration_layout(bool no_min, bool no_max) {
	char *layout = NULL;
	GtkSettings *s = gtk_settings_get_default();
	if (s) g_object_get(s, "gtk-decoration-layout", &layout, NULL);
	const char *src = str_empty(layout) ? "menu:minimize,maximize,close" : layout;
	ak_buf b = {0};
	const char *p = src;
	while (*p) {
		const char *end = p;
		while (*end && *end != ',' && *end != ':') end++;
		size_t n = (size_t)(end - p);
		bool drop = (no_min && n == 8 && !strncmp(p, "minimize", 8)) || (no_max && n == 8 && !strncmp(p, "maximize", 8));
		if (!drop) {
			if (b.len && b.data[b.len - 1] != ':') ak_buf_u8(&b, ',');
			ak_buf_put(&b, p, n);
		}
		if (*end == ':') ak_buf_u8(&b, ':');
		p = *end ? end + 1 : end;
	}
	g_free(layout);
	char *out = strdup(lx_buf_cstr(&b));
	ak_buf_free(&b);
	return out;
}

static void on_top_bar(GObject *o, GParamSpec *ps, gpointer data) {
	(void)o;
	(void)ps;
	(void)data;
	lx_env_changed();
}

// lx_window_css gives a window its own background (a color, or none, so
// the compositor shows what's behind where the content draws nothing)
// and, with a hidden title bar, a header bar with none either: a style
// provider of its own, selecting the window by its name.
void lx_window_css(win *w) {
	ak_window_config *c = &w->cfg;
	if (!w->css_name) {
		char name[48];
		snprintf(name, sizeof name, "asuka-window-%d", ++H.next_win);
		w->css_name = strdup(name);
	}
	ak_buf css = {0};
	const char *sel = w->css_name;
	GdkRGBA col;
	if (c->flags & AK_WINDOW_TRANSPARENT)
		ak_buf_printf(&css, "window#%s, window#%s toolbarview { background: none; background-color: transparent; }\n", sel, sel);
	else if (c->background && lx_resolve(c->background, &col))
		ak_buf_printf(&css, "window#%s { background-color: rgba(%d,%d,%d,%g); }\n", sel, (int)(col.red * 255 + 0.5f), (int)(col.green * 255 + 0.5f), (int)(col.blue * 255 + 0.5f), col.alpha);
	if (c->title_bar == AK_TITLE_BAR_HIDDEN)
		ak_buf_printf(&css, "window#%s toolbarview > .top-bar, window#%s headerbar { background: none; background-color: transparent; box-shadow: none; }\n", sel, sel);
	if (!css.len && !w->css) {
		ak_buf_free(&css);
		return;
	}
	gtk_widget_set_name((GtkWidget *)w->window, w->css_name);
	if (!w->css) {
		w->css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_display(gdk_display_get_default(), (GtkStyleProvider *)w->css, 800);  // above the app's (700)
	}
	gtk_css_provider_load_from_string(w->css, lx_buf_cstr(&css));
	ak_buf_free(&css);
}

void lx_drop_window_css(win *w) {
	if (w->css) {
		gtk_style_context_remove_provider_for_display(gdk_display_get_default(), (GtkStyleProvider *)w->css);
		g_object_unref(w->css);
		w->css = NULL;
	}
}

static win *parent_win(ak_id id) {
	node *n = node_get(id);
	if (!n) return NULL;
	if (n->kind == AK_KIND_WINDOW) return n->win;
	return H.main;  // the root (which the core may not have said is the root yet)
}

// lx_configure applies a window's options before it shows: its size (or
// the one remembered under its key), its least, what the user can do,
// how it looks, and whose child it is.
void lx_configure(win *w, const ak_window_config *c0) {
	ak_window_config c = *c0;
	str_set(&w->cfg_title, c.title);
	str_set(&w->cfg_key, c.state_key);
	c.title = ak_cstr(w->cfg_title);
	c.state_key = ak_cstr(w->cfg_key);
	w->cfg = c;
	w->configured = true;
	w->hidden = (c.flags & AK_WINDOW_HIDDEN) != 0;

	gtk_window_set_resizable(w->window, !(c.flags & AK_WINDOW_FIXED_SIZE));
	gtk_window_set_deletable(w->window, !(c.flags & AK_WINDOW_NO_CLOSE));
	if (c.flags & (AK_WINDOW_NO_MINIMIZE | AK_WINDOW_NO_MAXIMIZE)) {
		char *l = decoration_layout(c.flags & AK_WINDOW_NO_MINIMIZE, c.flags & AK_WINDOW_NO_MAXIMIZE);
		adw_header_bar_set_decoration_layout(w->header, l);
		free(l);
	}
	bool hidden = c.title_bar == AK_TITLE_BAR_HIDDEN;
	adw_toolbar_view_set_extend_content_to_top_edge(w->toolbar, hidden);
	adw_header_bar_set_show_title(w->header, !hidden);
	if (hidden) {
		adw_toolbar_view_set_top_bar_style(w->toolbar, ADW_TOOLBAR_FLAT);
		g_signal_connect(w->toolbar, "notify::top-bar-height", G_CALLBACK(on_top_bar), NULL);
	}
	lx_window_css(w);

	// Its size: the content's, the header bar on top; the user's, if it's
	// remembered.
	float width = c.width > 0 ? c.width : 800, height = c.height > 0 ? c.height : 600;
	gtk_window_set_default_size(w->window, lx_window_size(width), lx_window_size(height) + lx_header_room(w));
	str_set(&w->state_key, c.state_key);
	if (w == H.main && str_empty(w->state_key)) str_setc(&w->state_key, "main");
	bool fits = c.flags & AK_WINDOW_FIT_CONTENT;
	if (!fits) lx_restore_window(w);  // one sized to its content is its content's size
	float min_w = c.min_width, min_h = c.min_height;
	if (w == H.main && !fits) {
		// The main window's least is 320 points square unless the app says.
		if (min_w <= 0) min_w = 320;
		if (min_h <= 0) min_h = 320;
	}
	gtk_widget_set_size_request(w->content, lx_window_size(min_w), lx_window_size(min_h));
	if (c.flags & AK_WINDOW_MAXIMIZED) gtk_window_maximize(w->window);
	if (c.flags & AK_WINDOW_FULLSCREEN) gtk_window_fullscreen(w->window);
	if (c.flags & AK_WINDOW_CHILD) {
		win *p = parent_win(c.parent);
		if (p && p != w) {
			gtk_window_set_transient_for(w->window, p->window);
			gtk_window_set_destroy_with_parent(w->window, 1);
			gtk_window_set_modal(w->window, (c.flags & AK_WINDOW_MODAL) != 0);
		}
	}
	lx_configure_desk(w, &c);
}

// lx_show_main shows the main window as the app starts, unless it's to
// start hidden.
void lx_show_main(void) {
	win *w = H.main;
	if (!w->configured) {
		ak_window_config c = {0};
		lx_configure(w, &c);
	}
	if (!w->hidden) lx_show_win(w, true);
}

void lx_show_desk(win *w, bool present);

void lx_show_win(win *w, bool present) { lx_show_desk(w, present); }


static uint32_t toplevel_state(win *w) {
	GdkSurface *s = gtk_native_get_surface((GtkNative *)w->window);
	return s && gdk_toplevel_get_state ? (uint32_t)gdk_toplevel_get_state((GdkToplevel *)s) : 0;
}

static bool minimized(win *w) { return toplevel_state(w) & GDK_TOPLEVEL_STATE_MINIMIZED; }

static gboolean close_main(gpointer data) {
	win *w = data;
	if (!w->closing) gtk_window_close(w->window);
	return 0;
}

// lx_window_action does a WindowAction to w.
void lx_window_action(win *w, ak_span args) {
	uint8_t a;
	float x, y;
	if (!ak_decode_window_action((const uint8_t *)args.p, args.len, &a, &x, &y) || w->closing) return;
	GtkWindow *win_ = w->window;
	switch (a) {
	case AK_WINDOW_ACTION_SHOW:
		w->hidden = false;
		lx_show_win(w, false);
		break;
	case AK_WINDOW_ACTION_HIDE:
		gtk_widget_set_visible((GtkWidget *)win_, 0);
		break;
	case AK_WINDOW_ACTION_FOCUS:
		w->hidden = false;
		gtk_window_unminimize(win_);
		lx_show_win(w, true);
		break;
	case AK_WINDOW_ACTION_MINIMIZE:
		gtk_window_minimize(win_);
		break;
	case AK_WINDOW_ACTION_MAXIMIZE:
		gtk_window_maximize(win_);
		break;
	case AK_WINDOW_ACTION_RESTORE:
		if (minimized(w)) {
			gtk_window_unminimize(win_);
			gtk_window_present(win_);
		} else if (gtk_window_is_maximized(win_)) {
			gtk_window_unmaximize(win_);
		}
		break;
	case AK_WINDOW_ACTION_FULLSCREEN:
		gtk_window_fullscreen(win_);
		break;
	case AK_WINDOW_ACTION_EXIT_FULLSCREEN:
		gtk_window_unfullscreen(win_);
		break;
	case AK_WINDOW_ACTION_SIZE:
		// The window's size unmaximized: it takes it now, if it isn't
		// maximized or in full screen.
		gtk_window_set_default_size(win_, lx_window_size(x), lx_window_size(y) + lx_header_room(w));
		break;
	case AK_WINDOW_ACTION_CLOSE:
		if (w == H.main) {
			// On the main loop's next turn: the core's answer to a close
			// request comes while GTK is still emitting close-request.
			w->forced = true;
			g_idle_add_full(0, close_main, w, NULL);
		}
		break;
	case AK_WINDOW_ACTION_FIT:
		lx_fit_window(w, x, y);
		break;
	case AK_WINDOW_ACTION_CLICK_THROUGH:
	case AK_WINDOW_ACTION_NOT_CLICK_THROUGH:
		lx_set_click_through(w, a == AK_WINDOW_ACTION_CLICK_THROUGH);
		break;
	case AK_WINDOW_ACTION_ALWAYS_ON_TOP:
	case AK_WINDOW_ACTION_NOT_ALWAYS_ON_TOP:
		lx_set_on_top(w, a == AK_WINDOW_ACTION_ALWAYS_ON_TOP);  // X11's (deskwin.c)
		break;
	case AK_WINDOW_ACTION_POSITION:
		lx_move_to(w, x, y);  // X11's
		break;
	case AK_WINDOW_ACTION_CENTER:
		lx_center(w);
		break;
	}
	lx_queue_window_state(w);
}

// window_state is w's state: no position (GTK 4 doesn't know it), its
// content's size (as allocated; before that, as it will be).
static ak_window_state window_state(win *w) {
	ak_window_state s = {0, 0, w->env_w, w->env_h, 0};
	if (s.width == 0 && s.height == 0) {
		int dw = 0, dh = 0;
		gtk_window_get_default_size(w->window, &dw, &dh);
		s.width = (float)dw;
		int h = dh - lx_header_room(w);
		s.height = (float)(h > 0 ? h : 0);
	}
	if (gtk_widget_get_visible((GtkWidget *)w->window)) s.flags |= AK_WINDOW_STATE_VISIBLE;
	if (gtk_window_is_active(w->window)) s.flags |= AK_WINDOW_STATE_FOCUSED;
	if (minimized(w)) s.flags |= AK_WINDOW_STATE_MINIMIZED;
	if (gtk_window_is_maximized(w->window)) s.flags |= AK_WINDOW_STATE_MAXIMIZED;
	if (gtk_window_is_fullscreen(w->window)) s.flags |= AK_WINDOW_STATE_FULLSCREEN;
	if (lx_on_top(w)) s.flags |= AK_WINDOW_STATE_ON_TOP;
	if (w->desk.through) s.flags |= AK_WINDOW_STATE_CLICK_THROUGH;
	return s;
}

// report_window_state sends w's state, if it changed since it was last
// sent.
static void report_window_state(void *ctx) {
	win *w = ctx;
	node *n = lx_window_node(w);
	if (!n || w->closing || !listening(n, AK_EVENT_WINDOW_STATE)) return;
	ak_window_state s = window_state(w);
	if (w->reported && memcmp(&s, &w->sent, sizeof s) == 0) return;
	w->reported = true;
	w->sent = s;
	uint8_t buf[32];
	ak_buf b = {0};
	ak_append_window_state(&b, &s);
	memcpy(buf, b.data, b.len < sizeof buf ? b.len : sizeof buf);
	lx_emit(n, (ak_event){.kind = AK_EVENT_WINDOW_STATE, .s = ak_strn(buf, b.len)});
	ak_buf_free(&b);
}

// lx_queue_window_state reports w's state once the call into the core in
// progress returns (now, if none is), if the core listens for it.
void lx_queue_window_state(win *w) {
	node *n = lx_window_node(w);
	if (!n || !listening(n, AK_EVENT_WINDOW_STATE)) return;
	lx_after_core(report_window_state, w);
}

static void on_state(GObject *o, GParamSpec *ps, gpointer data) {
	(void)o;
	(void)ps;
	lx_queue_window_state(data);
}

static void on_realize(GtkWidget *window, gpointer data) {
	GdkSurface *s = gtk_native_get_surface((GtkNative *)window);
	if (s) g_signal_connect(s, "notify::state", G_CALLBACK(on_state), data);
}

// lx_watch_window_state follows what a window's state is made of:
// shown or hidden, active, maximized, full screen, its surface's state.
// Its size comes as its content is allocated.
void lx_watch_window_state(win *w) {
	static const char *const props[] = {"notify::visible", "notify::is-active", "notify::maximized", "notify::fullscreened"};
	for (size_t i = 0; i < AK_LEN(props); i++) g_signal_connect(w->window, props[i], G_CALLBACK(on_state), w);
	g_signal_connect(w->window, "realize", G_CALLBACK(on_realize), w);
}
