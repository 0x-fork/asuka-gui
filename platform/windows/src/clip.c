// clip.c: rounded clips (host/windows' clip.go). A child window ignores
// its parent's drawing, so a view that clips with a corner radius is
// shaped by a window region (SetWindowRgn), which clips its children's
// pixels too: the pixels its rounded rectangle covers whole. A region
// has no partial pixels, so the corners' edge pixels, outside it, are the
// parent's, and the parent draws them antialiased: the clipping view's
// tree drawn there and mixed with what's behind it by how much of each
// pixel the rounded rectangle covers. When something in the clipping
// view paints over its corners, the parent draws them again.
//
// A node's clip_rgn is its rounded clip's region (its client
// coordinates), clip_r its radius in pixels; H.round_clips counts them.
#include "host.h"
#include "parts.h"
#include "draw.h"

static node *clip_off;  // the view whose corners are being drawn whole

// clip_radius is n's rounded clip's radius in pixels, 0 for none.
static double clip_radius(node *n, int w, int h) {
	if (!n->clip || n->corner_radius <= 0 || w <= 0 || h <= 0) return 0;
	double r = n->corner_radius * wh_scale_of(n);
	if (r > w / 2.0) r = w / 2.0;
	if (r > h / 2.0) r = h / 2.0;
	return r;
}

// clip_rows is the region of the pixels a w by h rounded rectangle of
// radius rc covers whole (coverage 1), runs of rows of the same width
// merged.
static HRGN clip_rows(int w, int h, double rc) {
	int z = (int)ceil(rc - 0.5);  // rows whose pixel centers are above the corners' centers
	if (z > h / 2) z = h / 2;
	size_t cap = (size_t)z * 2 + 1;
	size_t hs = sizeof(RGNDATAHEADER);
	RGNDATA *d = wh_alloc(hs + cap * sizeof(RECT));
	RECT *rects = (RECT *)d->Buffer;
	DWORD n = 0;
	// The top corners' rows, the middle, the bottom corners' rows: a row's
	// inset is where its pixels' centers enter the corner's circle.
	for (int pass = 0; pass < 3; pass++) {
		int y0 = pass == 0 ? 0 : pass == 1 ? z : h - z;
		int y1 = pass == 0 ? z : pass == 1 ? h - z : h;
		for (int y = y0; y < y1; y++) {
			int x0 = 0, x1 = w, yb = y + 1;
			if (pass == 1) {
				yb = y1;
			} else {
				int ry = pass == 0 ? y : h - 1 - y;
				double dy = rc - (ry + 0.5);
				double s = (rc - 0.5) * (rc - 0.5) - dy * dy;
				double x = s < 0 ? ceil(rc - 0.5) : ceil(rc - 0.5 - sqrt(s));
				x = x < 0 ? 0 : x > w / 2.0 ? w / 2.0 : x;
				x0 = (int)x, x1 = w - (int)x;
			}
			if (x1 > x0 && yb > y) {
				RECT *p = n ? &rects[n - 1] : NULL;
				if (p && p->left == x0 && p->right == x1 && p->bottom == y) {
					p->bottom = yb;
				} else {
					rects[n++] = (RECT){x0, y, x1, yb};
				}
			}
			if (pass == 1) break;
		}
	}
	d->rdh.dwSize = (DWORD)hs;
	d->rdh.iType = RDH_RECTANGLES;
	d->rdh.nCount = n;
	d->rdh.nRgnSize = n * (DWORD)sizeof(RECT);
	d->rdh.rcBound = (RECT){0, 0, w, h};
	HRGN rgn = ExtCreateRegion(NULL, (DWORD)(hs + n * sizeof(RECT)), d);
	free(d);
	return rgn;
}

// wh_clip_coverage is how much of the pixel at (x, y) (pixels from the
// view's top left) a w by h rounded rectangle of radius rc covers.
double wh_clip_coverage(int x, int y, int w, int h, double rc) {
	double px = x + 0.5, py = y + 0.5, dx, dy;
	if (px < rc) dx = rc - px;
	else if (px > w - rc) dx = px - (w - rc);
	else return 1;
	if (py < rc) dy = rc - py;
	else if (py > h - rc) dy = py - (h - rc);
	else return 1;
	double c = rc - hypot(dx, dy) + 0.5;
	return c < 0 ? 0 : c > 1 ? 1 : c;
}

static HRGN copy_rgn(HRGN r) {
	HRGN c = CreateRectRgn(0, 0, 0, 0);
	CombineRgn(c, r, NULL, RGN_COPY);
	return c;
}

// wh_restore_shape gives n's window its own shape again: its rounded
// clip's, or none.
void wh_restore_shape(node *n) { SetWindowRgn(n->hwnd, n->clip_rgn ? copy_rgn(n->clip_rgn) : NULL, TRUE); }

void wh_shape_clip(node *n) {
	if (!n->hwnd) return;
	RECT r;
	GetClientRect(n->hwnd, &r);
	double rc = clip_radius(n, r.right, r.bottom);
	if (rc <= 0) {
		if (n->clip_rgn) {
			DeleteObject(n->clip_rgn);
			n->clip_rgn = NULL;
			H.round_clips--;
			if (!wh_picture_shaped(n)) SetWindowRgn(n->hwnd, NULL, TRUE);
		}
		return;
	}
	HRGN rgn = clip_rows(r.right, r.bottom, rc);
	if (!rgn) return;
	if (!n->clip_rgn) {
		H.round_clips++;
	} else if (n->clip_r == rc && EqualRgn(n->clip_rgn, rgn)) {
		DeleteObject(rgn);
		return;
	} else {
		DeleteObject(n->clip_rgn);
	}
	n->clip_rgn = rgn, n->clip_r = rc;
	if (wh_picture_shaped(n)) return;  // shaped to nothing while it's rotated (picture.c)
	SetWindowRgn(n->hwnd, copy_rgn(rgn), TRUE);
	HWND p = GetParent(n->hwnd);
	if (p) {
		RECT at = wh_rect_in(n->hwnd, p);
		RedrawWindow(p, &at, NULL, RDW_INVALIDATE);  // its corners' edges
	}
}

static bool overlaps(RECT a, RECT b) { return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom; }

// corners are the squares of c's window's corners, in its parent's
// client coordinates.
static void corners(const node *c, RECT at, RECT q[4]) {
	int z = (int)ceil(c->clip_r);
	q[0] = (RECT){at.left, at.top, at.left + z, at.top + z};
	q[1] = (RECT){at.right - z, at.top, at.right, at.top + z};
	q[2] = (RECT){at.left, at.bottom - z, at.left + z, at.bottom};
	q[3] = (RECT){at.right - z, at.bottom - z, at.right, at.bottom};
}

// paint_corners draws c's tree into its corners' squares, over what dc
// has there, mixed by how much its rounded rectangle covers each pixel.
static void paint_corners(node *c, HWND parent, HDC dc) {
	RECT box;
	if (GetClipBox(dc, &box) <= NULLREGION) return;
	RECT at = wh_rect_in(c->hwnd, parent);
	int w = at.right - at.left, h = at.bottom - at.top;
	int z = (int)ceil(c->clip_r);
	if (z <= 0) return;
	HDC mem = NULL;
	HBITMAP bmp = NULL;
	HGDIOBJ old = NULL;
	uint8_t *px = NULL, *behind = NULL;
	RECT q[4];
	corners(c, at, q);
	for (int i = 0; i < 4; i++) {
		if (!overlaps(q[i], box)) continue;
		if (!mem) {
			mem = CreateCompatibleDC(dc);
			bmp = wh_dib(z, z, &px);
			if (!bmp) {
				DeleteDC(mem);
				return;
			}
			old = SelectObject(mem, bmp);
			behind = wh_alloc((size_t)z * (size_t)z * 4);
		}
		BitBlt(mem, 0, 0, z, z, dc, q[i].left, q[i].top, SRCCOPY);
		GdiFlush();
		memcpy(behind, px, (size_t)z * (size_t)z * 4);
		int saved = SaveDC(mem);
		OffsetViewportOrgEx(mem, -q[i].left, -q[i].top, NULL);
		node *off = clip_off;
		clip_off = c;  // drawn whole: its region would leave out the edge
		wh_paint_tree(c->hwnd, parent, mem);
		clip_off = off;
		RestoreDC(mem, saved);
		GdiFlush();
		for (int y = 0; y < z; y++)
			for (int x = 0; x < z; x++) {
				size_t k = ((size_t)y * (size_t)z + (size_t)x) * 4;
				double cov = wh_clip_coverage(q[i].left - at.left + x, q[i].top - at.top + y, w, h, c->clip_r);
				if (cov >= 1) continue;
				for (int j = 0; j < 3; j++) px[k + j] = (uint8_t)(behind[k + j] * (1 - cov) + px[k + j] * cov + 0.5);
				px[k + 3] = 255;
			}
		BitBlt(dc, q[i].left, q[i].top, z, z, mem, 0, 0, SRCCOPY);
	}
	if (mem) {
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
		free(behind);
	}
}

void wh_paint_clip_corners(node *n, HWND hwnd, HDC dc) {
	if (!H.round_clips) return;
	for (uint32_t i = 0; i < n->nkids; i++) {
		node *c = n->kids[i];
		if (c->clip_rgn && c->hwnd && c != clip_off && !c->pict && IsWindowVisible(c->hwnd) && GetParent(c->hwnd) == hwnd)
			paint_corners(c, hwnd, dc);
	}
}

void wh_clip_tree(node *n, HDC dc) {
	if (!n->clip_rgn || n == clip_off) return;
	POINT o;
	GetViewportOrgEx(dc, &o);
	HRGN r = copy_rgn(n->clip_rgn);
	OffsetRgn(r, o.x, o.y);
	ExtSelectClipRgn(dc, r, RGN_AND);
	DeleteObject(r);
}

static bool window_kind(const node *c) {
	return c->kind == KIND_SHEET_WINDOW || c->kind == KIND_WINDOW_FRAME || c->kind == KIND_POPOVER_WINDOW || c->kind == KIND_ALERT_WINDOW;
}

void wh_corners_painted(HWND hwnd) {
	if (!H.round_clips) return;
	RECT r = {0};
	bool have = false;
	for (HWND w = hwnd; w; w = GetParent(w)) {
		node *c = wh_node_of(w);
		if (!c || c->hwnd != w || window_kind(c) || w == H.window) return;
		if (!c->clip_rgn) continue;
		HWND p = GetParent(w);
		if (!p) return;
		if (!have) GetWindowRect(hwnd, &r), have = true;
		RECT at = wh_rect_in(w, p);
		POINT o = {0, 0};
		MapWindowPoints(NULL, p, &o, 1);
		RECT painted = {r.left + o.x, r.top + o.y, r.right + o.x, r.bottom + o.y};
		RECT q[4];
		corners(c, at, q);
		for (int i = 0; i < 4; i++)
			if (overlaps(q[i], painted)) RedrawWindow(p, &q[i], NULL, RDW_INVALIDATE);
	}
}
