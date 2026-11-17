// tray.c: platform/tray on Windows: icons in the notification area
// (Shell_NotifyIcon, NOTIFYICON_VERSION_4), owned by a hidden top-level
// window of the modules' (a message-only window doesn't get TaskbarCreated,
// which says Explorer started again and the icons are to be added again). A
// click (NIN_SELECT, or Enter or Space on the focused icon) is the icon's
// OnClick, or opens its menu if it has none; a right click (WM_CONTEXTMENU)
// opens the menu, a popup menu built as the menu bar's (submenus, checks,
// destructive items drawn red). The icon is the image, fitted to a small
// icon's size at the system's DPI, or the symbol's Segoe Fluent Icons
// glyph; a symbol, and a template image, are drawn white on a dark taskbar
// and black on a light one, and drawn again when it turns. Without either,
// the app's icon. Windows shows no text beside an icon (Title).
//
// The Dock menu is the Tasks of the app's jump list
// (ICustomDestinationList): a link per item, starting the app with
// --asuka-dock-item=<number>. A running app that runs once is handed the
// number by the launch (instance.c); one that isn't running gets it as it
// starts. The jump list has separators, but no submenus, checks or
// disabled items: those items are left out, as is a quit item.
#include "modules.h"

#include <propvarutil.h>
#include <shobjidl.h>

#define TRAY_MESSAGE (WM_APP + 0x54)  // an icon's events
#define TRAY_QUEUE 64                 // what happens, kept while nothing asks
#define DOCK_ITEM_ARG L"--asuka-dock-item="

typedef struct tray_icon {
	uint32_t id;
	uint8_t flags;  // 1 template, 2 clickable
	char *image, *symbol;
	wchar_t tip[128];
	uint8_t *menu;  // ops/menu.go's
	size_t menu_len;
	HICON icon;
	bool own_icon;  // icon is the host's, to destroy
	bool added, ready;
	uint32_t gen;  // its image's loads: an older one's answer is dropped
} tray_icon;

static struct {
	HWND hwnd;
	UINT created;  // TaskbarCreated
	tray_icon **icons;  // in the order they came
	uint32_t n;
	wh_queue events;
	bool light;  // the taskbar is light
} tray = {.events = {.max = TRAY_QUEUE}};

static tray_icon *icon_of(uint32_t id) {
	for (uint32_t i = 0; i < tray.n; i++)
		if (tray.icons[i]->id == id) return tray.icons[i];
	return NULL;
}

static bool taskbar_light(void) {
	DWORD v = 0;
	return wh_reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", &v) && v;
}

static LRESULT tray_message(HWND hwnd, UINT m, WPARAM wp, LPARAM lp, bool *handled);

static void trays(void) {
	if (tray.hwnd) return;
	tray.created = RegisterWindowMessageW(L"TaskbarCreated");
	tray.light = taskbar_light();
	tray.hwnd = wh_module_window(false, tray_message);
}

void wh_tray_event(uint32_t id, uint8_t kind, uint32_t index) {
	uint8_t b[9];
	memcpy(b, &id, 4);
	b[4] = kind;
	memcpy(b + 5, &index, 4);
	wh_queue_deliver(&tray.events, b, sizeof b);
}

// ---- Showing an icon ----

static void notify_data(const tray_icon *t, NOTIFYICONDATAW *d) {
	memset(d, 0, sizeof *d);
	d->cbSize = sizeof *d;
	d->hWnd = tray.hwnd;
	d->uID = t->id;
	d->uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
	d->uCallbackMessage = TRAY_MESSAGE;
	d->hIcon = t->icon;
	wcsncpy(d->szTip, t->tip, WH_LEN(d->szTip) - 1);
}

// show adds the icon to the notification area, or sets it there.
static void show(tray_icon *t) {
	if (!t->ready) return;
	NOTIFYICONDATAW d;
	notify_data(t, &d);
	if (t->added) {
		Shell_NotifyIconW(NIM_MODIFY, &d);
		return;
	}
	if (!Shell_NotifyIconW(NIM_ADD, &d)) return;
	d.uVersion = NOTIFYICON_VERSION_4;
	Shell_NotifyIconW(NIM_SETVERSION, &d);
	t->added = true;
}

// fit_icon fits w×h premultiplied BGRA pixels into a square icon of size
// pixels, its aspect kept and centered (each icon pixel the average of
// the image's it covers): BGRA not premultiplied, as icons take it.
static uint8_t *fit_icon(const uint8_t *px, int w, int h, int size) {
	uint8_t *out = wh_alloc((size_t)size * size * 4);
	if (w <= 0 || h <= 0 || size <= 0) return out;
	double s = fmin((double)size / w, (double)size / h);
	double dw = w * s, dh = h * s, ox = (size - dw) / 2, oy = (size - dh) / 2;
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++) {
			double x0 = (x - ox) / s, x1 = (x + 1 - ox) / s, y0 = (y - oy) / s, y1 = (y + 1 - oy) / s;
			if (x1 <= 0 || y1 <= 0 || x0 >= w || y0 >= h) continue;
			int ix0 = (int)fmax(x0, 0), ix1 = wh_mini((int)(x1 + 0.999), w);
			int iy0 = (int)fmax(y0, 0), iy1 = wh_mini((int)(y1 + 0.999), h);
			if (ix1 <= ix0) ix1 = ix0 + 1;
			if (iy1 <= iy0) iy1 = iy0 + 1;
			uint32_t sum[4] = {0}, n = 0;
			for (int iy = iy0; iy < iy1; iy++)
				for (int ix = ix0; ix < ix1; ix++) {
					const uint8_t *p = px + ((size_t)iy * w + ix) * 4;
					for (int c = 0; c < 4; c++) sum[c] += p[c];
					n++;
				}
			uint32_t a = sum[3] / n;
			uint8_t *o = out + ((size_t)y * size + x) * 4;
			o[3] = (uint8_t)a;
			if (!a) continue;
			for (int c = 0; c < 3; c++) {
				uint32_t v = sum[c] / n * 255 / a;
				o[c] = (uint8_t)(v > 255 ? 255 : v);
			}
		}
	return out;
}

// picture sets the icon's picture from its image's pixels (premultiplied
// BGRA, w×h; NULL: none), else its symbol, else the app's icon, and
// shows it.
static void picture(tray_icon *t, const uint8_t *px, int w, int h) {
	int size = wh_system_metric(SM_CXSMICON, wh_dpi(tray.hwnd));
	if (size <= 0) size = 16;
	uint8_t ink[3] = {0, 0, 0};  // BGR: black on a light taskbar
	if (!tray.light) ink[0] = ink[1] = ink[2] = 255;
	uint8_t *out = NULL;
	if (px && w > 0 && h > 0) {
		out = fit_icon(px, w, h, size);
		if (t->flags & 1)
			for (int i = 0; i < size * size; i++) memcpy(out + i * 4, ink, 3);
	} else if (t->symbol && *t->symbol) {
		uint16_t g = wh_symbol_glyph(t->symbol);
		if (g) {
			uint8_t *cov = wh_glyph_coverage(g, wh_icon_face(), size);
			out = wh_alloc((size_t)size * size * 4);
			for (int i = 0; i < size * size; i++) {
				memcpy(out + i * 4, ink, 3);
				out[i * 4 + 3] = cov[i];
			}
			free(cov);
		} else wh_warn_symbol(t->symbol);
	}
	HICON old = t->icon;
	bool own = t->own_icon;
	t->icon = wh_app_icon(), t->own_icon = false;
	if (out) {
		HICON icon = wh_icon_from_pixels(out, size, size);
		if (icon) t->icon = icon, t->own_icon = true;
		free(out);
	}
	t->ready = true;
	show(t);
	if (own && old) DestroyIcon(old);
}

typedef struct {
	uint32_t id, gen;
	char *source;
	uint8_t *px;
	int w, h;
	char *err;
} image_job;

static void image_work(void *ctx) {
	image_job *j = ctx;
	char *path = wh_local_file(j->source, &j->err);
	if (!path) return;
	wchar_t *wp = wh_wide(path, -1);
	if (!wh_decode_image_file(wp, 256, &j->px, &j->w, &j->h)) {
		free(j->err);
		j->err = wh_strdup("not an image");
	}
	free(wp);
	free(path);
}

static void image_done(void *ctx) {
	image_job *j = ctx;
	tray_icon *t = icon_of(j->id);
	if (t && t->gen == j->gen) {
		if (j->err) wh_log("tray icon %s: %s", j->source, j->err);
		picture(t, j->err ? NULL : j->px, j->w, j->h);
	}
	free(j->source);
	free(j->px);
	free(j->err);
	free(j);
}

// draw makes the icon's picture again: its image once it's loaded, or
// its symbol.
static void draw(tray_icon *t) {
	t->gen++;
	if (!t->image || !*t->image) {
		picture(t, NULL, 0, 0);
		return;
	}
	image_job *j = wh_alloc(sizeof *j);
	j->id = t->id, j->gen = t->gen, j->source = wh_strdup(t->image);
	wh_off_main(image_work, image_done, j);
}

static void remove_icon(tray_icon *t) {
	if (!t) return;
	if (t->added) {
		NOTIFYICONDATAW d = {sizeof d};
		d.hWnd = tray.hwnd, d.uID = t->id;
		Shell_NotifyIconW(NIM_DELETE, &d);
	}
	if (t->own_icon && t->icon) DestroyIcon(t->icon);
	for (uint32_t i = 0; i < tray.n; i++)
		if (tray.icons[i] == t) {
			memmove(tray.icons + i, tray.icons + i + 1, (tray.n - i - 1) * sizeof *tray.icons);
			tray.n--;
			break;
		}
	free(t->image);
	free(t->symbol);
	free(t->menu);
	free(t);
}

void wh_remove_tray_icons(void) {
	while (tray.n) remove_icon(tray.icons[tray.n - 1]);
}

bool wh_tray_icon_rect(uint32_t id, RECT *r) {
	if (!tray.hwnd || !icon_of(id)) return false;
	NOTIFYICONIDENTIFIER ni = {sizeof ni};
	ni.hWnd = tray.hwnd, ni.uID = id;
	return SUCCEEDED(Shell_NotifyIconGetRect(&ni, r));
}

// ---- Its menu ----

typedef struct {
	uint32_t id;
	POINT p;
} menu_job;

// chose is an icon's menu item chosen (its number): reported, or a quit
// item quits.
static void chose(tray_icon *t, int64_t index) {
	ak_menu_entry_vec e = {0};
	if (ak_decode_menu(t->menu, t->menu_len, &e)) {
		for (uint32_t i = 0; i < e.len; i++) {
			const ak_menu_entry *it = &e.data[i];
			if ((int64_t)it->index != index) continue;
			if (it->flags & (AK_MENU_SEPARATOR | AK_MENU_DISABLED | AK_MENU_SUBMENU)) break;
			if (it->role == AK_MENU_ROLE_QUIT) wh_quit(false);
			else wh_tray_event(t->id, 2, it->index);
			break;
		}
	}
	ak_vec_free(&e);
}

// menu_now opens an icon's menu at the point its event gave (on the
// taskbar). The menu runs a modal loop, so it shows once the core has
// returned.
static void menu_now(void *ctx) {
	menu_job *j = ctx;
	tray_icon *t = icon_of(j->id);
	if (t && t->menu_len) {
		// A copy: the menu may change while it's open.
		size_t n = t->menu_len;
		uint8_t *m = wh_alloc(n);
		memcpy(m, t->menu, n);
		int64_t got = wh_track_menu(m, n, j->p, tray.hwnd);
		free(m);
		if (got >= 0 && (t = icon_of(j->id))) chose(t, got);
	}
	free(j);
}

static void open_menu(tray_icon *t, int x, int y) {
	if (!t->menu_len) return;
	menu_job *j = wh_alloc(sizeof *j);
	j->id = t->id, j->p = (POINT){x, y};
	wh_after_core(menu_now, j);
}

static LRESULT tray_message(HWND hwnd, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	*handled = true;
	if (m == TRAY_MESSAGE) {
		tray_icon *t = icon_of(HIWORD(lp));
		if (!t) return 0;
		int x = (short)LOWORD(wp), y = (short)HIWORD(wp);
		switch (LOWORD(lp)) {
		case NIN_SELECT:
		case NIN_KEYSELECT:
			if (t->flags & 2) {
				wh_tray_event(t->id, 1, 0);
				wh_tray_clicked(t->id);  // a flyout hanging from it (flyout.c)
			} else open_menu(t, x, y);
			break;
		case WM_CONTEXTMENU:
			open_menu(t, x, y);
			break;
		}
		return 0;
	}
	if (tray.created && m == tray.created) {
		// Explorer started again: the icons are gone from it.
		for (uint32_t i = 0; i < tray.n; i++) {
			tray.icons[i]->added = false;
			show(tray.icons[i]);
		}
		return 0;
	}
	if (m == WM_SETTINGCHANGE) {
		if (lp && !wcscmp((const wchar_t *)lp, L"ImmersiveColorSet")) {
			bool light = taskbar_light();
			if (light != tray.light) {
				tray.light = light;
				for (uint32_t i = 0; i < tray.n; i++) {
					tray_icon *t = tray.icons[i];
					if ((t->symbol && *t->symbol) || (t->flags & 1)) draw(t);
				}
			}
		}
		*handled = false;
		return 0;
	}
	if (m == WM_MEASUREITEM || m == WM_DRAWITEM || m == WM_MENUCHAR || m == WM_INITMENUPOPUP || m == WM_INITMENU ||
		m == WM_ENTERMENULOOP || m == WM_EXITMENULOOP)
		return wh_menu_item_message(hwnd, m, wp, lp, handled);
	*handled = false;
	return 0;
}

// ---- The jump list's tasks: the Dock menu ----

static bool automated(void) {
	const char *keys[] = {"ASUKA_SNAPSHOT", "ASUKA_SCRIPT", "ASUKA_COMMANDS", "ASUKA_STATE", "ASUKA_DEV"};
	for (size_t i = 0; i < WH_LEN(keys); i++)
		if (getenv(keys[i])) return true;
	return false;
}

static const PROPERTYKEY PKEY_Title_ = {{0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}}, 2};
static const PROPERTYKEY PKEY_DestListSeparator_ = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 6};

// task_link is a jump list task's shell link: a separator, or a link
// titled to the app with its arguments and its icon.
static IShellLinkW *task_link(const wchar_t *exe, const wchar_t *title, const wchar_t *args) {
	IShellLinkW *link = NULL;
	if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link))) return NULL;
	IPropertyStore *ps = NULL;
	if (FAILED(IShellLinkW_QueryInterface(link, &IID_IPropertyStore, (void **)&ps))) {
		IShellLinkW_Release(link);
		return NULL;
	}
	PROPVARIANT v;
	PropVariantInit(&v);
	if (!title) {
		v.vt = VT_BOOL, v.boolVal = VARIANT_TRUE;
		IPropertyStore_SetValue(ps, &PKEY_DestListSeparator_, &v);
	} else {
		IShellLinkW_SetPath(link, exe);
		IShellLinkW_SetArguments(link, args);  // the jump list shows only links with some
		IShellLinkW_SetIconLocation(link, exe, 0);
		IShellLinkW_SetDescription(link, title);  // its tooltip
		v.vt = VT_LPWSTR, v.pwszVal = (LPWSTR)title;
		IPropertyStore_SetValue(ps, &PKEY_Title_, &v);
	}
	IPropertyStore_Commit(ps);
	IPropertyStore_Release(ps);
	return link;
}

// set_jump_list makes the Dock menu's items the Tasks of the app's jump
// list: each starts the app for its item; separators between them, never
// first, last or two together; what the jump list can't show is left
// out. Not for automation's runs, which would change the user's.
static bool set_jump_list(const uint8_t *menu, size_t len, char **err) {
	if (automated()) return true;
	ak_menu_entry_vec e = {0};
	ak_decode_menu(menu, len, &e);
	wchar_t exe[MAX_PATH * 4];
	GetModuleFileNameW(NULL, exe, WH_LEN(exe));
	ICustomDestinationList *dl = NULL;
	bool ok = false;
	if (FAILED(CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER, &IID_ICustomDestinationList, (void **)&dl))) {
		*err = wh_strdup("jump list: no ICustomDestinationList");
		goto out;
	}
	uint32_t tasks = 0;
	for (uint32_t i = 0; i < e.len; i++) {
		const ak_menu_entry *it = &e.data[i];
		if (it->depth == 0 && !(it->flags & (AK_MENU_SEPARATOR | AK_MENU_DISABLED | AK_MENU_SUBMENU)) && it->role == AK_MENU_ROLE_NONE && it->title.len) tasks++;
	}
	if (!tasks) {
		ICustomDestinationList_DeleteList(dl, NULL);  // the app's own
		ok = true;
		goto out;
	}
	UINT slots = 0;
	IObjectArray *removed = NULL;
	HRESULT hr = ICustomDestinationList_BeginList(dl, &slots, &IID_IObjectArray, (void **)&removed);
	if (FAILED(hr)) {
		*err = wh_alloc(64);
		sprintf(*err, "jump list: BeginList %#lx", (unsigned long)hr);
		goto out;
	}
	if (removed) IObjectArray_Release(removed);  // what the user removed: tasks can't be
	IObjectCollection *oc = NULL;
	if (FAILED(CoCreateInstance(&CLSID_EnumerableObjectCollection, NULL, CLSCTX_INPROC_SERVER, &IID_IObjectCollection, (void **)&oc))) {
		ICustomDestinationList_AbortList(dl);
		*err = wh_strdup("jump list: no object collection");
		goto out;
	}
	bool sep = false, any = false;
	for (uint32_t i = 0; i < e.len; i++) {
		const ak_menu_entry *it = &e.data[i];
		if (it->depth != 0) continue;
		if (it->flags & AK_MENU_SEPARATOR) {
			sep = any;
			continue;
		}
		if ((it->flags & (AK_MENU_DISABLED | AK_MENU_SUBMENU)) || it->role != AK_MENU_ROLE_NONE || !it->title.len) continue;
		if (sep) {
			IShellLinkW *s = task_link(exe, NULL, NULL);
			if (s) {
				IObjectCollection_AddObject(oc, (IUnknown *)s);
				IShellLinkW_Release(s);
			}
			sep = false;
		}
		wchar_t *title = wh_wspan(it->title);
		wchar_t args[64];
		swprintf(args, WH_LEN(args), DOCK_ITEM_ARG L"%u", it->index);
		IShellLinkW *l = task_link(exe, title, args);
		free(title);
		if (l) {
			IObjectCollection_AddObject(oc, (IUnknown *)l);  // the collection holds its own
			IShellLinkW_Release(l);
			any = true;
		}
	}
	IObjectArray *arr = NULL;
	IObjectCollection_QueryInterface(oc, &IID_IObjectArray, (void **)&arr);
	hr = arr ? ICustomDestinationList_AddUserTasks(dl, arr) : E_FAIL;
	if (arr) IObjectArray_Release(arr);
	IObjectCollection_Release(oc);
	if (SUCCEEDED(hr)) hr = ICustomDestinationList_CommitList(dl);
	if (FAILED(hr)) {
		ICustomDestinationList_AbortList(dl);
		*err = wh_alloc(64);
		sprintf(*err, "jump list: %#lx", (unsigned long)hr);
		goto out;
	}
	ok = true;
out:
	if (dl) ICustomDestinationList_Release(dl);
	ak_vec_free(&e);
	return ok;
}

// wh_dock_item_launch reports whether a launch is a jump list task's (a
// Dock menu item chosen): the item is reported, and the launch opens
// nothing else.
bool wh_dock_item_launch(int argc, wchar_t **argv) {
	size_t al = wcslen(DOCK_ITEM_ARG);
	for (int i = 1; i < argc; i++) {
		if (wcsncmp(argv[i], DOCK_ITEM_ARG, al)) continue;
		wchar_t *end;
		long n = wcstol(argv[i] + al, &end, 10);
		if (end == argv[i] + al || *end || n < 0) continue;
		wh_tray_event(0, 2, (uint32_t)n);
		return true;
	}
	return false;
}

// ---- The module ----

// reads a u32-length string at *b (none past the end: "").
static char *take_str(const uint8_t **b, size_t *n) {
	if (*n < 4 || wh_u32(*b) > *n - 4) {
		*n = 0;
		return wh_strdup("");
	}
	uint32_t l = wh_u32(*b);
	char *s = wh_strndup((const char *)*b + 4, l);
	*b += 4 + l, *n -= 4 + l;
	return s;
}

static void m_supported(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	wh_reply_u8(r, 1);
}

// set: u32 id, u8 flags, image, symbol, title, tooltip and a fifth
// string, then the menu.
static void m_set(wh_reply *r, const uint8_t *a, size_t n) {
	if (n < 5) {
		wh_reply_fail(r, "tray.set: bad arguments");
		return;
	}
	trays();
	uint32_t id = wh_u32(a);
	uint8_t flags = a[4];
	const uint8_t *b = a + 5;
	size_t left = n - 5;
	char *image = take_str(&b, &left), *symbol = take_str(&b, &left), *title = take_str(&b, &left);
	char *tip = take_str(&b, &left), *extra = take_str(&b, &left);
	free(title);  // no title on Windows; the ID is its place
	free(extra);
	tray_icon *t = icon_of(id);
	if (!t) {
		t = wh_alloc(sizeof *t);
		t->id = id;
		tray.icons = wh_realloc(tray.icons, (tray.n + 1) * sizeof *tray.icons);
		tray.icons[tray.n++] = t;
	}
	bool redraw = !t->image || strcmp(t->image, image) || !t->symbol || strcmp(t->symbol, symbol) || (t->flags & 1) != (flags & 1) || !t->ready;
	free(t->image);
	free(t->symbol);
	t->image = image, t->symbol = symbol, t->flags = flags;
	wchar_t *wt = wh_wide(tip, -1);
	wcsncpy(t->tip, wt, WH_LEN(t->tip) - 1);
	t->tip[WH_LEN(t->tip) - 1] = 0;
	free(wt);
	free(tip);
	free(t->menu);
	t->menu_len = left;
	t->menu = wh_alloc(left ? left : 1);
	if (left) memcpy(t->menu, b, left);
	if (redraw) draw(t);
	else show(t);
	wh_reply_nil(r);
}

static void m_remove(wh_reply *r, const uint8_t *a, size_t n) {
	if (n >= 4) remove_icon(icon_of(wh_u32(a)));
	wh_reply_nil(r);
}

static void m_dock(wh_reply *r, const uint8_t *a, size_t n) {
	trays();
	char *err = NULL;
	if (set_jump_list(a, n, &err)) wh_reply_nil(r);
	else wh_reply_fail(r, "%s", err ? err : "jump list");
	free(err);
}

static void m_next(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	trays();
	// One poll at a time: a newer one waits instead.
	wh_queue_stop(&tray.events);
	wh_queue_next(&tray.events, r);
}

static void m_stop(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	wh_queue_stop(&tray.events);
	wh_reply_nil(r);
}

static const wh_method_def tray_methods[] = {
	{"supported", m_supported}, {"set", m_set}, {"remove", m_remove}, {"dock", m_dock}, {"next", m_next}, {"stop", m_stop}, {NULL, NULL},
};

const wh_module_def wh_tray_module = {"tray", tray_methods};
