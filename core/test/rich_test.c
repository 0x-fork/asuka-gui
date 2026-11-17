// RichText and its inline components, Selectable, a selectable Text, a
// rich TextField, its paste types, formats and accessory (rich.c).
#include "uih.h"

static char log_buf[512];

static void logs(const char *s) {
	size_t n = strlen(log_buf);
	snprintf(log_buf + n, sizeof log_buf - n, "%s%s", n ? "|" : "", s);
}

static const uint8_t inline_kinds[] = {AK_K_REF, AK_K_REF};
static const ak_shape inline_shape = {"Inline", 2, inline_kinds, NULL, NULL, 0, NULL, false, NULL};

static ak_struct *span(const char *text) {
	ak_struct *s = ak_struct_new(&ak_ui_shape_span);
	s->f[0].p = ak_str_c(text);
	ak_struct *f = ak_struct_new(&ak_ui_shape_font);
	f->f[3].p = ak_str_c("");
	s->f[1].p = f;
	s->f[8].p = ak_str_c("");
	return s;
}

static ak_struct *strong(const char *text) {
	ak_struct *s = span(text);
	((ak_struct *)s->f[1].p)->f[2].i = AK_FONT_WEIGHT_BOLD;
	return s;
}

static ak_struct *link(const char *text, const char *url) {
	ak_struct *s = span(text);
	ak_release(s->f[8].p);
	s->f[8].p = ak_str_c(url);
	return s;
}

static ak_struct *code(const char *text) {
	ak_struct *s = span(text);
	s->f[3].i = 1;
	s->f[7].i = (int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_SECONDARY_BACKGROUND);
	return s;
}

static ak_struct *embed(const char *key, ak_invoke_fn build) {
	ak_struct *s = span("");
	ak_struct *in = ak_struct_new(&inline_shape);
	in->f[0].p = ak_str_c(key);
	in->f[1].p = fn(build, NULL);
	s->f[9].p = in;
	return s;
}

static ak_arr *rich(ak_struct **spans, int n) {
	ak_arr *a = ak_arr_new(AK_K_REF, &ak_ui_shape_span, (uint32_t)n);
	for (int i = 0; i < n; i++) ak_arr_push(&a, (ak_val){.p = spans[i]});
	return a;
}

// paras is v's content as text, a paragraph a line, runs split by "/".
static void paras(const ak_fake_view *v, ak_prop p, char *out, size_t cap, ak_rich_run_vec *runs) {
	ak_span b = bytes_of(v, p);
	runs->len = 0;
	ak_decode_rich((const uint8_t *)b.p, b.len, runs);
	out[0] = 0;
	for (uint32_t i = 0; i < runs->len; i++) {
		size_t n = strlen(out);
		snprintf(out + n, cap - n, "%s%.*s", i && !(runs->data[i].text.len == 1 && runs->data[i].text.p[0] == '\n') ? "/" : "",
		         (int)runs->data[i].text.len, runs->data[i].text.p);
	}
}

// ---- RichText (TestRichText) ----

static void *text_cell;

static ak_val log_url(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	logs(((ak_str *)a[0].p)->data);
	return noval();
}

static ak_val rich_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	ak_view *r = ak_ui_new(AK_EL_RICH_TEXT);
	bind(r, AK_KEY_RICH_TEXT_TEXT, read_cell, text_cell);
	on(r, AK_EV_RICH_TEXT_LINK, log_url, NULL);
	ak_struct *font = ak_struct_new(&ak_ui_shape_font);
	font->f[0].i = AK_TEXT_STYLE_BODY;
	font->f[3].p = ak_str_c("");
	set(r, AK_KEY_FONT, (ak_val){.p = font});
	set_i(r, AK_KEY_COLOR, (int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_SECONDARY_LABEL));
	add(col, r);
	return (ak_val){.p = col};
}

static ak_arr *docs_text(bool more) {
	ak_struct *s[] = {span("Hello "), strong("world"), span(", see "), link("the docs", "https://x.dev"), span(".\nRun "), code("go test"), span("."), span(" More.")};
	if (!more) ak_release(s[7]);
	return rich(s, more ? 8 : 7);
}

// Spans go as styled paragraphs; the view's own style carries the
// defaults; appending sends the content once, the same content nothing.
TEST(rich_text) {
	log_buf[0] = 0;
	text_cell = cell_ref(docs_text(false));
	ak_uitest *a = mount(rich_app, NULL);
	ak_fake_view *v = nth(a, AK_KIND_RICH_TEXT, 0);
	REQUIRE(v);
	char got[256];
	ak_rich_run_vec runs = {0};
	paras(v, AK_PROP_RICH_TEXT_CONTENT, got, sizeof got, &runs);
	CHECK_EQ_STR(got, "Hello /world/, see /the docs/.\n/Run /go test/.");
	REQUIRE(runs.len == 9);
	CHECK(runs.data[1].style.weight == AK_FONT_WEIGHT_BOLD && ak_span_eqc(runs.data[3].style.link, "https://x.dev"));
	CHECK((runs.data[7].style.flags & AK_RICH_MONO) && runs.data[7].style.background == ak_semantic_color(AK_SEMANTIC_COLOR_SECONDARY_BACKGROUND));
	CHECK(ak_rich_style_eq(&runs.data[0].style, &runs.data[2].style));
	ak_vec_free(&runs);
	CHECK_EQ_INT(ak_fake_int(v, AK_PROP_TEXT_STYLE), AK_TEXT_STYLE_BODY);
	CHECK_EQ_F(v->frame[3], 40);  // a line each paragraph
	reset(a);
	write_p(text_cell, docs_text(true));
	ak_ui_flush();
	write_p(text_cell, docs_text(true));
	ak_ui_flush();
	CHECK_EQ_INT(sets(a), 1);
	ak_fake_listens(v, AK_EVENT_LINK);
	dispatch(a, (ak_event){.id = v->id, .kind = AK_EVENT_LINK, .s = AK_STR("https://x.dev")});
	CHECK_EQ_STR(log_buf, "https://x.dev");
	done(a);
	ak_release(text_cell);
}

// ---- Inline components (TestRichTextInline, TestRichTextInlineDisposal) ----

static int builds, cleanups;

static void count_cleanup(void *ctx) {
	AK_UNUSED(ctx);
	cleanups++;
}

static ak_val badge(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	builds++;
	ak_on_cleanup(count_cleanup, NULL);
	ak_view *v = ak_ui_new(AK_EL_VIEW);
	set_i(v, AK_KEY_WIDTH, ak_length_pt(30));
	set_i(v, AK_KEY_HEIGHT, ak_length_pt(44));
	set_i(v, AK_KEY_BACKGROUND, (int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_ACCENT));
	return (ak_val){.p = v};
}

static ak_val inline_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	ak_view *r = ak_ui_new(AK_EL_RICH_TEXT);
	bind(r, AK_KEY_RICH_TEXT_TEXT, read_cell, text_cell);
	add(col, r);
	return (ak_val){.p = col};
}

// An inline component is built once for its key, a child view of the
// RichText whose size the host is told before measuring the text; a span
// gone disposes it.
TEST(rich_text_inline) {
	builds = cleanups = 0;
	ak_struct *s1[] = {span("a "), embed("b", badge)};
	text_cell = cell_ref(rich(s1, 2));
	ak_uitest *a = mount(inline_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_view *v = nth(a, AK_KIND_RICH_TEXT, 0);
	REQUIRE(v);
	CHECK(builds == 1 && v->children.len == 1);
	REQUIRE(v->children.len == 1);
	ak_fake_view *holder = v->children.data[0];
	ak_span in = bytes_of(v, AK_PROP_RICH_TEXT_INLINES);
	REQUIRE(in.len == 16);
	uint32_t id;
	float w, h;
	memcpy(&id, in.p + 4, 4), memcpy(&w, in.p + 8, 4), memcpy(&h, in.p + 12, 4);
	CHECK(id == holder->id && w == 30 && h == 44);
	char got[64];
	ak_rich_run_vec runs = {0};
	paras(v, AK_PROP_RICH_TEXT_CONTENT, got, sizeof got, &runs);
	CHECK(runs.len == 2 && (runs.data[1].style.flags & AK_RICH_INLINE) && ak_span_eqc(runs.data[1].text, "\xEF\xBF\xBC"));
	ak_vec_free(&runs);
	CHECK_EQ_F(v->frame[3], 44);  // measured with the component's height
	check_frame(holder, 0, 0, 30, 44);  // at its own size, at the origin
	// Streaming more text keeps the component and sends no sizes again:
	// one crossing for the props, one for the frames.
	ak_fake_reset(f);
	ak_struct *s2[] = {span("a "), embed("b", badge), span(" more")};
	write_p(text_cell, rich(s2, 3));
	ak_ui_flush();
	CHECK(builds == 1 && ak_fake_count(f, AK_OP_CREATE) == 0 && f->applies <= 2);
	// The span gone: the component disposed, its view destroyed.
	ak_id hid = holder->id;
	ak_struct *s3[] = {span("a")};
	write_p(text_cell, rich(s3, 1));
	ak_ui_flush();
	CHECK(cleanups == 1 && v->children.len == 0 && ak_fake_view_of(f, hid) == NULL);
	CHECK_EQ_INT(bytes_of(v, AK_PROP_RICH_TEXT_INLINES).len, 4);  // none
	done(a);
	ak_release(text_cell);
}

static ak_val inline_root(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *r = ak_ui_new(AK_EL_RICH_TEXT);
	ak_struct *s[] = {embed("x", badge)};
	set(r, AK_KEY_RICH_TEXT_TEXT, (ak_val){.p = rich(s, 1)});
	return (ak_val){.p = r};
}

// Unmounting disposes the inline components.
TEST(rich_text_inline_disposal) {
	builds = cleanups = 0;
	ak_uitest *a = mount(inline_root, NULL);
	CHECK_EQ_INT(builds, 1);
	done(a);
	CHECK_EQ_INT(cleanups, 1);
}

// ---- A selectable Text, Selectable ----

static ak_val selectable_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	ak_view *t = text("Copy me");
	set_i(t, AK_KEY_COLOR, (int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_RED));
	set_i(t, AK_KEY_BACKGROUND, (int64_t)ak_semantic_color(AK_SEMANTIC_COLOR_GRAY));
	set_i(t, AK_KEY_TEXT_SELECTABLE, 1);
	add(col, t);
	add(col, text("plain"));
	ak_view *sel = ak_ui_new(AK_EL_SELECTABLE);
	ak_view *r = ak_ui_new(AK_EL_RICH_TEXT);
	ak_struct *s[] = {span("one")};
	set(r, AK_KEY_RICH_TEXT_TEXT, (ak_val){.p = rich(s, 1)});
	add(sel, r);
	add(col, sel);
	return (ak_val){.p = col};
}

// A selectable Text is a Selectable holding a RichText of its text, its
// look on the Selectable and its text keys on the RichText; a plain one
// is the platform's label.
TEST(selectable_text) {
	ak_uitest *a = mount(selectable_app, NULL);
	CHECK_EQ_INT(count_kind(a, AK_KIND_SELECTABLE), 2);
	CHECK_EQ_INT(count_kind(a, AK_KIND_TEXT), 1);
	ak_fake_view *sel = nth(a, AK_KIND_SELECTABLE, 0);
	REQUIRE(sel && sel->children.len == 1);
	ak_fake_view *inner = sel->children.data[0];
	CHECK(inner->kind == AK_KIND_RICH_TEXT);
	CHECK((ak_color)ak_fake_int(sel, AK_PROP_BACKGROUND) == ak_semantic_color(AK_SEMANTIC_COLOR_GRAY));
	CHECK((ak_color)ak_fake_int(inner, AK_PROP_COLOR) == ak_semantic_color(AK_SEMANTIC_COLOR_RED));
	CHECK(!has_prop(sel, AK_PROP_COLOR));
	char got[64];
	ak_rich_run_vec runs = {0};
	paras(inner, AK_PROP_RICH_TEXT_CONTENT, got, sizeof got, &runs);
	ak_vec_free(&runs);
	CHECK_EQ_STR(got, "Copy me");
	CHECK(sel->frame[2] == 390 && sel->frame[3] == 20);  // as tall as its text, measured
	check_frame(inner, 0, 0, 390, 20);
	done(a);
}

// ---- A rich TextField (richfield.go), paste, formats ----

static void *rich_cell, *plain_cell;

static ak_val set_rich(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	char s[128] = "";
	const ak_arr *spans = a[0].p;
	for (uint32_t i = 0; i < spans->len; i++) {
		const ak_struct *sp = spans->data[i].p;
		size_t k = strlen(s);
		snprintf(s + k, sizeof s - k, "%s[%s%s]", i ? "," : "", ((ak_str *)sp->f[0].p)->data,
		         ((ak_struct *)sp->f[1].p)->f[2].i == AK_FONT_WEIGHT_BOLD ? ":bold" : "");
	}
	logs(s);
	write_p(rich_cell, ak_retain(a[0].p));
	return noval();
}

static ak_val set_plain(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	write_p(plain_cell, ak_retain(a[0].p));
	return noval();
}

static ak_val log_formats(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	const ak_struct *f = a[0].p;
	char s[64];
	snprintf(s, sizeof s, "formats:%d%d%d%d%d:%s", (int)f->f[0].i, (int)f->f[1].i, (int)f->f[2].i, (int)f->f[3].i, (int)f->f[4].i,
	         ((ak_str *)f->f[5].p)->data);
	logs(s);
	return noval();
}

static ak_val log_paste(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	const ak_arr *items = a[0].p;
	char s[128];
	const ak_struct *it = items->data[0].p;
	snprintf(s, sizeof s, "paste:%u:%lld:%s:%s", items->len, (long long)it->f[0].i, ((ak_str *)it->f[2].p)->data, ((ak_str *)it->f[4].p)->data);
	logs(s);
	return noval();
}

static ak_view *field_ref;

static ak_val rich_field_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *tf = ak_ui_new(AK_EL_TEXT_FIELD);
	bind(tf, AK_KEY_TEXT_FIELD_TEXT, read_cell, plain_cell);
	on(tf, AK_EV_TEXT_FIELD_CHANGE, set_plain, NULL);
	bind(tf, AK_KEY_TEXT_FIELD_RICH, read_cell, rich_cell);
	on(tf, AK_EV_TEXT_FIELD_RICH_CHANGE, set_rich, NULL);
	on(tf, AK_EV_TEXT_FIELD_FORMATS, log_formats, NULL);
	ak_arr *types = ak_arr_new(AK_K_REF, NULL, 1);
	ak_arr_push(&types, (ak_val){.p = ak_str_c("image/*")});
	set(tf, AK_KEY_TEXT_FIELD_PASTE_TYPES, (ak_val){.p = types});
	on(tf, AK_EV_TEXT_FIELD_PASTE, log_paste, NULL);
	field_ref = ak_ui_ref(tf);
	return (ak_val){.p = tf};
}

// A rich field sends its spans (and is multiline); the user's edits come
// back as spans, and its text follows their plain text; its formats and
// pastes reach their handlers; its commands go to it.
TEST(rich_field) {
	log_buf[0] = 0;
	ak_struct *s[] = {span("Hi "), strong("there")};
	rich_cell = cell_ref(rich(s, 2));
	plain_cell = cell_ref(ak_str_c(""));
	ak_uitest *a = mount(rich_field_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	ak_fake_view *tf = nth(a, AK_KIND_TEXT_FIELD, 0);
	REQUIRE(tf);
	CHECK(ak_fake_bool(tf, AK_PROP_TEXT_FIELD_MULTILINE) && ak_fake_listens(tf, AK_EVENT_RICH_CHANGED));
	CHECK(ak_fake_listens(tf, AK_EVENT_PASTED) && ak_fake_listens(tf, AK_EVENT_FORMATS));
	char got[64];
	ak_rich_run_vec runs = {0};
	paras(tf, AK_PROP_TEXT_FIELD_RICH, got, sizeof got, &runs);
	CHECK_EQ_STR(got, "Hi /there");
	ak_val plain = ak_vm_cell_get(plain_cell, false);
	CHECK_EQ_STR(((ak_str *)plain.p)->data, "Hi there");  // the text follows
	ak_release(plain.p);
	// The user's edit: its spans come back, and aren't sent again.
	ak_rich_run edit[] = {{.text = AK_STR("Hi ")}, {.style = {.weight = AK_FONT_WEIGHT_BOLD}, .text = AK_STR("you\n")}, {.text = AK_STR("ok")}};
	ak_buf b = {0};
	ak_append_rich(&b, edit, 3);
	ak_fake_reset(f);
	dispatch(a, (ak_event){.id = tf->id, .kind = AK_EVENT_RICH_CHANGED, .s = ak_strn(b.data, b.len)});
	ak_buf_free(&b);
	CHECK_EQ_STR(log_buf, "[Hi ],[you\n:bold],[ok]");
	CHECK_EQ_INT(ak_fake_count(f, AK_OP_SET_PROP), 0);
	// Formats, a paste.
	ak_buf fb = {0};
	ak_buf_u8(&fb, 1 | 4);
	ak_buf_wstr(&fb, AK_STR("https://x.dev"));
	dispatch(a, (ak_event){.id = tf->id, .kind = AK_EVENT_FORMATS, .s = ak_strn(fb.data, fb.len)});
	ak_buf_free(&fb);
	ak_item it = {.kind = AK_SHARE_KIND_IMAGE, .path = AK_STR("/caches/p.png"), .mime = AK_STR("image/png"), .size = 10};
	ak_buf pb = {0};
	ak_append_items(&pb, &it, 1);
	dispatch(a, (ak_event){.id = tf->id, .kind = AK_EVENT_PASTED, .s = ak_strn(pb.data, pb.len)});
	ak_buf_free(&pb);
	CHECK_EQ_STR(log_buf, "[Hi ],[you\n:bold],[ok]|formats:10100:https://x.dev|paste:1:2:/caches/p.png:image/png");
	ak_str_vec types = {0};
	ak_fake_strings(tf, AK_PROP_TEXT_FIELD_PASTE_TYPES, &types);
	CHECK(types.len == 1 && ak_span_eqc(types.data[0], "image/*"));
	ak_vec_free(&types);
	// Commands.
	ak_fake_reset(f);
	ak_ui_format(field_ref, 0, 2);  // toggle bold
	ak_str *url = ak_str_c("https://y.dev"), *t = ak_str_c("😀");
	ak_ui_set_link(field_ref, url);
	ak_ui_insert_text(field_ref, t);
	ak_release(url), ak_release(t);
	ak_ui_flush();
	ak_fake_index_vec cmds = {0};
	ak_fake_commands(f, AK_CMD_FORMAT, &cmds);
	CHECK(cmds.len == 1 && ak_fake_op_at(f, cmds.data[0]).value.s.p[0] == 1 && ak_fake_op_at(f, cmds.data[0]).value.s.p[1] == 2);
	cmds.len = 0;
	ak_fake_commands(f, AK_CMD_SET_LINK, &cmds);
	CHECK(cmds.len == 1 && ak_span_eqc(ak_fake_op_at(f, cmds.data[0]).value.s, "https://y.dev"));
	cmds.len = 0;
	ak_fake_commands(f, AK_CMD_INSERT_TEXT, &cmds);
	CHECK_EQ_INT(cmds.len, 1);
	ak_vec_free(&cmds);
	ak_release(field_ref);
	ak_vec_free(&runs);
	done(a);
	ak_release(rich_cell), ak_release(plain_cell);
}

// ---- The accessory (TestAccessory) ----

static void *acc_shown, *acc_text;

static ak_val count_text(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_val t = ak_vm_cell_get(acc_text, true);
	char s[16];
	snprintf(s, sizeof s, "%u", ((ak_str *)t.p)->len);
	ak_release(t.p);
	return (ak_val){.p = ak_str_c(s)};
}

static ak_val set_acc_text(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	write_p(acc_text, ak_retain(a[0].p));
	return noval();
}

static ak_val acc_field(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_view *tf = ak_ui_new(AK_EL_TEXT_FIELD);
	bind(tf, AK_KEY_TEXT_FIELD_TEXT, read_cell, acc_text);
	on(tf, AK_EV_TEXT_FIELD_CHANGE, set_acc_text, NULL);
	ak_view *row = ak_ui_new(AK_EL_ROW);
	set(row, AK_KEY_PADDING, edges4(8, 8, 8, 8));
	ak_view *t = ak_ui_new(AK_EL_TEXT);
	bind(t, AK_KEY_TEXT_TEXT, count_text, NULL);
	set_f(t, AK_KEY_GROW, 1);
	add(row, t);
	ak_view *b = ak_ui_new(AK_EL_BUTTON);
	set_str(b, AK_KEY_BUTTON_TITLE, "Done");
	add(row, b);
	ak_ui_slot(tf, AK_KEY_TEXT_FIELD_ACCESSORY, row);
	ak_release(row);
	return (ak_val){.p = tf};
}

static ak_val acc_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_closure *cond = fn(read_cell, acc_shown), *then = fn(acc_field, NULL);
	ak_view *s = ak_ui_show(cond, then, NULL);
	ak_release(cond), ak_release(then);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	add(col, s);
	return (ak_val){.p = col};
}

// A field's accessory is a root as wide as the screen and as tall as its
// content, named by the field, and goes with it.
TEST(accessory) {
	acc_shown = ak_vm_cell_new((ak_val){.i = 1}, AK_K_BOOL, NULL).p;
	acc_text = cell_ref(ak_str_c(""));
	ak_uitest *a = mount(acc_app, NULL);
	ak_fake *f = ak_uitest_host(a);
	REQUIRE(count_kind(a, AK_KIND_ACCESSORY) == 1);
	ak_fake_view *acc = nth(a, AK_KIND_ACCESSORY, 0), *field = nth(a, AK_KIND_TEXT_FIELD, 0);
	CHECK_EQ_INT(ak_fake_int(field, AK_PROP_TEXT_FIELD_ACCESSORY), acc->id);
	CHECK_EQ_F(ak_fake_float(acc, AK_PROP_ACCESSORY_WIDTH), 390);
	CHECK_EQ_F(ak_fake_float(acc, AK_PROP_ACCESSORY_HEIGHT), 44 + 16);
	ak_fake_view *done_b = one(a, AK_KIND_BUTTON, "Done");
	CHECK(done_b && done_b->frame[0] + done_b->frame[2] == 382);
	ak_fake_type(f, field, "abc");
	ak_ui_flush();
	CHECK(one(a, AK_KIND_TEXT, "3") != NULL);
	write_i(acc_shown, 0);
	ak_ui_flush();
	CHECK(count_kind(a, AK_KIND_ACCESSORY) == 0 && count_kind(a, AK_KIND_BUTTON) == 0);
	done(a);
	ak_release(acc_shown), ak_release(acc_text);
}

T_MAIN()
