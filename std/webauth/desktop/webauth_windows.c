// asuka/webauth on Windows: a window owned by the one the user works in
// (which waits, disabled, while it's open), with WebView2 in it: the
// app's web profile, or with ephemeral a throwaway one in a folder of its
// own.
#include "asuka_windows.h"
#include <shellapi.h>
#include "webauth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// WebView2's vtable slots the sign-in uses.
enum {
	env_create_controller = 3,
	ctl_put_is_visible = 4,
	ctl_put_bounds = 6,
	ctl_close = 24,
	ctl_get_webview = 25,
	wv_navigate = 5,
	wv_add_navigation_starting = 7,
	wv_add_new_window_requested = 44,
	wv_add_document_title_changed = 46,
	wv_get_document_title = 48,
	nav_get_uri = 3,
	nav_put_cancel = 5,
	newwin_get_uri = 3,
	newwin_put_handled = 6,
};

typedef struct {
	HWND hwnd, owner;
	void *controller, *view;
	void *env;  // an ephemeral profile's
	wchar_t *dir;  // its folder
} win;

static char *take(wchar_t *p) {
	char *s = asuka_narrow(p, -1);
	CoTaskMemFree(p);
	return s;
}

static bool on_message(void *ctx, HWND hwnd, UINT m, WPARAM wp, LPARAM lp, LRESULT *r) {
	signin *s = ctx;
	win *w = s ? s->platform : NULL;
	if (!w) return false;
	if (m == WM_SIZE && w->controller) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		ASUKA_SLOT(w->controller, ctl_put_bounds, HRESULT(STDMETHODCALLTYPE *)(void *, RECT))(w->controller, rc);
	} else if (m == WM_CLOSE) {
		webauth_finish(s, NULL, NULL);
		*r = 0;
		return true;
	}
	return false;
}

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

static void navigation_starting(void *ctx, void *sender, void *args) {
	signin *s = ctx;
	wchar_t *p = NULL;
	ASUKA_SLOT(args, nav_get_uri, HRESULT(STDMETHODCALLTYPE *)(void *, wchar_t **))(args, &p);
	char *to = p ? take(p) : asuka_strdup("");
	if (s->done || !webauth_is_callback(to, s->scheme)) {
		free(to);
		return;
	}
	ASUKA_SLOT(args, nav_put_cancel, HRESULT(STDMETHODCALLTYPE *)(void *, BOOL))(args, TRUE);
	arrived *a = malloc(sizeof *a);
	*a = (arrived){s, to};
	asuka_desktop_post(arrive, a);  // not inside WebView2's callback
}

// new_window: a popup (a provider's "sign in with...") opens here.
static void new_window(void *ctx, void *sender, void *args) {
	signin *s = ctx;
	win *w = s->platform;
	wchar_t *p = NULL;
	ASUKA_SLOT(args, newwin_get_uri, HRESULT(STDMETHODCALLTYPE *)(void *, wchar_t **))(args, &p);
	ASUKA_SLOT(args, newwin_put_handled, HRESULT(STDMETHODCALLTYPE *)(void *, BOOL))(args, TRUE);
	if (p && w && w->view) ASUKA_SLOT(w->view, wv_navigate, HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *))(w->view, p);
	if (p) CoTaskMemFree(p);
}

static void title_changed(void *ctx, void *sender, void *args) {
	signin *s = ctx;
	win *w = s->platform;
	wchar_t *p = NULL;
	if (!w || s->done) return;
	ASUKA_SLOT(w->view, wv_get_document_title, HRESULT(STDMETHODCALLTYPE *)(void *, wchar_t **))(w->view, &p);
	if (p && *p) SetWindowTextW(w->hwnd, p);
	if (p) CoTaskMemFree(p);
}

static void add(signin *s, int slot, void (*fn)(void *, void *, void *)) {
	win *w = s->platform;
	int64_t token;
	void *h = asuka_win_handler(fn, s, NULL);
	ASUKA_SLOT(w->view, slot, HRESULT(STDMETHODCALLTYPE *)(void *, void *, int64_t *))(w->view, h, &token);
	asuka_com_release(h);
}

static void fail(signin *s, const char *what) {
	asuka_error e = {0};
	asuka_error_failed(&e, "webauth", 0, "webauth: %s", what);
	webauth_finish(s, NULL, &e);
}

static void controller_made(void *ctx, void *a, void *b) {
	signin *s = ctx;
	win *w = s->platform;
	if (FAILED((HRESULT)(INT_PTR)a) || !b || s->done || !w) {
		if (!s->done) fail(s, "couldn't start the sign-in");
		return;
	}
	w->controller = b;
	ASUKA_SLOT(b, 1, ULONG(STDMETHODCALLTYPE *)(void *))(b);
	ASUKA_SLOT(b, ctl_get_webview, HRESULT(STDMETHODCALLTYPE *)(void *, void **))(b, &w->view);
	RECT rc;
	GetClientRect(w->hwnd, &rc);
	ASUKA_SLOT(b, ctl_put_bounds, HRESULT(STDMETHODCALLTYPE *)(void *, RECT))(b, rc);
	add(s, wv_add_navigation_starting, navigation_starting);
	add(s, wv_add_new_window_requested, new_window);
	add(s, wv_add_document_title_changed, title_changed);
	ASUKA_SLOT(b, ctl_put_is_visible, HRESULT(STDMETHODCALLTYPE *)(void *, BOOL))(b, TRUE);
	wchar_t *link = asuka_wide(s->link);
	ASUKA_SLOT(w->view, wv_navigate, HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *))(w->view, link);
	free(link);
}

static void with_env(void *ctx, void *env, HRESULT hr) {
	signin *s = ctx;
	win *w = s->platform;
	if (s->done || !w) return;
	if (!env) return fail(s, "couldn't start the sign-in: WebView2 isn't there");
	void *h = asuka_win_handler(controller_made, s, NULL);
	hr = ASUKA_SLOT(env, env_create_controller, HRESULT(STDMETHODCALLTYPE *)(void *, HWND, void *))(env, w->hwnd, h);
	asuka_com_release(h);
	if (FAILED(hr)) fail(s, "couldn't start the sign-in");
}

static void with_own_env(void *ctx, void *env, HRESULT hr) {
	signin *s = ctx;
	win *w = s->platform;
	if (w) w->env = env;
	else asuka_com_release(env);
	with_env(ctx, env, hr);
}

void webauth_show(signin *s) {
	win *w = calloc(1, sizeof *w);
	s->platform = w;
	w->owner = (HWND)asuka_desktop_window(0);
	UINT dpi = 96;
	UINT(WINAPI * dpi_for)(HWND) = NULL;
	*(FARPROC *)&dpi_for = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	if (dpi_for && w->owner) dpi = dpi_for(w->owner);
	int width = MulDiv(480, (int)dpi, 96), height = MulDiv(640, (int)dpi, 96), x = CW_USEDEFAULT, y = CW_USEDEFAULT;
	if (w->owner) {
		RECT r;
		GetWindowRect(w->owner, &r);
		x = r.left + (r.right - r.left - width) / 2;
		y = r.top + (r.bottom - r.top - height) / 2;
	}
	wchar_t *title = asuka_wide(s->host);
	w->hwnd = asuka_win_window(0, title, WS_OVERLAPPEDWINDOW, x, y, width, height, w->owner, on_message, s);
	free(title);
	if (!w->hwnd) return fail(s, "no window for the sign-in");
	if (w->owner) EnableWindow(w->owner, FALSE);
	ShowWindow(w->hwnd, SW_SHOW);
	if (!s->ephemeral) {
		asuka_win_webview2(with_env, s);
		return;
	}
	// A profile of its own, in a new folder, thrown away after.
	wchar_t local[MAX_PATH];
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) wcscpy(local, L".");
	wchar_t *app = asuka_wide(asuka_desktop_app_name());
	w->dir = malloc(3 * MAX_PATH * sizeof(wchar_t));
	swprintf(w->dir, 3 * MAX_PATH, L"%ls\\%ls", local, app);
	CreateDirectoryW(w->dir, NULL);
	wcscat(w->dir, L"\\WebView2-signin");
	CreateDirectoryW(w->dir, NULL);
	size_t n = wcslen(w->dir);
	swprintf(w->dir + n, 3 * MAX_PATH - n, L"\\ephemeral-%lu-%lu", (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
	CreateDirectoryW(w->dir, NULL);
	free(app);
	asuka_win_webview2_at(w->dir, with_own_env, s);
}

// remove_tree removes a folder and what's in it.
static void remove_tree(const wchar_t *dir) {
	size_t n = wcslen(dir);
	wchar_t *from = calloc(n + 2, sizeof(wchar_t));  // double NUL-terminated
	wcscpy(from, dir);
	SHFILEOPSTRUCTW op;
	memset(&op, 0, sizeof op);
	op.wFunc = FO_DELETE;
	op.pFrom = from;
	op.fFlags = FOF_NO_UI;
	SHFileOperationW(&op);
	free(from);
}

// The browser's processes let the folder go once they've exited.
static void sweep(void *p) {
	wchar_t *dir = p;
	for (int i = 0; i < 20 && GetFileAttributesW(dir) != INVALID_FILE_ATTRIBUTES; i++) {
		remove_tree(dir);
		Sleep(500);
	}
	free(dir);
}

typedef struct {
	signin *s;
	win *w;
} closing;

// close_later closes the window on a later turn: finishing is often
// inside WebView2's callbacks.
static void close_later(void *p) {
	closing *c = p;
	win *w = c->w;
	if (w->controller) {
		ASUKA_SLOT(w->controller, ctl_close, HRESULT(STDMETHODCALLTYPE *)(void *))(w->controller);
		asuka_com_release(w->view);
		asuka_com_release(w->controller);
	}
	if (w->owner) {
		EnableWindow(w->owner, TRUE);
		SetForegroundWindow(w->owner);
	}
	if (w->hwnd) {
		DestroyWindow(w->hwnd);
	}
	asuka_com_release(w->env);
	if (w->dir && !asuka_desktop_spawn(sweep, w->dir)) free(w->dir);
	free(w);
	webauth_free(c->s);
	free(c);
}

void webauth_close(signin *s) {
	win *w = s->platform;
	s->platform = NULL;
	closing *c = malloc(sizeof *c);
	*c = (closing){s, w};
	if (!w) {
		webauth_free(s);
		free(c);
		return;
	}
	asuka_desktop_post(close_later, c);
}
