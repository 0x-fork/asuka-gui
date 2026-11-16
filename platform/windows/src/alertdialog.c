// alertdialog.c: an alert with text fields (host/windows' alertdialog.go).
// A task dialog holds no edit controls, so it's a dialog of the host's
// own, laid out as WinUI's ContentDialog: a window over the one it's
// declared in (which is disabled meanwhile, as under a sheet), with the
// app's name in its title bar as a task dialog has; in it the title
// (Title2, semibold) and the message (Body), the fields (the host's own
// text boxes, Fluent's), then a band of buttons on the window's
// background below a stroke, as wide as each other, the cancel one last
// and the default one in the accent color.
//
// The first field has the focus. Tab goes through the fields and the
// buttons; Enter in a field goes on to the next, and in the last presses
// the default button (the first whose role is Default) if it's enabled;
// Enter or Space on a button presses it; Escape and the close button are
// the cancel button, when there's one (a task dialog without one can't be
// closed either). Screen readers read the title and the message (its
// statics), each field by its placeholder, and the buttons.
#include "nav.h"
#include "win.h"

// Its measures, in points (ContentDialog's).
#define DLG_WIDTH 400   // its content's width
#define DLG_PAD 24      // around the content and the buttons
#define DLG_GAP 12      // under the title
#define DLG_FIELD 32    // a field's height
#define DLG_SPACE 8     // between fields, between buttons
#define DLG_BUTTON 32   // a button's height
#define DLG_ABOVE 16    // over the first field
#define DLG_STROKE 1

// An alert's dialog, while it shows (its wh_nav's dlg).
typedef struct {
	HWND win;
	node *wn;  // the window's (KIND_ALERT_WINDOW), whose parent is the alert
	HWND owner;
	HWND title, message;  // STATICs (NULL: none)
	node **fields;
	uint32_t nfields;
	HWND *buttons;
	int *actions;  // each button's action
	uint32_t nbuttons;
	int def;     // the default action (-1: none)
	int cancel;  // the cancel action (-1: none)
	HWND focus;
	int footer;  // the buttons' band's top, in pixels
	bool done;
} dialog;

static dialog *dlg_of(node *n) { return n && n->nav ? n->nav->dlg : NULL; }

HWND wh_alert_dialog_window(node *n) {
	dialog *d = dlg_of(n);
	return d ? d->win : NULL;
}

static int layout(node *n, int *ht);

// wh_open_alert_dialog shows an alert with fields as the host's own
// dialog.
void wh_open_alert_dialog(node *n) {
	if (dlg_of(n)) return;
	HWND owner = wh_present_on(n->nav->on);
	if (!owner) owner = H.window;
	dialog *d = wh_alloc(sizeof *d);
	d->owner = owner, d->def = -1, d->cancel = -1;
	for (uint32_t i = 0; i < n->acts.n; i++) {
		if (d->def < 0 && n->acts.v[i].role == AK_ALERT_BUTTON_ROLE_DEFAULT) d->def = (int)i;
		if (n->acts.v[i].role == AK_ALERT_BUTTON_ROLE_CANCEL) d->cancel = (int)i;
	}
	// The buttons: in order, the cancel one last (ContentDialog's close
	// button).
	d->actions = wh_alloc((n->acts.n + 1) * sizeof *d->actions);
	for (uint32_t i = 0; i < n->acts.n; i++)
		if (n->acts.v[i].role != AK_ALERT_BUTTON_ROLE_CANCEL) d->actions[d->nbuttons++] = (int)i;
	if (d->cancel >= 0) d->actions[d->nbuttons++] = d->cancel;
	DWORD style = WS_POPUP | WS_CAPTION | WS_CLIPCHILDREN;
	if (d->cancel >= 0) style |= WS_SYSMENU;  // the close button: Cancel
	int dpi = wh_dpi(owner);
	HWND win = CreateWindowExW(wh_top_level_style() | WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, WH_CLASS_WINDOW, H.app_namew, style, 0, 0, 100, 100,
		owner, NULL, GetModuleHandleW(NULL), NULL);
	dpi = wh_dpi(win);
	float k = (float)dpi / 96;
	node *wn = wh_node_new(0, KIND_ALERT_WINDOW);
	wn->hwnd = win;
	wn->parent = n;
	wn->k = k;
	d->win = win, d->wn = wn;
	wh_bind(wn, win);
	n->nav->dlg = d;
	wh_set_dark_title_bar(win, H.dark);

	bool rtl = wh_is_rtl(n);
	DWORD side = rtl ? SS_RIGHT : SS_LEFT, ex = rtl ? WS_EX_RTLREADING : 0;
	if (n->title.n) {
		d->title = CreateWindowExW(ex, L"STATIC", wstr_c(&n->title), WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_EDITCONTROL | side, 0, 0, 0, 0, win, NULL,
			GetModuleHandleW(NULL), NULL);
		wh_bind(wn, d->title);
	}
	if (n->message.n) {
		d->message = CreateWindowExW(ex, L"STATIC", wstr_c(&n->message), WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_EDITCONTROL | side, 0, 0, 0, 0, win, NULL,
			GetModuleHandleW(NULL), NULL);
		wh_bind(wn, d->message);
	}
	ak_alert_field_vec fs = {0};
	if (n->nav->fields_len) ak_decode_alert_fields(n->nav->fields, n->nav->fields_len, &fs);
	d->fields = wh_alloc((fs.len + 1) * sizeof *d->fields);
	for (uint32_t i = 0; i < fs.len; i++) {
		const ak_alert_field *f = &fs.data[i];
		node *fn = wh_node_new(wh_host_id(), AK_KIND_TEXT_FIELD);
		fn->k = k;
		fn->dir = n->dir;
		fn->alert_of = n;
		wstr_set(&fn->placeholder, f->placeholder);
		wstr_set(&fn->text, f->text);
		fn->secure = (f->flags & AK_ALERT_FIELD_SECURE) != 0;
		fn->keyboard = f->keyboard;
		wh_make_field(fn);
		wh_node_add(fn);
		SetParent(fn->hwnd, win);
		if (fn->ctl) wh_set_accessible_name(fn->ctl, wstr_c(&fn->placeholder));  // no label beside it: its placeholder is its name
		if (i == 0 && fn->ctl) {
			d->focus = fn->ctl;
			SendMessageW(fn->ctl, EM_SETSEL, 0, -1);
		}
		d->fields[d->nfields++] = fn;
	}
	ak_vec_free(&fs);
	d->buttons = wh_alloc((d->nbuttons + 1) * sizeof *d->buttons);
	for (uint32_t k2 = 0; k2 < d->nbuttons; k2++) {
		int i = d->actions[k2];
		const action *a = &n->acts.v[i];
		DWORD bs = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == d->def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
		HWND b = CreateWindowExW(0, L"BUTTON", a->title ? a->title : L"", bs, 0, 0, 0, 0, win, (HMENU)(INT_PTR)(100 + i), GetModuleHandleW(NULL), NULL);
		SendMessageW(b, WM_UPDATEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);
		if (a->flags & AK_MENU_DISABLED) EnableWindow(b, FALSE);
		wh_bind(wn, b);
		d->buttons[k2] = b;
	}
	int ht, w = layout(n, &ht);
	RECT r = {0, 0, w, ht};
	wh_adjust_window_rect(&r, style, WS_EX_DLGMODALFRAME, dpi);
	RECT wr;
	GetWindowRect(owner, &wr);
	int x = wr.left + ((wr.right - wr.left) - (r.right - r.left)) / 2;
	int y = wr.top + ((wr.bottom - wr.top) - (r.bottom - r.top)) / 2;
	SetWindowPos(win, NULL, x, y, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
	EnableWindow(owner, FALSE);
	n->presented = true;
	ShowWindow(win, wh_snapshotting() ? SW_SHOWNOACTIVATE : SW_SHOW);
	if (d->focus) SetFocus(d->focus);
}

// layout places the dialog's controls in its window at its scale,
// mirrored right to left, and returns its client width; ht its height
// (pixels).
static int layout(node *n, int *ht) {
	dialog *d = dlg_of(n);
	node *wn = d->wn;
	float k = wn->k;
	int pad = wh_px(wn, DLG_PAD), cw = wh_px(wn, DLG_WIDTH);
	int w = cw + 2 * pad;
	bool rtl = wh_is_rtl(n);
	int y = pad;
	struct {
		HWND ctl;
		const wstr *s;
		font_spec spec;
	} texts[2] = {
		{d->title, &n->title, wh_spec(AK_TEXT_STYLE_TITLE2, 0, 0, NULL)},
		{d->message, &n->message, wh_spec(AK_TEXT_STYLE_BODY, 0, 0, NULL)},
	};
	for (int i = 0; i < 2; i++) {
		if (!texts[i].ctl) continue;
		if (i == 1 && d->title) y += wh_px(wn, DLG_GAP);
		font_spec spec = texts[i].spec;
		spec.k = k;
		HFONT font = wh_font(spec);
		SendMessageW(texts[i].ctl, WM_SETFONT, (WPARAM)font, 0);
		HDC dc = H.measure_dc;
		HGDIOBJ old = SelectObject(dc, font);
		RECT tr = {0, 0, cw, 0};
		DrawTextW(dc, wstr_c(texts[i].s), texts[i].s->n, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | wh_text_direction(rtl));
		SelectObject(dc, old);
		SetWindowPos(texts[i].ctl, NULL, pad, y, cw, tr.bottom - tr.top, SWP_NOZORDER | SWP_NOACTIVATE);
		y += tr.bottom - tr.top;
	}
	for (uint32_t i = 0; i < d->nfields; i++) {
		node *f = d->fields[i];
		if (i == 0) {
			if (d->title || d->message) y += wh_px(wn, DLG_ABOVE);
		} else {
			y += wh_px(wn, DLG_SPACE);
		}
		f->k = k;
		if (f->ctl) SendMessageW(f->ctl, WM_SETFONT, (WPARAM)wh_node_font(f), 0);
		int fh = wh_px(wn, DLG_FIELD * wh_font_scale());
		SetWindowPos(f->hwnd, NULL, pad, y, cw, fh, SWP_NOZORDER | SWP_NOACTIVATE);
		wh_layout_field(f);
		y += fh;
	}
	y += pad;
	d->footer = y;
	// The buttons, as wide as each other across the band.
	if (d->nbuttons) {
		int c = (int)d->nbuttons;
		int gap = wh_px(wn, DLG_SPACE);
		int bw = (cw - gap * (c - 1)) / c;
		int bh = wh_px(wn, DLG_BUTTON * wh_font_scale());
		for (int i = 0; i < c; i++) {
			int x = pad + i * (bw + gap);
			SetWindowPos(d->buttons[i], NULL, rtl ? w - x - bw : x, y + pad, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		y += pad + bh + pad;
	}
	*ht = y;
	return w;
}

static void cancel(node *wn);

// wh_alert_dialog_message handles the dialog's window's messages.
LRESULT wh_alert_dialog_message(node *wn, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	node *n = wn->parent;
	dialog *d = dlg_of(n);
	*handled = false;
	if (!d) return 0;
	*handled = true;
	switch (m) {
	case WM_PAINT:
		wh_paint_window(wn, wn->hwnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PRINTCLIENT: {
		RECT r;
		GetClientRect(wn->hwnd, &r);
		wh_paint(wn, wn->hwnd, (HDC)wp, r);
		return 0;
	}
	case WM_CLOSE:
		cancel(wn);
		return 0;
	case WM_ACTIVATE:
		if (LOWORD(wp) != WA_INACTIVE) {
			H.in_use = wn->hwnd;
			if (d->focus && IsWindow(d->focus)) SetFocus(d->focus);
		} else {
			HWND f = GetFocus();
			if (f && IsChild(wn->hwnd, f)) d->focus = f;  // where the focus goes back to
		}
		return 0;
	case WM_DPICHANGED: {
		wn->k = (float)LOWORD(wp) / 96;
		RECT s = *(RECT *)lp;
		int ht, w = layout(n, &ht);
		RECT r = {0, 0, w, ht};
		wh_adjust_window_rect(&r, (DWORD)GetWindowLongPtrW(wn->hwnd, GWL_STYLE), WS_EX_DLGMODALFRAME, LOWORD(wp));
		SetWindowPos(wn->hwnd, NULL, s.left, s.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
		RedrawWindow(wn->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
		return 0;
	}
	}
	*handled = false;
	return 0;
}

// wh_paint_alert_dialog draws the dialog's background: its content on the
// layer's color, the buttons' band on the window's, a stroke between.
void wh_paint_alert_dialog(node *wn, gfx g, RECT r) {
	dialog *d = dlg_of(wn->parent);
	if (!d) return;
	const palette *p = &H.theme;
	float w = (float)(r.right - r.left), ht = (float)(r.bottom - r.top);
	float top = (float)d->footer;
	g_fill_rect(g, rgba_argb(p->layer), 0, 0, w, top);
	g_fill_rect(g, rgba_argb(p->bg), 0, top, w, ht - top);
	g_fill_rect(g, rgba_argb(p->card_stroke), 0, top, w, wh_maxf((float)wh_px(wn, DLG_STROKE), 1));
}

// wh_alert_dialog_colors answers a static's WM_CTLCOLORSTATIC: the label's
// color on the content's.
HBRUSH wh_alert_dialog_colors(HDC dc) {
	rgba bg = H.theme.layer;
	SetTextColor(dc, rgba_ref(rgba_over(H.theme.label, bg)));
	SetBkColor(dc, rgba_ref(bg));
	return wh_brush(bg);
}

// press presses action i, if it's enabled: Action, and the core destroys
// the alert, which closes the dialog.
static void press(node *n, int i) {
	dialog *d = dlg_of(n);
	if (!d || d->done || i < 0 || (uint32_t)i >= n->acts.n || (n->acts.v[i].flags & AK_MENU_DISABLED) || n->dying) return;
	d->done = true;
	wh_emit_int(n, AK_EVENT_ACTION, i);
}

// wh_alert_dialog_command is a button of the dialog pressed: its action.
void wh_alert_dialog_command(node *wn, HWND ctl, UINT code) {
	node *n = wn->parent;
	dialog *d = dlg_of(n);
	if (code != BN_CLICKED || !d) return;
	for (uint32_t k = 0; k < d->nbuttons; k++)
		if (d->buttons[k] == ctl) {
			press(n, d->actions[k]);
			return;
		}
}

// cancel is Escape or the close button: the cancel button, when there's
// one.
static void cancel(node *wn) {
	node *n = wn->parent;
	dialog *d = dlg_of(n);
	if (d && d->cancel >= 0) press(n, d->cancel);
}

void wh_cancel_alert_dialog(node *wn) { cancel(wn); }

// wh_alert_field_event is an event of a dialog's field: typing is the
// alert's FieldChanged; Enter goes on to the next field, and in the last
// presses the default button if it's enabled.
void wh_alert_field_event(node *f, ak_event *e) {
	node *n = f->alert_of;
	dialog *d = dlg_of(n);
	if (!d || n->dying) return;
	int i = -1;
	for (uint32_t k = 0; k < d->nfields; k++)
		if (d->fields[k] == f) i = (int)k;
	if (i < 0) return;
	switch (e->kind) {
	case AK_EVENT_TEXT_CHANGED: {
		ak_buf b = {0};
		ak_append_field_changed(&b, (uint32_t)i, e->s);
		wh_emit_bytes(n, AK_EVENT_FIELD_CHANGED, b.data, b.len);
		ak_buf_free(&b);
		break;
	}
	case AK_EVENT_SUBMIT:
		if ((uint32_t)i + 1 < d->nfields) {
			HWND next = d->fields[i + 1]->ctl;
			SetFocus(next);
			SendMessageW(next, EM_SETSEL, 0, -1);
			return;
		}
		press(n, d->def);
		break;
	default:
		break;
	}
}

// wh_update_alert_dialog shows the buttons' titles and Disabled again.
void wh_update_alert_dialog(node *n) {
	dialog *d = dlg_of(n);
	if (!d) return;
	for (uint32_t k = 0; k < d->nbuttons; k++) {
		int i = d->actions[k];
		if ((uint32_t)i >= n->acts.n) continue;
		const action *a = &n->acts.v[i];
		HWND b = d->buttons[k];
		wchar_t cur[512];
		GetWindowTextW(b, cur, (int)WH_LEN(cur));
		const wchar_t *t = a->title ? a->title : L"";
		if (wcscmp(cur, t)) SetWindowTextW(b, t);
		EnableWindow(b, !(a->flags & AK_MENU_DISABLED));
		InvalidateRect(b, NULL, FALSE);
	}
}

// wh_close_alert_dialog closes the dialog: the window it's over comes
// back first, so it's activated.
void wh_close_alert_dialog(node *n) {
	dialog *d = dlg_of(n);
	if (!d) return;
	n->nav->dlg = NULL;
	EnableWindow(d->owner, TRUE);
	if (H.in_use == d->win) H.in_use = d->owner;
	for (uint32_t i = 0; i < d->nfields; i++)
		if (d->fields[i]->ctl) wh_clear_accessible_name(d->fields[i]->ctl);  // as Dynamic Annotation asks, before the window goes
	wh_forget_tree(d->win);
	DestroyWindow(d->win);
	for (uint32_t i = 0; i < d->nfields; i++) {
		node *f = d->fields[i];
		wh_map_del(&H.nodes, f->id);
		f->alert_of = NULL;
		wh_free_node(f);
	}
	d->wn->parent = NULL;
	wh_free_node(d->wn);
	free(d->fields);
	free(d->buttons);
	free(d->actions);
	free(d);
}

// wh_alert_dialog_default_button reports whether ctl is the dialog's
// default button.
bool wh_alert_dialog_default_button(node *wn, HWND ctl) {
	dialog *d = dlg_of(wn->parent);
	if (!d) return false;
	for (uint32_t k = 0; k < d->nbuttons; k++)
		if (d->buttons[k] == ctl) return d->actions[k] == d->def;
	return false;
}

// wh_alert_dialog_tap is automation's tap on a dialog showing: a button's
// title.
bool wh_alert_dialog_tap(const wchar_t *t) {
	uint32_t i = 0;
	void *v;
	while (wh_map_next(&H.nodes, &i, NULL, &v)) {
		node *n = v;
		dialog *d = n->kind == AK_KIND_ALERT ? dlg_of(n) : NULL;
		if (!d || !IsWindowVisible(d->win)) continue;
		for (uint32_t k = 0; k < d->nbuttons; k++) {
			wchar_t cur[512];
			GetWindowTextW(d->buttons[k], cur, (int)WH_LEN(cur));
			if (!wcscmp(cur, t)) {
				SendMessageW(d->buttons[k], BM_CLICK, 0, 0);
				return true;
			}
		}
	}
	return false;
}
