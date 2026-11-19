// Per-path tracking (rfcs/0013, core/src/vm/path.c): readers of paths
// below a cell run only when what they read changed. Each reader is a
// render effect that reads a path through the cell and counts its runs.
#include "t.h"
#include "vm/path.h"
#include "vm/vm_int.h"

// struct User { name: string; avatar: string }
static const uint8_t user_kinds[] = {AK_K_REF, AK_K_REF};
static const ak_shape user_shape = {.name = "User", .nfields = 2, .kinds = user_kinds};
// struct Todo { id: int; title: string; done: bool }
static const uint8_t todo_kinds[] = {AK_K_INT, AK_K_REF, AK_K_BOOL};
static const ak_shape todo_shape = {.name = "Todo", .nfields = 3, .kinds = todo_kinds};

static ak_str *str(const char *s) { return ak_str_c(s); }

static ak_struct *user(const char *name, const char *avatar) {
	ak_struct *u = ak_struct_new(&user_shape);
	u->f[0].p = str(name);
	u->f[1].p = str(avatar);
	return u;
}

static ak_struct *todo(int64_t id, const char *title, bool done) {
	ak_struct *t = ak_struct_new(&todo_shape);
	t->f[0].i = id;
	t->f[1].p = str(title);
	t->f[2].i = done;
	return t;
}

static ak_arr *todos(int n) {
	ak_arr *a = ak_arr_new(AK_K_REF, &todo_shape, (uint32_t)n);
	char title[16];
	for (int i = 0; i < n; i++) {
		snprintf(title, sizeof title, "t%d", i);
		ak_arr_vals(a)[i].p = todo(i, title, false);
	}
	a->len = (uint32_t)n;
	return a;
}

// ---- Readers ----

typedef struct {
	void *cell;
	const char *path;  // as a path constant's bytes
	uint32_t n;
	ak_val args[2];
	int runs;
	bool threw;
	ak_val last;
	bool scalar;
} reader;

static void reader_run(void *ctx) {
	reader *r = ctx;
	r->runs++;
	ak_val v = ak_vm_cell_path(r->cell, (const uint8_t *)r->path, r->n, r->args, false);
	r->threw = ak_error_pending();
	if (r->threw) {
		ak_release(ak_error_take());
		return;
	}
	if (!r->scalar) ak_release(v.p);
	r->last = v;
}

static void watch(reader *r) { ak_effect_new(reader_run, r, AK_EFFECT_RENDER, NULL); }

// Paths: f fields (2 bytes each), i an index argument, k a key argument
// (a kind after it), n the length.
#define F0 "f\x00\x00"
#define F1 "f\x01\x00"
#define F2 "f\x02\x00"
#define P(s) s, (uint32_t)(sizeof(s) - 1)

static void put(void *cell, ak_val v, const char *p, uint32_t n, const ak_val *args) { ak_vm_cell_putp(cell, v, (const uint8_t *)p, n, args); }

static reader *rd(reader *r, void *cell, const char *p, uint32_t n, int64_t a0, bool scalar) {
	*r = (reader){.cell = cell, .path = p, .n = n, .args = {{.i = a0}}, .scalar = scalar};
	return r;
}

// field writes a struct's field in place at the place p (its value
// taken from the cell), the old value compared.
static void set_field(void *cell, const char *p, uint32_t n, int64_t idx, uint16_t f, ak_val v, ak_kind k) {
	ak_val now = ak_vm_cell_take(cell);
	ak_struct *s;
	bool item = p[0] == 'i';
	if (item) {
		// todos[idx].f: the element taken out of the array.
		ak_arr *a = ak_arr_mut((ak_arr **)&now.p);
		s = ak_struct_mut((ak_struct **)&ak_arr_vals(a)[idx].p);
	} else {
		s = ak_struct_mut((ak_struct **)&now.p);
	}
	ak_val old = s->f[f];
	ak_retain_val(old, k);
	ak_release_val(s->f[f], k);
	s->f[f] = v;
	char buf[16];
	memcpy(buf, p, n);
	buf[n] = 'o';
	ak_val args[2] = {{.i = idx}, old};
	put(cell, now, buf, n + 1, item ? args : args + 1);
	ak_release_val(old, k);
}

static void cell_free(void *cell) { ak_release(cell); }

// ---- Tests ----

static reader r1, r2, r3, r4, r5;

static void user_readers(void *ctx, ak_owner *root) {
	AK_UNUSED(root);
	void *c = ctx;
	watch(rd(&r1, c, P(F0), 0, false));  // u.name
	watch(rd(&r2, c, P(F1), 0, false));  // u.avatar
	watch(rd(&r3, c, "", 0, 0, false));            // u, whole
}

TEST(a_field_write_runs_its_readers_only) {
	void *c = ak_vm_cell_new((ak_val){.p = user("a", "x")}, AK_K_REF, NULL).p;
	ak_owner *o = ak_root(user_readers, c);
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 1);
	CHECK_EQ_INT(r3.runs, 1);
	CHECK_EQ_INT(ak_vm_cell_nodes(c), 2);
	set_field(c, P(F1), 0, 1, (ak_val){.p = str("y")}, AK_K_REF);
	CHECK_EQ_INT(r1.runs, 1);  // name: not read again
	CHECK_EQ_INT(r2.runs, 2);
	CHECK_EQ_INT(r3.runs, 2);  // whole: its value changed
	// An equal write notifies no one.
	set_field(c, P(F1), 0, 1, (ak_val){.p = str("y")}, AK_K_REF);
	CHECK_EQ_INT(r2.runs, 2);
	CHECK_EQ_INT(r3.runs, 2);
	ak_owner_dispose(o);
	CHECK_EQ_INT(ak_vm_cell_nodes(c), 0);  // pruned as their readers went
	cell_free(c);
}

static void todo_readers(void *ctx, ak_owner *root) {
	AK_UNUSED(root);
	void *c = ctx;
	watch(rd(&r1, c, P("i" F2), 0, true));  // todos[0].done
	watch(rd(&r2, c, P("i" F2), 1, true));  // todos[1].done
	watch(rd(&r3, c, P("i" F1), 1, false)); // todos[1].title
	watch(rd(&r4, c, P("n"), 0, true));               // todos.count()
	watch(rd(&r5, c, "", 0, 0, false));               // todos, whole
}

TEST(an_element_write_runs_that_element_only) {
	void *c = ak_vm_cell_new((ak_val){.p = todos(3)}, AK_K_REF, NULL).p;
	ak_owner *o = ak_root(todo_readers, c);
	set_field(c, P("i" F2), 1, 2, (ak_val){.i = 1}, AK_K_BOOL);
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 2);
	CHECK_EQ_INT(r2.last.i, 1);
	CHECK_EQ_INT(r3.runs, 1);  // the same element's title
	CHECK_EQ_INT(r4.runs, 1);  // the length
	CHECK_EQ_INT(r5.runs, 2);
	set_field(c, P("i" F2), 1, 2, (ak_val){.i = 1}, AK_K_BOOL);  // equal
	CHECK_EQ_INT(r2.runs, 2);
	CHECK_EQ_INT(r5.runs, 2);
	ak_owner_dispose(o);
	CHECK_EQ_INT(ak_vm_cell_nodes(c), 0);
	cell_free(c);
}

TEST(a_whole_write_compares_path_by_path) {
	ak_arr *a = todos(3);
	void *c = ak_vm_cell_new((ak_val){.p = ak_retain(a)}, AK_K_REF, NULL).p;
	ak_owner *o = ak_root(todo_readers, c);
	// A refetched list, equal: no one, the whole's readers neither.
	ak_arr *b = ak_arr_copy(a);
	ak_release(ak_arr_vals(b)[1].p);
	ak_arr_vals(b)[1].p = todo(1, "t1", false);
	ak_vm_cell_set(c, (ak_val){.p = b});
	CHECK_EQ_INT(r1.runs + r2.runs + r3.runs + r4.runs + r5.runs, 5);
	// Item 1's title changes; items 0 and 2 are shared.
	ak_arr *d = ak_arr_copy(a);
	ak_release(ak_arr_vals(d)[1].p);
	ak_arr_vals(d)[1].p = todo(1, "new", false);
	ak_vm_cell_set(c, (ak_val){.p = d});
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 1);
	CHECK_EQ_INT(r3.runs, 2);
	CHECK_EQ_INT(r4.runs, 1);
	CHECK_EQ_INT(r5.runs, 2);
	// A shorter list: the length, and what's past it.
	ak_vm_cell_set(c, (ak_val){.p = todos(1)});
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 2);
	CHECK(r2.threw);  // todos[1] is out of range now
	CHECK_EQ_INT(r4.runs, 2);
	ak_owner_dispose(o);
	ak_release(a);
	cell_free(c);
}

// A builtin's change in place, as CTAKE, the builtin, CPUTP do.
static void change(void *cell, void (*fn)(ak_val *v, void *ctx), void *ctx) {
	ak_val v = ak_vm_cell_take(cell);
	fn(&v, ctx);
	put(cell, v, P("r"), NULL);
}

static void do_push(ak_val *v, void *ctx) {
	AK_UNUSED(ctx);
	ak_change_range(((ak_arr *)v->p)->len, -1, true);
	ak_arr_push((ak_arr **)&v->p, (ak_val){.p = todo(9, "pushed", false)});
}

static void do_remove1(ak_val *v, void *ctx) {
	AK_UNUSED(ctx);
	ak_change_range(1, -1, true);
	ak_release(ak_arr_remove_at((ak_arr **)&v->p, 1).p);
}

static void do_swap_last(ak_val *v, void *ctx) {
	AK_UNUSED(ctx);
	ak_arr *a = ak_arr_mut((ak_arr **)&v->p);
	ak_val t = ak_arr_vals(a)[1];
	ak_arr_vals(a)[1] = ak_arr_vals(a)[2];
	ak_arr_vals(a)[2] = t;
	ak_change_range(1, 3, false);  // as a sort that moved 1 and 2
}

static void do_nothing_sorted(ak_val *v, void *ctx) {
	AK_UNUSED(ctx);
	ak_change_range(0, ((ak_arr *)v->p)->len, false);
}

static void do_nothing(ak_val *v, void *ctx) {
	AK_UNUSED(v);
	AK_UNUSED(ctx);
	ak_change_none();
}

static reader r6;

static void range_readers(void *ctx, ak_owner *root) {
	todo_readers(ctx, root);
	watch(rd(&r6, ctx, P("i"), 3, false));  // todos[3]: out of range for now
}

TEST(each_builtin_change_reaches_its_range) {
	void *c = ak_vm_cell_new((ak_val){.p = todos(3)}, AK_K_REF, NULL).p;
	ak_owner *o = ak_root(range_readers, c);
	CHECK(r6.threw);
	// push: the length, and the index past the end.
	change(c, do_push, NULL);
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 1);
	CHECK_EQ_INT(r4.runs, 2);
	CHECK_EQ_INT(r6.runs, 2);
	CHECK(!r6.threw);  // it exists now
	// A change of 1 and 2, the length kept.
	change(c, do_swap_last, NULL);
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 2);
	CHECK_EQ_INT(r3.runs, 2);
	CHECK_EQ_INT(r4.runs, 2);
	CHECK_EQ_INT(r6.runs, 2);
	// removeAt(1): from 1 on, and the length.
	change(c, do_remove1, NULL);
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 3);
	CHECK_EQ_INT(r4.runs, 3);
	CHECK_EQ_INT(r6.runs, 3);
	CHECK(r6.threw);
	// A builtin that changed nothing: no one, not even the root.
	int whole = r5.runs;
	change(c, do_nothing, NULL);
	CHECK_EQ_INT(r5.runs, whole);
	ak_owner_dispose(o);
	CHECK_EQ_INT(ak_vm_cell_nodes(c), 0);
	cell_free(c);
}

// Maps and sets: m[k] and contains(x) follow their key.
static void key_readers(void *ctx, ak_owner *root) {
	AK_UNUSED(root);
	void **cs = ctx;
	rd(&r1, cs[0], P("k\x04"), 0, false);  // m["a"]
	r1.args[0].p = str("a");
	rd(&r2, cs[0], P("k\x04"), 0, false);  // m["z"]: missing for now
	r2.args[0].p = str("z");
	rd(&r3, cs[1], P("m\x04"), 0, true);   // s.contains("a")
	r3.args[0].p = str("a");
	rd(&r4, cs[1], P("m\x04"), 0, true);   // s.contains("b")
	r4.args[0].p = str("b");
	rd(&r5, cs[1], P("n"), 0, true);       // s.count()
	watch(&r1);
	watch(&r2);
	watch(&r3);
	watch(&r4);
	watch(&r5);
}

static void map_set(void *cell, const char *key, ak_struct *v) {
	ak_val now = ak_vm_cell_take(cell);
	ak_val k = {.p = str(key)}, old = {0};
	bool had = ak_map_get(now.p, k, &old);
	ak_retain_val(old, AK_K_REF);
	ak_map_set((ak_map **)&now.p, (ak_val){.p = ak_retain(k.p)}, (ak_val){.p = v});
	ak_val args[3] = {k, old, {.i = had}};
	put(cell, now, P("k\x04o"), args);
	ak_release(old.p);
	ak_release(k.p);
}

static void set_insert(void *cell, const char *x) {
	ak_val now = ak_vm_cell_take(cell);
	ak_val k = {.p = str(x)};
	ak_change_key(k, AK_K_REF, true);
	if (!ak_set_insert((ak_map **)&now.p, k)) ak_change_none();
	put(cell, now, P("r"), NULL);
}

TEST(keys_and_members_follow_their_key) {
	ak_map *m = ak_map_new(AK_K_REF, true, AK_K_REF, &todo_shape);
	ak_map_set(&m, (ak_val){.p = str("a")}, (ak_val){.p = todo(1, "a", false)});
	void *cs[2] = {ak_vm_cell_new((ak_val){.p = m}, AK_K_REF, NULL).p, ak_vm_cell_new((ak_val){.p = ak_set_new(AK_K_REF, true)}, AK_K_REF, NULL).p};
	ak_owner *o = ak_root(key_readers, cs);
	CHECK(r1.last.p != NULL);
	CHECK(r2.last.p == NULL);
	// A missing key that comes to be.
	map_set(cs[0], "z", todo(2, "z", false));
	CHECK_EQ_INT(r1.runs, 1);
	CHECK_EQ_INT(r2.runs, 2);
	CHECK(r2.last.p != NULL);
	// An equal value: no one.
	map_set(cs[0], "z", todo(2, "z", false));
	CHECK_EQ_INT(r2.runs, 2);
	// A member inserted: its readers and the count; another's not.
	set_insert(cs[1], "a");
	CHECK_EQ_INT(r3.runs, 2);
	CHECK_EQ_INT(r3.last.i, 1);
	CHECK_EQ_INT(r4.runs, 1);
	CHECK_EQ_INT(r5.runs, 2);
	set_insert(cs[1], "a");  // already there
	CHECK_EQ_INT(r3.runs, 2);
	CHECK_EQ_INT(r5.runs, 2);
	ak_owner_dispose(o);
	reader *keyed[] = {&r1, &r2, &r3, &r4};
	for (int i = 0; i < 4; i++) ak_release(keyed[i]->args[0].p);
	CHECK_EQ_INT(ak_vm_cell_nodes(cs[0]) + ak_vm_cell_nodes(cs[1]), 0);
	cell_free(cs[0]);
	cell_free(cs[1]);
}

// A for keyed by a field path follows one signal for all items: the
// length, the order, the keys; not the rest of an item.
static int key_runs;
static void keys_run(void *ctx) {
	key_runs++;
	static const uint16_t id[1] = {0};
	ak_vm_cell_keys(ctx, NULL, 0, NULL, id, 1);
}
static void keys_build(void *ctx, ak_owner *root) {
	AK_UNUSED(root);
	ak_effect_new(keys_run, ctx, AK_EFFECT_RENDER, NULL);
}

TEST(a_for_follows_its_keys) {
	void *c = ak_vm_cell_new((ak_val){.p = todos(3)}, AK_K_REF, NULL).p;
	key_runs = 0;
	ak_owner *o = ak_root(keys_build, c);
	CHECK_EQ_INT(key_runs, 1);
	set_field(c, P("i" F2), 1, 2, (ak_val){.i = 1}, AK_K_BOOL);  // done
	CHECK_EQ_INT(key_runs, 1);
	set_field(c, P("i" F0), 1, 0, (ak_val){.i = 1}, AK_K_INT);  // id: the same
	CHECK_EQ_INT(key_runs, 1);
	set_field(c, P("i" F0), 1, 0, (ak_val){.i = 7}, AK_K_INT);  // id: another
	CHECK_EQ_INT(key_runs, 2);
	change(c, do_push, NULL);
	CHECK_EQ_INT(key_runs, 3);
	// A change it can't compare (a mut fn's on an item: anything below
	// it) tells the for only when the keys differ from those it saw.
	ak_val now = ak_vm_cell_take(c);
	ak_struct *s = ak_struct_mut((ak_struct **)&ak_arr_vals(ak_arr_mut((ak_arr **)&now.p))[2].p);
	s->f[2].i = !s->f[2].i;
	ak_val at = {.i = 2};
	put(c, now, P("ia"), &at);
	CHECK_EQ_INT(key_runs, 3);
	// A builtin's range whose keys stay (a sort that moved nothing).
	change(c, do_nothing_sorted, NULL);
	CHECK_EQ_INT(key_runs, 3);
	change(c, do_swap_last, NULL);
	CHECK_EQ_INT(key_runs, 4);
	ak_owner_dispose(o);
	CHECK_EQ_INT(ak_vm_cell_nodes(c), 0);
	cell_free(c);
}

// A loop's reads through its element follow a column: that field of
// every element, one node for all of them.
static int col_runs;
static void col_run(void *ctx) {
	col_runs++;
	ak_val n = ak_vm_cell_path(ctx, NULL, 0, NULL, true);
	for (int64_t i = 0; i < n.i; i++) {
		ak_val at = {.i = i};
		ak_vm_cell_path(ctx, (const uint8_t *)"I" F2, 4, &at, false);  // todos[i].done, each
	}
}
static void col_build(void *ctx, ak_owner *root) {
	AK_UNUSED(root);
	ak_effect_new(col_run, ctx, AK_EFFECT_RENDER, NULL);
}

TEST(a_loop_follows_a_column) {
	void *c = ak_vm_cell_new((ak_val){.p = todos(100)}, AK_K_REF, NULL).p;
	col_runs = 0;
	ak_owner *o = ak_root(col_build, c);
	CHECK_EQ_INT(col_runs, 1);
	CHECK_EQ_INT(ak_vm_cell_nodes(c), 2);  // the length and the column: not 100 nodes
	set_field(c, P("i" F1), 7, 1, (ak_val){.p = str("x")}, AK_K_REF);  // a title
	CHECK_EQ_INT(col_runs, 1);
	set_field(c, P("i" F2), 7, 2, (ak_val){.i = 1}, AK_K_BOOL);  // a done
	CHECK_EQ_INT(col_runs, 2);
	set_field(c, P("i" F2), 7, 2, (ak_val){.i = 1}, AK_K_BOOL);  // equal
	CHECK_EQ_INT(col_runs, 2);
	change(c, do_push, NULL);
	CHECK_EQ_INT(col_runs, 3);
	// A whole write: compared, the column's values.
	ak_val cur = ak_vm_cell_get(c, false);
	ak_arr *a = ak_arr_copy(cur.p);
	ak_release(cur.p);
	ak_struct *t = ak_struct_mut((ak_struct **)&ak_arr_vals(a)[3].p);
	ak_release(t->f[1].p);
	t->f[1].p = str("renamed");
	ak_vm_cell_set(c, (ak_val){.p = a});
	CHECK_EQ_INT(col_runs, 3);
	uint64_t before = ak_alloc_count;
	set_field(c, P("i" F2), 7, 2, (ak_val){.i = 0}, AK_K_BOOL);
	CHECK_EQ_INT(col_runs, 4);
	CHECK_EQ_INT(ak_alloc_count - before, 0);
	ak_owner_dispose(o);
	CHECK_EQ_INT(ak_vm_cell_nodes(c), 0);
	cell_free(c);
}

// A reader that runs again reading the same paths allocates nothing,
// and neither does a write that notifies it.
TEST(steady_state_allocates_nothing) {
	void *c = ak_vm_cell_new((ak_val){.p = todos(3)}, AK_K_REF, NULL).p;
	ak_owner *o = ak_root(todo_readers, c);
	set_field(c, P("i" F2), 1, 2, (ak_val){.i = 1}, AK_K_BOOL);  // warm up
	uint64_t before = ak_alloc_count;
	for (int i = 0; i < 100; i++) {
		ak_val now = ak_vm_cell_take(c);
		ak_struct *s = ak_arr_vals(((ak_arr *)now.p))[1].p;
		ak_val old = s->f[2];
		s->f[2].i = !old.i;
		ak_val args[2] = {{.i = 1}, old};
		put(c, now, P("i" F2 "o"), args);
	}
	CHECK_EQ_INT(ak_alloc_count - before, 0);
	CHECK_EQ_INT(r2.runs, 102);
	CHECK_EQ_INT(r1.runs, 1);
	ak_owner_dispose(o);
	cell_free(c);
}

// The cell going first lets go of its readers: their owner's disposal
// after is safe.
TEST(a_cell_dropped_before_its_readers) {
	void *c = ak_vm_cell_new((ak_val){.p = todos(3)}, AK_K_REF, NULL).p;
	ak_owner *o = ak_root(todo_readers, c);
	CHECK(ak_vm_cell_nodes(c) > 0);
	cell_free(c);
	ak_owner_dispose(o);
}

T_MAIN();
