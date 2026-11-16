// boundary.c: error boundaries (ui/control.go's Catch): Boundary shows
// its content; when building it fails, or an effect, a handler or a task
// in it does later, it shows what its error handler returns instead
// (React's and Solid's error boundaries). The handler gets the error and
// reset, which builds the content again. The failed content is disposed:
// its effects stop, its views are destroyed.
//
// An error the error handler raises goes to the Boundary around this
// one, or to the app's.
#include "ui/ui_int.h"

typedef struct {
	ak_view *e;          // the Boundary, a fragment (not retained: it holds this)
	ak_closure *build;
	void *err;           // the error shown (retained), NULL: the content
	ak_node node;        // written as it fails or resets
	ak_view *shown;      // e's kid
	ak_owner *branch;    // what shows: the catcher the content is built under, or the fallback's
	ak_owner *owner;     // what made it (retained)
	ak_comp_info info;
	bool ran;
} boundary_t;

static void clear(boundary_t *b) {
	if (b->shown) {
		ak_view_remove(b->e, b->shown, true);
		b->shown = NULL;
	}
	if (b->branch) {
		ak_owner *o = b->branch;
		b->branch = NULL;
		ak_owner_dispose(o);
	}
}

static void failed(void *ctx, void *err) {
	boundary_t *b = ctx;
	if (ak_is_cancel(err)) return;
	ak_retain(err);
	if (b->err) ak_release(err);  // the first error is the one shown
	else b->err = err;
	ak_signal_write(&b->node);
}

static ak_val reset_fn(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	ak_view *e = c->caps[0].p;
	boundary_t *b = e->impl;
	if (b && b->err) {
		ak_release(b->err);
		b->err = NULL;
		ak_signal_write(&b->node);
	}
	return (ak_val){0};
}

static const uint8_t ref_cap[] = {AK_K_REF};

typedef struct {
	boundary_t *b;
	ak_view *out;
} build_ctx;

static void build_content(void *ctx) {
	build_ctx *c = ctx;
	void *tok = ak_untrack_begin();
	c->out = ak_ui_build(c->b->e->r, c->b->build, NULL, 0);
	ak_untrack_end(tok);
}

static void catch_fn(void *ctx) {
	build_ctx *c = ctx;
	ak_catch(build_content, c, failed, c->b);  // held by the branch
}

static void fallback_fn(void *ctx) {
	build_ctx *c = ctx;
	boundary_t *b = c->b;
	ak_closure *h = ak_view_handler(b->e, AK_EV_BOUNDARY_ERROR);
	if (!h) return;
	ak_closure *reset = ak_closure_new(reset_fn, NULL, 1, ref_cap);
	reset->caps[0].p = ak_retain(b->e);
	ak_val args[2] = {{.p = b->err}, {.p = reset}};
	c->out = ak_ui_build(b->e->r, h, args, 2);
	ak_release(reset);
}

static void boundary_run(void *ctx) {
	boundary_t *b = ctx;
	ak_signal_read(&b->node);
	clear(b);
	build_ctx c = {b, NULL};
	// What shows is under a root of its own, under what made the Boundary:
	// the fallback, or a catcher the content is built under.
	b->branch = ak_root_under(b->owner);
	if (b->err) {
		ak_with_owner(b->branch, fallback_fn, &c);
	} else {
		ak_with_owner(b->branch, catch_fn, &c);
		if (b->err && c.out) {
			// It failed as it was built: what it made goes.
			ak_view_destroy(c.out);
			ak_release(c.out);
			c.out = NULL;
		}
	}
	if (c.out) {
		b->shown = c.out;
		ak_view_append(b->e, c.out);
		ak_release(c.out);
	}
}

static void boundary_drop_ctx(void *ctx) {
	boundary_t *b = ctx;
	clear(b);
	ak_release(b->e);
}

static void boundary_init(ak_view *e) {
	boundary_t *b = ak_alloc(sizeof *b);
	e->impl = b;
	b->e = e;
	b->owner = ak_current_owner();
	if (b->owner) ak_owner_retain(b->owner);
}

static void boundary_drop(ak_view *e) {
	boundary_t *b = e->impl;
	if (!b) return;
	if (b->branch) ak_owner_dispose(b->branch);
	ak_release(b->build);
	ak_release(b->err);
	ak_signal_drop(&b->node);
	if (b->owner) ak_owner_release(b->owner);
	ak_free(b);
}

static void boundary_lazy(ak_view *e, ak_closure *build) {
	boundary_t *b = e->impl;
	if (b->build) ak_panic("ui: a Boundary has one content");
	b->build = ak_retain(build);
	ak_retain(e);  // the effect's
	b->info = (ak_comp_info){.drop = boundary_drop_ctx, .label = "Boundary"};
	ak_effect_new(boundary_run, b, AK_EFFECT_RENDER, &b->info);
}

// A child added (by C code, which built it) is its content, outside the
// boundary's watch but for its effects.
static ak_val built(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	return (ak_val){.p = ak_retain(c->caps[0].p)};
}

static void boundary_add(ak_view *e, ak_view *child) {
	ak_closure *c = ak_closure_new(built, NULL, 1, ref_cap);
	c->caps[0].p = ak_retain(child);
	boundary_lazy(e, c);
	ak_release(c);
}

const ak_el_class ak_class_boundary = {.name = "Boundary", .kind = AK_KIND_VIEW, .flags = AK_ELF_FRAGMENT | AK_ELF_FLATTEN | AK_ELF_CHILDREN,
                                       .init = boundary_init, .add = boundary_add, .lazy = boundary_lazy, .drop = boundary_drop};
