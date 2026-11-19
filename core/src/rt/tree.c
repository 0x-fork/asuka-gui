// tree.c: B-trees, std/collections' SortedMap and SortedSet (rt.h's
// ak_tree). The algorithms are the textbook's top-down ones (CLRS's, and
// Go's google/btree's): an insert splits each full node on its way down,
// a removal fills each node it goes down into to more than the least, so
// neither ever goes back up. Every node changed on the way is made
// unique first (node_mut): shared, it's copied, its references retained,
// and the copy put in its parent, itself unique by then.
#include "rt/lib.h"

// ---- Nodes ----

static void layout(ak_tnode *n, ak_kind kk, ak_kind vk, bool leaf) {
	bool kref = kk == AK_K_REF, vref = vk == AK_K_REF, set = vk == AK_K_VOID;
	const uint16_t M = AK_TREE_MAX;
	if (set) {
		n->ko = 0;
		n->vo = 0;
		n->co = M;
		n->from = kref ? 0 : M;
	} else if (kref && !vref) {
		// The values, scalars, first: the keys and the children are the
		// references, side by side.
		n->vo = 0;
		n->ko = M;
		n->co = 2 * M;
		n->from = M;
	} else {
		n->ko = 0;
		n->vo = M;
		n->co = 2 * M;
		n->from = kref ? 0 : vref ? M : 2 * M;
	}
	n->to = leaf ? n->co : (uint16_t)(n->co + M + 1);
	if (n->from > n->to) n->from = n->to;
}

static size_t node_size(const ak_tnode *n) {
	return sizeof(ak_tnode) + sizeof(ak_val) * (ak_tnode_leaf(n) ? n->co : n->co + AK_TREE_MAX + 1);
}

// node_new is an empty node of t's layout, its references none.
static ak_tnode *node_new(const ak_tree *t, bool leaf) {
	ak_tnode probe = {0};
	layout(&probe, (ak_kind)t->kk, (ak_kind)t->vk, leaf);
	probe.h.flags = leaf ? AK_TNODE_LEAF : 0;
	size_t size = node_size(&probe);
	ak_tnode *n = ak_realloc(NULL, size);  // its scalars' slots are written before they're read
	*n = probe;
	n->h = (ak_obj){.rc = 1, .tag = AK_TAG_TNODE, .flags = probe.h.flags};
	memset(n->s + n->from, 0, sizeof(ak_val) * (size_t)(n->to - n->from));
	ak_obj_count(1);
	return n;
}

// node_copy is a unique copy of n, its references retained.
static ak_tnode *node_copy(const ak_tnode *n) {
	size_t size = node_size(n);
	ak_tnode *c = ak_realloc(NULL, size);
	memcpy(c, n, size);
	c->h.rc = 1;
	for (uint32_t i = n->from; i < n->to; i++) ak_retain(c->s[i].p);
	ak_obj_count(1);
	return c;
}

ak_tnode *ak_tnode_shell(const ak_tnode *n) {
	size_t size = node_size(n);
	ak_tnode *c = ak_realloc(NULL, size);
	memcpy(c, n, size);
	c->h.rc = 1;
	memset(c->s + c->from, 0, sizeof(ak_val) * (size_t)(c->to - c->from));
	ak_obj_count(1);
	return c;
}

// node_mut makes the node in slot (a child's, a root's) unique: a
// copy, when it's shared. Slots are ak_vals, read and written as their
// .p only (C's aliasing: never as an ak_tnode *'s object).
static ak_tnode *node_mut(ak_val *slot) {
	ak_tnode *n = slot->p;
	if (AK_LIKELY(ak_unique(n))) return n;
	ak_tnode *c = node_copy(n);
	ak_release(n);
	slot->p = c;
	return c;
}

static inline void set_len(ak_tnode *n, uint32_t len) { n->h.aux = (uint16_t)len; }

// ins puts x at i of a group of len, moving those from i on up one.
static inline void ins(ak_val *a, uint32_t len, uint32_t i, ak_val x) {
	memmove(a + i + 1, a + i, sizeof(ak_val) * (len - i));
	a[i] = x;
}

// del takes a group's i'th of len out (its reference the caller's),
// moving those after it down one, the slot left NULL.
static inline ak_val del(ak_val *a, uint32_t len, uint32_t i) {
	ak_val x = a[i];
	memmove(a + i, a + i + 1, sizeof(ak_val) * (len - i - 1));
	a[len - 1] = (ak_val){0};
	return x;
}

static inline ak_val *kidv(const ak_tnode *n) { return (ak_val *)n->s + n->co; }

// ---- Trees ----

ak_kind ak_tree_desc_kind(const char *d) {
	switch (d[0]) {
	case 'i':
	case 'r':
	case 'n':
	case 'e':
		return AK_K_INT;
	case 'f':
		return AK_K_FLOAT;
	case 'b':
		return AK_K_BOOL;
	default:
		return AK_K_REF;
	}
}

ak_tree_mode ak_tree_desc_mode(const char *d) {
	switch (d[0]) {
	case 'n':
		return d[1] == '0' + AK_U64 ? AK_TM_U64 : AK_TM_INT;
	case 'i':
	case 'r':
	case 'e':
		return AK_TM_INT;
	case 'f':
		return AK_TM_FLOAT;
	case 's':
		return AK_TM_STR;
	default:
		return AK_TM_CMP;
	}
}

ak_tree *ak_tree_new(ak_tree_mode mode, ak_kind kk, ak_kind vk) {
	ak_tree *t = ak_obj_new(AK_TAG_TREE, sizeof(ak_tree));
	t->mode = (uint8_t)mode;
	t->kk = (uint8_t)kk;
	t->vk = (uint8_t)vk;
	return t;
}

ak_tree *ak_tree_mut(ak_tree **tp) {
	ak_tree *t = *tp;
	if (AK_LIKELY(ak_unique(t))) return t;
	ak_tree *c = ak_tree_new((ak_tree_mode)t->mode, (ak_kind)t->kk, (ak_kind)t->vk);
	c->height = t->height;
	c->count = t->count;
	c->kshape = t->kshape;
	c->root.p = ak_retain(t->root.p);
	ak_release(t);
	*tp = c;
	return c;
}

static inline ak_tnode *root_of(const ak_tree *t) { return t->root.p; }

// ---- Ordering ----

// lt_call is a < b by o's closure, or the keys' own compare.
static AK_NOINLINE bool lt_call(ak_tree_order o, ak_val a, ak_val b) {
	ak_val args[2] = {a, b};
	if (o.by) return ak_call(o.by, args, 2).i != 0;
	const ak_shape *s = o.kshape ? o.kshape : ((const ak_struct *)a.p)->shape;  // an enum's is where a struct's is
	if (AK_LIKELY(ak_shape_has(s, AK_HOOK_CMP))) return ak_shape_call(s, AK_HOOK_CMP, args, 2).i < 0;
	if (o.kshape || ((const ak_obj *)a.p)->tag != AK_TAG_ENUM) ak_panic("%s has no compare", s->name);
	// An enum without payloads kept as a reference: its cases' order.
	return ak_enum_case(a.p) < ak_enum_case(b.p);
}

static inline bool lt(ak_tree_order o, ak_val a, ak_val b) {
	if (AK_UNLIKELY(o.by != NULL)) return lt_call(o, a, b);
	switch ((ak_tree_mode)o.mode) {
	case AK_TM_INT: return a.i < b.i;
	case AK_TM_U64: return (uint64_t)a.i < (uint64_t)b.i;
	case AK_TM_FLOAT: return a.f < b.f;
	case AK_TM_STR: return ak_str_cmp(a.p, b.p) < 0;
	default: return lt_call(o, a, b);
	}
}

// lower is where k is or would go among a node's n keys: the first not
// less than k, and whether it's k (*found). Without a branch on the
// comparisons for the keys' own order (a search goes either way as
// often: a branch would be mispredicted half the time).
static AK_ALWAYS_INLINE uint32_t lower(ak_tree_order o, const ak_val *keys, uint32_t n, ak_val k, bool *found) {
	if (AK_UNLIKELY(n == 0)) {
		*found = false;
		return 0;
	}
	if (AK_LIKELY(!o.by)) {
		switch ((ak_tree_mode)o.mode) {
		case AK_TM_INT: {
			const ak_val *b = keys;
			uint32_t len = n;
			while (len > 1) {
				uint32_t half = len / 2;
				b = b[half].i < k.i ? b + half : b;
				len -= half;
			}
			uint32_t i = (uint32_t)(b - keys) + (b->i < k.i);
			*found = i < n && keys[i].i == k.i;
			return i;
		}
		case AK_TM_U64: {
			const ak_val *b = keys;
			uint32_t len = n;
			while (len > 1) {
				uint32_t half = len / 2;
				b = (uint64_t)b[half].i < (uint64_t)k.i ? b + half : b;
				len -= half;
			}
			uint32_t i = (uint32_t)(b - keys) + ((uint64_t)b->i < (uint64_t)k.i);
			*found = i < n && keys[i].i == k.i;
			return i;
		}
		case AK_TM_STR: {
			// One comparison a probe tells both ways and equality.
			uint32_t lo = 0, hi = n;
			bool eq = false;
			while (lo < hi) {
				uint32_t mid = (lo + hi) / 2;
				int c = ak_str_cmp(keys[mid].p, k.p);
				if (c < 0) {
					lo = mid + 1;
				} else {
					hi = mid;
					eq = c == 0;
				}
			}
			*found = lo < n && eq;
			return lo;
		}
		default:
			break;
		}
	}
	uint32_t lo = 0, hi = n;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		if (lt(o, keys[mid], k)) lo = mid + 1;
		else hi = mid;
	}
	*found = lo < n && !lt(o, k, keys[lo]);
	return lo;
}

// upper is the first of a node's n keys greater than k.
static uint32_t upper(ak_tree_order o, const ak_val *keys, uint32_t n, ak_val k) {
	uint32_t lo = 0, hi = n;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		if (!lt(o, k, keys[mid])) lo = mid + 1;
		else hi = mid;
	}
	return lo;
}

// ---- Finding ----

bool ak_tree_find(const ak_tree *t, ak_tree_order o, ak_val key, const ak_tnode **at, uint32_t *i) {
	const ak_tnode *n = root_of(t);
	while (n) {
		bool found;
		uint32_t j = lower(o, ak_tnode_keys(n), ak_tnode_len(n), key, &found);
		if (found) {
			*at = n;
			*i = j;
			return true;
		}
		if (ak_tnode_leaf(n)) break;
		n = ak_tnode_kid(n, j);
	}
	return false;
}

bool ak_tree_near(const ak_tree *t, ak_tree_order o, ak_val key, bool below, bool equal, const ak_tnode **at, uint32_t *i) {
	const ak_tnode *n = root_of(t), *best = NULL;
	uint32_t bi = 0;
	while (n) {
		bool found;
		uint32_t len = ak_tnode_len(n);
		uint32_t j = lower(o, ak_tnode_keys(n), len, key, &found);
		if (ak_error_pending()) return false;
		if (found && equal) {
			*at = n;
			*i = j;
			return true;
		}
		// below: the keys before j are less than key; else those from c
		// on are greater. The child between holds the nearer ones.
		uint32_t c = below || !found ? j : j + 1;
		if (below && j > 0) {
			best = n;
			bi = j - 1;
		} else if (!below && c < len) {
			best = n;
			bi = c;
		}
		if (ak_tnode_leaf(n)) break;
		n = ak_tnode_kid(n, c);
	}
	*at = best;
	*i = bi;
	return best != NULL;
}

bool ak_tree_end(const ak_tree *t, bool last, const ak_tnode **at, uint32_t *i) {
	const ak_tnode *n = root_of(t);
	if (!n) return false;
	while (!ak_tnode_leaf(n)) n = ak_tnode_kid(n, last ? ak_tnode_len(n) : 0);
	*at = n;
	*i = last ? ak_tnode_len(n) - 1 : 0;
	return true;
}

// ---- Inserting ----

// split makes p's i'th child, full, two, its middle key coming up into p
// (unique, not full) at i.
static void split(const ak_tree *t, ak_tnode *p, uint32_t i) {
	ak_tnode *c = node_mut(&kidv(p)[i]);
	bool leaf = ak_tnode_leaf(c), set = ak_tree_is_set(t);
	ak_tnode *r = node_new(t, leaf);
	const uint32_t m = AK_TREE_MIN, len = ak_tnode_len(c), rn = len - m - 1;
	ak_val *ck = ak_tnode_keys(c), *cv = ak_tnode_vals(c);
	memcpy(ak_tnode_keys(r), ck + m + 1, sizeof(ak_val) * rn);
	ak_val mk = ck[m], mv = {0};
	memset(ck + m, 0, sizeof(ak_val) * (len - m));
	if (!set) {
		memcpy(ak_tnode_vals(r), cv + m + 1, sizeof(ak_val) * rn);
		mv = cv[m];
		memset(cv + m, 0, sizeof(ak_val) * (len - m));
	}
	if (!leaf) {
		ak_val *cc = kidv(c);
		memcpy(kidv(r), cc + m + 1, sizeof(ak_val) * (rn + 1));
		memset(cc + m + 1, 0, sizeof(ak_val) * (len - m));
	}
	set_len(c, m);
	set_len(r, rn);
	uint32_t pn = ak_tnode_len(p);
	ins(ak_tnode_keys(p), pn, i, mk);
	if (!set) ins(ak_tnode_vals(p), pn, i, mv);
	ins(kidv(p), pn + 1, i + 1, (ak_val){.p = r});
	set_len(p, pn + 1);
}

static inline void store(ak_val *slot, ak_val v, ak_kind k) {
	ak_retain_val(v, k);
	ak_release_val(*slot, k);
	*slot = v;
}

// grow gives *t a root with room: a new one above a full one, split.
static ak_tnode *grow(ak_tree *t) {
	ak_tnode *root = node_mut(&t->root);
	if (ak_tnode_len(root) < AK_TREE_MAX) return root;
	ak_tnode *up = node_new(t, false);
	kidv(up)[0].p = root;
	t->root.p = up;
	t->height++;
	split(t, up, 0);
	return up;
}

bool ak_tree_put(ak_tree *t, ak_tree_order o, ak_val key, ak_val v) {
	ak_kind kk = (ak_kind)t->kk, vk = (ak_kind)t->vk;
	bool set = ak_tree_is_set(t);
	if (!t->root.p) {
		ak_tnode *n = node_new(t, true);
		ak_retain_val(key, kk);
		ak_tnode_keys(n)[0] = key;
		if (!set) {
			ak_retain_val(v, vk);
			ak_tnode_vals(n)[0] = v;
		}
		set_len(n, 1);
		t->root.p = n;
		t->count = 1;
		return true;
	}
	ak_tnode *n = grow(t);
	for (;;) {
		bool found;
		uint32_t len = ak_tnode_len(n);
		uint32_t i = lower(o, ak_tnode_keys(n), len, key, &found);
		if (AK_UNLIKELY(o.by != NULL || t->mode == AK_TM_CMP) && ak_error_pending()) return false;
		if (found) {
			// Its value changes; the key stays the one it has.
			if (!set) store(&ak_tnode_vals(n)[i], v, vk);
			return false;
		}
		if (ak_tnode_leaf(n)) {
			ak_retain_val(key, kk);
			ins(ak_tnode_keys(n), len, i, key);
			if (!set) {
				ak_retain_val(v, vk);
				ins(ak_tnode_vals(n), len, i, v);
			}
			set_len(n, len + 1);
			t->count++;
			return true;
		}
		ak_val *kids = kidv(n);
		if (ak_tnode_len(kids[i].p) == AK_TREE_MAX) {
			split(t, n, i);
			// The middle key came up to i: key goes left of it, right of
			// it, or is it.
			ak_val mid = ak_tnode_keys(n)[i];
			if (lt(o, mid, key)) {
				i++;
			} else if (!lt(o, key, mid)) {
				if (ak_error_pending()) return false;
				if (!set) store(&ak_tnode_vals(n)[i], v, vk);
				return false;
			}
			if (AK_UNLIKELY(o.by != NULL || t->mode == AK_TM_CMP) && ak_error_pending()) return false;
		}
		n = node_mut(&kids[i]);
	}
}

void ak_tree_append(ak_tree *t, ak_val key, ak_val v) {
	bool set = ak_tree_is_set(t);
	if (!t->root.p) t->root.p = node_new(t, true);
	ak_tnode *n = grow(t);
	for (;;) {
		uint32_t len = ak_tnode_len(n);
		if (ak_tnode_leaf(n)) {
			ak_tnode_keys(n)[len] = key;
			if (!set) ak_tnode_vals(n)[len] = v;
			set_len(n, len + 1);
			t->count++;
			return;
		}
		ak_val *kids = kidv(n);
		if (ak_tnode_len(kids[len].p) == AK_TREE_MAX) {
			split(t, n, len);
			len++;
		}
		n = node_mut(&kids[len]);
	}
}

// ---- Removing ----

// merge makes p's i'th child, its i'th key and its i + 1'th child one
// child.
static void merge(const ak_tree *t, ak_tnode *p, uint32_t i) {
	bool set = ak_tree_is_set(t);
	ak_val *kids = kidv(p);
	ak_tnode *l = node_mut(&kids[i]), *r = node_mut(&kids[i + 1]);
	uint32_t pn = ak_tnode_len(p), ln = ak_tnode_len(l), rn = ak_tnode_len(r);
	ak_val *lk = ak_tnode_keys(l), *rk = ak_tnode_keys(r);
	lk[ln] = del(ak_tnode_keys(p), pn, i);
	memcpy(lk + ln + 1, rk, sizeof(ak_val) * rn);
	memset(rk, 0, sizeof(ak_val) * rn);
	if (!set) {
		ak_val *lv = ak_tnode_vals(l), *rv = ak_tnode_vals(r);
		lv[ln] = del(ak_tnode_vals(p), pn, i);
		memcpy(lv + ln + 1, rv, sizeof(ak_val) * rn);
		memset(rv, 0, sizeof(ak_val) * rn);
	}
	if (!ak_tnode_leaf(l)) {
		memcpy(kidv(l) + ln + 1, kidv(r), sizeof(ak_val) * (rn + 1));
		memset(kidv(r), 0, sizeof(ak_val) * (rn + 1));
	}
	set_len(l, ln + 1 + rn);
	set_len(r, 0);
	del(kidv(p), pn + 1, i + 1);
	set_len(p, pn - 1);
	ak_release(r);  // empty: nothing of what it held was its
}

// fill gives p's i'th child more than the least keys, from a sibling or
// merged with one, and makes it unique: where it is then.
static uint32_t fill(const ak_tree *t, ak_tnode *p, uint32_t i) {
	bool set = ak_tree_is_set(t);
	ak_val *kids = kidv(p);
	uint32_t pn = ak_tnode_len(p);
	ak_tnode *c = node_mut(&kids[i]);
	uint32_t cn = ak_tnode_len(c);
	if (cn > AK_TREE_MIN) return i;
	if (i > 0 && ak_tnode_len(kids[i - 1].p) > AK_TREE_MIN) {
		// The separator down to its start, the left sibling's last up.
		ak_tnode *l = node_mut(&kids[i - 1]);
		uint32_t ln = ak_tnode_len(l);
		ins(ak_tnode_keys(c), cn, 0, ak_tnode_keys(p)[i - 1]);
		ak_tnode_keys(p)[i - 1] = del(ak_tnode_keys(l), ln, ln - 1);
		if (!set) {
			ins(ak_tnode_vals(c), cn, 0, ak_tnode_vals(p)[i - 1]);
			ak_tnode_vals(p)[i - 1] = del(ak_tnode_vals(l), ln, ln - 1);
		}
		if (!ak_tnode_leaf(c)) ins(kidv(c), cn + 1, 0, del(kidv(l), ln + 1, ln));
		set_len(l, ln - 1);
		set_len(c, cn + 1);
		return i;
	}
	if (i < pn && ak_tnode_len(kids[i + 1].p) > AK_TREE_MIN) {
		// The separator down to its end, the right sibling's first up.
		ak_tnode *r = node_mut(&kids[i + 1]);
		uint32_t rn = ak_tnode_len(r);
		ak_tnode_keys(c)[cn] = ak_tnode_keys(p)[i];
		ak_tnode_keys(p)[i] = del(ak_tnode_keys(r), rn, 0);
		if (!set) {
			ak_tnode_vals(c)[cn] = ak_tnode_vals(p)[i];
			ak_tnode_vals(p)[i] = del(ak_tnode_vals(r), rn, 0);
		}
		if (!ak_tnode_leaf(c)) kidv(c)[cn + 1] = del(kidv(r), rn + 1, 0);
		set_len(r, rn - 1);
		set_len(c, cn + 1);
		return i;
	}
	if (i < pn) {
		merge(t, p, i);
		return i;
	}
	merge(t, p, i - 1);
	return i - 1;
}

// take_end takes the least entry (last: the greatest) out of n's
// subtree, n unique with more than the least keys (or the root): its key
// and value, references and all.
static void take_end(const ak_tree *t, ak_tnode *n, bool last, ak_val *k, ak_val *v) {
	bool set = ak_tree_is_set(t);
	for (;;) {
		uint32_t len = ak_tnode_len(n);
		if (ak_tnode_leaf(n)) {
			uint32_t i = last ? len - 1 : 0;
			*k = del(ak_tnode_keys(n), len, i);
			if (!set) *v = del(ak_tnode_vals(n), len, i);
			set_len(n, len - 1);
			return;
		}
		uint32_t j = fill(t, n, last ? len : 0);
		n = ak_tnode_kid(n, j);
	}
}

// remove takes key out of n's subtree (n unique with more than the least
// keys, or the root): whether it was there, its key and value (their
// references) in *k and *v.
static bool remove_from(const ak_tree *t, ak_tree_order o, ak_tnode *n, ak_val key, ak_val *k, ak_val *v) {
	bool set = ak_tree_is_set(t);
	for (;;) {
		bool found;
		uint32_t len = ak_tnode_len(n);
		uint32_t i = lower(o, ak_tnode_keys(n), len, key, &found);
		if (ak_error_pending()) return false;
		if (ak_tnode_leaf(n)) {
			if (!found) return false;
			*k = del(ak_tnode_keys(n), len, i);
			if (!set) *v = del(ak_tnode_vals(n), len, i);
			set_len(n, len - 1);
			return true;
		}
		ak_val *kids = kidv(n);
		if (found) {
			// Its place takes the greatest before it or the least after
			// it, from a child that can spare one, else the two children
			// and it merge and it's taken out of that.
			ak_val *ks = ak_tnode_keys(n), *vs = set ? NULL : ak_tnode_vals(n);
			if (ak_tnode_len(kids[i].p) > AK_TREE_MIN || ak_tnode_len(kids[i + 1].p) > AK_TREE_MIN) {
				bool before = ak_tnode_len(kids[i].p) > AK_TREE_MIN;
				ak_tnode *c = node_mut(&kids[before ? i : i + 1]);
				*k = ks[i];
				if (vs) *v = vs[i];
				ak_val nk, nv = {0};
				take_end(t, c, before, &nk, &nv);
				ks[i] = nk;
				if (vs) vs[i] = nv;
				return true;
			}
			merge(t, n, i);
			n = kids[i].p;
			continue;
		}
		uint32_t j = fill(t, n, i);
		n = kids[j].p;
	}
}

bool ak_tree_delete(ak_tree *t, ak_tree_order o, ak_val key, ak_val *out) {
	if (!t->root.p) return false;
	if (!ak_unique(t->root.p)) {
		// Shared: found first, so that a miss copies nothing.
		const ak_tnode *at;
		uint32_t i;
		if (!ak_tree_find(t, o, key, &at, &i)) return false;
	}
	ak_tnode *root = node_mut(&t->root);
	ak_val k = {0}, v = {0};
	bool gone = remove_from(t, o, root, key, &k, &v);
	if (ak_tnode_len(root) == 0) {
		if (ak_tnode_leaf(root)) {
			t->root.p = NULL;
		} else {
			t->root = kidv(root)[0];
			kidv(root)[0].p = NULL;
			t->height--;
		}
		ak_release(root);
	}
	if (gone) {
		t->count--;
		ak_release_val(k, (ak_kind)t->kk);
		if (out) *out = v;
		else if (!ak_tree_is_set(t)) ak_release_val(v, (ak_kind)t->vk);
	}
	return gone;
}

// ---- Walking in order ----

void ak_titer_start(ak_titer *it, const ak_tree *t) {
	it->top = -1;
	for (const ak_tnode *n = root_of(t); n; n = ak_tnode_kid(n, 0)) {
		it->top++;
		it->n[it->top] = n;
		it->i[it->top] = 0;
		if (ak_tnode_leaf(n)) break;
	}
}

void ak_titer_seek(ak_titer *it, const ak_tree *t, ak_tree_order o, ak_val from, bool after) {
	it->top = -1;
	for (const ak_tnode *n = root_of(t); n;) {
		uint32_t len = ak_tnode_len(n), i;
		if (after) {
			i = upper(o, ak_tnode_keys(n), len, from);
		} else {
			bool found;
			i = lower(o, ak_tnode_keys(n), len, from, &found);
		}
		if (ak_error_pending()) {
			it->top = -1;
			return;
		}
		it->top++;
		it->n[it->top] = n;
		it->i[it->top] = i;
		if (ak_tnode_leaf(n)) break;
		n = ak_tnode_kid(n, i);
	}
}

bool ak_titer_next(ak_titer *it, const ak_tnode **at, uint32_t *idx) {
	while (it->top >= 0) {
		const ak_tnode *n = it->n[it->top];
		uint32_t i = it->i[it->top];
		if (i >= ak_tnode_len(n)) {
			it->top--;
			continue;
		}
		*at = n;
		*idx = i;
		it->i[it->top] = i + 1;
		if (!ak_tnode_leaf(n)) {
			// The key at i, then the child after it, from its least.
			for (const ak_tnode *c = ak_tnode_kid(n, i + 1); c; c = ak_tnode_kid(c, 0)) {
				it->top++;
				it->n[it->top] = c;
				it->i[it->top] = 0;
				if (ak_tnode_leaf(c)) break;
			}
		}
		return true;
	}
	return false;
}

// ---- Equality and hashing ----

bool ak_tree_eq(const ak_tree *a, const ak_tree *b) {
	if (a == b) return true;
	if (a->count != b->count || a->kk != b->kk || a->vk != b->vk) return false;
	bool set = ak_tree_is_set(a);
	ak_titer x, y;
	ak_titer_start(&x, a);
	ak_titer_start(&y, b);
	const ak_tnode *n, *m;
	uint32_t i, j;
	while (ak_titer_next(&x, &n, &i)) {
		if (!ak_titer_next(&y, &m, &j)) return false;
		if (!ak_val_eq(ak_tnode_keys(n)[i], ak_tnode_keys(m)[j], (ak_kind)a->kk)) return false;
		if (!set && !ak_val_eq(ak_tnode_vals(n)[i], ak_tnode_vals(m)[j], (ak_kind)a->vk)) return false;
	}
	return true;
}

uint64_t ak_tree_hash(const ak_tree *t) {
	uint64_t h = (uint64_t)t->count * 0x9e3779b97f4a7c15ull;
	bool set = ak_tree_is_set(t);
	ak_titer it;
	ak_titer_start(&it, t);
	const ak_tnode *n;
	uint32_t i;
	while (ak_titer_next(&it, &n, &i)) {
		h = (h ^ ak_val_hash(ak_tnode_keys(n)[i], (ak_kind)t->kk)) * 0xff51afd7ed558ccdull;
		if (!set) h = (h ^ ak_val_hash(ak_tnode_vals(n)[i], (ak_kind)t->vk)) * 0xff51afd7ed558ccdull;
	}
	return h ^ (h >> 33);
}

// ---- Builtins: BTree<K, V>'s methods (lang/check/methods.go's "tree") ----
//
// The receiver first; one that changes it (put, add, take, delete) is
// given its slot, which it makes unique (ak_tree_mut), as an array's
// push is. by, a less-than closure or none, orders the keys when it's
// given; a set's tree (collections.newSet) has no values.

#define TREE(i) ((ak_tree *)args[i].p)

static inline ak_tree_order order_of(const ak_tree *t, ak_val by) { return (ak_tree_order){.mode = t->mode, .by = by.p, .kshape = t->kshape}; }

// raised reports an error a comparison raised (a by's, a compare's):
// what a builtin returns then is none, which its caller doesn't keep.
static inline bool raised(ak_tree_order o) { return AK_UNLIKELY(o.by != NULL || o.mode == AK_TM_CMP) && ak_error_pending(); }

// boxed is v of kind k as an optional's value: a scalar boxed.
static inline ak_val boxed(ak_val v, ak_kind k) {
	if (k == AK_K_REF) return (ak_val){.p = ak_retain(v.p)};
	return (ak_val){.p = ak_box_new(k, v)};
}

// unboxed is an optional's value of kind k (o not none), borrowed.
static inline ak_val unboxed(ak_val o, ak_kind k) { return k == AK_K_REF ? o : ((ak_box *)o.p)->v; }

// entry is (k, v): a tuple of proto's elements' shape.
static ak_val entry(const ak_arr *proto, ak_val k, ak_val v) {
	const ak_shape *s = proto->shape;
	ak_struct *e = ak_struct_new(s);
	e->f[0] = k;
	e->f[1] = v;
	ak_retain_val(k, (ak_kind)s->kinds[0]);
	ak_retain_val(v, (ak_kind)s->kinds[1]);
	return (ak_val){.p = e};
}

// tree.count(t) -> int
static ak_val b_count(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.i = TREE(0)->count};
}

// tree.get(t, key, by) -> V?
static ak_val b_get(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_tree *t = TREE(0);
	ak_tree_order o = order_of(t, args[2]);
	const ak_tnode *at;
	uint32_t i;
	if (!ak_tree_find(t, o, args[1], &at, &i) || raised(o)) return (ak_val){0};
	return boxed(ak_tnode_vals(at)[i], (ak_kind)t->vk);
}

// tree.has(t, key, by) -> bool
static ak_val b_has(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_tree *t = TREE(0);
	const ak_tnode *at;
	uint32_t i;
	return (ak_val){.i = ak_tree_find(t, order_of(t, args[2]), args[1], &at, &i)};
}

// tree.put(t, key, value, by) -> bool
static ak_val b_put(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_tree *t = ak_tree_mut((ak_tree **)&args[0].p);
	return (ak_val){.i = ak_tree_put(t, order_of(t, args[3]), args[1], args[2])};
}

// tree.add(t, key, by) -> bool
static ak_val b_add(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_tree *t = ak_tree_mut((ak_tree **)&args[0].p);
	return (ak_val){.i = ak_tree_put(t, order_of(t, args[2]), args[1], (ak_val){0})};
}

// removed takes key out of the tree in *slot: whether it was there, its
// value in *v (NULL: let go of). A shared tree that hasn't key isn't
// copied.
static bool removed(ak_val *slot, ak_val key, ak_val by, ak_val *v) {
	ak_tree *t = slot->p;
	ak_tree_order o = order_of(t, by);
	if (!ak_unique(t) || (t->root.p && !ak_unique(t->root.p))) {
		const ak_tnode *at;
		uint32_t i;
		if (!ak_tree_find(t, o, key, &at, &i)) return false;
	}
	return ak_tree_delete(ak_tree_mut((ak_tree **)&slot->p), o, key, v);
}

// tree.take(t, key, by) -> V?
static ak_val b_take(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_val v;
	if (!removed(&args[0], args[1], args[2], &v)) return (ak_val){0};
	ak_kind vk = (ak_kind)TREE(0)->vk;
	if (raised(order_of(TREE(0), args[2]))) {
		ak_release_val(v, vk);
		return (ak_val){0};
	}
	if (vk == AK_K_REF) return v;
	return (ak_val){.p = ak_box_new(vk, v)};
}

// tree.delete(t, key, by) -> bool
static ak_val b_delete(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.i = removed(&args[0], args[1], args[2], NULL)};
}

// found is what near and end found: the entry, as a tuple of proto's
// elements' shape, or (proto NULL) its key; none when there's none.
static ak_val found(const ak_tree *t, bool ok, const ak_tnode *at, uint32_t i, const ak_arr *proto) {
	if (!ok) return (ak_val){0};
	if (!proto) return boxed(ak_tnode_keys(at)[i], (ak_kind)t->kk);
	return entry(proto, ak_tnode_keys(at)[i], ak_tnode_vals(at)[i]);
}

// tree.near(t, key, by, below, equal, entryProto) -> (K, V)?
// tree.near_key(t, key, by, below, equal) -> K?
static ak_val b_near(ak_val *args, uint32_t n) {
	ak_tree *t = TREE(0);
	ak_tree_order o = order_of(t, args[2]);
	const ak_tnode *at = NULL;
	uint32_t i = 0;
	bool ok = ak_tree_near(t, o, args[1], args[3].i, args[4].i, &at, &i) && !raised(o);
	return found(t, ok, at, i, n > 5 ? args[5].p : NULL);
}

// tree.end(t, last, entryProto) -> (K, V)?
// tree.end_key(t, last) -> K?
static ak_val b_end(ak_val *args, uint32_t n) {
	ak_tree *t = TREE(0);
	const ak_tnode *at = NULL;
	uint32_t i = 0;
	bool ok = ak_tree_end(t, args[1].i, &at, &i);
	return found(t, ok, at, i, n > 2 ? args[2].p : NULL);
}

// What tree.range makes of each entry.
enum { RANGE_ENTRIES, RANGE_KEYS, RANGE_VALUES };

// tree.range(t, from, after, to, by, limit, proto, what) -> [X]: the
// entries (what: their keys,
// their values) from the first at or after from (after: past it; none:
// the first), before to (none: to the end), limit at most (negative:
// all), in an array like proto (an empty one of the result's type).
static ak_val b_range(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_tree *t = TREE(0);
	int64_t limit = args[5].i, what = args[7].i;
	ak_arr *a = ak_arr_like(args[6].p, (uint32_t)(limit >= 0 && limit < t->count ? limit : t->count));
	if (t->count == 0 || limit == 0) return (ak_val){.p = a};
	ak_kind kk = (ak_kind)t->kk, vk = (ak_kind)t->vk;
	ak_tree_order o = order_of(t, args[4]);
	ak_titer it;
	if (args[1].p) ak_titer_seek(&it, t, o, unboxed(args[1], kk), args[2].i);
	else ak_titer_start(&it, t);
	bool to = args[3].p != NULL;
	ak_val end = to ? unboxed(args[3], kk) : (ak_val){0};
	const ak_tnode *at;
	uint32_t i;
	while ((limit < 0 || a->len < limit) && ak_titer_next(&it, &at, &i)) {
		ak_val k = ak_tnode_keys(at)[i];
		if (to && !lt(o, k, end)) break;
		if (raised(o)) break;
		ak_val x;
		switch (what) {
		case RANGE_KEYS:
			x = k;
			ak_retain_val(x, kk);
			break;
		case RANGE_VALUES:
			x = ak_tnode_vals(at)[i];
			ak_retain_val(x, vk);
			break;
		default:
			x = entry(a, k, ak_tnode_vals(at)[i]);
			break;
		}
		ak_arr_push(&a, x);
	}
	if (raised(o)) {
		ak_release(a);
		return (ak_val){0};
	}
	if (ak_arr_kind(a) == AK_K_REF && a->len && ((ak_obj *)ak_arr_vals(a)[0].p)->tag == AK_TAG_STR) a->h.flags = 1;  // strings: compared as strings
	return (ak_val){.p = a};
}

const ak_lib_entry ak_tree_lib[] = {
	{"tree.count", b_count},
	{"tree.get", b_get},
	{"tree.has", b_has},
	{"tree.put", b_put},
	{"tree.add", b_add},
	{"tree.take", b_take},
	{"tree.delete", b_delete},
	{"tree.near", b_near},
	{"tree.near_key", b_near},
	{"tree.end", b_end},
	{"tree.end_key", b_end},
	{"tree.range", b_range},
	{NULL, NULL},
};
