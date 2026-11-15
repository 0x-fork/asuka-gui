// window.c: windows (ui/window.go, windowhandle.go): the Window element,
// a desktop window of the app's own while it's open, and window handles:
// the commands (WindowAction) and state (WindowState) of a Window, or of
// the main window (the root view), and close requests.
//
// A handle is the Window element itself (Window(…) as w), or none: the
// window the calling code is in (a Window's content finds its Window as
// a context), else the main window. On a phone there are no windows: the
// commands go to the root, which its host ignores.
#include "ui/ui_int.h"

static const ak_val *key_of(const ak_view *e, uint16_t key) { return ak_style_get(e, key); }

static void send_str(ak_view *e, ak_prop p, ak_val v) {
	const ak_str *s = v.p;
	ak_batch_set_string(e->r->batch, e->id, p, s ? s->data : "", s ? s->len : 0);
}

static void unlisten(ak_view *e, ak_event_kind ev) {
	uint64_t bit = UINT64_C(1) << ev;
	if (!(e->listens & bit)) return;
	e->listens &= ~bit;
	if (e->real && !e->dead) ak_batch_unlisten(e->r->batch, e->id, ev);
}

// ---- Handles ----

// A window's handle: what asks before it closes, and its state as the
// host reports it while the core listens.
typedef struct {
	AK_VEC(ak_closure *) asks;  // OnCloseRequest's (retained)
	ak_window_state st;
	ak_node node;
	bool watch;  // its state is followed
} handle_t;

static void handle_free(handle_t *h) {
	for (uint32_t i = 0; i < h->asks.len; i++) ak_release(h->asks.data[i]);
	ak_vec_free(&h->asks);
	ak_signal_drop(&h->node);
}

typedef struct {
	ak_closure *build;
	ak_view *win;     // the window, while it's open (retained)
	ak_owner *owner;  // its content's
	handle_t h;
	ak_window_config cfg;  // as it opened (a window sized to its content's least and most)
} window_t;

static handle_t *main_handle(ak_ui_root *r) {
	if (!r->el->impl) r->el->impl = ak_alloc(sizeof(handle_t));
	return r->el->impl;
}

void ak_window_root_drop(ak_view *e) {
	handle_t *h = e->impl;
	if (!h) return;
	handle_free(h);
	ak_free(h);
	e->impl = NULL;
}

// The view a handle's commands go to: the open window's, or the root's
// (the main window); NULL for a closed Window.
static ak_view *handle_view(ak_view *w) {
	if (!w) return NULL;
	if (w->el == AK_EL_WINDOW) return ((window_t *)w->impl)->win;
	return w;
}

static handle_t *handle_of(ak_view *w) { return w->el == AK_EL_WINDOW ? &((window_t *)w->impl)->h : main_handle(w->r); }

// target is the handle w names: a Window element, or none: the window
// the calling code is in, else the main window.
static ak_view *target(ak_view *w) {
	if (w && w->el == AK_EL_WINDOW) return w;
	void *v = NULL;
	if (ak_context_lookup(ak_window_key, &v)) return v;
	ak_ui_root *r = w ? w->r : ak_ui_current();
	return r->el;
}

ak_view *ak_window_of(ak_view *e) {
	if (!e) return NULL;
	if (e->el == AK_EL_WINDOW) return handle_view(e);
	ak_view *x = e;
	for (int i = 0; i < 1 << 16; i++) {
		if (x->parent) x = x->parent;
		else if (x->up) x = x->up;
		else break;
	}
	return x;
}

// sync_listen listens for what the open window's view must report: its
// close requests while something asks, its state while it's followed.
static void sync_listen(ak_view *w) {
	ak_view *v = handle_view(w);
	if (!v || v->dead) return;
	handle_t *h = handle_of(w);
	bool asks = h->asks.len > 0 || (w->el == AK_EL_WINDOW && ak_view_handler(w, AK_EV_WINDOW_CLOSE_REQUEST));
	if (asks) ak_view_listen(v, AK_EVENT_CLOSE_REQUEST);
	else unlisten(v, AK_EVENT_CLOSE_REQUEST);
	if (h->watch) ak_view_listen(v, AK_EVENT_WINDOW_STATE);
}

static void window_close(ak_view *w);

// destroy closes the window now, without asking.
static void destroy(ak_view *w) {
	if (w->el == AK_EL_WINDOW) {
		window_t *wt = w->impl;
		if (!wt->win) return;
		ak_retain(w);
		window_close(w);
		ak_val f = {.i = 0};
		ak_view_call(w, AK_EV_WINDOW_CHANGE, &f, 1);
		ak_view_call(w, AK_EV_WINDOW_CLOSE, NULL, 0);
		ak_release(w);
		return;
	}
	uint8_t a = AK_WINDOW_ACTION_CLOSE;
	ak_batch_command(w->r->batch, w->id, AK_CMD_WINDOW_ACTION, &a, 1);
}

// allow asks the OnCloseRequest functions; all must agree.
static bool allow(ak_view *w) {
	handle_t *h = handle_of(w);
	bool ok = true;
	uint32_t n = h->asks.len;
	ak_closure **fs = ak_alloc((n ? n : 1) * sizeof *fs);
	for (uint32_t i = 0; i < n; i++) fs[i] = ak_retain(h->asks.data[i]);
	for (uint32_t i = 0; i < n; i++) {
		ak_val v = ak_call(fs[i], NULL, 0);
		if (ak_error_pending()) {
			ak_ui_raise_pending(NULL);
			ok = false;
		} else if (!v.i) {
			ok = false;
		}
		ak_release(fs[i]);
	}
	ak_free(fs);
	return ok;
}

static void close_requested(ak_view *w) {
	if (w->el == AK_EL_WINDOW && ak_view_handler(w, AK_EV_WINDOW_CLOSE_REQUEST)) {
		ak_view_call(w, AK_EV_WINDOW_CLOSE_REQUEST, NULL, 0);  // it closes it, or not (closeWindow)
		if (!allow(w)) return;
		return;
	}
	if (allow(w)) destroy(w);
}

static void set_state(ak_view *w, const ak_event *ev) {
	handle_t *h = handle_of(w);
	h->st = ak_decode_window_state((const uint8_t *)ev->s.p, ev->s.len);
	ak_signal_write(&h->node);
}

// The main window's events, to the root view.
void ak_window_event(ak_view *e, const ak_event *ev) {
	if (ev->kind == AK_EVENT_CLOSE_REQUEST) close_requested(e);
	else if (ev->kind == AK_EVENT_WINDOW_STATE) set_state(e, ev);
}

typedef struct {
	ak_view *w;  // retained
	ak_closure *c;
} ask_reg;

static void ask_remove(void *ctx) {
	ask_reg *g = ctx;
	handle_t *h = handle_of(g->w);
	for (uint32_t i = 0; i < h->asks.len; i++) {
		if (h->asks.data[i] == g->c) {
			memmove(h->asks.data + i, h->asks.data + i + 1, (h->asks.len - i - 1) * sizeof(ak_closure *));
			h->asks.len--;
			ak_release(g->c);
			break;
		}
	}
	if (!g->w->dead) sync_listen(g->w);
	ak_release(g->w);
	ak_free(g);
}

// ak_ui_on_close_request asks f when the user closes the window the
// calling component is in, while the component lives: false keeps it
// open.
void ak_ui_on_close_request(ak_closure *f) {
	ak_view *w = target(NULL);
	handle_t *h = handle_of(w);
	ak_vec_push(&h->asks, (ak_closure *)ak_retain(f));
	sync_listen(w);
	ask_reg *g = ak_alloc(sizeof *g);
	g->w = ak_retain(w);
	g->c = f;
	ak_on_cleanup(ask_remove, g);
}

// ak_ui_window_action does a WindowAction (AK_WINDOW_ACTION_*) on w's
// window: x, y are a Size's or a Position's. A closed Window opens for
// Show and Focus (its change handler is told), and ignores the others.
void ak_ui_window_action(ak_view *w, int64_t action, float x, float y) {
	w = target(w);
	ak_view *v = handle_view(w);
	if (action == AK_WINDOW_ACTION_CLOSE) {
		if (v && allow(w)) destroy(w);
		return;
	}
	if (!v) {
		if (action == AK_WINDOW_ACTION_SHOW || action == AK_WINDOW_ACTION_FOCUS) {
			ak_val t = {.i = 1};
			ak_view_call(w, AK_EV_WINDOW_CHANGE, &t, 1);
		}
		return;
	}
	ak_buf *b = ak_batch_command_begin(v->r->batch, v->id, AK_CMD_WINDOW_ACTION);
	ak_append_window_action(b, (uint8_t)action, x, y);
	ak_batch_command_end(v->r->batch);
}

// ak_ui_window_close closes w's window now, without asking: what a
// closeRequest that said no calls once the user has chosen.
void ak_ui_window_close(ak_view *w) {
	w = target(w);
	if (handle_view(w)) destroy(w);
}

static const uint8_t state_kinds[] = {AK_K_FLOAT, AK_K_FLOAT, AK_K_FLOAT, AK_K_FLOAT, AK_K_BOOL, AK_K_BOOL,
                                      AK_K_BOOL, AK_K_BOOL, AK_K_BOOL, AK_K_BOOL, AK_K_BOOL};
static const char *const state_names[] = {"x", "y", "width", "height", "visible", "focused", "minimized", "maximized", "fullscreen",
                                          "alwaysOnTop", "clickThrough"};
static const ak_shape *const no_shapes[11];
const ak_shape ak_ui_shape_window_state = {"WindowState", 11, state_kinds, no_shapes, state_names, 0, NULL, false, NULL};

// ak_ui_window_state is w's window's state (WindowState), read: a
// binding that reads it follows it. The core listens from then on; the
// host reports it as it starts.
ak_val ak_ui_window_state(ak_view *w) {
	w = target(w);
	handle_t *h = handle_of(w);
	if (!h->watch) {
		h->watch = true;
		sync_listen(w);
	}
	ak_signal_read(&h->node);
	ak_struct *s = ak_struct_new(&ak_ui_shape_window_state);
	const ak_window_state *st = &h->st;
	s->f[0].f = st->x, s->f[1].f = st->y, s->f[2].f = st->width, s->f[3].f = st->height;
	static const uint8_t bits[] = {AK_WINDOW_STATE_VISIBLE, AK_WINDOW_STATE_FOCUSED, AK_WINDOW_STATE_MINIMIZED, AK_WINDOW_STATE_MAXIMIZED,
	                               AK_WINDOW_STATE_FULLSCREEN, AK_WINDOW_STATE_ON_TOP, AK_WINDOW_STATE_CLICK_THROUGH};
	for (int i = 0; i < 7; i++) s->f[4 + i].i = (st->flags & bits[i]) != 0;
	return (ak_val){.p = s};
}

// ---- The Window element ----

static void win_view_event(ak_view *e, const ak_event *ev);

static void win_view_adjust(ak_view *e, ak_lstyle *ls) {
	ls->flex_direction = AK_LCOLUMN;
	ak_prov *p = e->prov;
	ak_view *w = e->up;
	if (!p || !p->fit_win || !w) {
		ls->width = ak_lpct(100), ls->height = ak_lpct(100);
		return;
	}
	// Sized to its content, within its least and most size.
	const ak_window_config *c = &((window_t *)w->impl)->cfg;
	if (c->min_width > 0) ls->min_width = ak_lpt(c->min_width);
	if (c->min_height > 0) ls->min_height = ak_lpt(c->min_height);
	if (c->max_width > 0) ls->max_width = ak_lpt(c->max_width);
	if (c->max_height > 0) ls->max_height = ak_lpt(c->max_height);
}

static const ak_el_class cls_win_view = {.name = "Window", .kind = AK_KIND_WINDOW, .flags = AK_ELF_CHILDREN, .event = win_view_event,
                                         .adjust = win_view_adjust};

static void win_view_event(ak_view *e, const ak_event *ev) {
	ak_view *w = e->up;  // its element
	if (!w || w->dead || ((window_t *)w->impl)->win != e) return;
	switch (ev->kind) {
	case AK_EVENT_DISMISSED: destroy(w); break;  // the user closed it
	case AK_EVENT_CLOSE_REQUEST: close_requested(w); break;
	case AK_EVENT_WINDOW_STATE: set_state(w, ev); break;
	}
}

static void window_close(ak_view *w) {
	window_t *wt = w->impl;
	if (!wt->win) return;
	ak_view *v = wt->win;
	wt->win = NULL;
	if (wt->owner) {
		ak_owner *o = wt->owner;
		wt->owner = NULL;
		ak_owner_dispose(o);
	}
	ak_view_destroy(v);
	ak_release(v);
}

static uint32_t config_flags(const ak_struct *o) {
	static const uint32_t bit[] = {0, AK_WINDOW_CENTERED, AK_WINDOW_FIXED_SIZE, AK_WINDOW_NO_MINIMIZE, AK_WINDOW_NO_MAXIMIZE,
	                               AK_WINDOW_NO_CLOSE, AK_WINDOW_ALWAYS_ON_TOP, AK_WINDOW_HIDDEN, AK_WINDOW_SKIP_TASKBAR,
	                               AK_WINDOW_TRANSPARENT, AK_WINDOW_MAXIMIZED, AK_WINDOW_FULLSCREEN, AK_WINDOW_CHILD};
	uint32_t f = 0;
	for (int i = 1; i < (int)AK_LEN(bit); i++)
		if (o->f[8 + i].i) f |= bit[i];
	if (o->f[8].i || o->f[6].f != 0 || o->f[7].f != 0) f |= AK_WINDOW_POSITIONED;
	if (o->f[21].i) f |= AK_WINDOW_CHILD | AK_WINDOW_MODAL;
	if (o->f[22].i) f |= AK_WINDOW_PANEL | AK_WINDOW_ALWAYS_ON_TOP | AK_WINDOW_SKIP_TASKBAR | AK_WINDOW_NO_MINIMIZE | AK_WINDOW_NO_MAXIMIZE;
	if (o->f[23].i) f |= AK_WINDOW_FIT_CONTENT | AK_WINDOW_FIXED_SIZE;
	if (o->f[24].i) f |= AK_WINDOW_CLICK_THROUGH;
	if (o->f[25].i) f |= AK_WINDOW_MENU_BAR;
	return f;
}

static void window_open(ak_view *w) {
	window_t *wt = w->impl;
	if (wt->win || !wt->build || w->dead || !w->finalized) return;
	ak_ui_root *r = w->r;
	// The window it's declared in: its parent, for a child window.
	void *pv = NULL;
	ak_view *parent = ak_context_lookup(ak_window_key, &pv) ? handle_view(pv) : NULL;
	if (!parent) parent = r->el;
	const ak_val *ov = key_of(w, AK_KEY_WINDOW_OPTIONS);
	const ak_struct *o = ov ? ov->p : NULL;
	ak_window_config c = {0};
	if (o) {
		float *fs[] = {&c.width, &c.height, &c.min_width, &c.min_height, &c.max_width, &c.max_height, &c.x, &c.y};
		for (int i = 0; i < 8; i++) *fs[i] = (float)o->f[i].f;
		c.flags = config_flags(o);
		c.title_bar = (uint8_t)o->f[26].i;
		c.background = (ak_color)o->f[27].i;
		const ak_str *k = o->f[28].p;
		if (k) c.state_key = ak_strn(k->data, k->len);
	}
	if (c.flags & (AK_WINDOW_CHILD | AK_WINDOW_FLYOUT)) c.parent = parent->id;
	else c.flags &= ~(uint32_t)(AK_WINDOW_CHILD | AK_WINDOW_MODAL);
	wt->cfg = c;
	wt->cfg.state_key = ak_cstr("");  // the span is the options'
	ak_view *e = ak_view_new(r, &cls_win_view, 0, true);
	ak_prov *p = ak_prov_new(r);
	p->fit_win = (c.flags & AK_WINDOW_FIT_CONTENT) != 0;
	e->up = w;
	ak_prov_attach(e, p);
	ak_release(p);
	e->finalized = true;
	wt->win = e;
	ak_buf *b = ak_batch_set_bytes_begin(r->batch, e->id, AK_PROP_WINDOW_CONFIG);
	ak_append_window_config(b, &c);
	ak_batch_set_bytes_end(r->batch);
	ak_view_listen(e, AK_EVENT_DISMISSED);
	sync_listen(w);
	const ak_val *t = key_of(w, AK_KEY_WINDOW_TITLE), *ed = key_of(w, AK_KEY_WINDOW_EDITED), *fl = key_of(w, AK_KEY_WINDOW_FILE);
	if (t) send_str(e, AK_PROP_WINDOW_TITLE, *t);
	if (ed && ed->i) ak_batch_set_bool(r->batch, e->id, AK_PROP_WINDOW_EDITED, true);
	if (fl) send_str(e, AK_PROP_WINDOW_FILE, *fl);
	// Opened before its content is built: a Window declared in it, a
	// child of it, opens after it.
	ak_batch_command(r->batch, e->id, AK_CMD_PRESENT, NULL, 0);
	wt->owner = ak_root_under(w->owner);
	ak_owner_set_context(wt->owner, ak_window_key, w);
	ak_owner_set_context(wt->owner, ak_env_key, p);
	ak_view *v = ak_build_under(r, wt->owner, wt->build, NULL, 0);
	if (v) {
		ak_view *k = ak_only_kid(r, v);
		ak_view_append(e, k);
		ak_release(k);
	}
}

static void window_cleanup(void *ctx) {
	ak_view *w = ctx;
	window_close(w);
	ak_release(w);
}

static void window_init(ak_view *e) {
	e->impl = ak_alloc(sizeof(window_t));
	ak_on_cleanup(window_cleanup, ak_retain(e));
}

static void window_drop(ak_view *e) {
	window_t *wt = e->impl;
	if (!wt) return;
	if (wt->win) {
		if (wt->owner) ak_owner_dispose(wt->owner);
		ak_view_destroy(wt->win);
		ak_release(wt->win);
	}
	ak_release(wt->build);
	handle_free(&wt->h);
	ak_free(wt);
}

static void window_set(ak_view *w, uint16_t key, ak_val v, const ak_val *prev) {
	window_t *wt = w->impl;
	ak_view *e = wt->win;
	switch (key) {
	case AK_KEY_WINDOW_OPEN:
		if (v.i) window_open(w);
		else window_close(w);
		break;
	case AK_KEY_WINDOW_TITLE:
		if (e) send_str(e, AK_PROP_WINDOW_TITLE, v);
		break;
	case AK_KEY_WINDOW_EDITED:
		if (e && (prev || v.i)) ak_batch_set_bool(w->r->batch, e->id, AK_PROP_WINDOW_EDITED, v.i != 0);
		break;
	case AK_KEY_WINDOW_FILE:
		if (e) send_str(e, AK_PROP_WINDOW_FILE, v);
		break;
	case AK_KEY_WINDOW_WATCH_STATE:
		if (v.i && !wt->h.watch) {
			wt->h.watch = true;
			sync_listen(w);
		}
		break;
	}
}

static void window_finalize(ak_view *w) {
	const ak_val *o = key_of(w, AK_KEY_WINDOW_OPEN);
	if (o && o->i) window_open(w);
}

static void window_lazy(ak_view *w, ak_closure *build) {
	window_t *wt = w->impl;
	ak_retain(build);
	ak_release(wt->build);
	wt->build = build;
	window_finalize(w);
}

const ak_el_class ak_class_window = {.name = "Window", .kind = AK_KIND_VIEW, .flags = AK_ELF_FRAGMENT | AK_ELF_FLATTEN | AK_ELF_CHILDREN,
                                     .init = window_init, .set = window_set, .finalize = window_finalize, .lazy = window_lazy,
                                     .drop = window_drop};
