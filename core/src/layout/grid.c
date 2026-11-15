// grid.c: grid, following CSS Grid Layout: placement (§8) and the track
// sizing algorithm (§12). Not supported: named lines and areas, subgrid,
// masonry, baseline alignment of grid items.
#include "layout/internal.h"

typedef struct {
	ak_lbreadth min_b, max_b;
	float base;
	float limit;
	float planned;
	float offset;
	bool collapsed;  // an empty auto-fit track
	bool auto_fit;
} track;

static bool is_flex(const track *t) { return t->max_b.kind == AK_LBREADTH_FR; }

static bool min_intrinsic(const track *t) {
	switch (t->min_b.kind) {
	case AK_LBREADTH_AUTO:
	case AK_LBREADTH_MIN_CONTENT:
	case AK_LBREADTH_MAX_CONTENT: return true;
	}
	return false;
}

static bool max_intrinsic(const track *t) {
	switch (t->max_b.kind) {
	case AK_LBREADTH_AUTO:
	case AK_LBREADTH_MIN_CONTENT:
	case AK_LBREADTH_MAX_CONTENT:
	case AK_LBREADTH_FIT_CONTENT: return true;
	}
	return false;
}

#define AUTO_SPAN (-(1 << 30))

typedef struct {
	ak_lnode *node;
	int span[2][2];  // per axis: [start, end) track indices
	ak_ledges margin;
	l_bedges auto_m;
	ak_lsize size;
	ak_lsize min, max;
	ak_lsize pb;
	uint8_t align[2];  // justify-self, align-self
	bool flex[2];      // crosses a flexible track
	float min_c[2];
	float max_c[2];
	float minimum[2];  // minimum contribution
	bool placed;
} grid_item;

static int span_len(const grid_item *it, int a) { return it->span[a][1] - it->span[a][0]; }

// Both axes' tracks.
typedef struct {
	track *t[2];
	int n[2];
} tracks2;

typedef struct {
	int r0, r1, c0, c1;
} area;

static bool is_fixed(ak_lbreadth b, float basis) {
	return b.kind == AK_LBREADTH_POINT || (b.kind == AK_LBREADTH_PERCENT && l_isdef(basis));
}

static float resolve_breadth(ak_lbreadth b, float basis) {
	switch (b.kind) {
	case AK_LBREADTH_POINT: return b.v;
	case AK_LBREADTH_PERCENT:
	case AK_LBREADTH_FIT_CONTENT_PERCENT: return b.v * basis / 100;
	case AK_LBREADTH_FIT_CONTENT: return b.v;
	}
	return 0;
}

static float fixed_breadth(ak_ltrack_size s, float basis) {
	if (is_fixed(s.max, basis)) {
		float v = resolve_breadth(s.max, basis);
		if (is_fixed(s.min, basis)) v = l_maxf(v, resolve_breadth(s.min, basis));
		return v;
	}
	if (is_fixed(s.min, basis)) return resolve_breadth(s.min, basis);
	return 0;
}

// Browsers limit grids to lines within ±MAX_LINES.
#define MAX_LINES 10000

// expand_count returns how many explicit tracks a template makes, given
// the inner size (NaN when indefinite) for auto-repeat, and the
// auto-repeat's repetitions in *reps.
static int expand_count(const ak_lgrid_track *tpl, uint32_t ntpl, float inner, float gap, int *reps) {
	float fixed = 0;
	int n = 0;
	const ak_lgrid_track *autot = NULL;
	for (uint32_t i = 0; i < ntpl; i++) {
		const ak_lgrid_track *t = &tpl[i];
		if (t->count < 0) {
			autot = t;
			continue;
		}
		int c = l_imax(t->count, 1);
		for (int k = 0; k < c; k++) {
			for (uint32_t j = 0; j < t->nsizes; j++) {
				fixed += fixed_breadth(t->sizes[j], inner);
				n++;
			}
		}
	}
	if (!autot) {
		*reps = 0;
		return n;
	}
	int r = 1;
	if (l_isdef(inner) && autot->nsizes > 0) {
		float per = 0;
		bool ok = true;
		for (uint32_t j = 0; j < autot->nsizes; j++) {
			ak_ltrack_size s = autot->sizes[j];
			float v = fixed_breadth(s, inner);
			if (v <= 0 && !is_fixed(s.max, inner) && !is_fixed(s.min, inner)) ok = false;
			per += v;
		}
		if (ok) {
			per += gap * (float)autot->nsizes;
			float free = inner - fixed - gap * (float)n;
			if (n == 0) free += gap;
			if (per > 0) {
				// Clamped before converting, which C leaves undefined out
				// of int's range: no grid has more than MAX_LINES lines,
				// and any negative count acts as -1 does.
				float qf = (free + 0.0001f) / per;
				int q = qf != qf ? 0 : qf >= MAX_LINES ? MAX_LINES : qf <= -1 ? -1 : (int)qf;
				if (n == 0 && q == 0) q = 1;
				r = l_imax(1, q);
			}
		}
	}
	*reps = r;
	return n + r * (int)autot->nsizes;
}

static int clamp_line(int v) { return l_imin(l_imax(v, -MAX_LINES), MAX_LINES); }

// resolve_line turns a grid line into a zero-based line, relative to the
// start of the explicit grid.
static int resolve_line(ak_lgrid_line l, int explicit) {
	int v = explicit + 1 + (int)l.n;
	if (l.n > 0) v = (int)l.n - 1;
	return clamp_line(v);
}

static int span_of(ak_lgrid_line l) {
	if (l.kind == AK_LLINE_SPAN && l.n > 0) return l_imin((int)l.n, MAX_LINES);
	return 1;
}

// placement resolves one axis: (start, end) when definite, or the span.
static bool placement(ak_lplacement pl, int explicit, int *start, int *end, int *span) {
	ak_lgrid_line s = pl.start, e = pl.end;
	if (s.kind == AK_LLINE_INDEX && s.n == 0) s = (ak_lgrid_line){0, 0};
	if (e.kind == AK_LLINE_INDEX && e.n == 0) e = (ak_lgrid_line){0, 0};
	if (s.kind == AK_LLINE_INDEX && e.kind == AK_LLINE_INDEX) {
		int a = resolve_line(s, explicit), b = resolve_line(e, explicit);
		if (b < a) {
			int t = a;
			a = b;
			b = t;
		}
		if (a == b) {
			if (a == MAX_LINES) a--;
			b = a + 1;
		}
		*start = a;
		*end = b;
		*span = b - a;
		return true;
	}
	if (s.kind == AK_LLINE_INDEX) {
		int a = l_imin(resolve_line(s, explicit), MAX_LINES - 1);
		*start = a;
		*end = l_imin(a + span_of(e), MAX_LINES);
		*span = span_of(e);
		return true;
	}
	if (e.kind == AK_LLINE_INDEX) {
		int b = l_imax(resolve_line(e, explicit), -MAX_LINES + 1);
		*start = l_imax(b - span_of(s), -MAX_LINES);
		*end = b;
		*span = span_of(s);
		return true;
	}
	*start = 0;
	*end = 0;
	*span = span_of(s);
	return false;
}

static bool fits(const area *occ, int nocc, int cols, int r, int c, int rs, int cs) {
	if (c + cs > cols) return false;
	for (int i = 0; i < nocc; i++) {
		const area *o = &occ[i];
		if (r < o->r1 && o->r0 < r + rs && c < o->c1 && o->c0 < c + cs) return false;
	}
	return true;
}

static bool is_auto(const grid_item *it, int a) { return it->span[a][0] == AUTO_SPAN; }

static float tracks_total(const track *ts, int nts, float gap) {
	float s = 0;
	int visible = 0;
	for (int i = 0; i < nts; i++) {
		if (ts[i].collapsed) continue;
		s += ts[i].base;
		visible++;
	}
	if (visible > 1) s += gap * (float)(visible - 1);
	return s;
}

static ak_lsize grid_item_size(grid_item *it, const float area_size[2], ak_lpass *p);
static void size_tracks(ak_lnode *n, int a, grid_item *items, int nitems, tracks2 *tracks, ak_lsize inner, ak_lsize avail, float gap, ak_lpass *p);

typedef struct {
	int key, value;
} cursor;

static l_output grid(ak_lnode *n, const l_input *in, ak_lpass *p) {
	const ak_lstyle *st = &n->style;
	ak_ledges pad, bor;
	l_padding_border(n, in->parent.w, &pad, &bor);
	ak_lsize gutter = l_scroll_gutter(n);
	ak_ledges inset = l_content_inset(n, pad, bor, gutter);
	ak_lsize pbs = l_sum_both(inset);
	ak_ledges margin = l_margins_of(n, in->parent.w);

	ak_lsize size = in->known;
	ak_lsize min_s = {LNAN, LNAN}, max_s = {LNAN, LNAN};
	if (in->sizing == SIZE_INHERENT) {
		ak_lsize ss = l_style_size(n, in->parent);
		size = (ak_lsize){l_orf(size.w, ss.w), l_orf(size.h, ss.h)};
		min_s = l_style_min(n, in->parent);
		max_s = l_style_max(n, in->parent);
	}
	size.w = l_clampn(size.w, min_s.w, max_s.w);
	size.h = l_clampn(size.h, min_s.h, max_s.h);
	if (in->mode == MODE_SIZE && l_isdef(size.w) && l_isdef(size.h)) {
		return (l_output){.size = {l_maxf(size.w, pbs.w), l_maxf(size.h, pbs.h)}, .baseline = LNAN};
	}
	ak_lsize inner = {LNAN, LNAN};
	ak_lsize avail;
	for (int a = 0; a < 2; a++) {
		float s = l_get(size, a);
		if (l_isdef(s)) l_set(&inner, a, l_maxf(0, s - l_get(pbs, a)));
		float v = l_get(in->avail, a);
		if (l_isdef(l_get(inner, a))) v = l_get(inner, a);
		else if (l_definite(v)) v = l_maxf(0, l_clampn(v - l_sum_axis(margin, a), l_get(min_s, a), l_get(max_s, a)) - l_get(pbs, a));
		l_set(&avail, a, v);
	}
	// For auto-repeat, an indefinite size falls back to max (then min).
	ak_lsize repeat_basis = inner;
	for (int a = 0; a < 2; a++) {
		if (l_isnan(l_get(repeat_basis, a))) {
			float v = l_get(max_s, a);
			if (l_isdef(v)) {
				l_set(&repeat_basis, a, l_maxf(0, v - l_get(pbs, a)));
			} else {
				v = l_get(min_s, a);
				if (l_isdef(v)) l_set(&repeat_basis, a, l_maxf(0, v - l_get(pbs, a)));
			}
		}
	}
	float gaps[2] = {l_or0(l_resolve(st->column_gap, inner.w)), l_or0(l_resolve(st->row_gap, inner.h))};
	const ak_lgrid_track *templates[2] = {st->grid_template_columns, st->grid_template_rows};
	uint32_t ntemplates[2] = {st->ngrid_template_columns, st->ngrid_template_rows};
	const ak_ltrack_size *autos[2] = {st->grid_auto_columns, st->grid_auto_rows};
	uint32_t nautos[2] = {st->ngrid_auto_columns, st->ngrid_auto_rows};
	int explicit[2], reps[2];
	for (int a = 0; a < 2; a++) explicit[a] = expand_count(templates[a], ntemplates[a], l_get(repeat_basis, a), gaps[a], &reps[a]);

	// Items and placement.
	grid_item *items = L_SCRATCH(p, grid_item, n->nchildren);
	int nitems = 0;
	int lo[2] = {0, 0}, hi[2] = {explicit[0], explicit[1]};
	int max_span[2] = {1, 1};
	for (uint32_t ci = 0; ci < n->nchildren; ci++) {
		ak_lnode *ch = n->children[ci];
		const ak_lstyle *cs = &ch->style;
		if (cs->display == AK_LDISPLAY_NONE) {
			if (in->mode == MODE_LAYOUT) {
				l_place(p, ch, 0, 0, (ak_lsize){0, 0});
				l_hide(ch, p);
			}
			continue;
		}
		if (cs->position == AK_LABSOLUTE) continue;
		grid_item it;
		memset(&it, 0, sizeof it);
		it.node = ch;
		ak_lplacement pls[2] = {cs->grid_column, cs->grid_row};
		for (int a = 0; a < 2; a++) {
			int s, e, span;
			if (placement(pls[a], explicit[a], &s, &e, &span)) {
				it.span[a][0] = s;
				it.span[a][1] = e;
				lo[a] = l_imin(lo[a], s);
				hi[a] = l_imax(hi[a], e);
			} else {
				it.span[a][0] = AUTO_SPAN;
				it.span[a][1] = span;
				max_span[a] = l_imax(max_span[a], span);
			}
		}
		items[nitems++] = it;
	}
	bool flow_col = l_flow_is_column(st->grid_auto_flow);
	bool dense = l_flow_is_dense(st->grid_auto_flow);
	int pa = 0, sa = 1;  // the cursor walks the primary axis; the secondary grows
	if (flow_col) {
		pa = 1;
		sa = 0;
	}
	// Items locked in the secondary axis or fully auto also need the
	// primary axis to fit their span.
	hi[pa] = l_imax(hi[pa], lo[pa] + max_span[pa]);
	int off[2] = {-lo[0], -lo[1]};
	for (int i = 0; i < nitems; i++) {
		for (int a = 0; a < 2; a++) {
			if (items[i].span[a][0] != AUTO_SPAN) {
				items[i].span[a][0] += off[a];
				items[i].span[a][1] += off[a];
			}
		}
	}
	int cols = hi[pa] - lo[pa];  // the primary axis's track count is fixed
	// Placed areas, checked for overlap (sparse: grids may be huge).
	area *occ = L_SCRATCH(p, area, nitems);
	int nocc = 0;
#define MARK(it) \
	do { \
		occ[nocc++] = (area){(it)->span[sa][0], (it)->span[sa][1], (it)->span[pa][0], (it)->span[pa][1]}; \
		(it)->placed = true; \
	} while (0)
	// 1. Fully definite items.
	for (int i = 0; i < nitems; i++) {
		if (!is_auto(&items[i], 0) && !is_auto(&items[i], 1)) MARK(&items[i]);
	}
	// 2. Items locked to a secondary track.
	cursor *cursors = L_SCRATCH(p, cursor, nitems);
	int ncursors = 0;
	for (int i = 0; i < nitems; i++) {
		grid_item *it = &items[i];
		if (it->placed || is_auto(it, sa) || !is_auto(it, pa)) continue;
		int span = it->span[pa][1];
		int r = it->span[sa][0];
		int rs = span_len(it, sa);
		int start = 0;
		int cur = -1;
		if (!dense) {
			for (int k = 0; k < ncursors; k++) {
				if (cursors[k].key == r) {
					cur = k;
					break;
				}
			}
			if (cur >= 0) start = cursors[cur].value;
		}
		int c = start;
		while (!fits(occ, nocc, cols, r, c, rs, span)) {
			c++;
			// Doesn't fit: extending the implicit primary axis would be
			// needed; place at the end.
			if (c + span > cols) break;
		}
		it->span[pa][0] = c;
		it->span[pa][1] = c + span;
		if (c + span > cols) {
			it->span[pa][0] = 0;
			it->span[pa][1] = span;
		}
		MARK(it);
		if (!dense) {
			if (cur < 0) {
				cur = ncursors++;
				cursors[cur].key = r;
			}
			cursors[cur].value = it->span[pa][1];
		}
	}
	// 4. The rest.
	int cr = 0, cc = 0;
	for (int i = 0; i < nitems; i++) {
		grid_item *it = &items[i];
		if (it->placed) continue;
		if (dense) {
			cr = 0;
			cc = 0;
		}
		if (!is_auto(it, pa)) {
			int c = it->span[pa][0];
			int span = span_len(it, pa);
			if (!dense && c < cc) cr++;
			int r = cr;
			int rs = it->span[sa][1];
			while (!fits(occ, nocc, cols, r, c, rs, span)) r++;
			it->span[sa][0] = r;
			it->span[sa][1] = r + rs;
			cr = r;
			cc = c;
			MARK(it);
			continue;
		}
		int span = l_imin(it->span[pa][1], cols);
		int rs = it->span[sa][1];
		int r = cr, c = cc;
		for (;;) {
			if (c + span > cols) {
				r++;
				c = 0;
				continue;
			}
			if (fits(occ, nocc, cols, r, c, rs, span)) break;
			c++;
		}
		it->span[pa][0] = c;
		it->span[pa][1] = c + span;
		it->span[sa][0] = r;
		it->span[sa][1] = r + rs;
		MARK(it);
		cr = r;
		cc = c + span;
	}
#undef MARK
	int counts[2];
	counts[pa] = cols;
	counts[sa] = hi[sa] - lo[sa];
	for (int i = 0; i < nitems; i++) counts[sa] = l_imax(counts[sa], items[i].span[sa][1]);

	// Tracks.
	tracks2 tracks;
	for (int a = 0; a < 2; a++) {
		int before = off[a];
		track *ts = L_SCRATCH(p, track, counts[a]);
		memset(ts, 0, sizeof(track) * (size_t)counts[a]);
		int nts = counts[a];
		// Explicit tracks.
		int k = before;
		for (uint32_t ti = 0; ti < ntemplates[a]; ti++) {
			const ak_lgrid_track *t = &templates[a][ti];
			int c = t->count;
			bool auto_fit = c == AK_LREPEAT_AUTO_FIT;
			if (c < 0) c = reps[a];
			c = l_imax(c, 1);
			for (int rep = 0; rep < c; rep++) {
				for (uint32_t j = 0; j < t->nsizes; j++) {
					if (k < nts) {
						ts[k].min_b = t->sizes[j].min;
						ts[k].max_b = t->sizes[j].max;
						ts[k].auto_fit = auto_fit;
					}
					k++;
				}
			}
		}
		// Implicit tracks cycle through grid-auto-rows/columns.
		const ak_ltrack_size *al = autos[a];
		int nal = (int)nautos[a];
		for (int i = 0; i < nts; i++) {
			if (i >= before && i < before + explicit[a]) continue;
			ak_ltrack_size s = ak_lauto_track();
			if (nal > 0) {
				if (i >= before + explicit[a]) {
					s = al[(i - before - explicit[a]) % nal];
				} else {
					int j = (before - i) % nal;
					s = al[(nal - j) % nal];
				}
			}
			ts[i].min_b = s.min;
			ts[i].max_b = s.max;
		}
		tracks.t[a] = ts;
		tracks.n[a] = nts;
	}
	// Collapse empty auto-fit tracks.
	for (int a = 0; a < 2; a++) {
		for (int ti = 0; ti < tracks.n[a]; ti++) {
			track *t = &tracks.t[a][ti];
			if (!t->auto_fit) continue;
			bool used = false;
			for (int i = 0; i < nitems; i++) {
				if (items[i].span[a][0] <= ti && ti < items[i].span[a][1]) {
					used = true;
					break;
				}
			}
			t->collapsed = !used;
		}
	}

	// Item styles.
	for (int i = 0; i < nitems; i++) {
		grid_item *it = &items[i];
		ak_lnode *ch = it->node;
		const ak_lstyle *cs = &ch->style;
		it->size = l_style_size(ch, inner);
		it->min = l_style_min(ch, inner);
		it->max = l_style_max(ch, inner);
		it->margin = l_margins_of(ch, inner.w);
		it->auto_m = l_auto_margins(cs);
		ak_ledges cpad, cbor;
		l_padding_border(ch, inner.w, &cpad, &cbor);
		it->pb = (ak_lsize){cpad.left + cpad.right + cbor.left + cbor.right, cpad.top + cpad.bottom + cbor.top + cbor.bottom};
		it->align[0] = cs->justify_self;
		if (it->align[0] == AK_LALIGN_AUTO) it->align[0] = st->justify_items;
		it->align[0] = l_resolve_self_align(it->align[0], true, n->rtl, l_rtl_under(ch, n->rtl));
		it->align[1] = cs->align_self;
		if (it->align[1] == AK_LALIGN_AUTO) it->align[1] = st->align_items;
		it->align[1] = l_resolve_self_align(it->align[1], false, n->rtl, l_rtl_under(ch, n->rtl));
		for (int a = 0; a < 2; a++) {
			it->min_c[a] = it->max_c[a] = it->minimum[a] = LNAN;
			for (int ti = it->span[a][0]; ti < it->span[a][1]; ti++) {
				if (is_flex(&tracks.t[a][ti])) it->flex[a] = true;
			}
		}
	}

	// Size columns, then rows.
	for (int a = 0; a < 2; a++) size_tracks(n, a, items, nitems, &tracks, inner, avail, gaps[a], p);

	// The container's size.
	ak_lsize content;
	for (int a = 0; a < 2; a++) l_set(&content, a, tracks_total(tracks.t[a], tracks.n[a], gaps[a]));
	ak_lsize out_size = size;
	for (int a = 0; a < 2; a++) {
		if (l_isnan(l_get(out_size, a))) {
			l_set(&out_size, a, l_clampn(l_get(content, a) + l_get(pbs, a), l_get(min_s, a), l_get(max_s, a)));
		}
		l_set(&out_size, a, l_maxf(l_get(out_size, a), l_get(pbs, a)));
	}
	if (in->mode == MODE_SIZE) return (l_output){.size = out_size, .baseline = LNAN};
	ak_lsize inner_final = {out_size.w - pbs.w, out_size.h - pbs.h};
	// Right to left, the columns are placed from the right of the border
	// box, where scrollbars and padding can't push it out.
	float box_w = out_size.w;
	if (l_isdef(size.w)) box_w = l_maxf(size.w, pad.left + pad.right + bor.left + bor.right);

	// Align the tracks (justify-content, align-content). Their offsets are
	// from the start of the axis: on the columns of a right-to-left grid,
	// from the right of the border box, until an item's x is worked out.
	uint8_t contents[2] = {st->justify_content, st->align_content};
	for (int a = 0; a < 2; a++) {
		track *ts = tracks.t[a];
		int nts = tracks.n[a];
		float free = l_get(inner_final, a) - l_get(content, a);
		int visible = 0;
		for (int i = 0; i < nts; i++) {
			if (!ts[i].collapsed) visible++;
		}
		float lead, between;
		l_distribute(contents[a], free, visible, &lead, &between);
		float pos = l_start(inset, a) + lead;
		if (a == 0 && n->rtl) pos = inset.right + lead;
		bool first = true;
		for (int i = 0; i < nts; i++) {
			track *t = &ts[i];
			if (t->collapsed) {
				t->offset = pos;
				continue;
			}
			if (!first) pos += gaps[a] + between;
			first = false;
			t->offset = pos;
			pos += t->base;
		}
	}

	// Lay out the items in their areas.
	float baseline = LNAN;
	for (int i = 0; i < nitems; i++) {
		grid_item *it = &items[i];
		float area_pos[2], area_size[2];
		for (int a = 0; a < 2; a++) {
			const track *ts = tracks.t[a];
			int s = it->span[a][0], e = it->span[a][1] - 1;
			area_pos[a] = ts[s].offset;
			area_size[a] = ts[e].offset + ts[e].base - ts[s].offset;
		}
		ak_lsize known = {LNAN, LNAN};
		float pos[2];
		ak_lsize final = grid_item_size(it, area_size, p);
		for (int a = 0; a < 2; a++) {
			float sz = l_get(final, a);
			float free = area_size[a] - sz - l_sum_axis(it->margin, a);
			float o = l_start(it->margin, a);
			bool as = l_bstart(it->auto_m, a), ae = l_bend(it->auto_m, a);
			if (a == 0 && n->rtl) {
				o = it->margin.right;
				bool t = as;
				as = ae;
				ae = t;
			}
			if (as && ae) {
				o += free / 2;
			} else if (as) {
				o += free;
			} else if (ae) {
			} else {
				switch (it->align[a]) {
				case AK_LALIGN_CENTER: o += free / 2; break;
				case AK_LALIGN_END:
				case AK_LALIGN_FLEX_END: o += free; break;
				}
			}
			pos[a] = area_pos[a] + o;
			l_set(&known, a, sz);
		}
		if (n->rtl) pos[0] = box_w - pos[0] - final.w;
		ak_lsize asz = {area_size[0], area_size[1]};
		l_output out = l_compute(it->node, (l_input){.mode = MODE_LAYOUT, .known = known, .parent = asz, .avail = asz}, p);
		float px = pos[0], py = pos[1];
		l_relative_offset(it->node, &px, &py, asz);
		l_place(p, it->node, px, py, out.size);
		if (l_isnan(baseline) && it->span[1][0] == 0) {
			float bl = out.baseline;
			if (l_isnan(bl)) bl = out.size.h;
			baseline = py + bl;
		}
	}

	// Absolutely positioned children: their grid area, or the padding box.
	for (uint32_t ci = 0; ci < n->nchildren; ci++) {
		ak_lnode *ch = n->children[ci];
		const ak_lstyle *cs = &ch->style;
		if (cs->position != AK_LABSOLUTE || cs->display == AK_LDISPLAY_NONE) continue;
		float ax = bor.left, ay = bor.top;
		if (n->rtl) ax = bor.right;  // on the columns' offsets, from the right
		float aw = out_size.w - bor.left - bor.right - gutter.w, ah = out_size.h - bor.top - bor.bottom - gutter.h;
		float ar[2][2] = {{ax, ax + aw}, {ay, ay + ah}};
		ak_lplacement pls[2] = {cs->grid_column, cs->grid_row};
		for (int a = 0; a < 2; a++) {
			const track *ts = tracks.t[a];
			int nts = tracks.n[a];
			int ks = 0, ke = 0;
			bool has_start = pls[a].start.kind == AK_LLINE_INDEX && pls[a].start.n != 0;
			bool has_end = pls[a].end.kind == AK_LLINE_INDEX && pls[a].end.n != 0;
			if (has_start) ks = resolve_line(pls[a].start, explicit[a]) + off[a];
			if (has_end) ke = resolve_line(pls[a].end, explicit[a]) + off[a];
			// Two lines the wrong way round swap, and the same line twice is
			// one track (CSS Grid §8.3.1), for an absolute item too.
			if (has_start && has_end) {
				if (ks > ke) {
					int t = ks;
					ks = ke;
					ke = t;
				} else if (ks == ke) {
					ke = ks + 1;
				}
			}
			// A line the grid hasn't is the padding edge, as auto is. The end
			// of a track is this side of the gap after it.
			if (has_start && ks >= 0 && ks < nts) ar[a][0] = ts[ks].offset;
			else if (has_start && ks == nts && ks > 0) ar[a][0] = ts[ks - 1].offset + ts[ks - 1].base;
			if (has_end && ke > 0 && ke <= nts) ar[a][1] = ts[ke - 1].offset + ts[ke - 1].base;
			else if (has_end && ke == 0 && nts > 0) ar[a][1] = ts[0].offset;
		}
		// A line past the end: no size.
		ar[0][1] = l_maxf(ar[0][1], ar[0][0]);
		ar[1][1] = l_maxf(ar[1][1], ar[1][0]);
		float left = ar[0][0];
		if (n->rtl) left = box_w - ar[0][1];
		float frac[2] = {0, 0};
		// A static position on the inline axis honors justify-self.
		uint8_t al = cs->justify_self;
		if (al == AK_LALIGN_AUTO) al = st->justify_items;
		switch (l_resolve_self_align(al, true, n->rtl, l_rtl_under(ch, n->rtl))) {
		case AK_LALIGN_CENTER: frac[0] = 0.5f; break;
		case AK_LALIGN_END:
		case AK_LALIGN_FLEX_END: frac[0] = 1; break;
		}
		if (n->rtl) frac[0] = 1 - frac[0];
		// And on the block axis, align-self.
		al = cs->align_self;
		if (al == AK_LALIGN_AUTO) al = st->align_items;
		switch (l_resolve_self_align(al, false, n->rtl, l_rtl_under(ch, n->rtl))) {
		case AK_LALIGN_CENTER: frac[1] = 0.5f; break;
		case AK_LALIGN_END:
		case AK_LALIGN_FLEX_END: frac[1] = 1; break;
		}
		l_place_absolute_aligned(p, ch, left, ar[1][0], ar[0][1] - ar[0][0], ar[1][1] - ar[1][0],
			left, ar[1][0], ar[0][1] - ar[0][0], ar[1][1] - ar[1][0], frac);
	}
	return (l_output){.size = out_size, .baseline = baseline};
}

l_output l_grid(ak_lnode *n, const l_input *in, ak_lpass *p) {
	l_mark m = l_scratch_mark(p);
	l_output out = grid(n, in, p);
	l_scratch_release(p, m);
	return out;
}

// grid_item_size is an item's border-box size in its area: stretched, or
// fit to its content.
static ak_lsize grid_item_size(grid_item *it, const float area[2], ak_lpass *p) {
	ak_lsize sz = it->size;
	float ratio = it->node->style.aspect_ratio;
	for (int a = 0; a < 2; a++) {
		if (l_isdef(l_get(sz, a))) continue;
		uint8_t al = it->align[a];
		bool stretch = al == AK_LALIGN_STRETCH || (al == AK_LALIGN_AUTO && ratio <= 0);
		if (stretch && !l_bstart(it->auto_m, a) && !l_bend(it->auto_m, a)) {
			l_set(&sz, a, l_clampn(area[a] - l_sum_axis(it->margin, a), l_get(it->min, a), l_get(it->max, a)));
		}
	}
	sz = l_apply_aspect(sz, ratio);
	if (l_isnan(sz.w) || l_isnan(sz.h)) {
		ak_lsize m = l_measure_size(it->node, (l_input){.known = sz, .parent = {area[0], area[1]},
			.avail = {area[0] - l_sum_axis(it->margin, 0), area[1] - l_sum_axis(it->margin, 1)}}, p);
		if (l_isnan(sz.w)) sz.w = l_clampn(m.w, it->min.w, it->max.w);
		if (l_isnan(sz.h)) sz.h = l_clampn(m.h, it->min.h, it->max.h);
	}
	sz.w = l_maxf(sz.w, it->pb.w);
	sz.h = l_maxf(sz.h, it->pb.h);
	return sz;
}

// contribution measures an item's outer size along axis a under the
// given available space (min- or max-content), knowing the other axis's
// track sizes when they are sized.
static float contribution(grid_item *it, int a, float space, const tracks2 *tracks, ak_lsize inner, const float gaps[2], ak_lpass *p) {
	int o = 1 - a;
	ak_lsize known = {LNAN, LNAN};
	float v = l_get(it->size, a);
	if (l_isdef(v)) return l_maxf(l_clampn(v, l_get(it->min, a), l_get(it->max, a)), l_get(it->pb, a)) + l_sum_axis(it->margin, a);
	ak_lsize ca = {0, 0};
	l_set(&ca, a, space);
	if (a == 1) {  // columns are sized: the item's width follows from its area
		float other = 0;
		for (int ti = it->span[0][0]; ti < it->span[0][1]; ti++) other += tracks->t[0][ti].base;
		other += gaps[0] * (float)(span_len(it, 0) - 1);
		float w = it->size.w;
		if (l_isnan(w) && it->align[0] != AK_LALIGN_START && it->align[0] != AK_LALIGN_END && it->align[0] != AK_LALIGN_CENTER &&
			!it->auto_m.left && !it->auto_m.right) {
			w = l_clampn(other - l_sum_axis(it->margin, 0), it->min.w, it->max.w);
		}
		known.w = w;
		ca.w = l_maxf(0, other - l_sum_axis(it->margin, 0));
	} else {
		l_set(&known, o, l_get(it->size, o));
		l_set(&ca, o, AK_LMAX_CONTENT);
		if (l_isdef(l_get(inner, o))) l_set(&ca, o, l_get(inner, o));
	}
	known = l_apply_aspect(known, it->node->style.aspect_ratio);
	v = l_get(l_measure_size(it->node, (l_input){.known = known, .parent = inner, .avail = ca}, p), a);
	v = l_clampn(v, l_get(it->min, a), l_get(it->max, a));
	return l_maxf(v, l_get(it->pb, a)) + l_sum_axis(it->margin, a);
}

// The context of one axis's track sizing.
typedef struct {
	int a;
	const tracks2 *tracks;
	ak_lsize inner;
	float gaps[2];
	ak_lpass *p;
	bool under_min, under_max;
} sizing;

static float item_min_content(grid_item *it, const sizing *z) {
	if (l_isnan(it->min_c[z->a])) it->min_c[z->a] = contribution(it, z->a, AK_LMIN_CONTENT, z->tracks, z->inner, z->gaps, z->p);
	return it->min_c[z->a];
}

static float item_max_content(grid_item *it, const sizing *z) {
	if (l_isnan(it->max_c[z->a])) it->max_c[z->a] = contribution(it, z->a, AK_LMAX_CONTENT, z->tracks, z->inner, z->gaps, z->p);
	return it->max_c[z->a];
}

// item_minimum is the minimum contribution: the outer min-size, or for
// an auto min-size the automatic minimum (min-content, or 0 for scroll
// containers), capped when every spanned track has a fixed max.
static float item_minimum(grid_item *it, const sizing *z) {
	int a = z->a;
	if (l_isdef(it->minimum[a])) return it->minimum[a];
	const track *ts = z->tracks->t[a];
	float v;
	if (l_isdef(l_get(it->min, a))) {
		v = l_get(it->min, a) + l_sum_axis(it->margin, a);
	} else if (l_is_scroll_container(it->node)) {
		v = l_get(it->pb, a) + l_sum_axis(it->margin, a);
	} else {
		v = item_min_content(it, z);
		bool all_fixed = true;
		float limit = z->gaps[a] * (float)(span_len(it, a) - 1);
		for (int ti = it->span[a][0]; ti < it->span[a][1]; ti++) {
			const track *t = &ts[ti];
			if (t->max_b.kind != AK_LBREADTH_POINT && !(t->max_b.kind == AK_LBREADTH_PERCENT && l_isdef(l_get(z->inner, a)))) {
				all_fixed = false;
				break;
			}
			limit += resolve_breadth(t->max_b, l_get(z->inner, a));
		}
		if (all_fixed && l_isnan(l_get(it->size, a))) v = l_minf(v, l_maxf(limit, l_get(it->pb, a) + l_sum_axis(it->margin, a)));
	}
	it->minimum[a] = v;
	return v;
}

static float limit_of(const track *t) {
	if (is_fixed(t->max_b, LNAN)) return t->limit;
	return LINF;
}

static float limit_or_base(const track *t) {
	if (t->limit == LINF) return t->base;
	return t->limit;
}

static float grow_limit(float limit, float v) {
	if (limit == LINF) return v;
	return l_maxf(limit, v);
}

static int count_flex(const track *ts, int nts) {
	int c = 0;
	for (int i = 0; i < nts; i++) {
		if (is_flex(&ts[i])) c++;
	}
	return l_imax(c, 1);
}

static float gaps_total(const track *ts, int nts, float gap) {
	int v = 0;
	for (int i = 0; i < nts; i++) {
		if (!ts[i].collapsed) v++;
	}
	if (v <= 1) return 0;
	return gap * (float)(v - 1);
}

// The phases of sizing items that span several tracks: which tracks
// each grows, and with what contribution.
enum { PH_MIN_INTRINSIC, PH_CONTENT_MINS, PH_MAX_CONTENT_MINS, PH_MAX_INTRINSIC, PH_MAX_CONTENT_MAXES, PH_COUNT };

static bool phase_base(int ph) { return ph <= PH_MAX_CONTENT_MINS; }

static bool phase_tracks(int ph, const track *t, bool under_max) {
	switch (ph) {
	case PH_MIN_INTRINSIC: return min_intrinsic(t);
	case PH_CONTENT_MINS: return t->min_b.kind == AK_LBREADTH_MIN_CONTENT || t->min_b.kind == AK_LBREADTH_MAX_CONTENT;
	case PH_MAX_CONTENT_MINS: return t->min_b.kind == AK_LBREADTH_MAX_CONTENT || (under_max && t->min_b.kind == AK_LBREADTH_AUTO);
	case PH_MAX_INTRINSIC: return max_intrinsic(t);
	case PH_MAX_CONTENT_MAXES:
		return t->max_b.kind == AK_LBREADTH_MAX_CONTENT || t->max_b.kind == AK_LBREADTH_AUTO || t->max_b.kind == AK_LBREADTH_FIT_CONTENT;
	}
	return false;
}

static float phase_contrib(int ph, grid_item *it, const sizing *z) {
	switch (ph) {
	case PH_MIN_INTRINSIC:
		if (z->under_min) return item_min_content(it, z);
		if (z->under_max) return item_max_content(it, z);
		return item_minimum(it, z);
	case PH_CONTENT_MINS: return item_min_content(it, z);
	case PH_MAX_CONTENT_MINS: return item_max_content(it, z);
	case PH_MAX_INTRINSIC: return item_min_content(it, z);
	default: return item_max_content(it, z);
	}
}

static bool has_max_contentish(const track *ts, int nts, int ph, bool under_max) {
	for (int i = 0; i < nts; i++) {
		if (phase_tracks(ph, &ts[i], under_max) && (ts[i].max_b.kind == AK_LBREADTH_MAX_CONTENT || ts[i].max_b.kind == AK_LBREADTH_AUTO)) return true;
	}
	return false;
}

static float cur_of(const track *t, bool base) { return base ? t->base : limit_or_base(t); }

// distribute_extra spreads extra over the tracks phase ph selects,
// equally, not past their growth limits for bases while any can still
// grow, and records the largest increase per track in planned.
static void distribute_extra(track *ts, int nts, float extra, int ph, bool under_max, ak_lpass *p) {
	bool base = phase_base(ph);
	l_mark mk = l_scratch_mark(p);
	float *incs = L_SCRATCH(p, float, nts);
	for (int i = 0; i < nts; i++) incs[i] = 0;
	for (int iter = 0; iter < nts + 1; iter++) {
		int growable = 0;
		for (int i = 0; i < nts; i++) {
			const track *t = &ts[i];
			if (!phase_tracks(ph, t, under_max) || t->collapsed) continue;
			if (base && t->limit != LINF && cur_of(t, base) + incs[i] >= t->limit) continue;
			if (!base && t->max_b.kind == AK_LBREADTH_FIT_CONTENT && cur_of(t, base) + incs[i] >= t->max_b.v) continue;
			growable++;
		}
		if (growable == 0 || extra <= 0.0001f) break;
		float share = extra / (float)growable;
		for (int i = 0; i < nts; i++) {
			const track *t = &ts[i];
			if (!phase_tracks(ph, t, under_max) || t->collapsed) continue;
			float room = LINF;
			if (base && t->limit != LINF) room = t->limit - cur_of(t, base) - incs[i];
			if (!base && t->max_b.kind == AK_LBREADTH_FIT_CONTENT) room = t->max_b.v - cur_of(t, base) - incs[i];
			if (room <= 0) continue;
			float d = l_minf(share, room);
			incs[i] += d;
			extra -= d;
		}
	}
	if (extra > 0.0001f) {  // past the limits
		int k = 0;
		for (int i = 0; i < nts; i++) {
			const track *t = &ts[i];
			if (!phase_tracks(ph, t, under_max) || t->collapsed) continue;
			if (base && !(t->max_b.kind == AK_LBREADTH_MAX_CONTENT || t->max_b.kind == AK_LBREADTH_AUTO)) continue;
			k++;
		}
		if (k == 0) {
			for (int i = 0; i < nts; i++) {
				if (phase_tracks(ph, &ts[i], under_max) && !ts[i].collapsed) k++;
			}
		}
		for (int i = 0; i < nts; i++) {
			const track *t = &ts[i];
			if (!phase_tracks(ph, t, under_max) || t->collapsed) continue;
			if (base && k > 0 && !(t->max_b.kind == AK_LBREADTH_MAX_CONTENT || t->max_b.kind == AK_LBREADTH_AUTO) &&
				has_max_contentish(ts, nts, ph, under_max)) {
				continue;
			}
			incs[i] += extra / (float)k;
		}
	}
	for (int i = 0; i < nts; i++) ts[i].planned = l_maxf(ts[i].planned, incs[i]);
	l_scratch_release(p, mk);
}

// find_fr finds the size of an fr given the space to fill (over the
// tracks [s, e), or all tracks when s < 0): tracks whose base is larger
// than their share count as inflexible.
static float find_fr(const track *ts, int nts, float space, int s, int e, ak_lpass *p) {
	if (s < 0) {
		s = 0;
		e = nts;
	}
	l_mark mk = l_scratch_mark(p);
	bool *fl = L_SCRATCH(p, bool, e - s);
	for (int i = 0; i < e - s; i++) fl[i] = false;
	float result = 0;
	for (int iter = 0; iter < e - s + 1; iter++) {
		float left = space;
		float sum = 0;
		for (int i = s; i < e; i++) {
			const track *t = &ts[i];
			if (t->collapsed) continue;
			if (is_flex(t) && !fl[i - s]) sum += t->max_b.v;
			else left -= t->base;
		}
		if (sum < 1) sum = 1;
		float fr = left / sum;
		if (fr < 0) fr = 0;
		bool changed = false;
		for (int i = s; i < e; i++) {
			const track *t = &ts[i];
			if (is_flex(t) && !fl[i - s] && !t->collapsed && fr * t->max_b.v < t->base) {
				fl[i - s] = true;
				changed = true;
			}
		}
		if (!changed) {
			result = fr;
			break;
		}
	}
	l_scratch_release(p, mk);
	return result;
}

// size_tracks runs the track sizing algorithm on axis a.
static void size_tracks(ak_lnode *n, int a, grid_item *items, int nitems, tracks2 *tracks, ak_lsize inner, ak_lsize avail, float gap, ak_lpass *p) {
	track *ts = tracks->t[a];
	int nts = tracks->n[a];
	float basis = l_get(inner, a);
	float space = l_get(avail, a);
	sizing z = {.a = a, .tracks = tracks, .inner = inner, .p = p, .under_min = space == AK_LMIN_CONTENT, .under_max = space == AK_LMAX_CONTENT};
	z.gaps[0] = l_or0(l_resolve(n->style.column_gap, inner.w));
	z.gaps[1] = l_or0(l_resolve(n->style.row_gap, inner.h));
	// §12.4 Initialize.
	for (int i = 0; i < nts; i++) {
		track *t = &ts[i];
		if (t->min_b.kind == AK_LBREADTH_PERCENT && l_isnan(basis)) t->min_b = ak_lbauto();
		if (t->max_b.kind == AK_LBREADTH_PERCENT && l_isnan(basis)) t->max_b = ak_lbauto();
		if (t->max_b.kind == AK_LBREADTH_FIT_CONTENT_PERCENT) {
			if (l_isnan(basis)) t->max_b = ak_lbmax_content();
			else t->max_b = (ak_lbreadth){AK_LBREADTH_FIT_CONTENT, t->max_b.v * basis / 100};
		}
		if (t->min_b.kind == AK_LBREADTH_FR) t->min_b = ak_lbauto();  // fr isn't a valid minimum
		if (t->collapsed) {
			t->base = 0;
			t->limit = 0;
			continue;
		}
		t->base = 0;
		if (is_fixed(t->min_b, basis)) t->base = resolve_breadth(t->min_b, basis);
		t->limit = LINF;
		if (is_fixed(t->max_b, basis)) t->limit = resolve_breadth(t->max_b, basis);
		if (t->limit < t->base) t->limit = t->base;
	}
	bool under_min = z.under_min, under_max = z.under_max;

	// §12.5 Intrinsic sizes. Items spanning one non-flexible track:
	int max_span = 1;
	for (int i = 0; i < nitems; i++) {
		grid_item *it = &items[i];
		int l = span_len(it, a);
		if (l > max_span && !it->flex[a]) max_span = l;
		if (l != 1 || it->flex[a]) continue;
		track *t = &ts[it->span[a][0]];
		if (t->collapsed) continue;
		switch (t->min_b.kind) {
		case AK_LBREADTH_MIN_CONTENT: t->base = l_maxf(t->base, item_min_content(it, &z)); break;
		case AK_LBREADTH_MAX_CONTENT: t->base = l_maxf(t->base, item_max_content(it, &z)); break;
		case AK_LBREADTH_AUTO: {
			float v;
			if (under_min) {
				v = item_min_content(it, &z);
				v = l_maxf(l_minf(v, limit_of(t)), item_minimum(it, &z));
			} else if (under_max) {
				v = item_max_content(it, &z);
				v = l_maxf(l_minf(v, limit_of(t)), item_minimum(it, &z));
			} else {
				v = item_minimum(it, &z);
			}
			t->base = l_maxf(t->base, v);
			break;
		}
		}
		switch (t->max_b.kind) {
		case AK_LBREADTH_MIN_CONTENT: t->limit = grow_limit(t->limit, item_min_content(it, &z)); break;
		case AK_LBREADTH_MAX_CONTENT:
		case AK_LBREADTH_AUTO: t->limit = grow_limit(t->limit, item_max_content(it, &z)); break;
		case AK_LBREADTH_FIT_CONTENT: {
			float mc = item_max_content(it, &z);
			t->limit = grow_limit(t->limit, l_minf(mc, l_maxf(t->max_b.v, item_min_content(it, &z))));
			break;
		}
		}
		if (t->limit < t->base) t->limit = t->base;
	}
	for (int i = 0; i < nts; i++) {  // fit-content limits growth to its argument
		track *t = &ts[i];
		if (t->max_b.kind == AK_LBREADTH_FIT_CONTENT && t->limit != LINF) t->limit = l_maxf(t->base, l_minf(t->limit, t->max_b.v));
	}

	// Items spanning several tracks, by increasing span.
	for (int span = 2; span <= max_span; span++) {
		for (int ph = 0; ph < PH_COUNT; ph++) {
			bool base = phase_base(ph);
			for (int i = 0; i < nts; i++) ts[i].planned = 0;
			bool any = false;
			for (int i = 0; i < nitems; i++) {
				grid_item *it = &items[i];
				if (span_len(it, a) != span || it->flex[a]) continue;
				int s = it->span[a][0], e = it->span[a][1];
				int affected = 0;
				float used = gap * (float)(span - 1);
				for (int ti = s; ti < e; ti++) {
					const track *t = &ts[ti];
					if (base) used += t->base;
					else used += limit_or_base(t);
					if (phase_tracks(ph, t, under_max) && !t->collapsed) affected++;
				}
				if (affected == 0) continue;
				float extra = phase_contrib(ph, it, &z) - used;
				if (extra <= 0) continue;
				any = true;
				distribute_extra(ts + s, e - s, extra, ph, under_max, p);
			}
			if (!any) continue;
			for (int i = 0; i < nts; i++) {
				track *t = &ts[i];
				if (t->planned == 0) continue;
				if (base) {
					t->base += t->planned;
					if (t->limit < t->base) t->limit = t->base;
				} else {
					t->limit = limit_or_base(t) + t->planned;
				}
			}
		}
	}
	// Items crossing flexible tracks grow the flexible tracks' bases.
	for (int i = 0; i < nts; i++) ts[i].planned = 0;
	for (int i = 0; i < nitems; i++) {
		grid_item *it = &items[i];
		if (!it->flex[a]) continue;
		int s = it->span[a][0], e = it->span[a][1];
		float used = gap * (float)(e - s - 1);
		float fr_sum = 0;
		for (int ti = s; ti < e; ti++) {
			used += ts[ti].base;
			if (is_flex(&ts[ti])) fr_sum += ts[ti].max_b.v;
		}
		float c;
		if (under_min) c = item_min_content(it, &z);
		else if (under_max) c = item_max_content(it, &z);
		else c = item_minimum(it, &z);
		float extra = c - used;
		if (extra <= 0) continue;
		for (int ti = s; ti < e; ti++) {
			track *t = &ts[ti];
			if (!is_flex(t) || !min_intrinsic(t)) continue;
			float share = extra / (float)count_flex(ts + s, e - s);
			if (fr_sum > 0) share = extra * t->max_b.v / fr_sum;
			t->planned = l_maxf(t->planned, share);
		}
	}
	for (int i = 0; i < nts; i++) {
		track *t = &ts[i];
		t->base += t->planned;
		if (t->limit < t->base) t->limit = t->base;
	}
	for (int i = 0; i < nts; i++) {
		if (ts[i].limit == LINF) ts[i].limit = ts[i].base;
	}

	// §12.6 Maximize tracks.
	float free = LNAN;
	if (l_definite(space)) free = space - tracks_total(ts, nts, gap);
	if (l_isdef(free) && free > 0) {
		for (int iter = 0; iter < nts + 1; iter++) {
			int grow = 0;
			for (int i = 0; i < nts; i++) {
				if (ts[i].limit > ts[i].base && !ts[i].collapsed) grow++;
			}
			if (grow == 0 || free <= 0.0001f) break;
			float share = free / (float)grow;
			for (int i = 0; i < nts; i++) {
				track *t = &ts[i];
				if (t->limit > t->base && !t->collapsed) {
					float inc = l_minf(share, t->limit - t->base);
					t->base += inc;
					free -= inc;
				}
			}
		}
	} else if (under_max) {
		for (int i = 0; i < nts; i++) ts[i].base = ts[i].limit;
	}

	// §12.7 Expand flexible tracks.
	bool has_flex = false;
	for (int i = 0; i < nts; i++) {
		if (is_flex(&ts[i]) && !ts[i].collapsed) {
			has_flex = true;
			break;
		}
	}
	if (has_flex) {
		float fr = 0;
		if (l_definite(space)) {
			fr = find_fr(ts, nts, space - gaps_total(ts, nts, gap), -1, -1, p);
		} else if (!under_min) {
			for (int i = 0; i < nts; i++) {
				const track *t = &ts[i];
				if (is_flex(t) && !t->collapsed) {
					float f = t->max_b.v;
					if (f > 1) fr = l_maxf(fr, t->base / f);
					else fr = l_maxf(fr, t->base);
				}
			}
			for (int i = 0; i < nitems; i++) {
				grid_item *it = &items[i];
				if (!it->flex[a]) continue;
				int s = it->span[a][0], e = it->span[a][1];
				float c = item_max_content(it, &z);
				fr = l_maxf(fr, find_fr(ts, nts, c - gap * (float)(e - s - 1), s, e, p));
			}
		}
		for (int i = 0; i < nts; i++) {
			track *t = &ts[i];
			if (is_flex(t) && !t->collapsed) t->base = l_maxf(t->base, fr * t->max_b.v);
		}
	}

	// §12.8 Stretch auto tracks.
	uint8_t c = a == 1 ? n->style.align_content : n->style.justify_content;
	if ((c == AK_LCONTENT_NORMAL || c == AK_LCONTENT_STRETCH) && l_definite(space)) {
		float f = space - tracks_total(ts, nts, gap);
		int autos = 0;
		for (int i = 0; i < nts; i++) {
			if (ts[i].max_b.kind == AK_LBREADTH_AUTO && !ts[i].collapsed) autos++;
		}
		if (f > 0 && autos > 0) {
			float share = f / (float)autos;
			for (int i = 0; i < nts; i++) {
				if (ts[i].max_b.kind == AK_LBREADTH_AUTO && !ts[i].collapsed) ts[i].base += share;
			}
		}
	}
}
