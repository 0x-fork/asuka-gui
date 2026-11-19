#include "rt/rt.h"

// An insertion-ordered hash map: entries in arrays (keys, vals, dead),
// in the order they were made, and an open-addressing index of them, as
// Python's compact dict and Rust's indexmap lay theirs out. Each slot of
// the index holds an entry's number and its key's hash (32 bits), so a
// probe passes the slots of other keys without reading their entries,
// and growing rebuilds the index from the slots, hashing nothing.
// Deleting marks the entry dead and its slot a tombstone, which probes
// pass; growing compacts both away.

#define EMPTY 0ull
#define TOMB 0xffffffffull  // a deleted entry's slot: its number is all ones

static inline uint64_t slot_of(uint32_t e, uint32_t h) { return (uint64_t)h << 32 | (e + 1); }
static inline uint32_t slot_entry(uint64_t s) { return (uint32_t)s - 1; }
static inline uint32_t slot_hash(uint64_t s) { return (uint32_t)(s >> 32); }
static inline bool slot_live(uint64_t s) { return (uint32_t)s != 0 && (uint32_t)s != (uint32_t)TOMB; }

static ak_kind key_kind(const ak_map *m) { return (ak_kind)(m->h.aux & 0xf); }
static ak_kind val_kind(const ak_map *m) { return (ak_kind)(m->h.aux >> 4); }

ak_map *ak_map_new(ak_kind key, bool string_keys, ak_kind val, const ak_shape *vshape) {
	ak_map *m = ak_obj_new(AK_TAG_MAP, sizeof(ak_map));
	m->h.aux = (uint16_t)(key | val << 4);
	m->h.flags = string_keys ? 1 : 0;
	m->vshape = vshape;
	return m;
}

static inline bool is_str(ak_val v) { return v.p && ((const ak_obj *)v.p)->tag == AK_TAG_STR; }

// The key's hash, 32 bits of it: a string's own (cached in it, which is
// what ak_val_hash gives for one), anything else's by ak_val_hash,
// folded. (AK_MAP_STRKEYS isn't trusted here: a set of any references
// has it.)
static inline uint32_t hash_key(const ak_map *m, ak_val k) {
	if (key_kind(m) == AK_K_REF && is_str(k)) return ak_str_hash(k.p);
	uint64_t h = ak_val_hash(k, key_kind(m));
	return (uint32_t)(h ^ h >> 32);
}

static inline bool key_eq(const ak_map *m, ak_val a, ak_val b) {
	switch (key_kind(m)) {
	case AK_K_INT: return a.i == b.i;
	case AK_K_REF:
		if (a.p == b.p) return true;
		if (is_str(a) && is_str(b)) return ak_str_eq(a.p, b.p);
		return ak_val_eq(a, b, AK_K_REF);
	default: return ak_val_eq(a, b, key_kind(m));
	}
}

// probe finds key's slot (its index, or -1), and, when insert isn't
// NULL, where it would go: the first tombstone or empty slot on its way.
static int64_t probe(const ak_map *m, ak_val key, uint32_t h, int64_t *insert) {
	if (insert) *insert = -1;
	if (!m->nslots) return -1;
	uint32_t mask = m->nslots - 1;
	for (uint32_t i = h & mask;; i = (i + 1) & mask) {
		uint64_t s = m->slots[i];
		if (s == EMPTY) {
			if (insert && *insert < 0) *insert = i;
			return -1;
		}
		if ((uint32_t)s == (uint32_t)TOMB) {
			if (insert && *insert < 0) *insert = i;
			continue;
		}
		if (slot_hash(s) == h && key_eq(m, m->keys[slot_entry(s)], key)) return i;
	}
}

static void place(ak_map *m, uint32_t e, uint32_t h) {
	uint32_t mask = m->nslots - 1, i = h & mask;
	while (m->slots[i] != EMPTY) i = (i + 1) & mask;
	m->slots[i] = slot_of(e, h);
}

// reindex compacts dead entries away and rebuilds the index with nslots
// (a power of 2) slots, from the old one's hashes.
static void reindex(ak_map *m, uint32_t nslots) {
	uint64_t *old = m->slots;
	uint32_t nold = m->nslots;
	m->slots = ak_alloc(sizeof(uint64_t) * nslots);
	m->nslots = nslots;
	if (m->live == m->len) {
		// No dead entries: the entries stay where they are.
		for (uint32_t i = 0; i < nold; i++)
			if (slot_live(old[i])) place(m, slot_entry(old[i]), slot_hash(old[i]));
	} else {
		// Each live entry's new number: the live ones before it.
		uint32_t *to = ak_alloc(sizeof(uint32_t) * (m->len ? m->len : 1));
		uint32_t j = 0;
		for (uint32_t i = 0; i < m->len; i++) {
			if (m->dead[i]) continue;
			to[i] = j;
			m->keys[j] = m->keys[i];
			m->vals[j] = m->vals[i];
			m->dead[j] = 0;
			j++;
		}
		for (uint32_t i = 0; i < nold; i++)
			if (slot_live(old[i])) place(m, to[slot_entry(old[i])], slot_hash(old[i]));
		m->len = j;
		ak_free(to);
	}
	ak_free(old);
}

// copy_of: a new map of m's live entries, retained, in order; its index
// m's when m has no dead entries.
static ak_map *copy_of(const ak_map *m) {
	ak_map *c = ak_map_new(key_kind(m), m->h.flags & AK_MAP_STRKEYS, val_kind(m), m->vshape);
	c->h.flags = m->h.flags;
	uint32_t n = m->live;
	if (!n) return c;
	c->cap = n;
	c->keys = ak_alloc(sizeof(ak_val) * n);
	c->vals = ak_alloc(sizeof(ak_val) * n);
	c->dead = ak_alloc(n);
	ak_kind kk = key_kind(m), vk = val_kind(m);
	uint32_t j = 0;
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		c->keys[j] = m->keys[i];
		c->vals[j] = m->vals[i];
		ak_retain_val(c->keys[j], kk);
		ak_retain_val(c->vals[j], vk);
		j++;
	}
	c->len = c->live = n;
	if (m->len == m->live) {
		c->nslots = m->nslots;
		c->slots = ak_alloc(sizeof(uint64_t) * m->nslots);
		memcpy(c->slots, m->slots, sizeof(uint64_t) * m->nslots);
	} else {
		uint32_t ns = 8;
		while (n * 4 >= ns * 3) ns *= 2;
		c->nslots = ns;
		c->slots = ak_alloc(sizeof(uint64_t) * ns);
		for (uint32_t i = 0; i < n; i++) place(c, i, hash_key(c, c->keys[i]));
	}
	return c;
}

ak_map *ak_map_mut(ak_map **mp) {
	ak_map *m = *mp;
	if (ak_unique(m)) return m;
	ak_map *c = copy_of(m);
	ak_release(m);
	*mp = c;
	return c;
}

bool ak_map_get(const ak_map *m, ak_val key, ak_val *out) {
	if (!m->live) return false;
	int64_t i = probe(m, key, hash_key(m, key), NULL);
	if (i < 0) return false;
	*out = m->vals[slot_entry(m->slots[i])];
	return true;
}

// put sets key's value (taking both references), or, when keep_old,
// keeps what key has (releasing key and v): whether key was new.
static bool put(ak_map **mp, ak_val key, ak_val v, bool keep_old) {
	ak_map *m = ak_map_mut(mp);
	uint32_t h = hash_key(m, key);
	int64_t at;
	int64_t i = probe(m, key, h, &at);
	if (i >= 0) {
		uint32_t e = slot_entry(m->slots[i]);
		if (keep_old) {
			ak_release_val(key, key_kind(m));
			ak_release_val(v, val_kind(m));
			return false;
		}
		ak_release_val(m->keys[e], key_kind(m));
		ak_release_val(m->vals[e], val_kind(m));
		m->keys[e] = key;
		m->vals[e] = v;
		return false;
	}
	if (m->len == m->cap) {
		uint32_t c = m->cap ? m->cap * 2 : 8;
		m->keys = ak_realloc(m->keys, sizeof(ak_val) * c);
		m->vals = ak_realloc(m->vals, sizeof(ak_val) * c);
		m->dead = ak_realloc(m->dead, c);
		m->cap = c;
	}
	if ((m->len + 1) * 4 >= m->nslots * 3) {
		uint32_t n = m->nslots ? m->nslots : 8;
		while ((m->live + 1) * 4 >= n * 3) n *= 2;
		reindex(m, n);
		at = -1;  // the index moved
	}
	uint32_t e = m->len++;
	m->keys[e] = key;
	m->vals[e] = v;
	m->dead[e] = 0;
	m->live++;
	if (at >= 0) m->slots[at] = slot_of(e, h);
	else place(m, e, h);
	return true;
}

void ak_map_set(ak_map **mp, ak_val key, ak_val v) { put(mp, key, v, false); }

bool ak_map_put_new(ak_map **mp, ak_val key, ak_val v) { return put(mp, key, v, true); }

bool ak_map_delete(ak_map **mp, ak_val key) {
	if (!(*mp)->live) return false;
	uint32_t h = hash_key(*mp, key);
	if (probe(*mp, key, h, NULL) < 0) return false;
	ak_map *m = ak_map_mut(mp);
	int64_t i = probe(m, key, h, NULL);
	uint32_t e = slot_entry(m->slots[i]);
	ak_release_val(m->keys[e], key_kind(m));
	ak_release_val(m->vals[e], val_kind(m));
	m->dead[e] = 1;
	m->live--;
	m->slots[i] = TOMB;
	return true;
}

ak_arr *ak_map_keys(const ak_map *m) {
	ak_arr *a = ak_arr_new(key_kind(m), NULL, m->live);
	a->h.flags = m->h.flags & 1;
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		ak_retain_val(m->keys[i], key_kind(m));
		ak_arr_put(a, a->len++, m->keys[i]);
	}
	return a;
}

ak_arr *ak_map_values(const ak_map *m) {
	ak_arr *a = ak_arr_new(val_kind(m), m->vshape, m->live);
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		ak_retain_val(m->vals[i], val_kind(m));
		ak_arr_put(a, a->len++, m->vals[i]);
	}
	return a;
}
