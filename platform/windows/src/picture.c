// picture.c: pictures (host/windows' picture.go, opacity.go): what a
// child window can't be, a view of the host's (a picture, above the view
// in its parent, taking no clicks) draws: a rotated view, turned about its
// center, and a view with child windows at an opacity between 0 and 1,
// faded as a whole (child windows can't be translucent: a layered child
// window's own children aren't painted). The picture draws what's behind
// it, then the view's tree as it paints, turned or blended. A rotated
// view's own window is shaped to nothing meanwhile, so neither it nor
// what's in it shows or takes clicks: a turned view is drawn, not used. A
// faded one stays, under its picture, and takes clicks. What's in it
// painting, or moving, draws the picture again.
#include "host.h"
#include "parts.h"
#include "draw.h"

struct wh_picture {
	node *pic;    // the picture's view (its host_data: the view it draws)
	float deg;    // its turn, degrees clockwise
	bool shaped;  // the view's window is shaped to nothing
};

static int npictures;

HBITMAP wh_dib(int w, int h, uint8_t **px) {
	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = NULL;
	HBITMAP b = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	*px = bits;
	return bits ? b : NULL;
}

bool wh_picture_shaped(const node *n) { return n->pict && n->pict->shaped; }

// faded is whether n is drawn faded by a picture: it has child windows.
static bool faded(const node *n) { return n->hwnd && n->opacity > 0.01f && n->opacity < 1 && GetWindow(n->hwnd, GW_CHILD); }

static LRESULT picture_message(node *p, UINT m, WPARAM wp, LPARAM lp, bool *handled);

void wh_update_picture(node *n) {
	if (!n->hwnd || n->kind == KIND_PICTURE) return;
	float deg = fmodf(n->xf.rot, 360);
	if (deg == 0 && !faded(n)) {
		if (n->pict) {
			// Let it go, and show n again.
			wh_picture *p = n->pict;
			bool shaped = p->shaped;
			wh_drop_picture(n);
			if (n->hwnd) {
				if (shaped) wh_restore_shape(n);
				wh_redraw(n);
			}
		}
		return;
	}
	if (!n->pict) {
		node *p = wh_node_new(wh_host_id(), KIND_PICTURE);
		p->hwnd = wh_make_view(0);
		p->k = n->k;
		p->host_msg = picture_message;
		p->host_data = n;
		wh_node_add(p);
		n->pict = wh_alloc(sizeof *n->pict);
		n->pict->pic = p;
		npictures++;
		wh_redraw(n);  // its own drawing, unfaded: the picture fades it
	}
	n->pict->deg = deg;
	bool shaped = deg != 0;
	if (shaped != n->pict->shaped) {
		n->pict->shaped = shaped;
		if (shaped) SetWindowRgn(n->hwnd, CreateRectRgn(0, 0, 0, 0), TRUE);
		else wh_restore_shape(n);
	}
	wh_place_picture(n);
}

void wh_drop_picture(node *n) {
	wh_picture *pict = n->pict;
	if (!pict) return;
	n->pict = NULL;
	node *p = pict->pic;
	free(pict);
	npictures--;
	wh_map_del(&H.nodes, p->id);
	wh_release(p);
	wh_free_node(p);
}

// turned_bounds is the rectangle around r turned deg degrees about its
// center, a pixel more for the antialiased edge.
static RECT turned_bounds(RECT r, float deg) {
	double cx = (r.left + r.right) / 2.0, cy = (r.top + r.bottom) / 2.0;
	double w = (r.right - r.left) / 2.0, h = (r.bottom - r.top) / 2.0;
	double a = deg * M_PI / 180;
	double c = fabs(cos(a)), s = fabs(sin(a));
	double hw = w * c + h * s, hh = w * s + h * c;
	return (RECT){(LONG)floor(cx - hw) - 1, (LONG)floor(cy - hh) - 1, (LONG)ceil(cx + hw) + 1, (LONG)ceil(cy + hh) + 1};
}

void wh_place_picture(node *n) {
	if (!n->pict || !n->hwnd) return;
	node *p = n->pict->pic;
	HWND parent = GetParent(n->hwnd);
	if (GetParent(p->hwnd) != parent) SetParent(p->hwnd, parent);
	RECT b = wh_rect_in(n->hwnd, parent);
	if (n->pict->deg != 0) b = turned_bounds(b, n->pict->deg);
	HWND after = GetWindow(n->hwnd, GW_HWNDPREV);
	if (!after) after = HWND_TOP;
	if (after == p->hwnd)
		SetWindowPos(p->hwnd, NULL, b.left, b.top, b.right - b.left, b.bottom - b.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	else
		SetWindowPos(p->hwnd, after, b.left, b.top, b.right - b.left, b.bottom - b.top, SWP_NOACTIVATE | SWP_NOCOPYBITS);
	bool vis = IsWindowVisible(n->hwnd);
	if (vis != (bool)IsWindowVisible(p->hwnd)) ShowWindow(p->hwnd, vis ? SW_SHOWNA : SW_HIDE);
	InvalidateRect(p->hwnd, NULL, FALSE);
}

static LRESULT picture_message(node *p, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	(void)lp;
	*handled = false;
	switch (m) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(p->hwnd, &ps);
		RECT r;
		GetClientRect(p->hwnd, &r);
		if (r.right > 0 && r.bottom > 0) {
			HDC mem = NULL;
			HPAINTBUFFER pb = BeginBufferedPaint(dc, &r, BPBF_COMPATIBLEBITMAP, NULL, &mem);
			wh_paint_picture(p, pb && mem ? mem : dc, true);
			if (pb) EndBufferedPaint(pb, TRUE);
		}
		EndPaint(p->hwnd, &ps);
		*handled = true;
		return 0;
	}
	case WM_PRINTCLIENT:
		wh_paint_picture(p, (HDC)wp, true);
		*handled = true;
		return 0;
	case WM_ERASEBKGND:
		*handled = true;
		return 1;
	}
	return 0;
}

// draw_tree draws n's tree into dc, n's top left at (x, y) of it, as it
// paints.
static void draw_tree(node *n, HWND parent, HDC dc, RECT at, int x, int y) {
	int saved = SaveDC(dc);
	OffsetViewportOrgEx(dc, x - at.left, y - at.top, NULL);
	node *was = H.drawing;
	H.drawing = n;
	wh_paint_tree(n->hwnd, parent, dc);
	H.drawing = was;
	RestoreDC(dc, saved);
}

static BYTE alpha255(float v) { return (BYTE)lroundf(wh_clampf(v, 0, 1) * 255); }

// paint_faded draws n's tree over what dc has at r (n's place in it) and
// blends it with that at n's opacity.
static void paint_faded(node *n, HWND parent, HDC dc, RECT r, RECT at) {
	int w = r.right - r.left, h = r.bottom - r.top;
	HDC mem = CreateCompatibleDC(dc);
	uint8_t *px;
	HBITMAP bmp = wh_dib(w, h, &px);
	if (!bmp) {
		DeleteDC(mem);
		return;
	}
	HGDIOBJ old = SelectObject(mem, bmp);
	BitBlt(mem, 0, 0, w, h, dc, r.left, r.top, SRCCOPY);
	draw_tree(n, parent, mem, at, 0, 0);
	GdiFlush();
	for (size_t i = 3; i < (size_t)w * (size_t)h * 4; i += 4) px[i] = 255;
	BLENDFUNCTION bf = {AC_SRC_OVER, 0, alpha255(n->opacity), 0};
	AlphaBlend(dc, r.left, r.top, w, h, mem, 0, 0, w, h, bf);
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
}

// tree_picture is n's tree drawn as it would show, unturned, w by h
// pixels of premultiplied BGRA (free it): drawn over black and over
// white, which tells each pixel's coverage, so what's behind it doesn't
// show.
static uint8_t *tree_picture(node *n, HWND parent, RECT at) {
	int w = at.right - at.left, h = at.bottom - at.top;
	size_t size = (size_t)w * (size_t)h * 4;
	uint8_t *out[2] = {NULL, NULL};
	for (int i = 0; i < 2; i++) {
		HDC mem = CreateCompatibleDC(NULL);
		uint8_t *px;
		HBITMAP bmp = wh_dib(w, h, &px);
		if (!bmp) {
			DeleteDC(mem);
			free(out[0]);
			return NULL;
		}
		HGDIOBJ old = SelectObject(mem, bmp);
		memset(px, i ? 255 : 0, size);
		draw_tree(n, parent, mem, at, 0, 0);
		GdiFlush();
		out[i] = wh_alloc(size);
		memcpy(out[i], px, size);
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
	}
	uint8_t *black = out[0], *white = out[1];
	for (size_t i = 0; i + 3 < size; i += 4) {
		// Over black a pixel is its color times its coverage; over white
		// that plus the white left showing.
		int a = 255 - ((int)white[i + 1] - (int)black[i + 1]);
		a = a < 0 ? 0 : a > 255 ? 255 : a;
		black[i + 3] = (uint8_t)a;
		for (int j = 0; j < 3; j++)
			if (black[i + j] > a) black[i + j] = (uint8_t)a;
	}
	free(white);
	return black;
}

void wh_paint_picture(node *p, HDC dc, bool behind) {
	node *n = p->host_data;
	if (!n || !n->hwnd || !n->pict || n->pict->pic != p) return;
	if (behind) {
		HWND skip = H.skip_tree;
		H.skip_tree = n->hwnd;  // the picture draws it
		wh_paint_behind(p->hwnd, dc);
		H.skip_tree = skip;
	}
	HWND parent = GetParent(n->hwnd);
	RECT at = wh_rect_in(n->hwnd, parent), pr = wh_rect_in(p->hwnd, parent);
	int w = at.right - at.left, h = at.bottom - at.top;
	if (w <= 0 || h <= 0) return;
	if (n->pict->deg == 0) {
		paint_faded(n, parent, dc, (RECT){at.left - pr.left, at.top - pr.top, at.right - pr.left, at.bottom - pr.top}, at);
		return;
	}
	uint8_t *px = tree_picture(n, parent, at);
	if (!px) return;
	if (n->opacity < 1)
		for (size_t i = 0; i < (size_t)w * (size_t)h * 4; i++) px[i] = (uint8_t)(px[i] * n->opacity + 0.5f);
	GpBitmap *b = g_bitmap(w, h, px);
	free(px);
	if (!b) return;
	gfx g = g_new(dc);
	float cx = (at.left + at.right) / 2.0f - (float)pr.left, cy = (at.top + at.bottom) / 2.0f - (float)pr.top;
	GdipTranslateWorldTransform(g, cx, cy, MatrixOrderPrepend);
	GdipRotateWorldTransform(g, n->pict->deg, MatrixOrderPrepend);
	g_draw_image(g, b, 1, -w / 2.0f, -h / 2.0f, (float)w, (float)h, 0, 0, (float)w, (float)h);
	g_close(g);
	GdipDisposeImage((GpImage *)b);
}

bool wh_tree_in_picture(node *n) { return n->pict && H.drawing != n; }

static bool window_kind(const node *c) {
	return c->kind == KIND_SHEET_WINDOW || c->kind == KIND_WINDOW_FRAME || c->kind == KIND_POPOVER_WINDOW || c->kind == KIND_ALERT_WINDOW;
}

void wh_pictures_stale(HWND hwnd) {
	if (!npictures) return;
	for (HWND w = hwnd; w; w = GetParent(w)) {
		node *n = wh_node_of(w);
		if (!n || window_kind(n) || w == H.window) return;
		if (n->hwnd == w && n->pict) InvalidateRect(n->pict->pic->hwnd, NULL, FALSE);
	}
}
