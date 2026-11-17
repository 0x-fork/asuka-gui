// pantry.c: the pantry's C, for Windows and Linux. It implements every
// method so platform/desktop's test (a test host running an Asuka app on
// the core) crosses every kind of value both ways; it says what it got on
// stdout.
#include "pantry.asuka.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say_item(const char *what, const pantry_Item *it) {
	printf("pantry: %s %s weight=%g level=%lld tags=", what, it->name, it->weight, (long long)it->level);
	for (size_t i = 0; i < it->tags.len; i++) printf("%s%s", i ? "," : "", it->tags.data[i]);
	printf(" notes=");
	for (size_t i = 0; i < it->notes.len; i++) printf("%s%s:%s", i ? "," : "", it->notes.keys[i], it->notes.values[i]);
	printf(" next=%s data=%.*s took=%lldms count=%lld fresh=%d\n", it->next ? it->next->name : "none", (int)it->data.len,
	       (const char *)it->data.data, (long long)(it->took / 1000000), (long long)it->count, it->fresh);
	fflush(stdout);
}

static pantry_Item item(const char *name, int64_t count) {
	pantry_Item it = {0};
	it.name = asuka_strdup(name);
	it.weight = 2.5;
	it.level = pantry_Level_high;
	it.tags.len = 2;
	it.tags.data = malloc(2 * sizeof(char *));
	it.tags.data[0] = asuka_strdup("dry");
	it.tags.data[1] = asuka_strdup("cool");
	it.notes.len = 1;
	it.notes.keys = malloc(sizeof(char *));
	it.notes.values = malloc(sizeof(char *));
	it.notes.keys[0] = asuka_strdup("shelf");
	it.notes.values[0] = asuka_strdup("top");
	it.data = asuka_bytes_copy("raw", 3);
	it.took = 1500 * 1000000LL;
	it.count = count;
	it.fresh = true;
	return it;
}

// ---- Pantry ----

static char *p_name(pantry_Pantry *self, asuka_error *err) { return asuka_strdup("Larder"); }

static pantry_list_Item p_items(pantry_Pantry *self, const char *filter, asuka_error *err) {
	pantry_list_Item l = {calloc(2, sizeof(pantry_Item)), 2};
	char *a = asuka_sprintf("%s-1", filter);
	l.data[0] = item(a, 1);
	free(a);
	l.data[1] = item("jam", 2);
	l.data[1].next = malloc(sizeof(pantry_Item));
	*l.data[1].next = item("lid", 3);
	return l;
}

static void p_add(pantry_Pantry *self, const pantry_Item *it, asuka_error *err) {
	say_item("add", it);
	if (it->count < 0) asuka_error_declared(err, pantry_PantryError_empty);
}

typedef struct {
	char *recipe;
	pantry_fn_float progress;
	asuka_call *call;
} cooking;

// cook runs on a thread of its own: its progress is posted to the main
// thread, and it answers from here.
static void cook_run(void *p) {
	cooking *c = p;
	pantry_fn_float_call(c->progress, 0.5);
	pantry_fn_float_call(c->progress, 1.0);
	if (strcmp(c->recipe, "burnt") == 0) {
		asuka_error e = {0};
		asuka_error_failed(&e, "oven", 7, "the %s burnt", c->recipe);
		asuka_call_fail(c->call, &e);
	} else {
		char *n = asuka_sprintf("%s cooked", c->recipe);
		pantry_Item it = item(n, 9);
		free(n);
		pantry_Pantry_cook_done(c->call, it);
	}
	free(c->recipe);
	free(c);
}

// A cook that never ends answers as its task goes.
static void cook_cancelled(void *p) {
	asuka_call *call = p;
	printf("pantry: cook cancelled: %d\n", asuka_call_cancelled(call));
	fflush(stdout);
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(call, &e);
}

static void p_cook(pantry_Pantry *self, const char *recipe, pantry_fn_float progress, asuka_call *call) {
	if (strcmp(recipe, "forever") == 0) {
		asuka_call_on_cancel(call, cook_cancelled, call);
		return;
	}
	cooking *c = malloc(sizeof *c);
	*c = (cooking){asuka_strdup(recipe), progress, call};
	if (!asuka_desktop_spawn(cook_run, c)) {
		asuka_error e = {0};
		asuka_error_failed(&e, "pantry", 0, "no thread");
		asuka_call_fail(call, &e);
		free(c->recipe);
		free(c);
	}
}

static bool p_ask(pantry_Pantry *self, pantry_fn_string__bool question, asuka_error *err) {
	return pantry_fn_string__bool_call(question, "ready?");
}

typedef struct {
	pantry_Shelf base;
	char *name;
} shelf;

static void s_release(pantry_Shelf *self) {
	shelf *s = (shelf *)self;
	printf("pantry: release shelf %s\n", s->name);
	fflush(stdout);
	free(s->name);
	free(s);
}

static void s_stack(pantry_Shelf *self, int64_t count, pantry_Level level, asuka_error *err) {
	printf("pantry: stack %s %lld level=%lld\n", ((shelf *)self)->name, (long long)count, (long long)level);
	fflush(stdout);
}

static void s_close(pantry_Shelf *self, asuka_error *err) {
	printf("pantry: close %s\n", ((shelf *)self)->name);
	fflush(stdout);
}

static const pantry_Shelf_vtable shelf_vt = {.release = s_release, .stack = s_stack, .close = s_close};

static pantry_Shelf *p_open(pantry_Pantry *self, const char *name, asuka_error *err) {
	if (strcmp(name, "locked") == 0) {
		asuka_error_declared(err, pantry_PantryError_busy);
		return NULL;
	}
	shelf *s = calloc(1, sizeof *s);
	s->base.vt = &shelf_vt;
	s->name = asuka_strdup(name);
	return &s->base;
}

static void p_show(pantry_Pantry *self, asuka_error *err) {
	printf("pantry: show on the main thread: %d\n", asuka_desktop_on_main());
	fflush(stdout);
}

static pantry_map_int p_counts(pantry_Pantry *self, asuka_error *err) {
	pantry_map_int m = {calloc(2, sizeof(char *)), calloc(2, sizeof(int64_t)), 2};
	m.keys[0] = asuka_strdup("jars");
	m.values[0] = 12;
	m.keys[1] = asuka_strdup("tins");
	m.values[1] = 4;
	return m;
}

// total sums decimals of one exponent and another: aligned to the
// smaller (the test's prices fit).
static asuka_decimal p_total(pantry_Pantry *self, pantry_list_decimal prices, asuka_error *err) {
	asuka_decimal sum = {0, 0};
	for (size_t i = 0; i < prices.len; i++) {
		asuka_decimal p = prices.data[i];
		while (sum.exponent > p.exponent) sum.coefficient *= 10, sum.exponent--;
		while (p.exponent > sum.exponent && sum.coefficient != 0) p.coefficient *= 10, p.exponent--;
		if (sum.coefficient == 0) sum.exponent = p.exponent;
		sum.coefficient += p.coefficient;
	}
	return sum;
}

static void p_release(pantry_Pantry *self) {
	printf("pantry: release pantry\n");
	fflush(stdout);
	free(self);
}

// shows gets a view as its id, which the host knows.
static bool p_shows(pantry_Pantry *self, uint32_t view, asuka_error *err) {
	bool shown = asuka_desktop_view(view) != NULL;
	printf("pantry: shows %s %d\n", view ? "a view" : "none", shown);
	fflush(stdout);
	return shown;
}

static const pantry_Pantry_vtable pantry_vt = {
	.release = p_release,
	.name = p_name,
	.items = p_items,
	.add = p_add,
	.cook = p_cook,
	.ask = p_ask,
	.open = p_open,
	.show = p_show,
	.counts = p_counts,
	.total = p_total,
	.shows = p_shows,
};

pantry_Pantry *pantry_make_Pantry(asuka_error *err) {
	pantry_Pantry *p = malloc(sizeof *p);
	p->vt = &pantry_vt;
	return p;
}

pantry_Shelf *pantry_make_Shelf(asuka_error *err) {
	asuka_error_unsupported(err, "a shelf comes from Pantry.open");
	return NULL;
}

// ---- Dial ----

typedef struct {
	pantry_Dial base;
	pantry_Dial_events events;
	char widget[8];  // the test host's widget: nothing to draw
} dial;

static void d_destroy(pantry_Dial *self) {
	printf("pantry: dial destroyed\n");
	fflush(stdout);
	free(self);
}

typedef struct {
	pantry_Dial_events events;
} turned;

// The user turns the dial, on another thread: the event is posted.
static void turn(void *p) {
	turned *t = p;
	pantry_Dial_changed(t->events, 0.75);
	free(t);
}

static void d_value(pantry_Dial *self, double v) {
	dial *d = (dial *)self;
	printf("pantry: dial value %g\n", v);
	fflush(stdout);
	if (v == 0.5) {
		turned *t = malloc(sizeof *t);
		t->events = d->events;
		asuka_desktop_spawn(turn, t);
	}
}

static void d_label(pantry_Dial *self, const char *v) {
	printf("pantry: dial label %s\n", v);
	fflush(stdout);
}

static void d_items(pantry_Dial *self, pantry_list_Item v) {
	dial *d = (dial *)self;
	for (size_t i = 0; i < v.len; i++) say_item("dial item", &v.data[i]);
	if (v.len > 0) pantry_Dial_picked(d->events, &v.data[0], (int64_t)v.len);
}

static bool d_measure(pantry_Dial *self, float w, float h, float *ow, float *oh) {
	*ow = 120;
	*oh = 32;
	return true;
}

static const pantry_Dial_vtable dial_vt = {
	.destroy = d_destroy,
	.set_value = d_value,
	.set_label = d_label,
	.set_items = d_items,
	.measure = d_measure,
};

pantry_Dial *pantry_make_Dial(pantry_Dial_events events) {
	dial *d = calloc(1, sizeof *d);
	d->base.vt = &dial_vt;
	d->base.widget = d->widget;
	d->events = events;
	printf("pantry: dial made for view %u\n", events.view);
	fflush(stdout);
	return &d->base;
}
