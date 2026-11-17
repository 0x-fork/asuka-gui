// webview2.c (platform/desktop/src): asuka_windows.h's WebView2: the
// process's environment, and pages that are never shown (platform/js's,
// platform/print's), through WebView2's COM interfaces by vtable slot.
// Built on Windows only.
#ifdef _WIN32
#include "asuka_windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The vtable slots a hidden page uses.
enum {
	env_create_controller = 3,
	ctl_put_bounds = 6,
	ctl_close = 24,
	ctl_get_webview = 25,
	wv_navigate_to_string = 6,
	wv_add_navigation_completed = 15,
	wv_execute_script = 29,
	wv_call_devtools = 36,
	wv16_show_print_ui = 114,
	nav_is_success = 3,
	page_width = 816,  // a Letter page at 96 dpi
	page_height = 1056,
};

static const GUID iid_webview2_16 = {0x0eb34dc9, 0x9f91, 0x41e1, {0x86, 0x39, 0x95, 0xcd, 0x59, 0x43, 0x90, 0x6b}};

typedef void (*env_done)(void *, void *, HRESULT);

static struct {
	void *env;
	HRESULT hr;
	bool started, made;
	struct waiting {
		env_done fn;
		void *ctx;
	} *wait;
	size_t nwait;
} web;

// runtime is the WebView2 Runtime's client library, found as
// WebView2Loader.dll finds it: EdgeUpdate records where each channel's
// runtime is, per machine and per user.
static wchar_t *runtime_dll(void) {
#if defined(_M_ARM64) || defined(__aarch64__)
	const wchar_t *arch = L"arm64";
#else
	const wchar_t *arch = L"x64";
#endif
	wchar_t dir[MAX_PATH];
	DWORD n = GetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", dir, MAX_PATH);
	wchar_t *out = malloc(2 * MAX_PATH * sizeof(wchar_t));
	if (n > 0 && n < MAX_PATH) {
		swprintf(out, 2 * MAX_PATH, L"%ls\\EBWebView\\%ls\\EmbeddedBrowserWebView.dll", dir, arch);
		return out;
	}
	static const wchar_t *channels[] = {L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}", L"{2CD8A007-E189-409D-A2C8-9AF4EF3C72AA}",
	                                    L"{0D50BFEC-CD6A-4F9A-964C-C7416E3ACB10}", L"{65C35B14-6C1D-4122-AC46-7148CC9D6497}"};
	HKEY roots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
	for (size_t c = 0; c < 4; c++) {
		for (size_t r = 0; r < 2; r++) {
			wchar_t key[256];
			swprintf(key, 256, L"SOFTWARE\\Microsoft\\EdgeUpdate\\ClientState\\%ls", channels[c]);
			DWORD size = sizeof dir;
			if (RegGetValueW(roots[r], key, L"EBWebView", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, NULL, dir, &size) != ERROR_SUCCESS) continue;
			swprintf(out, 2 * MAX_PATH, L"%ls\\EBWebView\\%ls\\EmbeddedBrowserWebView.dll", dir, arch);
			if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return out;
		}
	}
	free(out);
	return NULL;
}

static void made(void *ctx, void *a, void *b) {
	HRESULT hr = (HRESULT)(INT_PTR)a;
	web.made = true;
	web.hr = FAILED(hr) || !b ? (FAILED(hr) ? hr : E_FAIL) : S_OK;
	if (SUCCEEDED(web.hr)) {
		web.env = b;
		ASUKA_SLOT(b, 1, ULONG(STDMETHODCALLTYPE *)(void *))(b);  // AddRef: the process's
	}
	struct waiting *w = web.wait;
	size_t n = web.nwait;
	web.wait = NULL;
	web.nwait = 0;
	for (size_t i = 0; i < n; i++) w[i].fn(w[i].ctx, web.env, web.hr);
	free(w);
}

// create makes an environment of data's, answering through handler h's
// Invoke(hr, env): false (with *hr) when the runtime isn't there.
static bool create(const wchar_t *data, void *h, HRESULT *hr) {
	wchar_t *dll = runtime_dll();
	HMODULE m = dll ? LoadLibraryW(dll) : NULL;
	free(dll);
	HRESULT(STDMETHODCALLTYPE * make)(int, int, const wchar_t *, void *, void *) = NULL;
	if (m) *(FARPROC *)&make = GetProcAddress(m, "CreateWebViewEnvironmentWithOptionsInternal");
	if (!make) {
		*hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		return false;
	}
	*hr = make(1, 0, data, NULL, h);
	return SUCCEEDED(*hr);
}

typedef struct {
	env_done fn;
	void *ctx;
} own_env;

static void own_made(void *ctx, void *a, void *b) {
	own_env *o = ctx;
	HRESULT hr = (HRESULT)(INT_PTR)a;
	if (SUCCEEDED(hr) && b) ASUKA_SLOT(b, 1, ULONG(STDMETHODCALLTYPE *)(void *))(b);  // the caller's
	o->fn(o->ctx, SUCCEEDED(hr) ? b : NULL, SUCCEEDED(hr) && b ? S_OK : FAILED(hr) ? hr : E_FAIL);
	o->fn = NULL;
}

void asuka_win_webview2_at(const wchar_t *folder, void (*done)(void *ctx, void *env, HRESULT hr), void *ctx) {
	own_env *o = malloc(sizeof *o);
	*o = (own_env){done, ctx};
	void *h = asuka_win_handler(own_made, o, free);
	HRESULT hr;
	bool started = create(folder, h, &hr);
	if (!started && o->fn) done(ctx, NULL, hr);
	asuka_com_release(h);
}

void asuka_win_webview2(void (*done)(void *ctx, void *env, HRESULT hr), void *ctx) {
	if (web.made) {
		done(ctx, web.env, web.hr);
		return;
	}
	web.wait = realloc(web.wait, (web.nwait + 1) * sizeof *web.wait);
	web.wait[web.nwait++] = (struct waiting){done, ctx};
	if (web.started) return;
	web.started = true;
	// Videos play without a click, as a WebView's autoPlay asks.
	SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--autoplay-policy=no-user-gesture-required");
	wchar_t local[MAX_PATH];
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) wcscpy(local, L".");
	wchar_t *app = asuka_wide(asuka_desktop_app_name());
	wchar_t data[2 * MAX_PATH];
	swprintf(data, 2 * MAX_PATH, L"%ls\\%ls", local, app);
	CreateDirectoryW(data, NULL);
	wcscat(data, L"\\WebView2");
	CreateDirectoryW(data, NULL);
	free(app);
	void *h = asuka_win_handler(made, NULL, NULL);
	HRESULT hr;
	if (!create(data, h, &hr) && !web.made) made(NULL, (void *)(INT_PTR)hr, NULL);
	asuka_com_release(h);
}

struct asuka_web_page {
	HWND window;
	void *controller, *view;
	int64_t nav;  // the navigation's EventRegistrationToken
	void (*done)(void *, asuka_web_page *, asuka_error *);
	void *ctx;
	bool finished;
};

void asuka_win_web_page_close(asuka_web_page *p) {
	if (!p) return;
	if (p->controller) {
		ASUKA_SLOT(p->controller, ctl_close, HRESULT(STDMETHODCALLTYPE *)(void *))(p->controller);
		asuka_com_release(p->view);
		asuka_com_release(p->controller);
	}
	if (p->window) DestroyWindow(p->window);
	free(p);
}

static void fail_page(asuka_web_page *p, HRESULT hr, const char *what) {
	asuka_error e = {0};
	if (hr) asuka_win_fail(&e, hr, what);
	else asuka_error_failed(&e, "WebView2", 0, "%s", what);
	void (*done)(void *, asuka_web_page *, asuka_error *) = p->done;
	void *ctx = p->ctx;
	asuka_win_web_page_close(p);
	done(ctx, NULL, &e);
}

static void navigated(void *ctx, void *sender, void *args) {
	asuka_web_page *p = ctx;
	if (p->finished) return;
	p->finished = true;
	BOOL ok = FALSE;
	ASUKA_SLOT(args, nav_is_success, HRESULT(STDMETHODCALLTYPE *)(void *, BOOL *))(args, &ok);
	if (!ok) return fail_page(p, 0, "WebView2: the page couldn't load");
	p->done(p->ctx, p, NULL);
}

typedef struct {
	asuka_web_page *page;
	wchar_t *html;
} loading;

static void controller_made(void *ctx, void *a, void *b) {
	loading *l = ctx;
	asuka_web_page *p = l->page;
	HRESULT hr = (HRESULT)(INT_PTR)a;
	if (FAILED(hr) || !b) return fail_page(p, FAILED(hr) ? hr : E_FAIL, "WebView2 couldn't make a view");
	p->controller = b;
	ASUKA_SLOT(b, 1, ULONG(STDMETHODCALLTYPE *)(void *))(b);
	ASUKA_SLOT(b, ctl_get_webview, HRESULT(STDMETHODCALLTYPE *)(void *, void **))(b, &p->view);
	RECT r = {0, 0, page_width, page_height};
	ASUKA_SLOT(b, ctl_put_bounds, HRESULT(STDMETHODCALLTYPE *)(void *, RECT))(b, r);
	void *h = asuka_win_handler(navigated, p, NULL);
	ASUKA_SLOT(p->view, wv_add_navigation_completed, HRESULT(STDMETHODCALLTYPE *)(void *, void *, int64_t *))(p->view, h, &p->nav);
	asuka_com_release(h);
	ASUKA_SLOT(p->view, wv_navigate_to_string, HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *))(p->view, l->html);
}

static void free_loading(void *ctx) {
	loading *l = ctx;
	free(l->html);
	free(l);
}

typedef struct {
	asuka_web_page *page;
	char *html;
} env_wait;

static void env_ready(void *ctx, void *env, HRESULT hr) {
	env_wait *w = ctx;
	asuka_web_page *p = w->page;
	char *html = w->html;
	free(w);
	if (!env) {
		free(html);
		return fail_page(p, hr, "WebView2 isn't available");
	}
	loading *l = malloc(sizeof *l);
	*l = (loading){p, asuka_wide(html)};
	free(html);
	void *h = asuka_win_handler(controller_made, l, free_loading);
	hr = ASUKA_SLOT(env, env_create_controller, HRESULT(STDMETHODCALLTYPE *)(void *, HWND, void *))(env, p->window, h);
	asuka_com_release(h);
	if (FAILED(hr)) fail_page(p, hr, "WebView2 couldn't make a view");
}

void asuka_win_web_page(const char *html, void (*done)(void *ctx, asuka_web_page *page, asuka_error *err), void *ctx) {
	asuka_web_page *p = calloc(1, sizeof *p);
	p->done = done;
	p->ctx = ctx;
	p->window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, page_width, page_height, NULL, NULL, GetModuleHandleW(NULL), NULL);
	if (!p->window) return fail_page(p, HRESULT_FROM_WIN32(GetLastError()), "WebView2: no window for the page");
	env_wait *w = malloc(sizeof *w);
	*w = (env_wait){p, asuka_strdup(html)};
	asuka_win_webview2(env_ready, w);
}

void *asuka_win_web_page_view(asuka_web_page *p) { return p ? p->view : NULL; }

void asuka_win_webview_print(void *view, int kind) {
	void *v16 = asuka_com_query(view, &iid_webview2_16);
	if (v16) {
		ASUKA_SLOT(v16, wv16_show_print_ui, HRESULT(STDMETHODCALLTYPE *)(void *, int))(v16, kind);
		asuka_com_release(v16);
		return;
	}
	ASUKA_SLOT(view, wv_execute_script, HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *, void *))(view, L"window.print()", NULL);
}

void asuka_win_web_page_print(asuka_web_page *p, int kind) { asuka_win_webview_print(p->view, kind); }

typedef struct {
	void (*done)(void *, char *, asuka_error *);
	void *ctx;
	char *json;
	asuka_error err;
} evaluated;

// answer is evaluate's, on a later turn of the main thread: never inside
// WebView2's callback.
static void answer(void *p) {
	evaluated *e = p;
	e->done(e->ctx, e->json, e->err.status ? &e->err : NULL);
	asuka_error_free(&e->err);
	free(e);
}

// devtools_answered reads Runtime.evaluate's answer: the value's JSON
// (null for undefined), or what was thrown.
// field is key's string in json (NULL: none).
static char *field(const char *json, const char *key) {
	char *raw = asuka_json_get(json, key);
	char *s = asuka_json_string(raw);
	free(raw);
	return s;
}

static void devtools_answered(void *ctx, void *a, void *b) {
	evaluated *e = ctx;
	HRESULT hr = (HRESULT)(INT_PTR)a;
	if (FAILED(hr) || !b) {
		asuka_win_fail(&e->err, FAILED(hr) ? hr : E_FAIL, "WebView2: Runtime.evaluate");
	} else {
		char *r = asuka_narrow(b, -1);
		char *exc = asuka_json_get(r, "exceptionDetails");
		if (exc) {
			char *text = field(exc, "text");
			char *inner = asuka_json_get(exc, "exception");
			char *desc = inner ? field(inner, "description") : NULL;
			const char *msg = desc && *desc ? desc : text ? text : "an error";
			size_t n = strcspn(msg, "\n");  // the message, not the stack
			asuka_error_failed(&e->err, "js", 0, "%.*s", (int)n, msg);
			free(text), free(inner), free(desc), free(exc);
		} else {
			char *res = asuka_json_get(r, "result");
			char *v = res ? asuka_json_get(res, "value") : NULL;
			e->json = v ? v : asuka_strdup("null");
			free(res);
		}
		free(r);
	}
	asuka_desktop_post(answer, e);
}

void asuka_win_web_page_evaluate(asuka_web_page *p, const char *script, void (*done)(void *ctx, char *json, asuka_error *err), void *ctx) {
	evaluated *e = calloc(1, sizeof *e);
	e->done = done;
	e->ctx = ctx;
	asuka_writer params = {0};
	asuka_writer_put(&params, "{\"expression\":", 14);
	asuka_json_quote(&params, script);
	const char *rest = ",\"awaitPromise\":true,\"returnByValue\":true}";
	asuka_writer_put(&params, rest, strlen(rest));
	asuka_writer_u8(&params, 0);
	wchar_t *wp = asuka_wide((const char *)params.data);
	asuka_writer_free(&params);
	void *h = asuka_win_handler(devtools_answered, e, NULL);
	HRESULT hr = ASUKA_SLOT(p->view, wv_call_devtools, HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *, const wchar_t *, void *))(p->view, L"Runtime.evaluate", wp, h);
	asuka_com_release(h);
	free(wp);
	if (FAILED(hr)) {
		asuka_win_fail(&e->err, hr, "WebView2: Runtime.evaluate");
		asuka_desktop_post(answer, e);
	}
}

static void devtools_called(void *ctx, void *a, void *b) {
	evaluated *e = ctx;
	HRESULT hr = (HRESULT)(INT_PTR)a;
	if (FAILED(hr) || !b) asuka_win_fail(&e->err, FAILED(hr) ? hr : E_FAIL, "WebView2: a DevTools call");
	else e->json = asuka_narrow(b, -1);
	asuka_desktop_post(answer, e);
}

void asuka_win_web_page_devtools(asuka_web_page *p, const char *method, const char *params,
                                 void (*done)(void *ctx, char *json, asuka_error *err), void *ctx) {
	evaluated *e = calloc(1, sizeof *e);
	e->done = done;
	e->ctx = ctx;
	wchar_t *wm = asuka_wide(method), *wp = asuka_wide(params && *params ? params : "{}");
	void *h = asuka_win_handler(devtools_called, e, NULL);
	HRESULT hr = ASUKA_SLOT(p->view, wv_call_devtools, HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *, const wchar_t *, void *))(p->view, wm, wp, h);
	asuka_com_release(h);
	free(wm), free(wp);
	if (FAILED(hr)) {
		asuka_win_fail(&e->err, hr, "WebView2: a DevTools call");
		asuka_desktop_post(answer, e);
	}
}
#endif
