// show.c: values as asuka test prints them, in snapshots and a property
// test's failing input (docs/testing.md#snapshots). As the language
// writes them: "a" with its escapes, [1, 2], ["a": 1], Set([1]), a
// struct as Todo(title: "Milk", done: false), an enum's case as
// .circle(radius: 2), none; a map's entries and a set's elements sorted,
// so that the text doesn't depend on the order they were added in; a
// line per item, indented by two, when a list doesn't fit in 80 columns.
// A type's name is said without its package's path.
#include "testing.h"
#include "vm/vm_int.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 80
// Deeper than this, a value is "…": a recursive type's long chain.
#define MAX_DEPTH 200

ak_kind ak_desc_kind(const char *d) {
	switch (*d) {
	case 'i':
	case 'e':
	case 'd':
	case 'c':
		return AK_K_INT;
	case 'f':
		return AK_K_FLOAT;
	case 'b':
		return AK_K_BOOL;
	default:
		return AK_K_REF;
	}
}

// own_show writes v's own description, when its type (shape sh) has
// one: how it says it prints.
static bool own_show(ak_strb *b, const ak_shape *sh, ak_val v) {
	if (!ak_shape_has(sh, AK_HOOK_SHOW)) return false;
	ak_val t = ak_shape_call(sh, AK_HOOK_SHOW, &v, 1);
	if (t.p) {
		ak_strb_str(b, t.p);
		ak_release(t.p);
	}
	return true;
}

const char *ak_desc_end(const char *p) {
	for (;;) {
		switch (*p) {
		case '?':
		case '[':
		case '{':
		case '<':
			p++;
			continue;
		case 'S':
		case 'E':
		case 'J':
		case 'e': {
			const char *q = strchr(p, ';');
			return q ? q + 1 : p + strlen(p);
		}
		case 'm':
			return ak_desc_end(ak_desc_end(p + 1));
		case '\0':
			return p;
		default:
			return p + 1;
		}
	}
}

static size_t name_len(const char *d) {
	const char *q = strchr(d + 1, ';');
	return q ? (size_t)(q - d - 1) : strlen(d + 1);
}

// type_name writes a shape's name without its packages' paths
// ("./model.Todo" is Todo, "Box<std/time.Date>" Box<Date>).
static void type_name(ak_strb *b, const char *n) {
	const char *seg = n;
	for (const char *p = n;; p++) {
		char c = *p;
		if (c == '<' || c == '>' || c == ',' || c == '(' || c == ')' || c == '?' || c == '[' || c == ']' || c == ' ' || c == ':' || c == 0) {
			const char *dot = NULL;
			for (const char *q = seg; q < p; q++)
				if (*q == '.') dot = q;
			const char *from = dot ? dot + 1 : seg;
			ak_strb_cstr(b, from, (size_t)(p - from));
			if (!c) return;
			ak_strb_cstr(b, p, 1);
			seg = p + 1;
		}
	}
}

// show_str writes s as an Asuka string literal.
static void show_str(ak_strb *b, const char *s, size_t n) {
	ak_strb_cstr(b, "\"", 1);
	size_t start = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		const char *esc = NULL;
		char buf[16];
		switch (c) {
		case '"': esc = "\\\""; break;
		case '\\': esc = "\\\\"; break;
		case '{': esc = "\\{"; break;
		case '}': esc = "\\}"; break;
		case '\n': esc = "\\n"; break;
		case '\t': esc = "\\t"; break;
		case '\r': esc = "\\r"; break;
		case 0: esc = "\\0"; break;
		default:
			if (c < 0x20 || c == 0x7f) {
				snprintf(buf, sizeof buf, "\\u{%x}", c);
				esc = buf;
			}
		}
		if (!esc) continue;
		ak_strb_cstr(b, s + start, i - start);
		ak_strb_cstr(b, esc, strlen(esc));
		start = i + 1;
	}
	ak_strb_cstr(b, s + start, n - start);
	ak_strb_cstr(b, "\"", 1);
}

// A list being written: its items, each written on its own, then joined
// on a line or a line each.
typedef struct {
	ak_buf *items;
	uint32_t len, cap;
} items;

static void items_add(items *l, ak_strb *b) {
	if (l->len == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 8;
		l->items = ak_realloc(l->items, sizeof(ak_buf) * l->cap);
	}
	l->items[l->len++] = *ak_strb_buf(b);
	*b = (ak_strb){0};
}

static void items_free(items *l) {
	for (uint32_t i = 0; i < l->len; i++) ak_buf_free(&l->items[i]);
	ak_free(l->items);
}

static int cmp_buf(const void *a, const void *b) {
	const ak_buf *x = a, *y = b;
	size_t n = x->len < y->len ? x->len : y->len;
	int c = memcmp(x->data, y->data, n);
	if (c) return c;
	return x->len < y->len ? -1 : x->len > y->len;
}

// join writes open, the items and close: on the line when they fit in
// what's left of it (none has a line break), else an item a line,
// indented, each ending with a comma.
static void join(ak_strb *b, const char *open, items *l, const char *close, int indent) {
	size_t total = strlen(open) + strlen(close) + (size_t)indent;
	bool lines = false;
	for (uint32_t i = 0; i < l->len; i++) {
		total += l->items[i].len + (i ? 2 : 0);
		if (memchr(l->items[i].data, '\n', l->items[i].len)) lines = true;
	}
	if (total > WIDTH) lines = true;
	ak_strb_cstr(b, open, strlen(open));
	for (uint32_t i = 0; i < l->len; i++) {
		if (lines) {
			ak_strb_cstr(b, "\n", 1);
			for (int k = 0; k < indent + 2; k++) ak_strb_cstr(b, " ", 1);
		} else if (i) {
			ak_strb_cstr(b, ", ", 2);
		}
		ak_strb_cstr(b, (const char *)l->items[i].data, l->items[i].len);
		if (lines) ak_strb_cstr(b, ",", 1);
	}
	if (lines && l->len) {
		ak_strb_cstr(b, "\n", 1);
		for (int k = 0; k < indent; k++) ak_strb_cstr(b, " ", 1);
	}
	ak_strb_cstr(b, close, strlen(close));
}

typedef struct {
	const ak_json_types *t;
	int depth;
} shower;

static void show(shower *s, ak_strb *b, ak_val v, const char *d, int indent);

static const ak_shape *shape_of(shower *s, const char *d) {
	const ak_json_types *t = s->t;
	return t && t->shape ? t->shape(t->ctx, d + 1, name_len(d)) : NULL;
}

static void show_duration(ak_strb *b, int64_t ns) {
	static const struct {
		int64_t n;
		const char *unit;
	} units[] = {{3600000000000LL, "h"}, {60000000000LL, "min"}, {1000000000LL, "s"}, {1000000LL, "ms"}};
	if (ns == 0) {
		ak_strb_cstr(b, "0s", 2);
		return;
	}
	for (size_t i = 0; i < sizeof units / sizeof *units; i++) {
		if (ns % units[i].n == 0) {
			ak_strb_int(b, ns / units[i].n);
			ak_strb_cstr(b, units[i].unit, strlen(units[i].unit));
			return;
		}
	}
	ak_strb_float(b, (double)ns / 1e6);
	ak_strb_cstr(b, "ms", 2);
}

static void show_color(ak_strb *b, int64_t c) {
	ak_builtin_fn hex = ak_builtin_find("color.hex");
	if (!hex) {
		ak_strb_int(b, c);
		return;
	}
	ak_val arg = {.i = c};
	ak_val r = hex(&arg, 1);
	ak_str *str = r.p;
	if (str) ak_strb_str(b, str);
	ak_release(str);
}

// fields writes a struct's fields, or an enum case's payload: name: v, or
// v alone where the name is a position ("0").
static void fields(shower *s, items *l, const ak_val *f, uint16_t n, const char *const *names, const ak_shape *sh, int ci, int indent) {
	for (uint16_t i = 0; i < n; i++) {
		ak_strb fb = {0};
		const char *name = names ? names[i] : NULL;
		if (name && *name && !(name[0] >= '0' && name[0] <= '9')) {
			ak_strb_cstr(&fb, name, strlen(name));
			ak_strb_cstr(&fb, ": ", 2);
		}
		extern const char *ak_json_field_desc(const ak_shape *s, int i);
		extern const char *ak_json_case_desc(const ak_shape *s, int c, int i);
		const char *fd = ci < 0 ? ak_json_field_desc(sh, i) : ak_json_case_desc(sh, ci, i);
		show(s, &fb, f[i], fd, indent + 2);
		items_add(l, &fb);
	}
}

// A map's or a set's entries, sorted by their keys: ints by value,
// strings by their bytes, others by how they print.
typedef struct {
	ak_val k, v;
	ak_buf ktext;
} entry;

static ak_kind key_kind(const ak_map *m) { return (ak_kind)(m->h.aux & 0xf); }

static int cmp_entry_int(const void *a, const void *b) {
	int64_t x = ((const entry *)a)->k.i, y = ((const entry *)b)->k.i;
	return x < y ? -1 : x > y;
}

static int cmp_entry_text(const void *a, const void *b) { return cmp_buf(&((const entry *)a)->ktext, &((const entry *)b)->ktext); }

static int cmp_entry_str(const void *a, const void *b) {
	const ak_str *x = ((const entry *)a)->k.p, *y = ((const entry *)b)->k.p;
	size_t n = x->len < y->len ? x->len : y->len;
	int c = memcmp(x->data, y->data, n);
	if (c) return c;
	return x->len < y->len ? -1 : x->len > y->len;
}

static entry *sorted(shower *s, const ak_map *m, const char *kd, uint32_t *n) {
	entry *e = ak_alloc(sizeof(entry) * (m->live ? m->live : 1));
	uint32_t k = 0;
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		e[k].k = m->keys[i];
		e[k].v = ak_map_is_set(m) ? (ak_val){0} : m->vals[i];
		k++;
	}
	if (*kd == 'i' || *kd == 'e' || *kd == 'd' || *kd == 'c' || *kd == 'b') {
		qsort(e, k, sizeof *e, cmp_entry_int);
	} else if (*kd == 's' && (m->h.flags & AK_MAP_STRKEYS)) {
		qsort(e, k, sizeof *e, cmp_entry_str);
	} else {
		for (uint32_t i = 0; i < k; i++) {
			ak_strb kb = {0};
			show(s, &kb, e[i].k, kd, 0);
			e[i].ktext = *ak_strb_buf(&kb);
		}
		qsort(e, k, sizeof *e, cmp_entry_text);
		for (uint32_t i = 0; i < k; i++) ak_buf_free(&e[i].ktext);
	}
	AK_UNUSED(key_kind);
	*n = k;
	return e;
}

static void show(shower *s, ak_strb *b, ak_val v, const char *d, int indent) {
	if (s->depth > MAX_DEPTH) {
		ak_strb_cstr(b, "…", strlen("…"));
		return;
	}
	s->depth++;
	while (*d == '?') {
		if (!v.p) {
			ak_strb_cstr(b, "none", 4);
			goto done;
		}
		if (ak_desc_kind(d + 1) != AK_K_REF) v = ((ak_box *)v.p)->v;
		d++;
	}
	switch (*d) {
	case 'i':
		ak_strb_int(b, v.i);
		break;
	case 'd':
		show_duration(b, v.i);
		break;
	case 'c':
		show_color(b, v.i);
		break;
	case 'f':
		if (v.f != v.f) ak_strb_cstr(b, "nan", 3);
		else if (isinf(v.f)) ak_strb_cstr(b, v.f > 0 ? "inf" : "-inf", v.f > 0 ? 3 : 4);
		else {
			size_t at = ak_strb_buf(b)->len;
			ak_strb_float(b, v.f);
			// A float says it's one: 1.0, not 1.
			bool frac = false;
			for (size_t i = at; i < b->b.len; i++)
				if (b->b.data[i] == '.' || b->b.data[i] == 'e') frac = true;
			if (!frac) ak_strb_cstr(b, ".0", 2);
		}
		break;
	case 'b':
		ak_strb_bool(b, v.i != 0);
		break;
	case 's': {
		ak_str *str = v.p;
		if (str) show_str(b, str->data, str->len);
		else ak_strb_cstr(b, "\"\"", 2);
		break;
	}
	case 'y': {
		ak_bytes *by = v.p;
		ak_strb_cstr(b, "<bytes", 6);
		static const char hex[] = "0123456789abcdef";
		for (uint32_t i = 0; by && i < by->len; i++) {
			char h[3] = {i % 4 == 0 ? ' ' : 0, hex[by->data[i] >> 4], hex[by->data[i] & 15]};
			if (h[0]) ak_strb_cstr(b, h, 3);
			else ak_strb_cstr(b, h + 1, 2);
		}
		ak_strb_cstr(b, ">", 1);
		break;
	}
	case 'e': {
		const ak_json_types *t = s->t;
		const char *const *cases;
		uint32_t n;
		if (t && t->cases && t->cases(t->ctx, d + 1, name_len(d), &cases, &n) && v.i >= 0 && (uint64_t)v.i < n) {
			ak_strb_cstr(b, ".", 1);
			ak_strb_cstr(b, cases[v.i], strlen(cases[v.i]));
		} else {
			ak_strb_int(b, v.i);
		}
		break;
	}
	case '[': {
		ak_arr *a = v.p;
		items l = {0};
		for (uint32_t i = 0; a && i < a->len; i++) {
			ak_strb ib = {0};
			show(s, &ib, a->data[i], d + 1, indent + 2);
			items_add(&l, &ib);
		}
		join(b, "[", &l, "]", indent);
		items_free(&l);
		break;
	}
	case '<': {
		ak_map *m = v.p;
		items l = {0};
		uint32_t n = 0;
		entry *e = m ? sorted(s, m, d + 1, &n) : NULL;
		for (uint32_t i = 0; i < n; i++) {
			ak_strb ib = {0};
			show(s, &ib, e[i].k, d + 1, indent + 2);
			items_add(&l, &ib);
		}
		ak_free(e);
		join(b, "Set([", &l, "])", indent);
		items_free(&l);
		break;
	}
	case '{':
	case 'm': {
		ak_map *m = v.p;
		const char *kd = *d == '{' ? "s" : d + 1;
		const char *vd = *d == '{' ? d + 1 : ak_desc_end(d + 1);
		if (!m || !m->live) {
			ak_strb_cstr(b, "[:]", 3);
			break;
		}
		items l = {0};
		uint32_t n = 0;
		entry *e = sorted(s, m, kd, &n);
		for (uint32_t i = 0; i < n; i++) {
			ak_strb ib = {0};
			show(s, &ib, e[i].k, kd, indent + 2);
			ak_strb_cstr(&ib, ": ", 2);
			show(s, &ib, e[i].v, vd, indent + 2);
			items_add(&l, &ib);
		}
		ak_free(e);
		join(b, "[", &l, "]", indent);
		items_free(&l);
		break;
	}
	case 'S':
	case '(': {
		ak_struct *o = v.p;
		const ak_shape *sh = o ? o->shape : shape_of(s, d);
		if (!o || !sh) {
			ak_strb_cstr(b, "none", 4);
			break;
		}
		if (own_show(b, sh, v)) break;
		items l = {0};
		fields(s, &l, o->f, sh->nfields, sh->names, sh, -1, indent);
		ak_strb open = {0};
		if (*d == 'S') type_name(&open, sh->name);
		ak_strb_cstr(&open, "(", 1);
		ak_buf_put(ak_strb_buf(&open), "", 1);
		join(b, (const char *)ak_strb_buf(&open)->data, &l, ")", indent);
		ak_buf_free(&open.b);
		items_free(&l);
		break;
	}
	case 'E': {
		ak_enum *en = v.p;
		if (!en) {
			ak_strb_cstr(b, "none", 4);
			break;
		}
		const ak_shape *sh = en->shape;
		if (own_show(b, sh, v)) break;
		uint16_t ci = ak_enum_case(en);
		const ak_case *c = &sh->cases[ci];
		if (!c->nfields) {
			ak_strb_cstr(b, ".", 1);
			ak_strb_cstr(b, c->name, strlen(c->name));
			break;
		}
		items l = {0};
		fields(s, &l, en->f, c->nfields, c->names, sh, ci, indent);
		ak_strb open = {0};
		ak_strb_cstr(&open, ".", 1);
		ak_strb_cstr(&open, c->name, strlen(c->name));
		ak_strb_cstr(&open, "(", 1);
		ak_buf_put(ak_strb_buf(&open), "", 1);
		join(b, (const char *)ak_strb_buf(&open)->data, &l, ")", indent);
		ak_buf_free(&open.b);
		items_free(&l);
		break;
	}
	case 'J': {
		ak_str *j = ak_json_encode(v, d, s->t, 0);
		ak_strb_str(b, j);
		ak_release(j);
		break;
	}
	default:
		ak_strb_cstr(b, "<value>", 7);
	}
done:
	s->depth--;
}

void ak_show(ak_strb *b, ak_val v, const char *desc, const ak_json_types *t) {
	shower s = {t, 0};
	show(&s, b, v, desc, 0);
}

// test.show(value, desc): what a snapshot says of a value.
static ak_val test_show(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *desc = args[1].p;
	ak_json_types t;
	ak_vm_json_types(&t);
	ak_strb b = {0};
	ak_show(&b, args[0], desc->data, &t);
	return (ak_val){.p = ak_strb_finish(&b)};
}

void ak_show_builtins_init(void) { ak_builtin_register("test.show", test_show); }
