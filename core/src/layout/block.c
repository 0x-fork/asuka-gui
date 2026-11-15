// block.c: CSS 2's block formatting context without floats or inline
// content. Children stack vertically; auto widths fill the container;
// vertical margins collapse between siblings, through empty boxes, and
// with the container's own edges.
#include "layout/internal.h"

typedef struct {
	ak_lnode *node;
	ak_ledges margin;
	l_bedges auto_m;
	ak_lsize size;
	ak_lsize min, max;
	ak_lsize pb;
} block_item;

static l_output block(ak_lnode *n, const l_input *in, ak_lpass *p) {
	const ak_lstyle *st = &n->style;
	ak_ledges pad, bor;
	l_padding_border(n, in->parent.w, &pad, &bor);
	ak_lsize gutter = l_scroll_gutter(n);
	ak_ledges inset = l_content_inset(n, pad, bor, gutter);
	ak_lsize pbs = {pad.left + pad.right + bor.left + bor.right, pad.top + pad.bottom + bor.top + bor.bottom};
	float inset_w = inset.left + inset.right, inset_h = inset.top + inset.bottom;

	ak_lsize min = l_style_min(n, in->parent), max = l_style_max(n, in->parent);
	ak_lsize size = in->known;
	if (in->sizing == SIZE_INHERENT) {
		ak_lsize ss = l_style_size(n, in->parent);
		ss.w = l_clampn(ss.w, min.w, max.w);
		ss.h = l_clampn(ss.h, min.h, max.h);
		for (int a = 0; a < 2; a++) {
			if (l_isnan(l_get(size, a))) {
				float lo = l_get(min, a), hi = l_get(max, a);
				if (l_isdef(lo) && l_isdef(hi) && hi <= lo) l_set(&size, a, lo);
				else l_set(&size, a, l_get(ss, a));
			}
		}
	}
	if (l_isdef(size.w)) size.w = l_maxf(size.w, pbs.w);
	if (l_isdef(size.h)) size.h = l_maxf(size.h, pbs.h);
	if (in->mode == MODE_SIZE && l_isdef(size.w) && l_isdef(size.h)) return (l_output){.size = size, .baseline = LNAN};
	bool new_bfc = l_is_scroll_container(n) || st->align_content != AK_LCONTENT_NORMAL;
	bool adjoin_top = in->collapse[0] && !new_bfc && st->position != AK_LABSOLUTE && pad.top == 0 && bor.top == 0;
	bool adjoin_bottom = in->collapse[1] && !new_bfc && st->position != AK_LABSOLUTE && pad.bottom == 0 && bor.bottom == 0;
	bool collapse_bottom = adjoin_bottom && l_isnan(size.h);
	bool blocks_through = st->display != AK_LDISPLAY_BLOCK || !in->collapse[0] || new_bfc || st->position == AK_LABSOLUTE ||
		pad.top > 0 || pad.bottom > 0 || bor.top > 0 || bor.bottom > 0;

	ak_lsize content_size = {LNAN, LNAN};
	if (l_isdef(size.w)) content_size.w = l_maxf(0, size.w - inset_w);
	if (l_isdef(size.h)) content_size.h = l_maxf(0, size.h - inset_h);
	block_item *items = L_SCRATCH(p, block_item, n->nchildren);
	uint32_t nitems = 0;
	for (uint32_t i = 0; i < n->nchildren; i++) {
		ak_lnode *ch = n->children[i];
		const ak_lstyle *cs = &ch->style;
		if (cs->display == AK_LDISPLAY_NONE) {
			if (in->mode == MODE_LAYOUT) {
				l_place(p, ch, 0, 0, (ak_lsize){0, 0});
				l_hide(ch, p);
			}
			continue;
		}
		if (cs->position == AK_LABSOLUTE) continue;
		block_item it = {.node = ch};
		it.size = l_style_size(ch, content_size);
		it.min = l_style_min(ch, content_size);
		it.max = l_style_max(ch, content_size);
		it.margin = l_margins_of(ch, content_size.w);
		it.auto_m = l_auto_margins(cs);
		ak_ledges cpad, cbor;
		l_padding_border(ch, content_size.w, &cpad, &cbor);
		it.pb = (ak_lsize){cpad.left + cpad.right + cbor.left + cbor.right, cpad.top + cpad.bottom + cbor.top + cbor.bottom};
		items[nitems++] = it;
	}

	// The container's width.
	if (l_isnan(size.w)) {
		float avail_w = in->avail.w;
		if (l_definite(avail_w)) avail_w -= inset_w;
		float w = 0;
		for (uint32_t i = 0; i < nitems; i++) {
			block_item *it = &items[i];
			ak_lsize known = {l_clampn(it->size.w, it->min.w, it->max.w), l_clampn(it->size.h, it->min.h, it->max.h)};
			float mw = it->margin.left + it->margin.right;
			float cw = known.w;
			if (l_isnan(cw)) {
				float a = avail_w;
				if (l_definite(a)) a -= mw;
				cw = l_measure_size(it->node, (l_input){.known = known, .parent = {LNAN, LNAN}, .avail = {a, AK_LMIN_CONTENT}, .collapse = {true, true}}, p).w;
			}
			w = l_maxf(w, l_maxf(cw, it->pb.w) + mw);
		}
		size.w = l_maxf(l_clampn(w + inset_w, min.w, max.w), pbs.w);
		content_size.w = l_maxf(0, size.w - inset_w);
	}
	if (in->mode == MODE_SIZE && l_isdef(size.h)) return (l_output){.size = size, .baseline = LNAN};
	float inner_w = content_size.w;
	float percent_h = content_size.h;
	if (l_isnan(percent_h) && in->sizing == SIZE_INHERENT) {
		float h = l_style_size(n, in->parent).h;
		if (l_isdef(h)) percent_h = l_maybe_max(h, min.h) - inset_h;
	}
	ak_lsize parent = {inner_w, percent_h};

	// The content box hangs from the inline start's edge, which shows when
	// padding and scrollbars overrun the box.
	float content_x = inset.left;
	if (n->rtl) content_x = size.w - inset.right - inner_w;

	// Lay out the in-flow children.
	bool final = in->mode == MODE_LAYOUT;
	float y = inset.top;
	l_margins first_top = {0, 0}, active = {0, 0};
	bool collapsing_first = true;
	bool all_through = true;
	float first_baseline = LNAN;
	float abs_y = inset.top;
	uint32_t ii = 0;
	for (uint32_t i = 0; i < n->nchildren; i++) {
		ak_lnode *ch = n->children[i];
		if (ch->style.position == AK_LABSOLUTE) {
			ch->static_y = abs_y;
			continue;
		}
		if (ch->style.display == AK_LDISPLAY_NONE) continue;
		block_item *it = &items[ii++];
		float mx = it->margin.left + it->margin.right;
		ak_lsize known = {it->size.w, LNAN};
		if (l_isnan(known.w)) known.w = l_clampn(inner_w - mx, it->min.w, it->max.w);
		l_input cin = {.known = known, .parent = parent, .avail = {inner_w - mx, AK_LMIN_CONTENT}, .collapse = {true, true}};
		if (final) cin.mode = MODE_LAYOUT;
		l_output out = l_compute(ch, cin, p);
		l_margins top = l_margins_add(out.top, it->margin.top);
		l_margins bottom = l_margins_add(out.bottom, it->margin.bottom);
		float free_x = l_maxf(0, inner_w - out.size.w - mx);
		float left = it->margin.left;
		if (it->auto_m.left && it->auto_m.right) left += free_x / 2;
		else if (it->auto_m.left) left += free_x;
		if (n->rtl) {  // the same from the inline start: margin-left gives way
			float right = it->margin.right;
			if (it->auto_m.left && it->auto_m.right) right += free_x / 2;
			else if (it->auto_m.right) right += free_x;
			left = inner_w - out.size.w - right;
		}
		float y_off = l_margins_value(l_margins_join(active, top));
		if (collapsing_first && adjoin_top) y_off = 0;
		if (final) {
			float px = content_x + left, py = y + y_off;
			l_relative_offset(ch, &px, &py, parent);
			l_place(p, ch, px, py, out.size);
			if (l_isnan(first_baseline) && l_isdef(out.baseline)) first_baseline = py + out.baseline;
		}
		if (collapsing_first) {
			if (out.collapses_through) {
				first_top = l_margins_join(l_margins_join(first_top, top), bottom);
			} else {
				first_top = l_margins_join(first_top, top);
				collapsing_first = false;
			}
		}
		if (out.collapses_through) {
			active = l_margins_join(l_margins_join(active, top), bottom);
			abs_y = y + out.size.h + y_off;
		} else {
			all_through = false;
			y += out.size.h + y_off;
			active = bottom;
			abs_y = y + l_margins_value(active);
		}
	}
	l_margins last_bottom = active;
	float bottom_off = l_margins_value(last_bottom);
	if (collapse_bottom) bottom_off = 0;
	float content_h = l_maxf(0, y + inset.bottom + bottom_off);
	if (l_isnan(size.h)) size.h = l_maxf(l_clampn(content_h, min.h, max.h), pbs.h);
	if (l_isdef(min.h) && min.h > 0 && min.h >= size.h) collapse_bottom = false;
	l_output out = {.size = size, .baseline = first_baseline};
	if (adjoin_top) out.top = first_top;
	if (collapse_bottom) out.bottom = last_bottom;
	out.collapses_through = !blocks_through && all_through && size.h == 0 && (l_isnan(min.h) || min.h <= 0);
	if (!final) return out;

	// Absolutely positioned children, from their static position.
	float cw = size.w - inset_w;
	float ch2 = size.h - inset_h;
	float ax = bor.left, fx = 0;
	if (n->rtl) {
		ax = bor.left + gutter.w;
		fx = 1;
	}
	for (uint32_t i = 0; i < n->nchildren; i++) {
		ak_lnode *c = n->children[i];
		if (c->style.position != AK_LABSOLUTE || c->style.display == AK_LDISPLAY_NONE) continue;
		float frac[2] = {fx, 0};
		l_place_absolute_aligned(p, c, ax, bor.top, size.w - bor.left - bor.right - gutter.w, size.h - bor.top - bor.bottom - gutter.h,
			inset.left, c->static_y, cw, ch2, frac);
	}
	return out;
}

l_output l_block(ak_lnode *n, const l_input *in, ak_lpass *p) {
	l_mark m = l_scratch_mark(p);
	l_output out = block(n, in, p);
	l_scratch_release(p, m);
	return out;
}
