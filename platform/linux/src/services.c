// services.c: the host's modules (what asuka/platform's call reaches: the
// Call command), and what the host does for the app itself: opening a
// URL, sharing, the clipboard's text, keeping the session awake, GNOME's
// settings GTK doesn't follow.
//
// A module's method answers once, on the main thread, after the batch
// that called it: a Result event (u32 call, u8 ok, then the answer's
// bytes or the error's message) on the root that called. Each area's file
// registers its modules' methods (area.modules); a call of a method no one
// registered answers that it isn't on Linux, which asuka/platform's
// unsupported reads.
#include "host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct reply {
	uint32_t call;
	ak_id root;
	bool done;
};

static AK_VEC(method_t) methods;

// lx_register_methods adds methods to the host's modules.
void lx_register_methods(const method_t *m, size_t n) {
	for (size_t i = 0; i < n; i++) ak_vec_push(&methods, m[i]);
}

// lx_method is a module's method, NULL if no one registered it.
method_fn lx_method(const char *module, const char *name) {
	for (uint32_t i = 0; i < methods.len; i++)
		if (!strcmp(methods.data[i].module, module) && !strcmp(methods.data[i].name, name)) return methods.data[i].fn;
	return NULL;
}

typedef struct {
	ak_id root;
	size_t len;
	uint8_t data[];
} result_t;

static void deliver(void *ctx) {
	result_t *res = ctx;
	lx_dispatch(&(ak_event){.id = res->root, .kind = AK_EVENT_RESULT, .s = ak_strn((const char *)res->data, res->len)});
	free(res);
}

static void answer(reply *r, bool ok, const void *p, size_t n) {
	if (!r || r->done) return;
	r->done = true;
	result_t *res = malloc(sizeof *res + 5 + n);
	if (!res) abort();
	res->root = r->root;
	res->len = 5 + n;
	memcpy(res->data, &r->call, 4);  // little-endian, as every platform the host runs on
	res->data[4] = ok;
	if (n) memcpy(res->data + 5, p, n);
	free(r);
	// Answered after the batch, as a host's modules do.
	lx_post(deliver, res);
}

void reply_ok(reply *r, const void *p, size_t n) { answer(r, true, p, n); }

void reply_str(reply *r, const char *s) { answer(r, true, s, s ? strlen(s) : 0); }

void reply_err(reply *r, const char *fmt, ...) {
	char msg[1024];
	va_list ap;
	va_start(ap, fmt);
	int k = vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	if (k < 0) k = 0;
	if ((size_t)k >= sizeof msg) k = sizeof msg - 1;
	answer(r, false, msg, (size_t)k);
}

// lx_call runs a module's method (asuka/platform's call): args are u32
// call, string module, string method, then the method's arguments.
void lx_call(node *root, ak_span args) {
	ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
	uint32_t call = ak_read_u32(&rd);
	uint32_t ml, nl;
	const uint8_t *m = ak_read_bytes(&rd, &ml);
	const uint8_t *nm = ak_read_bytes(&rd, &nl);
	if (rd.err) return;
	char module[64], name[64];
	snprintf(module, sizeof module, "%.*s", (int)ml, (const char *)m);
	snprintf(name, sizeof name, "%.*s", (int)nl, (const char *)nm);
	reply *r = calloc(1, sizeof *r);
	if (!r) abort();
	r->call = call;
	r->root = root->id;
	method_fn fn = lx_method(module, name);
	if (!fn) {
		reply_err(r, "unsupported: %s.%s isn't on Linux", module, name);
		return;
	}
	fn(ak_strn((const char *)rd.p + rd.off, rd.len - rd.off), r);
}


// ---- The app's own ----

// lx_open_url opens a URL with the user's app for it (through the portal
// in a sandbox).
void lx_open_url(const char *u) {
	GtkUriLauncher *l = gtk_uri_launcher_new(u);
	gtk_uri_launcher_launch(l, H.main ? H.main->window : NULL, NULL, NULL, NULL);
	g_object_unref(l);
}

const char *lx_os_version(void);

// lx_toast shows a toast in the window in use.
void lx_toast(const char *text) {
	win *w = H.active_win ? H.active_win : H.main;
	if (!w || !w->toasts) return;
	adw_toast_overlay_add_toast(w->toasts, adw_toast_new(text));
}

// lx_share shares text: Linux has no share sheet (no portal offers one),
// so it's copied, with a toast saying so.
void lx_share(node *n, ak_span text) {
	(void)n;
	char *t = str_dup(text);
	gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()), t);
	free(t);
	lx_toast("Copied to clipboard");
}

// keep_awake inhibits the session's idling (the screen blanking) while on.
static void keep_awake(bool on) {
	if (on == (H.inhibit != 0)) return;
	if (on) {
		H.inhibit = gtk_application_inhibit((GtkApplication *)H.app, H.main ? H.main->window : NULL, GTK_APPLICATION_INHIBIT_IDLE, "Keeping the screen on");
		return;
	}
	gtk_application_uninhibit((GtkApplication *)H.app, H.inhibit);
	H.inhibit = 0;
}

typedef struct {
	void (*done)(const char *text, bool ok, void *ctx);
	void *ctx;
} clip_read;

static void clip_read_done(GObject *src, GAsyncResult *res, gpointer data) {
	clip_read *c = data;
	GError *err = NULL;
	char *t = gdk_clipboard_read_text_finish((GdkClipboard *)src, res, &err);
	if (err) g_error_free(err);
	c->done(t ? t : "", t != NULL, c->ctx);
	g_free(t);
	free(c);
}

// lx_read_clipboard_text reads the clipboard's text (asynchronously, as
// GDK does).
void lx_read_clipboard_text(void (*done)(const char *text, bool ok, void *ctx), void *ctx) {
	clip_read *c = malloc(sizeof *c);
	if (!c) abort();
	c->done = done;
	c->ctx = ctx;
	gdk_clipboard_read_text_async(gdk_display_get_clipboard(gdk_display_get_default()), NULL, clip_read_done, c);
}

// ---- device, browser ----

// device.info: JSON: model, system, systemVersion, appVersion, build.
static void device_info(ak_span args, reply *r) {
	(void)args;
	dsk_jw w = {0};
	dsk_jw_obj(&w);
	dsk_jw_key(&w, "model");
	dsk_jw_str(&w, "PC");
	dsk_jw_key(&w, "system");
	dsk_jw_str(&w, "Linux");
	dsk_jw_key(&w, "systemVersion");
	dsk_jw_str(&w, lx_os_version());
	dsk_jw_key(&w, "appVersion");
	dsk_jw_str(&w, S(H.cfg.version));
	dsk_jw_key(&w, "build");
	dsk_jw_str(&w, S(H.cfg.build));
	dsk_jw_end(&w);
	size_t n;
	char *s = dsk_jw_take(&w, &n);
	reply_ok(r, s, n);
	free(s);
}

static void clipboard_answer(const char *text, bool ok, void *ctx) {
	reply *r = ctx;
	if (!ok) {
		reply_ok(r, "\0", 1);
		return;
	}
	size_t n = strlen(text);
	uint8_t *b = malloc(n + 1);
	if (!b) abort();
	b[0] = 1;
	memcpy(b + 1, text, n);
	reply_ok(r, b, n + 1);
	free(b);
}

// device.clipboard: u8 has, then the clipboard's text.
static void device_clipboard(ak_span args, reply *r) {
	(void)args;
	lx_read_clipboard_text(clipboard_answer, r);
}

// device.keepAwake: u8 on: the session doesn't go idle while on.
static void device_keep_awake(ak_span args, reply *r) {
	keep_awake(args.len > 0 && args.p[0] == 1);
	reply_ok(r, NULL, 0);
}

// browser.open: the URL: the user's browser opens it.
static void browser_open(ak_span args, reply *r) {
	char *u = str_dup(args);
	lx_open_url(u);
	free(u);
	reply_ok(r, NULL, 0);
}

static const method_t builtin[] = {
	{"device", "info", device_info},
	{"device", "clipboard", device_clipboard},
	{"device", "keepAwake", device_keep_awake},
	{"browser", "open", browser_open},
};

// lx_modules_init registers the host's modules: these, and each area's.
void lx_modules_init(void) {
	if (methods.len) return;
	lx_register_methods(builtin, AK_LEN(builtin));
	for (size_t i = 0; i < lx_area_count; i++)
		if (lx_areas[i]->modules) lx_areas[i]->modules();
}

// ---- GNOME's settings ----

// settings_if_any is a GSettings of a schema the system has, with the
// key (NULL when it hasn't: not every desktop is GNOME's).
GSettings *lx_settings_if_any(const char *schema, const char *key) {
	GSettingsSchemaSource *src = g_settings_schema_source_get_default();
	if (!src) return NULL;
	GSettingsSchema *sc = g_settings_schema_source_lookup(src, schema, 1);
	if (!sc) return NULL;
	bool has = g_settings_schema_has_key(sc, key);
	g_settings_schema_unref(sc);
	return has ? g_settings_new(schema) : NULL;
}


static void read_screen_reader(GSettings *s, const char *key, gpointer data) {
	(void)key;
	H.screen_reader = g_settings_get_boolean(s, "screen-reader-enabled");
	if (data) lx_env_changed();
}

static void read_clock(GSettings *s, const char *key, gpointer data) {
	(void)key;
	char *f = g_settings_get_string(s, "clock-format");
	H.hour_cycle = f && !strcmp(f, "12h") ? 12 : 24;
	g_free(f);
	if (data) lx_env_changed();
}

// lx_watch_desktop follows GNOME's settings that GTK doesn't: the screen
// reader, the clock's format.
void lx_watch_desktop(void) {
	GSettings *s = lx_settings_if_any("org.gnome.desktop.a11y.applications", "screen-reader-enabled");
	if (s) {
		read_screen_reader(s, NULL, NULL);
		g_signal_connect(s, "changed::screen-reader-enabled", G_CALLBACK(read_screen_reader), (gpointer)1);
	}
	s = lx_settings_if_any("org.gnome.desktop.interface", "clock-format");
	if (s) {
		read_clock(s, NULL, NULL);
		g_signal_connect(s, "changed::clock-format", G_CALLBACK(read_clock), (gpointer)1);
	}
}
