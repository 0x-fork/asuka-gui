// cmdbar.c: a screen's Toolbar and Primary on Windows: a command bar under
// its stack's screens (Fluent's CommandBar at the bottom of a page). The
// Toolbar's buttons go from the leading edge, each its icon with its title
// beside it; those that don't fit go in the menu of a More options button
// after them. The Primary action is an accent button at the trailing end.
// The bar shows while the stack's top screen has either.
#include "nav.h"

static HWND cmd_button(node *b, const wchar_t *title) {
	HWND btn = CreateWindowExW(0, L"BUTTON", title, WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, b->hwnd, (HMENU)1, GetModuleHandleW(NULL), NULL);
	SendMessageW(btn, WM_UPDATEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);
	wh_bind(b, btn);
	return btn;
}

static node *make_command_bar(node *stack) {
	node *b = wh_node_new(wh_host_id(), KIND_COMMAND_BAR);
	b->hwnd = wh_make_view(0);
	b->nav = wh_alloc(sizeof *b->nav);
	b->nav->hot = -1;
	b->nav->stack = stack;
	b->nav->cmd = wh_alloc(sizeof *b->nav->cmd);
	b->nav->cmd->stack = stack;
	b->k = stack->k, b->dir = stack->dir;
	SetParent(b->hwnd, stack->hwnd);
	b->nav->cmd->more = cmd_button(b, wh_word("moreOptions"));
	b->nav->cmd->primary = cmd_button(b, L"");
	wh_node_add(b);
	return b;
}

// wh_forget_command_bar forgets a stack's command bar, which goes with its
// window.
void wh_forget_command_bar(node *stack) {
	node *f = stack->nav->foot;
	stack->nav->foot = NULL;
	wh_cmdbar *c = f->nav->cmd;
	for (uint32_t i = 0; i < c->nbuttons; i++) wh_unbind(c->buttons[i]);
	wh_unbind(c->more);
	wh_unbind(c->primary);
	wh_unbind(f->hwnd);
	wh_map_del(&H.nodes, f->id);
	free(c->buttons);
	free(c);
	free(f->nav);
	f->nav = NULL;
	wh_free_node(f);
}

static bool shown(node *stack) {
	node *s = wh_top_screen(stack);
	return s && s->nav && (s->nav->toolbar.n || s->nav->primary.n);
}

float wh_cmd_bar_height_of(node *stack) { return stack->kind == AK_KIND_STACK && shown(stack) ? CMD_BAR_HEIGHT : 0; }

void wh_update_command_bar(node *stack) {
	bool show = shown(stack);
	node *f = stack->nav->foot;
	if (!f) {
		if (!show) return;
		f = make_command_bar(stack);
		stack->nav->foot = f;
	}
	wh_cmdbar *c = f->nav->cmd;
	node *screen = wh_top_screen(stack);
	c->screen = screen;
	const actions *toolbar = screen && screen->nav ? &screen->nav->toolbar : NULL;
	const actions *primary = screen && screen->nav ? &screen->nav->primary : NULL;
	uint32_t want = toolbar ? toolbar->n : 0;
	while (c->nbuttons > want) {
		HWND btn = c->buttons[--c->nbuttons];
		wh_unbind(btn);
		DestroyWindow(btn);
	}
	while (c->nbuttons < want) {
		c->buttons = wh_realloc(c->buttons, (c->nbuttons + 1) * sizeof *c->buttons);
		c->buttons[c->nbuttons++] = cmd_button(f, L"");
	}
	for (uint32_t i = 0; i < want; i++) {
		SetWindowTextW(c->buttons[i], toolbar->v[i].title);
		EnableWindow(c->buttons[i], !(toolbar->v[i].flags & AK_MENU_DISABLED));
		InvalidateRect(c->buttons[i], NULL, FALSE);
	}
	if (primary && primary->n) {
		SetWindowTextW(c->primary, primary->v[0].title);
		EnableWindow(c->primary, !(primary->v[0].flags & AK_MENU_DISABLED));
		ShowWindow(c->primary, SW_SHOWNA);
		InvalidateRect(c->primary, NULL, FALSE);
	} else {
		ShowWindow(c->primary, SW_HIDE);
	}
	ShowWindow(f->hwnd, show ? SW_SHOWNA : SW_HIDE);
	InvalidateRect(f->hwnd, NULL, FALSE);
}

// button_width is a command bar button's width in pixels: its icon and
// title, padded as Fluent's AppBarButton with its label on the right.
static int button_width(node *b, const action *a) {
	HDC dc = H.measure_dc;
	HGDIOBJ old = SelectObject(dc, wh_body_font(b));
	int w = wh_text_extent(dc, a->title, (int)wcslen(a->title)).cx + wh_px(b, 24);
	SelectObject(dc, old);
	if (wh_symbol_glyph(a->icon)) w += wh_px(b, 16 + 8);
	return w;
}

void wh_layout_command_bar(node *b) {
	wh_cmdbar *c = b->nav->cmd;
	RECT r;
	GetClientRect(b->hwnd, &r);
	int size = wh_px(b, 40), y = (r.bottom - size) / 2;
	bool rtl = wh_is_rtl(b);
#define ACROSS(x, w) (rtl ? r.right - (x) - (w) : (x))
	const actions *toolbar = c->screen && c->screen->nav ? &c->screen->nav->toolbar : NULL;
	const actions *primary = c->screen && c->screen->nav ? &c->screen->nav->primary : NULL;
	UINT f = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;
	int end = r.right - wh_px(b, 8);
	if (primary && primary->n) {
		int w = button_width(b, &primary->v[0]);
		end -= w;
		SetWindowPos(c->primary, NULL, ACROSS(end, w), y, w, size, f);
		end -= wh_px(b, 8);
	}
	int more_w = wh_px(b, 40);
	uint32_t n = toolbar ? toolbar->n : 0;
	int *widths = wh_alloc((n ? n : 1) * sizeof *widths);
	int total = 0;
	for (uint32_t i = 0; i < n; i++) {
		widths[i] = button_width(b, &toolbar->v[i]);
		total += widths[i] + wh_px(b, 4);
	}
	int x = wh_px(b, 8);
	c->fit = n;
	if (x + total > end) {
		// What fits before the overflow button.
		int room = end - more_w - wh_px(b, 4) - x;
		c->fit = 0;
		while (c->fit < n && widths[c->fit] + wh_px(b, 4) <= room) {
			room -= widths[c->fit] + wh_px(b, 4);
			c->fit++;
		}
	}
	for (uint32_t i = 0; i < c->nbuttons; i++) {
		if (i >= c->fit) {
			ShowWindow(c->buttons[i], SW_HIDE);
			continue;
		}
		SetWindowPos(c->buttons[i], NULL, ACROSS(x, widths[i]), y, widths[i], size, f | SWP_SHOWWINDOW);
		x += widths[i] + wh_px(b, 4);
	}
	if (c->fit < n) SetWindowPos(c->more, NULL, ACROSS(x, more_w), y, more_w, size, f | SWP_SHOWWINDOW);
	else ShowWindow(c->more, SW_HIDE);
	free(widths);
#undef ACROSS
}

static void copy_title(wchar_t *title, int cap, const wchar_t *s) {
	wcsncpy(title, s, (size_t)cap - 1);
	title[cap - 1] = 0;
}

uint16_t wh_command_bar_button(node *n, HWND ctl, bool rtl, wchar_t *title, int cap) {
	wh_cmdbar *c = n->nav->cmd;
	node *s = c->screen;
	if (!s || !s->nav) return 0;
	if (ctl == c->more) return 0xE712;  // More
	if (ctl == c->primary && s->nav->primary.n) {
		copy_title(title, cap, s->nav->primary.v[0].title);
		return wh_directed_glyph(s->nav->primary.v[0].icon, rtl);
	}
	for (uint32_t i = 0; i < c->nbuttons; i++)
		if (c->buttons[i] == ctl && i < s->nav->toolbar.n) {
			copy_title(title, cap, s->nav->toolbar.v[i].title);
			return wh_directed_glyph(s->nav->toolbar.v[i].icon, rtl);
		}
	return 0;
}

bool wh_command_bar_is_primary(node *n, HWND ctl) { return n->nav && n->nav->cmd && ctl == n->nav->cmd->primary; }

static void pressed(node *s, node *b, const action *a, int64_t base, HWND btn) {
	if (a->flags & AK_MENU_SUBMENU) {
		actions sub = {a->items, a->count};
		wh_pop_up_actions(s, btn, b, &sub, base);
		return;
	}
	wh_emit_int(s, AK_EVENT_ACTION, base + a->index);
}

// wh_command_bar_command is a click on a command bar's button: its Action,
// or its menu (its children; the overflow's: the toolbar's buttons that
// don't fit).
void wh_command_bar_command(node *b, HWND ctl) {
	wh_cmdbar *c = b->nav->cmd;
	node *s = c->screen;
	if (!s || !s->nav) return;
	if (ctl == c->more) {
		if (c->fit < s->nav->toolbar.n) {
			actions rest = {s->nav->toolbar.v + c->fit, s->nav->toolbar.n - c->fit};
			wh_pop_up_actions(s, ctl, b, &rest, AK_SCREEN_TOOLBAR_ACTIONS);
		}
		return;
	}
	if (ctl == c->primary) {
		if (s->nav->primary.n) pressed(s, b, &s->nav->primary.v[0], AK_SCREEN_PRIMARY_ACTION, ctl);
		return;
	}
	for (uint32_t i = 0; i < c->nbuttons; i++)
		if (c->buttons[i] == ctl && i < s->nav->toolbar.n) pressed(s, b, &s->nav->toolbar.v[i], AK_SCREEN_TOOLBAR_ACTIONS, ctl);
}

// wh_paint_command_bar draws a hairline over the bar.
void wh_paint_command_bar(node *b, gfx g, RECT r) {
	if (!g) return;
	float k = wh_scale_of(b);
	g_fill_rect(g, rgba_argb(H.theme.separator), 0, 0, (float)r.right, wh_maxf(k, 1));
}

// wh_tap_command_bar presses the command bar's button with this title
// (the automation's tap).
bool wh_tap_command_bar(node *b, const wchar_t *text) {
	wh_cmdbar *c = b->nav->cmd;
	node *s = c->screen;
	if (!s || !s->nav || !IsWindowVisible(b->hwnd)) return false;
	if (s->nav->primary.n && !wcscmp(s->nav->primary.v[0].title, text)) {
		SendMessageW(c->primary, BM_CLICK, 0, 0);
		return true;
	}
	for (uint32_t i = 0; i < s->nav->toolbar.n; i++) {
		const action *a = &s->nav->toolbar.v[i];
		if (wcscmp(a->title, text) || i >= c->nbuttons) continue;
		if (i < c->fit) SendMessageW(c->buttons[i], BM_CLICK, 0, 0);
		else if (!(a->flags & (AK_MENU_SUBMENU | AK_MENU_DISABLED))) wh_emit_int(s, AK_EVENT_ACTION, AK_SCREEN_TOOLBAR_ACTIONS + a->index);
		return true;
	}
	return false;
}
