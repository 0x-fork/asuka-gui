// web.c: WebView views, and the "web" and "js" modules.
//
// A WebView is a WebKitWebView (WebKitGTK 6.0, opened the first time one
// is made). The page posts to the app with window.asuka.postMessage(text),
// as on the other platforms (a script message handler); Preload is a
// user script at document start in every frame; Block is a content
// filter (the rule list format of Safari's content blockers), the page
// loading once it's compiled; Requests follows resource-load-started; a
// response the page can't show is a Download, and a page for a new window
// NewWindow (the "create" signal). The app's assets load as file:// URLs.
// Its prefers-color-scheme follows the app's (libadwaita tells WebKit).
//
// Without WebKitGTK the view shows nothing and reports Failed once, saying
// what to install.
#include "host.h"

#include <stdio.h>
#include <stdlib.h>

#define BRIDGE_JS "window.asuka = { postMessage: function (m) { window.webkit.messageHandlers.asuka.postMessage(String(m)); } };"

enum { LOAD_STARTED = 0, LOAD_FINISHED = 3 };
enum { POLICY_RESPONSE = 2 };
enum { ERR_POLICY_INTERRUPTED = 102, ERR_NETWORK_CANCELLED = 302, ERR_JS_INVALID_RESULT = 601 };

typedef struct {
	WebKitWebView *view;  // n->inner, the host's reference
	WebKitUserContentManager *ucc;
	WebKitBackForwardList *bf;
	gulong req_id;  // resource-load-started's handler, with Requests
	int block;      // Block's filters compiled, to keep only the last
	int pending;    // the filter compiling (its block), 0 for none
	// A load waiting for Block's filter to compile: a URL or HTML.
	char *held;
	bool held_html;
	bool queued;  // a WebState report waits for the loop to be idle
	bool failed;  // the load in progress failed: no Navigated
	bool missing, reported, closed;
} web;

static const char *webkit_err;
static bool webkit_tried;

// webkit opens WebKitGTK (and libsoup, for its headers), saying once on
// stderr when it can't: NULL, or what to install.
static const char *webkit(void) {
	if (webkit_tried) return webkit_err;
	webkit_tried = true;
	if (!lx_lib_load(ASUKA_LIB_JSC) || !lx_lib_load(ASUKA_LIB_WEBKIT))
		webkit_err = "WebKitGTK 6.0 isn't installed: install it to show web pages (Ubuntu, Debian: libwebkitgtk-6.0-4; Fedora: webkitgtk6.0; Arch: webkitgtk-6.0)";
	else if (!webkit_web_view_call_async_javascript_function || !webkit_network_session_get_default)
		webkit_err = "WebKitGTK 6.0 is too old: update it to show web pages";
	lx_lib_load(ASUKA_LIB_SOUP);
	if (webkit_err) lx_log("%s", webkit_err);
	return webkit_err;
}

static web *web_of(node *n) { return n && n->kind == AK_KIND_WEB_VIEW ? n->ext : NULL; }

static const char *err_message(const GError *e) { return e && e->message ? e->message : ""; }

static void add_script(web *w, const char *js) {
	WebKitUserScript *s = webkit_user_script_new(js, 0 /* all frames */, 0 /* document start */, NULL, NULL);
	webkit_user_content_manager_add_script(w->ucc, s);
	webkit_user_script_unref(s);
}

static void put_str(ak_buf *b, const char *s) { ak_buf_str(b, s ? s : "", s ? strlen(s) : 0); }

static void web_state(node *n, web *w) {
	WebKitWebView *v = w->view;
	ak_buf b = {0};
	put_str(&b, webkit_web_view_get_uri(v));
	put_str(&b, webkit_web_view_get_title(v));
	uint8_t flags = 0;
	if (webkit_web_view_is_loading(v)) flags |= 1;
	if (webkit_web_view_can_go_back(v)) flags |= 2;
	if (webkit_web_view_can_go_forward(v)) flags |= 4;
	ak_buf_u8(&b, flags);
	float progress = flags & 1 ? (float)webkit_web_view_get_estimated_load_progress(v) : 1;
	ak_buf_f32(&b, progress);
	lx_emit(n, (ak_event){.kind = AK_EVENT_WEB_STATE, .s = ak_strn((const char *)b.data, b.len)});
	ak_buf_free(&b);
}

static gboolean state_idle(gpointer data) {
	node *n = node_get((ak_id)(uintptr_t)data);
	web *w = web_of(n);
	if (!w) return 0;
	w->queued = false;
	if (!w->closed) {
		lx_enter_begin();
		web_state(n, w);
		lx_enter_end();
	}
	return 0;
}

// queue_state reports the page's state once for the changes of a turn of
// the main loop.
static void queue_state(node *n, web *w) {
	if (w->queued || w->closed || !listening(n, AK_EVENT_WEB_STATE)) return;
	w->queued = true;
	g_idle_add_full(G_PRIORITY_DEFAULT, state_idle, (gpointer)(uintptr_t)n->id, NULL);
}

static void emit_in(node *n, ak_event e) {
	lx_enter_begin();
	lx_emit(n, e);
	lx_enter_end();
}

static void on_message(WebKitUserContentManager *m, JSCValue *val, node *n) {
	(void)m;
	char *s = jsc_value_to_string(val);
	emit_in(n, (ak_event){.kind = AK_EVENT_MESSAGE, .s = ak_cstr(s ? s : "")});
	g_free(s);
}

static void on_load_changed(WebKitWebView *v, int ev, node *n) {
	web *w = web_of(n);
	if (!w) return;
	if (ev == LOAD_STARTED) w->failed = false;
	if (ev == LOAD_FINISHED && !w->failed) emit_in(n, (ak_event){.kind = AK_EVENT_NAVIGATED, .s = ak_cstr(S(webkit_web_view_get_uri(v)))});
	queue_state(n, w);
}

static gboolean on_load_failed(WebKitWebView *v, int ev, const char *uri, GError *e, node *n) {
	(void)v;
	(void)ev;
	(void)uri;
	web *w = web_of(n);
	if (!w) return 1;
	w->failed = true;
	// Stopped, or a response that's a Download: not a failure.
	bool cancelled = e && e->domain == webkit_network_error_quark() && e->code == ERR_NETWORK_CANCELLED;
	bool interrupted = e && e->domain == webkit_policy_error_quark() && e->code == ERR_POLICY_INTERRUPTED;
	if (!cancelled && !interrupted) emit_in(n, (ak_event){.kind = AK_EVENT_FAILED, .s = ak_cstr(err_message(e))});
	queue_state(n, w);
	return 1;  // no error page, as WKWebView shows none
}

static void on_terminated(WebKitWebView *v, int reason, node *n) {
	(void)v;
	(void)reason;
	emit_in(n, (ak_event){.kind = AK_EVENT_FAILED, .s = AK_STR("the page's web process ended")});
	if (web_of(n)) queue_state(n, web_of(n));
}

// decide_response answers a response's policy decision: the main frame's
// that the view can't show, or an attachment, is ignored and reported as
// a Download; the rest load (WebKit's own decision).
static gboolean on_decide(WebKitWebView *v, WebKitPolicyDecision *d, int kind, node *n) {
	(void)v;
	if (kind != POLICY_RESPONSE) return 0;
	WebKitResponsePolicyDecision *rd = (WebKitResponsePolicyDecision *)d;
	if (!webkit_response_policy_decision_is_main_frame_main_resource(rd)) return 0;
	WebKitURIResponse *r = webkit_response_policy_decision_get_response(rd);
	const char *disposition = NULL, *length = NULL;
	SoupMessageHeaders *hdrs = webkit_uri_response_get_http_headers(r);
	if (hdrs && soup_message_headers_get_one) {
		disposition = soup_message_headers_get_one(hdrs, "Content-Disposition");
		length = soup_message_headers_get_one(hdrs, "Content-Length");
	}
	const char *dp = S(disposition);
	while (*dp == ' ') dp++;
	if (webkit_response_policy_decision_is_mime_type_supported(rd) && g_ascii_strncasecmp(dp, "attachment", 10)) return 0;
	webkit_policy_decision_ignore(d);
	const char *u = S(webkit_uri_response_get_uri(r));
	const char *name = webkit_uri_response_get_suggested_filename(r);
	char *base = NULL;
	if (str_empty(name)) {
		GUri *gu = g_uri_parse(u, 0, NULL);
		const char *path = gu ? g_uri_get_path(gu) : "";
		const char *slash = strrchr(path, '/');
		base = str_dup(ak_cstr(slash ? slash + 1 : path));
		if (gu) g_uri_unref(gu);
		name = *base && strcmp(base, ".") ? base : "download";
	}
	int64_t size = length && *length ? (int64_t)webkit_uri_response_get_content_length(r) : -1;
	ak_buf b = {0};
	put_str(&b, u);
	put_str(&b, webkit_uri_response_get_mime_type(r));
	put_str(&b, name);
	ak_buf_u64(&b, (uint64_t)size);
	emit_in(n, (ak_event){.kind = AK_EVENT_DOWNLOAD, .s = ak_strn((const char *)b.data, b.len)});
	ak_buf_free(&b);
	free(base);
	return 1;
}

static GtkWidget *on_create(WebKitWebView *v, WebKitNavigationAction *action, node *n) {
	// A page for a new window: the app's NewWindow, or this view.
	const char *u = S(webkit_uri_request_get_uri(webkit_navigation_action_get_request(action)));
	if (listening(n, AK_EVENT_NEW_WINDOW))
		emit_in(n, (ak_event){.kind = AK_EVENT_NEW_WINDOW, .s = ak_cstr(u)});
	else if (*u)
		webkit_web_view_load_uri(v, u);
	return NULL;
}

static void on_notify(GObject *o, GParamSpec *p, node *n) {
	(void)o;
	(void)p;
	if (web_of(n)) queue_state(n, web_of(n));
}

static void on_bf_changed(WebKitBackForwardList *l, gpointer a, gpointer b, node *n) {
	(void)l;
	(void)a;
	(void)b;
	if (web_of(n)) queue_state(n, web_of(n));
}

static void on_request(WebKitWebView *v, WebKitWebResource *res, WebKitURIRequest *req, node *n) {
	(void)v;
	(void)res;
	emit_in(n, (ak_event){.kind = AK_EVENT_REQUEST, .s = ak_cstr(S(webkit_uri_request_get_uri(req)))});
}

static void web_close(node *n, web *w) {
	if (w->closed) return;
	w->closed = true;
	w->block++;  // a filter still compiling is dropped
	free(w->held);
	w->held = NULL;
	if (!w->view) return;
	g_signal_handlers_disconnect_matched(w->view, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, n);
	g_signal_handlers_disconnect_matched(w->ucc, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, n);
	if (w->bf) g_signal_handlers_disconnect_matched(w->bf, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, n);
	webkit_web_view_stop_loading(w->view);
	g_object_unref(w->view);  // its parent's reference goes with the node's view
	w->view = NULL;
	w->ucc = NULL;
}

static void free_web(node *n) {
	web *w = n->ext;
	web_close(n, w);
	free(w);
	n->ext = NULL;
}

static bool web_create(node *n) {
	if (n->kind != AK_KIND_WEB_VIEW) return false;
	web *w = calloc(1, sizeof *w);
	if (!w) abort();
	n->ext = w;
	n->ext_free = free_web;
	if (webkit()) {
		w->missing = true;
		return true;
	}
	WebKitWebView *v = g_object_ref_sink(webkit_web_view_new());
	w->view = v;
	w->ucc = webkit_web_view_get_user_content_manager(v);
	webkit_settings_set_enable_developer_extras(webkit_web_view_get_settings(v), 1);
	// Connected before the handler is registered, as WebKit asks.
	g_signal_connect(w->ucc, "script-message-received::asuka", G_CALLBACK(on_message), n);
	webkit_user_content_manager_register_script_message_handler(w->ucc, "asuka", NULL);
	add_script(w, BRIDGE_JS);
	g_signal_connect(v, "load-changed", G_CALLBACK(on_load_changed), n);
	g_signal_connect(v, "load-failed", G_CALLBACK(on_load_failed), n);
	g_signal_connect(v, "web-process-terminated", G_CALLBACK(on_terminated), n);
	g_signal_connect(v, "decide-policy", G_CALLBACK(on_decide), n);
	g_signal_connect(v, "create", G_CALLBACK(on_create), n);
	static const char *const notes[] = {"notify::uri", "notify::title", "notify::estimated-load-progress", "notify::is-loading"};
	for (size_t i = 0; i < AK_LEN(notes); i++) g_signal_connect(v, notes[i], G_CALLBACK(on_notify), n);
	w->bf = webkit_web_view_get_back_forward_list(v);
	if (w->bf) g_signal_connect(w->bf, "changed", G_CALLBACK(on_bf_changed), n);
	lx_set_inner(n, (GtkWidget *)v);
	return true;
}

// lx_web_view is a WebView node's WebKitWebView (NULL: another node,
// or WebKitGTK missing): what asuka_desktop_webview gives a package.
void *lx_web_view(node *n) {
	web *w = web_of(n);
	return w && !w->missing ? w->view : NULL;
}

static void load(web *w, const char *s) {
	if (!*s) return;
	const char *colon = strchr(s, ':');
	if (colon && colon > s && s[0] != '/') {
		static const char *const schemes[] = {"http", "https", "file", "about", "data", "blob"};
		size_t k = (size_t)(colon - s);
		for (size_t i = 0; i < AK_LEN(schemes); i++)
			if (strlen(schemes[i]) == k && !g_ascii_strncasecmp(s, schemes[i], k)) {
				webkit_web_view_load_uri(w->view, s);
				return;
			}
	}
	// An asset (or a file): its relative links are its directory's.
	char *p = s[0] == '/' ? str_dup(ak_cstr(s)) : dsk_join(S(H.assets), s);
	char *abs = g_canonicalize_filename(p, NULL);
	char *uri = g_filename_to_uri(abs, NULL, NULL);
	if (uri) webkit_web_view_load_uri(w->view, uri);
	g_free(uri);
	g_free(abs);
	free(p);
}

static void run_load(web *w, const char *s, bool html) {
	if (html)
		webkit_web_view_load_html(w->view, s, NULL);
	else
		load(w, s);
}

// hold runs a load now, or once Block's filter has compiled, so the
// page's first requests are blocked too.
static void hold(web *w, ak_span s, bool html) {
	char *t = str_dup(s);
	if (w->pending && w->pending == w->block) {
		free(w->held);
		w->held = t;
		w->held_html = html;
		return;
	}
	run_load(w, t, html);
	free(t);
}

// released runs the load held while the filter compiled.
static void released(web *w) {
	w->pending = 0;
	char *t = w->held;
	if (!t) return;
	w->held = NULL;
	run_load(w, t, w->held_html);
	free(t);
}

static WebKitUserContentFilterStore *filter_store;

typedef struct {
	ak_id id;
	int gen;
} filter_wait;

static void on_filter_saved(GObject *src, GAsyncResult *res, gpointer data) {
	filter_wait *fw = data;
	GError *e = NULL;
	WebKitUserContentFilter *f = webkit_user_content_filter_store_save_finish((WebKitUserContentFilterStore *)src, res, &e);
	if (e) {
		lx_log("the WebView's Block didn't compile: %s", err_message(e));
		g_error_free(e);
	}
	web *w = web_of(node_get(fw->id));
	if (w && w->block == fw->gen) {  // else another filter, or the view went
		if (f) webkit_user_content_manager_add_filter(w->ucc, f);
		released(w);
	}
	if (f) webkit_user_content_filter_unref(f);
	free(fw);
}

// quote_meta appends s with what a regular expression reads specially
// escaped.
static void quote_meta(ak_buf *b, ak_span s) {
	for (size_t i = 0; i < s.len; i++) {
		if (strchr("\\.+*?()|[]{}^$", s.p[i])) ak_buf_u8(b, '\\');
		ak_buf_u8(b, (uint8_t)s.p[i]);
	}
}

// set_block compiles Block's patterns (u32 count, then u32-length
// strings) into a content filter: a host blocks it and its subdomains, a
// part of a URL any URL with it.
static void set_block(node *n, web *w, ak_span b) {
	webkit_user_content_manager_remove_all_filters(w->ucc);
	int gen = ++w->block;
	ak_str_vec pats = {0};
	ak_decode_strings((const uint8_t *)b.p, b.len, &pats);
	if (!pats.len) {
		ak_vec_free(&pats);
		released(w);
		return;
	}
	dsk_jw j = {0};
	dsk_jw_arr(&j);
	ak_buf f = {0};
	for (uint32_t i = 0; i < pats.len; i++) {
		ak_buf_reset(&f);
		bool path = memchr(pats.data[i].p, '/', pats.data[i].len) != NULL;
		if (!path) ak_buf_put(&f, "^[a-z]+://([^/:]+\\.)?", 21);
		quote_meta(&f, pats.data[i]);
		if (!path) ak_buf_put(&f, "[:/]", 4);
		dsk_jw_obj(&j);
		dsk_jw_key(&j, "trigger");
		dsk_jw_obj(&j);
		dsk_jw_key(&j, "url-filter");
		dsk_jw_strn(&j, (const char *)f.data, f.len);
		dsk_jw_end(&j);
		dsk_jw_key(&j, "action");
		dsk_jw_obj(&j);
		dsk_jw_key(&j, "type");
		dsk_jw_str(&j, "block");
		dsk_jw_end(&j);
		dsk_jw_end(&j);
	}
	dsk_jw_aend(&j);
	ak_buf_free(&f);
	ak_vec_free(&pats);
	size_t len;
	char *js = dsk_jw_take(&j, &len);
	if (!filter_store) {
		char *dir = dsk_join(lx_cache_dir(), "content-filters");
		filter_store = webkit_user_content_filter_store_new(dir);
		free(dir);
	}
	w->pending = gen;
	GBytes *bytes = g_bytes_new(js, len);
	free(js);
	char id[48];
	snprintf(id, sizeof id, "asuka-block-%u", (unsigned)n->id);
	filter_wait *fw = malloc(sizeof *fw);
	if (!fw) abort();
	*fw = (filter_wait){n->id, gen};
	webkit_user_content_filter_store_save(filter_store, id, bytes, NULL, on_filter_saved, fw);
	g_bytes_unref(bytes);
}

static void report_missing(void *ctx) {
	node *n = node_get((ak_id)(uintptr_t)ctx);
	if (n) lx_emit(n, (ak_event){.kind = AK_EVENT_FAILED, .s = ak_cstr(webkit())});
}

static bool web_prop(node *n, ak_prop p, const ak_value *v) {
	if (n->kind != AK_KIND_WEB_VIEW) return false;
	web *w = web_of(n);
	if (!w) return true;
	if (w->missing) {
		if ((p == AK_PROP_WEB_VIEW_URL || p == AK_PROP_WEB_VIEW_HTML) && !w->reported) {
			w->reported = true;
			lx_after_core(report_missing, (void *)(uintptr_t)n->id);
		}
		return true;
	}
	switch (p) {
	case AK_PROP_WEB_VIEW_URL: hold(w, v->s, false); break;
	case AK_PROP_WEB_VIEW_HTML: hold(w, v->s, true); break;
	case AK_PROP_WEB_VIEW_PRELOAD:
		webkit_user_content_manager_remove_all_scripts(w->ucc);
		add_script(w, BRIDGE_JS);
		if (v->s.len) {
			char *js = str_dup(v->s);
			add_script(w, js);
			free(js);
		}
		break;
	case AK_PROP_WEB_VIEW_USER_AGENT: {
		char *ua = v->s.len ? str_dup(v->s) : NULL;
		webkit_settings_set_user_agent(webkit_web_view_get_settings(w->view), ua);  // NULL: WebKit's own
		free(ua);
		break;
	}
	case AK_PROP_WEB_VIEW_BLOCK: set_block(n, w, v->s); break;
	case AK_PROP_WEB_VIEW_REQUESTS:
		if (v->i && !w->req_id) {
			w->req_id = g_signal_connect(w->view, "resource-load-started", G_CALLBACK(on_request), n);
		} else if (!v->i && w->req_id) {
			g_signal_handler_disconnect(w->view, w->req_id);
			w->req_id = 0;
		}
		break;
	default: break;
	}
	return true;
}

static bool web_command(node *n, ak_cmd c, ak_span args) {
	web *w = web_of(n);
	if (!w) return false;
	switch (c) {
	case AK_CMD_EVAL:
		if (w->view) {
			char *js = str_dup(args);
			webkit_web_view_evaluate_javascript(w->view, js, -1, NULL, NULL, NULL, NULL, NULL);
			free(js);
		}
		return true;
	case AK_CMD_WEB_NAVIGATE:
		if (!w->view || !args.len) return true;
		switch (args.p[0]) {
		case 0: webkit_web_view_go_back(w->view); break;
		case 1: webkit_web_view_go_forward(w->view); break;
		case 2: webkit_web_view_reload(w->view); break;
		case 3: webkit_web_view_stop_loading(w->view); break;
		}
		return true;
	default:
		return false;
	}
}

// ---- The web module and asuka/js ----

// eval_body runs s in the page's world and answers with its value as
// JSON: the last statement's, or what its promise resolves to. A page
// whose Content Security Policy blocks eval answers EVAL_BLOCKED instead,
// before any of s has run.
#define EVAL_BODY "try { (0, eval)(\"0\") } catch { return \"!eval\" }\nconst v = await (0, eval)(s); return JSON.stringify(v === undefined ? null : v)"
// EVAL_BLOCKED is EVAL_BODY's answer on a page that blocks eval: not JSON,
// so no value of a script's is mistaken for it.
#define EVAL_BLOCKED "!eval"

typedef void (*eval_done)(void *ctx, const char *err, const char *json);

typedef struct {
	WebKitWebView *view;
	char *script;
	eval_done done;
	void *ctx;
} eval_call;

static void eval_end(eval_call *c, const char *err, const char *json) {
	c->done(c->ctx, err, json);
	g_object_unref(c->view);
	free(c->script);
	free(c);
}

static void on_native_eval(GObject *src, GAsyncResult *res, gpointer data) {
	eval_call *c = data;
	GError *e = NULL;
	JSCValue *val = webkit_web_view_evaluate_javascript_finish((WebKitWebView *)src, res, &e);
	if (e) {
		const char *msg = e->domain == webkit_javascript_error_quark() && e->code == ERR_JS_INVALID_RESULT
		                      ? "the script's value is a promise, a function or another value WebKit can't return: the page's Content Security Policy blocks eval, so the script ran as EvalJS runs it, and a promise isn't awaited"
		                      : err_message(e);
		eval_end(c, msg, NULL);
		g_error_free(e);
		return;
	}
	char *s = val ? jsc_value_to_json(val, 0) : NULL;
	eval_end(c, NULL, s && *s ? s : "null");  // undefined: null
	g_free(s);
	if (val) g_object_unref(val);
}

static void on_async_eval(GObject *src, GAsyncResult *res, gpointer data) {
	eval_call *c = data;
	GError *e = NULL;
	JSCValue *val = webkit_web_view_call_async_javascript_function_finish((WebKitWebView *)src, res, &e);
	if (e) {
		eval_end(c, err_message(e), NULL);
		g_error_free(e);
		return;
	}
	char *s = val ? jsc_value_to_string(val) : NULL;
	if (val) g_object_unref(val);
	if (s && !strcmp(s, EVAL_BLOCKED)) {
		// On a page that blocks eval, it runs as EvalJS runs it
		// (webkit_web_view_evaluate_javascript, which no Content Security
		// Policy applies to): its last statement's value.
		g_free(s);
		webkit_web_view_evaluate_javascript(c->view, c->script, -1, NULL, NULL, NULL, on_native_eval, c);
		return;
	}
	eval_end(c, NULL, s && *s ? s : "null");
	g_free(s);
}

// eval_in runs script in a web view and answers with its value as JSON,
// or what it threw.
static void eval_in(WebKitWebView *view, ak_span script, eval_done done, void *ctx) {
	eval_call *c = malloc(sizeof *c);
	if (!c) abort();
	*c = (eval_call){g_object_ref(view), str_dup(script), done, ctx};
	GVariantDict d;
	g_variant_dict_init(&d, NULL);
	g_variant_dict_insert_value(&d, "s", g_variant_new_string(c->script));
	GVariant *args = g_variant_ref_sink(g_variant_dict_end(&d));
	webkit_web_view_call_async_javascript_function(view, EVAL_BODY, -1, args, NULL, NULL, NULL, on_async_eval, c);
	g_variant_unref(args);
}

static void reply_eval(void *ctx, const char *err, const char *json) {
	if (err)
		reply_err(ctx, "%s", err);
	else
		reply_str(ctx, json);
}

// web.eval: u32 a WebView's node id, then the script: its value as JSON.
static void web_eval(ak_span args, reply *r) {
	if (args.len < 4) {
		reply_err(r, "bad arguments");
		return;
	}
	ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
	web *w = web_of(node_get(ak_read_u32(&rd)));
	if (!w) {
		reply_err(r, "not a WebView");
		return;
	}
	if (!w->view) {
		reply_err(r, "%s", S(webkit()));
		return;
	}
	eval_in(w->view, ak_strn(args.p + 4, args.len - 4), reply_eval, r);
}

static void on_cookies(GObject *src, GAsyncResult *res, gpointer data) {
	reply *r = data;
	GError *e = NULL;
	GList *list = webkit_cookie_manager_get_cookies_finish((WebKitCookieManager *)src, res, &e);
	if (e) {
		reply_err(r, "%s", err_message(e));
		g_error_free(e);
		return;
	}
	ak_buf b = {0};
	for (GList *l = list; l; l = l->next) {
		if (b.len) ak_buf_put(&b, "; ", 2);
		const char *k = soup_cookie_get_name(l->data), *v = soup_cookie_get_value(l->data);
		ak_buf_put(&b, k, strlen(k));
		ak_buf_u8(&b, '=');
		ak_buf_put(&b, v, strlen(v));
		soup_cookie_free(l->data);
	}
	g_list_free(list);
	reply_ok(r, b.data, b.len);
	ak_buf_free(&b);
}

// web.cookies: a URL: the cookies the WebViews have for it, as a Cookie
// header.
static void web_cookies(ak_span args, reply *r) {
	char *u = str_dup(args);
	GUri *gu = g_uri_parse(u, 0, NULL);
	bool ok = gu && !str_empty(g_uri_get_host(gu));
	if (gu) g_uri_unref(gu);
	if (!ok) {
		free(u);
		reply_ok(r, NULL, 0);
		return;
	}
	if (webkit() || !soup_cookie_get_name) {
		free(u);
		reply_err(r, "%s", webkit() ? webkit() : "libsoup 3 isn't installed");
		return;
	}
	WebKitCookieManager *mgr = webkit_network_session_get_cookie_manager(webkit_network_session_get_default());
	webkit_cookie_manager_get_cookies(mgr, u, NULL, on_cookies, r);
	free(u);
}

// A asuka/js run: a web view that's never shown, and who waits for it.
typedef struct {
	WebKitWebView *view;
	char *src;
	gulong handler;
	guint timer;
	reply *r;
} script_run;

static void script_end(script_run *s, const char *err, const char *json) {
	if (!s->r) return;
	reply *r = s->r;
	s->r = NULL;
	if (s->timer) g_source_remove(s->timer);
	s->timer = 0;
	g_signal_handler_disconnect(s->view, s->handler);
	webkit_web_view_stop_loading(s->view);
	g_object_unref(s->view);
	free(s->src);
	free(s);
	reply_eval(r, err, json);
}

static void script_evaluated(void *ctx, const char *err, const char *json) { script_end(ctx, err, json); }

static void on_script_load(WebKitWebView *v, int ev, script_run *s) {
	if (ev != LOAD_FINISHED || !s->r) return;
	eval_in(v, ak_cstr(s->src), script_evaluated, s);
}

static gboolean script_timeout(gpointer data) {
	script_run *s = data;
	s->timer = 0;
	script_end(s, "timeout", NULL);
	return 0;
}

// js.eval: u32 timeout ms, then the script: run in a web view made for it
// and let go after, its value as JSON, or what it threw (or "timeout").
static void js_eval(ak_span args, reply *r) {
	if (args.len < 4) {
		reply_err(r, "bad arguments");
		return;
	}
	if (webkit()) {
		reply_err(r, "%s", webkit());
		return;
	}
	ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
	uint32_t ms = ak_read_u32(&rd);
	script_run *s = calloc(1, sizeof *s);
	if (!s) abort();
	s->view = g_object_ref_sink(webkit_web_view_new());
	s->src = str_dup(ak_strn(args.p + 4, args.len - 4));
	s->r = r;
	s->handler = g_signal_connect(s->view, "load-changed", G_CALLBACK(on_script_load), s);
	s->timer = g_timeout_add(ms, script_timeout, s);
	webkit_web_view_load_html(s->view, "<!doctype html><html></html>", NULL);
}

static const method_t web_methods[] = {
	{"web", "eval", web_eval},
	{"web", "cookies", web_cookies},
	{"js", "eval", js_eval},
};

static void web_modules(void) { lx_register_methods(web_methods, AK_LEN(web_methods)); }

const area lx_area_web = {
	.name = "web",
	.create = web_create,
	.prop = web_prop,
	.command = web_command,
	.modules = web_modules,
};
