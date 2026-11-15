// keys.c: what a view does with keys while the keyboard's focus is in it
// (ui/keys.go's KeyMap): the keys style key, a [Shortcut], and the
// root's keys (ui.use_keys: window.go's UseKeys), which take keys no
// view took and keys when nothing has the focus.
//
// A key goes down (and repeats, held) to the nearest view, from the
// focused one out, whose Keys prop has it: the host finds it and reports
// Key with the key's index in that prop, whose entries are sorted by
// name, then modifiers. A character with no modifiers takes typing from
// a field inside: give it a modifier, or put it where no field is.
#include "ui/ui_ext.h"

// A key, as the Keys prop has it, and what it runs.
typedef struct {
	char buf[4];
	ak_span name;  // its own when not in buf (kname)
	bool inbuf;
	uint8_t mods;
	ak_closure *run;  // borrowed from the array it came from
	uint32_t index;   // in that array
} kent;

static uint8_t mods_of(const ak_struct *s) {
	return (uint8_t)((s->f[1].i ? 1 : 0) | (s->f[2].i ? 2 : 0) | (s->f[3].i ? 4 : 0) | (s->f[4].i ? 8 : 0));
}

static ak_span kname(const kent *k) { return k->inbuf ? (ak_span){k->buf, k->name.len} : k->name; }

static void kent_of(kent *k, const ak_struct *s, uint32_t index) {
	k->name = ak_key_name(ak_span_str(s->f[0].p), k->buf);
	k->inbuf = k->name.p == k->buf;
	k->mods = mods_of(s);
	k->run = s->f[5].p;
	k->index = index;
}

static int kent_cmp(const kent *a, const kent *b) {
	ak_span x = kname(a), y = kname(b);
	uint32_t n = x.len < y.len ? x.len : y.len;
	int c = n ? memcmp(x.p, y.p, n) : 0;
	if (c) return c;
	if (x.len != y.len) return x.len < y.len ? -1 : 1;
	return (int)a->mods - (int)b->mods;
}

// sort_keys sorts ks (insertion: maps of keys are small) and drops all
// but the last of each key, which wins.
static uint32_t sort_keys(kent *ks, uint32_t n) {
	for (uint32_t i = 1; i < n; i++) {
		kent x = ks[i];
		uint32_t j = i;
		// Stable: an equal key stays after, so the last given is last.
		while (j > 0 && kent_cmp(&ks[j - 1], &x) > 0) {
			ks[j] = ks[j - 1];
			j--;
		}
		ks[j] = x;
	}
	uint32_t m = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (i + 1 < n && kent_cmp(&ks[i], &ks[i + 1]) == 0) continue;
		ks[m++] = ks[i];
	}
	return m;
}

static void send_keys(ak_view *e, const kent *ks, uint32_t n) {
	ak_key *wire = n ? ak_alloc(n * sizeof(ak_key)) : NULL;
	for (uint32_t i = 0; i < n; i++) wire[i] = (ak_key){kname(&ks[i]), ks[i].mods};
	ak_buf *b = ak_batch_set_bytes_begin(ak_view_batch(e), e->id, AK_PROP_KEYS);
	ak_append_keys(b, wire, n);
	ak_batch_set_bytes_end(ak_view_batch(e));
	ak_free(wire);
	if (n) ak_view_listen(e, AK_EVENT_KEY);
}

// ak_keys_set gives e the keys of a (Shortcut structs).
void ak_keys_set(ak_view *e, const ak_arr *a) {
	ak_ix *ix = ak_ix_of(e);
	uint32_t n = a ? a->len : 0;
	kent *ks = n ? ak_alloc(n * sizeof(kent)) : NULL;
	for (uint32_t i = 0; i < n; i++) kent_of(&ks[i], a->data[i].p, i);
	uint32_t m = sort_keys(ks, n);
	ak_free(ix->key_order);
	ix->key_order = m ? ak_alloc(m * sizeof(uint32_t)) : NULL;
	ix->nkeys = m;
	for (uint32_t i = 0; i < m; i++) ix->key_order[i] = ks[i].index;
	send_keys(e, ks, m);
	ak_free(ks);
}

// ---- The root's keys ----

// A use_keys call's keys, owned by the component that made them.
typedef struct keyreg {
	ak_ui_root *r;
	ak_arr *keys;  // retained
	bool gone;
} keyreg;

typedef struct ak_rootkeys {
	AK_VEC(keyreg *) regs;
	kent *merged;  // sorted; the last registration's win
	uint32_t n;
} ak_rootkeys;

void ak_rootkeys_free(ak_rootkeys *k);

void ak_rootkeys_free(ak_rootkeys *k) {
	if (!k) return;
	for (uint32_t i = 0; i < k->regs.len; i++) {
		ak_release(k->regs.data[i]->keys);
		ak_free(k->regs.data[i]);
	}
	ak_vec_free(&k->regs);
	ak_free(k->merged);
	ak_free(k);
}

static ak_rootkeys *rootkeys(ak_ui_root *r) {
	ak_rext *x = ak_rext_of(r);
	if (!x->keys) x->keys = ak_alloc(sizeof(ak_rootkeys));
	return x->keys;
}

static void root_send(ak_ui_root *r) {
	ak_rootkeys *k = rootkeys(r);
	uint32_t n = 0;
	for (uint32_t i = 0; i < k->regs.len; i++) n += k->regs.data[i]->keys ? k->regs.data[i]->keys->len : 0;
	ak_free(k->merged);
	k->merged = n ? ak_alloc(n * sizeof(kent)) : NULL;
	uint32_t at = 0;
	for (uint32_t i = 0; i < k->regs.len; i++) {
		const ak_arr *a = k->regs.data[i]->keys;
		for (uint32_t j = 0; a && j < a->len; j++) kent_of(&k->merged[at++], a->data[j].p, j);
	}
	k->n = sort_keys(k->merged, at);
	if (r->el && !r->el->dead) send_keys(r->el, k->merged, k->n);
}

static void reg_gone(void *ctx) {
	keyreg *g = ctx;
	ak_ui_root *r = g->r;
	ak_rootkeys *k = rootkeys(r);
	for (uint32_t i = 0; i < k->regs.len; i++) {
		if (k->regs.data[i] != g) continue;
		memmove(k->regs.data + i, k->regs.data + i + 1, (k->regs.len - i - 1) * sizeof(keyreg *));
		k->regs.len--;
		break;
	}
	ak_release(g->keys);
	ak_free(g);
	if (!r->disposed) root_send(r);
	ak_root_release(r);
}

// ak_ui_use_keys gives the root keys (Shortcut structs) while the
// current owner lives: what no view takes, and keys with nothing
// focused. Several: the last one's keys win.
void ak_ui_use_keys(ak_arr *keys) {
	ak_ui_root *r = ak_ui_current();
	keyreg *g = ak_alloc(sizeof *g);
	g->r = r;
	ak_root_retain(r);
	g->keys = ak_retain(keys);
	ak_vec_push(&rootkeys(r)->regs, g);
	ak_on_cleanup(reg_gone, g);
	root_send(r);
}

// ak_keys_event runs the index'th key of e's Keys prop.
bool ak_keys_event(ak_view *e, int64_t index) {
	if (e->r && e == e->r->el) {
		ak_rootkeys *k = e->r->ext ? e->r->ext->keys : NULL;
		if (!k || index < 0 || index >= k->n) return true;
		ak_closure *run = k->merged[index].run;
		ak_call_closure(run, e->r->owner, NULL, 0);
		return true;
	}
	ak_ix *ix = e->ix;
	const ak_val *v = ak_style_get(e, AK_KEY_KEYS);
	const ak_arr *a = v ? v->p : NULL;
	if (!ix || !a || index < 0 || index >= ix->nkeys) return true;
	uint32_t i = ix->key_order[index];
	if (i >= a->len) return true;
	const ak_struct *s = a->data[i].p;
	ak_call_closure(s->f[5].p, e->owner, NULL, 0);
	return true;
}
