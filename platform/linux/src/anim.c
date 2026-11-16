// anim.c: animation. GTK doesn't animate a widget's allocation, so
// between AnimateBegin and AnimateEnd the frames, opacity and transforms
// the core sets are stepped on the frame clock with the core's curve
// (CSS's cubic béziers, a damped spring); a view's first frame doesn't
// animate; enter and exit transitions start from, and go out to, the
// transition's state (a view going out stays until it has, with the
// views in it).
//
// Style.Match, as on macOS and Windows: when a view with a key is
// destroyed during an animation and another with the key is inserted,
// the new one grows from the old one's place and size, and the old one
// moves along to the new one's, fading. The views with keys are noted
// where they are as the animated batch starts destroying (in their
// "top": the outermost view the host places them in, a screen's or the
// window's root); the old view's widget moves into its top, where it
// was, on top, until the batch's frames are applied; then they're
// matched, their frames stepped with the other tweens. A view going with
// no match fades where it is.
#include "host.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

struct tween {
	animation a;
	double start;
	float from[4], to[4];
	float from_op, to_op;
	transform_t from_t, to_t;
	bool frame, op, tr;
	bool exit;  // a view going out: removed when done
	GtkWidget *widget;
	node_vec kept;  // destroyed views in it, released with it (exit)
	float now[4];   // frame_now's answer
};

struct transition {
	bool fade;
	float scale, dx, dy;
};

static node_vec tweening;  // the nodes with a tween
static bool ticking;

double lx_now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void lx_begin_animation(ak_span args) {
	ak_reader r = {(const uint8_t *)args.p, args.len, 0, false};
	animation a = {0};
	a.duration = ak_read_f32(&r);
	a.curve = ak_read_u8(&r);
	a.repeat = (int32_t)ak_read_u32(&r);
	a.autoreverse = ak_read_u8(&r) != 0;
	a.delay = ak_read_f32(&r);
	a.damping = ak_read_f32(&r);
	a.velocity = ak_read_f32(&r);
	if (a.damping <= 0) a.damping = 1;
	H.anim = a;
	H.animating = a.duration > 0 && !H.env.reduce_motion;
}

static void run_matches(void);

void lx_end_animation(void) {
	if (H.animating) run_matches();  // once the frames are applied
	H.animating = false;
}

// bezier is a CSS cubic-bezier timing function at t.
static double bezier(double t, double x1, double y1, double x2, double y2) {
#define CX(s) (3 * x1 * (s) * (1 - (s)) * (1 - (s)) + 3 * x2 * (s) * (s) * (1 - (s)) + (s) * (s) * (s))
#define CY(s) (3 * y1 * (s) * (1 - (s)) * (1 - (s)) + 3 * y2 * (s) * (s) * (1 - (s)) + (s) * (s) * (s))
	double lo = 0, hi = 1, s = t;
	for (int i = 0; i < 20; i++) {
		double x = CX(s);
		if (fabs(x - t) < 1e-5) break;
		if (x < t)
			lo = s;
		else
			hi = s;
		s = (lo + hi) / 2;
	}
	return CY(s);
#undef CX
#undef CY
}

// spring is a damped spring's position from 0 to 1 over the animation's
// duration, settling as the duration ends.
static double spring(double t, double damping, double velocity, double duration) {
	if (t >= 1) return 1;
	double omega = 2 * M_PI / fmax(duration, 0.05) * 1.2;
	double time = t * duration;
	if (damping < 1) {
		double wd = omega * sqrt(1 - damping * damping);
		double b = (damping * omega - velocity) / wd;
		return 1 - exp(-damping * omega * time) * (cos(wd * time) + b * sin(wd * time));
	}
	return 1 - exp(-omega * time) * (1 + (omega - velocity) * time);
}

static double ease(const animation *a, double t) {
	switch (a->curve) {
	case AK_ANIMATION_CURVE_LINEAR:
		return t;
	case AK_ANIMATION_CURVE_EASE_IN:
		return bezier(t, 0.42, 0, 1, 1);
	case AK_ANIMATION_CURVE_EASE_OUT:
		return bezier(t, 0, 0, 0.58, 1);
	case AK_ANIMATION_CURVE_SPRING:
		return spring(t, a->damping, a->velocity, a->duration);
	}
	return bezier(t, 0.42, 0, 0.58, 1);
}

double anim_progress(const animation *a, double elapsed, bool *done) {
	elapsed -= a->delay;
	*done = false;
	if (elapsed <= 0) return 0;
	double d = fmax(a->duration, 1e-3);
	double cycle = elapsed / d;
	double runs = (double)a->repeat + 1;
	*done = a->repeat >= 0 && cycle >= runs;
	if (*done) cycle = runs;
	double i = floor(cycle), f = cycle - i;
	if (*done) {
		f = 1;
		i = runs - 1;
	}
	if (a->autoreverse && ((long)i) % 2 == 1) f = 1 - f;
	return ease(a, f);
}

static float lerp(float a, float b, double f) { return a + (b - a) * (float)f; }

static gboolean tick(GtkWidget *w, GdkFrameClock *clock, gpointer data);

static void start_ticking(void) {
	if (ticking || !H.main) return;
	ticking = true;
	gtk_widget_add_tick_callback(H.main->content, tick, NULL, NULL);
}

static void tween_free(node *n) {
	if (!n->tw) return;
	ak_vec_free(&n->tw->kept);
	free(n->tw);
	n->tw = NULL;
	for (uint32_t i = 0; i < tweening.len; i++)
		if (tweening.data[i] == n) {
			tweening.data[i] = tweening.data[--tweening.len];
			break;
		}
}

static tween *tween_of(node *n) {
	tween *t = n->tw;
	if (!t || t->exit) {
		if (t) tween_free(n);
		t = calloc(1, sizeof *t);
		if (!t) abort();
		n->tw = t;
		ak_vec_push(&tweening, n);
	}
	t->a = H.anim;
	t->start = lx_now();
	start_ticking();
	return t;
}

const float *lx_frame_now(node *n) {
	tween *t = n->tw;
	if (!t || !t->frame) return NULL;
	bool done;
	double f = anim_progress(&t->a, lx_now() - t->start, &done);
	for (int i = 0; i < 4; i++) t->now[i] = lerp(t->from[i], t->to[i], f);
	return t->now;
}

void lx_relayout(node *n) {
	if (!n->widget) return;
	GtkWidget *p = gtk_widget_get_parent(n->widget);
	if (p) gtk_widget_queue_allocate(p);
	gtk_widget_queue_allocate(n->widget);
}

// lx_animate moves n from where it shows to its new frame.
void lx_animate(node *n, const float old_[4]) {
	float old[4];
	memcpy(old, old_, sizeof old);
	const float *cur = lx_frame_now(n);
	if (cur) memcpy(old, cur, sizeof old);
	tween *t = tween_of(n);
	memcpy(t->from, old, sizeof old);
	memcpy(t->to, n->frame, sizeof t->to);
	t->frame = true;
	lx_relayout(n);
}

void lx_animate_opacity(node *n) {
	tween *t = tween_of(n);
	t->from_op = n->widget ? (float)gtk_widget_get_opacity(n->widget) : 1;
	t->to_op = n->opacity;
	t->op = true;
}

static const transform_t identity = {.sx = 1, .sy = 1};

void lx_animate_transform(node *n, transform_t to) {
	tween *t = tween_of(n);
	transform_t from = n->transform.set ? n->transform : identity;
	if (!to.set) to = identity;
	t->from_t = from;
	t->to_t = to;
	t->tr = true;
	n->transform = to;
	n->transform.set = true;
}

static void exited(node *n, tween *t);

// tick steps the animations and sends Frame while the core asks for it.
static gboolean tick(GtkWidget *w, GdkFrameClock *clock, gpointer data) {
	(void)w;
	(void)data;
	double now = lx_now();
	for (uint32_t i = 0; i < tweening.len;) {
		node *n = tweening.data[i];
		tween *t = n->tw;
		bool done;
		double f = anim_progress(&t->a, now - t->start, &done);
		if (t->op && n->widget) gtk_widget_set_opacity(n->widget, lerp(t->from_op, t->to_op, f));
		if (t->tr) {
			n->transform = (transform_t){.set = true,
				.tx = lerp(t->from_t.tx, t->to_t.tx, f),
				.ty = lerp(t->from_t.ty, t->to_t.ty, f),
				.sx = lerp(t->from_t.sx, t->to_t.sx, f),
				.sy = lerp(t->from_t.sy, t->to_t.sy, f),
				.rotation = lerp(t->from_t.rotation, t->to_t.rotation, f)};
		}
		if (t->frame || t->tr) lx_relayout(n);
		if (!done) {
			i++;
			continue;
		}
		// Done: out of the list (another takes its place at i).
		tweening.data[i] = tweening.data[--tweening.len];
		n->tw = NULL;
		if (t->tr) {
			transform_t e = t->to_t;
			e.set = !(e.tx == 0 && e.ty == 0 && e.sx == 1 && e.sy == 1 && e.rotation == 0);
			n->transform = e;
			lx_relayout(n);
		}
		if (t->exit) exited(n, t);
		ak_vec_free(&t->kept);
		free(t);
	}
	if (H.frames && clock && H.root) {
		gint64 us = gdk_frame_clock_get_frame_time(clock);
		ak_event ev = {.id = H.root->id, .kind = AK_EVENT_FRAME, .f = (double)us / 1e6};
		lx_dispatch(&ev);
	}
	if (tweening.len == 0 && !H.frames) ticking = false;
	return ticking;
}

void lx_set_frames(bool on) {
	H.frames = on;
	if (on) start_ticking();
}

// ---- Transitions ----

void lx_set_transition(node *n, ak_span b) {
	if (!b.len) {
		free(n->transition);
		n->transition = NULL;
		return;
	}
	ak_reader r = {(const uint8_t *)b.p, b.len, 0, false};
	if (!n->transition) n->transition = calloc(1, sizeof *n->transition);
	if (!n->transition) abort();
	n->transition->fade = ak_read_u8(&r) != 0;
	n->transition->scale = ak_read_f32(&r);
	n->transition->dx = ak_read_f32(&r);
	n->transition->dy = ak_read_f32(&r);
}

// lx_enter_from brings a view in from its transition's state, as it's
// placed for the first time during an animation.
void lx_enter_from(node *n) {
	transition *tr = n->transition;
	tween *t = tween_of(n);
	if (tr->fade) {
		t->from_op = 0;
		t->to_op = n->opacity;
		t->op = true;
		gtk_widget_set_opacity(n->widget, 0);
	}
	transform_t from = {.set = true, .sx = 1, .sy = 1, .tx = tr->dx, .ty = tr->dy};
	if (tr->scale > 0) from.sx = from.sy = tr->scale;
	transform_t to = n->transform.set ? n->transform : identity;
	t->from_t = from;
	t->to_t = to;
	t->tr = true;
	n->transform = from;
}

static bool match_out(node *n);

// kept_by_exit keeps a destroyed view inside one going out: it goes with
// it. It reports whether it does.
static bool kept_by_exit(node *n) {
	if (!tweening.len || !n->widget) return false;
	for (GtkWidget *w = gtk_widget_get_parent(n->widget); w; w = gtk_widget_get_parent(w)) {
		node *x = lx_view_node(w);
		if (x && x->tw && x->tw->exit) {
			ak_vec_push(&x->tw->kept, n);
			return true;
		}
	}
	return false;
}

// lx_exit keeps a destroyed view on screen, going out to its transition's
// state, until the animation ends. It reports whether it does.
bool lx_exit(node *n) {
	if (match_out(n) || kept_by_exit(n)) return true;
	if (!H.animating || !n->transition || !n->widget || !gtk_widget_get_parent(n->widget)) return false;
	transition *tr = n->transition;
	tween *t = tween_of(n);
	t->exit = true;
	t->widget = n->widget;
	if (tr->fade) {
		t->from_op = (float)gtk_widget_get_opacity(n->widget);
		t->to_op = 0;
		t->op = true;
	}
	transform_t from = n->transform.set ? n->transform : identity;
	transform_t to = {.set = true, .sx = 1, .sy = 1, .tx = tr->dx, .ty = tr->dy};
	if (tr->scale > 0) to.sx = to.sy = tr->scale;
	t->from_t = from;
	t->to_t = to;
	t->tr = true;
	return true;
}

// exited removes a view that went out, and the views kept in it.
static void exited(node *n, tween *t) {
	for (uint32_t i = 0; i < t->kept.len; i++) lx_release(t->kept.data[i]);
	if (t->widget && gtk_widget_get_parent(t->widget)) gtk_widget_unparent(t->widget);
	lx_release(n);
}

void lx_release_anim(node *n) {
	if (n->tw) {
		// Released while it animated (a view kept by an exit goes with it).
		for (uint32_t i = 0; i < n->tw->kept.len; i++) {
			node *k = n->tw->kept.data[i];
			if (!k->widget) continue;
			lx_release(k);
		}
		tween_free(n);
	}
	free(n->transition);
	n->transition = NULL;
	free(n->match_key);
	n->match_key = NULL;
}

// ---- Style.Match ----

typedef struct {
	char *key;
	node *top;
	float r[4];  // in top
} match_place;

typedef struct {
	char *key;
	node *n;
	node *top;
	float r[4];
} match_out_view;

static node_vec match_keyed;  // the views with keys
static AK_VEC(match_place) noted;
static bool noted_valid;
static AK_VEC(match_out_view) outs;
static bool match_queued;

static void forget_keyed(node *n) {
	for (uint32_t i = 0; i < match_keyed.len; i++)
		if (match_keyed.data[i] == n) {
			match_keyed.data[i] = match_keyed.data[--match_keyed.len];
			return;
		}
}

static void clear_ins(void *ctx) {
	(void)ctx;
	for (uint32_t i = 0; i < match_keyed.len; i++) match_keyed.data[i]->match_in = false;
}

void lx_set_match(node *n, ak_span key) {
	if (!key.len) {
		free(n->match_key);
		n->match_key = NULL;
		n->match_in = false;
		forget_keyed(n);
		return;
	}
	if (!n->match_key) ak_vec_push(&match_keyed, n);
	str_set(&n->match_key, key);
	if (H.animating) {
		bool any = false;
		for (uint32_t i = 0; i < match_keyed.len; i++) any |= match_keyed.data[i]->match_in;
		// The batch's views that may be coming: matched as the frames
		// batch ends, forgotten once the core returns.
		if (!any) lx_after_core(clear_ins, NULL);
		n->match_in = true;
	}
}

// match_top is the outermost view n's view is placed in by the host,
// through views the host places (and scrolled content), and n's origin in
// it.
static node *match_top(node *n, float *x, float *y) {
	*x = *y = 0;
	node *c = n;
	for (;;) {
		node *p = c->parent;
		if (!p || !c->widget) return c;
		if (p->scroll && p->scroll->content == c) {
			*x += c->frame[0] - (float)gtk_adjustment_get_value(p->scroll->hadj);
			*y += c->frame[1] - (float)gtk_adjustment_get_value(p->scroll->vadj);
		} else if (gtk_widget_get_parent(c->widget) == p->widget) {
			const float *f = lx_frame_now(c);
			if (!f) f = c->frame;
			*x += f[0];
			*y += f[1];
		} else {
			return c;
		}
		c = p;
	}
}

static void note_matches(void) {
	if (noted_valid) return;
	noted_valid = true;
	for (uint32_t i = 0; i < noted.len; i++) free(noted.data[i].key);
	noted.len = 0;
	for (uint32_t i = 0; i < match_keyed.len; i++) {
		node *n = match_keyed.data[i];
		if (!n->placed || !n->widget || !gtk_widget_get_parent(n->widget) || !gtk_widget_get_mapped(n->widget)) continue;
		float x, y;
		node *top = match_top(n, &x, &y);
		if (top == n) continue;
		const float *f = lx_frame_now(n);
		if (!f) f = n->frame;
		match_place p = {strdup(n->match_key), top, {x, y, f[2], f[3]}};
		ak_vec_push(&noted, p);
	}
}

static void fade_out(node *n) {
	tween *t = n->tw;
	if (!t || !n->widget) {
		if (t) {
			n->tw = NULL;
			for (uint32_t i = 0; i < tweening.len; i++)
				if (tweening.data[i] == n) tweening.data[i] = tweening.data[--tweening.len];
			exited(n, t);
			ak_vec_free(&t->kept);
			free(t);
		}
		return;
	}
	t->start = lx_now();
	t->from_op = (float)gtk_widget_get_opacity(n->widget);
	t->to_op = 0;
	t->op = true;
	start_ticking();
}

static void fade_outs(void *ctx) {
	(void)ctx;
	match_queued = false;
	while (outs.len) {
		match_out_view o = outs.data[--outs.len];
		free(o.key);
		fade_out(o.n);
	}
}

// match_out keeps a view with a key destroyed during an animation in its
// top, where it was, until its match comes. It reports whether it did.
static bool match_out(node *n) {
	if (!H.animating || !match_keyed.len) return false;
	note_matches();  // the first destroy of the batch: the tree is still whole
	if (!n->match_key) return false;
	char *key = n->match_key;
	n->match_key = NULL;
	n->match_in = false;
	forget_keyed(n);
	match_place *o = NULL;
	for (uint32_t i = 0; i < noted.len; i++)
		if (strcmp(noted.data[i].key, key) == 0) o = &noted.data[i];
	if (!o || o->r[2] <= 0 || o->r[3] <= 0 || !n->widget || !o->top->widget || node_get(o->top->id) != o->top) {
		free(key);
		return false;
	}
	for (uint32_t i = 0; i < outs.len; i++)
		if (strcmp(outs.data[i].key, key) == 0) {
			// Two going with one key: the first goes now.
			node *old = outs.data[i].n;
			free(outs.data[i].key);
			outs.data[i] = outs.data[--outs.len];
			if (old->tw) {
				tween *t = old->tw;
				old->tw = NULL;
				for (uint32_t j = 0; j < tweening.len; j++)
					if (tweening.data[j] == old) tweening.data[j] = tweening.data[--tweening.len];
				exited(old, t);
				ak_vec_free(&t->kept);
				free(t);
			}
			break;
		}
	// Into its top, on top of what's there, where it was.
	lx_detach(n->widget);
	gtk_widget_set_parent(n->widget, o->top->widget);
	memcpy(n->frame, o->r, sizeof n->frame);
	tween *t = tween_of(n);
	t->exit = true;
	t->widget = n->widget;
	memcpy(t->from, o->r, sizeof t->from);
	memcpy(t->to, o->r, sizeof t->to);
	t->frame = true;
	gtk_widget_queue_allocate(o->top->widget);
	match_out_view mo = {key, n, o->top, {o->r[0], o->r[1], o->r[2], o->r[3]}};
	ak_vec_push(&outs, mo);
	if (!match_queued) {
		// If no frames come to match it, it fades once the core has
		// returned.
		match_queued = true;
		lx_after_core(fade_outs, NULL);
	}
	return true;
}

// run_matches matches the views going and coming, as an animation's
// batch ends: once the frames batch's has, the new views are placed.
static void run_matches(void) {
	noted_valid = false;
	if (!outs.len) return;
	for (uint32_t k = 0; k < match_keyed.len; k++) {
		node *in = match_keyed.data[k];
		if (!in->match_in) continue;
		uint32_t oi = UINT32_MAX;
		for (uint32_t i = 0; i < outs.len; i++)
			if (strcmp(outs.data[i].key, in->match_key) == 0) oi = i;
		if (oi == UINT32_MAX || !in->placed || !in->widget || !in->parent) continue;
		float x, y;
		node *top = match_top(in, &x, &y);
		match_out_view o = outs.data[oi];
		if (top != o.top) continue;
		outs.data[oi] = outs.data[--outs.len];
		in->match_in = false;
		// The new one, from the old one's place, in its parent.
		float px = x - in->frame[0], py = y - in->frame[1];
		tween *t = tween_of(in);
		t->from[0] = o.r[0] - px;
		t->from[1] = o.r[1] - py;
		t->from[2] = o.r[2];
		t->from[3] = o.r[3];
		memcpy(t->to, in->frame, sizeof t->to);
		t->frame = true;
		lx_relayout(in);
		// The old one, to the new one's place, fading.
		tween *ot = o.n->tw;
		free(o.key);
		if (!ot) continue;
		ot->a = H.anim;
		ot->start = lx_now();
		memcpy(ot->from, o.r, sizeof ot->from);
		ot->to[0] = x;
		ot->to[1] = y;
		ot->to[2] = in->frame[2];
		ot->to[3] = in->frame[3];
		ot->frame = true;
		ot->from_op = (float)gtk_widget_get_opacity(o.n->widget);
		ot->to_op = 0;
		ot->op = true;
		start_ticking();
	}
}

static bool anim_common(node *n, ak_prop p, const ak_value *v) {
	if (p != AK_PROP_MATCH) return false;
	lx_set_match(n, v->s);
	return true;
}

static void anim_release(node *n) {
	forget_keyed(n);
	lx_release_anim(n);
}

const area lx_area_anim = {.name = "anim", .common = anim_common, .release = anim_release};
