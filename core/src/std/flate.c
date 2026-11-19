// flate.c: DEFLATE (RFC 1951), compressed and decompressed (flate.h).
//
// The compressor's levels are Go's compress/flate's (Go 1.27): 1 to 6
// Klaus Post's encoders (hash tables of the positions last seen, no
// chains; below), each 64 KB block encoded with its 32 KB history; 7 to
// 9 zlib's lazy matching over hash chains, with Go's refinements; 0
// stores. Matches are at least 4 bytes, compared 8 bytes at a time. Each
// block is written with whichever is smallest: its own Huffman codes,
// the fixed ones, stored, or (when matches saved little) its bytes as
// literals.
//
// The decompressor is libdeflate's design: canonical Huffman codes
// looked up in tables (11 bits for literals and lengths, 8 for
// distances, subtables past them), a 64-bit bit buffer refilled 8 bytes
// at a time, matches copied 8 or 16 bytes at a time into an output
// buffer with room to spare. Near the input's or the output's end it
// goes a symbol at a time, checking each, and stops between symbols for
// more input or room: so it streams, and the caller decides where the
// output goes (a growing buffer whole; a window it empties).
#include "std/flate.h"

#include "base/base.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__wasm__)
#include <pthread.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ctz64(x) ((unsigned)__builtin_ctzll(x))
#else
static unsigned ctz64(uint64_t x) {
	unsigned n = 0;
	while (!(x & 1)) x >>= 1, n++;
	return n;
}
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline uint64_t load64(const uint8_t *p) {
	uint64_t v = 0;
	for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
	return v;
}
static inline uint32_t load32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void store64(uint8_t *p, uint64_t v) {
	for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
#else
static inline uint64_t load64(const uint8_t *p) {
	uint64_t v;
	memcpy(&v, p, 8);
	return v;
}
static inline uint32_t load32(const uint8_t *p) {
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}
static inline void store64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }
#endif

// The length codes (257 + i) and the distance codes: their bases and
// extra bits (RFC 1951 3.2.5).
static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                      35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                       257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// The order the code lengths' code's lengths come in (3.2.7).
static const uint8_t cl_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static inline uint32_t reverse(uint32_t code, unsigned len) {
	uint32_t r = 0;
	for (unsigned i = 0; i < len; i++, code >>= 1) r = r << 1 | (code & 1);
	return r;
}

// ============================================================================
// Compressing
// ============================================================================

enum {
	WSIZE = 32768,
	WMASK = WSIZE - 1,
	WBUF = 8 * WSIZE,     // the window's buffer: 32 KB of history, the rest input
	WPAD = 16,            // read past the end by match_len's 8-byte compares
	MIN_MATCH = 4,
	MAX_MATCH = 258,
	LOOK = MAX_MATCH + 4,  // what a position needs ahead of it, unless the input has ended
	BLK = 65535,           // levels 1 to 6: a block's input
	SYMS = 65536,          // a block's symbols, at most
	LAZY_SYMS = 32768,     // levels 7 to 9: a block's symbols
	LAZY_HBITS = 17,
	EMPTY = -0x40000000,   // a hash table's entry for no position
};

enum { M_STORE, M_FAST, M_LAZY };

// Levels 7 to 9's search, as Go's (zlib's, with shorter chains): a
// match this good cuts the next search's chain to a quarter; one this
// long isn't followed by a lazy search; one this long ends the search;
// at most this many candidates.
static const struct {
	uint16_t good, lazy, nice, chain;
} lazy_levels[3] = {
	{8, 12, 16, 24},
	{16, 30, 40, 64},
	{32, 258, 258, 1024},
};

// Levels 1 to 6's tables: bits, and whether each entry keeps the
// position before it too.
static const struct {
	uint8_t bits1, bits2;
	bool pair1, pair2;
} fast_tables[7] = {
	{0, 0, false, false},
	{15, 0, false, false},
	{17, 0, false, false},
	{16, 0, true, false},
	{15, 15, false, false},
	{15, 15, false, true},
	{15, 15, false, true},
};

struct ak_deflater {
	int level, mode;
	uint32_t good, lazy, nice, chain;
	ak_fl_put put;
	void *user;
	bool ok;
	uint8_t *win;               // WBUF + WPAD bytes
	uint32_t pos, end;          // the next position to look at; the input's end in win
	uint32_t block_start;       // where the block being gathered starts
	uint32_t sym_end;           // where its symbols reach
	int32_t *t1, *t2;           // levels 1 to 6: their tables (positions in win)
	uint32_t n1, n2;            // their entries
	int32_t *head, *prev;       // levels 7 to 9: the hash chains
	uint32_t prev_len, prev_dist;
	bool avail;                 // lazy: the byte before pos isn't emitted yet
	uint32_t lit_run;           // lazy: literals since the last match
	uint8_t *lit_bits;          // level 9: each byte's literal code's length, the input ahead's
	uint32_t *syms;             // a literal (< 256), or a match: dist << 9 | len
	uint32_t nsyms;
	uint32_t lfreq[288], dfreq[32];
	uint8_t *out;               // a block's bytes, as they're written
	size_t out_cap;
	uint64_t bits;              // bits written not yet in out
	unsigned nbits;
	uint8_t len_code[MAX_MATCH + 1];  // a length's code, 0 to 28
	uint8_t dist_lo[256], dist_hi[256];
};

static inline unsigned dist_code(const ak_deflater *d, uint32_t dist) {
	return dist <= 256 ? d->dist_lo[dist - 1] : d->dist_hi[(dist - 1) >> 7];
}

// The hashes of a word's first 4, 5 and 7 bytes, to b bits.
static inline uint32_t h4(uint64_t u, unsigned b) { return ((uint32_t)u * 2654435761u) >> (32 - b); }
static inline uint32_t h5(uint64_t u, unsigned b) { return (uint32_t)(((u << 24) * 889523592379ull) >> (64 - b)); }
static inline uint32_t h7(uint64_t u, unsigned b) { return (uint32_t)(((u << 8) * 58295818150454627ull) >> (64 - b)); }

static int32_t *table(uint32_t n) {
	int32_t *t = ak_alloc(sizeof(int32_t) * n);
	for (uint32_t i = 0; i < n; i++) t[i] = EMPTY;
	return t;
}

// ---- A spare compressor a thread ----
//
// A compressor's buffers are a megabyte (its window, its tables): an
// archive of many small files would make and drop one a file, each
// time the system's to map and unmap. The last one freed is kept for
// the thread (a level's next one reuses it, its tables emptied), and
// goes with the thread.

#if !defined(__wasm__)
static void spare_drop(void *v);
#endif

#if defined(__wasm__)
static ak_deflater *the_spare;  // one thread
#elif defined(_WIN32)
static INIT_ONCE spare_once = INIT_ONCE_STATIC_INIT;
static DWORD spare_fls = FLS_OUT_OF_INDEXES;
static VOID WINAPI spare_fls_drop(PVOID v) {
	if (v) spare_drop(v);
}
static BOOL CALLBACK spare_init(PINIT_ONCE o, PVOID a, PVOID *c) {
	(void)o, (void)a, (void)c;
	spare_fls = FlsAlloc(spare_fls_drop);
	return TRUE;
}
#else
static pthread_once_t spare_once = PTHREAD_ONCE_INIT;
static pthread_key_t spare_key;
static bool spare_ok;
static void spare_init(void) { spare_ok = pthread_key_create(&spare_key, spare_drop) == 0; }
#endif

static ak_deflater *spare_take(void) {
#if defined(__wasm__)
	ak_deflater *d = the_spare;
	the_spare = NULL;
	return d;
#elif defined(_WIN32)
	InitOnceExecuteOnce(&spare_once, spare_init, NULL, NULL);
	if (spare_fls == FLS_OUT_OF_INDEXES) return NULL;
	ak_deflater *d = FlsGetValue(spare_fls);
	if (d) FlsSetValue(spare_fls, NULL);
	return d;
#else
	pthread_once(&spare_once, spare_init);
	if (!spare_ok) return NULL;
	ak_deflater *d = pthread_getspecific(spare_key);
	if (d) pthread_setspecific(spare_key, NULL);
	return d;
#endif
}

// spare_keep keeps d as the thread's spare; false when there's one.
static bool spare_keep(ak_deflater *d) {
#if defined(__wasm__)
	if (the_spare) return false;
	the_spare = d;
	return true;
#elif defined(_WIN32)
	InitOnceExecuteOnce(&spare_once, spare_init, NULL, NULL);
	if (spare_fls == FLS_OUT_OF_INDEXES || FlsGetValue(spare_fls)) return false;
	return FlsSetValue(spare_fls, d) != 0;
#else
	pthread_once(&spare_once, spare_init);
	if (!spare_ok || pthread_getspecific(spare_key)) return false;
	return pthread_setspecific(spare_key, d) == 0;
#endif
}

static void release(ak_deflater *d);

#if !defined(__wasm__)
static void spare_drop(void *v) { release(v); }
#endif

// reset makes d (its buffers kept) a new stream's at its level.
static void reset(ak_deflater *d, ak_fl_put put, void *user) {
	d->put = put;
	d->user = user;
	d->ok = true;
	d->pos = d->end = d->block_start = d->sym_end = 0;
	d->prev_len = d->prev_dist = d->lit_run = 0;
	d->avail = false;
	d->nsyms = 0;
	memset(d->lfreq, 0, sizeof d->lfreq);
	memset(d->dfreq, 0, sizeof d->dfreq);
	d->bits = 0, d->nbits = 0;
	for (uint32_t i = 0; i < d->n1; i++) d->t1[i] = EMPTY;
	for (uint32_t i = 0; i < d->n2; i++) d->t2[i] = EMPTY;
	if (d->head)
		for (uint32_t i = 0; i < (1u << LAZY_HBITS); i++) d->head[i] = EMPTY;
	if (d->prev)
		for (uint32_t i = 0; i < WSIZE; i++) d->prev[i] = EMPTY;
}

ak_deflater *ak_deflater_new(int level, ak_fl_put put, void *user) {
	if (level < 0) level = 0;
	if (level > 9) level = 9;
	ak_deflater *d = spare_take();
	if (d && d->level == level) {
		reset(d, put, user);
		return d;
	}
	if (d) release(d);
	d = ak_alloc(sizeof *d);
	d->level = level;
	d->mode = level == 0 ? M_STORE : level <= 6 ? M_FAST : M_LAZY;
	d->put = put;
	d->user = user;
	d->ok = true;
	d->win = ak_alloc(WBUF + WPAD);
	if (d->mode == M_FAST) {
		d->n1 = (1u << fast_tables[level].bits1) << fast_tables[level].pair1;
		d->t1 = table(d->n1);
		if (fast_tables[level].bits2) {
			d->n2 = (1u << fast_tables[level].bits2) << fast_tables[level].pair2;
			d->t2 = table(d->n2);
		}
	} else if (d->mode == M_LAZY) {
		d->good = lazy_levels[level - 7].good;
		d->lazy = lazy_levels[level - 7].lazy;
		d->nice = lazy_levels[level - 7].nice;
		d->chain = lazy_levels[level - 7].chain;
		d->head = table(1u << LAZY_HBITS);
		d->prev = table(WSIZE);
		if (d->chain >= 100) d->lit_bits = ak_alloc(256);
	}
	if (d->mode != M_STORE) d->syms = ak_alloc(sizeof(uint32_t) * SYMS);
	for (unsigned c = 0; c < 29; c++)
		for (unsigned k = 0; k < (1u << len_extra[c]) && len_base[c] + k <= MAX_MATCH; k++) d->len_code[len_base[c] + k] = (uint8_t)c;
	d->len_code[MAX_MATCH] = 28;
	for (unsigned c = 0; c < 30; c++) {
		for (unsigned k = 0; k < (1u << dist_extra[c]); k++) {
			unsigned v = dist_base[c] - 1 + k;
			if (v < 256) d->dist_lo[v] = (uint8_t)c;
			else d->dist_hi[v >> 7] = (uint8_t)c;
		}
	}
	return d;
}

void ak_deflater_free(ak_deflater *d) {
	if (!d) return;
	if (spare_keep(d)) return;
	release(d);
}

static void release(ak_deflater *d) {
	ak_free(d->win);
	ak_free(d->t1);
	ak_free(d->t2);
	ak_free(d->head);
	ak_free(d->prev);
	ak_free(d->lit_bits);
	ak_free(d->syms);
	ak_free(d->out);
	ak_free(d);
}

// ---- Bits ----

typedef struct {
	uint8_t *p;
	uint64_t bits;
	unsigned n;  // never more than 63 between flushes
} bitw;

#define PUT(w, v, k) ((w).bits |= (uint64_t)(v) << (w).n, (w).n += (k))
// FLUSH writes the whole bytes, keeping the last one's bits that aren't.
#define FLUSH(w)                        \
	do {                                \
		store64((w).p, (w).bits);       \
		(w).p += (w).n >> 3;            \
		(w).bits >>= (w).n & ~7u;       \
		(w).n &= 7;                     \
	} while (0)

static void out_reserve(ak_deflater *d, size_t n) {
	if (n + 16 > d->out_cap) {
		d->out_cap = n + 16 + n / 4;
		d->out = ak_realloc(d->out, d->out_cap);
	}
}

static bitw w_begin(ak_deflater *d) { return (bitw){.p = d->out, .bits = d->bits, .n = d->nbits}; }

// w_end hands the whole bytes written to put, keeping the last bits.
static void w_end(ak_deflater *d, bitw w) {
	size_t k = (size_t)(w.p - d->out);
	d->bits = w.bits;
	d->nbits = w.n;
	if (k && d->ok) d->ok = d->put(d->user, d->out, k);
}

// ---- Huffman codes ----

static int cmp_u32(const void *a, const void *b) {
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y;
}

// huff_lengths gives each of n symbols a code length from its frequency,
// none longer than limit: Moffat and Katajainen's in-place minimum
// redundancy code, its lengths then limited as miniz does (the deepest
// codes moved up, Kraft's sum restored by lengthening shorter ones). At
// least two symbols get codes (zlib's inflate once refused one).
static void huff_lengths(const uint32_t *freq, unsigned n, unsigned limit, uint8_t *lens) {
	uint32_t key[288], a[288];
	unsigned m = 0;
	for (unsigned s = 0; s < n; s++) {
		lens[s] = 0;
		if (freq[s]) key[m++] = freq[s] << 9 | s;
	}
	for (unsigned s = 0; m < 2 && s < n; s++)
		if (!freq[s]) key[m++] = 1u << 9 | s;
	qsort(key, m, sizeof key[0], cmp_u32);
	for (unsigned i = 0; i < m; i++) a[i] = key[i] >> 9;
	// Moffat and Katajainen: a[] (ascending) becomes each one's length.
	{
		int root = 0, leaf = 2, next, avbl, used, dpth, nn = (int)m;
		a[0] += a[1];
		for (next = 1; next < nn - 1; next++) {
			if (leaf >= nn || a[root] < a[leaf]) {
				a[next] = a[root];
				a[root++] = (uint32_t)next;
			} else {
				a[next] = a[leaf++];
			}
			if (leaf >= nn || (root < next && a[root] < a[leaf])) {
				a[next] += a[root];
				a[root++] = (uint32_t)next;
			} else {
				a[next] += a[leaf++];
			}
		}
		a[nn - 2] = 0;
		for (next = nn - 3; next >= 0; next--) a[next] = a[a[next]] + 1;
		avbl = 1, used = dpth = 0, root = nn - 2, next = nn - 1;
		while (avbl > 0) {
			while (root >= 0 && (int)a[root] == dpth) used++, root--;
			while (avbl > used) a[next--] = (uint32_t)dpth, avbl--;
			avbl = 2 * used, dpth++, used = 0;
		}
	}
	// Limit the lengths.
	unsigned count[33] = {0}, maxlen = 0;
	for (unsigned i = 0; i < m; i++) {
		count[a[i]]++;
		if (a[i] > maxlen) maxlen = a[i];
	}
	if (maxlen > limit) {
		for (unsigned l = limit + 1; l <= 32; l++) count[limit] += count[l], count[l] = 0;
		uint32_t total = 0;
		for (unsigned l = 1; l <= limit; l++) total += count[l] << (limit - l);
		while (total > (1u << limit)) {
			count[limit]--;
			for (unsigned l = limit - 1; l > 0; l--) {
				if (count[l]) {
					count[l]--;
					count[l + 1] += 2;
					break;
				}
			}
			total--;
		}
		unsigned i = 0;
		for (unsigned l = limit; l > 0; l--)
			for (unsigned k = 0; k < count[l]; k++) a[i++] = l;
	}
	for (unsigned i = 0; i < m; i++) lens[key[i] & 511] = (uint8_t)a[i];
}

// huff_codes: the canonical codes of lens, bit-reversed (written LSB first).
static void huff_codes(const uint8_t *lens, unsigned n, uint16_t *codes) {
	unsigned count[16] = {0}, next[16];
	for (unsigned s = 0; s < n; s++) count[lens[s]]++;
	count[0] = 0;
	unsigned code = 0;
	for (unsigned l = 1; l < 16; l++) {
		code = (code + count[l - 1]) << 1;
		next[l] = code;
	}
	for (unsigned s = 0; s < n; s++)
		codes[s] = lens[s] ? (uint16_t)reverse(next[lens[s]]++, lens[s]) : 0;
}

// The fixed codes' lengths (3.2.6).
static uint8_t fixed_len(unsigned s) { return s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8; }

// ---- Blocks ----

static void stored_blocks(ak_deflater *d, const uint8_t *p, size_t n, bool last) {
	size_t chunks = n ? (n + 65534) / 65535 : 1;
	out_reserve(d, n + chunks * 6 + 16);
	bitw w = w_begin(d);
	size_t off = 0;
	do {
		size_t k = n - off < 65535 ? n - off : 65535;
		PUT(w, last && off + k == n, 3);
		FLUSH(w);
		if (w.n) {
			*w.p++ = (uint8_t)w.bits;
			w.bits = 0, w.n = 0;
		}
		w.p[0] = (uint8_t)k, w.p[1] = (uint8_t)(k >> 8), w.p[2] = (uint8_t)~k, w.p[3] = (uint8_t)(~k >> 8);
		memcpy(w.p + 4, p + off, k);
		w.p += 4 + k;
		off += k;
	} while (off < n);
	w_end(d, w);
}

// flush_block writes the block gathered: [block_start, sym_end), its
// symbols in syms; or (lit_only) its bytes as literals, when matches
// saved too little to be worth their codes (Go's writeBlockHuff).
static void flush_block(ak_deflater *d, bool last, bool lit_only) {
	uint32_t *lf = d->lfreq, *df = d->dfreq;
	const uint8_t *raw_p = d->win + d->block_start;
	if (lit_only) {
		memset(d->lfreq, 0, sizeof d->lfreq);
		memset(d->dfreq, 0, sizeof d->dfreq);
		for (uint32_t i = 0, n = d->sym_end - d->block_start; i < n; i++) lf[raw_p[i]]++;
	}
	lf[256] = 1;
	uint8_t ll[288] = {0}, dl[32] = {0}, cl[19];
	uint16_t lc[288], dc[32], cc[19];
	huff_lengths(lf, 286, 15, ll);
	huff_lengths(df, 30, 15, dl);
	unsigned hlit = 286, hdist = 30;
	while (hlit > 257 && !ll[hlit - 1]) hlit--;
	while (hdist > 1 && !dl[hdist - 1]) hdist--;
	// The code lengths, run-length coded (3.2.7).
	uint8_t all[316], tok[316], tex[316];
	unsigned nall = 0, ntok = 0;
	memcpy(all, ll, hlit), nall += hlit;
	memcpy(all + nall, dl, hdist), nall += hdist;
	uint32_t cf[19] = {0};
	for (unsigned i = 0; i < nall;) {
		unsigned l = all[i], run = 1;
		while (i + run < nall && all[i + run] == l) run++;
		i += run;
		if (l == 0) {
			while (run >= 11) {
				unsigned k = run < 138 ? run : 138;
				tok[ntok] = 18, tex[ntok++] = (uint8_t)(k - 11), cf[18]++, run -= k;
			}
			if (run >= 3) tok[ntok] = 17, tex[ntok++] = (uint8_t)(run - 3), cf[17]++, run = 0;
		} else {
			tok[ntok] = (uint8_t)l, tex[ntok++] = 0, cf[l]++, run--;
			while (run >= 3) {
				unsigned k = run < 6 ? run : 6;
				tok[ntok] = 16, tex[ntok++] = (uint8_t)(k - 3), cf[16]++, run -= k;
			}
		}
		while (run--) tok[ntok] = (uint8_t)l, tex[ntok++] = 0, cf[l]++;
	}
	huff_lengths(cf, 19, 7, cl);
	unsigned hclen = 19;
	while (hclen > 4 && !cl[cl_order[hclen - 1]]) hclen--;

	// What each kind of block costs, in bits.
	uint64_t extra = 0, dyn = 3 + 14 + 3 * hclen, fix = 3;
	for (unsigned c = 0; c < 29; c++) extra += (uint64_t)lf[257 + c] * len_extra[c];
	for (unsigned c = 0; c < 30; c++) extra += (uint64_t)df[c] * dist_extra[c];
	for (unsigned i = 0; i < ntok; i++) dyn += cl[tok[i]] + (tok[i] == 16 ? 2 : tok[i] == 17 ? 3 : tok[i] == 18 ? 7 : 0);
	for (unsigned s = 0; s < 286; s++) dyn += (uint64_t)lf[s] * ll[s], fix += (uint64_t)lf[s] * fixed_len(s);
	for (unsigned c = 0; c < 30; c++) dyn += (uint64_t)df[c] * dl[c], fix += (uint64_t)df[c] * 5;
	dyn += extra, fix += extra;
	size_t raw = d->sym_end - d->block_start;
	uint64_t stored = ((raw ? (raw + 65534) / 65535 : 1) * 5 + raw) * 8 + 10;

	if (stored <= dyn && stored <= fix) {
		stored_blocks(d, raw_p, raw, last);
	} else {
		bool fixed = fix <= dyn;
		if (fixed) {
			for (unsigned s = 0; s < 288; s++) ll[s] = fixed_len(s);
			for (unsigned c = 0; c < 30; c++) dl[c] = 5;
		}
		huff_codes(ll, 288, lc);
		huff_codes(dl, 30, dc);
		out_reserve(d, (fixed ? fix : dyn) / 8 + 16);
		bitw w = w_begin(d);
		PUT(w, last, 1);
		PUT(w, fixed ? 1 : 2, 2);
		if (!fixed) {
			huff_codes(cl, 19, cc);
			PUT(w, hlit - 257, 5);
			PUT(w, hdist - 1, 5);
			PUT(w, hclen - 4, 4);
			FLUSH(w);
			for (unsigned i = 0; i < hclen; i++) {
				PUT(w, cl[cl_order[i]], 3);
				FLUSH(w);
			}
			for (unsigned i = 0; i < ntok; i++) {
				unsigned t = tok[i];
				PUT(w, cc[t], cl[t]);
				if (t >= 16) PUT(w, tex[i], t == 16 ? 2 : t == 17 ? 3 : 7);
				FLUSH(w);
			}
		}
		if (lit_only) {
			for (size_t i = 0; i < raw; i++) {
				PUT(w, lc[raw_p[i]], ll[raw_p[i]]);
				if (i & 1) FLUSH(w);
			}
			FLUSH(w);
		}
		const uint32_t *sy = d->syms;
		for (uint32_t i = 0, n = lit_only ? 0 : d->nsyms; i < n; i++) {
			uint32_t s = sy[i];
			if (s < 256) {
				PUT(w, lc[s], ll[s]);
			} else {
				unsigned len = s & 511, dist = s >> 9;
				unsigned c = d->len_code[len], e = dist_code(d, dist);
				PUT(w, lc[257 + c], ll[257 + c]);
				PUT(w, len - len_base[c], len_extra[c]);
				PUT(w, dc[e], dl[e]);
				PUT(w, dist - dist_base[e], dist_extra[e]);
			}
			FLUSH(w);
		}
		PUT(w, lc[256], ll[256]);
		FLUSH(w);
		w_end(d, w);
	}
	memset(d->lfreq, 0, sizeof d->lfreq);
	memset(d->dfreq, 0, sizeof d->dfreq);
	d->nsyms = 0;
	d->block_start = d->sym_end;
}

// ---- Matching ----

static inline void lit(ak_deflater *d, uint8_t c) {
	d->syms[d->nsyms++] = c;
	d->lfreq[c]++;
	d->sym_end++;
}

static inline void lits(ak_deflater *d, const uint8_t *w, int32_t from, int32_t to) {
	uint32_t *sy = d->syms + d->nsyms;
	for (int32_t i = from; i < to; i++) {
		uint8_t c = w[i];
		*sy++ = c;
		d->lfreq[c]++;
	}
	if (to > from) d->nsyms += (uint32_t)(to - from), d->sym_end += (uint32_t)(to - from);
}

static inline void match(ak_deflater *d, uint32_t len, uint32_t dist) {
	d->syms[d->nsyms++] = dist << 9 | len;
	d->lfreq[257 + d->len_code[len]]++;
	d->dfreq[dist_code(d, dist)]++;
	d->sym_end += len;
}

// match_long emits a match of any length, as matches of at most 258
// (none left shorter than 3).
static inline void match_long(ak_deflater *d, int32_t len, int32_t dist) {
	while (len > 0) {
		int32_t k = len <= MAX_MATCH ? len : len > MAX_MATCH + 3 ? MAX_MATCH : MAX_MATCH - 3;
		match(d, (uint32_t)k, (uint32_t)dist);
		len -= k;
	}
}

// match_len: how many bytes from n on a and b share, the first n known
// equal, up to max. Reads up to 8 bytes past max (WPAD).
static inline uint32_t match_len(const uint8_t *a, const uint8_t *b, uint32_t n, uint32_t max) {
	for (;;) {
		uint64_t x = load64(a + n) ^ load64(b + n);
		if (x) {
			n += ctz64(x) >> 3;
			break;
		}
		n += 8;
		if (n >= max) break;
	}
	return n < max ? n : max;
}

// mlen: how long the match of s with t is, s's up to end.
static inline int32_t mlen(const uint8_t *w, int32_t s, int32_t t, int32_t end) {
	return s >= end ? 0 : (int32_t)match_len(w + t, w + s, 0, (uint32_t)(end - s));
}

// mlim: likewise, up to 254 (with the 4 bytes known: a 258's).
static inline int32_t mlim(const uint8_t *w, int32_t s, int32_t t, int32_t end) {
	return mlen(w, s, t, end - s > MAX_MATCH - 4 ? s + MAX_MATCH - 4 : end);
}

// ---- Levels 1 to 6 ----
//
// Each encodes a block, [start, end) of the window, the 32 KB before it
// its history, as Go's compress/flate does (Klaus Post's encoders, from
// github.com/klauspost/compress; BSD-licensed, Copyright 2011 The Snappy-Go
// Authors and 2019 Klaus Post): hash tables of the positions last seen,
// a candidate checked by its first 4 bytes, matches extended both ways,
// positions skipped faster the longer nothing matches (by a shift of the
// distance from the last match: 5 at levels 1 and 2, 6 at 4 and 5, 7 at 3
// and 6). Level 1: one table of 5-byte hashes, 2^15. 2: 2^17. 3: 2^16
// entries of two positions each, the better match taken. 4: a 4-byte
// table and a 7-byte one, the next position's long match taken if it's
// longer. 5: the 7-byte table's entries two deep, and a match that
// starts 2 bytes later and ends past this one's end looked for. 6: also
// the last match's distance tried one byte on, and more positions
// indexed.

#define CAND_OK(s, t) ((s) - (t) < WSIZE)

static void enc1(ak_deflater *d, int32_t start, int32_t end) {
	const uint8_t *w = d->win;
	int32_t *tab = d->t1;
	enum { B = 15 };
	int32_t s = start, next_emit = start, s_limit = end - 11;
	if (end - start < 13) goto rem;
	uint64_t cv = load64(w + s);
	for (;;) {
		int32_t next_s, t;
		for (;;) {
			uint32_t nh = h5(cv, B);
			int32_t cand = tab[nh];
			next_s = s + 2 + ((s - next_emit) >> 5);
			if (next_s > s_limit) goto rem;
			uint64_t now = load64(w + next_s);
			tab[nh] = s;
			nh = h5(now, B);
			t = cand;
			if (CAND_OK(s, t) && (uint32_t)cv == load32(w + t)) {
				tab[nh] = next_s;
				break;
			}
			cv = now, s = next_s, next_s++;
			cand = tab[nh];
			now >>= 8;
			tab[nh] = s;
			t = cand;
			if (CAND_OK(s, t) && (uint32_t)cv == load32(w + t)) {
				tab[nh] = next_s;
				break;
			}
			cv = now, s = next_s;
		}
		for (;;) {
			int32_t l = 4 + mlen(w, s + 4, t + 4, end);
			while (t > 0 && s > next_emit && w[t - 1] == w[s - 1]) s--, t--, l++;
			lits(d, w, next_emit, s);
			match_long(d, l, s - t);
			s += l;
			next_emit = s;
			if (next_s >= s) s = next_s + 1;
			if (s >= s_limit) {
				if (s + l + 8 < end) tab[h5(load64(w + s), B)] = s;
				goto rem;
			}
			uint64_t x = load64(w + s - 2);
			tab[h5(x, B)] = s - 2;
			x >>= 16;
			uint32_t ch = h5(x, B);
			t = tab[ch];
			tab[ch] = s;
			if (s - t > WSIZE || (uint32_t)x != load32(w + t)) {
				cv = x >> 8;
				s++;
				break;
			}
		}
	}
rem:
	lits(d, w, next_emit, end);
}

static void enc2(ak_deflater *d, int32_t start, int32_t end) {
	const uint8_t *w = d->win;
	int32_t *tab = d->t1;
	enum { B = 17 };
	int32_t s = start, next_emit = start, s_limit = end - 11;
	if (end - start < 13) goto rem;
	uint64_t cv = load64(w + s);
	for (;;) {
		int32_t next_s = s, cand;
		for (;;) {
			uint32_t nh = h5(cv, B);
			s = next_s;
			next_s = s + 2 + ((s - next_emit) >> 5);
			if (next_s > s_limit) goto rem;
			cand = tab[nh];
			uint64_t now = load64(w + next_s);
			tab[nh] = s;
			nh = h5(now, B);
			if (CAND_OK(s, cand) && (uint32_t)cv == load32(w + cand)) {
				tab[nh] = next_s;
				break;
			}
			cv = now, s = next_s, next_s++;
			cand = tab[nh];
			now >>= 8;
			tab[nh] = s;
			if (CAND_OK(s, cand) && (uint32_t)cv == load32(w + cand)) break;
			cv = now;
		}
		for (;;) {
			int32_t t = cand;
			int32_t l = 4 + mlen(w, s + 4, t + 4, end);
			while (t > 0 && s > next_emit && w[t - 1] == w[s - 1]) s--, t--, l++;
			lits(d, w, next_emit, s);
			match_long(d, l, s - t);
			s += l;
			next_emit = s;
			if (next_s >= s) s = next_s + 1;
			if (s >= s_limit) {
				if (s + l + 8 < end) tab[h5(load64(w + s), B)] = s;
				goto rem;
			}
			for (int32_t i = s - l + 2; i < s - 5; i += 7) {
				uint64_t x = load64(w + i);
				tab[h5(x, B)] = i;
				tab[h5(x >> 16, B)] = i + 2;
				tab[h5(x >> 32, B)] = i + 4;
			}
			uint64_t x = load64(w + s - 2);
			tab[h5(x, B)] = s - 2;
			tab[h5(x >> 8, B)] = s - 1;
			uint32_t ch = h5(x >> 16, B);
			cand = tab[ch];
			tab[ch] = s;
			if (s - cand > WSIZE || (uint32_t)(x >> 16) != load32(w + cand)) {
				cv = x >> 24;
				s++;
				break;
			}
		}
	}
rem:
	lits(d, w, next_emit, end);
}

// push makes p the newest of a two-deep entry's positions.
static inline void push(int32_t *tab, uint32_t h, int32_t p) {
	tab[2 * h + 1] = tab[2 * h];
	tab[2 * h] = p;
}

static void enc3(ak_deflater *d, int32_t start, int32_t end) {
	const uint8_t *w = d->win;
	int32_t *tab = d->t1;
	enum { B = 16 };
	int32_t s = start, next_emit = start, s_limit = end - 11;
	if (end - start < 13) goto rem;
	uint64_t cv = load64(w + s);
	for (;;) {
		int32_t next_s = s, cand;
		for (;;) {
			uint32_t nh = h5(cv, B);
			s = next_s;
			next_s = s + 1 + ((s - next_emit) >> 7);
			if (next_s > s_limit) goto rem;
			int32_t c_cur = tab[2 * nh], c_prev = tab[2 * nh + 1];
			uint64_t now = load64(w + next_s);
			int32_t min_off = s - (WSIZE - 4);
			push(tab, nh, s);
			cand = c_cur;
			if (cand < min_off) {
				cv = now;
				continue;
			}
			if ((uint32_t)cv == load32(w + cand)) {
				if (c_prev < min_off || (uint32_t)cv != load32(w + c_prev)) break;
				if (mlen(w, s + 4, c_prev + 4, end) > mlen(w, s + 4, cand + 4, end)) cand = c_prev;
				break;
			}
			cand = c_prev;
			if (cand > min_off && (uint32_t)cv == load32(w + cand)) break;
			cv = now;
		}
		for (;;) {
			int32_t t = cand;
			int32_t l = 4 + mlen(w, s + 4, t + 4, end);
			while (t > 0 && s > next_emit && w[t - 1] == w[s - 1]) s--, t--, l++;
			lits(d, w, next_emit, s);
			match_long(d, l, s - t);
			s += l;
			next_emit = s;
			if (next_s >= s) s = next_s + 1;
			if (s >= s_limit) {
				t += l;
				if (t + 8 < end && t > 0) push(tab, h5(load64(w + t), B), t);
				goto rem;
			}
			for (int32_t i = s - l + 2; i < s - 5; i += 6) push(tab, h5(load64(w + i), B), i);
			uint64_t x = load64(w + s - 2);
			push(tab, h5(x, B), s - 2);
			x >>= 8;
			push(tab, h5(x, B), s - 1);
			x >>= 8;
			uint32_t ch = h5(x, B);
			int32_t c_cur = tab[2 * ch], c_prev = tab[2 * ch + 1];
			cv = x;
			push(tab, ch, s);
			cand = c_cur;
			int32_t min_off = s - (WSIZE - 4);
			if (cand > min_off) {
				if ((uint32_t)cv == load32(w + cand)) continue;
				cand = c_prev;
				if (cand > min_off && (uint32_t)cv == load32(w + cand)) continue;
			}
			cv = x >> 8;
			s++;
			break;
		}
	}
rem:
	lits(d, w, next_emit, end);
}

static void enc4(ak_deflater *d, int32_t start, int32_t end) {
	const uint8_t *w = d->win;
	int32_t *ts = d->t1, *tl = d->t2;
	enum { B = 15 };
	int32_t s = start, next_emit = start, s_limit = end - 11;
	if (end - start < 13) goto rem;
	uint64_t cv = load64(w + s);
	for (;;) {
		int32_t next_s = s, t;
		for (;;) {
			uint32_t hs = h4(cv, B), hl = h7(cv, B);
			s = next_s;
			next_s = s + 1 + ((s - next_emit) >> 6);
			if (next_s > s_limit) goto rem;
			int32_t sc = ts[hs], lc = tl[hl];
			uint64_t next = load64(w + next_s);
			ts[hs] = s, tl[hl] = s;
			t = lc;
			if (CAND_OK(s, t) && (uint32_t)cv == load32(w + t)) break;
			t = sc;
			if (CAND_OK(s, t) && (uint32_t)cv == load32(w + t)) {
				lc = tl[h7(next, B)];
				if (CAND_OK(next_s, lc) && load32(w + lc) == (uint32_t)next) {
					if (mlen(w, next_s + 4, lc + 4, end) > mlen(w, s + 4, t + 4, end)) s = next_s, t = lc;
				}
				break;
			}
			cv = next;
		}
		int32_t l = 4 + mlen(w, s + 4, t + 4, end);
		while (t > 0 && s > next_emit && w[t - 1] == w[s - 1]) s--, t--, l++;
		lits(d, w, next_emit, s);
		match_long(d, l, s - t);
		s += l;
		next_emit = s;
		if (next_s >= s) s = next_s + 1;
		if (s >= s_limit) {
			if (s + 8 < end) {
				cv = load64(w + s);
				ts[h4(cv, B)] = s, tl[h7(cv, B)] = s;
			}
			goto rem;
		}
		for (int32_t i = next_s; i < s - 1; i += 3) {
			uint64_t x = load64(w + i);
			tl[h7(x, B)] = i;
			tl[h7(x >> 8, B)] = i + 1;
			ts[h4(x >> 8, B)] = i + 1;
		}
		uint64_t x = load64(w + s - 1);
		ts[h4(x, B)] = s - 1, tl[h7(x, B)] = s - 1;
		cv = x >> 8;
	}
rem:
	lits(d, w, next_emit, end);
}

// enc56: levels 5 and 6 (six: its extra searches).
static inline void enc56(ak_deflater *d, int32_t start, int32_t end, bool six) {
	const uint8_t *w = d->win;
	int32_t *ts = d->t1, *tl = d->t2;
	enum { B = 15 };
	int32_t s = start, next_emit = start, s_limit = end - 11, repeat = 1;
	unsigned skip = six ? 7 : 6;
	if (end - start < 13) goto rem;
	uint64_t cv = load64(w + s);
	for (;;) {
		int32_t next_s = s, l = 0, t;
		for (;;) {
			uint32_t hs = h4(cv, B), hl = h7(cv, B);
			s = next_s;
			next_s = s + 1 + ((s - next_emit) >> skip);
			if (next_s > s_limit) goto rem;
			int32_t sc = ts[hs], lcur = tl[2 * hl], lprev = tl[2 * hl + 1];
			uint64_t next = load64(w + next_s);
			ts[hs] = s;
			push(tl, hl, s);
			hs = h4(next, B), hl = h7(next, B);
			t = lcur;
			if (CAND_OK(s, t)) {
				if ((uint32_t)cv == load32(w + t)) {
					ts[hs] = next_s;
					push(tl, hl, next_s);
					int32_t t2 = lprev;
					if (CAND_OK(s, t2) && (uint32_t)cv == load32(w + t2)) {
						l = 4 + mlim(w, s + 4, t + 4, end);
						int32_t l1 = 4 + mlim(w, s + 4, t2 + 4, end);
						if (l1 > l) t = t2, l = l1;
					}
					break;
				}
				t = lprev;
				if (CAND_OK(s, t) && (uint32_t)cv == load32(w + t)) {
					ts[hs] = next_s;
					push(tl, hl, next_s);
					break;
				}
			}
			t = sc;
			if (CAND_OK(s, t) && (uint32_t)cv == load32(w + t)) {
				l = 4 + mlim(w, s + 4, t + 4, end);
				int32_t ncur = tl[2 * hl], nprev = tl[2 * hl + 1];
				ts[hs] = next_s;
				push(tl, hl, next_s);
				if (six) {
					// The last match's distance, a byte on.
					int32_t t2 = s - repeat + 1;
					if (load32(w + t2) == (uint32_t)(cv >> 8)) {
						int32_t ml = 4 + mlim(w, s + 5, t2 + 4, end);
						if (ml > l) {
							t = t2, l = ml, s++;
							break;
						}
					}
				}
				int32_t t2 = ncur;
				if (CAND_OK(next_s, t2)) {
					if (load32(w + t2) == (uint32_t)next) {
						int32_t ml = 4 + mlim(w, next_s + 4, t2 + 4, end);
						if (ml > l) {
							t = t2, s = next_s, l = ml;
							if (!six) break;
						}
					}
					t2 = nprev;
					if (CAND_OK(next_s, t2) && load32(w + t2) == (uint32_t)next) {
						int32_t ml = 4 + mlim(w, next_s + 4, t2 + 4, end);
						if (ml > l) {
							t = t2, s = next_s, l = ml;
							break;
						}
					}
				}
				break;
			}
			cv = next;
		}
		if (l == 0) l = 4 + mlen(w, s + 4, t + 4, end);
		else if (l == MAX_MATCH) l += mlen(w, s + l, t + l, end);
		// A match 2 bytes on that reaches past this one's end: where the
		// 7 bytes at its end were last seen, l back.
		int32_t s_at = s + l;
		if ((six || l < 30) && s_at < s_limit) {
			uint32_t he = h7(load64(w + s_at), B);
			int32_t s2 = s + 2, t2 = tl[2 * he] - l + 2, off = s2 - t2;
			if (off < WSIZE || !six) {
				if (t2 >= 0 && off > 0 && off < WSIZE) {
					int32_t l2 = mlen(w, s2, t2, end);
					if (l2 > l) t = t2, l = l2, s = s2;
				}
				if (six) {
					t2 = tl[2 * he + 1] - l + 2, off = s2 - t2;
					if (t2 >= 0 && off > 0 && off < WSIZE) {
						int32_t l2 = mlen(w, s2, t2, end);
						if (l2 > l) t = t2, l = l2, s = s2;
					}
				}
			}
		}
		while (t > 0 && s > next_emit && w[t - 1] == w[s - 1]) s--, t--, l++;
		lits(d, w, next_emit, s);
		match_long(d, l, s - t);
		repeat = s - t;
		s += l;
		next_emit = s;
		if (next_s >= s) s = next_s + 1;
		if (six) {
			if (s >= s_limit) {
				for (int32_t i = next_s + 1; i < end - 8; i += 2) {
					uint64_t x = load64(w + i);
					ts[h4(x, B)] = i;
					push(tl, h7(x, B), i);
				}
				goto rem;
			}
			for (int32_t i = next_s + 1; i < s - 1; i += 2) {
				uint64_t x = load64(w + i);
				ts[h4(x, B)] = i;
				push(tl, h7(x, B), i);
				push(tl, h7(x >> 8, B), i + 1);
			}
			cv = load64(w + s);
			continue;
		}
		if (s >= s_limit) goto rem;
		int32_t i = s - l + 1;
		if (i < s - 1) {
			uint64_t x = load64(w + i);
			ts[h4(x, B)] = i;
			push(tl, h7(x, B), i);
			push(tl, h7(x >> 8, B), i + 1);
			ts[h4(x >> 16, B)] = i + 2;
			for (i += 4; i < s - 1; i += 3) {
				x = load64(w + i);
				push(tl, h7(x, B), i);
				ts[h4(x >> 8, B)] = i + 1;
			}
		}
		uint64_t x = load64(w + s - 1);
		ts[h4(x, B)] = s - 1;
		push(tl, h7(x, B), s - 1);
		cv = x >> 8;
	}
rem:
	lits(d, w, next_emit, end);
}

static void enc5(ak_deflater *d, int32_t start, int32_t end) { enc56(d, start, end, false); }
static void enc6(ak_deflater *d, int32_t start, int32_t end) { enc56(d, start, end, true); }

static void (*const encs[7])(ak_deflater *, int32_t, int32_t) = {NULL, enc1, enc2, enc3, enc4, enc5, enc6};

// run_fast encodes the window's whole blocks (and, last, what's left),
// each written as it's done.
static void run_fast(ak_deflater *d, bool last) {
	while (d->ok && (d->end - d->pos > BLK || (last && d->pos < d->end))) {
		uint32_t bend = d->end - d->pos > BLK ? d->pos + BLK : d->end;
		encs[d->level](d, (int32_t)d->pos, (int32_t)bend);
		d->pos = bend;
		uint32_t raw = bend - d->block_start;
		flush_block(d, last && bend == d->end, d->nsyms > raw - (raw >> 4));
	}
}

// ---- Levels 7 to 9 ----
//
// zlib's lazy matching over hash chains, with Go's refinements: before
// a match is emitted, one that starts up to 2 bytes later and reaches
// further (found by the hash of the bytes at its end) replaces it; long
// runs of literals skip searching, more the longer they run; and level 9
// takes a longer match only if what it saves (its bytes' literal codes,
// from the input ahead's histogram, less its extra bits) grows.

static inline uint32_t lhash(const uint8_t *p) { return h4(load32(p), LAZY_HBITS); }

// longest follows pos's chain from cand for a match longer than best
// (at least 3), up to max; 0 if none is, else its length (dist set).
static inline uint32_t longest(const ak_deflater *d, uint32_t pos, int32_t cand, uint32_t best, uint32_t max,
                               uint32_t chain, uint32_t *dist) {
	const uint8_t *w = d->win, *cur = w + pos;
	const int32_t *prev = d->prev;
	int32_t lo = (int32_t)pos - WSIZE;
	uint32_t nice = d->nice < max ? d->nice : max, found = 0;
	int gain = 4;
	if (best >= max) return 0;
	uint32_t first = load32(cur), tail = load32(cur + best - 3);
	while (cand >= lo) {
		const uint8_t *m = w + cand;
		if (load32(m + best - 3) == tail && load32(m) == first) {
			uint32_t len = match_len(m, cur, 4, max);
			if (len > best) {
				uint32_t dd = pos - (uint32_t)cand;
				if (d->lit_bits) {
					int g = -3 - (int)dist_extra[dist_code(d, dd)] - (int)len_extra[d->len_code[len]];
					for (uint32_t i = 0; i < len; i++) g += d->lit_bits[cur[i]];
					if (g <= gain) goto next;
					gain = g;
				}
				best = found = len;
				*dist = dd;
				if (len >= nice) break;
				tail = load32(cur + best - 3);
			}
		}
	next:
		if (--chain == 0) break;
		int32_t nx = prev[cand & WMASK];
		if (nx >= cand) break;
		cand = nx;
	}
	return found;
}

static inline int32_t insert(ak_deflater *d, uint32_t p) {
	uint32_t h = lhash(d->win + p);
	int32_t cand = d->head[h];
	d->head[h] = (int32_t)p;
	d->prev[p & WMASK] = cand;
	return cand;
}

// better_at_end: the match at p (len, dist) or one starting up to 2
// bytes on that reaches further (Go's tryBetterMatchAtEnd); *start
// where it starts.
static uint32_t better_at_end(ak_deflater *d, uint32_t p, uint32_t pos, uint32_t len, uint32_t *dist, uint32_t *start) {
	const uint8_t *w = d->win;
	*start = p;
	if (len >= MAX_MATCH - 2 || p + len + MIN_MATCH > d->end) return len;
	uint32_t look = d->end - pos;
	uint32_t lim = (look < MAX_MATCH + 2 ? look : MAX_MATCH + 2) + p;
	int32_t lo = pos > WSIZE ? (int32_t)(pos - WSIZE) : 0;
	int32_t c = d->head[lhash(w + p + len)] - (int32_t)len;
	if (c <= lo + 2 || p - (uint32_t)c == *dist) return len;
	if (lim <= p + 2) return len;
	uint32_t n = match_len(w + c + 2, w + p + 2, 0, lim - p - 2);
	if (n <= len) return len;
	*dist = p - (uint32_t)c;
	uint32_t i = 2;
	while (i > 0 && n < MAX_MATCH && w[p + i - 1] == w[c + i - 1]) i--, n++;
	*start = p + i;
	return n;
}

static void lit_costs(ak_deflater *d) {
	uint32_t freq[256] = {0};
	uint32_t n = d->end - d->pos < LAZY_SYMS ? d->end - d->pos : LAZY_SYMS;
	for (uint32_t i = 0; i < n; i++) freq[d->win[d->pos + i]]++;
	huff_lengths(freq, 256, 15, d->lit_bits);
}

static void run_lazy(ak_deflater *d, bool last) {
	const uint8_t *w = d->win;
	uint32_t pos = d->pos, end = d->end;
	uint32_t limit = last ? end : end > LOOK ? end - LOOK : 0;
	if (pos >= limit) return;
	if (d->lit_bits) lit_costs(d);
	uint32_t prev_len = d->prev_len, prev_dist = d->prev_dist, run = d->lit_run;
	bool avail = d->avail;
	while (pos < limit) {
		if (d->nsyms >= LAZY_SYMS) flush_block(d, false, false);
		uint32_t cur_len = 0, cur_dist = 0, max = end - pos;
		if (max > MAX_MATCH) max = MAX_MATCH;
		if (max >= MIN_MATCH) {
			int32_t cand = insert(d, pos);
			if (prev_len < d->lazy && cand >= (int32_t)pos - WSIZE) {
				uint32_t chain = prev_len >= d->good ? d->chain >> 2 : d->chain;
				cur_len = longest(d, pos, cand, prev_len > MIN_MATCH - 1 ? prev_len : MIN_MATCH - 1, max, chain ? chain : 1, &cur_dist);
			}
		}
		if (prev_len >= MIN_MATCH && cur_len <= prev_len) {
			uint32_t start, dist = prev_dist;
			uint32_t len = better_at_end(d, pos - 1, pos, prev_len, &dist, &start);
			for (uint32_t p = pos - 1; p < start; p++) lit(d, w[p]);
			match(d, len, dist);
			uint32_t stop = start + len;
			uint32_t ins_end = end - MIN_MATCH + 1 < stop ? end - MIN_MATCH + 1 : stop;
			for (uint32_t p = pos + 1; p < ins_end; p++) insert(d, p);
			pos = stop;
			prev_len = 0, run = 0;
			avail = false;
			continue;
		}
		if (cur_len >= MIN_MATCH) run = 0;
		if (!avail) {
			avail = true;
			prev_len = cur_len, prev_dist = cur_dist;
			pos++;
			continue;
		}
		lit(d, w[pos - 1]);
		prev_len = cur_len, prev_dist = cur_dist;
		pos++;
		if (++run > d->chain) {
			// A long run of literals: skip ahead, searching nowhere.
			uint32_t k = 1 + ((run - d->chain) >> 6);
			for (; k > 0 && pos < end - 1; k--, pos++) {
				lit(d, w[pos - 1]);
				if (pos + MIN_MATCH <= end) insert(d, pos);
			}
			lit(d, w[pos - 1]);
			avail = false;
			prev_len = 0;
		}
	}
	if (last && avail) {
		lit(d, w[pos - 1]);
		avail = false;
		prev_len = 0;
	}
	d->pos = pos;
	d->prev_len = prev_len, d->prev_dist = prev_dist, d->lit_run = run;
	d->avail = avail;
}

static void rebase(int32_t *t, uint32_t n, int32_t s) {
	if (!t) return;
	for (uint32_t i = 0; i < n; i++) t[i] = t[i] >= s ? t[i] - s : EMPTY;
}

// slide moves the window's last 32 KB before pos (and what's after it)
// to its start, the block gathered written first.
static void slide(ak_deflater *d) {
	if (d->nsyms) flush_block(d, false, false);
	uint32_t shift = d->pos > WSIZE ? d->pos - WSIZE : 0;
	if (d->avail && shift > 0) shift--;  // the byte the lazy match waits on
	if (shift == 0) return;
	memmove(d->win, d->win + shift, d->end - shift);
	d->end -= shift, d->pos -= shift, d->block_start -= shift, d->sym_end -= shift;
	int32_t s = (int32_t)shift;
	rebase(d->t1, d->n1, s);
	rebase(d->t2, d->n2, s);
	rebase(d->head, d->head ? 1u << LAZY_HBITS : 0, s);
	rebase(d->prev, d->prev ? WSIZE : 0, s);
}

bool ak_deflater_write(ak_deflater *d, const uint8_t *p, size_t n) {
	while (n > 0 && d->ok) {
		if (d->end == WBUF) {
			if (d->mode == M_STORE) {
				stored_blocks(d, d->win, d->end, false);
				d->end = 0;
			} else {
				slide(d);
			}
		}
		size_t k = WBUF - d->end < n ? WBUF - d->end : n;
		memcpy(d->win + d->end, p, k);
		d->end += (uint32_t)k, p += k, n -= k;
		if (d->mode == M_FAST) run_fast(d, false);
		else if (d->mode == M_LAZY) run_lazy(d, false);
	}
	return d->ok;
}

bool ak_deflater_finish(ak_deflater *d) {
	if (!d->ok) return false;
	if (d->mode == M_STORE) {
		stored_blocks(d, d->win, d->end, true);
		d->end = 0;
	} else if (d->mode == M_FAST && d->pos < d->end) {
		run_fast(d, true);
	} else {
		if (d->mode == M_LAZY) run_lazy(d, true);
		flush_block(d, true, false);
	}
	if (d->nbits && d->ok) {
		uint8_t b = (uint8_t)d->bits;
		d->ok = d->put(d->user, &b, 1);
		d->bits = 0, d->nbits = 0;
	}
	return d->ok;
}

// ============================================================================
// Decompressing
// ============================================================================

// A table entry: bits 0-7 the bits a symbol takes, its code's and its
// extra bits together, so a symbol is consumed by one shift (the decoding
// loop's dependency chain: a table read, a shift, the next read;
// libdeflate's layout), 8-11 what it is, 12-15 the code's length alone
// (a pointer: the subtable's bits), 16-31 its value (a literal, a
// length's or a distance's base, a code length; a pointer: the
// subtable's index).
enum { E_LIT = 0x100, E_EOB = 0x200, E_SUB = 0x400, E_BAD = 0x800 };

// e_sub: the entry a pointer e leads to, after the root's bits of bb.
#define e_sub(t, e, bb, root) ((t)[((e) >> 16) + (((bb) >> (root)) & ((1u << (((e) >> 12) & 15)) - 1))])

// e_extra: e's extra bits, from saved (bb before e was consumed).
static inline uint32_t e_extra(uint64_t saved, uint32_t e) {
	return (uint32_t)((saved & ((UINT64_C(1) << (uint8_t)e) - 1)) >> ((e >> 12) & 15));
}

enum {
	LBITS = 11,
	DBITS = 8,
	PBITS = 7,
	L_ENOUGH = 2342,  // the most a table of 288 codes up to 15 bits with an 11-bit root takes (zlib's enough)
	D_ENOUGH = 402,   // 32 codes, an 8-bit root
	HDR_NEED = 600,   // a dynamic block's header, at most (bytes)
};

enum { S_HEADER, S_STORED, S_HUFF, S_DONE, S_BAD };

struct ak_inflater {
	uint64_t bb;
	unsigned nb;    // bits in bb
	unsigned over;  // zero bytes added past the input's end
	int state;
	bool final;     // the block is the last
	bool fixed;     // the tables are the fixed codes'
	uint32_t stored;
	const char *err;
	uint32_t lt[L_ENOUGH];
	uint32_t dt[D_ENOUGH];
	uint32_t pt[1 << PBITS];
};

static const char *const too_soon = "the data ends too soon";

ak_inflater *ak_inflater_new(void) {
	ak_inflater *d = ak_alloc(sizeof *d);
	d->state = S_HEADER;
	return d;
}

void ak_inflater_reset(ak_inflater *d) {
	d->bb = 0, d->nb = 0, d->over = 0;
	d->state = S_HEADER;
	d->final = false;
	d->stored = 0;
	d->err = NULL;
}

void ak_inflater_free(ak_inflater *d) { ak_free(d); }
const char *ak_inflater_error(const ak_inflater *d) { return d->err ? d->err : "not DEFLATE"; }

// build fills t (root bits, then subtables) from n symbols' code lengths,
// vals[s] each one's entry but its length. A code that's over-subscribed
// is broken; one that's incomplete is too, unless it's a single 1-bit
// code (zlib's rule); none at all is fine (a block with no distances).
static bool build(uint32_t *t, unsigned size, const uint8_t *lens, unsigned n, unsigned root, const uint32_t *vals) {
	uint16_t count[16] = {0}, offs[16], sorted[288];
	for (unsigned s = 0; s < n; s++) count[lens[s]]++;
	count[0] = 0;
	unsigned max = 15;
	while (max > 0 && !count[max]) max--;
	if (max == 0) {
		for (unsigned i = 0; i < (1u << root); i++) t[i] = E_BAD;
		return true;
	}
	int left = 1;
	for (unsigned l = 1; l < 16; l++) {
		left = left * 2 - count[l];
		if (left < 0) return false;
	}
	if (left > 0) {
		if (max != 1) return false;
		for (unsigned i = 0; i < (1u << root); i++) t[i] = E_BAD;
	}
	offs[1] = 0;
	for (unsigned l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + count[l]);
	for (unsigned s = 0; s < n; s++)
		if (lens[s]) sorted[offs[lens[s]]++] = (uint16_t)s;
	uint16_t rem[16];
	memcpy(rem, count, sizeof rem);
	unsigned used = 1u << root, idx = 0, sub_base = 0, sub_bits = 0;
	uint32_t code = 0, cur = ~0u, mask = (1u << root) - 1;
	for (unsigned len = 1; len <= max; len++, code <<= 1) {
		for (unsigned k = 0; k < count[len]; k++, idx++, code++) {
			uint32_t e = (vals[sorted[idx]] + len) | len << 12, rev = reverse(code, len);
			if (len <= root) {
				for (uint32_t j = rev; j <= mask; j += 1u << len) t[j] = e;
			} else {
				if ((rev & mask) != cur) {
					cur = rev & mask;
					unsigned bits = len - root;
					int room = 1 << bits;
					while (bits + root < max) {
						room -= rem[bits + root];
						if (room <= 0) break;
						bits++, room <<= 1;
					}
					if (used + (1u << bits) > size) return false;
					sub_base = used, sub_bits = bits;
					used += 1u << bits;
					t[cur] = E_SUB | sub_base << 16 | bits << 12;
				}
				for (uint32_t j = rev >> root; j < (1u << sub_bits); j += 1u << (len - root)) t[sub_base + j] = e;
			}
			rem[len]--;
		}
	}
	return true;
}

static uint32_t lvals[288], dvals[32], pvals[19];
static int vals_made;  // 0 not made, 1 being made, 2 made

static void make_vals(void) {
	if (__atomic_load_n(&vals_made, __ATOMIC_ACQUIRE) == 2) return;
	int none = 0;
	if (!__atomic_compare_exchange_n(&vals_made, &none, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		while (__atomic_load_n(&vals_made, __ATOMIC_ACQUIRE) != 2) {}
		return;
	}
	for (unsigned s = 0; s < 256; s++) lvals[s] = E_LIT | s << 16;
	lvals[256] = E_EOB;
	for (unsigned c = 0; c < 29; c++) lvals[257 + c] = (uint32_t)len_base[c] << 16 | (uint32_t)len_extra[c];
	lvals[286] = lvals[287] = E_BAD;
	for (unsigned c = 0; c < 30; c++) dvals[c] = (uint32_t)dist_base[c] << 16 | (uint32_t)dist_extra[c];
	dvals[30] = dvals[31] = E_BAD;
	for (unsigned s = 0; s < 19; s++) pvals[s] = s << 16;
	__atomic_store_n(&vals_made, 2, __ATOMIC_RELEASE);
}

// The decoder's state while it runs, in locals.
typedef struct {
	const uint8_t *in, *in_end;
	uint8_t *out, *out_end, *out_lo;
	uint64_t bb;
	unsigned nb, over;
	bool eof;
} run_st;

// fill tops the bit buffer up a byte at a time, zeros past the input's
// end (counted: reading them is the data ending too soon).
static inline void fill(run_st *r) {
	while (r->nb <= 56) {
		if (r->in < r->in_end) r->bb |= (uint64_t)*r->in++ << r->nb;
		else if (r->eof) r->over++;
		else break;
		r->nb += 8;
	}
}

static inline uint32_t take(run_st *r, unsigned k) {
	uint32_t v = (uint32_t)(r->bb & ((1ull << k) - 1));
	r->bb >>= k, r->nb -= k;
	return v;
}

static inline bool overran(const run_st *r) { return r->over * 8 > r->nb; }

// give_back aligns to a byte and returns the whole bytes in the bit
// buffer to the input; false if it read past the input's end.
static bool give_back(run_st *r) {
	unsigned k = r->nb >> 3;
	if (r->over > k) return false;
	r->in -= k - r->over;
	r->bb = 0, r->nb = 0, r->over = 0;
	return true;
}

static bool fail(ak_inflater *d, const char *why) {
	d->err = why;
	d->state = S_BAD;
	return false;
}

static bool header(ak_inflater *d, run_st *r) {
	fill(r);
	d->final = take(r, 1);
	unsigned type = take(r, 2);
	if (type == 0) {
		take(r, r->nb & 7);
		fill(r);
		uint32_t len = take(r, 16), nlen = take(r, 16);
		if (overran(r) || !give_back(r)) return fail(d, too_soon);
		if (len != (~nlen & 0xffff)) return fail(d, "not DEFLATE: a stored block's length is broken");
		d->stored = len;
		d->state = S_STORED;
		return true;
	}
	if (type == 3) return fail(d, "not DEFLATE: a block's type is 3");
	if (type == 1) {
		if (!d->fixed) {
			uint8_t lens[288];
			for (unsigned s = 0; s < 288; s++) lens[s] = fixed_len(s);
			build(d->lt, L_ENOUGH, lens, 288, LBITS, lvals);
			memset(lens, 5, 32);
			build(d->dt, D_ENOUGH, lens, 32, DBITS, dvals);
			d->fixed = true;
		}
		if (overran(r)) return fail(d, too_soon);
		d->state = S_HUFF;
		return true;
	}
	unsigned hlit = take(r, 5) + 257, hdist = take(r, 5) + 1, hclen = take(r, 4) + 4;
	if (hlit > 286 || hdist > 30) return fail(d, "not DEFLATE: too many length or distance codes");
	uint8_t cl[19] = {0}, lens[320];
	for (unsigned i = 0; i < hclen; i++) {
		fill(r);
		cl[cl_order[i]] = (uint8_t)take(r, 3);
	}
	// The code lengths' code is complete, or broken.
	{
		unsigned count[8] = {0};
		int left = 1;
		for (unsigned i = 0; i < 19; i++) count[cl[i]]++;
		for (unsigned l = 1; l < 8 && left >= 0; l++) left = left * 2 - (int)count[l];
		if (left != 0) return fail(d, "not DEFLATE: a block's code lengths' code is broken");
	}
	build(d->pt, 1u << PBITS, cl, 19, PBITS, pvals);
	d->fixed = false;
	unsigned n = hlit + hdist;
	for (unsigned i = 0; i < n;) {
		fill(r);
		uint32_t e = d->pt[r->bb & ((1u << PBITS) - 1)];
		take(r, (uint8_t)e);
		unsigned sym = e >> 16, rep, v = 0;
		if (sym < 16) {
			lens[i++] = (uint8_t)sym;
			continue;
		}
		if (sym == 16) {
			if (i == 0) return fail(d, "not DEFLATE: a repeat with no length before it");
			v = lens[i - 1];
			rep = 3 + take(r, 2);
		} else if (sym == 17) {
			rep = 3 + take(r, 3);
		} else {
			rep = 11 + take(r, 7);
		}
		if (i + rep > n) return fail(d, "not DEFLATE: a block's code lengths are too many");
		memset(lens + i, (int)v, rep);
		i += rep;
	}
	if (overran(r)) return fail(d, too_soon);
	if (!lens[256]) return fail(d, "not DEFLATE: a block has no end code");
	if (!build(d->lt, L_ENOUGH, lens, hlit, LBITS, lvals)) return fail(d, "not DEFLATE: a block's literal/length code is broken");
	if (!build(d->dt, D_ENOUGH, lens + hlit, hdist, DBITS, dvals)) return fail(d, "not DEFLATE: a block's distance code is broken");
	d->state = S_HUFF;
	return true;
}

// copy_match copies len bytes from dist back; fast: there's room for 16
// bytes past them.
static inline uint8_t *copy_match(uint8_t *out, uint32_t dist, uint32_t len) {
	const uint8_t *src = out - dist;
	uint8_t *end = out + len;
	if (dist >= 16) {
		do {
			uint64_t a = load64(src), b = load64(src + 8);
			store64(out, a);
			store64(out + 8, b);
			out += 16, src += 16;
		} while (out < end);
	} else if (dist >= 8) {
		do {
			store64(out, load64(src));
			out += 8, src += 8;
		} while (out < end);
	} else if (dist == 1) {
		memset(out, *src, len + 8);
	} else {
		// A pattern shorter than 8 bytes: one byte at a time.
		for (uint32_t i = 0; i < len; i++) out[i] = src[i];
	}
	return end;
}

// The fast loop's conditions: a symbol's worst (48 bits, 2 refills of 8
// bytes) left in the input; a match and its copy's overrun in the output.
#define FAST_IN 16

enum { H_EOB = -1, H_MORE = -2 };

// huff decodes a Huffman block's symbols; H_EOB at its end, H_MORE when
// it stopped for input or room (r says which), 0 when it failed.
static int huff(ak_inflater *d, run_st *r) {
	const uint32_t *lt = d->lt, *dt = d->dt;
	const uint8_t *in = r->in, *in_end = r->in_end;
	uint8_t *out = r->out, *out_end = r->out_end, *out_lo = r->out_lo;
	uint64_t bb = r->bb;
	unsigned nb = r->nb;
	for (;;) {
		// Fast: the input and the output have room for anything a symbol does.
		while (in_end - in >= FAST_IN && out_end - out >= AK_FL_OUT_SLACK) {
			bb |= load64(in) << nb;
			in += (63 - nb) >> 3;
			nb |= 56;
			uint32_t e = lt[bb & ((1u << LBITS) - 1)];
			if (AK_UNLIKELY(e & E_SUB)) e = e_sub(lt, e, bb, LBITS);
			if (e & E_LIT) {
				bb >>= (uint8_t)e, nb -= (uint8_t)e;
				*out++ = (uint8_t)(e >> 16);
				e = lt[bb & ((1u << LBITS) - 1)];
				if (AK_UNLIKELY(e & E_SUB)) e = e_sub(lt, e, bb, LBITS);
				if (e & E_LIT) {
					bb >>= (uint8_t)e, nb -= (uint8_t)e;
					*out++ = (uint8_t)(e >> 16);
					e = lt[bb & ((1u << LBITS) - 1)];
					if (AK_UNLIKELY(e & E_SUB)) e = e_sub(lt, e, bb, LBITS);
					if (e & E_LIT) {
						bb >>= (uint8_t)e, nb -= (uint8_t)e;
						*out++ = (uint8_t)(e >> 16);
						continue;
					}
				}
			}
			// nb >= 26 here: a length's code and extra bits (20) fit.
			if (AK_UNLIKELY(e & (E_EOB | E_BAD))) {
				if (e & E_BAD) goto bad_code;
				bb >>= (uint8_t)e, nb -= (uint8_t)e;
				goto eob;
			}
			uint64_t saved = bb;
			bb >>= (uint8_t)e, nb -= (uint8_t)e;
			uint32_t len = (e >> 16) + e_extra(saved, e);
			if (nb < 28) {
				bb |= load64(in) << nb;
				in += (63 - nb) >> 3;
				nb |= 56;
			}
			e = dt[bb & ((1u << DBITS) - 1)];
			if (AK_UNLIKELY(e & E_SUB)) e = e_sub(dt, e, bb, DBITS);
			if (AK_UNLIKELY(e & E_BAD)) goto bad_dist;
			saved = bb;
			bb >>= (uint8_t)e, nb -= (uint8_t)e;
			uint32_t dist = (e >> 16) + e_extra(saved, e);
			if (AK_UNLIKELY(dist > (size_t)(out - out_lo))) goto too_far;
			out = copy_match(out, dist, len);
		}
		// Slow: a symbol at a time, each checked.
		if (out_end - out < AK_FL_OUT_SLACK) goto more;
		r->in = in, r->bb = bb, r->nb = nb;
		fill(r);
		in = r->in, bb = r->bb, nb = r->nb;
		if (nb < 48 && !r->eof) goto more;
		uint32_t e = lt[bb & ((1u << LBITS) - 1)];
		if (e & E_SUB) e = e_sub(lt, e, bb, LBITS);
		if (e & E_BAD) goto bad_code;
		uint64_t saved = bb;
		bb >>= (uint8_t)e, nb -= (uint8_t)e;
		if (e & E_LIT) {
			*out++ = (uint8_t)(e >> 16);
		} else if (e & E_EOB) {
			goto eob;
		} else {
			uint32_t len = (e >> 16) + e_extra(saved, e);
			e = dt[bb & ((1u << DBITS) - 1)];
			if (e & E_SUB) e = e_sub(dt, e, bb, DBITS);
			if (e & E_BAD) goto bad_dist;
			saved = bb;
			bb >>= (uint8_t)e, nb -= (uint8_t)e;
			uint32_t dist = (e >> 16) + e_extra(saved, e);
			if (dist > (size_t)(out - out_lo)) goto too_far;
			if (r->over * 8 > nb) goto short_in;
			const uint8_t *src = out - dist;
			for (uint32_t i = 0; i < len; i++) out[i] = src[i];
			out += len;
		}
		if (r->over * 8 > nb) goto short_in;
	}
eob:
	r->in = in, r->out = out, r->bb = bb, r->nb = nb;
	if (overran(r)) return fail(d, too_soon), 0;
	return H_EOB;
more:
	r->in = in, r->out = out, r->bb = bb, r->nb = nb;
	return H_MORE;
bad_code:
	return fail(d, "not DEFLATE: a literal/length code that isn't one"), 0;
bad_dist:
	return fail(d, "not DEFLATE: a distance code that isn't one"), 0;
too_far:
	return fail(d, "not DEFLATE: a match reaches back before the data's start"), 0;
short_in:
	return fail(d, too_soon), 0;
}

int ak_inflater_run(ak_inflater *d, ak_fl_io *io) {
	make_vals();
	run_st r = {
		.in = io->in + io->in_pos,
		.in_end = io->in + io->in_len,
		.out = io->out + io->pos,
		.out_end = io->out + io->cap,
		.out_lo = io->out + io->start,
		.bb = d->bb,
		.nb = d->nb,
		.over = d->over,
		.eof = io->eof,
	};
	int res;
	for (;;) {
		if (d->state == S_HEADER) {
			if (!r.eof && (size_t)(r.in_end - r.in) + r.nb / 8 < HDR_NEED) {
				res = AK_FL_NEED_INPUT;
				break;
			}
			if (!header(d, &r)) {
				res = AK_FL_CORRUPT;
				break;
			}
		} else if (d->state == S_STORED) {
			if (d->stored == 0) {
				d->state = d->final ? S_DONE : S_HEADER;
				continue;
			}
			size_t k = d->stored;
			if ((size_t)(r.in_end - r.in) < k) k = (size_t)(r.in_end - r.in);
			if ((size_t)(r.out_end - r.out) < k) k = (size_t)(r.out_end - r.out);
			if (k == 0) {
				if (r.out == r.out_end) {
					res = AK_FL_NEED_OUTPUT;
				} else if (r.eof) {
					fail(d, too_soon);
					res = AK_FL_CORRUPT;
				} else {
					res = AK_FL_NEED_INPUT;
				}
				break;
			}
			memcpy(r.out, r.in, k);
			r.out += k, r.in += k;
			d->stored -= (uint32_t)k;
		} else if (d->state == S_HUFF) {
			int h = huff(d, &r);
			if (h == H_EOB) {
				d->state = d->final ? S_DONE : S_HEADER;
				continue;
			}
			if (h == 0) {
				res = AK_FL_CORRUPT;
				break;
			}
			res = r.out_end - r.out < AK_FL_OUT_SLACK ? AK_FL_NEED_OUTPUT : AK_FL_NEED_INPUT;
			break;
		} else if (d->state == S_DONE) {
			if (!give_back(&r)) {
				fail(d, too_soon);
				res = AK_FL_CORRUPT;
				break;
			}
			res = AK_FL_DONE;
			break;
		} else {
			res = AK_FL_CORRUPT;
			break;
		}
	}
	io->in_pos = (size_t)(r.in - io->in);
	io->pos = (size_t)(r.out - io->out);
	d->bb = r.bb, d->nb = r.nb, d->over = r.over;
	return res;
}

size_t ak_inflate_mem(uint8_t *out, size_t cap, const uint8_t *src, size_t len) {
	ak_inflater *d = ak_inflater_new();
	ak_fl_io io = {.in = src, .in_len = len, .eof = true, .out = out, .cap = cap};
	int r = ak_inflater_run(d, &io);
	ak_inflater_free(d);
	return r == AK_FL_DONE ? io.pos : SIZE_MAX;
}

// ---- Adler-32 ----

uint32_t ak_adler32(uint32_t adler, const uint8_t *p, size_t n) {
	uint32_t a = adler & 0xffff, b = adler >> 16;
	while (n > 0) {
		// 5552: the most bytes before b can overflow 32 bits (zlib's NMAX).
		size_t k = n < 5552 ? n : 5552;
		n -= k;
		while (k >= 8) {
			a += p[0], b += a;
			a += p[1], b += a;
			a += p[2], b += a;
			a += p[3], b += a;
			a += p[4], b += a;
			a += p[5], b += a;
			a += p[6], b += a;
			a += p[7], b += a;
			p += 8, k -= 8;
		}
		while (k--) a += *p++, b += a;
		a %= 65521, b %= 65521;
	}
	return b << 16 | a;
}
