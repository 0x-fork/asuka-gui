// path.c: instance paths, by which the dev loop keeps state cells across
// reloads (ui.h's "Instance paths"). A path is a chain of segments: a
// component instance's name and its count among those of its name in
// the same place (App#0, MessageRow#2), a Show's branch (?then, ?else), a
// Switch's case (?3), a For's or a List's row key ([42], [milk]), a
// Dynamic's build (~). The current path is a stack the compiler's
// ak_ui_enter and ak_ui_leave push and pop around a component's body;
// control flow captures the path it's made in and pushes its segment
// when it builds, later.
#include "ui/ui_int.h"

#include <stdio.h>

typedef struct {
	ak_str *name;
	uint32_t n;
} path_count;

struct ak_path {
	uint32_t rc;
	ak_path *parent;
	char *seg;
	uint32_t nseg;
	AK_VEC(path_count) counts;  // the components entered here, by name
};

static ak_path *cur;  // held

static ak_path *node_new(ak_path *parent, const char *seg, size_t n) {
	ak_path *p = ak_alloc(sizeof *p);
	p->rc = 1;
	p->parent = parent;
	if (parent) parent->rc++;
	p->seg = ak_alloc(n + 1);
	memcpy(p->seg, seg, n);
	p->nseg = (uint32_t)n;
	return p;
}

void ak_path_release(ak_path *p) {
	while (p && --p->rc == 0) {
		ak_path *parent = p->parent;
		for (uint32_t i = 0; i < p->counts.len; i++) ak_release(p->counts.data[i].name);
		ak_vec_free(&p->counts);
		ak_free(p->seg);
		ak_free(p);
		p = parent;
	}
}

void ak_path_reset(void) {
	ak_path_release(cur);
	cur = node_new(NULL, "", 0);
}

static ak_path *current(void) {
	if (!cur) cur = node_new(NULL, "", 0);
	return cur;
}

ak_path *ak_path_capture(void) {
	ak_path *p = current();
	p->rc++;
	return p;
}

ak_path *ak_path_push(ak_path *base, const char *seg, size_t n) {
	ak_path *prev = current();  // the caller holds it until ak_path_pop
	cur = node_new(base ? base : prev, seg, n);
	return prev;
}

void ak_path_pop(ak_path *prev) {
	ak_path_release(cur);
	cur = prev;
}

void ak_ui_enter(ak_str *name) {
	ak_path *p = current();
	uint32_t n = 0;
	bool found = false;
	for (uint32_t i = 0; i < p->counts.len; i++) {
		if (ak_str_eq(p->counts.data[i].name, name)) {
			n = p->counts.data[i].n++;
			found = true;
			break;
		}
	}
	// A copy, not name retained: a component's name is its module's
	// constant, which a reload frees while the path (the dev loop's
	// keep) outlives it.
	if (!found) ak_vec_push(&p->counts, ((path_count){ak_str_new(name->data, name->len), 1}));
	char buf[16];
	int k = snprintf(buf, sizeof buf, "#%u", n);
	char *seg = ak_alloc(name->len + (size_t)k);
	memcpy(seg, name->data, name->len);
	memcpy(seg + name->len, buf, (size_t)k);
	// The node's parent is where it was entered: leave goes back there.
	cur = node_new(p, seg, name->len + (size_t)k);
	ak_path_release(p);  // cur's parent holds it now
	ak_free(seg);
}

void ak_ui_leave(void) {
	ak_path *p = current();
	if (!p->parent) return;  // unbalanced: stay at the top
	ak_path *parent = p->parent;
	parent->rc++;
	ak_path_release(p);
	cur = parent;
}

static void write_path(ak_buf *b, const ak_path *p) {
	if (!p) return;
	write_path(b, p->parent);
	if (!p->nseg) return;
	if (b->len) ak_buf_u8(b, '/');
	ak_buf_put(b, p->seg, p->nseg);
}

ak_str *ak_ui_path(void) {
	ak_buf b = {0};
	write_path(&b, current());
	ak_str *s = ak_str_new((const char *)b.data, b.len);
	ak_buf_free(&b);
	return s;
}

ak_str *ak_ui_path_key(ak_str *key) {
	ak_buf b = {0};
	write_path(&b, current());
	if (b.len) ak_buf_u8(&b, '/');
	ak_buf_put(&b, key->data, key->len);
	ak_str *s = ak_str_new((const char *)b.data, b.len);
	ak_buf_free(&b);
	return s;
}
