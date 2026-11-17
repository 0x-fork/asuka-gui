// react.c: the VM's state, on the signal graph (core/src/signal): cells,
// memos, accessors, effects, cleanups, and tasks tied to owners.
#include "vm/vm_int.h"
#include "signal/signal.h"
#include "vm/keep.h"
#include "vm/path.h"

#include <stdio.h>
#include <string.h>

// ---- Cells ----
//
// A state's cell holds its value. A state whose initializer reads what
// changes is a follow (rfcs/0012): a cell whose value a memo of its
// initializer replaces when what it read changes (core/src/signal's
// ak_follow). Reads and writes of a cell check which it is (CELL_FOLLOW,
// one branch); a follow's readers follow its ak_follow, not the cell's
// node.
//
// A cell's readers follow paths below it (rfcs/0013, vm/path.h): its
// own node is the root, read whole; paths, the nodes below it, made as
// they're read. A follow's paths are read whole: its readers follow its
// ak_follow, whose value its initializer replaces.

typedef struct {
	ak_opaque h;
	ak_node node;
	ak_val v;
	uint8_t kind;
	// Its name in its key (Picker.selected#…: selected), which an effect
	// writing it notes for the panic of a loop (ak_signal_note_write).
	uint16_t name_off, name_len;
	ak_str *key;  // retained; NULL for a cell made without one
	ak_pnode *paths;  // the paths read below it (NULL: none)
} cell;

enum {
	CELL_KEPT = 1,    // kept across reloads (keep.c)
	CELL_FOLLOW = 2,  // a follow: the cell is a follow's first field
};

typedef struct follow follow;
struct follow {
	cell c;
	ak_follow *f;        // NULL once the graph let go of it
	ak_closure *update;  // the initializer's thunk; NULL once settled or its owner went
	ak_comp_info info;
	ak_box *selfbox;     // a scalar's current value as T?, reused while only it holds it
	bool self;           // the thunk takes the state's current value (T?)
	bool has;            // computed or set once
};

static void cell_drop(ak_opaque *o) {
	cell *c = (cell *)o;
	if (o->h.flags & CELL_KEPT) ak_keep_forget(c);  // a keyed cell (keep.c)
	if (o->h.flags & CELL_FOLLOW) {
		ak_release(((follow *)c)->update);
		ak_release(((follow *)c)->selfbox);
	}
	ak_release(c->key);
	ak_path_free(&c->paths);
	ak_signal_drop(&c->node);
	ak_release_val(c->v, (ak_kind)c->kind);
}

static inline bool is_follow(const cell *c) { return (c->h.h.flags & CELL_FOLLOW) != 0; }

static ak_val follow_get(follow *w, bool track);

// cell_name keeps key as c's name: what's between its last . and its #.
static void cell_name(cell *c, ak_str *key) {
	if (!key) return;
	c->key = ak_retain(key);
	const char *s = key->data;
	size_t n = strcspn(s, "#"), start = 0;
	for (size_t i = 0; i < n; i++)
		if (s[i] == '.') start = i + 1;
	c->name_off = (uint16_t)(start < 0xFFFF ? start : 0);
	c->name_len = (uint16_t)(n - c->name_off < 0xFFFF ? n - c->name_off : 0);
}

// cell_wrote notes, for an effect writing c, what it wrote.
static inline void cell_wrote(const cell *c) {
	if (c->key) ak_signal_note_write(c->key->data + c->name_off, c->name_len);
}

// A cell kept across reloads is looked up and stored through these
// (dev.c sets them while the dev loop runs; NULL otherwise).
bool (*ak_vm_keep_find)(ak_str *key, ak_kind k, ak_val *out);
void (*ak_vm_keep_add)(ak_str *key, void *cell);

ak_val ak_vm_cell_new(ak_val v, ak_kind k, ak_str *keep_key) {
	cell *c = ak_opaque_new(AK_TAG_CELL, sizeof(cell), cell_drop);
	ak_signal_born(&c->node);
	c->kind = (uint8_t)k;
	c->v = v;
	cell_name(c, keep_key);
	if (keep_key && ak_vm_keep_find) {
		ak_val kept;
		if (ak_vm_keep_find(keep_key, k, &kept)) {
			ak_release_val(c->v, k);
			c->v = kept;
		}
		ak_vm_keep_add(keep_key, c);
	}
	return (ak_val){.p = c};
}

ak_kind ak_vm_cell_kind(void *p) { return (ak_kind)((cell *)p)->kind; }

// follow_update runs the initializer, given the state's current value
// when it asks for it (none the first time), and takes what it returns
// unless that's equal to the value now.
static bool follow_update(void *ctx) {
	follow *w = ctx;
	if (!w->update) return false;  // its owner went (follow_gone)
	ak_kind k = (ak_kind)w->c.kind;
	ak_val arg = {0}, v;
	if (w->self) {
		// T?: a reference is its own optional; a scalar, boxed, in a box
		// the follow keeps while nothing else holds it (no allocation
		// each time it runs).
		if (w->has && k == AK_K_REF) arg = w->c.v;
		else if (w->has) {
			ak_box *b = w->selfbox;
			if (b && b->h.rc == 1) {
				b->v = w->c.v;
			} else {
				ak_release(b);
				b = w->selfbox = ak_box_new(k, w->c.v);
			}
			arg.p = b;
		}
		v = ak_call(w->update, &arg, 1);
	} else {
		v = ak_call(w->update, NULL, 0);
	}
	if (ak_error_pending()) {
		ak_raise(ak_error_take());  // the follow holds it, as a memo does
		return true;
	}
	if (w->has && ak_val_eq(w->c.v, v, k)) {
		ak_release_val(v, k);
		return false;
	}
	ak_val old = w->c.v;
	w->c.v = v;
	w->has = true;
	ak_release_val(old, k);
	return true;
}

static void follow_comp_drop(void *ctx) {
	follow *w = ctx;
	w->f = NULL;
	ak_release(w);  // the graph's reference
}

// follow_gone runs as the owner the follow was made under is disposed:
// it never runs its initializer again, and lets go of it now (it may
// hold the component's env, which holds the follow).
static void follow_gone(void *ctx) {
	follow *w = ctx;
	ak_closure *c = w->update;
	w->update = NULL;
	ak_release(c);
	ak_release(w);
}

// follow_settle: an initializer that ran and read nothing (a parameter
// whose caller passed a constant) never runs again; the follow is a
// plain state from then on, and lets go of it.
static inline void follow_settle(follow *w) {
	if (w->update && w->f && ak_follow_settled(w->f)) {
		ak_closure *c = w->update;
		w->update = NULL;
		ak_release(c);
	}
}

// follow_store takes v as the follow's value (a write): brought up to
// date first, so that a write after what it follows changed stands.
static void follow_store(follow *w, ak_val v, bool always) {
	ak_kind k = (ak_kind)w->c.kind;
	if (w->f) ak_follow_refresh(w->f);
	follow_settle(w);
	if (!always && w->has && ak_val_eq(w->c.v, v, k)) {
		ak_release_val(v, k);
		return;
	}
	ak_val old = w->c.v;
	w->c.v = v;
	w->has = true;
	ak_release_val(old, k);
	cell_wrote(&w->c);
	if (w->f) ak_follow_set(w->f);
}

ak_val ak_vm_follow_new(ak_closure *update, ak_kind k, ak_str *keep_key, bool self) {
	follow *w = ak_opaque_new(AK_TAG_CELL, sizeof(follow), cell_drop);
	w->c.h.h.flags |= CELL_FOLLOW;
	w->c.kind = (uint8_t)k;
	cell_name(&w->c, keep_key);
	w->update = ak_retain(update);
	w->self = self;
	w->info = (ak_comp_info){.drop = follow_comp_drop, .label = update->invoke == ak_vm_closure_invoke ? ak_func_name(update->fn) : "a prop's caller's value"};
	ak_retain(w);  // the graph's, until the computation is dropped
	w->f = ak_follow_new(follow_update, w, &w->info);
	if (ak_current_owner()) ak_on_cleanup(follow_gone, ak_retain(w));
	if (keep_key && ak_vm_keep_find) {
		// A value kept across a reload is a write: it stands until what
		// the initializer reads changes.
		ak_val kept;
		if (ak_vm_keep_find(keep_key, k, &kept)) follow_store(w, kept, true);
		ak_vm_keep_add(keep_key, w);
	}
	return (ak_val){.p = w};
}

// follow_get reads a follow, brought up to date: tracked (a binding
// follows it) or not. Its initializer's error is thrown in the reader.
static ak_val follow_get(follow *w, bool track) {
	if (w->f) {
		bool ok = track ? ak_follow_read(w->f) : (ak_follow_refresh(w->f), ak_memo_error(w->f) == NULL);
		follow_settle(w);
		if (!ok) {
			ak_throw(ak_retain(ak_memo_error(w->f)));
			return (ak_val){0};
		}
	}
	ak_retain_val(w->c.v, (ak_kind)w->c.kind);
	return w->c.v;
}

ak_val ak_vm_cell_get(void *p, bool track) {
	cell *c = p;
	if (AK_UNLIKELY(is_follow(c))) return follow_get((follow *)c, track);
	if (track) ak_signal_read(&c->node);
	ak_retain_val(c->v, (ak_kind)c->kind);
	return c->v;
}

void ak_vm_cell_set(void *p, ak_val v) {
	cell *c = p;
	if (AK_UNLIKELY(is_follow(c))) {
		follow_store((follow *)c, v, false);
		return;
	}
	if (ak_val_eq(c->v, v, (ak_kind)c->kind)) {
		ak_release_val(v, (ak_kind)c->kind);
		return;
	}
	ak_val old = c->v;
	c->v = v;
	cell_wrote(c);
	if (c->paths) ak_path_set(&c->paths, &c->node, old, v, (ak_kind)c->kind);
	else ak_signal_write(&c->node);
	ak_release_val(old, (ak_kind)c->kind);
}

ak_val ak_vm_cell_take(void *p) {
	cell *c = p;
	if (AK_UNLIKELY(is_follow(c)) && ((follow *)c)->f) {
		// Changed in place: from its current value.
		ak_follow_refresh(((follow *)c)->f);
		follow_settle((follow *)c);
	}
	ak_val v = c->v;
	if (c->kind == AK_K_REF) c->v.p = NULL;
	return v;
}

void ak_vm_cell_put(void *p, ak_val v) {
	cell *c = p;
	if (AK_UNLIKELY(is_follow(c))) {
		// What take moved out, back: the follow is up to date already.
		follow *w = (follow *)c;
		ak_release_val(c->v, (ak_kind)c->kind);
		c->v = v;
		w->has = true;
		cell_wrote(c);
		if (w->f) ak_follow_set(w->f);
		return;
	}
	ak_release_val(c->v, (ak_kind)c->kind);
	c->v = v;
	cell_wrote(c);
	if (c->paths) {
		static const uint8_t all[1] = {AK_PS_ALL};
		ak_path_put(&c->paths, &c->node, v, (ak_kind)c->kind, all, 1, NULL);
	} else {
		ak_signal_write(&c->node);
	}
}

ak_val ak_vm_cell_path(void *p, const uint8_t *path, uint32_t n, const ak_val *args, bool track) {
	cell *c = p;
	ak_val out = {0};
	if (AK_UNLIKELY(is_follow(c))) {
		// Read whole (its ak_follow), and the path in its value.
		ak_val v = follow_get((follow *)c, track);
		if (ak_error_pending()) return out;
		ak_path_read(NULL, NULL, v, (ak_kind)c->kind, path, n, args, false, &out);
		ak_release_val(v, (ak_kind)c->kind);
		return out;
	}
	ak_path_read(&c->paths, &c->node, c->v, (ak_kind)c->kind, path, n, args, track && ak_tracking(), &out);
	return out;
}

void ak_vm_cell_putp(void *p, ak_val v, const uint8_t *path, uint32_t n, const ak_val *args) {
	cell *c = p;
	if (AK_UNLIKELY(is_follow(c))) {
		ak_vm_cell_put(p, v);
		return;
	}
	ak_release_val(c->v, (ak_kind)c->kind);
	c->v = v;
	cell_wrote(c);
	ak_path_put(&c->paths, &c->node, v, (ak_kind)c->kind, path, n, args);
}

void ak_vm_cell_keys(void *p, const uint8_t *path, uint32_t n, const ak_val *args, const uint16_t *fields, uint16_t nf) {
	cell *c = p;
	if (AK_UNLIKELY(is_follow(c))) {
		ak_val v = follow_get((follow *)c, true);
		ak_release_val(v, (ak_kind)c->kind);
		return;
	}
	if (ak_tracking()) ak_path_keys(&c->paths, path, n, args, fields, nf);
}

uint32_t ak_vm_cell_nodes(void *p) { return ak_path_nodes(((cell *)p)->paths); }

// ---- Errors between the VM and the graph ----
//
// The VM unwinds with a pending error (ak_throw); the graph delivers an
// error a computation raised to a catcher (ak_raise). Where VM code runs
// as a computation, an error it leaves pending is raised.

static void raise_pending(void) {
	if (!ak_error_pending()) return;
	void *err = ak_error_take();
	if (ak_is_cancel(err)) {
		ak_release(err);
		return;
	}
	if (ak_failed()) {
		ak_release(err);  // raised already (a memo's error, read)
		return;
	}
	ak_raise(err);
}

static const char *label_of(ak_closure *c) {
	return c->invoke == ak_vm_closure_invoke ? ak_func_name(c->fn) : "closure";
}

// ---- Memos ----

typedef struct {
	ak_opaque h;
	ak_memo *m;
	ak_closure *c;
	ak_val v;
	uint8_t kind;
	bool has;
	ak_comp_info info;
} memo;

static bool memo_update(void *ctx) {
	memo *m = ctx;
	if (!m->c) return false;  // its owner went (memo_gone)
	ak_val v = ak_call(m->c, NULL, 0);
	if (ak_error_pending()) {
		ak_raise(ak_error_take());  // the memo holds it (ak_memo_error)
		return true;
	}
	if (m->has && ak_val_eq(m->v, v, (ak_kind)m->kind)) {
		ak_release_val(v, (ak_kind)m->kind);
		return false;
	}
	ak_release_val(m->v, (ak_kind)m->kind);
	m->v = v;
	m->has = true;
	return true;
}

static void memo_comp_drop(void *ctx) {
	memo *m = ctx;
	m->m = NULL;
	ak_release(m);  // the graph's reference
}

// memo_gone runs as the owner the memo was made under is disposed (the
// component or store it's in goes): it never runs again, and its closure
// and value go now, not with the memo, which they may hold (a store's
// memo, whose closure holds the store), or which may outlive the
// computation's drop (a view it computed retains the computation's
// owner). Read after, it throws.
static void memo_gone(void *ctx) {
	memo *m = ctx;
	ak_closure *c = m->c;
	m->c = NULL;
	ak_release(c);
	if (m->has) {
		ak_val v = m->v;
		m->v = (ak_val){0};
		m->has = false;
		ak_release_val(v, (ak_kind)m->kind);
	}
	ak_release(m);
}

static void memo_drop(ak_opaque *o) {
	memo *m = (memo *)o;
	ak_release_val(m->v, (ak_kind)m->kind);
	ak_release(m->c);
}

ak_val ak_vm_memo_new(ak_closure *c, ak_kind k) {
	memo *m = ak_opaque_new(AK_TAG_MEMO, sizeof(memo), memo_drop);
	m->kind = (uint8_t)k;
	m->c = ak_retain(c);
	m->info = (ak_comp_info){.drop = memo_comp_drop, .label = label_of(c)};
	ak_retain(m);  // the graph's, until the computation is dropped
	m->m = ak_memo_new(memo_update, m, &m->info);
	if (ak_current_owner()) ak_on_cleanup(memo_gone, ak_retain(m));
	return (ak_val){.p = m};
}

ak_val ak_vm_memo_get(void *p) {
	memo *m = p;
	if (m->m && !ak_memo_read(m->m)) {
		// It failed: reading it throws its error in the VM too.
		ak_throw(ak_retain(ak_memo_error(m->m)));
		return (ak_val){0};
	}
	if (!m->has) {
		// Disposed before it ever computed.
		ak_throw_msg("read of a disposed memo (%s)", m->info.label);
		return (ak_val){0};
	}
	ak_retain_val(m->v, (ak_kind)m->kind);
	return m->v;
}

// ---- Accessors ----

enum { ACC_VALUE, ACC_FUNC, ACC_CELL, ACC_MEMO, ACC_MODEL };

typedef struct {
	ak_opaque h;
	uint8_t mode, kind;
	ak_val v;  // the value, or the closure, cell or memo
} acc;

static void acc_drop(ak_opaque *o) {
	acc *a = (acc *)o;
	if (a->mode == ACC_VALUE) ak_release_val(a->v, (ak_kind)a->kind);
	else ak_release(a->v.p);
}

static acc *acc_new(uint8_t mode, ak_kind k, ak_val v) {
	acc *a = ak_opaque_new(AK_TAG_ACC, sizeof(acc), acc_drop);
	a->mode = mode;
	a->kind = (uint8_t)k;
	a->v = v;
	return a;
}

ak_val ak_vm_acc_value(ak_val v, ak_kind k) { return (ak_val){.p = acc_new(ACC_VALUE, k, v)}; }
ak_val ak_vm_acc_func(ak_closure *c, ak_kind k) { return (ak_val){.p = acc_new(ACC_FUNC, k, (ak_val){.p = ak_retain(c)})}; }

ak_val ak_vm_acc_cell(void *p) {
	ak_obj *o = p;
	if (o->tag == AK_TAG_MEMO) return (ak_val){.p = acc_new(ACC_MEMO, (ak_kind)((memo *)p)->kind, (ak_val){.p = ak_retain(p)})};
	if (o->tag == AK_TAG_ACC) return (ak_val){.p = ak_retain(p)};
	return (ak_val){.p = acc_new(ACC_CELL, (ak_kind)((cell *)p)->kind, (ak_val){.p = ak_retain(p)})};
}

static ak_val model_get(void *m);

ak_val ak_vm_acc_get(void *p) {
	acc *a = p;
	switch (a->mode) {
	case ACC_VALUE:
		ak_retain_val(a->v, (ak_kind)a->kind);
		return a->v;
	case ACC_FUNC:
		return ak_call(a->v.p, NULL, 0);
	case ACC_CELL:
		return ak_vm_cell_get(a->v.p, true);
	case ACC_MODEL:
		return model_get(a->v.p);
	default:
		return ak_vm_memo_get(a->v.p);
	}
}

// ---- Models: a prop its component assigns ----
//
// A prop with its change event (open, openChange) that its component
// assigns is read through a model, an accessor of the component's: bound
// by the caller (bind open: s), it reads the caller's accessor, and the
// component's assignment emits openChange, whose bind writes s; unbound,
// it's a follow of the caller's accessor (rfcs/0012): the caller's value
// until the component assigns it, and the caller's again when that
// changes. A caller's constant settles it: a state of the component's.

typedef struct {
	ak_opaque h;
	acc *inner;  // the caller's accessor
	void *own;   // unbound: the follow of it
	uint8_t kind;
	bool bound;
} model;

static void model_drop(ak_opaque *o) {
	model *m = (model *)o;
	ak_release(m->own);
	ak_release(m->inner);
}

static ak_val model_get(void *p) {
	model *m = p;
	if (m->bound) return ak_vm_acc_get(m->inner);
	return ak_vm_cell_get(m->own, true);
}

// The follow's initializer: the caller's accessor, read.
static const uint8_t model_read_caps[1] = {AK_K_REF};
static ak_val model_read(ak_closure *c, ak_val *args, uint32_t nargs) {
	AK_UNUSED(args);
	AK_UNUSED(nargs);
	return ak_vm_acc_get(c->caps[0].p);
}

ak_val ak_vm_model_new(void *inner, bool bound) {
	acc *in = inner;
	if (!in || ((ak_obj *)in)->tag != AK_TAG_ACC) ak_panic("ui.model: a prop's accessor, not %p", inner);
	model *m = ak_opaque_new(AK_TAG_OPAQUE, sizeof(model), model_drop);
	m->kind = in->kind;
	m->inner = ak_retain(in);
	m->bound = bound;
	if (!bound) {
		ak_closure *read = ak_closure_new(model_read, NULL, 1, model_read_caps);
		read->caps[0].p = ak_retain(in);
		m->own = ak_vm_follow_new(read, (ak_kind)m->kind, NULL, false).p;
		ak_release(read);
	}
	return (ak_val){.p = acc_new(ACC_MODEL, (ak_kind)m->kind, (ak_val){.p = m})};
}

static model *model_of(void *p, const char *what) {
	acc *a = p;
	if (!a || ((ak_obj *)a)->tag != AK_TAG_ACC || a->mode != ACC_MODEL) ak_panic("%s: a model, not %p", what, p);
	return a->v.p;
}

bool ak_vm_model_bound(void *p) { return model_of(p, "ui.model_bound")->bound; }

void ak_vm_model_set(void *p, ak_val v) {
	model *m = model_of(p, "ui.model_set");
	if (m->bound) ak_panic("ui.model_set: a bound prop's value is its caller's");
	ak_retain_val(v, (ak_kind)m->kind);
	ak_vm_cell_set(m->own, v);
}

// ---- Untracked calls, effects, cleanups, batches ----

void ak_vm_untrack_begin(ak_co *co) {
	if (co->nuntracks == co->ucap) {
		co->ucap = co->ucap ? co->ucap * 2 : 8;
		co->untracks = ak_realloc(co->untracks, sizeof(void *) * co->ucap);
	}
	co->untracks[co->nuntracks++] = ak_untrack_begin();
}

ak_val ak_vm_call_untracked(ak_closure *c, ak_val *args, uint32_t nargs) {
	void *tok = ak_untrack_begin();
	ak_val v = ak_call(c, args, nargs);
	ak_untrack_end(tok);
	return v;
}

typedef struct {
	ak_closure *c;
	ak_comp_info info;
} effect_ctx;

static void effect_run(void *ctx) {
	effect_ctx *e = ctx;
	ak_val v = ak_call(e->c, NULL, 0);
	AK_UNUSED(v);  // effects return nothing
	raise_pending();
}

static void effect_drop(void *ctx) {
	effect_ctx *e = ctx;
	ak_release(e->c);
	ak_free(e);
}

// effect_describe names an effect for a panic as its source says it:
// "Picker's effect at picker.asuka:12" (its closure, Picker/effect, made
// where its declaration is).
static void effect_describe(void *ctx, char *buf, size_t n) {
	effect_ctx *e = ctx;
	if (e->c->invoke != ak_vm_closure_invoke) {
		snprintf(buf, n, "effect %s", label_of(e->c));
		return;
	}
	const ak_func *f = e->c->fn;
	const char *name = f->name->data;
	size_t owner = strcspn(name, "/");
	const char *what = name[owner] ? name + owner + 1 : "effect";
	uint32_t line = f->nlines ? f->lines[0].line : 0;
	snprintf(buf, n, "%.*s's %s at %s:%u", (int)owner, name, what, f->file->data, line);
}

void ak_vm_effect(ak_closure *c, bool render) {
	effect_ctx *e = ak_alloc(sizeof *e);
	e->c = ak_retain(c);
	e->info = (ak_comp_info){.drop = effect_drop, .label = label_of(c), .describe = render ? NULL : effect_describe};
	ak_effect_new(effect_run, e, render ? AK_EFFECT_RENDER : AK_EFFECT_USER, &e->info);
}

static void cleanup_run(void *ctx) {
	ak_closure *c = ctx;
	ak_val v = ak_call(c, NULL, 0);
	AK_UNUSED(v);
	raise_pending();
	ak_release(c);
}

void ak_vm_cleanup(ak_closure *c) {
	if (!ak_current_owner()) return;
	ak_on_cleanup(cleanup_run, ak_retain(c));
}

void ak_vm_batch(bool begin) {
	if (begin) ak_batch_begin();
	else ak_batch_end();
}

// ---- Tasks and owners ----
//
// Each task has an owner of its own under the owner that started it:
// disposing that one cancels the task, and the task disposes its owner
// when it finishes, so a finished task holds nothing. The owner's
// cleanup refers to the task, not its future: the task is there until
// it disposes the owner, and the future's references are those who can
// await it (an error none of them can see goes to the owner's catcher).

static void task_owner_cleanup(void *ctx) {
	ak_task *t = ctx;
	if (t->fut) ak_future_cancel(t->fut);
}

static void register_cancel(void *ctx) { ak_on_cleanup(task_owner_cleanup, ctx); }

void ak_vm_task_own(ak_task *t, ak_owner *holder) {
	if (!holder && !ak_current_owner()) return;
	t->owner = holder ? ak_owner_new_in(holder) : ak_owner_new();
	ak_owner_retain(t->owner);
	ak_with_owner(t->owner, register_cancel, t);
}

typedef struct {
	ak_task *t;
	ak_val out;
	bool suspended;
} step_ctx;

static void step_in_owner(void *ctx) {
	step_ctx *s = ctx;
	s->suspended = ak_vm_run(&s->t->co, &s->out);
}

bool ak_vm_task_run(ak_task *t, ak_val *out) {
	if (!t->owner) return ak_vm_run(&t->co, out);
	step_ctx s = {t, {0}, false};
	ak_with_owner(t->owner, step_in_owner, &s);
	*out = s.out;
	return s.suspended;
}

static void raise_ctx(void *err) { ak_raise(err); }

// The owner, kept past its disposal for its catcher (its parents are
// held as long as it is), reached when the future is dropped unseen.
static void raise_unseen(void *ctx, void *err) {
	ak_owner *o = ctx;
	if (err) {
		if (o) ak_with_owner(o, raise_ctx, err);
		else ak_raise(err);
	}
	if (o) ak_owner_release(o);
}

void ak_vm_task_report_unseen(ak_task *t, ak_future *f) {
	if (t->owner) ak_owner_retain(t->owner);
	f->unseen = raise_unseen;
	f->unseen_ctx = t->owner;
}

void ak_vm_task_done(ak_task *t, void *err) {
	ak_owner *o = t->owner;
	t->owner = NULL;
	if (err) {
		if (o) ak_with_owner(o, raise_ctx, err);
		else ak_raise(err);
	}
	if (o) {
		ak_owner_dispose(o);
		ak_owner_release(o);
	}
}

// ---- Components' event handlers ----
//
// An async handler of a component's event (lang/LOWERING.md's
// "Components") is a task of the component that declares it, the
// receiver: whoever emits the event (a sync handler, a fn running on a
// task, an async function that waits for it), the handler runs until it
// finishes or the receiver goes. ui.owner, at the receiver's start, is
// the owner it was made under, which goes when it goes; ui.handler wraps
// the handler as it's passed: calling it starts the task held by that
// owner, its contexts and catcher those of where the handler is declared
// (the owner current then: a sheet's, a stack's screen's).

typedef struct {
	ak_opaque h;
	ak_owner *o;
} owner_ref;

static void owner_ref_drop(ak_opaque *p) { ak_owner_release(((owner_ref *)p)->o); }

static ak_val b_owner(ak_val *a, uint32_t n) {
	AK_UNUSED(a);
	AK_UNUSED(n);
	ak_owner *o = ak_current_owner();
	if (!o) return (ak_val){0};
	owner_ref *r = ak_opaque_new(AK_TAG_OPAQUE, sizeof *r, owner_ref_drop);
	ak_owner_retain(o);
	r->o = o;
	return (ak_val){.p = r};
}

// A handler's captures: the async closure, where it's declared and what
// holds its tasks (owners, retained: released by handler_drop).
static const uint8_t handler_caps[3] = {AK_K_REF, AK_K_INT, AK_K_INT};

static void handler_drop(ak_closure *c) {
	if (c->caps[1].p) ak_owner_release(c->caps[1].p);
	if (c->caps[2].p) ak_owner_release(c->caps[2].p);
}

typedef struct {
	ak_closure *c;
	ak_val *args;
	uint32_t nargs;
	ak_owner *holder;
	ak_val out;
} handler_call;

static void handler_start(void *ctx) {
	handler_call *h = ctx;
	h->out = (ak_val){.p = ak_vm_task_start_in(h->c->fn, h->c, h->args, h->nargs, h->holder)};
}

static ak_val handler_invoke(ak_closure *c, ak_val *args, uint32_t nargs) {
	ak_owner *where = c->caps[1].p, *holder = c->caps[2].p;
	// Its component is gone, and the handler with it.
	if (holder && ak_owner_disposed(holder)) return (ak_val){0};
	handler_call h = {c->caps[0].p, args, nargs, holder, {0}};
	if (where) ak_with_owner(where, handler_start, &h);
	else handler_start(&h);
	return h.out;
}

static ak_val b_handler(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_closure *f = a[0].p;
	owner_ref *r = a[1].p;
	if (!f || !ak_closure_async(f)) return (ak_val){.p = ak_retain(f)};
	ak_owner *where = ak_current_owner(), *holder = r ? r->o : where;
	ak_closure *c = ak_closure_new(handler_invoke, NULL, 3, handler_caps);
	c->caps[0].p = ak_retain(f);
	if (where) ak_owner_retain(where);
	c->caps[1].p = where;
	if (holder) ak_owner_retain(holder);
	c->caps[2].p = holder;
	c->drop = handler_drop;
	return (ak_val){.p = c};
}

// ---- Setup ----

static void err_retain(void *e) { ak_retain(e); }
static void err_release(void *e) { ak_release(e); }

void ak_vm_react_init(void) {
	ak_signal_set_error_refs(err_retain, err_release);
	ak_builtin_register("ui.owner", b_owner);
	ak_builtin_register("ui.handler", b_handler);
}
