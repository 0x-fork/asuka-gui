// list.c: List (ui/list.go): a for backed by the platform's recycling
// list (UICollectionView, RecyclerView). Rows are made only for the
// cells on screen, when the host binds them (ListBind, from its data
// source, on the main thread); a recycled cell rebinds its row to another
// item, which updates only the props that differ.
//
// Changes to the items send ListUpdate: an ID per item (stable while its
// key lasts), which the host diffs. Bound rows whose item changed are
// updated in place. Row heights come from the core's layout of each row
// at the list's width (a grid's column's): a cell's SetFrame carries its
// row's height.
//
// Deferred (ui.h): sections and their headers, swipe actions, selection.
#include "ui/ui_int.h"

// A bound cell: the host's view, and the row built in it.
typedef struct {
	ak_view *el;  // retained
	ak_row row;   // its key, cells, owner (its view is el's kid)
	float width;  // as last laid out
} lcell;

// An item's ID, by key.
typedef struct {
	ak_val key;
	uint32_t hash;  // 0: empty
	int64_t id;
	uint32_t index;
} lid;

typedef struct {
	ak_view *e;  // the List
	ak_closure *items, *key, *row;
	ak_kind key_kind;
	ak_owner *owner;   // what the List was made under: its rows' owners' parent
	ak_arr *list;      // the items, retained
	lid *ids, *next_ids;
	uint32_t ids_cap, next_cap;
	int64_t next;      // the last ID made
	AK_VEC(lcell *) cells;  // bound, in the order they were
	AK_VEC(int64_t) order;  // the items' IDs, in order (scratch)
	int64_t end_at;    // the length the end was last reached at
	ak_val start_key;  // the first item the start was last reached at
	bool started;
	bool refresh_shown;
	float inset;  // an inset grouped list's, each side
	int columns;
	ak_path *path;
	ak_comp_info info;
} list_t;

// ---- The class ----

static void list_adjust(ak_view *e, ak_lstyle *ls) {
	// A List fills the space it's given: with no height and no grow, it
	// grows.
	AK_UNUSED(e);
	if (ak_llength_is_auto(ls->height) && ls->flex_grow == 0) ls->flex_grow = 1;
}

static void list_init(ak_view *e) {
	list_t *st = ak_alloc(sizeof *st);
	e->impl = st;
	st->e = e;
	st->end_at = -1;
	st->columns = 1;
	ak_vec_push(&e->r->lists, (ak_view *)ak_retain(e));
	ak_style_relayout(e);  // its adjust
}

static void free_ids(lid *t, uint32_t cap, ak_kind k) {
	for (uint32_t i = 0; t && i < cap; i++)
		if (t[i].hash) ak_release_val(t[i].key, k);
}

static void list_drop(ak_view *e) {
	list_t *st = e->impl;
	if (!st) return;
	ak_free(st);
}

static int64_t key_int(const ak_view *e, uint16_t key, int64_t def) {
	const ak_val *v = ak_style_get(e, key);
	return v ? v->i : def;
}

static void list_set(ak_view *e, uint16_t key, ak_val v, const ak_val *prev) {
	list_t *st = e->impl;
	ak_batch *b = e->r->batch;
	switch (key) {
	case AK_KEY_LIST_REFRESHING: ak_refresh_set(e, &st->refresh_shown, AK_PROP_LIST_REFRESHING, v); break;
	case AK_KEY_LIST_COLUMNS:
		st->columns = v.i > 1 ? (int)v.i : 1;
		if (e->finalized && (prev || v.i > 1)) ak_batch_set_int(b, e->id, AK_PROP_LIST_COLUMNS, v.i);
		break;
	case AK_KEY_LIST_APPEARANCE:
	case AK_KEY_LIST_ANCHOR:
		if (e->finalized) ak_batch_set_enum(b, e->id, key == AK_KEY_LIST_ANCHOR ? AK_PROP_LIST_ANCHOR : AK_PROP_LIST_APPEARANCE, (int32_t)v.i);
		break;
	}
}

static void list_styled(ak_view *e, uint16_t key) {
	list_t *st = e->impl;
	if (key == AK_KEY_GAP && e->finalized && st->columns > 1) ak_batch_set_float(e->r->batch, e->id, AK_PROP_LIST_GAP, ak_style_float(e, AK_KEY_GAP, 0));
}

// list_inset is an inset grouped list's rows' inset from each side: the
// platform's margins (UIKit's layout margins, Material's window margins,
// System Settings' grouped forms).
static float list_inset(const ak_environment *env) {
	bool wide = env->width_class != AK_SIZE_CLASS_COMPACT;
	switch (env->platform) {
	case AK_PLATFORM_IOS:
		if (wide) return 20;
		break;
	case AK_PLATFORM_ANDROID:
		if (wide) return 24;
		break;
	case AK_PLATFORM_MACOS:
		return 20;
	}
	return 16;
}

static void inset_run(void *ctx) {
	ak_view *e = ctx;
	list_t *st = e->impl;
	ak_signal_read(&e->r->env_node);
	float in = list_inset(&e->r->env);
	if (in == st->inset) return;
	st->inset = in;  // the rows are laid out again at the new width
	ak_batch_set_float(e->r->batch, e->id, AK_PROP_LIST_INSET, in);
}

static void release_ctx(void *ctx) { ak_release(ctx); }

static const ak_comp_info inset_info = {.drop = release_ctx, .label = "List.inset"};

static void list_finalize(ak_view *e) {
	list_t *st = e->impl;
	ak_batch *b = e->r->batch;
	int64_t appearance = key_int(e, AK_KEY_LIST_APPEARANCE, 0), anchor = key_int(e, AK_KEY_LIST_ANCHOR, 0);
	if (st->columns > 1) {
		// A grid: plain, read from its top (Android's grid can't stack
		// from its end).
		ak_batch_set_int(b, e->id, AK_PROP_LIST_COLUMNS, st->columns);
		float gap = ak_style_float(e, AK_KEY_GAP, 0);
		if (gap > 0) ak_batch_set_float(b, e->id, AK_PROP_LIST_GAP, gap);
		appearance = anchor = 0;
	}
	if (anchor != AK_SCROLL_ANCHOR_TOP) ak_batch_set_enum(b, e->id, AK_PROP_LIST_ANCHOR, (int32_t)anchor);
	if (appearance != AK_LIST_APPEARANCE_PLAIN) ak_batch_set_enum(b, e->id, AK_PROP_LIST_APPEARANCE, (int32_t)appearance);
	if (appearance == AK_LIST_APPEARANCE_INSET_GROUPED) {
		ak_effect_new(inset_run, ak_retain(e), AK_EFFECT_RENDER, &inset_info);
	}
	if (ak_view_handler(e, AK_EV_LIST_REFRESH)) ak_batch_set_bool(b, e->id, AK_PROP_LIST_REFRESHABLE, true);
}

static void list_event(ak_view *e, const ak_event *ev) {
	list_t *st = e->impl;
	if (ev->kind == AK_EVENT_SCROLLED) {
		ak_val args[2] = {{.f = (float)ev->x}, {.f = (float)ev->y}};
		ak_view_call(e, AK_EV_LIST_SCROLL, args, 2);
	} else if (ev->kind == AK_EVENT_REFRESH) {
		ak_refresh_event(e, &st->refresh_shown, AK_PROP_LIST_REFRESHING, AK_KEY_LIST_REFRESHING, AK_EV_LIST_REFRESH);
	}
}

static void list_add(ak_view *e, ak_view *child) {
	AK_UNUSED(e), AK_UNUSED(child);
	ak_panic("ui: a List's rows come from a for in it");
}

const ak_el_class ak_class_list = {.name = "List", .kind = AK_KIND_LIST, .flags = AK_ELF_CHILDREN, .init = list_init, .set = list_set,
                                   .finalize = list_finalize, .event = list_event, .adjust = list_adjust, .add = list_add,
                                   .styled = list_styled, .drop = list_drop};

// ---- IDs ----

static ak_val key_of(list_t *st, ak_val item, int64_t i) {
	if (!st->key) return (ak_val){.i = i};
	return ak_call(st->key, &item, 1);
}

static lid *find_id(lid *t, uint32_t cap, ak_val k, uint32_t hash, ak_kind kind) {
	for (uint32_t i = hash & (cap - 1);; i = (i + 1) & (cap - 1)) {
		if (!t[i].hash) return &t[i];
		if (t[i].hash == hash && ak_val_eq(t[i].key, k, kind)) return &t[i];
	}
}

static uint32_t hash_of(ak_val k, ak_kind kind) {
	uint32_t h = (uint32_t)ak_val_hash(k, kind);
	return h ? h : 1;
}

// update sends the new item IDs (the host diffs them) and updates the
// rows that are bound.
static void update(list_t *st, ak_arr *list) {
	ak_view *e = st->e;
	uint32_t n = list ? list->len : 0;
	ak_kind ik = list ? ak_arr_kind(list) : AK_K_INT;
	uint32_t need = 16;
	while (need < n * 2) need *= 2;
	if (st->next_cap < need) {
		ak_free(st->next_ids);
		st->next_ids = ak_alloc(need * sizeof(lid));
		st->next_cap = need;
	} else {
		memset(st->next_ids, 0, st->next_cap * sizeof(lid));
	}
	// The IDs first: a key that throws leaves the list as it was.
	st->order.len = 0;
	ak_vec_reserve(&st->order, n);
	for (uint32_t i = 0; i < n; i++) {
		ak_val k = key_of(st, list->data[i], i);
		if (ak_error_pending()) {
			free_ids(st->next_ids, st->next_cap, st->key_kind);
			memset(st->next_ids, 0, st->next_cap * sizeof(lid));
			return;
		}
		uint32_t h = hash_of(k, st->key_kind);
		lid *old = st->ids ? find_id(st->ids, st->ids_cap, k, h, st->key_kind) : NULL;
		int64_t id = old && old->hash ? old->id : ++st->next;
		lid *slot = find_id(st->next_ids, st->next_cap, k, h, st->key_kind);
		if (slot->hash) {
			// A duplicate key gets its own ID.
			id = ++st->next;
			ak_release_val(k, st->key_kind);
		} else {
			*slot = (lid){k, h, id, i};
		}
		st->order.data[st->order.len++] = id;
	}
	ak_buf *b = ak_batch_command_begin(e->r->batch, e->id, AK_CMD_LIST_UPDATE);
	ak_buf_u32(b, n);
	for (uint32_t i = 0; i < n; i++) ak_buf_u64(b, (uint64_t)st->order.data[i]);
	ak_batch_command_end(e->r->batch);
	free_ids(st->ids, st->ids_cap, st->key_kind);
	lid *t = st->ids;
	uint32_t c = st->ids_cap;
	st->ids = st->next_ids, st->ids_cap = st->next_cap;
	st->next_ids = t, st->next_cap = c;
	if (st->next_ids) memset(st->next_ids, 0, st->next_cap * sizeof(lid));
	ak_retain(list);
	ak_release(st->list);
	st->list = list;
	for (uint32_t i = 0; i < st->cells.len; i++) {
		lcell *c = st->cells.data[i];
		lid *x = find_id(st->ids, st->ids_cap, c->row.key, c->row.hash, st->key_kind);
		if (x->hash) ak_row_update(&c->row, list->data[x->index], ik, x->index);
	}
}

typedef struct {
	list_t *st;
	ak_arr *list;
} update_ctx;

static void update_fn(void *ctx) {
	update_ctx *u = ctx;
	update(u->st, u->list);
}

static void items_run(void *ctx) {
	list_t *st = ctx;
	ak_val v = ak_call(st->items, NULL, 0);
	if (ak_error_pending()) {
		void *err = ak_error_take();
		if (ak_is_cancel(err)) ak_release(err);
		else ak_raise(err);
		return;
	}
	update_ctx u = {st, v.p};
	ak_untrack(update_fn, &u);
	ak_release(v.p);
}

static void unbind_cell(list_t *st, uint32_t i);

// The List's rows go with what owns it: their owners, and their views,
// which the hosts keep by ID until they're destroyed (the cells are the
// host's, and go with the list's view).
static void items_drop(void *ctx) {
	list_t *st = ctx;
	while (st->cells.len) unbind_cell(st, st->cells.len - 1);
	ak_vec_free(&st->cells);
	ak_vec_free(&st->order);
	free_ids(st->ids, st->ids_cap, st->key_kind);
	ak_free(st->ids);
	ak_free(st->next_ids);
	st->ids = st->next_ids = NULL;
	st->ids_cap = st->next_cap = 0;
	ak_release_val(st->start_key, st->key_kind);
	st->started = false;
	ak_release(st->list);
	st->list = NULL;
	ak_release(st->items);
	ak_release(st->key);
	ak_release(st->row);
	st->items = st->key = st->row = NULL;
	if (st->owner) ak_owner_release(st->owner);
	st->owner = NULL;
	ak_path_release(st->path);
	st->path = NULL;
	ak_release(st->e);
}

void ak_ui_lazy_for(ak_view *v, ak_closure *items, ak_closure *key, ak_closure *row, ak_kind key_kind) {
	if (v->el != AK_EL_LIST) ak_panic("ui: %s doesn't build its rows lazily", v->el ? ak_el_names[v->el] : v->cls->name);
	list_t *st = v->impl;
	if (st->items) ak_panic("ui: a List has one for");
	ak_retain(v);  // the effect's
	st->items = ak_retain(items);
	st->key = ak_retain(key);
	st->row = ak_retain(row);
	st->key_kind = key ? key_kind : AK_K_INT;
	st->owner = ak_current_owner();
	if (st->owner) ak_owner_retain(st->owner);
	st->path = ak_path_capture();
	st->info = (ak_comp_info){.drop = items_drop, .label = "List"};
	ak_effect_new(items_run, st, AK_EFFECT_RENDER, &st->info);
}

// ---- From the host ----

int64_t ak_list_count(ak_view *e) {
	list_t *st = e->impl;
	return st->list ? st->list->len : 0;
}

static void post_handler(void *ctx) {
	void **p = ctx;
	ak_view *e = p[0];
	uint16_t ev = (uint16_t)(uintptr_t)p[1];
	ak_free(p);
	if (!e->dead) {
		ak_batch_begin();
		ak_view_call(e, ev, NULL, 0);
		ak_batch_end();
		ak_ui_flush();
	}
	ak_release(e);
}

// post calls e's handler for ev soon: rows bind while the host lays out,
// not then.
static void post(ak_view *e, uint16_t ev) {
	void **p = ak_alloc(2 * sizeof(void *));
	p[0] = ak_retain(e);
	p[1] = (void *)(uintptr_t)ev;
	ak_post(post_handler, p);
}

// reached posts endReached when row i is near the end, once per length.
static void reached(list_t *st, int64_t i) {
	int64_t n = st->list ? st->list->len : 0;
	if (!ak_view_handler(st->e, AK_EV_LIST_END_REACHED) || i < n - 3 || n == st->end_at) return;
	st->end_at = n;
	post(st->e, AK_EV_LIST_END_REACHED);
}

// reached_start posts startReached when row i is near the start, once
// per first item.
static void reached_start(list_t *st, int64_t i) {
	if (!ak_view_handler(st->e, AK_EV_LIST_START_REACHED) || i > 2 || !st->list || st->list->len == 0) return;
	ak_val k = key_of(st, st->list->data[0], 0);
	if (ak_error_pending()) {
		ak_ui_raise_pending(st->owner);
		return;
	}
	if (st->started && ak_val_eq(k, st->start_key, st->key_kind)) {
		ak_release_val(k, st->key_kind);
		return;
	}
	ak_release_val(st->start_key, st->key_kind);
	st->started = true;
	st->start_key = k;
	post(st->e, AK_EV_LIST_START_REACHED);
}

static int32_t find_cell(list_t *st, ak_id cell) {
	for (uint32_t i = 0; i < st->cells.len; i++)
		if (st->cells.data[i]->el->id == cell) return (int32_t)i;
	return -1;
}

typedef struct {
	list_t *st;
	lcell *c;
	ak_val item;
	ak_kind ik;
	int64_t i;
} make_ctx;

static void make_row(void *ctx) {
	make_ctx *m = ctx;
	lcell *c = m->c;
	ak_row_make(&c->row, m->st->e->r, m->st->row, m->st->path, m->st->owner, m->item, m->ik, m->i, m->st->key_kind);
	if (c->row.el) {
		ak_view_append(c->el, c->row.el);
		ak_release(c->row.el);  // the cell holds it
	}
}

void ak_list_bind(ak_view *e, int64_t i, ak_id cell) {
	list_t *st = e->impl;
	if (i < 0) return;  // a section's header (deferred)
	if (!st->list || i >= st->list->len) return;
	ak_val item = st->list->data[i];
	ak_kind ik = ak_arr_kind(st->list);
	reached(st, i);
	reached_start(st, i);
	int32_t ci = find_cell(st, cell);
	if (ci >= 0) {
		// A recycled cell: its row takes the item, which updates only the
		// props that differ.
		lcell *c = st->cells.data[ci];
		ak_val k = key_of(st, item, i);
		if (ak_error_pending()) {
			ak_ui_raise_pending(st->owner);
			return;
		}
		ak_release_val(c->row.key, st->key_kind);
		c->row.key = k;
		c->row.hash = hash_of(k, st->key_kind);
		ak_row_update(&c->row, item, ik, i);
		return;
	}
	ak_val k = key_of(st, item, i);
	if (ak_error_pending()) {
		ak_ui_raise_pending(st->owner);
		return;
	}
	// The cell's element: the host made its view.
	ak_ui_root *r = e->r;
	lcell *c = ak_alloc(sizeof *c);
	ak_view *el = ak_view_new(r, &ak_class_cell, 0, false);
	el->real = true;
	el->cell = true;
	el->id = cell;
	el->up = e;
	el->dir = e->dir;
	ak_view_put_id(el);
	ak_style_relayout(el);  // a layout root: its direction (and as wide as the row it's laid out at)
	if (el->dir != AK_LAYOUT_DIRECTION_INHERIT) ak_batch_set_enum(r->batch, cell, AK_PROP_LAYOUT_DIRECTION, el->dir);
	el->finalized = true;
	c->el = el;
	c->row.key = k;
	c->row.hash = hash_of(k, st->key_kind);
	c->row.old = -1;
	ak_vec_push(&st->cells, c);
	// Under the List's owner: it sees what the List sees.
	make_ctx m = {st, c, item, ik, i};
	ak_ui_root *prev = ak_ui.cur;
	ak_ui.cur = r;
	make_row(&m);
	ak_ui.cur = prev;
}

static void unbind_cell(list_t *st, uint32_t i) {
	lcell *c = st->cells.data[i];
	memmove(st->cells.data + i, st->cells.data + i + 1, (st->cells.len - i - 1) * sizeof(lcell *));
	st->cells.len--;
	ak_row_free(&c->row, st->key_kind);
	ak_view *el = c->el;
	while (el->kids.len) ak_view_remove(el, el->kids.data[el->kids.len - 1], true);
	el->dead = true;  // the host's view: not destroyed by the core
	if (ak_view_of(el->id) == el) ak_view_drop_id(el->id);
	ak_release(el);
	ak_free(c);
}

void ak_list_unbind(ak_view *e, ak_id cell) {
	list_t *st = e->impl;
	int32_t i = find_cell(st, cell);
	if (i >= 0) unbind_cell(st, (uint32_t)i);
}

void ak_list_each_cell(ak_view *e, void (*f)(ak_view *c, void *ctx), void *ctx) {
	list_t *st = e->impl;
	for (uint32_t i = 0; st && i < st->cells.len; i++) f(st->cells.data[i]->el, ctx);
}

// ak_list_layout lays out the bound rows at the list's width (an inset
// grouped list's less its insets, a grid's cells at a column's); each
// cell's frame tells the host the row's height.
void ak_list_layout(ak_view *e, ak_lpass *p) {
	list_t *st = e->impl;
	if (!st || !st->cells.len) return;
	float w = ak_lnode_frame(e->box).w - 2 * st->inset;
	if (st->columns > 1) {
		float gap = ak_style_float(e, AK_KEY_GAP, 0);
		w = (ak_lnode_frame(e->box).w - gap * (float)(st->columns - 1)) / (float)st->columns;
	}
	if (w <= 0) return;
	for (uint32_t i = 0; i < st->cells.len; i++) {
		lcell *c = st->cells.data[i];
		if (ak_lnode_dirty(c->el->box) || c->width != w) {
			c->width = w;
			ak_lnode_layout(c->el->box, (ak_lsize){w, AK_LMAX_CONTENT}, p);
		}
	}
}
