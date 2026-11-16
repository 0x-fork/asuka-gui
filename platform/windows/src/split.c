// split.c: a SplitView (host/windows' split.go) is WinUI's NavigationView,
// left: a pane of the destinations (drawn by the host, its section headers
// and items, the selected one on a subtle fill with an accent pill at its
// leading edge) beside the detail Stack, which fills the rest. And what a
// screen reader hears as the keyboard chooses a tab or an item.
#include "nav.h"

#define PANE_WIDTH 280

typedef struct {
	wstr title, badge;
	char *icon;
	bool header;
	int index;  // a destination's, among them all
} pane_row;

// A sidebar's (KIND_SIDEBAR) state: its node's split.
struct wh_split {
	node *split;
	pane_row *rows;
	uint32_t nrows;
	int selected, hot;
};

static LRESULT pane_message(node *b, UINT m, WPARAM wp, LPARAM lp, bool *handled);

static void free_rows(wh_split *s) {
	for (uint32_t i = 0; i < s->nrows; i++) {
		wstr_free(&s->rows[i].title);
		wstr_free(&s->rows[i].badge);
		free(s->rows[i].icon);
	}
	free(s->rows);
	s->rows = NULL;
	s->nrows = 0;
}

// wh_split_release lets go of a SplitView's pane, with the view.
void wh_split_release(node *n) {
	node *b = n->pane_node;
	if (!b) return;
	n->pane_node = NULL;
	wh_map_del(&H.nodes, b->id);
	if (b->hwnd) {
		HWND hw = b->hwnd;
		wh_forget_tree(hw);
		DestroyWindow(hw);
	}
	if (b->split) {
		free_rows(b->split);
		free(b->split);
	}
	wh_free_node(b);
}

void wh_make_split(node *n) {
	n->hwnd = wh_make_view(0);
	node *b = wh_node_new(wh_host_id(), KIND_SIDEBAR);
	b->hwnd = wh_make_view(WS_TABSTOP);
	b->split = wh_alloc(sizeof *b->split);
	b->split->split = n;
	b->split->selected = b->split->hot = -1;
	b->k = n->k, b->dir = n->dir;
	b->host_msg = pane_message;
	SetParent(b->hwnd, n->hwnd);
	wh_node_add(b);
	n->pane_node = b;
}

static void decode_pane(wh_split *s, const uint8_t *b, size_t len) {
	free_rows(s);
	ak_sidebar_heading_vec secs = {0};
	ak_sidebar_item_vec items = {0};
	if (!ak_decode_sidebar(b, len, &secs, &items)) {
		ak_vec_free(&secs);
		ak_vec_free(&items);
		return;
	}
	s->rows = wh_alloc((secs.len + items.len + 1) * sizeof *s->rows);
	for (uint32_t i = 0; i < secs.len; i++) {
		const ak_sidebar_heading *sec = &secs.data[i];
		if (sec->title.len) {
			pane_row *r = &s->rows[s->nrows++];
			wstr_set(&r->title, sec->title);
			r->header = true;
		}
		for (uint32_t j = 0; j < sec->count; j++) {
			const ak_sidebar_item *it = &items.data[sec->first + j];
			pane_row *r = &s->rows[s->nrows++];
			wstr_set(&r->title, it->title);
			wstr_set(&r->badge, it->badge);
			r->icon = it->icon.len ? wh_strndup(it->icon.p, it->icon.len) : NULL;
			r->index = (int)(sec->first + j);
		}
	}
	ak_vec_free(&secs);
	ak_vec_free(&items);
}

void wh_set_split_prop(node *n, uint16_t p, const ak_value *v) {
	node *b = n->pane_node;
	if (!b) return;
	wh_split *s = b->split;
	switch (p) {
	case AK_PROP_SPLIT_ITEMS: decode_pane(s, (const uint8_t *)v->s.p, v->s.len); break;
	case AK_PROP_SPLIT_SELECTED: s->selected = (int)v->i; break;
	case AK_PROP_SPLIT_TITLE: wstr_set(&n->title, v->s); break;
	}
	InvalidateRect(b->hwnd, NULL, FALSE);
}

// wh_layout_split puts the pane at the leading edge and the detail beside
// it.
void wh_layout_split(node *n) {
	node *b = n->pane_node;
	if (!b || !n->hwnd || !b->hwnd) return;
	RECT r;
	GetClientRect(n->hwnd, &r);
	int pw = wh_mini(wh_px(n, PANE_WIDTH), r.right);
	int px = 0, dx = pw;
	if (wh_is_rtl(n)) px = r.right - pw, dx = 0;
	SetWindowPos(b->hwnd, NULL, px, 0, pw, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	for (uint32_t i = 0; i < n->nkids; i++) {
		node *c = n->kids[i];
		if (!c->hwnd) continue;
		SetWindowPos(c->hwnd, NULL, dx, 0, r.right - pw, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
		wh_resized(c);
	}
}

// pane_rects are the rows' places, in pixels: items 40 high, section
// headers 36, under the title's 48.
static void pane_rects(node *b, RECT *out) {
	wh_split *s = b->split;
	RECT r;
	GetClientRect(b->hwnd, &r);
	int y = s->split->title.n ? wh_px(b, 48) : wh_px(b, 8);
	for (uint32_t i = 0; i < s->nrows; i++) {
		int ht = wh_px(b, s->rows[i].header ? 36 : 40);
		out[i] = (RECT){wh_px(b, 4), y, r.right - wh_px(b, 4), y + ht};
		y += ht + wh_px(b, 2);
	}
}

static int destinations(wh_split *s) {
	int n = 0;
	for (uint32_t i = 0; i < s->nrows; i++)
		if (!s->rows[i].header) n++;
	return n;
}

static void choose(node *b, int i) {
	wh_split *s = b->split;
	if (i < 0 || i >= destinations(s) || i == s->selected) return;
	s->selected = i;
	InvalidateRect(b->hwnd, NULL, FALSE);
	wh_emit_int(s->split, AK_EVENT_SELECTED, i);
}

static int row_at(node *b, LPARAM lp) {
	wh_split *s = b->split;
	if (!s->nrows) return -1;
	RECT *rects = wh_alloc(s->nrows * sizeof *rects);
	pane_rects(b, rects);
	int y = GET_Y_LPARAM(lp), got = -1;
	for (uint32_t i = 0; i < s->nrows; i++)
		if (y >= rects[i].top && y < rects[i].bottom && !s->rows[i].header) {
			got = (int)i;
			break;
		}
	free(rects);
	return got;
}

static LRESULT pane_message(node *b, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	wh_split *s = b->split;
	*handled = true;
	switch (m) {
	case WM_MOUSEMOVE: {
		int i = row_at(b, lp);
		if (i != s->hot) {
			s->hot = i;
			TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, b->hwnd, 0};
			TrackMouseEvent(&t);
			InvalidateRect(b->hwnd, NULL, FALSE);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		s->hot = -1;
		InvalidateRect(b->hwnd, NULL, FALSE);
		return 0;
	case WM_LBUTTONDOWN: {
		int i = row_at(b, lp);
		if (i >= 0) choose(b, s->rows[i].index);
		return 0;
	}
	case WM_GETDLGCODE:
		return DLGC_WANTARROWS;
	case WM_KEYDOWN:
		if (wp == VK_UP || wp == VK_DOWN) {
			choose(b, s->selected + (wp == VK_UP ? -1 : 1));
			wh_announce_choice(b);
			return 0;
		}
		break;
	}
	*handled = false;
	return 0;
}

static RECT flip_in(RECT r, RECT outer) {
	LONG l = outer.left + outer.right - r.right, rr = outer.left + outer.right - r.left;
	r.left = l, r.right = rr;
	return r;
}

// pane_badge is the place of an item's badge: at its trailing end, as
// WinUI's NavigationViewItem puts an InfoBadge.
static RECT pane_badge(node *b, const pane_row *row, RECT tr) {
	bool rtl = wh_is_rtl(b);
	int end = rtl ? tr.left + wh_px(b, 12) : tr.right - wh_px(b, 12);
	return wh_badge_at(b, &row->badge, end, tr.top, tr.bottom, rtl);
}

// wh_paint_pane draws the rows: the hovered and the selected on a subtle
// fill, the selected with an accent pill at its leading edge (g); the
// title, the headers and the items' icons and titles (dc).
void wh_paint_pane(node *b, HDC dc, gfx g, RECT r) {
	wh_split *s = b->split;
	if (!s) return;
	float k = wh_scale_of(b);
	const palette *p = &H.theme;
	RECT *rects = wh_alloc((s->nrows + 1) * sizeof *rects);
	pane_rects(b, rects);
	bool rtl = wh_is_rtl(b);
	int rw = r.right - r.left, rh = r.bottom - r.top;
	if (g) {
		float line = wh_maxf(k, 1);
		g_fill_rect(g, rgba_argb(p->separator), rtl ? 0 : (float)rw - line, 0, line, (float)rh);
		for (uint32_t i = 0; i < s->nrows; i++) {
			const pane_row *row = &s->rows[i];
			if (row->header) continue;
			RECT tr = rects[i];
			bool sel = row->index == s->selected;
			if (sel || (int)i == s->hot)
				g_fill_round_rect(g, rgba_argb(sel ? p->subtle_pressed : p->subtle_hot), (float)tr.left, (float)tr.top, (float)(tr.right - tr.left),
					(float)(tr.bottom - tr.top), 4 * k);
			if (sel) {
				float pw = 3 * k, ph = 16 * k;
				float x = rtl ? (float)tr.right - pw : (float)tr.left;
				g_fill_round_rect(g, rgba_argb(p->accent), x, (float)(tr.top + tr.bottom) / 2 - ph / 2, pw, ph, pw / 2);
			}
			if (row->badge.n) wh_paint_badge(b, NULL, g, pane_badge(b, row, tr), &row->badge);
		}
		free(rects);
		return;
	}
	SetBkMode(dc, TRANSPARENT);
	const wstr *title = &s->split->title;
	if (title->n) {
		font_spec fs = wh_spec(0, AK_FONT_WEIGHT_SEMIBOLD, 14, NULL);
		fs.k = k;
		HGDIOBJ old = SelectObject(dc, wh_font(fs));
		SetTextColor(dc, rgba_ref(rgba_over(p->label, p->bg)));
		RECT tr = {wh_px(b, 16), 0, rw - wh_px(b, 16), wh_px(b, 48)};
		if (rtl) tr = flip_in(tr, r);
		DrawTextW(dc, wstr_c(title), title->n, &tr, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | wh_start_side(rtl));
		SelectObject(dc, old);
	}
	HFONT body = wh_body_font(b);
	for (uint32_t i = 0; i < s->nrows; i++) {
		const pane_row *row = &s->rows[i];
		RECT tr = rects[i];
		int x = tr.left + wh_px(b, 12);
		if (row->header) {
			font_spec fs = wh_spec(0, AK_FONT_WEIGHT_SEMIBOLD, 12, NULL);
			fs.k = k;
			HGDIOBJ old = SelectObject(dc, wh_font(fs));
			SetTextColor(dc, rgba_ref(rgba_over(p->secondary, p->bg)));
			RECT lr = {x, tr.top, tr.right, tr.bottom};
			if (rtl) lr = flip_in(lr, tr);
			DrawTextW(dc, wstr_c(&row->title), row->title.n, &lr, DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | wh_start_side(rtl));
			SelectObject(dc, old);
			continue;
		}
		SetTextColor(dc, rgba_ref(rgba_over(p->label, p->bg)));
		wchar_t glyph = row->icon ? wh_directed_glyph(row->icon, rtl) : 0;
		if (glyph) {
			HGDIOBJ old = SelectObject(dc, wh_icon_font(b, 16 * wh_font_scale()));
			RECT gr = {x, tr.top, x + wh_px(b, 16), tr.bottom};
			if (rtl) gr = flip_in(gr, tr);
			DrawTextW(dc, &glyph, 1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(dc, old);
		}
		x += wh_px(b, 16 + 12);
		HGDIOBJ old = SelectObject(dc, body);
		int end = tr.right - wh_px(b, 8);
		int bw, bh;
		wh_badge_size(b, &row->badge, &bw, &bh);
		if (bw > 0) end -= bw + wh_px(b, 8);
		RECT lr = {x, tr.top, end, tr.bottom};
		if (rtl) lr = flip_in(lr, tr);
		DrawTextW(dc, wstr_c(&row->title), row->title.n, &lr, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | wh_start_side(rtl));
		SelectObject(dc, old);
		if (row->badge.n) wh_paint_badge(b, dc, NULL, pane_badge(b, row, tr), &row->badge);
	}
	free(rects);
}

// chosen is the tab or item a tab bar or sidebar has chosen: its title and
// badge.
static bool chosen(node *b, const wstr **title, const wstr **badge) {
	if (b->kind == KIND_TAB_BAR && b->nav && b->nav->tabs) {
		node *tabs = b->nav->tabs;
		if (tabs->selected >= 0 && (uint32_t)tabs->selected < tabs->nkids) {
			node *s = tabs->kids[tabs->selected];
			*title = &s->title, *badge = &s->badge;
			return true;
		}
	} else if (b->kind == KIND_SIDEBAR && b->split) {
		wh_split *s = b->split;
		for (uint32_t i = 0; i < s->nrows; i++)
			if (!s->rows[i].header && s->rows[i].index == s->selected) {
				*title = &s->rows[i].title, *badge = &s->rows[i].badge;
				return true;
			}
	}
	return false;
}

// wh_announce_choice says the tab or item the keyboard chose: its title,
// and its badge ("Inbox, 3 items").
void wh_announce_choice(node *b) {
	const wstr *title, *badge;
	if (!chosen(b, &title, &badge) || !title->n) return;
	int tl;
	char *t = wh_utf8(wstr_c(title), title->n, &tl);
	char said[1024], words[256] = "";
	if (badge->n) {
		int bl;
		char *bs = wh_utf8(wstr_c(badge), badge->n, &bl);
		int32_t count;
		if (ak_badge_number(ak_strn(bs, (size_t)bl), &count)) {
			char num[16];
			snprintf(num, sizeof num, "%d", (int)count);
			const char *vars[] = {"count", num};
			dsk_word_format(words, sizeof words, H.locale[0] ? H.locale : H.env.locale, "badgeCount", vars, 2);
		} else {
			snprintf(words, sizeof words, "%.*s", bl, bs);
		}
		free(bs);
	}
	if (words[0]) snprintf(said, sizeof said, "%.*s, %s", tl, t, words);
	else snprintf(said, sizeof said, "%.*s", tl, t);
	free(t);
	wh_announce(said, strlen(said));
}

// wh_pane_tap is automation's tap on a sidebar's item showing.
bool wh_pane_tap(const wchar_t *t) {
	uint32_t i = 0;
	void *v;
	while (wh_map_next(&H.nodes, &i, NULL, &v)) {
		node *b = v;
		if (b->kind != KIND_SIDEBAR || !b->split || !b->hwnd || !IsWindowVisible(b->hwnd)) continue;
		wh_split *s = b->split;
		for (uint32_t k = 0; k < s->nrows; k++)
			if (!s->rows[k].header && s->rows[k].title.n && !wcscmp(wstr_c(&s->rows[k].title), t)) {
				choose(b, s->rows[k].index);
				return true;
			}
	}
	return false;
}
