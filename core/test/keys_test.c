// Keys (keys.c), animate { } and frames (animate.c): ports of
// ui/keys_test.go, media_test.go's TestAnimateWrapsWhatItChanges and
// TestAnimating, ui_test.go's TestOnFrame.
#include "uih.h"

static const uint8_t shortcut_kinds[] = {AK_K_REF, AK_K_BOOL, AK_K_BOOL, AK_K_BOOL, AK_K_BOOL, AK_K_REF};
static const ak_shape shortcut_shape = {"Shortcut", 6, shortcut_kinds, NULL, NULL, 0, NULL, false, NULL};

static char log_buf[256];

static void logs(const char *s) {
	size_t n = strlen(log_buf);
	snprintf(log_buf + n, sizeof log_buf - n, "%s%s", n ? "|" : "", s);
}

static ak_val log_run(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	logs((const char *)c->fn);
	return noval();
}

static ak_struct *shortcut(const char *key, uint8_t mods, ak_invoke_fn run, const void *data) {
	ak_struct *s = ak_struct_new(&shortcut_shape);
	s->f[0].p = ak_str_c(key);
	for (int i = 0; i < 4; i++) s->f[1 + i].i = (mods >> i) & 1;
	s->f[5].p = fn(run, data);
	return s;
}

static ak_arr *keymap(void) { return ak_arr_new(AK_K_REF, &shortcut_shape, 4); }

static void push(ak_arr **m, ak_struct *s) { ak_arr_push(m, (ak_val){.p = s}); }

// ---- Keys around a field (TestKeysAroundField) ----

static void *sel;
static int64_t opened;

static ak_val down(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	write_i(sel, peek_i(sel) + 1);
	return noval();
}

static ak_val up(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	write_i(sel, peek_i(sel) - 1);
	return noval();
}

static ak_val enter(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	opened = peek_i(sel);
	return noval();
}

static ak_val hundred(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	write_i(sel, 100);
	return noval();
}

static ak_val around_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *v = ak_ui_new(AK_EL_VIEW);
	ak_arr *m = keymap();
	push(&m, shortcut("down", 0, down, NULL));
	push(&m, shortcut("up", 0, up, NULL));
	push(&m, shortcut("enter", 0, enter, NULL));
	push(&m, shortcut("D", 1, hundred, NULL));  // ⌘D
	set(v, AK_KEY_KEYS, (ak_val){.p = m});
	ak_view *field = ak_ui_new(AK_EL_TEXT_FIELD);
	set_str(field, AK_KEY_TEXT_FIELD_TEXT, "");
	add(v, field);
	return (ak_val){.p = v};
}

// A search field's Up, Down and Enter go to the view around it, which
// declared them; what it didn't declare goes on to the field.
TEST(keys_around_field) {
	sel = cell_int(0);
	opened = -1;
	ak_uitest *a = mount(around_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_view *field = nth(a, AK_KIND_TEXT_FIELD, 0);
	const char *seq[] = {"down", "down", "up", "down"};
	for (int i = 0; i < 4; i++) CHECK(ak_fake_key(f, field, seq[i], 0));
	CHECK_EQ_INT(peek_i(sel), 2);
	ak_fake_key(f, field, "enter", 0);
	CHECK_EQ_INT(opened, 2);
	// A character, any case, with its modifiers exactly.
	CHECK(ak_fake_key(f, field, "d", 1) && peek_i(sel) == 100);
	CHECK(!ak_fake_key(f, field, "a", 0));
	CHECK(!ak_fake_key(f, field, "down", 2));
	CHECK(!ak_fake_key(f, field, "d", 0));
	CHECK(!ak_fake_key(f, field, "d", 3));
	CHECK(!ak_fake_key(f, field, "escape", 0));
	done(a);
	ak_release(sel);
}

// ---- The nearest view first, then the root's (TestKeysNearestFirst) ----

static ak_val nearest_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_arr *win = keymap();
	push(&win, shortcut("escape", 0, log_run, "window escape"));
	push(&win, shortcut("space", 0, log_run, "window space"));
	ak_ui_use_keys(win);
	ak_release(win);
	ak_view *v = ak_ui_new(AK_EL_VIEW);
	ak_arr *outer = keymap();
	push(&outer, shortcut("escape", 0, log_run, "outer escape"));
	push(&outer, shortcut("tab", 0, log_run, "outer tab"));
	set(v, AK_KEY_KEYS, (ak_val){.p = outer});
	ak_view *field = ak_ui_new(AK_EL_TEXT_FIELD);
	ak_arr *own = keymap();
	push(&own, shortcut("tab", 0, log_run, "field tab"));
	set(field, AK_KEY_KEYS, (ak_val){.p = own});
	add(v, field);
	return (ak_val){.p = v};
}

// The nearest view with a key takes it: a field's own keys, then the
// views around it, then the root's (use_keys), which also takes keys when
// nothing has the focus.
TEST(keys_nearest_first) {
	log_buf[0] = 0;
	ak_uitest *a = mount(nearest_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_view *field = nth(a, AK_KIND_TEXT_FIELD, 0);
	ak_fake_key(f, field, "tab", 0);
	ak_fake_key(f, field, "escape", 0);
	ak_fake_key(f, field, "space", 0);
	ak_fake_key(f, NULL, "space", 0);  // nothing focused: the root's
	CHECK(!ak_fake_key(f, NULL, "tab", 0));  // nor the views': no one
	CHECK_EQ_STR(log_buf, "field tab|outer escape|window space|window space");
	done(a);
}

static void *showing;

static ak_val viewer(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_arr *m = keymap();
	push(&m, shortcut("left", 0, log_run, "viewer"));
	push(&m, shortcut("right", 0, log_run, "viewer right"));
	ak_ui_use_keys(m);
	ak_release(m);
	return (ak_val){.p = text("viewer")};
}

static ak_val owners_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_arr *m = keymap();
	push(&m, shortcut("left", 0, log_run, "app"));
	ak_ui_use_keys(m);
	ak_release(m);
	ak_closure *cond = fn(read_cell, showing), *then = fn(viewer, NULL);
	ak_view *s = ak_ui_show(cond, then, NULL);
	ak_release(cond), ak_release(then);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	add(col, s);
	return (ak_val){.p = col};
}

// use_keys' keys go with their component, and the last one's win.
TEST(use_keys_owners) {
	log_buf[0] = 0;
	showing = ak_vm_cell_new((ak_val){.i = 1}, AK_K_BOOL, NULL).p;
	ak_uitest *a = mount(owners_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_key(f, NULL, "left", 0);
	ak_fake_key(f, NULL, "right", 0);
	write_i(showing, 0);
	ak_ui_flush();
	ak_fake_key(f, NULL, "left", 0);
	CHECK(!ak_fake_key(f, NULL, "right", 0));
	CHECK_EQ_STR(log_buf, "viewer|viewer right|app");
	done(a);
	ak_release(showing);
}

// ---- A Window's own (use_keys in its content) ----

static ak_val panel_content(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_arr *m = keymap();
	push(&m, shortcut("escape", 0, log_run, "panel escape"));
	push(&m, shortcut("down", 0, log_run, "panel down"));
	ak_ui_use_keys(m);
	ak_release(m);
	ak_view *field = ak_ui_new(AK_EL_TEXT_FIELD);
	set_str(field, AK_KEY_TEXT_FIELD_PLACEHOLDER, "in the panel");
	return (ak_val){.p = field};
}

static ak_val panel_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_arr *m = keymap();
	push(&m, shortcut("escape", 0, log_run, "main escape"));
	push(&m, shortcut("space", 0, log_run, "main space"));
	ak_ui_use_keys(m);
	ak_release(m);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	ak_view *field = ak_ui_new(AK_EL_TEXT_FIELD);
	set_str(field, AK_KEY_TEXT_FIELD_PLACEHOLDER, "in the main window");
	add(col, field);
	ak_view *w = ak_ui_new(AK_EL_WINDOW);
	set_i(w, AK_KEY_WINDOW_OPEN, 1);
	set_i(w, AK_KEY_WINDOW_PANEL, 1);
	ak_closure *content = fn(panel_content, NULL);
	ak_ui_lazy(w, content);
	ak_release(content);
	add(col, w);
	return (ak_val){.p = col};
}

// use_keys in a Window's content gives that window keys, not the main
// window's: a key in the panel goes to the panel's, and no further; one in
// the main window, to the main window's.
TEST(use_keys_in_window) {
	log_buf[0] = 0;
	ak_uitest *a = mount(panel_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_view *main_field = nth(a, AK_KIND_TEXT_FIELD, 0), *panel_field = nth(a, AK_KIND_TEXT_FIELD, 1);
	REQUIRE(main_field && panel_field);
	CHECK(ak_fake_key(f, panel_field, "escape", 0));
	CHECK(ak_fake_key(f, panel_field, "down", 0));
	CHECK(!ak_fake_key(f, panel_field, "space", 0));  // the main window's: not here
	CHECK(ak_fake_key(f, main_field, "escape", 0));
	CHECK(!ak_fake_key(f, main_field, "down", 0));  // the panel's: not here
	CHECK_EQ_STR(log_buf, "panel escape|panel down|main escape");
	done(a);
}

// ---- animate { } (TestAnimateWrapsWhatItChanges, TestAnimating) ----

static void *wide;

static ak_val wide_width(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	return (ak_val){.i = ak_length_pt(ak_vm_cell_get(wide, true).i ? 200 : 100)};
}

static ak_val wide_opacity(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	return (ak_val){.f = ak_vm_cell_get(wide, true).i ? 0.5 : 1};
}

static ak_val animate_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	ak_view *v = ak_ui_new(AK_EL_VIEW);
	bind(v, AK_KEY_WIDTH, wide_width, NULL);
	bind(v, AK_KEY_OPACITY, wide_opacity, NULL);
	set_i(v, AK_KEY_HEIGHT, ak_length_pt(50));
	add(col, v);
	add(col, text("x"));
	return (ak_val){.p = col};
}

static ak_val widen(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	write_i(wide, 1);
	return noval();
}

static void op_shapes(ak_fake *f, char *out, size_t cap) {
	out[0] = 0;
	for (uint32_t i = 0; i < ak_fake_op_count(f); i++) {
		ak_op op = ak_fake_op_at(f, i);
		const char *s = NULL;
		if (op.code == AK_OP_COMMAND && op.cmd == AK_CMD_ANIMATE_BEGIN) s = "begin";
		else if (op.code == AK_OP_COMMAND && op.cmd == AK_CMD_ANIMATE_END) s = "end";
		else if (op.code == AK_OP_SET_PROP) s = "prop";
		else if (op.code == AK_OP_SET_FRAME) s = "frame";
		if (s) {
			size_t n = strlen(out);
			snprintf(out + n, cap - n, "%s ", s);
		}
	}
}

// What animate's body changes animates: the props, then the frames, each
// between AnimateBegin and AnimateEnd; a later flush doesn't.
TEST(animate_wraps_what_it_changes) {
	wide = ak_vm_cell_new((ak_val){.i = 0}, AK_K_BOOL, NULL).p;
	ak_uitest *a = mount(animate_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_reset(f);
	ak_closure *body = fn(widen, NULL);
	ak_ui_animate(NULL, body);
	ak_release(body);
	ak_ui_flush();
	char got[128];
	op_shapes(f, got, sizeof got);
	CHECK_EQ_STR(got, "begin prop end begin frame end ");
	ak_fake_index_vec cmds = {0};
	ak_fake_commands(f, AK_CMD_ANIMATE_BEGIN, &cmds);
	REQUIRE(cmds.len == 2);
	ak_op op = ak_fake_op_at(f, cmds.data[0]);
	float d;
	memcpy(&d, op.value.s.p, 4);
	CHECK(op.value.s.len == 22 && d == 0.25f);  // no Animation: a quarter second
	ak_vec_free(&cmds);
	ak_fake_reset(f);
	write_i(wide, 0);
	ak_ui_flush();
	ak_fake_commands(f, AK_CMD_ANIMATE_BEGIN, &cmds);
	CHECK_EQ_INT(cmds.len, 0);
	ak_vec_free(&cmds);
	done(a);
	ak_release(wide);
}

static void *seen_cell;
static char seen[64];

static void seen_run(void *ctx) {
	AK_UNUSED(ctx);
	ak_vm_cell_get(seen_cell, true);
	size_t n = strlen(seen);
	snprintf(seen + n, sizeof seen - n, "%s", ak_ui_animating() ? "a" : "-");
}

static const ak_comp_info seen_info = {.label = "seen"};

static ak_val animating_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_effect_new(seen_run, NULL, AK_EFFECT_RENDER, &seen_info);
	ak_view *v = ak_ui_new(AK_EL_VIEW);
	return (ak_val){.p = v};
}

static ak_val bump(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	write_i(seen_cell, peek_i(seen_cell) + 1);
	return noval();
}

static const uint8_t anim_kinds[] = {AK_K_INT, AK_K_INT, AK_K_INT, AK_K_INT, AK_K_BOOL, AK_K_FLOAT, AK_K_FLOAT, AK_K_REF};
static const ak_shape anim_shape = {"Animation", 8, anim_kinds, NULL, NULL, 0, NULL, false, NULL};

static int dones;

static ak_val count_done(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	dones++;
	return noval();
}

// animating says the changes being made are in an animation, until its
// flush; an animation's done is called once it's complete.
TEST(animating) {
	seen[0] = 0;
	dones = 0;
	seen_cell = cell_int(0);
	ak_sched_manual_clock(true);
	ak_uitest *a = mount(animating_app, NULL);
	ak_struct *anim = ak_struct_new(&anim_shape);
	anim->f[0].i = 400000000;  // 400ms
	anim->f[1].i = AK_ANIMATION_CURVE_SPRING;
	anim->f[3].i = 1;  // once more
	anim->f[7].p = fn(count_done, NULL);
	ak_closure *body = fn(bump, NULL);
	ak_ui_animate(anim, body);
	ak_ui_flush();
	write_i(seen_cell, 2);
	ak_ui_flush();
	CHECK_EQ_STR(seen, "-a-");
	// Done after two runs of 400ms.
	ak_sched_advance(700000000);
	ak_uitest_settle(a);
	CHECK_EQ_INT(dones, 0);
	ak_sched_advance(200000000);
	ak_uitest_settle(a);
	CHECK_EQ_INT(dones, 1);
	ak_release(body);
	ak_release(anim);
	done(a);
	ak_sched_manual_clock(false);
	ak_release(seen_cell);
}

// ---- onFrame (TestOnFrame) ----

static void *frames_shown, *frame_n;
static int64_t frame_times[8];
static int nframes;

static ak_val frame_text(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	return (ak_val){.p = ak_str_from_int(ak_vm_cell_get(frame_n, true).i)};
}

static ak_val on_frame_cb(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	if (nframes < 8) frame_times[nframes] = a[0].i;
	nframes++;
	write_i(frame_n, peek_i(frame_n) + 1);
	return noval();
}

static ak_val animating_view(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_closure *cb = fn(on_frame_cb, NULL);
	ak_closure *stop = ak_ui_on_frame(cb);
	ak_release(stop);
	ak_release(cb);
	return (ak_val){.p = text("animating")};
}

static ak_val frame_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	ak_view *t = ak_ui_new(AK_EL_TEXT);
	bind(t, AK_KEY_TEXT_TEXT, frame_text, NULL);
	add(col, t);
	ak_closure *cond = fn(read_cell, frames_shown), *then = fn(animating_view, NULL);
	add(col, ak_ui_show(cond, then, NULL));
	ak_release(cond), ak_release(then);
	return (ak_val){.p = col};
}

static int frames_cmd(ak_fake *f, uint8_t *last) {
	ak_fake_index_vec cmds = {0};
	ak_fake_commands(f, AK_CMD_FRAMES, &cmds);
	int n = (int)cmds.len;
	if (n) {
		ak_op op = ak_fake_op_at(f, cmds.data[n - 1]);
		*last = op.value.s.len ? (uint8_t)op.value.s.p[0] : 0xff;
	}
	ak_vec_free(&cmds);
	return n;
}

// onFrame calls its callback at each frame with its time, while the host
// is asked for frames; the callback goes with its owner, and frames stop.
TEST(on_frame) {
	nframes = 0;
	frames_shown = ak_vm_cell_new((ak_val){.i = 1}, AK_K_BOOL, NULL).p;
	frame_n = cell_int(0);
	ak_uitest *a = mount(frame_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	uint8_t last = 0;
	CHECK(frames_cmd(f, &last) == 1 && last == 1);
	ak_id rid = root(a)->id;
	ak_fake_reset(f);
	dispatch(a, (ak_event){.id = rid, .kind = AK_EVENT_FRAME, .f = 1.5});
	dispatch(a, (ak_event){.id = rid, .kind = AK_EVENT_FRAME, .f = 1.5 + 1.0 / 60});
	CHECK(nframes == 2 && frame_times[0] == 1500000000 && one(a, AK_KIND_TEXT, "2") != NULL);
	ak_fake_reset(f);
	write_i(frames_shown, 0);
	ak_ui_flush();
	CHECK(frames_cmd(f, &last) == 1 && last == 0);
	dispatch(a, (ak_event){.id = rid, .kind = AK_EVENT_FRAME, .f = 2});
	CHECK_EQ_INT(nframes, 2);
	done(a);
	ak_release(frames_shown), ak_release(frame_n);
}

T_MAIN()
