#include "rt/rt.h"

// An insertion-ordered hash map: entries in arrays (keys, vals, dead),
// an open-addressing index of entry numbers. Deleting marks an entry
// dead; growing compacts.

static ak_kind key_kind(const ak_map *m) { return (ak_kind)(m->h.aux & 0xf); }
static ak_kind val_kind(const ak_map *m) { return (ak_kind)(m->h.aux >> 4); }

ak_map *ak_map_new(ak_kind key, bool string_keys, ak_kind val, const ak_shape *vshape) {
	ak_map *m = ak_obj_new(AK_TAG_MAP, sizeof(ak_map));
	m->h.aux = (uint16_t)(key | val << 4);
	m->h.flags = string_keys ? 1 : 0;
	m->vshape = vshape;
	return m;
}

static uint64_t hash_key(const ak_map *m, ak_val k) { return ak_val_hash(k, key_kind(m)); }

static bool key_eq(const ak_map *m, ak_val a, ak_val b) { return ak_val_eq(a, b, key_kind(m)); }

static int64_t find(const ak_map *m, ak_val k) {
	if (!m->nslots) return -1;
	uint32_t mask = m->nslots - 1;
	for (uint32_t i = (uint32_t)hash_key(m, k) & mask;; i = (i + 1) & mask) {
		uint32_t s = m->slots[i];
		if (s == 0) return -1;
		if (!m->dead[s - 1] && key_eq(m, m->keys[s - 1], k)) return s - 1;
	}
}

static void reindex(ak_map *m, uint32_t nslots) {
	// Compact dead entries away, then rebuild the index.
	uint32_t j = 0;
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		m->keys[j] = m->keys[i];
		m->vals[j] = m->vals[i];
		m->dead[j] = 0;
		j++;
	}
	m->len = j;
	ak_free(m->slots);
	m->nslots = nslots;
	m->slots = ak_alloc(sizeof(uint32_t) * nslots);
	uint32_t mask = nslots - 1;
	for (uint32_t e = 0; e < m->len; e++) {
		uint32_t i = (uint32_t)hash_key(m, m->keys[e]) & mask;
		while (m->slots[i]) i = (i + 1) & mask;
		m->slots[i] = e + 1;
	}
}

ak_map *ak_map_mut(ak_map **mp) {
	ak_map *m = *mp;
	if (ak_unique(m)) return m;
	ak_map *c = ak_map_new(key_kind(m), m->h.flags & AK_MAP_STRKEYS, val_kind(m), m->vshape);
	c->h.flags = m->h.flags;
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		ak_retain_val(m->keys[i], key_kind(m));
		ak_retain_val(m->vals[i], val_kind(m));
		ak_map_set(&c, m->keys[i], m->vals[i]);
	}
	ak_release(m);
	*mp = c;
	return c;
}

bool ak_map_get(const ak_map *m, ak_val key, ak_val *out) {
	int64_t e = find(m, key);
	if (e < 0) return false;
	*out = m->vals[e];
	return true;
}

void ak_map_set(ak_map **mp, ak_val key, ak_val v) {
	ak_map *m = ak_map_mut(mp);
	int64_t e = find(m, key);
	if (e >= 0) {
		ak_release_val(m->keys[e], key_kind(m));
		ak_release_val(m->vals[e], val_kind(m));
		m->keys[e] = key;
		m->vals[e] = v;
		return;
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
	}
	e = m->len++;
	m->keys[e] = key;
	m->vals[e] = v;
	m->dead[e] = 0;
	m->live++;
	uint32_t mask = m->nslots - 1;
	uint32_t i = (uint32_t)hash_key(m, key) & mask;
	while (m->slots[i]) i = (i + 1) & mask;
	m->slots[i] = (uint32_t)e + 1;
}

bool ak_map_delete(ak_map **mp, ak_val key) {
	if (find(*mp, key) < 0) return false;
	ak_map *m = ak_map_mut(mp);
	int64_t e = find(m, key);
	ak_release_val(m->keys[e], key_kind(m));
	ak_release_val(m->vals[e], val_kind(m));
	m->dead[e] = 1;
	m->live--;
	// The slot keeps pointing at the dead entry (a tombstone) until the
	// next reindex.
	return true;
}

ak_arr *ak_map_keys(const ak_map *m) {
	ak_arr *a = ak_arr_new(key_kind(m), NULL, m->live);
	a->h.flags = m->h.flags & 1;
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		ak_retain_val(m->keys[i], key_kind(m));
		a->data[a->len++] = m->keys[i];
	}
	return a;
}

ak_arr *ak_map_values(const ak_map *m) {
	ak_arr *a = ak_arr_new(val_kind(m), m->vshape, m->live);
	for (uint32_t i = 0; i < m->len; i++) {
		if (m->dead[i]) continue;
		ak_retain_val(m->vals[i], val_kind(m));
		a->data[a->len++] = m->vals[i];
	}
	return a;
}
