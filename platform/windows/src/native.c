// native.c: what the app's native packages' C meets in the host (RFC 0008,
// "Native packages on the desktop"): the host's half of asuka_desktop.h
// (posting, a view's window, colors, the app's names and directories),
// Native views (a package's widget, which the registry makes, in the view
// of the host's that holds it), and the core's native_* callbacks: the
// registry's (platform/desktop/src/native.c), but for asuka/host's
// Modules, which are the host's own modules (wh_call_module).
#include "nav.h"

// The registry's half for the hosts, as asuka_desktop.h has it once the
// registry lands (G1); declared here until then (the same declarations).
bool asuka_native_view_measure(void *widget, float width, float height, float *out_width, float *out_height);
void asuka_native_view_prop(void *widget, const uint8_t *op, size_t len);

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
// its events come back through asuka_native_emit; its package measures
// it, or it measures nothing (a child window has no size of its own).
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
	if (!asuka_native_view_measure(n->native->widget, unbounded(w), unbounded(h), ow, oh)) *ow = *oh = 0;
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

// ---- The core's native_* callbacks ----

// The registry's callbacks, which the host's wrap: asuka/host's objects
// are the host's (their handles marked), the rest the registry's.
static asuka_host_callbacks reg;

#define HOST_HANDLE ((uint64_t)1 << 62)
#define HOST_CLASS "asuka/host"

static uint64_t next_host;

// A call into asuka/host's Modules under way: answered once, by the
// module or by its cancellation (what the module says after is dropped).
typedef struct {
	uint64_t call;
	bool done;
} host_call;

static wh_map host_calls;  // by call: host_call

// answer copies b (an answer: a status byte, then the value) to the
// core's buffer, or to one of its own the core frees.
static size_t answer(const uint8_t *b, size_t len, uint8_t *out, size_t cap, uint8_t **big) {
	if (len <= cap) {
		memcpy(out, b, len);
		return len;
	}
	*big = malloc(len);
	if (!*big) return 0;
	memcpy(*big, b, len);
	return len;
}

// fail_into is an error's answer: Asuka's unsupported for a message that
// says it ("unsupported: ..."), else the platform's error, its message.
static void fail_into(ak_buf *b, const char *err) {
	static const char un[] = "unsupported: ";
	if (!strncmp(err, un, sizeof un - 1)) {
		ak_buf_u8(b, 3);
		ak_buf_str(b, err + sizeof un - 1, strlen(err + sizeof un - 1));
		return;
	}
	ak_buf_u8(b, 2);
	ak_buf_str(b, "", 0);
	ak_buf_u64(b, 0);
	ak_buf_str(b, err, strlen(err));
}

static size_t unsupported(const char *what, uint8_t *out, size_t cap, uint8_t **big) {
	ak_buf b = {0};
	char msg[200];
	snprintf(msg, sizeof msg, "unsupported: %s", what);
	fail_into(&b, msg);
	size_t n = answer(b.data, b.len, out, cap, big);
	ak_buf_free(&b);
	return n;
}

// read_str reads a u32-length string at *at.
static bool read_str(const uint8_t *in, size_t len, size_t *at, const char **s, size_t *n) {
	if (*at + 4 > len) return false;
	uint32_t l = wh_u32(in + *at);
	if (l > len - *at - 4) return false;
	*s = (const char *)in + *at + 4;
	*n = l;
	*at += 4 + (size_t)l;
	return true;
}

// Modules' methods' arguments: module, method, args (bytes).
typedef struct {
	const char *module, *method;
	size_t module_len, method_len;
	const uint8_t *args;
	size_t args_len;
} module_args;

static bool read_module_args(const uint8_t *in, size_t len, module_args *a) {
	size_t at = 0;
	const char *args;
	return read_str(in, len, &at, &a->module, &a->module_len) && read_str(in, len, &at, &a->method, &a->method_len) &&
		read_str(in, len, &at, &args, &a->args_len) && (a->args = (const uint8_t *)args, true);
}

static void call_answered(void *ctx, const uint8_t *b, size_t n, const char *err) {
	host_call *c = ctx;
	if (!c->done) {
		ak_buf out = {0};
		if (err) {
			fail_into(&out, err);
		} else {
			ak_buf_u8(&out, 0);
			ak_buf_str(&out, (const char *)b, n);
		}
		wh_map_del(&host_calls, c->call);
		asuka_native_done(c->call, out.data, out.len);
		ak_buf_free(&out);
	}
	free(c);
}

static void sent(void *ctx, const uint8_t *b, size_t n, const char *err) {
	(void)ctx, (void)b, (void)n;
	if (err) wh_log("%s", err);
}

static size_t native_new(const uint8_t *cls, size_t len, uint32_t iface, uint8_t *out, size_t cap, uint8_t **big) {
	if (len == strlen(HOST_CLASS) && !memcmp(cls, HOST_CLASS, len)) {
		if (iface != 0) return unsupported("asuka/host has no such interface", out, cap, big);
		uint8_t a[9] = {0};
		uint64_t h = HOST_HANDLE | ++next_host;
		for (int i = 0; i < 8; i++) a[1 + i] = (uint8_t)(h >> (8 * i));
		return answer(a, sizeof a, out, cap, big);
	}
	if (reg.native_new) return reg.native_new(cls, len, iface, out, cap, big);
	char what[160];
	snprintf(what, sizeof what, "no native code for %.*s on Windows", (int)wh_mini((int)len, 100), (const char *)cls);
	return unsupported(what, out, cap, big);
}

static size_t native_call(uint64_t h, uint32_t method, const uint8_t *in, size_t len, uint8_t *out, size_t cap, uint8_t **big) {
	if (h & HOST_HANDLE) {
		module_args a;
		if (method != 2 || !read_module_args(in, len, &a)) return unsupported("asuka/host: a bad call", out, cap, big);
		wh_call_module(a.module, a.module_len, a.method, a.method_len, a.args, a.args_len, sent, NULL);  // Send
		uint8_t ok = 0;
		return answer(&ok, 1, out, cap, big);
	}
	if (reg.native_call) return reg.native_call(h, method, in, len, out, cap, big);
	return unsupported("no native code on Windows", out, cap, big);
}

static void native_start(uint64_t h, uint32_t method, const uint8_t *in, size_t len, uint64_t call) {
	if (h & HOST_HANDLE) {
		module_args a;
		host_call *c = wh_alloc(sizeof *c);
		c->call = call;
		if (method != 1 || !read_module_args(in, len, &a)) {
			call_answered(c, NULL, 0, "asuka/host: a bad call");
			return;
		}
		wh_map_put(&host_calls, call, c);
		wh_call_module(a.module, a.module_len, a.method, a.method_len, a.args, a.args_len, call_answered, c);  // Call
		return;
	}
	if (reg.native_start) {
		reg.native_start(h, method, in, len, call);
		return;
	}
	ak_buf b = {0};
	fail_into(&b, "unsupported: no native code on Windows");
	asuka_native_done(call, b.data, b.len);
	ak_buf_free(&b);
}

static void native_cancel(uint64_t call) {
	host_call *c = wh_map_get(&host_calls, call);
	if (!c) {
		if (reg.native_cancel) reg.native_cancel(call);
		return;
	}
	// The host's modules don't stop: the call answers cancelled now, and
	// what the module answers later goes nowhere.
	wh_map_del(&host_calls, call);
	c->done = true;
	wh_module_call_gone(c);
	uint8_t cancelled = 4;
	asuka_native_done(call, &cancelled, 1);
}

static void native_release(uint64_t h) {
	if (h & HOST_HANDLE) return;  // Modules hold nothing
	if (reg.native_release) reg.native_release(h);
}

static void native_free(void *p) {
	// The host's own answers are malloc's, as the registry's are.
	if (reg.native_free) reg.native_free(p);
	else free(p);
}

void wh_native_host_callbacks(asuka_host_callbacks *cb) {
	main_thread = GetCurrentThreadId();
	reg = *cb;
	asuka_native_callbacks(&reg);
	cb->native_new = native_new;
	cb->native_call = native_call;
	cb->native_start = native_start;
	cb->native_cancel = native_cancel;
	cb->native_release = native_release;
	cb->native_free = native_free;
}
