// Segmented on Windows: a tab control drawn as buttons of one width
// (TCS_BUTTONS, TCS_FIXEDWIDTH), Windows' nearest to a segmented control,
// in a window of the packages' own class that hears its TCN_SELCHANGE
// and gives it its size.
#include "asuka_windows.h"
#include "segmented.asuka.h"

#include <commctrl.h>
#include <math.h>
#include <stdlib.h>

typedef struct {
	segmented_Segmented base;
	segmented_Segmented_events events;
	HWND tabs;
	HFONT font;
	int64_t selected;
	bool destroyed;
} view;

static UINT dpi_of(HWND w) {
	UINT d = GetDpiForWindow(w);
	return d ? d : 96;
}

// handle is the container's: its tab control's notifications, its size,
// and its end (the view's memory goes with the window).
static bool handle(void *ctx, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result) {
	(void)wp;
	view *v = ctx;
	switch (msg) {
	case WM_NOTIFY: {
		NMHDR *h = (NMHDR *)lp;
		if (h->hwndFrom != v->tabs || h->code != TCN_SELCHANGE || v->destroyed) return false;
		v->selected = (int64_t)SendMessageW(v->tabs, TCM_GETCURSEL, 0, 0);
		segmented_Segmented_changed(v->events, v->selected);
		*result = 0;
		return true;
	}
	case WM_SIZE:
		if (v->tabs) MoveWindow(v->tabs, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
		return false;
	case WM_NCDESTROY:
		if (v->font) DeleteObject(v->font);
		free(v);
		(void)hwnd;
		return false;
	}
	return false;
}

static void set_items(segmented_Segmented *self, segmented_list_string items) {
	view *v = (view *)self;
	SendMessageW(v->tabs, TCM_DELETEALLITEMS, 0, 0);
	for (size_t i = 0; i < items.len; i++) {
		wchar_t *text = asuka_wide(items.data[i]);
		TCITEMW item = {.mask = TCIF_TEXT, .pszText = text};
		SendMessageW(v->tabs, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&item);
		free(text);
	}
	SendMessageW(v->tabs, TCM_SETCURSEL, (WPARAM)v->selected, 0);
}

static void set_selected(segmented_Segmented *self, int64_t selected) {
	view *v = (view *)self;
	v->selected = selected;
	SendMessageW(v->tabs, TCM_SETCURSEL, (WPARAM)selected, 0);
}

// measure is the buttons' size, in points: the last one's far corner.
static bool measure(segmented_Segmented *self, float width, float height, float *ow, float *oh) {
	(void)width, (void)height;
	view *v = (view *)self;
	LRESULT n = SendMessageW(v->tabs, TCM_GETITEMCOUNT, 0, 0);
	if (n <= 0) {
		*ow = *oh = 0;
		return true;
	}
	RECT r;
	if (!SendMessageW(v->tabs, TCM_GETITEMRECT, (WPARAM)(n - 1), (LPARAM)&r)) return false;
	float scale = (float)dpi_of(v->tabs) / 96.0f;
	*ow = (float)r.right / scale;
	*oh = (float)r.bottom / scale;
	return true;
}

static void destroy(segmented_Segmented *self) {
	// The host destroys the window after; the view goes with it.
	((view *)self)->destroyed = true;
}

static const segmented_Segmented_vtable vtable = {
	.destroy = destroy,
	.set_items = set_items,
	.set_selected = set_selected,
	.measure = measure,
};

segmented_Segmented *segmented_make_Segmented(segmented_Segmented_events events) {
	view *v = calloc(1, sizeof *v);
	if (!v) return NULL;
	v->base.vt = &vtable;
	v->events = events;
	HWND box = asuka_win_window(0, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, (HWND)events.parent, handle, v);
	if (!box) {
		free(v);
		return NULL;
	}
	v->tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | TCS_BUTTONS | TCS_FIXEDWIDTH, 0, 0, 0, 0, box, NULL,
	                          GetModuleHandleW(NULL), NULL);
	// Segoe UI at 9 points, the window's DPI.
	v->font = CreateFontW(-MulDiv(9, (int)dpi_of(box), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
	                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	if (v->tabs && v->font) SendMessageW(v->tabs, WM_SETFONT, (WPARAM)v->font, FALSE);
	v->base.widget = box;
	return &v->base;
}
