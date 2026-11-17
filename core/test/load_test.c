// Loaders (control.c's): let x = load f(…). A load is pending only
// before its first answer; a run after that keeps its case until it
// answers (RFC 0014), busy says one is in flight, and an answer equal to
// the last changes nothing. The tests count what each signal notifies:
// a binding that reads only the state, one that reads only loading.
#include "uih.h"

#include "rt/sched.h"

static void *id;            // the cell the thunk reads
static ak_val loader;       // the app's loader (the test's reference)
static ak_future *runs[16]; // each run's future (the test's reference)
static int nruns;
static int state_reads, busy_reads, builds;

// The thunk: a run is a future the test settles.
static ak_val thunk(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	(void)ak_vm_cell_get(id, true);
	ak_future *f = ak_future_new(AK_K_INT);
	runs[nruns++] = ak_retain(f);
	return (ak_val){.p = f};
}

static ak_val state_of(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	state_reads++;
	return (ak_val){.i = ak_ui_load_state(loader)};
}

static ak_val busy_text(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	busy_reads++;
	return (ak_val){.p = ak_str_c(ak_ui_load_loading(loader) ? "busy" : "idle")};
}

static ak_val value_text(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_val v = ak_ui_load_value(loader);
	char s[32];
	snprintf(s, sizeof s, "value %lld", v.p ? (long long)((ak_box *)v.p)->v.i : -1LL);
	if (v.p) ak_release(v.p);
	return (ak_val){.p = ak_str_c(s)};
}

// A case's content, built once per case: builds counts them, so a
// reload that kept the content builds nothing.
static ak_val case_view(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(n);
	builds++;
	static const char *const names[] = {"pending", "ready", "failed"};
	ak_view *t = ak_ui_new(AK_EL_TEXT);
	set_str(t, AK_KEY_TEXT_TEXT, names[a[0].i]);
	return (ak_val){.p = t};
}

static ak_val load_app(ak_closure *c, ak_val *a, uint32_t n) {
	AK_UNUSED(c), AK_UNUSED(a), AK_UNUSED(n);
	ak_closure *t = fn(thunk, NULL);
	loader = ak_ui_load(t);
	ak_release(t);
	ak_view *col = ak_ui_new(AK_EL_COLUMN);
	ak_closure *sel = fn(state_of, NULL), *build = fn(case_view, NULL);
	add(col, ak_ui_switch(sel, build));
	ak_release(sel), ak_release(build);
	ak_view *busy = ak_ui_new(AK_EL_TEXT);
	bind(busy, AK_KEY_TEXT_TEXT, busy_text, NULL);
	add(col, busy);
	ak_view *val = ak_ui_new(AK_EL_TEXT);
	bind(val, AK_KEY_TEXT_TEXT, value_text, NULL);
	add(col, val);
	return (ak_val){.p = col};
}

static ak_uitest *start(void) {
	nruns = state_reads = busy_reads = builds = 0;
	id = cell_int(1);
	ak_uitest *a = mount(load_app, NULL);
	ak_uitest_settle(a);
	return a;
}

static void settle(ak_uitest *a) {
	ak_sched_run();
	ak_uitest_settle(a);
}

static void answer(ak_uitest *a, int i, int64_t v) {
	ak_future_resolve(runs[i], (ak_val){.i = v});
	settle(a);
}

static void *error_of(const char *msg) {
	ak_struct *e = ak_struct_new(&ak_shape_error);
	e->f[0].p = ak_str_c(msg);
	return e;
}

static void fail(ak_uitest *a, int i, const char *msg) {
	ak_future_reject(runs[i], error_of(msg));
	settle(a);
}

static void finish(ak_uitest *a) {
	done(a);
	ak_release(loader.p);
	for (int i = 0; i < nruns; i++) ak_release(runs[i]);
	ak_release(id);
}

TEST(pending_only_before_the_first_answer) {
	ak_uitest *a = start();
	REQUIRE(nruns == 1);
	check_text(root(a), "pending|busy|value -1");
	answer(a, 0, 10);
	check_text(root(a), "ready|idle|value 10");
	CHECK_EQ_INT(builds, 2);
	finish(a);
}

// A run after the first keeps the content (built once) and writes busy
// alone; its answer writes the state once.
TEST(a_run_keeps_its_case) {
	ak_uitest *a = start();
	answer(a, 0, 10);
	int states = state_reads, busies = busy_reads;
	reset(a);
	write_i(id, 2);
	settle(a);
	REQUIRE(nruns == 2);
	check_text(root(a), "ready|busy|value 10");
	CHECK_EQ_INT(state_reads, states);
	CHECK_EQ_INT(busy_reads, busies + 1);
	CHECK_EQ_INT(builds, 2);
	CHECK_EQ_INT(ak_fake_count(ak_uitest_host(a), AK_OP_DESTROY), 0);
	answer(a, 1, 20);
	check_text(root(a), "ready|idle|value 20");
	CHECK_EQ_INT(state_reads, states + 1);
	CHECK_EQ_INT(busy_reads, busies + 2);
	CHECK_EQ_INT(builds, 2);
	CHECK_EQ_INT(ak_fake_count(ak_uitest_host(a), AK_OP_DESTROY), 0);
	finish(a);
}

// An answer equal to the last notifies nothing but busy.
TEST(an_equal_answer_notifies_nothing) {
	ak_uitest *a = start();
	answer(a, 0, 10);
	int states = state_reads, busies = busy_reads;
	ak_ui_load_reload(loader);
	settle(a);
	REQUIRE(nruns == 2);
	check_text(root(a), "ready|busy|value 10");
	answer(a, 1, 10);
	check_text(root(a), "ready|idle|value 10");
	CHECK_EQ_INT(state_reads, states);
	CHECK_EQ_INT(busy_reads, busies + 2);
	finish(a);
}

// A run a newer one replaced changes nothing when it answers, and the
// newer one's answer is the one shown; cancelling it changes nothing.
TEST(a_replaced_run_changes_nothing) {
	ak_uitest *a = start();
	answer(a, 0, 10);
	write_i(id, 2);
	settle(a);
	write_i(id, 3);
	settle(a);
	REQUIRE(nruns == 3);
	int states = state_reads, busies = busy_reads;
	answer(a, 1, 20);
	ak_future_cancel(runs[1]);
	settle(a);
	check_text(root(a), "ready|busy|value 10");
	CHECK_EQ_INT(state_reads, states);
	CHECK_EQ_INT(busy_reads, busies);
	answer(a, 2, 30);
	check_text(root(a), "ready|idle|value 30");
	CHECK_EQ_INT(builds, 2);
	finish(a);
}

// A run that fails after a value is failed, the value kept; failing the
// same way again notifies nothing; a later answer is ready again.
TEST(a_failure_keeps_the_value) {
	ak_uitest *a = start();
	answer(a, 0, 10);
	write_i(id, 2);
	settle(a);
	fail(a, 1, "offline");
	check_text(root(a), "failed|idle|value 10");
	CHECK_EQ_INT(builds, 3);
	ak_val e = ak_ui_load_error(loader);
	REQUIRE(e.p);
	CHECK_EQ_STR(ak_error_message(e.p)->data, "offline");
	ak_release(e.p);
	int states = state_reads;
	ak_ui_load_reload(loader);
	settle(a);
	check_text(root(a), "failed|busy|value 10");
	fail(a, 2, "offline");
	check_text(root(a), "failed|idle|value 10");
	CHECK_EQ_INT(state_reads, states);
	ak_ui_load_reload(loader);
	settle(a);
	fail(a, 3, "timed out");
	CHECK_EQ_INT(state_reads, states + 1);
	ak_ui_load_reload(loader);
	settle(a);
	answer(a, 4, 10);
	check_text(root(a), "ready|idle|value 10");
	CHECK_EQ_INT(state_reads, states + 2);
	e = ak_ui_load_error(loader);
	CHECK(e.p == NULL);
	finish(a);
}

// Disposing the component while a run is in flight: no run is in flight
// any more, its answer changes nothing, and nothing outlives the app.
TEST(disposal_ends_the_run) {
	ak_uitest *a = start();
	answer(a, 0, 10);
	write_i(id, 2);
	settle(a);
	CHECK(ak_ui_load_loading(loader));
	ak_buf err = {0};
	if (!ak_uitest_unmount(a, &err)) T_FAIL("%s", cstr(&err));
	ak_buf_free(&err);
	ak_release(a);
	CHECK(!ak_ui_load_loading(loader));
	ak_future_resolve(runs[1], (ak_val){.i = 20});
	ak_sched_run();
	CHECK_EQ_INT(ak_ui_load_state(loader), 1);
	ak_val v = ak_ui_load_value(loader);
	CHECK_EQ_INT(((ak_box *)v.p)->v.i, 10);
	ak_release(v.p);
	write_i(id, 3);
	ak_ui_flush();
	CHECK_EQ_INT(nruns, 2);  // it never runs again
	ak_release(loader.p);
	for (int i = 0; i < nruns; i++) ak_release(runs[i]);
	ak_release(id);
	ak_ui_stats s;
	ak_ui_get_stats(&s);
	CHECK_EQ_INT(s.views, 0);
}

// A run that keeps its case allocates no more than its ticket.
TEST(a_run_allocates_its_ticket) {
	ak_uitest *a = start();
	answer(a, 0, 10);
	ak_ui_load_reload(loader);
	settle(a);
	answer(a, 1, 10);
	uint64_t before = ak_alloc_count;
	ak_ui_load_reload(loader);
	settle(a);
	answer(a, 2, 10);
	// The thunk's future, its reference in runs, and the run's ticket;
	// the waiter the future keeps for it.
	uint64_t per_run = ak_alloc_count - before;
	ak_ui_load_reload(loader);
	settle(a);
	answer(a, 3, 10);
	CHECK_EQ_INT(ak_alloc_count - before, 2 * per_run);
	finish(a);
}

T_MAIN();
