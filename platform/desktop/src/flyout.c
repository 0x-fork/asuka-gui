// flyout.c: where a flyout window goes against its anchor, and a panel
// across its display (desktop.h).
#include "desktop.h"

static double minf(double a, double b) { return a < b ? a : b; }
static double maxf(double a, double b) { return a > b ? a : b; }

dsk_rect dsk_flyout_center(double w, double h, dsk_rect r) {
	w = minf(w, r.w), h = minf(h, r.h);
	return (dsk_rect){r.x + (r.w - w) / 2, r.y + (r.h - h) / 3, w, h};
}

enum { BOTTOM, TOP, RIGHT, LEFT };
enum { CENTER, START, END };

// line_up is where a flyout size long starts along an anchor at pos, n
// long, lined up as a says; an end with no room flips to the other end
// where that has room.
static double line_up(double pos, double n, double size, int a, double lo, double hi) {
	switch (a) {
	case START:
		if (pos + size > hi && pos + n - size >= lo) return pos + n - size;
		return pos;
	case END:
		if (pos + n - size < lo && pos + size <= hi) return pos;
		return pos + n - size;
	}
	return pos + (n - size) / 2;
}

dsk_rect dsk_flyout_place(dsk_rect anchor, double w, double h, int p, double gap, dsk_rect work) {
	if (p < 0 || p > 11) p = 0;  // AK_PLACEMENT_BOTTOM .. AK_PLACEMENT_LEFT_END
	int s = p / 3, a = p % 3;
	dsk_rect r = {0, 0, w, h};
	if (s == BOTTOM || s == TOP) {
		double after = work.y + work.h - (anchor.y + anchor.h + gap), before = anchor.y - gap - work.y;
		if (s == BOTTOM && h > after && before > after) s = TOP;
		else if (s == TOP && h > before && after > before) s = BOTTOM;
		r.y = s == BOTTOM ? anchor.y + anchor.h + gap : anchor.y - gap - h;
		r.x = line_up(anchor.x, anchor.w, w, a, work.x, work.x + work.w);
	} else {
		double after = work.x + work.w - (anchor.x + anchor.w + gap), before = anchor.x - gap - work.x;
		if (s == RIGHT && w > after && before > after) s = LEFT;
		else if (s == LEFT && w > before && after > before) s = RIGHT;
		r.x = s == RIGHT ? anchor.x + anchor.w + gap : anchor.x - gap - w;
		r.y = line_up(anchor.y, anchor.h, h, a, work.y, work.y + work.h);
	}
	// Slid back into the work area, then shrunk to it.
	r.w = minf(r.w, work.w), r.h = minf(r.h, work.h);
	r.x = maxf(minf(r.x, work.x + work.w - r.w), work.x);
	r.y = maxf(minf(r.y, work.y + work.h - r.h), work.y);
	return r;
}
