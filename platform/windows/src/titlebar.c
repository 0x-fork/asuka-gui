// titlebar.c: a window with a hidden title bar gives its client area, and
// so its content, the whole window: WM_NCCALCSIZE keeps the left, right
// and bottom borders (invisible, and still resizing it) and gives the top
// to the client area, but a pixel on Windows 11, without which it opens
// no snap layouts. As MyGo's windows do, two child windows of the host's
// sit above the content: the caption buttons, drawn as Windows 11 draws
// its own in a layered window, answering the buttons' hit-test codes (so
// snap layouts open over maximize), and an invisible top edge answering
// HTTOP. Below them, the 32-point strip answers HTCAPTION where the views
// let the mouse through.
#include "win.h"

#define BUTTON_WIDTH 46
#define GLYPH_SIZE 10
#define RESIZE_CORNER 16

enum { BUTTON_MINIMIZE, BUTTON_MAXIMIZE, BUTTON_CLOSE };

int wh_hidden_top(void) { return wh_windows11() ? 1 : 0; }

static wchar_t glyph(int b, bool maximized) {
	switch (b) {
	case BUTTON_MINIMIZE: return 0xE921;                      // ChromeMinimize
	case BUTTON_MAXIMIZE: return maximized ? 0xE923 : 0xE922;  // ChromeRestore, ChromeMaximize
	}
	return 0xE8BB;  // ChromeClose
}

static LRESULT hit_test(int b) { return b == BUTTON_MINIMIZE ? HTMINBUTTON : b == BUTTON_MAXIMIZE ? HTMAXBUTTON : HTCLOSE; }

void wh_hide_title_bar(wh_wctl *c) {
	c->hidden = true;
	if (wh_windows11()) {
		COLORREF none = 0xFFFFFFFE;  // DWMWA_COLOR_NONE: no accent on what's left of the caption
		DwmSetWindowAttribute(c->hwnd, 35, &none, sizeof none);  // DWMWA_CAPTION_COLOR
	}
	HINSTANCE inst = GetModuleHandleW(NULL);
	HWND b = CreateWindowExW(WS_EX_LAYERED, WH_CLASS_VIEW, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, c->hwnd, NULL, inst, NULL);
	if (b) {
		c->caption = wh_alloc(sizeof *c->caption);
		c->caption->hwnd = b;
		c->caption->hot = c->caption->pressed = -1;
		c->caption->active = true;
		wh_map_put(&H.chrome, (uint64_t)(uintptr_t)b, c);
	}
	HWND e = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP, WH_CLASS_VIEW, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, c->hwnd, NULL, inst, NULL);
	if (e) {
		// A layered window shows, and takes the mouse, once it has
		// attributes; this one has no pixels to show.
		SetLayeredWindowAttributes(e, 0, 255, LWA_ALPHA);
		c->top_edge = e;
		wh_map_put(&H.chrome, (uint64_t)(uintptr_t)e, c);
	}
	if (c->main && GetMenu(H.window)) wh_build_menu_bar();  // without its bar now
}

void wh_forget_chrome(wh_wctl *c) {
	if (c->caption) wh_map_del(&H.chrome, (uint64_t)(uintptr_t)c->caption->hwnd);
	if (c->top_edge) wh_map_del(&H.chrome, (uint64_t)(uintptr_t)c->top_edge);
}

// buttons are the caption buttons a window shows, left to right: close
// alone when it can be neither minimized nor maximized, as Windows does.
static int buttons(wh_wctl *c, int out[3]) {
	LONG_PTR style = GetWindowLongPtrW(c->hwnd, GWL_STYLE);
	if (!(style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX))) {
		out[0] = BUTTON_CLOSE;
		return 1;
	}
	out[0] = BUTTON_MINIMIZE, out[1] = BUTTON_MAXIMIZE, out[2] = BUTTON_CLOSE;
	return 3;
}

static bool enabled(wh_wctl *c, int b) {
	LONG_PTR style = GetWindowLongPtrW(c->hwnd, GWL_STYLE);
	if (b == BUTTON_MINIMIZE) return (style & WS_MINIMIZEBOX) != 0;
	if (b == BUTTON_MAXIMIZE) return (style & WS_MAXIMIZEBOX) != 0;
	return !wh_ctl_has(c, AK_WINDOW_NO_CLOSE);
}

static bool mirrored(wh_wctl *c) { return (GetWindowLongPtrW(c->hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0; }

static int roundi(float v) { return (int)lroundf(v); }

static void paint(wh_wctl *c);

// resize_border is the height of the band along the top edge that resizes
// a window: the thickness of its sizing frame, as above a caption.
static int resize_border(int dpi) {
	int b = wh_system_metric(SM_CYSIZEFRAME, dpi) + wh_system_metric(SM_CXPADDEDBORDER, dpi);
	return b > 0 ? b : 8 * dpi / 96;
}

// wh_layout_chrome puts a hidden title bar's buttons at the top right,
// above the content, and the top edge along the rest of the top, and
// paints the buttons; or hides them where they have no place.
void wh_layout_chrome(wh_wctl *c) {
	if (!c || !c->hidden || IsIconic(c->hwnd)) return;
	float k = wh_ctl_k(c);
	RECT r;
	GetClientRect(c->hwnd, &r);
	int width = 0;
	wh_caption *b = c->caption;
	if (b) {
		b->nshown = buttons(c, b->shown);
		if (b->hot >= b->nshown || b->pressed >= b->nshown) b->hot = b->pressed = -1;
		width = roundi(BUTTON_WIDTH * k) * b->nshown;
		UINT show = c->full ? SWP_HIDEWINDOW : SWP_SHOWWINDOW;
		SetWindowPos(b->hwnd, HWND_TOP, r.right - width, 0, width, roundi(CAPTION_HEIGHT * k), SWP_NOACTIVATE | show);
		paint(c);
	}
	if (c->top_edge) {
		UINT show = c->full || IsZoomed(c->hwnd) || !(GetWindowLongPtrW(c->hwnd, GWL_STYLE) & WS_THICKFRAME) ? SWP_HIDEWINDOW : SWP_SHOWWINDOW;
		SetWindowPos(c->top_edge, HWND_TOP, 0, 0, wh_maxi(r.right - width, 0), resize_border(wh_dpi(c->hwnd)), SWP_NOACTIVATE | show);
	}
}

void wh_layout_all_chrome(void) {
	uint32_t i = 0;
	void *v;
	while (wh_map_next(&H.ctls, &i, NULL, &v)) wh_layout_chrome(v);
}

LRESULT wh_title_bar_message(wh_wctl *c, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	*handled = false;
	if (!c->hidden) return 0;
	if (m == WM_NCCALCSIZE) {
		if (!wp || c->full) return 0;
		NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
		*handled = true;
		if (IsZoomed(c->hwnd)) {
			// A maximized window would overflow its monitor by its borders:
			// the proposed rectangle's monitor's work area.
			p->rgrc[0] = wh_monitor_work_area(p->rgrc[0]);
			return 0;
		}
		LONG top = p->rgrc[0].top;
		DefWindowProcW(c->hwnd, m, wp, lp);
		p->rgrc[0].top = top + wh_hidden_top();
		return 0;
	}
	if (m == WM_NCHITTEST) {
		*handled = true;
		LRESULT r = DefWindowProcW(c->hwnd, m, wp, lp);
		if (r == HTCLIENT && !c->full) {
			POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
			ScreenToClient(c->hwnd, &pt);
			if (pt.y < roundi(CAPTION_HEIGHT * wh_ctl_k(c))) return HTCAPTION;
		}
		return r;
	}
	return 0;
}

// top_edge_hit is the hit-test code at x on a top edge edge_width pixels
// wide in a client area client_width wide: the corner within corner
// pixels of the left, or of the right where the edge reaches it, and the
// top elsewhere.
static LRESULT top_edge_hit(int x, int edge_width, int client_width, int corner) {
	if (x < corner) return HTTOPLEFT;
	if (edge_width >= client_width && x >= client_width - corner) return HTTOPRIGHT;
	return HTTOP;
}

static int index_at(wh_wctl *c, LPARAM lp) {
	wh_caption *b = c->caption;
	POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
	ScreenToClient(b->hwnd, &pt);
	float k = wh_ctl_k(c);
	int bw = roundi(BUTTON_WIDTH * k);
	if (pt.x < 0 || pt.y < 0 || pt.y >= roundi(CAPTION_HEIGHT * k) || bw <= 0) return -1;
	int i = pt.x / bw;
	if (i >= b->nshown) return -1;
	if (mirrored(c)) i = b->nshown - 1 - i;
	return i;
}

static void set_hot(wh_wctl *c, int i) {
	wh_caption *b = c->caption;
	if (i >= 0 && !enabled(c, b->shown[i])) i = -1;
	if (i != b->hot) {
		b->hot = i;
		paint(c);
	}
}

void wh_caption_activate(wh_wctl *c, bool active) {
	if (c->caption && c->caption->active != active) {
		c->caption->active = active;
		paint(c);
	}
}

// click runs a button as the system's do: a system command, sent.
static void click(wh_wctl *c, int btn) {
	if (!enabled(c, btn)) return;
	WPARAM cmd = SC_CLOSE;
	if (btn == BUTTON_MINIMIZE) cmd = SC_MINIMIZE;
	else if (btn == BUTTON_MAXIMIZE) cmd = IsZoomed(c->hwnd) ? SC_RESTORE : SC_MAXIMIZE;
	SendMessageW(c->hwnd, WM_SYSCOMMAND, cmd, 0);
}

LRESULT wh_chrome_message(wh_wctl *c, HWND hwnd, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	*handled = true;
	if (hwnd == c->top_edge) {
		if (m == WM_NCHITTEST) {
			POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
			ScreenToClient(c->hwnd, &pt);
			RECT edge, client;
			GetClientRect(c->top_edge, &edge);
			GetClientRect(c->hwnd, &client);
			return top_edge_hit(pt.x, edge.right, client.right, roundi(RESIZE_CORNER * wh_ctl_k(c)));
		}
		if (m == WM_NCLBUTTONDOWN || m == WM_NCLBUTTONDBLCLK) return SendMessageW(c->hwnd, m, wp, lp);  // resized from the top, as from a caption
		*handled = false;
		return 0;
	}
	wh_caption *b = c->caption;
	if (!b || hwnd != b->hwnd) {
		*handled = false;
		return 0;
	}
	int i;
	switch (m) {
	case WM_NCHITTEST:
		i = index_at(c, lp);
		if (i >= 0 && enabled(c, b->shown[i])) return hit_test(b->shown[i]);
		return HTCLIENT;
	case WM_NCMOUSEMOVE:
		set_hot(c, index_at(c, lp));
		if (!b->tracking) {
			TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE | TME_NONCLIENT, hwnd, 0};
			TrackMouseEvent(&t);
			b->tracking = true;
		}
		*handled = false;  // DefWindowProc brings the snap layouts over HTMAXBUTTON
		return 0;
	case WM_NCMOUSELEAVE:
	case WM_MOUSELEAVE:
		b->tracking = false;
		b->hot = b->pressed = -1;
		paint(c);
		return 0;
	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONDBLCLK:
		// Not DefWindowProc: it would run the system's buttons on this
		// child window.
		i = index_at(c, lp);
		if (i >= 0 && enabled(c, b->shown[i])) {
			b->pressed = i;
			paint(c);
		}
		return 0;
	case WM_NCLBUTTONUP: {
		int pressed = b->pressed;
		i = index_at(c, lp);
		b->pressed = -1;
		paint(c);
		if (i >= 0 && i == pressed) click(c, b->shown[i]);
		return 0;
	}
	case WM_NCRBUTTONDOWN:
	case WM_NCRBUTTONUP:
	case WM_NCRBUTTONDBLCLK:
	case WM_ERASEBKGND:
		return m == WM_ERASEBKGND ? 1 : 0;
	}
	*handled = false;
	return 0;
}

// buttons_dark reports whether a window's caption buttons draw for a dark
// background: its own background's, else the app's appearance.
static bool buttons_dark(wh_wctl *c) {
	if (c->cfg.background) {
		rgba bg = wh_bg_of(c->hwnd);
		return 0.299 * bg.r + 0.587 * bg.g + 0.114 * bg.b < 128;
	}
	return H.dark;
}

// colors are the i-th button's backplate and glyph colors, after
// Windows' own: the glyph in the text color, fainter while the window is
// inactive or the button disabled; under the pointer and pressed, a
// backplate of the glyph's color at 10% and 20%, or red behind a white
// glyph for close. At rest the backplate is all but transparent, which
// keeps the buttons' window taking the mouse there.
static void colors(wh_wctl *c, int i, int btn, bool dark, rgba *back, rgba *fore) {
	wh_caption *b = c->caption;
	*fore = dark ? (rgba){0xFF, 0xFF, 0xFF, 0xFF} : (rgba){0, 0, 0, 0xE4};
	*back = (rgba){0, 0, 0, 1};
	rgba red = wh_windows11() ? (rgba){0xC4, 0x2B, 0x1C, 0xFF} : (rgba){0xE8, 0x11, 0x23, 0xFF};
	rgba white = {0xFF, 0xFF, 0xFF, 0xFF};
	bool hot = i == b->hot && (b->pressed < 0 || b->pressed == i);
	if (!enabled(c, btn)) fore->a = 0x5C;
	else if (hot && b->pressed == i && btn == BUTTON_CLOSE) *back = (rgba){red.r, red.g, red.b, 0xCC}, *fore = white;
	else if (hot && btn == BUTTON_CLOSE) *back = red, *fore = white;
	else if (hot && b->pressed == i) *back = (rgba){fore->r, fore->g, fore->b, 0x33};
	else if (hot) *back = (rgba){fore->r, fore->g, fore->b, 0x1A};
	else if (!b->active) fore->a = 0x72;
}

// blend lays a glyph color of coverage cov over a backplate, into a
// premultiplied BGRA pixel, as UpdateLayeredWindow takes it.
static void blend(uint8_t *px, rgba fore, uint32_t cov, rgba back) {
	uint32_t fa = fore.a * cov / 255, ba = back.a * (255 - fa) / 255;
#define CH(f, b) (uint8_t)(((uint32_t)(f) * fa + (uint32_t)(b) * ba) / 255)
	px[0] = CH(fore.b, back.b), px[1] = CH(fore.g, back.g), px[2] = CH(fore.r, back.r), px[3] = (uint8_t)(fa + ba);
#undef CH
}

static HBITMAP dib(int w, int h, uint8_t **px) {
	BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB}};
	return CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)px, NULL, 0);
}

// paint draws the buttons for the window's state and appearance into
// their layered window: the glyphs drawn white on black give their
// coverage, laid in their colors over their backplates.
static void paint(wh_wctl *c) {
	wh_caption *b = c->caption;
	if (!b || !b->nshown) return;
	float k = wh_ctl_k(c);
	int bw = roundi(BUTTON_WIDTH * k), height = roundi(CAPTION_HEIGHT * k), width = bw * b->nshown;
	if (width <= 0 || height <= 0) return;
	uint8_t *px, *cov;
	HBITMAP img = dib(width, height, &px), mask = dib(width, height, &cov);
	if (!img || !mask) {
		if (img) DeleteObject(img);
		if (mask) DeleteObject(mask);
		return;
	}
	HDC mdc = CreateCompatibleDC(NULL);
	HGDIOBJ old_mask = SelectObject(mdc, mask);
	char face[64];
	WideCharToMultiByte(CP_UTF8, 0, wh_icon_face(), -1, face, sizeof face, NULL, NULL);
	font_spec fs = {.size = GLYPH_SIZE, .weight = 400, .family = face, .k = k, .gray = true};
	HGDIOBJ old_font = SelectObject(mdc, wh_font(fs));
	SetBkMode(mdc, TRANSPARENT);
	SetTextColor(mdc, 0xFFFFFF);
	bool maximized = IsZoomed(c->hwnd), mirror = mirrored(c);
	for (int i = 0; i < b->nshown; i++) {
		wchar_t g = glyph(b->shown[i], maximized);
		int x = (mirror ? b->nshown - 1 - i : i) * bw;
		RECT r = {x, 0, x + bw, height};
		DrawTextW(mdc, &g, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	SelectObject(mdc, old_font);
	SelectObject(mdc, old_mask);
	GdiFlush();
	bool dark = buttons_dark(c);
	for (int i = 0; i < b->nshown; i++) {
		rgba back, fore;
		colors(c, i, b->shown[i], dark, &back, &fore);
		int x0 = (mirror ? b->nshown - 1 - i : i) * bw;
		for (int y = 0; y < height; y++)
			for (int x = x0; x < x0 + bw; x++) {
				size_t o = ((size_t)y * (size_t)width + (size_t)x) * 4;
				blend(px + o, fore, cov[o + 1], back);  // the green channel: the glyph's coverage
			}
	}
	HGDIOBJ old = SelectObject(mdc, img);
	SIZE size = {width, height};
	POINT src = {0, 0};
	BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	UpdateLayeredWindow(b->hwnd, NULL, NULL, &size, mdc, &src, 0, &bf, ULW_ALPHA);
	SelectObject(mdc, old);
	DeleteDC(mdc);
	DeleteObject(img);
	DeleteObject(mask);
}
