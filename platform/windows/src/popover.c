// popover.c: a Popover (host/windows' popover.go) is a popup window over
// its anchor, as WinUI's Flyout: below the anchor and centered on it
// (above it where the monitor has no room), with Windows 11's rounded
// corners and border, light dismissed: clicking elsewhere (it loses
// activation) or Escape reports Dismissed, or Back while ui.OnBack in it
// intercepts.
#include "nav.h"
#include "win.h"

static void place_popover(node *n);

void wh_set_popover_prop(node *n, uint16_t p, const ak_value *v) {
	switch (p) {
	case AK_PROP_POPOVER_ANCHOR: n->nav->anchor = (uint32_t)v->i; break;
	case AK_PROP_POPOVER_WIDTH: n->nav->pop_w = (float)v->f; break;
	case AK_PROP_POPOVER_HEIGHT: n->nav->pop_h = (float)v->f; break;
	default: return;
	}
	if (n->nav->window) place_popover(n);
}

void wh_open_popover(node *n) {
	node *a = wh_node(n->nav->anchor);
	if (n->nav->window || !a || !a->hwnd || !n->hwnd) return;
	HWND owner = GetAncestor(a->hwnd, GA_ROOT);
	HWND win = CreateWindowExW(WS_EX_TOOLWINDOW | wh_top_level_style(), WH_CLASS_WINDOW, L"", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, NULL,
		GetModuleHandleW(NULL), NULL);
	DWORD pref = 2;  // DWMWCP_ROUND
	DwmSetWindowAttribute(win, 33, &pref, sizeof pref);  // DWMWA_WINDOW_CORNER_PREFERENCE
	node *pw = wh_node_new(0, KIND_POPOVER_WINDOW);
	pw->hwnd = win;
	pw->parent = n;
	pw->k = (float)wh_dpi(win) / 96;
	wh_bind(pw, win);
	n->nav->window = win, n->nav->owner = owner;
	n->presented = true;
	SetParent(n->hwnd, win);
	wh_rescale(n->hwnd, pw->k);
	place_popover(n);
	ShowWindow(win, wh_snapshotting() ? SW_SHOWNOACTIVATE : SW_SHOW);
}

// place_popover sizes the popover to its content and puts it by its
// anchor.
static void place_popover(node *n) {
	HWND win = n->nav->window;
	node *a = wh_node(n->nav->anchor);
	if (!win || !a || !a->hwnd) return;
	float k = (float)wh_dpi(win) / 96;
	int w = (int)(n->nav->pop_w * k + 0.5f), ht = (int)(n->nav->pop_h * k + 0.5f);
	RECT ar;
	GetWindowRect(a->hwnd, &ar);
	int gap = (int)(4 * k);
	int x = ar.left + ((ar.right - ar.left) - w) / 2;
	int y = ar.bottom + gap;
	RECT wa = wh_monitor_work_area(ar);
	if (y + ht > wa.bottom && ar.top - gap - ht >= wa.top) y = ar.top - gap - ht;
	x = wh_mini(wh_maxi(x, wa.left), wh_maxi(wa.right - w, wa.left));
	SetWindowPos(win, NULL, x, y, w, ht, SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(n->hwnd, NULL, 0, 0, w, ht, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	wh_report_screen(n, n->nav->pop_w, n->nav->pop_h);
}

static void fill(HDC dc, HWND w, rgba c) {
	RECT r;
	GetClientRect(w, &r);
	FillRect(dc, &r, wh_brush(c));
}

LRESULT wh_popover_message(node *pw, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	(void)lp;
	node *n = pw->parent;
	*handled = true;
	switch (m) {
	case WM_ERASEBKGND:
		fill((HDC)wp, pw->hwnd, H.theme.bg);
		return 1;
	case WM_PRINTCLIENT:
		fill((HDC)wp, pw->hwnd, H.theme.bg);
		return 0;
	case WM_ACTIVATE:
		if (LOWORD(wp) == WA_INACTIVE && n && !n->dying) wh_cancel_popover(pw);  // a click elsewhere
		*handled = false;
		return 0;
	case WM_CLOSE:
		wh_cancel_popover(pw);
		return 0;
	}
	*handled = false;
	return 0;
}

// wh_cancel_popover is the user dismissing a popover.
void wh_cancel_popover(node *pw) {
	node *n = pw ? pw->parent : NULL;
	if (!n || n->dying) return;
	if (n->intercept_back) wh_emit_kind(n, AK_EVENT_BACK);
	else wh_emit_kind(n, AK_EVENT_DISMISSED);
}

void wh_close_popover(node *n) {
	HWND win = n->nav->window;
	if (!win) return;
	n->nav->window = NULL;
	if (n->hwnd) SetParent(n->hwnd, H.park);
	node *pw = wh_node_of(win);
	wh_forget_tree(win);
	DestroyWindow(win);
	if (pw && pw->kind == KIND_POPOVER_WINDOW) {
		pw->parent = NULL;
		wh_free_node(pw);
	}
}
