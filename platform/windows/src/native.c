// native.c: what the app's native packages' C meets in the host (RFC 0008,
// "Native packages on the desktop"): the host's half of asuka_desktop.h
// (posting, a view's window, colors, the app's names and directories),
// Native views (a package's widget, which the registry makes, in the view
// of the host's that holds it), and the core's native_* callbacks: the
// registry's (platform/desktop/src/native.c).
#include "nav.h"

// ---- The host's half of asuka_desktop.h ----

static DWORD main_thread;

void asuka_desktop_post(void (*fn)(void *ctx), void *ctx) { wh_post(fn, ctx); }  // the core's queue too: one FIFO

bool asuka_desktop_on_main(void) { return GetCurrentThreadId() == main_thread; }

void *asuka_desktop_window(uint32_t view) {
	if (view == 0) return H.window;
	node *n = wh_node(view);
	while (n && !n->hwnd) n = n->parent;  // a view without a window is placed in its ancestor's
	if (!n) return NULL;
	HWND top = GetAncestor(n->hwnd, GA_ROOT);
	return top && top != H.park ? top : NULL;  // parked: in no window yet
}

void *asuka_desktop_view(uint32_t view) {
	node *n = view ? wh_node(view) : NULL;
	return n ? n->hwnd : NULL;
}

void *asuka_desktop_webview(uint32_t view) { return view ? wh_web_view(wh_node(view)) : NULL; }

uint32_t asuka_desktop_color(uint64_t color) {
	rgba c = H.theme.label;  // the platform's default: the text's
	if (color) wh_resolve((ak_color)color, &c);
	return (uint32_t)c.r << 24 | (uint32_t)c.g << 16 | (uint32_t)c.b << 8 | c.a;
}

bool asuka_desktop_dark(void) { return H.dark; }

const char *asuka_desktop_app_name(void) { return H.app_name ? H.app_name : ""; }
const char *asuka_desktop_app_id(void) { return H.app_id ? H.app_id : ""; }
const char *asuka_desktop_app_version(void) { return H.config.version ? H.config.version : ""; }
const char *asuka_desktop_app_build(void) { return H.config.build ? H.config.build : ""; }

// made reports a directory, made the first time it's asked for.
static const char *made(const char *dir, bool *done) {
	if (!dir) return "";
	if (!*done) {
		dsk_mkdirs(dir);
		*done = true;
	}
	return dir;
}

const char *asuka_desktop_data_dir(void) {
	static bool done;
	return made(H.dirs.data, &done);
}

const char *asuka_desktop_config_dir(void) {
	static bool done;
	return made(H.dirs.config, &done);
}

const char *asuka_desktop_cache_dir(void) {
	static bool done;
	return made(H.dirs.cache, &done);
}

const char *asuka_desktop_logs_dir(void) {
	static bool done;
	return made(H.dirs.logs, &done);
}

const char *asuka_desktop_assets_dir(void) {
	static char *abs;
	if (!abs) {
		const char *a = H.assets ? H.assets : "assets";
		wchar_t *w = wh_wide(a, (int)strlen(a)), full[MAX_PATH * 4];
		DWORD n = GetFullPathNameW(w, (DWORD)WH_LEN(full), full, NULL);
		abs = n > 0 && n < WH_LEN(full) ? wh_utf8(full, (int)n, NULL) : wh_strdup(H.assets ? H.assets : "assets");
		free(w);
	}
	return abs;
}

// ---- Native views ----

// A Native view shows a package's widget: the registry makes it (a child
// window) in the view's window, which holds it and keeps it filling it
// (it's the node's control). Its props go to the registry as they come;
// its events come back through asuka_native_emit (the registry's); its
// package measures it, or the host does, as it would a control of its
// own: a button's ideal size, else the size the package made it.
struct wh_native {
	char *kind;  // "<path>#<Name>"
	HWND widget;
};

void wh_set_native_prop(node *n, uint16_t p, const ak_value *v) {
	switch (p) {
	case AK_PROP_NATIVE_TYPE: {
		if (n->native) return;
		wh_native *nv = wh_alloc(sizeof *nv);
		nv->kind = wh_strndup(v->s.p, v->s.len);
		n->native = nv;
		nv->widget = asuka_native_view(nv->kind, n->id, n->hwnd);
		if (!nv->widget) {
			wh_log("no native view \"%s\" is registered (a native package's desktop C)", nv->kind);
			return;
		}
		if (GetParent(nv->widget) != n->hwnd) SetParent(nv->widget, n->hwnd);
		n->ctl = nv->widget;
		EnableWindow(nv->widget, !n->disabled);
		wh_layout_field(n);
		break;
	}
	case AK_PROP_NATIVE_PROP:
		if (n->native && n->native->widget) asuka_native_view_prop(n->native->widget, (const uint8_t *)v->s.p, v->s.len);
		break;
	}
}

// unbounded is NaN for a size the layout doesn't know (NaN, infinite or
// huge), as asuka_native_view_measure takes it.
static float unbounded(float f) { return f != f || f > 1e30f || f < -1e30f ? NAN : f; }

void wh_measure_native(node *n, float w, float h, float *ow, float *oh) {
	*ow = *oh = 0;
	if (!n->native || !n->native->widget) return;
	HWND widget = n->native->widget;
	if (asuka_native_view_measure(widget, unbounded(w), unbounded(h), ow, oh)) return;
	wchar_t cls[32] = {0};
	GetClassNameW(widget, cls, (int)WH_LEN(cls));
	SIZE ideal = {0, 0};
	if (!_wcsicmp(cls, L"Button") && SendMessageW(widget, BCM_GETIDEALSIZE, 0, (LPARAM)&ideal) && ideal.cx > 0) {
		*ow = wh_pt(n, ideal.cx), *oh = wh_pt(n, ideal.cy);
		return;
	}
	RECT r;
	GetWindowRect(widget, &r);
	*ow = wh_pt(n, r.right - r.left), *oh = wh_pt(n, r.bottom - r.top);
}

// wh_native_message gives the package's widget what it tells its parent
// (the view): reflected back to it as OCM_ messages (OCM__BASE plus the
// message, ATL's and WinForms' convention), so that a widget that's a
// control hears its own notifications. A nonzero answer is taken.
LRESULT wh_native_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	*handled = false;
	if (!n->native || !n->native->widget) return 0;
	HWND w = n->native->widget, from = NULL;
	switch (m) {
	case WM_COMMAND: case WM_HSCROLL: case WM_VSCROLL:
	case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLORLISTBOX:
		from = (HWND)lp;
		break;
	case WM_NOTIFY:
		from = ((NMHDR *)lp)->hwndFrom;
		break;
	case WM_DRAWITEM:
		from = ((DRAWITEMSTRUCT *)lp)->hwndItem;
		break;
	default:
		return 0;
	}
	if (from != w) return 0;
	LRESULT r = SendMessageW(w, (WM_USER + 0x1c00) + m, wp, lp);  // OCM__BASE + m
	*handled = r != 0;
	return r;
}

void wh_destroy_native(node *n) {
	wh_native *nv = n->native;
	if (!nv) return;
	if (nv->widget) asuka_native_view_destroy(nv->kind, nv->widget);  // the widget goes with the view's window
	free(nv->kind);
	free(nv);
	n->native = NULL;
}

// ---- The core's native_* callbacks: the registry's ----

void wh_native_host_callbacks(asuka_host_callbacks *cb) {
	main_thread = GetCurrentThreadId();
	asuka_native_callbacks(cb);
}
