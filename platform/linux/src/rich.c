// rich.c: rich text and selection.
//
// A RichText is a GtkLabel whose text is styled with Pango attributes:
// what draws it (the label's layout) is what measures it (a layout of the
// same text and attributes, at the same width). Links are the host's own:
// colored and underlined as GTK colors a label's, hit-tested in its
// layout (a pointer over them, a click on one reports Link, else opens
// it). Inline components are shape attributes of their size on the
// baseline: their views are placed where the layout puts them.
//
// A Selectable selects across the RichTexts in it as one text. GTK keeps
// one label's selection at a time, so the selection is the host's: a drag
// on the Selectable's view (claimed in the capture phase) maps the pointer
// to a text and a character in its label's layout, each text between the
// ends, in reading order, has its part, drawn under its label (GDK's clip
// region of the range) in libadwaita's selection color (the accent's
// while the Selectable has the focus). A double click selects words, a
// triple click paragraphs, Shift+click extends, Ctrl+A selects all, and
// Ctrl+C, Ctrl+Insert and the context menu's Copy (GTK's words) copy (each
// text's part a paragraph), as a selection fills the primary selection. A
// press on a link, a control or a pressable view in it is theirs; on a
// touchscreen a drag scrolls, a long press selects a word.
#include "host.h"

#include <math.h>
#include <stdlib.h>

void lx_popup_at(node *n, GMenuModel *model, double x, double y);

typedef struct {
	ak_id id;
	float w, h;
} inline_size;

typedef struct {
	int start, end;  // bytes of the text
	char *url;
} link_span;

typedef struct {
	ak_buf content;
	AK_VEC(inline_size) inlines;
	bool dirty, queued;
	char *text;  // the label's text: U+FFFC for an inline component
	AK_VEC(link_span) links;
	AK_VEC(int) inline_at;  // each inline component's byte index
	int pressed;            // the link a click went down on (-1: none)
	bool over_link;
	int sel[2];  // its selected part (characters), when has_sel
	bool has_sel;
} rich_text;

static rich_text *rich_of(node *n) { return n->kind == AK_KIND_RICH_TEXT ? n->ext : NULL; }

static void free_links(rich_text *r) {
	for (uint32_t i = 0; i < r->links.len; i++) free(r->links.data[i].url);
	r->links.len = 0;
}

static void free_rich(node *n) {
	rich_text *r = n->ext;
	ak_buf_free(&r->content);
	ak_vec_free(&r->inlines);
	free(r->text);
	free_links(r);
	ak_vec_free(&r->links);
	ak_vec_free(&r->inline_at);
	free(r);
	n->ext = NULL;
}

// lx_run_attrs adds a run's style as Pango attributes: its font where it
// sets one (the view's otherwise), italic, monospaced, underline (a
// link's too), strikethrough, its color (a link's the accent) and
// background.
void lx_run_attrs(node *n, const ak_rich_style *s, void (*add)(PangoAttribute *a, void *ctx), void *ctx) {
	if (s->text_style || s->size || s->weight) {
		node x = {.text_style = n->text_style, .font_size = n->font_size, .font_weight = n->font_weight};
		if (s->text_style) x.text_style = s->text_style;
		if (s->size) x.font_size = s->size;
		if (s->weight) x.font_weight = s->weight;
		float size;
		int weight;
		lx_font(&x, &size, &weight);
		add(pango_attr_size_new_absolute((int)(size * 1024 + 0.5f)), ctx);
		add(pango_attr_weight_new((PangoWeight)weight), ctx);
	}
	if (s->flags & AK_RICH_ITALIC) add(pango_attr_style_new(PANGO_STYLE_ITALIC), ctx);
	if (s->flags & AK_RICH_MONO) add(pango_attr_family_new("monospace"), ctx);
	if ((s->flags & AK_RICH_UNDERLINE) || s->link.len) add(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE), ctx);
	if (s->flags & AK_RICH_STRIKE) add(pango_attr_strikethrough_new(1), ctx);
	GdkRGBA c;
	bool ok = lx_resolve(s->color, &c);
	if (!ok && s->link.len) {
		c = H.theme.accent;
		ok = true;
	}
	if (ok) {
		add(pango_attr_foreground_new(u16c(c.red), u16c(c.green), u16c(c.blue)), ctx);
		if (c.alpha < 1) add(pango_attr_foreground_alpha_new(u16c(c.alpha)), ctx);
	}
	if (lx_resolve(s->background, &c) && c.alpha > 0) {
		add(pango_attr_background_new(u16c(c.red), u16c(c.green), u16c(c.blue)), ctx);
		if (c.alpha < 1) add(pango_attr_background_alpha_new(u16c(c.alpha)), ctx);
	}
}

// link_at is the link under a point of the label, or -1.
static int link_at(node *n, rich_text *r, double x, double y) {
	if (!r->links.len || !n->inner) return -1;
	PangoLayout *l = gtk_label_get_layout((GtkLabel *)n->inner);
	if (!l) return -1;
	int ox, oy, index, trailing;
	gtk_label_get_layout_offsets((GtkLabel *)n->inner, &ox, &oy);
	if (!pango_layout_xy_to_index(l, (int)((x - ox) * 1024), (int)((y - oy) * 1024), &index, &trailing)) return -1;
	for (uint32_t k = 0; k < r->links.len; k++)
		if (index >= r->links.data[k].start && index < r->links.data[k].end) return (int)k;
	return -1;
}

static void hover(node *n, rich_text *r, bool over) {
	if (over == r->over_link || !n->inner) return;
	r->over_link = over;
	if (over)
		gtk_widget_set_cursor_from_name(n->inner, "pointer");
	else
		gtk_widget_set_cursor(n->inner, NULL);
}

// open_link reports a link the user clicked, or opens it if the app
// doesn't listen.
static void open_link(node *n, const char *url) {
	if (listening(n, AK_EVENT_LINK)) {
		lx_emit(n, (ak_event){.kind = AK_EVENT_LINK, .s = ak_cstr(url)});
		return;
	}
	lx_open_url(url);
}

static void on_link_pressed(GtkGestureClick *g, int count, double x, double y, node *n) {
	(void)count;
	rich_text *r = rich_of(n);
	r->pressed = link_at(n, r, x, y);
	gtk_gesture_set_state((GtkGesture *)g, r->pressed >= 0 ? GTK_EVENT_SEQUENCE_CLAIMED : GTK_EVENT_SEQUENCE_DENIED);
}

static void on_link_released(GtkGestureClick *g, int count, double x, double y, node *n) {
	(void)g;
	(void)count;
	rich_text *r = rich_of(n);
	int k = r->pressed;
	r->pressed = -1;
	if (k >= 0 && (uint32_t)k < r->links.len && link_at(n, r, x, y) == k) {
		lx_enter_begin();
		open_link(n, r->links.data[k].url);
		lx_enter_end();
	}
}

static void on_link_motion(GtkEventControllerMotion *m, double x, double y, node *n) {
	(void)m;
	hover(n, rich_of(n), link_at(n, rich_of(n), x, y) >= 0);
}

static void on_link_leave(GtkEventControllerMotion *m, node *n) {
	(void)m;
	hover(n, rich_of(n), false);
}

static bool rich_create(node *n) {
	if (n->kind != AK_KIND_RICH_TEXT) return false;
	GtkWidget *l = gtk_label_new("");
	gtk_label_set_wrap((GtkLabel *)l, 1);
	gtk_label_set_wrap_mode((GtkLabel *)l, PANGO_WRAP_WORD_CHAR);
	gtk_label_set_xalign((GtkLabel *)l, 0);
	gtk_label_set_yalign((GtkLabel *)l, 0);
	lx_set_inner(n, l);
	rich_text *r = calloc(1, sizeof *r);
	if (!r) abort();
	r->dirty = true;
	r->pressed = -1;
	n->ext = r;
	n->ext_free = free_rich;
	// A click on a link is the link's: claimed, so what's under the text
	// doesn't take it too.
	GtkGesture *g = gtk_gesture_click_new();
	gtk_event_controller_set_propagation_phase((GtkEventController *)g, GTK_PHASE_CAPTURE);
	lx_connect(n, g, "pressed", G_CALLBACK(on_link_pressed));
	lx_connect(n, g, "released", G_CALLBACK(on_link_released));
	gtk_widget_add_controller(l, (GtkEventController *)g);
	GtkEventController *m = gtk_event_controller_motion_new();
	lx_connect(n, m, "motion", G_CALLBACK(on_link_motion));
	lx_connect(n, m, "leave", G_CALLBACK(on_link_leave));
	gtk_widget_add_controller(l, m);
	return true;
}

typedef struct {
	PangoAttrList *list;
	int start, end;
} attr_ctx;

static void add_attr(PangoAttribute *a, void *data) {
	attr_ctx *c = data;
	a->start_index = (guint)c->start;
	a->end_index = (guint)c->end;
	pango_attr_list_insert(c->list, a);
}

// build sets the label's text and attributes from the content, styled by
// the view's text props.
static void build(node *n) {
	rich_text *r = rich_of(n);
	if (!r || !r->dirty || !n->inner) return;
	r->dirty = false;
	ak_rich_run_vec runs = {0};
	ak_decode_rich(r->content.data, r->content.len, &runs);
	ak_buf b = {0};
	attr_ctx c = {lx_attrs(n, true), 0, 0};
	free_links(r);
	r->inline_at.len = 0;
	uint32_t inl = 0;
	for (uint32_t i = 0; i < runs.len; i++) {
		ak_rich_run *run = &runs.data[i];
		int start = (int)b.len;
		if (run->style.flags & AK_RICH_INLINE) {
			// An inline component: a shape of its size on the baseline.
			ak_buf_put(&b, "\xef\xbf\xbc", 3);
			ak_vec_push(&r->inline_at, start);
			inline_size in = inl < r->inlines.len ? r->inlines.data[inl] : (inline_size){0};
			inl++;
			PangoRectangle box = {0, -(int)(in.h * 1024 + 0.5f), (int)(in.w * 1024 + 0.5f), (int)(in.h * 1024 + 0.5f)};
			c.start = start;
			c.end = (int)b.len;
			add_attr(pango_attr_shape_new(&box, &box), &c);
			continue;
		}
		ak_buf_put(&b, run->text.p, run->text.len);
		int end = (int)b.len;
		if ((run->text.len == 1 && run->text.p[0] == '\n') || end == start) continue;
		c.start = start;
		c.end = end;
		lx_run_attrs(n, &run->style, add_attr, &c);
		if (run->style.link.len) {
			link_span s = {start, end, str_dup(run->style.link)};
			ak_vec_push(&r->links, s);
		}
	}
	ak_vec_free(&runs);
	free(r->text);
	r->text = str_dup(ak_strn(b.data, b.len));
	r->has_sel = false;  // as a label's goes with its text
	// n->text: the text without its inline components (what's found, dumped).
	ak_buf t = {0};
	for (size_t i = 0; i < b.len; i++) {
		if (i + 2 < b.len && b.data[i] == 0xef && b.data[i + 1] == 0xbf && b.data[i + 2] == 0xbc) {
			i += 2;
			continue;
		}
		ak_buf_u8(&t, b.data[i]);
	}
	str_set(&n->text, ak_strn(t.data, t.len));
	ak_buf_free(&t);
	ak_buf_free(&b);
	GtkLabel *l = (GtkLabel *)n->inner;
	gtk_label_set_text(l, r->text);
	gtk_label_set_attributes(l, c.list);
	pango_attr_list_unref(c.list);
	gtk_label_set_xalign(l, lx_xalign(n));
	GtkJustification j = GTK_JUSTIFY_LEFT;
	switch (lx_physical_align(n)) {
	case 1: j = GTK_JUSTIFY_CENTER; break;
	case 2: j = GTK_JUSTIFY_RIGHT; break;
	case 3: j = GTK_JUSTIFY_FILL; break;
	}
	gtk_label_set_justify(l, j);
	if (n->line_limit > 0) {
		gtk_label_set_lines(l, n->line_limit);
		gtk_label_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	} else {
		gtk_label_set_lines(l, -1);
		gtk_label_set_ellipsize(l, PANGO_ELLIPSIZE_NONE);
	}
	if (n->widget) gtk_widget_queue_allocate(n->widget);  // the inline components move with the text
}

static void build_later(void *ctx) {
	node *n = node_get((ak_id)(uintptr_t)ctx);
	rich_text *r = n ? rich_of(n) : NULL;
	if (!r) return;
	r->queued = false;
	if (n->widget) build(n);
}

// invalidate builds the label again: when the core measures it, or once
// the core has returned.
static void invalidate(node *n) {
	rich_text *r = rich_of(n);
	r->dirty = true;
	if (r->queued) return;
	r->queued = true;
	lx_after_core(build_later, (void *)(uintptr_t)n->id);
}

static bool rich_prop(node *n, ak_prop p, const ak_value *v) {
	rich_text *r = rich_of(n);
	if (!r) return false;
	switch (p) {
	case AK_PROP_RICH_TEXT_CONTENT:
		ak_buf_reset(&r->content);
		ak_buf_put(&r->content, v->s.p, v->s.len);
		break;
	case AK_PROP_RICH_TEXT_INLINES: {
		r->inlines.len = 0;
		ak_reader rd = {(const uint8_t *)v->s.p, v->s.len, 0, false};
		uint32_t count = ak_read_u32(&rd);
		for (uint32_t i = 0; i < count && rd.off + 12 <= rd.len; i++) {
			inline_size in;
			in.id = ak_read_u32(&rd);
			in.w = ak_read_f32(&rd);
			in.h = ak_read_f32(&rd);
			ak_vec_push(&r->inlines, in);
		}
		break;
	}
	default:
		return false;
	}
	invalidate(n);
	return true;
}

static void rich_text_changed(node *n) {
	if (rich_of(n)) invalidate(n);
}

// layout_at is a layout of the label's text and attributes at a width
// (big: unbounded), to its line limit.
static PangoLayout *layout_at(node *n, rich_text *r, float w, PangoWrapMode wrap) {
	PangoLayout *pl = gtk_widget_create_pango_layout(n->inner, r->text);
	PangoAttrList *a = gtk_label_get_attributes((GtkLabel *)n->inner);
	if (a) pango_layout_set_attributes(pl, a);
	pango_layout_set_wrap(pl, wrap);
	if (n->line_limit > 0) {
		pango_layout_set_height(pl, -n->line_limit);
		pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
	}
	pango_layout_set_width(pl, w < AK_BIG ? (int)(w * 1024) : -1);
	return pl;
}

// measure: as wide as its widest line at the width it may take, as tall
// as its lines; min-content is its widest word.
static bool rich_measure(node *n, float cw, float ch, float aw, float ah, float *ow, float *oh) {
	(void)ch;
	(void)ah;
	rich_text *r = rich_of(n);
	if (!r) return false;
	build(n);
	*ow = *oh = 0;
	if (str_empty(r->text) || !n->inner) return true;
	float w;
	int pw, ph;
	if (cw != cw && aw <= -AK_INF) {
		PangoLayout *pl = layout_at(n, r, 0, PANGO_WRAP_WORD);  // a word a line: the widest word
		pango_layout_set_width(pl, 1);
		pango_layout_get_pixel_size(pl, &pw, &ph);
		g_object_unref(pl);
		w = (float)pw;
	} else {
		w = lx_fit(cw, aw, 0);
	}
	float inner = w < AK_BIG ? fmaxf(w - n->inset[1] - n->inset[3], 0) : w;
	PangoLayout *pl = layout_at(n, r, inner, PANGO_WRAP_WORD_CHAR);
	pango_layout_get_pixel_size(pl, &pw, &ph);
	g_object_unref(pl);
	*ow = fminf((float)pw + n->inset[1] + n->inset[3], w);
	*oh = (float)ph + n->inset[0] + n->inset[2];
	return true;
}

// lx_place_inline puts an inline component (a child of a RichText, its
// frame at 0, 0) where the label's layout puts its shape: its bottom on
// the line's baseline.
void lx_place_inline(node *p, node *c, float f[4]) {
	rich_text *r = rich_of(p);
	if (!r || !p->inner) return;
	int k = -1;
	for (uint32_t i = 0; i < r->inlines.len; i++)
		if (r->inlines.data[i].id == c->id) {
			k = (int)i;
			break;
		}
	if (k < 0 || (uint32_t)k >= r->inline_at.len) return;
	PangoLayout *l = gtk_label_get_layout((GtkLabel *)p->inner);
	if (!l) return;
	int ox, oy;
	gtk_label_get_layout_offsets((GtkLabel *)p->inner, &ox, &oy);
	PangoLayoutIter *it = pango_layout_get_iter(l);
	while (pango_layout_iter_get_index(it) < r->inline_at.data[k] && pango_layout_iter_next_char(it)) {
	}
	PangoRectangle ext;
	pango_layout_iter_get_char_extents(it, &ext);
	int base = pango_layout_iter_get_baseline(it);
	pango_layout_iter_free(it);
	int x = ext.width < 0 ? ext.x + ext.width : ext.x;
	f[0] = p->inset[3] + (float)ox + (float)x / 1024;
	f[1] = p->inset[0] + (float)oy + (float)base / 1024 - r->inlines.data[k].h;
}

// ---- Selectable ----

typedef struct {
	node *t;
	int off;
} sel_pos;

typedef struct {
	GtkGesture *drag;
	GSimpleAction *copy;
	sel_pos anchor[2];
	bool has;
	int unit;  // 0 characters, 1 words, 2 paragraphs
	bool taking;
	double start_x, start_y;
	int clicks;
	guint32 last_time;
	double last_x, last_y;
} select_area;

static select_area *area_of(node *n) { return n->kind == AK_KIND_SELECTABLE ? n->ext : NULL; }

static int char_count(const char *s) { return s ? (int)g_utf8_strlen(s, -1) : 0; }

// byte_at is the byte index of character c of s.
static int byte_at(const char *s, int c) {
	if (!s) return 0;
	const char *p = s;
	while (c > 0 && *p) {
		p = g_utf8_next_char(p);
		c--;
	}
	return (int)(p - s);
}

static void texts_of(node *n, node_vec *out) {
	for (uint32_t i = 0; i < n->children.len; i++) {
		node *c = n->children.data[i];
		if (c->kind == AK_KIND_RICH_TEXT && c->inner && rich_of(c) && gtk_widget_get_mapped(c->inner)) ak_vec_push(out, c);
		texts_of(c, out);
	}
}

static int order_of(node_vec *texts, sel_pos p, int *off) {
	for (uint32_t i = 0; i < texts->len; i++)
		if (texts->data[i] == p.t) {
			*off = p.off;
			return (int)i;
		}
	*off = 0;
	return -1;
}

static bool less(node_vec *texts, sel_pos a, sel_pos b) {
	int ao, bo, ai = order_of(texts, a, &ao), bi = order_of(texts, b, &bo);
	return ai < bi || (ai == bi && ao < bo);
}

// selected_text is the selection's text: each text's part, a paragraph
// each, without its inline components (malloc'd).
static char *selected_text(node *s) {
	node_vec texts = {0};
	texts_of(s, &texts);
	ak_buf b = {0};
	bool first = true;
	for (uint32_t i = 0; i < texts.len; i++) {
		rich_text *r = rich_of(texts.data[i]);
		if (!r->has_sel) continue;
		if (!first) ak_buf_u8(&b, '\n');
		first = false;
		int from = byte_at(r->text, r->sel[0]), to = byte_at(r->text, r->sel[1]);
		for (int k = from; k < to; k++) {
			if (k + 2 < to && (uint8_t)r->text[k] == 0xef && (uint8_t)r->text[k + 1] == 0xbf && (uint8_t)r->text[k + 2] == 0xbc) {
				k += 2;
				continue;
			}
			ak_buf_u8(&b, (uint8_t)r->text[k]);
		}
	}
	ak_vec_free(&texts);
	char *out = str_dup(ak_strn(b.data, b.len));
	ak_buf_free(&b);
	return out;
}

// select_range selects from a to b: in each text between, its part.
static void select_range(node *s, sel_pos a, sel_pos b) {
	node_vec texts = {0};
	texts_of(s, &texts);
	if (less(&texts, b, a)) {
		sel_pos t = a;
		a = b;
		b = t;
	}
	int ao, bo, ai = order_of(&texts, a, &ao), bi = order_of(&texts, b, &bo);
	for (uint32_t i = 0; i < texts.len; i++) {
		node *t = texts.data[i];
		rich_text *r = rich_of(t);
		int n = char_count(r->text), st = 0, en = 0;
		int ii = (int)i;
		if (ai < 0 || bi < 0 || ii < ai || ii > bi) {
		} else if (ii == ai && ii == bi) {
			st = ao;
			en = bo;
		} else if (ii == ai) {
			st = ao;
			en = n;
		} else if (ii == bi) {
			en = bo;
		} else {
			en = n;
		}
		if (st > n) st = n;
		if (en > n) en = n;
		if (st == en) {
			if (r->has_sel) {
				r->has_sel = false;
				gtk_widget_queue_draw(t->widget);
			}
			continue;
		}
		if (!r->has_sel || r->sel[0] != st || r->sel[1] != en) {
			r->has_sel = true;
			r->sel[0] = st;
			r->sel[1] = en;
			gtk_widget_queue_draw(t->widget);
		}
	}
	ak_vec_free(&texts);
	select_area *area = area_of(s);
	if (area && area->copy) {
		char *t = selected_text(s);
		g_simple_action_set_enabled(area->copy, *t != 0);
		free(t);
	}
}

// text_hit is the character offset in t's label nearest x, y (in the
// Selectable's view).
static sel_pos text_hit(node *s, node *t, double x, double y) {
	rich_text *r = rich_of(t);
	PangoLayout *l = gtk_label_get_layout((GtkLabel *)t->inner);
	if (!l) return (sel_pos){t, 0};
	graphene_point_t in = {(float)x, (float)y}, p = {0, 0};
	gtk_widget_compute_point(s->widget, t->inner, &in, &p);
	int ox, oy, index = 0, trailing = 0;
	gtk_label_get_layout_offsets((GtkLabel *)t->inner, &ox, &oy);
	// Outside the layout, Pango clamps to its nearest place.
	pango_layout_xy_to_index(l, (int)((p.x - ox) * 1024), (int)((p.y - oy) * 1024), &index, &trailing);
	int len = (int)strlen(S(r->text));
	if (index < 0) index = 0;
	if (index > len) index = len;
	return (sel_pos){t, (int)g_utf8_strlen(S(r->text), index) + trailing};
}

// select_hit is the place in a Selectable's texts at x, y in its view.
static sel_pos select_hit(node *s, double x, double y) {
	node_vec texts = {0};
	texts_of(s, &texts);
	sel_pos out = {0};
	if (!texts.len) goto done;
	int best = -1;
	double best_dist = INFINITY;
	for (uint32_t i = 0; i < texts.len; i++) {
		graphene_rect_t b;
		if (!gtk_widget_compute_bounds(texts.data[i]->inner, s->widget, &b)) continue;
		if (y < b.origin.y || y >= b.origin.y + b.size.height) continue;
		double d = x < b.origin.x ? b.origin.x - x : x > b.origin.x + b.size.width ? x - (b.origin.x + b.size.width) : 0;
		if (d < best_dist) {
			best = (int)i;
			best_dist = d;
		}
	}
	if (best >= 0) {
		out = text_hit(s, texts.data[best], x, y);
		goto done;
	}
	for (uint32_t i = 0; i < texts.len; i++) {
		graphene_rect_t b;
		if (gtk_widget_compute_bounds(texts.data[i]->inner, s->widget, &b) && b.origin.y > y) {
			out = (sel_pos){texts.data[i], 0};
			goto done;
		}
	}
	node *last = texts.data[texts.len - 1];
	out = (sel_pos){last, char_count(rich_of(last)->text)};
done:
	ak_vec_free(&texts);
	return out;
}

// PangoLogAttr's bits.
enum { LOG_WHITE = 1 << 3, LOG_WORD_START = 1 << 5, LOG_WORD_END = 1 << 6 };

// select_unit is the unit (a character's place, a word, a paragraph) at p.
static void select_unit(sel_pos p, int unit, sel_pos out[2]) {
	out[0] = out[1] = p;
	if (!p.t || !unit) return;
	rich_text *r = rich_of(p.t);
	int n = char_count(r->text);
	if (!n) return;
	int c = p.off < n - 1 ? p.off : n - 1;
	if (unit == 2) {
		gunichar *u = g_utf8_to_ucs4_fast(r->text, -1, NULL);
		int s = c, e = c;
		while (s > 0 && u[s - 1] != '\n') s--;
		while (e < n && u[e] != '\n') e++;
		g_free(u);
		out[0].off = s;
		out[1].off = e;
		return;
	}
	PangoLayout *l = gtk_label_get_layout((GtkLabel *)p.t->inner);
	if (!l) return;
	int na = 0;
	const PangoLogAttr *attrs = pango_layout_get_log_attrs_readonly(l, &na);
	const uint32_t *a = (const uint32_t *)attrs;
	if (na < n + 1 || c < 0 || c >= n) return;
	int s = c;
	for (;;) {
		if (a[s] & LOG_WORD_START) break;
		if ((a[s] & LOG_WORD_END) || s == 0) {
			s = -1;
			break;
		}
		s--;
	}
	if (s >= 0) {
		int e = c + 1;
		while (e < n && !(a[e] & LOG_WORD_END)) e++;
		out[0].off = s;
		out[1].off = e;
		return;
	}
	if (a[c] & LOG_WHITE) {
		int ws = c, we = c + 1;
		while (ws > 0 && (a[ws - 1] & LOG_WHITE)) ws--;
		while (we < n && (a[we] & LOG_WHITE)) we++;
		out[0].off = ws;
		out[1].off = we;
		return;
	}
	out[0].off = c;
	out[1].off = c + 1;
}

static bool in_texts(node *s, node *t) {
	for (node *p = t; p; p = p->parent)
		if (p == s) return t->inner && !t->dead;
	return false;
}

static void select_to(node *s, sel_pos p) {
	select_area *a = area_of(s);
	if (!a || !a->has || !p.t) return;
	node_vec texts = {0};
	texts_of(s, &texts);
	sel_pos u[2];
	select_unit(p, a->unit, u);
	bool before = less(&texts, u[0], a->anchor[0]);
	ak_vec_free(&texts);
	if (before)
		select_range(s, u[0], a->anchor[1]);
	else
		select_range(s, a->anchor[0], u[1]);
}

static void select_press(node *s, double x, double y, int clicks, bool extend) {
	select_area *a = area_of(s);
	if (!a) return;
	sel_pos p = select_hit(s, x, y);
	if (!p.t) return;
	a->taking = true;
	a->start_x = x;
	a->start_y = y;
	if (extend && a->has && in_texts(s, a->anchor[0].t) && in_texts(s, a->anchor[1].t)) {
		select_to(s, p);
		return;
	}
	a->unit = (clicks < 1 ? 1 : clicks > 3 ? 3 : clicks) - 1;
	select_unit(p, a->unit, a->anchor);
	a->has = true;
	select_range(s, a->anchor[0], a->anchor[1]);
}

// select_primary makes the selection X11's and Wayland's primary
// selection, as a label's is.
static void select_primary(node *s) {
	char *t = selected_text(s);
	if (*t) gdk_clipboard_set_text(gdk_display_get_primary_clipboard(gtk_widget_get_display(s->widget)), t);
	free(t);
}

static void select_all(node *s) {
	select_area *a = area_of(s);
	node_vec texts = {0};
	texts_of(s, &texts);
	if (a && texts.len) {
		node *last = texts.data[texts.len - 1];
		a->anchor[0] = a->anchor[1] = (sel_pos){texts.data[0], 0};
		a->unit = 0;
		a->has = true;
		select_range(s, (sel_pos){texts.data[0], 0}, (sel_pos){last, char_count(rich_of(last)->text)});
		select_primary(s);
	}
	ak_vec_free(&texts);
}

static bool select_copy(node *s) {
	char *t = selected_text(s);
	bool ok = *t != 0;
	if (ok) gdk_clipboard_set_text(gdk_display_get_clipboard(gtk_widget_get_display(s->widget)), t);
	free(t);
	return ok;
}

// select_menu shows the selection's context menu (GTK's words for a
// label's): Copy, Select All.
static void select_menu(node *s, double x, double y) {
	GMenu *m = g_menu_new();
	GMenuItem *copy = g_menu_item_new(g_dgettext("gtk40", "_Copy"), "selection.copy");
	g_menu_append_item(m, copy);
	g_object_unref(copy);
	GMenuItem *all = g_menu_item_new(g_dgettext("gtk40", "Select _All"), "selection.select-all");
	g_menu_append_item(m, all);
	g_object_unref(all);
	lx_popup_at(s, (GMenuModel *)m, x, y);
	g_object_unref(m);
}

// select_takes reports whether a press at x, y in a Selectable selects
// (button 1) or shows the selection's menu (3): not on a link, a control
// or a view that takes the press itself.
static bool select_takes(node *s, double x, double y, guint button) {
	if (button != 1 && button != 3) return false;
	GtkWidget *picked = gtk_widget_pick(s->widget, x, y, GTK_PICK_DEFAULT);
	for (GtkWidget *w = picked; w && w != s->widget; w = gtk_widget_get_parent(w)) {
		node *n = lx_view_node(w);
		if (!n) {
			node *t = lx_view_node(gtk_widget_get_parent(w));
			if (t && t->inner == w && t->kind == AK_KIND_RICH_TEXT && button == 1) {
				graphene_point_t in = {(float)x, (float)y}, p = {0, 0};
				gtk_widget_compute_point(s->widget, w, &in, &p);
				if (link_at(t, rich_of(t), p.x, p.y) >= 0) return false;
			}
			continue;
		}
		if (n->inner && n->kind != AK_KIND_RICH_TEXT && n->kind != AK_KIND_TEXT) return false;  // a control
		if (listening(n, AK_EVENT_PRESS) || listening(n, AK_EVENT_LONG_PRESS) || listening(n, AK_EVENT_PAN)) return false;
		if (button == 3 && n->has_menu) return false;
	}
	return picked != NULL;
}

static int count_click(select_area *s, double x, double y, guint32 t) {
	int wait = 400, dist = 5;
	GtkSettings *st = gtk_settings_get_default();
	if (st) g_object_get(st, "gtk-double-click-time", &wait, "gtk-double-click-distance", &dist, NULL);
	if (s->clicks > 0 && t - s->last_time <= (guint32)wait && fabs(x - s->last_x) <= dist && fabs(y - s->last_y) <= dist)
		s->clicks = s->clicks % 3 + 1;
	else
		s->clicks = 1;
	s->last_time = t;
	s->last_x = x;
	s->last_y = y;
	return s->clicks;
}

static bool from_touch(GtkGesture *g) {
	GdkDevice *d = gtk_gesture_get_device(g);
	return d && gdk_device_get_source(d) == GDK_SOURCE_TOUCHSCREEN;
}

static void on_sel_begin(GtkGestureDrag *g, double x, double y, node *n) {
	select_area *s = area_of(n);
	guint button = gtk_gesture_single_get_current_button((GtkGestureSingle *)g);
	GdkModifierType state = gtk_event_controller_get_current_event_state((GtkEventController *)g);
	if (from_touch((GtkGesture *)g) || !select_takes(n, x, y, button)) {
		gtk_gesture_set_state((GtkGesture *)g, GTK_EVENT_SEQUENCE_DENIED);
		return;
	}
	gtk_gesture_set_state((GtkGesture *)g, GTK_EVENT_SEQUENCE_CLAIMED);
	gtk_widget_grab_focus(n->widget);
	if (button == 3) {
		select_menu(n, x, y);
		return;
	}
	select_press(n, x, y, count_click(s, x, y, gtk_event_controller_get_current_event_time((GtkEventController *)g)), state & GDK_SHIFT_MASK);
}

static void on_sel_update(GtkGestureDrag *g, double dx, double dy, node *n) {
	(void)g;
	select_area *s = area_of(n);
	if (s->taking) select_to(n, select_hit(n, s->start_x + dx, s->start_y + dy));
}

static void on_sel_end(GtkGestureDrag *g, double dx, double dy, node *n) {
	(void)g;
	(void)dx;
	(void)dy;
	select_area *s = area_of(n);
	if (s->taking) {
		s->taking = false;
		select_primary(n);
	}
}

static void on_sel_long(GtkGestureLongPress *g, double x, double y, node *n) {
	(void)g;
	if (!select_takes(n, x, y, 1)) return;
	gtk_widget_grab_focus(n->widget);
	select_press(n, x, y, 2, false);
	area_of(n)->taking = false;
	select_primary(n);
}

static void on_sel_focus(GtkEventControllerFocus *f, node *n) {
	(void)f;
	node_vec texts = {0};
	texts_of(n, &texts);
	for (uint32_t i = 0; i < texts.len; i++)
		if (rich_of(texts.data[i])->has_sel) gtk_widget_queue_draw(texts.data[i]->widget);
	ak_vec_free(&texts);
}

static void on_sel_copy(GSimpleAction *a, GVariant *p, node *n) {
	(void)a;
	(void)p;
	select_copy(n);
}

static void on_sel_all(GSimpleAction *a, GVariant *p, node *n) {
	(void)a;
	(void)p;
	select_all(n);
}

static gboolean on_sel_key(GtkEventControllerKey *k, guint keyval, guint keycode, GdkModifierType state, node *n) {
	(void)k;
	(void)keycode;
	bool ctrl = (state & GDK_CONTROL_MASK) && !(state & (GDK_ALT_MASK | GDK_SUPER_MASK));
	if (ctrl && (keyval == 'a' || keyval == 'A')) {
		select_all(n);
		return 1;
	}
	if (ctrl && (keyval == 'c' || keyval == 'C' || keyval == 0xff63)) return select_copy(n);
	if (keyval == 0xff67 || (keyval == 0xffc7 && (state & GDK_SHIFT_MASK))) {
		select_menu(n, -1, -1);
		return 1;
	}
	return 0;
}

static void free_area(node *n) {
	free(n->ext);
	n->ext = NULL;
}

static bool selectable_create(node *n) {
	if (n->kind != AK_KIND_SELECTABLE) return false;
	select_area *s = calloc(1, sizeof *s);
	if (!s) abort();
	n->ext = s;
	n->ext_free = free_area;
	GtkWidget *w = n->widget;
	gtk_widget_add_css_class(w, "asuka-selectable");
	gtk_widget_set_focusable(w, 1);
	gtk_widget_set_focus_on_click(w, 0);  // the drag grabs it when it selects
	s->drag = gtk_gesture_drag_new();
	gtk_gesture_single_set_button((GtkGestureSingle *)s->drag, 0);  // the primary and the secondary
	gtk_event_controller_set_propagation_phase((GtkEventController *)s->drag, GTK_PHASE_CAPTURE);
	lx_connect(n, s->drag, "drag-begin", G_CALLBACK(on_sel_begin));
	lx_connect(n, s->drag, "drag-update", G_CALLBACK(on_sel_update));
	lx_connect(n, s->drag, "drag-end", G_CALLBACK(on_sel_end));
	gtk_widget_add_controller(w, (GtkEventController *)s->drag);
	// A touchscreen: a long press selects the word.
	GtkGesture *lng = gtk_gesture_long_press_new();
	gtk_gesture_single_set_touch_only((GtkGestureSingle *)lng, 1);
	lx_connect(n, lng, "pressed", G_CALLBACK(on_sel_long));
	gtk_widget_add_controller(w, (GtkEventController *)lng);
	// The selection's color follows the focus.
	GtkEventController *focus = gtk_event_controller_focus_new();
	lx_connect(n, focus, "enter", G_CALLBACK(on_sel_focus));
	lx_connect(n, focus, "leave", G_CALLBACK(on_sel_focus));
	gtk_widget_add_controller(w, focus);
	// Its actions: the menu's, and a screen reader's.
	GSimpleActionGroup *group = g_simple_action_group_new();
	s->copy = g_simple_action_new("copy", NULL);
	lx_connect(n, s->copy, "activate", G_CALLBACK(on_sel_copy));
	g_simple_action_set_enabled(s->copy, 0);
	g_action_map_add_action((GActionMap *)group, (GAction *)s->copy);
	g_object_unref(s->copy);
	GSimpleAction *all = g_simple_action_new("select-all", NULL);
	lx_connect(n, all, "activate", G_CALLBACK(on_sel_all));
	g_action_map_add_action((GActionMap *)group, (GAction *)all);
	g_object_unref(all);
	gtk_widget_insert_action_group(w, "selection", (GActionGroup *)group);  // the view holds it
	g_object_unref(group);
	GtkEventController *keys = gtk_event_controller_key_new();
	lx_connect(n, keys, "key-pressed", G_CALLBACK(on_sel_key));
	gtk_widget_add_controller(w, keys);
	return true;
}

// draw_selection draws a text's selected part under its label: the boxes
// of its lines.
static bool rich_draw(node *n, GtkSnapshot *snap, const graphene_rect_t *bounds) {
	(void)bounds;
	rich_text *r = rich_of(n);
	if (!r || !r->has_sel || r->sel[0] >= r->sel[1] || !n->inner) return false;
	PangoLayout *l = gtk_label_get_layout((GtkLabel *)n->inner);
	if (!l) return false;
	graphene_rect_t b;
	gtk_widget_compute_bounds(n->inner, n->widget, &b);
	int ox, oy;
	gtk_label_get_layout_offsets((GtkLabel *)n->inner, &ox, &oy);
	GdkRGBA c = H.theme.fg;
	c.alpha *= 0.1f;  // libadwaita's selection: view_fg_color at 10%
	for (node *p = n->parent; p; p = p->parent)
		if (p->kind == AK_KIND_SELECTABLE) {
			if (gtk_widget_has_focus(p->widget)) {
				c = H.theme.accent_bg;
				c.alpha *= 0.3f;  // focused: accent_bg_color at 30%
			}
			break;
		}
	int ranges[2] = {byte_at(r->text, r->sel[0]), byte_at(r->text, r->sel[1])};
	cairo_region_t *region = gdk_pango_layout_get_clip_region(l, 0, 0, ranges, 1);
	if (!region) return false;
	for (int i = 0, k = cairo_region_num_rectangles(region); i < k; i++) {
		cairo_rectangle_int_t rc;
		cairo_region_get_rectangle(region, i, &rc);
		graphene_rect_t box = {{b.origin.x + (float)(ox + rc.x), b.origin.y + (float)(oy + rc.y)}, {(float)rc.width, (float)rc.height}};
		gtk_snapshot_append_color(snap, &c, &box);
	}
	cairo_region_destroy(region);
	return false;
}

static bool rich_area_create(node *n) { return rich_create(n) || selectable_create(n); }

const area lx_area_rich = {
	.name = "rich",
	.create = rich_area_create,
	.prop = rich_prop,
	.text = rich_text_changed,
	.measure = rich_measure,
	.draw = rich_draw,
};
