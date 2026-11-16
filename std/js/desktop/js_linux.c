// asuka/js on Linux: a WebKitGTK 6 web view that's never shown, the
// script run with webkit_web_view_call_async_javascript_function, which
// awaits a promise.
#include "asuka_linux.h"
#include "js.h"

#include <stdlib.h>
#include <string.h>

typedef struct _JSCValue JSCValue;

static struct {
	bool tried;
	const char *missing;
} wk;

static bool load(void) {
	if (wk.tried) return !wk.missing;
	wk.tried = true;
	if (!asuka_linux() || !asuka_linux_load(ASUKA_LIB_WEBKIT) || !asuka_linux_load(ASUKA_LIB_JSC)) {
		wk.missing = "WebKitGTK 6.0 isn't installed: install it to run JavaScript (Ubuntu, Debian: libwebkitgtk-6.0-4; Fedora: webkitgtk6.0; Arch: webkitgtk-6.0)";
		return false;
	}
	if (!G(webkit_web_view_new) || !G(webkit_web_view_load_html) || !G(webkit_web_view_call_async_javascript_function) || !G(webkit_web_view_call_async_javascript_function_finish) || !G(jsc_value_to_string)) wk.missing = "WebKitGTK 6.0 is too old: update it to run JavaScript";
	return !wk.missing;
}

// body runs s by indirect eval (the global scope, the last statement's
// value) and returns its value as JSON.
static const char body[] = "const v = await (0, eval)(s); return JSON.stringify(v === undefined ? null : v)";

typedef struct {
	gpointer view;
	gulong handler;
} page;

void js_stop(js_run *r) {
	page *p = r->page;
	if (!p) return;
	r->page = NULL;
	G(g_signal_handler_disconnect)(p->view, p->handler);
	if (G(webkit_web_view_stop_loading)) G(webkit_web_view_stop_loading)(p->view);
	G(g_object_unref)(p->view);
	free(p);
}

static void evaluated(GObject *src, GAsyncResult *res, gpointer d) {
	js_run *r = d;
	GError *e = NULL;
	JSCValue *v = G(webkit_web_view_call_async_javascript_function_finish)((WebKitWebView *)src, res, &e);
	G(g_object_unref)(src);  // the call's reference
	if (r->done) {  // answered already (a timeout, a cancel)
		if (e) G(g_error_free)(e);
		if (v) G(g_object_unref)(v);
		js_unref(r);
		return;
	}
	if (e) {
		asuka_error err = {0};
		asuka_error_failed(&err, "js", 0, "%s", e->message);
		G(g_error_free)(e);
		js_answer(r, NULL, &err);
	} else {
		char *s = v ? G(jsc_value_to_string)(v) : NULL;
		js_answer(r, asuka_strdup(s && *s ? s : "null"), NULL);
		G(g_free)(s);
	}
	if (v) G(g_object_unref)(v);
	js_unref(r);
}

static void load_changed(gpointer view, int event, gpointer d) {
	js_run *r = d;
	if (event != 3 || !r->page || r->done) return;  // WEBKIT_LOAD_FINISHED
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	G(g_variant_builder_add)(&b, "{sv}", "s", G(g_variant_new_string)(r->script));
	GVariant *args = G(g_variant_builder_end)(&b);
	G(g_object_ref)(view);
	r->refs++;  // evaluated's
	G(webkit_web_view_call_async_javascript_function)(view, body, -1, args, NULL, NULL, NULL, evaluated, r);
}

void js_start(js_run *r) {
	if (!asuka_linux() || !load()) {
		asuka_error e = {0};
		asuka_error_unsupported(&e, "%s", wk.missing ? wk.missing : "no GLib");
		js_answer(r, NULL, &e);
		return;
	}
	page *p = calloc(1, sizeof *p);
	p->view = G(g_object_ref_sink)(G(webkit_web_view_new)());
	p->handler = g_signal_connect(p->view, "load-changed", load_changed, r);
	r->page = p;
	G(webkit_web_view_load_html)(p->view, "<!doctype html><html></html>", NULL);
}
