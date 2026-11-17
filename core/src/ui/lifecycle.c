// lifecycle.c: the app's lifecycle on the desktop:
// quitting (quit(), relaunch(): the Quit command, which the host does as
// the user's Quit), what hears a quit coming and may cancel it (on quit,
// onQuit: asuka_quit_request, which the host asks however the quit
// started), and another launch of the app while it runs (on
// secondInstance: the SecondInstance event).
#include "ui/ui_ext.h"

typedef struct {
	ak_closure *f;
	ak_owner *owner;  // not retained: the handler goes with it
} handler;

typedef AK_VEC(handler *) handler_vec;

static handler_vec quit_handlers, second_handlers;

static void handler_gone(void *ctx) {
	handler *h = ctx;
	handler_vec *vs[] = {&quit_handlers, &second_handlers};
	for (size_t k = 0; k < AK_LEN(vs); k++) {
		for (uint32_t i = 0; i < vs[k]->len; i++) {
			if (vs[k]->data[i] != h) continue;
			memmove(vs[k]->data + i, vs[k]->data + i + 1, (vs[k]->len - i - 1) * sizeof(handler *));
			vs[k]->len--;
		}
		if (!vs[k]->len) ak_vec_free(vs[k]);
	}
	ak_release(h->f);
	ak_free(h);
}

static void add_handler(handler_vec *v, ak_closure *f) {
	handler *h = ak_alloc(sizeof *h);
	h->f = ak_retain(f);
	h->owner = ak_current_owner();
	ak_vec_push(v, h);
	ak_on_cleanup(handler_gone, h);
}

// call_all calls each handler of v with args, those that still are as
// it comes to them (a handler may add or remove others).
static void call_all(handler_vec *v, ak_val *args, uint32_t nargs) {
	uint32_t n = v->len;
	if (!n) return;
	handler **hs = ak_alloc(n * sizeof(handler *));
	memcpy(hs, v->data, n * sizeof(handler *));
	for (uint32_t i = 0; i < n; i++) {
		bool live = false;
		for (uint32_t j = 0; j < v->len; j++) live |= v->data[j] == hs[i];
		if (live) ak_call_closure(hs[i]->f, hs[i]->owner, args, nargs);
	}
	ak_free(hs);
}

// ak_ui_quit quits the app as the user's Quit does (relaunch: and starts
// it again once it has): the host asks the app first. Phones and the web
// ignore it.
void ak_ui_quit(bool relaunch) {
	ak_ui_root *r = ak_ui.root;
	if (!r || !r->el || r->el->dead) return;
	uint8_t flags = relaunch ? 1 : 0;
	ak_batch_command(r->batch, r->el->id, AK_CMD_QUIT, &flags, 1);
}

// ak_ui_on_quit calls f (fn(QuitEvent)) as the app is about to quit,
// while the current owner lives.
void ak_ui_on_quit(ak_closure *f) { add_handler(&quit_handlers, f); }

// ak_ui_on_second_instance calls f (fn([string], string)) with another
// launch's arguments and working directory, while the current owner
// lives.
void ak_ui_on_second_instance(ak_closure *f) { add_handler(&second_handlers, f); }

// A quit's state, which its QuitEvent's closures share: a box of the
// bits below.
enum { QUIT_CANCELED = 1, QUIT_ENDING = 2 };
static const uint8_t ref_cap[] = {AK_K_REF};

static ak_val quit_cancel(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	ak_box *b = c->caps[0].p;
	if (!(b->v.i & QUIT_ENDING)) b->v.i |= QUIT_CANCELED;
	return (ak_val){0};
}

static ak_val quit_canceled(ak_closure *c, ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	ak_box *b = c->caps[0].p;
	return (ak_val){.i = (b->v.i & QUIT_CANCELED) != 0};
}

static const uint8_t quit_kinds[] = {AK_K_BOOL, AK_K_REF, AK_K_REF};
static const char *const quit_names[] = {"sessionEnding", "cancel", "canceled"};
static const ak_shape *const no_shapes[3];
const ak_shape ak_ui_shape_quit_event = {"QuitEvent", 3, quit_kinds, no_shapes, quit_names, 0, NULL, false, NULL};

// ak_ui_quit_request runs the on quit handlers as the app is about to
// quit (ending: the session ends, and the quit can't be refused), and
// reports whether one canceled it. The host asks once per quit, however
// it started; what the handlers change (an alert) is flushed.
bool ak_ui_quit_request(bool ending) {
	if (!quit_handlers.len) return false;
	ak_box *b = ak_box_new(AK_K_INT, (ak_val){.i = ending ? QUIT_ENDING : 0});
	ak_closure *cancel = ak_closure_new(quit_cancel, NULL, 1, ref_cap);
	cancel->caps[0].p = ak_retain(b);
	ak_closure *canceled = ak_closure_new(quit_canceled, NULL, 1, ref_cap);
	canceled->caps[0].p = ak_retain(b);
	ak_struct *e = ak_struct_new(&ak_ui_shape_quit_event);
	e->f[0].i = ending;
	e->f[1].p = cancel;
	e->f[2].p = canceled;
	ak_batch_begin();
	ak_val arg = {.p = e};
	call_all(&quit_handlers, &arg, 1);
	ak_sched_run();
	ak_batch_end();
	ak_ui_flush();
	bool out = (b->v.i & QUIT_CANCELED) != 0;
	ak_release(e);
	ak_release(b);
	return out;
}

// ak_app_second_instance takes a SecondInstance event's payload: u32
// count, the arguments, then the working directory.
void ak_app_second_instance(ak_span s) {
	ak_reader r = {(const uint8_t *)s.p, s.len, 0, false};
	uint32_t n = ak_read_u32(&r);
	if (r.err || n > s.len / 4) return;
	ak_arr *args = ak_arr_new(AK_K_REF, NULL, n);
	for (uint32_t i = 0; i < n && !r.err; i++) {
		ak_span a = ak_read_str(&r);
		if (!r.err) ak_arr_push(&args, (ak_val){.p = ak_str_span(a)});
	}
	ak_span dir = ak_read_str(&r);
	ak_str *d = ak_str_span(r.err ? ak_cstr("") : dir);
	ak_val vals[2] = {{.p = args}, {.p = d}};
	call_all(&second_handlers, vals, 2);
	ak_release(args);
	ak_release(d);
}
