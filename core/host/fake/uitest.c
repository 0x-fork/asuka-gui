// uitest.c: testing an app's UI on the fake host (uitest.h), and the
// test.* builtins `asuka test` runs test blocks with.
#include "fake/uitest.h"
#include "vm/vm.h"
#include "rt/sched.h"
#include "signal/signal.h"

#include <math.h>
#include <stdio.h>

// A List's rows, bound as a collection view binds them: those that fit
// the list's height from where it's scrolled (or back from its end, for a
// list anchored there), and a line more.
typedef struct {
	int64_t index;
	ak_fake_view *cell;
} bound;

typedef struct {
	ak_id list;
	AK_VEC(bound) cells;  // by index, sorted
	ak_fake_view_vec free;
	int64_t first;
	bool end;
	bool seen;
	AK_VEC(int64_t) items;  // the list's items as last bound
	int64_t count;
} list_state;

struct ak_uitest {
	ak_opaque h;
	ak_fake *f;
	bool mounted;
	ak_fake_view *focused;  // the field tapped or typed in last
	ak_fake_view *menu;     // the button whose menu is open
	AK_VEC(list_state) lists;
	ak_buf fails;  // the fake host's misuses
};

// The cap on a list's bound rows: rows of no height would bind them all.
#define MAX_BOUND 200

static ak_uitest *active;  // the app posts wake

static ak_span sp(const char *s) { return ak_cstr(s); }

// ---- The fake host, connected ----

static void ev_cb(void *ctx, const uint8_t *buf, size_t len) {
	AK_UNUSED(ctx);
	ak_ui_dispatch(buf, len);
}
static int64_t count_cb(void *ctx, ak_id list) {
	AK_UNUSED(ctx);
	return ak_ui_list_count(list);
}
static void bind_cb(void *ctx, ak_id list, int64_t index, ak_id cell) {
	AK_UNUSED(ctx);
	ak_ui_list_bind(list, index, cell);
}
static void unbind_cb(void *ctx, ak_id list, ak_id cell) {
	AK_UNUSED(ctx);
	ak_ui_list_unbind(list, cell);
}
static void posted_cb(void *ctx) {
	AK_UNUSED(ctx);
	ak_ui_run_posted();
}
static void apply_cb(void *ctx, const uint8_t *buf, size_t len) { ak_fake_apply(ctx, buf, len); }
static void measure_cb(void *ctx, uint32_t id, float w, float h, float aw, float ah, float *ow, float *oh) {
	ak_fake_measure(ctx, id, w, h, aw, ah, ow, oh);
}
static void fail_cb(void *ctx, const char *msg) {
	ak_buf *b = ctx;
	ak_buf_put(b, msg, strlen(msg));
	ak_buf_u8(b, '\n');
}
static void wake(void) {
	if (active) ak_fake_post(active->f);
}

static void free_lists(ak_uitest *a) {
	for (uint32_t i = 0; i < a->lists.len; i++) {
		ak_vec_free(&a->lists.data[i].cells);
		ak_vec_free(&a->lists.data[i].free);
		ak_vec_free(&a->lists.data[i].items);
	}
	ak_vec_free(&a->lists);
}

static void uitest_drop(ak_opaque *o) {
	ak_uitest *a = (ak_uitest *)o;
	if (a->mounted) ak_uitest_unmount(a, NULL);
	free_lists(a);
	ak_fake_free(a->f);
	ak_buf_free(&a->fails);
	if (active == a) active = NULL;
}

static uint8_t default_platform = AK_PLATFORM_IOS;

void ak_uitest_set_platform(uint8_t platform) { default_platform = platform; }

ak_uitest *ak_uitest_mount(ak_closure *build, const ak_environment *env) {
	if (active && active->mounted) ak_uitest_unmount(active, NULL);
	ak_uitest *a = ak_opaque_new(AK_TAG_OPAQUE, sizeof(ak_uitest), uitest_drop);
	a->f = ak_fake_new();
	ak_fake *f = a->f;
	if (env) f->env = *env;
	else f->env.platform = default_platform;
	f->core = (ak_fake_core){a, ev_cb, count_cb, bind_cb, unbind_cb, posted_cb};
	f->fail_fn = fail_cb;
	f->fail_ctx = &a->fails;
	active = a;
	ak_sched_set_wake(wake);
	ak_ui_init(&(ak_host){f, apply_cb, measure_cb});
	ak_ui_set_environment(&f->env);
	ak_ui_mount(build);
	a->mounted = true;
	ak_uitest_settle(a);
	return a;
}

bool ak_uitest_unmount(ak_uitest *a, ak_buf *err) {
	if (!a->mounted) return true;
	a->mounted = false;
	ak_ui_unmount();
	ak_fake_run_posted(a->f);
	// Every view the core made goes with the app.
	ak_fake_view_vec left = {0};
	ak_fake *f = a->f;
	for (uint32_t i = 0; i < f->slots_cap; i++) {
		ak_fake_view *v = f->slots[i];
		if (v && !(v->id & AK_HOST_ID_BIT)) ak_vec_push(&left, v);
	}
	bool ok = left.len == 0;
	if (!ok && err) {
		ak_buf_printf(err, "%u views outlived the app (an owner left undisposed):\n", left.len);
		for (uint32_t i = 0; i < left.len; i++) {
			ak_buf_put(err, "  ", 2);
			ak_fake_kind_name(err, left.data[i]->kind);
			ak_buf_printf(err, " %u\n", left.data[i]->id);
		}
	}
	ak_vec_free(&left);
	return ok;
}

ak_fake *ak_uitest_host(ak_uitest *a) { return a->f; }

// ---- What the screen shows ----

static bool hidden(const ak_fake_view *v) { return ak_fake_bool(v, AK_PROP_HIDDEN); }

static list_state *list_of(ak_uitest *a, ak_id id) {
	for (uint32_t i = 0; i < a->lists.len; i++)
		if (a->lists.data[i].list == id) return &a->lists.data[i];
	return NULL;
}

typedef bool (*walk_fn)(ak_uitest *a, ak_fake_view *v, void *ctx);

// walk calls f on each view under v that shows, depth first, parents
// first; f returning false skips the view's children.
static void walk(ak_uitest *a, ak_fake_view *v, walk_fn f, void *ctx) {
	if (hidden(v) || !f(a, v, ctx)) return;
	for (uint32_t i = 0; i < v->children.len; i++) walk(a, v->children.data[i], f, ctx);
	if (v->kind == AK_KIND_LIST) {
		list_state *st = list_of(a, v->id);
		for (uint32_t i = 0; st && i < st->cells.len; i++) walk(a, st->cells.data[i].cell, f, ctx);
	}
}

// layers are the root and the views presented over it, bottom to top.
static void layers(ak_uitest *a, ak_fake_view_vec *out) {
	ak_fake_view *r = ak_fake_root(a->f);
	if (r) ak_vec_push(out, r);
	ak_fake_presented(a->f, out);
}

static bool collect_fn(ak_uitest *a, ak_fake_view *v, void *ctx) {
	AK_UNUSED(a);
	ak_vec_push((ak_fake_view_vec *)ctx, v);
	return true;
}

static void visible(ak_uitest *a, ak_fake_view_vec *out) {
	ak_fake_view_vec ls = {0};
	layers(a, &ls);
	for (uint32_t i = 0; i < ls.len; i++) walk(a, ls.data[i], collect_fn, out);
	ak_vec_free(&ls);
}

// shows calls f with each text v shows of its own (not its children's).
static void shows(const ak_fake_view *v, void (*f)(ak_span s, void *ctx), void *ctx) {
	ak_span s;
#define ADD(x) do { s = (x); if (s.len) f(s, ctx); } while (0)
	switch (v->kind) {
	case AK_KIND_TEXT: ADD(ak_fake_string(v, AK_PROP_TEXT_TEXT)); break;
	case AK_KIND_BUTTON: ADD(ak_fake_string(v, AK_PROP_BUTTON_TITLE)); break;
	case AK_KIND_TEXT_FIELD:
		ADD(ak_fake_string(v, AK_PROP_TEXT_FIELD_VALUE));
		ADD(ak_fake_string(v, AK_PROP_TEXT_FIELD_PLACEHOLDER));
		break;
	case AK_KIND_PICKER: {
		ak_str_vec items = {0};
		ak_fake_strings(v, AK_PROP_PICKER_ITEMS, &items);
		int64_t i = ak_fake_int(v, AK_PROP_PICKER_SELECTED);
		if (i >= 0 && i < items.len) ADD(items.data[i]);
		else ADD(ak_fake_string(v, AK_PROP_PICKER_LABEL));
		ak_vec_free(&items);
		break;
	}
	}
	ADD(ak_fake_string(v, AK_PROP_A11Y_LABEL));
#undef ADD
}

typedef struct {
	ak_span text;
	bool found;
} has_ctx;

static void has_fn(ak_span s, void *ctx) {
	has_ctx *h = ctx;
	if (ak_span_eq(s, h->text)) h->found = true;
}

static bool shows_text(const ak_fake_view *v, ak_span text) {
	has_ctx h = {text, false};
	shows(v, has_fn, &h);
	return h.found;
}

// A target is what a text names: a view showing it, or a segment.
enum { BY_VIEW, BY_SEGMENT };

typedef struct {
	ak_fake_view *v;
	int how;
	int64_t i;
} target;

typedef AK_VEC(target) target_vec;

static void targets(ak_uitest *a, ak_span text, target_vec *out) {
	ak_fake_view_vec vs = {0};
	visible(a, &vs);
	for (uint32_t i = 0; i < vs.len; i++) {
		ak_fake_view *v = vs.data[i];
		if (shows_text(v, text)) ak_vec_push(out, ((target){v, BY_VIEW, 0}));
		if (v->kind == AK_KIND_SEGMENTED) {
			ak_str_vec segs = {0};
			ak_fake_strings(v, AK_PROP_SEGMENTED_SEGMENTS, &segs);
			for (uint32_t j = 0; j < segs.len; j++)
				if (ak_span_eq(segs.data[j], text)) ak_vec_push(out, ((target){v, BY_SEGMENT, j}));
			ak_vec_free(&segs);
		}
	}
	ak_vec_free(&vs);
}

static bool menu_has(ak_uitest *a, ak_span text) {
	if (!a->menu) return false;
	ak_action_vec items = {0};
	ak_fake_action_items(a->menu, AK_PROP_MENU, &items);
	bool found = ak_action_named(items.data, items.len, text) >= 0;
	ak_vec_free(&items);
	return found;
}

bool ak_uitest_has(ak_uitest *a, ak_span text) {
	if (menu_has(a, text)) return true;
	target_vec ts = {0};
	targets(a, text, &ts);
	bool found = ts.len > 0;
	ak_vec_free(&ts);
	return found;
}

// ---- The snapshot ----

static void num(ak_buf *b, float f) {
	double r = round((double)f * 100) / 100;
	ak_fmt_float(b, r, 64);
}

static void quote_fn(ak_span s, void *ctx) {
	ak_buf *b = ctx;
	ak_buf_u8(b, ' ');
	ak_fmt_quote(b, s);
}

static void describe(ak_uitest *a, ak_buf *b, ak_fake_view *v, int depth) {
	if (hidden(v)) return;
	for (int i = 0; i < depth; i++) ak_buf_put(b, "  ", 2);
	ak_fake_kind_name(b, v->kind);
	ak_buf_u8(b, ' ');
	num(b, v->frame[0]);
	ak_buf_u8(b, ',');
	num(b, v->frame[1]);
	ak_buf_u8(b, ' ');
	num(b, v->frame[2]);
	ak_buf_u8(b, 'x');
	num(b, v->frame[3]);
	shows(v, quote_fn, b);
	if (v->kind == AK_KIND_SEGMENTED) {
		ak_str_vec segs = {0};
		ak_fake_strings(v, AK_PROP_SEGMENTED_SEGMENTS, &segs);
		for (uint32_t j = 0; j < segs.len; j++) quote_fn(segs.data[j], b);
		ak_vec_free(&segs);
	}
	switch (v->kind) {
	case AK_KIND_TOGGLE: ak_buf_printf(b, ak_fake_bool(v, AK_PROP_TOGGLE_ON) ? " on" : " off"); break;
	case AK_KIND_SLIDER: ak_buf_put(b, " =", 2), ak_fmt_float(b, ak_fake_float(v, AK_PROP_SLIDER_VALUE), 64); break;
	case AK_KIND_STEPPER: ak_buf_put(b, " =", 2), ak_fmt_float(b, ak_fake_float(v, AK_PROP_STEPPER_VALUE), 64); break;
	case AK_KIND_PROGRESS:
		if (ak_fake_bool(v, AK_PROP_PROGRESS_INDETERMINATE)) ak_buf_printf(b, " indeterminate");
		else ak_buf_put(b, " =", 2), ak_fmt_float(b, ak_fake_float(v, AK_PROP_PROGRESS_VALUE), 64);
		break;
	case AK_KIND_SEGMENTED: ak_buf_printf(b, " selected=%lld", (long long)ak_fake_int(v, AK_PROP_SEGMENTED_SELECTED)); break;
	case AK_KIND_TEXT_FIELD:
		if (ak_fake_bool(v, AK_PROP_TEXT_FIELD_SECURE)) ak_buf_printf(b, " secure");
		break;
	}
	if (ak_fake_bool(v, AK_PROP_SELECTED)) ak_buf_printf(b, " selected");
	if (ak_fake_bool(v, AK_PROP_DISABLED)) ak_buf_printf(b, " disabled");
	ak_buf_u8(b, '\n');
	for (uint32_t i = 0; i < v->children.len; i++) describe(a, b, v->children.data[i], depth + 1);
	if (v->kind == AK_KIND_LIST) {
		list_state *st = list_of(a, v->id);
		for (uint32_t i = 0; st && i < st->cells.len; i++) describe(a, b, st->cells.data[i].cell, depth + 1);
	}
}

void ak_uitest_snapshot(ak_uitest *a, ak_buf *out) {
	ak_fake_view_vec ls = {0};
	layers(a, &ls);
	for (uint32_t i = 0; i < ls.len; i++) {
		if (i) ak_buf_u8(out, '\n');
		describe(a, out, ls.data[i], 0);
	}
	ak_vec_free(&ls);
	if (a->menu) {
		ak_buf_printf(out, "\nMenu\n");
		ak_action_vec items = {0};
		ak_fake_action_items(a->menu, AK_PROP_MENU, &items);
		for (uint32_t i = 0; i < items.len; i++) {
			for (uint32_t d = 0; d <= items.data[i].depth; d++) ak_buf_put(out, "  ", 2);
			ak_fmt_quote(out, items.data[i].title);
			if (items.data[i].flags & AK_MENU_CHECKED) ak_buf_printf(out, " checked");
			if (items.data[i].flags & AK_MENU_DISABLED) ak_buf_printf(out, " disabled");
			ak_buf_u8(out, '\n');
		}
		ak_vec_free(&items);
	}
}

static bool failure(ak_uitest *a, ak_buf *err, const char *fmt, ak_span text) {
	if (err) {
		ak_buf_printf(err, fmt, (int)text.len, text.p);
		ak_buf_printf(err, "\nthe screen:\n");
		ak_uitest_snapshot(a, err);
	}
	return false;
}

// ---- Lists ----

static bool items_equal(const list_state *st, const ak_fake_view *list) {
	if (st->items.len != list->items.len) return false;
	return st->items.len == 0 || memcmp(st->items.data, list->items.data, st->items.len * sizeof(int64_t)) == 0;
}

static int32_t cell_at(const list_state *st, int64_t i) {
	for (uint32_t j = 0; j < st->cells.len; j++)
		if (st->cells.data[j].index == i) return (int32_t)j;
	return -1;
}

static void insert_bound(list_state *st, int64_t i, ak_fake_view *c) {
	uint32_t at = 0;
	while (at < st->cells.len && st->cells.data[at].index < i) at++;
	ak_vec_reserve(&st->cells, 1);
	memmove(st->cells.data + at + 1, st->cells.data + at, (st->cells.len - at) * sizeof(bound));
	st->cells.data[at] = (bound){i, c};
	st->cells.len++;
}

typedef struct {
	ak_uitest *a;
	list_state *st;
	ak_fake_view *list;
	bool stale, changed;
} bind_ctx;

static float bind_row(bind_ctx *b, int64_t i) {
	list_state *st = b->st;
	int32_t j = cell_at(st, i);
	ak_fake_view *c;
	if (j < 0) {
		c = st->free.len ? ak_vec_pop(&st->free) : ak_fake_new_cell(b->a->f);
		insert_bound(st, i, c);
		ak_fake_bind(b->a->f, b->list, i, c);
		b->changed = true;
	} else {
		c = st->cells.data[j].cell;
		if (b->stale) ak_fake_bind(b->a->f, b->list, i, c);
	}
	return c->frame[3];
}

static bool sync_list(ak_uitest *a, ak_fake_view *list) {
	list_state *st = list_of(a, list->id);
	if (!st) {
		ak_vec_push(&a->lists, ((list_state){.list = list->id}));
		st = &a->lists.data[a->lists.len - 1];
		st->end = ak_fake_int(list, AK_PROP_LIST_ANCHOR) == AK_SCROLL_ANCHOR_BOTTOM;
	}
	st->seen = true;
	int64_t count = ak_fake_list_count(a->f, list);
	bind_ctx b = {a, st, list, count != st->count || !items_equal(st, list), false};
	st->count = count;
	st->items.len = 0;
	for (uint32_t i = 0; i < list->items.len; i++) ak_vec_push(&st->items, list->items.data[i]);
	float height = list->frame[3] > 0 ? list->frame[3] : a->f->env.height;
	int64_t cols = ak_fake_int(list, AK_PROP_LIST_COLUMNS);
	if (cols < 1) cols = 1;
	int64_t lo, hi;
	if (st->end) {
		hi = count;
		lo = hi;
		for (float filled = 0; lo > 0 && filled <= height && hi - lo < MAX_BOUND;) {
			int64_t line = lo - 1 - (lo - 1) % cols;
			float h = 0;
			for (int64_t i = line; i < lo; i++) h = ak_maxf(h, bind_row(&b, i));
			filled += h;
			lo = line;
		}
	} else {
		lo = st->first < count - 1 ? st->first : (count - 1 > 0 ? count - 1 : 0);
		lo -= lo % cols;
		hi = lo;
		for (float filled = 0; hi < count && filled <= height && hi - lo < MAX_BOUND;) {
			float h = 0;
			int64_t end = hi + cols < count ? hi + cols : count;
			for (int64_t i = hi; i < end; i++) h = ak_maxf(h, bind_row(&b, i));
			filled += h;
			hi = end;
		}
	}
	for (uint32_t j = 0; j < st->cells.len;) {
		bound c = st->cells.data[j];
		if (c.index < lo || c.index >= hi) {
			ak_fake_unbind(a->f, list, c.cell);
			memmove(st->cells.data + j, st->cells.data + j + 1, (st->cells.len - j - 1) * sizeof(bound));
			st->cells.len--;
			ak_vec_push(&st->free, c.cell);
			b.changed = true;
			continue;
		}
		j++;
	}
	return b.changed;
}

static bool lists_fn(ak_uitest *a, ak_fake_view *v, void *ctx) {
	if (v->kind == AK_KIND_LIST && !list_of(a, v->id)) {
		ak_vec_push(&a->lists, ((list_state){.list = v->id, .end = ak_fake_int(v, AK_PROP_LIST_ANCHOR) == AK_SCROLL_ANCHOR_BOTTOM}));
	}
	if (v->kind == AK_KIND_LIST) {
		list_state *st = list_of(a, v->id);
		if (!st->seen && sync_list(a, v)) *(bool *)ctx = true;
	}
	return true;
}

// sync_lists binds what each visible list shows, and lets go of the
// lists that went; it reports whether it bound anything.
static bool sync_lists(ak_uitest *a) {
	for (uint32_t i = 0; i < a->lists.len; i++) a->lists.data[i].seen = false;
	bool changed = false;
	ak_fake_view_vec ls = {0};
	layers(a, &ls);
	for (uint32_t i = 0; i < ls.len; i++) walk(a, ls.data[i], lists_fn, &changed);
	ak_vec_free(&ls);
	for (uint32_t i = 0; i < a->lists.len;) {
		list_state *st = &a->lists.data[i];
		if (st->seen || ak_fake_view_of(a->f, st->list)) {  // hidden: kept, as a collection view keeps its cells
			i++;
			continue;
		}
		ak_vec_free(&st->cells);
		ak_vec_free(&st->free);
		ak_vec_free(&st->items);
		a->lists.data[i] = a->lists.data[--a->lists.len];
	}
	return changed;
}

void ak_uitest_settle(ak_uitest *a) {
	for (int round = 0; round < 1000; round++) {
		ak_fake_run_posted(a->f);
		bool changed = false;
		if (ak_sched_has_ready()) {
			ak_ui_run_posted();
			changed = true;
		}
		ak_ui_flush();
		changed = sync_lists(a) || changed;
		if (!changed && ak_fake_pending(a->f) == 0 && ak_sched_held()) {
			// Another thread works for the app (spawn, a database's
			// statement): wait for what it posts back.
			ak_sched_wait_posted();
			ak_sched_run_posted();
			continue;
		}
		if (!changed && ak_fake_pending(a->f) == 0) {
			if (a->focused && ak_fake_view_of(a->f, a->focused->id) != a->focused) a->focused = NULL;
			if (a->menu && ak_fake_view_of(a->f, a->menu->id) != a->menu) a->menu = NULL;
			return;
		}
	}
	ak_buf_printf(&a->fails, "the app never settles (it posts or rebinds forever)\n");
}

// ak_uitest_idle settles, then moves the clock (a manual one, under
// test) to each timer that's due and settles again, until no timer is
// left. Ten minutes of the app's time is the most it moves: a timer that
// keeps firing past that (a ticker) is a failure.
void ak_uitest_idle(ak_uitest *a) {
	int64_t start = ak_sched_now();
	for (;;) {
		ak_uitest_settle(a);
		int64_t at;
		if (!ak_sched_next_timer(&at)) return;
		if (at - start > 600 * (int64_t)1000000000) {
			ak_buf_printf(&a->fails, "the app never idles: a timer keeps firing (a ticker?)\n");
			return;
		}
		int64_t now = ak_sched_now();
		ak_sched_advance(at > now ? at - now : 0);
	}
}

// ---- Actions ----

static bool disabled(const ak_fake_view *v) {
	for (const ak_fake_view *p = v; p; p = p->parent)
		if (ak_fake_bool(p, AK_PROP_DISABLED)) return true;
	return false;
}

static bool has_menu(const ak_fake_view *v) { return ak_fake_prop_of(v, AK_PROP_MENU) != NULL && ak_fake_int(v, AK_PROP_MENU_TRIGGER) == AK_MENU_TRIGGER_TAP; }

// pressable is the view a touch on v goes to: v or the nearest around it
// that takes it; NULL if none does.
static ak_fake_view *pressable(ak_fake_view *v) {
	ak_fake_view *idle = NULL;  // a button that does nothing
	for (ak_fake_view *p = v; p; p = p->parent) {
		if (ak_fake_listens(p, AK_EVENT_PRESS) || has_menu(p)) return p;
		if (p->kind == AK_KIND_TOGGLE || p->kind == AK_KIND_TEXT_FIELD) return p;
		if (p->kind == AK_KIND_BUTTON && !idle) idle = p;
		if (p->kind == AK_FAKE_KIND_CELL && ak_fake_listens(p, AK_EVENT_ROW_SELECTED)) return p;
	}
	return idle;
}

static bool check_fails(ak_uitest *a, ak_buf *err) {
	if (!a->fails.len) return true;
	if (err) ak_buf_put(err, a->fails.data, a->fails.len);
	ak_buf_reset(&a->fails);
	return false;
}

bool ak_uitest_tap(ak_uitest *a, ak_span text, ak_buf *err) {
	if (a->menu) {
		int64_t i = -1;
		ak_action_vec items = {0};
		ak_fake_action_items(a->menu, AK_PROP_MENU, &items);
		i = ak_action_named(items.data, items.len, text);
		bool off = i >= 0 && (items.data[i].flags & AK_MENU_DISABLED);
		ak_vec_free(&items);
		if (i < 0) return failure(a, err, "the open menu has no %.*s", text);
		if (off) return failure(a, err, "the menu's %.*s is disabled", text);
		ak_fake_view *m = a->menu;
		a->menu = NULL;
		ak_fake_choose(a->f, m, i);
		ak_uitest_settle(a);
		return check_fails(a, err);
	}
	target_vec ts = {0};
	targets(a, text, &ts);
	// A view inside a pressable one names it once: a Text in a Button, in
	// a row.
	target_vec found = {0};
	for (uint32_t i = 0; i < ts.len; i++) {
		target t = ts.data[i];
		ak_fake_view *key = t.how == BY_VIEW ? pressable(t.v) : t.v;
		if (!key) key = t.v;
		bool dup = false;
		for (uint32_t j = 0; j < found.len; j++) {
			ak_fake_view *k2 = found.data[j].how == BY_VIEW ? pressable(found.data[j].v) : found.data[j].v;
			if (!k2) k2 = found.data[j].v;
			if (k2 == key && found.data[j].i == t.i) dup = true;
		}
		if (!dup) ak_vec_push(&found, t);
	}
	ak_vec_free(&ts);
	if (found.len != 1) {
		uint32_t n = found.len;
		ak_vec_free(&found);
		if (n == 0) return failure(a, err, "the screen doesn't show \"%.*s\"", text);
		if (err) ak_buf_printf(err, "%u things show ", n);
		return failure(a, err, "\"%.*s\"", text);
	}
	target t = found.data[0];
	ak_vec_free(&found);
	if (t.how == BY_SEGMENT) {
		if (disabled(t.v)) return failure(a, err, "\"%.*s\" is disabled", text);
		ak_fake_select(a->f, t.v, t.i);
		ak_uitest_settle(a);
		return check_fails(a, err);
	}
	ak_fake_view *p = pressable(t.v);
	if (!p) return failure(a, err, "nothing at \"%.*s\" takes a tap", text);
	if (disabled(p)) return failure(a, err, "\"%.*s\" is disabled", text);
	if (p->kind == AK_KIND_TEXT_FIELD) a->focused = p;
	else if (p->kind == AK_KIND_TOGGLE) ak_fake_flip(a->f, p);
	else if (has_menu(p) && !ak_fake_listens(p, AK_EVENT_PRESS)) a->menu = p;
	else if (p->kind == AK_FAKE_KIND_CELL) ak_fake_select_row(a->f, p, true);
	else if (ak_fake_listens(p, AK_EVENT_PRESS)) ak_fake_tap(a->f, p);
	ak_uitest_settle(a);
	return check_fails(a, err);
}

static bool contains(const ak_fake_view *u, const ak_fake_view *v) {
	for (const ak_fake_view *p = v; p; p = p->parent)
		if (p == u) return true;
	return false;
}

// near finds the field label names: one that shows it (its placeholder,
// text or label); else the one in a view that says label (a view
// labelled for the field it holds); else the first after a text saying
// it, in the order a screen reader reads.
static ak_fake_view *near(ak_uitest *a, ak_span label, int *n) {
	ak_fake_view_vec vs = {0};
	visible(a, &vs);
	ak_fake_view *found = NULL;
	*n = 0;
	for (uint32_t i = 0; i < vs.len; i++) {
		ak_fake_view *v = vs.data[i];
		if (v->kind == AK_KIND_TEXT_FIELD && shows_text(v, label)) {
			if (found != v) (*n)++;
			found = v;
		}
	}
	if (!found) {
		for (uint32_t i = 0; i < vs.len; i++) {
			ak_fake_view *v = vs.data[i];
			if (!shows_text(v, label)) continue;
			ak_fake_view *in = NULL;
			int inside = 0;
			for (uint32_t j = 0; j < vs.len; j++) {
				if (vs.data[j]->kind == AK_KIND_TEXT_FIELD && vs.data[j] != v && contains(v, vs.data[j])) {
					in = vs.data[j];
					inside++;
				}
			}
			ak_fake_view *c = inside == 1 ? in : NULL;
			for (uint32_t j = i + 1; !c && j < vs.len; j++)
				if (vs.data[j]->kind == AK_KIND_TEXT_FIELD) c = vs.data[j];
			if (c && c != found) {
				found = c;
				(*n)++;
			}
		}
	}
	ak_vec_free(&vs);
	return found;
}

bool ak_uitest_type(ak_uitest *a, ak_span text, ak_span label, ak_buf *err) {
	ak_fake_view *f = a->focused;
	if (label.len) {
		int n;
		f = near(a, label, &n);
		if (!f) return failure(a, err, "no TextField next to \"%.*s\"", label);
		if (n > 1) return failure(a, err, "more than one TextField next to \"%.*s\"", label);
	} else if (!f) {
		return failure(a, err, "type: no field to type in%.*s; tap one, or name it", sp(""));
	}
	if (disabled(f)) return failure(a, err, "the field \"%.*s\" is disabled", label);
	a->focused = f;
	ak_buf s = {0};
	ak_buf_put(&s, text.p, text.len);
	ak_buf_u8(&s, 0);
	ak_fake_type(a->f, f, (const char *)s.data);
	ak_buf_free(&s);
	ak_uitest_settle(a);
	return check_fails(a, err);
}

bool ak_uitest_expect(ak_uitest *a, ak_span text, ak_buf *err) {
	if (!check_fails(a, err)) return false;
	return ak_uitest_has(a, text) ? true : failure(a, err, "the screen doesn't show \"%.*s\"", text);
}

bool ak_uitest_expect_no(ak_uitest *a, ak_span text, ak_buf *err) {
	if (!check_fails(a, err)) return false;
	return !ak_uitest_has(a, text) ? true : failure(a, err, "the screen shows \"%.*s\"", text);
}

// ---- The test.* builtins ----
//
//	name             arguments                                     result
//	test.mount       build: fn() -> View                           App (an opaque handle)
//	test.tap         app: App, text: string                        -
//	test.type        app: App, text: string, into: string ("":
//	                 the field tapped or typed in last)            -
//	test.expect      app: App, texts: string or [string]           -
//	test.expect_no   app: App, texts: string or [string]           -
//	test.idle        app: App                                      Task<void> (settled; the
//	                                                               clock moved past every timer)
//	test.dump        app: App                                      string (what the screen shows)
//
// A failed action or expectation throws an error saying what's wrong and
// what the screen shows. The app is unmounted when the test's owner is
// disposed (the test ends), and a native view that outlives it fails the
// test.

static ak_uitest *app_arg(ak_val *a, uint32_t n, const char *name) {
	if (n < 1 || !a[0].p || ((ak_obj *)a[0].p)->tag != AK_TAG_OPAQUE || ((ak_opaque *)a[0].p)->drop != uitest_drop)
		ak_panic("%s: its first argument isn't an app from test.mount", name);
	return a[0].p;
}

static ak_span span_of(ak_val v) {
	const ak_str *s = v.p;
	return s ? ak_strn(s->data, s->len) : sp("");
}

static void throw_buf(ak_buf *b) {
	ak_buf_u8(b, 0);
	ak_throw_msg("%s", (const char *)b->data);
	ak_buf_free(b);
}

static void unmount_cleanup(void *ctx) {
	ak_uitest *a = ctx;
	ak_buf err = {0};
	if (!ak_uitest_unmount(a, &err)) {
		// The test fails: to the catcher of what the test runs in.
		ak_buf_u8(&err, 0);
		ak_throw_msg("%s", (const char *)err.data);
		ak_raise(ak_error_take());
	}
	ak_buf_free(&err);
	ak_release(a);
}

static ak_val t_mount(ak_val *a, uint32_t n) {
	if (n < 1 || !a[0].p) {
		ak_throw_msg("mount: no view to mount");
		return (ak_val){0};
	}
	ak_uitest *app = ak_uitest_mount(a[0].p, NULL);
	if (ak_current_owner()) ak_on_cleanup(unmount_cleanup, ak_retain(app));
	return (ak_val){.p = app};
}

static ak_val t_tap(ak_val *a, uint32_t n) {
	ak_uitest *app = app_arg(a, n, "test.tap");
	ak_buf err = {0};
	if (!ak_uitest_tap(app, span_of(a[1]), &err)) throw_buf(&err);
	ak_buf_free(&err);
	return (ak_val){0};
}

static ak_val t_type(ak_val *a, uint32_t n) {
	ak_uitest *app = app_arg(a, n, "test.type");
	ak_buf err = {0};
	if (!ak_uitest_type(app, span_of(a[1]), n > 2 ? span_of(a[2]) : sp(""), &err)) throw_buf(&err);
	ak_buf_free(&err);
	return (ak_val){0};
}

static ak_val expect(ak_val *a, uint32_t n, bool no, const char *name) {
	ak_uitest *app = app_arg(a, n, name);
	ak_buf err = {0};
	const ak_obj *o = n > 1 ? a[1].p : NULL;
	bool ok = true;
	if (o && o->tag == AK_TAG_ARR) {
		const ak_arr *arr = (const ak_arr *)o;
		for (uint32_t i = 0; ok && i < arr->len; i++)
			ok = no ? ak_uitest_expect_no(app, span_of(arr->data[i]), &err) : ak_uitest_expect(app, span_of(arr->data[i]), &err);
	} else {
		ok = no ? ak_uitest_expect_no(app, span_of(a[1]), &err) : ak_uitest_expect(app, span_of(a[1]), &err);
	}
	if (!ok) throw_buf(&err);
	ak_buf_free(&err);
	return (ak_val){0};
}

static ak_val t_expect(ak_val *a, uint32_t n) { return expect(a, n, false, "test.expect"); }
static ak_val t_expect_no(ak_val *a, uint32_t n) { return expect(a, n, true, "test.expect_no"); }

static ak_val t_idle(ak_val *a, uint32_t n) {
	ak_uitest *app = app_arg(a, n, "test.idle");
	ak_uitest_idle(app);
	ak_future *f = ak_future_new(AK_K_VOID);
	ak_future_resolve(f, (ak_val){0});
	return (ak_val){.p = f};
}

static ak_val t_dump(ak_val *a, uint32_t n) {
	ak_uitest *app = app_arg(a, n, "test.dump");
	ak_buf b = {0};
	ak_uitest_snapshot(app, &b);
	ak_str *s = ak_str_new((const char *)b.data, b.len);
	ak_buf_free(&b);
	return (ak_val){.p = s};
}

void ak_uitest_builtins_init(void) {
	ak_builtin_register("test.mount", t_mount);
	ak_builtin_register("test.tap", t_tap);
	ak_builtin_register("test.type", t_type);
	ak_builtin_register("test.expect", t_expect);
	ak_builtin_register("test.expect_no", t_expect_no);
	ak_builtin_register("test.idle", t_idle);
	ak_builtin_register("test.dump", t_dump);
}
