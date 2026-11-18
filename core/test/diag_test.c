// Run-time diagnostics (rfcs/0015; base.h's ak_diag): their format, each
// reported once per site, panics that carry their code, and the graph's
// detector of an effect's write that runs a binding again in its flush
// (effect-in-effect-pass). The VM's and the UI's detectors (wide-fanout,
// load-waterfall, long-hold) are run on the fake host by lang/diag's
// tests, each code's wrong program and right one.
#include "t.h"
#include "base/base.h"
#include "signal/signal.h"

// What the handler got.
static struct {
	int n;
	char code[64];
	char msg[1024];
	char where[4][256];
	int nwhere;
} got;

static void record(void *ctx, const char *code, const char *msg, const char *const *where, int nwhere) {
	(void)ctx;
	got.n++;
	snprintf(got.code, sizeof got.code, "%s", code);
	snprintf(got.msg, sizeof got.msg, "%s", msg);
	got.nwhere = nwhere;
	for (int i = 0; i < nwhere && i < 4; i++) snprintf(got.where[i], sizeof got.where[i], "%s", where[i]);
}

static bool was;

static void diag_on(bool on) {
	was = ak_diag_enabled;
	ak_diag_enabled = on;
	ak_diag_reset();
	memset(&got, 0, sizeof got);
	ak_diag_set_handler(record, NULL);
}

static void diag_restore(void) {
	ak_diag_set_handler(NULL, NULL);
	ak_diag_enabled = was;
}

TEST(diag_format) {
	const char *where[2] = {"Posts's load of posts at app.asuka:12", "Profile's load of user at app.asuka:17"};
	char buf[512];
	ak_diag_format(buf, sizeof buf, "load-waterfall", "Posts's load waited", where, 2);
	CHECK_EQ_STR(buf,
		"warning[load-waterfall]: Posts's load waited\n"
		"  --> Posts's load of posts at app.asuka:12\n"
		"  --> Profile's load of user at app.asuka:17\n"
		"  = see https://asuka.egoist.dev/docs/errors/load-waterfall");
	// Cut short, it still ends.
	char small[16];
	ak_diag_format(small, sizeof small, "load-waterfall", "x", where, 2);
	CHECK(strlen(small) == sizeof small - 1);
}

static const int site_a = 0, site_b = 0;

// Each site's code once per run, whatever the code's string's address;
// a reset starts a run.
TEST(diag_first_once_per_site) {
	diag_on(true);
	char code[] = "long-hold";
	CHECK(ak_diag_first(&site_a, "long-hold"));
	CHECK(!ak_diag_first(&site_a, code));
	CHECK(ak_diag_first(&site_a, "wide-fanout"));
	CHECK(ak_diag_first(&site_b, "long-hold"));
	// Past what the table starts with: it grows, and still knows.
	static int many[500];
	for (int i = 0; i < 500; i++) CHECK(ak_diag_first(&many[i], "wide-fanout"));
	for (int i = 0; i < 500; i++) CHECK(!ak_diag_first(&many[i], "wide-fanout"));
	CHECK(!ak_diag_first(&site_b, "long-hold"));
	ak_diag_reset();
	CHECK(ak_diag_first(&site_a, "long-hold"));
	diag_restore();
}

// A loop of effects panics with its code, in every build.
static struct {
	int n;
	char msg[512];
} panics;
static void on_panic(void *ctx, const char *msg) {
	(void)ctx;
	panics.n++;
	snprintf(panics.msg, sizeof panics.msg, "%s", msg);
}

typedef struct {
	ak_node n;
	int v;
} isig;

static int get(isig *s) {
	ak_signal_read(&s->n);
	return s->v;
}

static void set(isig *s, int v) {
	if (s->v == v) return;
	s->v = v;
	ak_signal_write(&s->n);
}

static isig loop_a;
static const ak_comp_info loop_info = {.label = "loop (diag_test.c)"};
static void loop_effect(void *ctx) {
	(void)ctx;
	ak_signal_note_write("loop_a", 6);
	set(&loop_a, get(&loop_a) + 1);
}
static void loop_build(void *ctx, ak_owner *root) {
	(void)ctx, (void)root;
	ak_effect_new(loop_effect, NULL, AK_EFFECT_USER, &loop_info);
}

TEST(effect_loop_panics_with_its_code) {
	for (int on = 0; on < 2; on++) {
		diag_on(on);
		panics.n = 0;
		ak_signal_set_panic_hook(on_panic, NULL);
		ak_owner *r = ak_root(loop_build, NULL);
		CHECK_EQ_INT(panics.n, 1);
		CHECK(strncmp(panics.msg, "error[effect-loop]: ", 20) == 0);
		CHECK(strstr(panics.msg, "loop (diag_test.c) is still writing loop_a after 100 passes") != NULL);
		ak_signal_set_panic_hook(NULL, NULL);
		ak_owner_dispose(r);
		loop_a.v = 0;
		diag_restore();
	}
}

// effect-in-effect-pass: a user effect writes what a binding (a render
// effect) reads, after the binding ran in the same flush, at its making.
static isig name, shown;
static int binding_runs;
static const void *binding_site(void *ctx) {
	(void)ctx;
	return &binding_runs;
}
static void binding_describe(void *ctx, char *buf, size_t n) {
	(void)ctx;
	snprintf(buf, n, "Label's text binding at app.asuka:6");
}
static void effect_describe(void *ctx, char *buf, size_t n) {
	(void)ctx;
	snprintf(buf, n, "Label's effect at app.asuka:4");
}
static const void *effect_site(void *ctx) {
	(void)ctx;
	return &name;
}
static const ak_comp_info binding_info = {.label = "text", .describe = binding_describe, .site = binding_site};
static const ak_comp_info effect_info = {.label = "effect", .describe = effect_describe, .site = effect_site, .app = true};
// The runtime's own effect (a load's run writing its busy signal): its
// writes aren't the app's.
static const ak_comp_info runtime_info = {.label = "load", .describe = effect_describe, .site = effect_site};

static void binding_run(void *ctx) {
	(void)ctx;
	binding_runs++;
	(void)get(&shown);
}
// The effect copies name into shown (a let would do: name read where shown is).
static void copy_effect(void *ctx) {
	(void)ctx;
	ak_signal_note_write("shown", 5);
	set(&shown, get(&name) * 10);
}
static void pass_build(void *ctx, ak_owner *root) {
	(void)root;
	ak_effect_new(binding_run, NULL, AK_EFFECT_RENDER, &binding_info);
	if (ctx) ak_effect_new(copy_effect, NULL, AK_EFFECT_USER, &effect_info);
}

TEST(effect_in_effect_pass) {
	for (int on = 0; on < 2; on++) {
		diag_on(on);
		name.v = 1, shown.v = 0, binding_runs = 0;
		ak_owner *r = ak_root(pass_build, (void *)1);
		// The binding ran at its making, then again for the effect's write.
		CHECK_EQ_INT(binding_runs, 2);
		CHECK_EQ_INT(shown.v, 10);
		if (on) {
			REQUIRE(got.n == 1);
			CHECK_EQ_STR(got.code, "effect-in-effect-pass");
			CHECK_EQ_STR(got.where[0], "Label's effect at app.asuka:4");
			CHECK_EQ_STR(got.where[1], "Label's text binding at app.asuka:6");
			CHECK(strstr(got.msg, "Label's effect at app.asuka:4 wrote shown, which Label's text binding at app.asuka:6 reads") != NULL);
		} else {
			CHECK_EQ_INT(got.n, 0);  // off: nothing is kept, nothing said
		}
		// A write the effect corrects in its flush again: said once per
		// site, and the steady state allocates nothing either way.
		set(&name, 2);
		got.n = 0;
		uint64_t before = ak_alloc_count;
		for (int i = 3; i < 50; i++) set(&name, i);
		CHECK_EQ_INT(ak_alloc_count - before, 0);
		CHECK_EQ_INT(got.n, 0);
		ak_owner_dispose(r);
		diag_restore();
	}
}

static void runtime_build(void *ctx, ak_owner *root) {
	(void)ctx, (void)root;
	ak_effect_new(binding_run, NULL, AK_EFFECT_RENDER, &binding_info);
	ak_effect_new(copy_effect, NULL, AK_EFFECT_USER, &runtime_info);
}

TEST(effect_in_effect_pass_not_the_runtimes) {
	diag_on(true);
	name.v = 1, shown.v = 0, binding_runs = 0;
	ak_owner *r = ak_root(runtime_build, NULL);
	CHECK_EQ_INT(binding_runs, 2);
	CHECK_EQ_INT(got.n, 0);
	ak_owner_dispose(r);
	diag_restore();
}

// A binding alone, and an effect that writes what no binding read: no
// report.
TEST(effect_in_effect_pass_not) {
	diag_on(true);
	name.v = 1, shown.v = 0, binding_runs = 0;
	ak_owner *r = ak_root(pass_build, NULL);
	set(&shown, 5);
	CHECK_EQ_INT(binding_runs, 2);  // the handler's write: one run in its flush
	CHECK_EQ_INT(got.n, 0);
	ak_owner_dispose(r);
	diag_restore();
}

T_MAIN()
