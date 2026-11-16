// asuka/webauth on Linux, as ASWebAuthenticationSession on a Mac: an
// Adwaita dialog over the window the user works in with a WebKitGTK 6 web
// view; closing it (its Cancel button, Escape) cancels. Its cookies are
// WebKit's default session's (the app's web views'), or, with ephemeral,
// a session of its own, gone with it.
#include "asuka_linux.h"
#include "webauth.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

static struct {
	bool tried;
	const char *missing;
	GtkWidget *(*view_new)(void);
	GType (*view_type)(void);
	gpointer (*session_ephemeral)(void);
	void (*load_uri)(GtkWidget *, const char *);
	void (*stop_loading)(GtkWidget *);
	const char *(*get_title)(GtkWidget *);
	const char *(*get_uri)(GtkWidget *);
	gpointer (*navigation_action)(gpointer);
	gpointer (*action_request)(gpointer);
	const char *(*request_uri)(gpointer);
	void (*ignore)(gpointer);
} wk;

static bool load(void) {
	if (wk.tried) return !wk.missing;
	wk.tried = true;
	void *l = dlopen("libwebkitgtk-6.0.so.4", RTLD_NOW | RTLD_GLOBAL);
	if (!l) {
		wk.missing = "WebKitGTK 6.0 isn't installed: install it to sign in (Ubuntu, Debian: libwebkitgtk-6.0-4; Fedora: webkitgtk6.0; Arch: webkitgtk-6.0)";
		return false;
	}
	*(void **)&wk.view_new = dlsym(l, "webkit_web_view_new");
	*(void **)&wk.view_type = dlsym(l, "webkit_web_view_get_type");
	*(void **)&wk.session_ephemeral = dlsym(l, "webkit_network_session_new_ephemeral");
	*(void **)&wk.load_uri = dlsym(l, "webkit_web_view_load_uri");
	*(void **)&wk.stop_loading = dlsym(l, "webkit_web_view_stop_loading");
	*(void **)&wk.get_title = dlsym(l, "webkit_web_view_get_title");
	*(void **)&wk.get_uri = dlsym(l, "webkit_web_view_get_uri");
	*(void **)&wk.navigation_action = dlsym(l, "webkit_navigation_policy_decision_get_navigation_action");
	*(void **)&wk.action_request = dlsym(l, "webkit_navigation_action_get_request");
	*(void **)&wk.request_uri = dlsym(l, "webkit_uri_request_get_uri");
	*(void **)&wk.ignore = dlsym(l, "webkit_policy_decision_ignore");
	if (!wk.view_new || !wk.load_uri || !wk.navigation_action || !wk.action_request || !wk.request_uri || !wk.ignore)
		wk.missing = "WebKitGTK 6.0 is too old: update it to sign in";
	return !wk.missing;
}

typedef struct {
	GtkWidget *dialog, *view, *title;
	gulong handlers[5];
	gpointer objects[5];
} ui;

static void on_cancel(gpointer button, gpointer d) { webauth_finish(d, NULL, NULL); }
static void on_closed(gpointer dialog, gpointer d) { webauth_finish(d, NULL, NULL); }

typedef struct {
	signin *s;
	char *to;
} arrived;

static void arrive(void *p) {
	arrived *a = p;
	webauth_finish(a->s, a->to, NULL);
	free(a->to);
	free(a);
}

static gboolean on_policy(gpointer view, gpointer decision, int kind, gpointer d) {
	signin *s = d;
	if (s->done || (kind != 0 && kind != 1)) return false;  // navigation, new window
	gpointer act = wk.navigation_action(decision);
	const char *to = act ? wk.request_uri(wk.action_request(act)) : NULL;
	if (!to || !webauth_is_callback(to, s->scheme)) return false;
	wk.ignore(decision);
	arrived *a = malloc(sizeof *a);
	*a = (arrived){s, asuka_strdup(to)};
	asuka_desktop_post(arrive, a);  // not inside WebKit's signal
	return true;
}

static void on_title(gpointer view, gpointer spec, gpointer d) {
	signin *s = d;
	ui *u = s->platform;
	const char *t = wk.get_title(u->view);
	G(adw_window_title_set_title)((AdwWindowTitle *)u->title, t ? t : "");
}

static void on_uri(gpointer view, gpointer spec, gpointer d) {
	signin *s = d;
	ui *u = s->platform;
	const char *uri = wk.get_uri(u->view);
	char *host = webauth_host(uri ? uri : "");
	G(adw_window_title_set_subtitle)((AdwWindowTitle *)u->title, host);
	free(host);
}

static void freed(void *p) { webauth_free(p); }

void webauth_close(signin *s) {
	ui *u = s->platform;
	if (u) {
		for (int i = 0; i < 5; i++)
			if (u->handlers[i]) G(g_signal_handler_disconnect)(u->objects[i], u->handlers[i]);
		wk.stop_loading(u->view);
		G(adw_dialog_force_close)((AdwDialog *)u->dialog);
		G(g_object_unref)(u->dialog);
		G(g_object_unref)(u->view);
		free(u);
		s->platform = NULL;
	}
	asuka_desktop_post(freed, s);
}

void webauth_show(signin *s) {
	if (!asuka_linux() || !G(adw_dialog_new) || !load()) {
		asuka_error e = {0};
		asuka_error_unsupported(&e, "%s", wk.missing ? wk.missing : "webauth: no libadwaita 1.5");
		webauth_finish(s, NULL, &e);
		return;
	}
	ui *u = calloc(1, sizeof *u);
	s->platform = u;
	if (s->ephemeral && wk.session_ephemeral && wk.view_type) {
		gpointer session = wk.session_ephemeral();
		u->view = G(g_object_new)(wk.view_type(), "network-session", session, NULL);
		G(g_object_unref)(session);
	} else {
		u->view = wk.view_new();
	}
	G(g_object_ref_sink)(u->view);
	G(gtk_widget_set_vexpand)(u->view, true);
	u->dialog = G(g_object_ref_sink)(G(adw_dialog_new)());
	G(adw_dialog_set_content_width)((AdwDialog *)u->dialog, 560);
	G(adw_dialog_set_content_height)((AdwDialog *)u->dialog, 680);
	GtkWidget *tv = G(adw_toolbar_view_new)(), *bar = G(adw_header_bar_new)();
	AdwHeaderBar *hb = (AdwHeaderBar *)bar;
	AdwToolbarView *tvv = (AdwToolbarView *)tv;
	G(adw_header_bar_set_show_start_title_buttons)(hb, false);
	G(adw_header_bar_set_show_end_title_buttons)(hb, false);
	// GTK's own word for it, in the user's language.
	GtkWidget *cancel = G(gtk_button_new_with_mnemonic)(G(g_dgettext)("gtk40", "_Cancel"));
	G(adw_header_bar_pack_start)(hb, cancel);
	u->title = G(adw_window_title_new)("", s->host);
	G(adw_header_bar_set_title_widget)(hb, u->title);
	G(adw_toolbar_view_add_top_bar)(tvv, bar);
	G(adw_toolbar_view_set_content)(tvv, u->view);
	G(adw_dialog_set_child)((AdwDialog *)u->dialog, tv);
	gpointer objs[5] = {cancel, u->dialog, u->view, u->view, u->view};
	memcpy(u->objects, objs, sizeof objs);
	u->handlers[0] = g_signal_connect(cancel, "clicked", on_cancel, s);
	u->handlers[1] = g_signal_connect(u->dialog, "closed", on_closed, s);
	u->handlers[2] = g_signal_connect(u->view, "decide-policy", on_policy, s);
	u->handlers[3] = g_signal_connect(u->view, "notify::title", on_title, s);
	u->handlers[4] = g_signal_connect(u->view, "notify::uri", on_uri, s);
	G(adw_dialog_present)((AdwDialog *)u->dialog, asuka_desktop_window(0));
	wk.load_uri(u->view, s->link);
}
