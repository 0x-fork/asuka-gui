// prop.c: property tests and fuzzing (docs/testing.md#property-tests).
//
// A property test's values come from a tape of choices: each value its
// parameters' types make (an int's size and digits, whether a list goes
// on, a string's characters, an enum's case) is a choice in [0, n),
// drawn at random as a case is generated and recorded, so the tape says
// the whole input. A smaller choice is a simpler value: 0 is an int's 0,
// an optional's none, a list's end, a string's "a", an enum's first case.
// A failing input is shrunk on its tape, whatever its types (as
// Hypothesis does): chunks of choices deleted, zeroed, each choice made as
// small as still fails; the input generated from the shortest, smallest
// tape that still fails is the one reported. A tape read past its end
// reads 0s.
//
// The cases are seeded: the run's seed and the test's name decide them,
// so every run (a CI's) tries the same ones, and --seed says another.
// Fuzzing (-fuzz) generates until its time or count is up, from a seed of
// its own, and keeps the inputs that reach code no other did (the
// coverage counters, when the module counts them: asuka test --fuzz
// compiles it so) to mutate their tapes. Inputs a fuzz found are kept
// as JSON (the corpus), which every run tries first.
#include "testing.h"
#include "vm/vm_int.h"
#include "rt/lib.h"
#include "base/thread.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---- The tape ----

typedef struct {
	AK_VEC(uint64_t) c;       // the choices made
	const uint64_t *in;       // the choices replayed (NULL: none, all drawn)
	uint32_t nin;
	uint32_t at;              // choices made so far
	bool extend;              // past in's end: draw at random (fuzzing), not 0
	uint64_t rng;             // splitmix64's state
	uint32_t budget;          // values left to make: a huge or deep value stops
	bool failed;              // a type it can't make (said in err)
	char err[200];
} tape;

#define BUDGET 4000
#define MAX_LEN 64    // a list's or a string's most items
#define MAX_DEPTH 8   // past it, lists and strings are empty, optionals none

static uint64_t splitmix(uint64_t *s) {
	uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static bool from_tape(tape *t) { return t->in && (t->at < t->nin || !t->extend); }

// choose records a choice in [0, n) (n 0: any 64 bits): the tape's next,
// or random's, which picks it as the caller says.
static uint64_t choose(tape *t, uint64_t n, uint64_t random) {
	uint64_t c;
	if (from_tape(t)) c = t->at < t->nin ? t->in[t->at] : 0;
	else c = random;
	if (n) c %= n;
	t->at++;
	ak_vec_push(&t->c, c);
	return c;
}

static uint64_t draw(tape *t, uint64_t n) {
	uint64_t r = from_tape(t) ? 0 : splitmix(&t->rng);
	return choose(t, n, r);
}

// pick chooses one of n by their weights, w[0] the simplest.
static uint32_t pick(tape *t, const uint16_t *w, uint32_t n) {
	uint64_t r = 0;
	if (!from_tape(t)) {
		uint32_t total = 0;
		for (uint32_t i = 0; i < n; i++) total += w[i];
		uint64_t x = splitmix(&t->rng) % total;
		for (r = 0; r < n - 1 && x >= w[r]; r++) x -= w[r];
	}
	return (uint32_t)choose(t, n, r);
}

static int64_t zigzag(uint64_t c) { return (c & 1) ? -(int64_t)(c >> 1) - 1 : (int64_t)(c >> 1); }
static uint64_t unzig(int64_t v) { return ((uint64_t)v << 1) ^ (uint64_t)(v >> 63); }

// keep records a value the caller made as the choice c (when drawing),
// or reads the tape's: a value is one choice whose order is simplicity's,
// so that shrinking it shrinks the value.
static uint64_t keep(tape *t, uint64_t c) { return choose(t, 0, c); }

// more says whether a list of len goes on: a choice, 0 its end.
static bool more(tape *t, uint32_t len, int depth) {
	if (len >= MAX_LEN || depth > MAX_DEPTH || t->budget == 0) return false;
	static const uint16_t w[] = {1, 4};
	return pick(t, w, 2) == 1;
}

// ---- Generating ----

static ak_val gen(tape *t, const char *d, const ak_json_types *ty, int depth);

// gen_int: one choice, the value zigzagged (0, -1, 1, -2, …). Drawn
// mostly small, then up to a thousand, edges (0, ±1, the 8, 16, 32 and
// 64 bit limits), any 32-bit, any 64-bit.
static int64_t gen_int(tape *t) {
	if (from_tape(t)) return zigzag(keep(t, 0));
	static const int64_t edge[] = {0, 1, -1, 2, 10, 100, 255, 256, 65535, 65536, INT32_MAX, INT32_MIN, (int64_t)UINT32_MAX, INT64_MAX, INT64_MIN, INT64_MAX - 1, INT64_MIN + 1};
	uint64_t k = splitmix(&t->rng) % 100, r = splitmix(&t->rng);
	int64_t v;
	if (k < 40) v = zigzag(r % 33);
	else if (k < 65) v = zigzag(r % 2001);
	else if (k < 75) v = edge[r % (sizeof edge / sizeof *edge)];
	else if (k < 85) v = zigzag(r % (1ULL << 32));
	else v = zigzag(r);
	return zigzag(keep(t, unzig(v)));
}

// gen_float: two choices: whether it's a whole number (0) or not, then
// the number zigzagged, or its bits with the sign last. Drawn as small
// whole numbers, fractions, any finite bits, and the special ones (±0,
// ±1, 0.1, the extremes, the infinities, NaN).
static double float_of(uint64_t kind, uint64_t c) {
	if (kind == 0) return (double)zigzag(c);
	uint64_t bits = c >> 1 | c << 63;
	double v;
	memcpy(&v, &bits, sizeof v);
	return v;
}

static double gen_float(tape *t) {
	uint64_t kind = 0, c = 0;
	if (from_tape(t)) {
		kind = keep(t, 0) % 2;
		c = keep(t, 0);
		return float_of(kind, c);
	}
	static const double special[] = {0.0, -0.0, 1.0, -1.0, 0.5, 0.1, 1e-300, 1e300, DBL_MAX, -DBL_MAX, DBL_MIN, DBL_EPSILON, INFINITY, -INFINITY, NAN};
	uint64_t k = splitmix(&t->rng) % 100;
	double d;
	if (k < 30) {
		d = (double)zigzag(splitmix(&t->rng) % 201);
	} else if (k < 60) {
		d = (double)zigzag(splitmix(&t->rng) % (1ULL << 24));
		d /= (double)(1 + splitmix(&t->rng) % 1000);
	} else if (k < 80) {
		uint64_t bits = splitmix(&t->rng);
		memcpy(&d, &bits, sizeof d);
		if (!isfinite(d)) d = (double)zigzag(bits >> 11);
	} else {
		d = special[splitmix(&t->rng) % (sizeof special / sizeof *special)];
	}
	if (d == floor(d) && fabs(d) < 0x1p53 && !(d == 0 && signbit(d))) {
		kind = 0, c = unzig((int64_t)d);
	} else {
		uint64_t bits;
		memcpy(&bits, &d, sizeof bits);
		kind = 1, c = bits << 1 | bits >> 63;
	}
	kind = keep(t, kind) % 2;
	c = keep(t, c);
	return float_of(kind, c);
}

static void put_utf8(ak_strb *b, uint32_t c) {
	char s[4];
	size_t n;
	if (c < 0x80) s[0] = (char)c, n = 1;
	else if (c < 0x800) s[0] = (char)(0xc0 | c >> 6), s[1] = (char)(0x80 | (c & 0x3f)), n = 2;
	else if (c < 0x10000) s[0] = (char)(0xe0 | c >> 12), s[1] = (char)(0x80 | (c >> 6 & 0x3f)), s[2] = (char)(0x80 | (c & 0x3f)), n = 3;
	else s[0] = (char)(0xf0 | c >> 18), s[1] = (char)(0x80 | (c >> 12 & 0x3f)), s[2] = (char)(0x80 | (c >> 6 & 0x3f)), s[3] = (char)(0x80 | (c & 0x3f)), n = 4;
	ak_strb_cstr(b, s, n);
}

// A character is one choice, in simplicity's order: a to z, the rest of
// printable ASCII, the control characters, then every other scalar by
// its code point (no surrogates).
static const char ascii[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
#define NASCII (sizeof ascii - 1)

static uint32_t char_of(uint64_t c) {
	if (c < NASCII) return (unsigned char)ascii[c];
	c -= NASCII;
	if (c < 32) return (uint32_t)c;
	c -= 32;
	if (c == 0) return 0x7f;
	c -= 1;
	uint64_t cp = 0x80 + c;
	if (cp >= 0xd800) cp += 0x800;
	return cp > 0x10ffff ? 0xfffd : (uint32_t)cp;
}

static uint64_t choice_of(uint32_t cp) {
	const char *p = cp && cp < 0x80 ? memchr(ascii, (int)cp, NASCII) : NULL;
	if (p) return (uint64_t)(p - ascii);
	if (cp < 32) return NASCII + cp;
	if (cp == 0x7f) return NASCII + 32;
	return NASCII + 33 + (cp - 0x80 - (cp >= 0xe000 ? 0x800 : 0));
}

// gen_char draws mostly letters and ASCII, then the rest of the BMP,
// then astral planes (emoji), then those that break naive code (NUL,
// line breaks, quotes, braces, a combining mark, a joiner, a BOM, a
// right-to-left override, CJK, Arabic).
static uint32_t gen_char(tape *t) {
	if (from_tape(t)) return char_of(keep(t, 0));
	static const uint32_t special[] = {0, '\n', '\t', '\r', '"', '\\', '{', '}', 0xe9, 0x301, 0x200d, 0xfeff, 0xfffd, 0x202e, 0x1f600, 0x1f44d, 0x4e2d, 0x3042, 0x627, 0x7f};
	uint64_t k = splitmix(&t->rng) % 100, r = splitmix(&t->rng);
	uint32_t cp;
	if (k < 40) cp = 'a' + (uint32_t)(r % 26);
	else if (k < 65) cp = (unsigned char)ascii[r % NASCII];
	else if (k < 80) cp = 0x80 + (uint32_t)(r % (0xd800 - 0x80));
	else if (k < 90) cp = 0x10000 + (uint32_t)(r % 0x100000);
	else cp = special[r % (sizeof special / sizeof *special)];
	return char_of(keep(t, choice_of(cp)));
}

static ak_str *gen_str(tape *t, int depth) {
	ak_strb b = {0};
	for (uint32_t n = 0; more(t, n, depth); n++) {
		put_utf8(&b, gen_char(t));
		if (t->budget) t->budget--;
	}
	return ak_strb_finish(&b);
}

static size_t name_len(const char *d) {
	const char *q = strchr(d + 1, ';');
	return q ? (size_t)(q - d - 1) : strlen(d + 1);
}

static const ak_shape *find_shape(tape *t, const char *d, const ak_json_types *ty) {
	const ak_shape *s = ty && ty->shape ? ty->shape(ty->ctx, d + 1, name_len(d)) : NULL;
	if (!s && !t->failed) {
		t->failed = true;
		snprintf(t->err, sizeof t->err, "asuka-run: no type %.*s to generate", (int)name_len(d), d + 1);
	}
	return s;
}

static void gen_fields(tape *t, ak_val *f, uint16_t n, const uint8_t *kinds, const ak_shape *s, int ci, const ak_json_types *ty, int depth) {
	extern const char *ak_json_field_desc(const ak_shape *s, int i);
	extern const char *ak_json_case_desc(const ak_shape *s, int c, int i);
	for (uint16_t i = 0; i < n; i++) {
		const char *fd = ci < 0 ? ak_json_field_desc(s, i) : ak_json_case_desc(s, ci, i);
		ak_release_val(f[i], (ak_kind)kinds[i]);
		f[i] = gen(t, fd, ty, depth + 1);
	}
}

static ak_val gen(tape *t, const char *d, const ak_json_types *ty, int depth) {
	if (t->budget) t->budget--;
	if (t->failed) return (ak_val){0};
	switch (*d) {
	case 'i':
		return (ak_val){.i = gen_int(t)};
	case 'f':
		return (ak_val){.f = gen_float(t)};
	case 'b':
		return (ak_val){.i = (int64_t)draw(t, 2)};
	case 's':
		return (ak_val){.p = gen_str(t, depth)};
	case 'y': {
		AK_VEC(uint8_t) by = {0};
		for (uint32_t n = 0; more(t, n, depth); n++) ak_vec_push(&by, (uint8_t)draw(t, 256));
		ak_bytes *b = ak_bytes_new(by.data, by.len);
		ak_free(by.data);
		return (ak_val){.p = b};
	}
	case '?': {
		static const uint16_t w[] = {1, 3};
		if (depth > MAX_DEPTH || t->budget == 0 || pick(t, w, 2) == 0) return (ak_val){0};
		ak_kind k = ak_desc_kind(d + 1);
		ak_val v = gen(t, d + 1, ty, depth + 1);
		if (k == AK_K_REF) return v;
		return (ak_val){.p = ak_box_new(k, v)};
	}
	case '[': {
		const char *ed = d + 1;
		const ak_shape *es = NULL;
		if (*ed == 'S' || *ed == 'E') es = find_shape(t, ed, ty);
		ak_arr *a = ak_arr_new(ak_desc_kind(ed), es, 0);
		if (*ed == 's') a->h.flags = 1;
		for (uint32_t n = 0; !t->failed && more(t, n, depth); n++) ak_arr_push(&a, gen(t, ed, ty, depth + 1));
		return (ak_val){.p = a};
	}
	case '<': {
		const char *ed = d + 1;
		ak_map *s = ak_set_new(ak_desc_kind(ed), *ed == 's');
		for (uint32_t n = 0; !t->failed && more(t, n, depth); n++) {
			ak_val x = gen(t, ed, ty, depth + 1);
			if (!ak_set_insert(&s, x)) ak_release_val(x, ak_desc_kind(ed));
		}
		return (ak_val){.p = s};
	}
	case '{': {
		const char *vd = d + 1;
		ak_map *m = ak_map_new(AK_K_REF, true, ak_desc_kind(vd), NULL);
		for (uint32_t n = 0; !t->failed && more(t, n, depth); n++) {
			ak_str *k = gen_str(t, depth + 1);
			ak_val v = gen(t, vd, ty, depth + 1);
			ak_map_set(&m, (ak_val){.p = k}, v);
		}
		return (ak_val){.p = m};
	}
	case 'S': {
		const ak_shape *s = find_shape(t, d, ty);
		if (!s) return (ak_val){0};
		ak_struct *o = ak_struct_new(s);
		gen_fields(t, o->f, s->nfields, s->kinds, s, -1, ty, depth);
		return (ak_val){.p = o};
	}
	case 'E': {
		const ak_shape *s = find_shape(t, d, ty);
		if (!s || !s->ncases) return (ak_val){0};
		uint16_t c = 0;
		if (depth > MAX_DEPTH || t->budget == 0) {
			// Deep enough: the case with the fewest fields, and no choice.
			for (uint16_t i = 1; i < s->ncases; i++)
				if (s->cases[i].nfields < s->cases[c].nfields) c = i;
		} else {
			c = (uint16_t)draw(t, s->ncases);
		}
		ak_enum *e = ak_enum_new(s, c);
		if (s->cases[c].nfields) gen_fields(t, e->f, s->cases[c].nfields, s->cases[c].kinds, s, c, ty, depth);
		return (ak_val){.p = e};
	}
	case 'e': {
		const char *const *cases;
		uint32_t n;
		if (!ty || !ty->cases || !ty->cases(ty->ctx, d + 1, name_len(d), &cases, &n) || !n) {
			find_shape(t, d, NULL);
			return (ak_val){0};
		}
		return (ak_val){.i = (int64_t)draw(t, n)};
	}
	}
	if (!t->failed) {
		t->failed = true;
		snprintf(t->err, sizeof t->err, "asuka-run: can't generate a value of %s", d);
	}
	return (ak_val){0};
}

// ---- A test's cases ----

typedef struct {
	ak_module *m;
	const char *name;
	const ak_func *f;
	uint32_t n;            // parameters
	const char **names;    // theirs
	const char **descs;
	ak_json_types types;
	uint64_t runs;         // cases run, shrinking's too
} prop;

static void release_args(prop *p, ak_val *args) {
	for (uint32_t i = 0; i < p->n; i++) ak_release_val(args[i], ak_desc_kind(p->descs[i]));
}

// make generates the parameters' values from t; false (and failure set)
// when it can't.
static bool make(prop *p, tape *t, ak_val *args) {
	t->budget = BUDGET;
	for (uint32_t i = 0; i < p->n; i++) args[i] = gen(t, p->descs[i], &p->types, 0);
	if (t->failed) {
		release_args(p, args);
		snprintf(failure, sizeof failure, "%s", t->err);
		return false;
	}
	return true;
}

static void quiet(const char *s, size_t n) {
	AK_UNUSED(s);
	AK_UNUSED(n);
}

// run_tape runs a case of the test with t's values: whether it failed
// (failure says how).
static bool run_tape(prop *p, tape *t) {
	ak_val args[64];
	failure[0] = 0;
	if (!make(p, t, args)) return true;
	p->runs++;
	ak_runner_run(p->f, args, p->n);
	release_args(p, args);
	return failure[0] != 0;
}

// shown writes the input t makes: a line "name = value" per parameter.
static void shown(prop *p, tape *t, ak_strb *b, bool json) {
	ak_val args[64];
	tape r = {.in = t->c.data, .nin = t->c.len};
	if (!make(p, &r, args)) {
		ak_free(r.c.data);
		return;
	}
	ak_json_types jt = p->types;
	jt.nonfinite = true;
	for (uint32_t i = 0; i < p->n; i++) {
		if (json) {
			ak_strb_cstr(b, i ? ",\n  " : "{\n  ", i ? 4 : 4);
			ak_jstr(b, p->names[i], strlen(p->names[i]));
			ak_strb_cstr(b, ": ", 2);
			ak_str *j = ak_json_encode(args[i], p->descs[i], &jt, 0);
			ak_strb_str(b, j);
			ak_release(j);
		} else {
			if (i) ak_strb_cstr(b, "\n", 1);
			ak_strb_cstr(b, p->names[i], strlen(p->names[i]));
			ak_strb_cstr(b, " = ", 3);
			ak_show(b, args[i], p->descs[i], &p->types);
		}
	}
	if (json) ak_strb_cstr(b, "\n}\n", 3);
	release_args(p, args);
	ak_free(r.c.data);
}

// ---- Shrinking ----

#define SHRINK_RUNS 5000

typedef struct {
	uint64_t *v;
	uint32_t n;
} choices;

// simpler reports whether a is simpler than b: shorter, or as long and
// smaller at its first difference.
static bool simpler(const uint64_t *a, uint32_t an, const uint64_t *b, uint32_t bn) {
	if (an != bn) return an < bn;
	for (uint32_t i = 0; i < an; i++)
		if (a[i] != b[i]) return a[i] < b[i];
	return false;
}

// try runs the candidate cand: when it fails and the tape it reads is
// simpler than best's, that's best now.
static bool try_(prop *p, choices *best, const uint64_t *cand, uint32_t n) {
	if (p->runs >= SHRINK_RUNS) return false;
	tape t = {.in = cand, .nin = n};
	bool fails = run_tape(p, &t);
	bool ok = fails && simpler(t.c.data, t.c.len, best->v, best->n);
	if (ok) {
		ak_free(best->v);
		best->v = t.c.data;
		best->n = t.c.len;
	} else {
		ak_free(t.c.data);
	}
	return ok;
}

static uint32_t shrink(prop *p, choices *best) {
	uint32_t steps = 0;
	uint64_t start = p->runs;
	uint64_t *cand = NULL;
	for (bool improved = true; improved && p->runs - start < SHRINK_RUNS;) {
		improved = false;
		cand = ak_realloc(cand, sizeof(uint64_t) * (best->n + 1));
		// Delete chunks of choices: shorter lists and strings, fewer cases.
		for (uint32_t size = 8; size >= 1; size /= 2) {
			for (int64_t i = (int64_t)best->n - size; i >= 0; i--) {
				if ((uint32_t)i + size > best->n) continue;
				uint32_t n = 0;
				for (uint32_t k = 0; k < best->n; k++)
					if (k < (uint32_t)i || k >= (uint32_t)i + size) cand[n++] = best->v[k];
				if (try_(p, best, cand, n)) improved = true, steps++;
			}
		}
		// Zero chunks: lists end, values are their simplest.
		for (uint32_t size = 8; size >= 1; size /= 2) {
			for (uint32_t i = 0; i + size <= best->n; i++) {
				bool any = false;
				for (uint32_t k = i; k < i + size; k++) any |= best->v[k] != 0;
				if (!any) continue;
				memcpy(cand, best->v, sizeof(uint64_t) * best->n);
				memset(cand + i, 0, sizeof(uint64_t) * size);
				if (try_(p, best, cand, best->n)) improved = true, steps++;
			}
		}
		// Each choice as small as still fails: first keeping its parity (a
		// zigzagged int's sign: failing is more often monotonic in its
		// size), then any.
		for (uint32_t i = 0; i < best->n; i++) {
			for (int pass = 0; pass < 2; pass++) {
				uint64_t parity = pass == 0 ? best->v[i] & 1 : 0;
				uint64_t lo = parity, hi = best->v[i];
				while (lo < hi && i < best->n) {
					uint64_t mid = lo + (hi - lo) / 2;
					if (pass == 0 && (mid & 1) != parity) mid = mid > lo ? mid - 1 : mid + 1;
					if (mid >= hi) break;
					memcpy(cand, best->v, sizeof(uint64_t) * best->n);
					cand[i] = mid;
					if (try_(p, best, cand, best->n)) {
						improved = true, steps++;
						hi = i < best->n ? best->v[i] : 0;
						if (hi > mid) hi = mid;
					} else {
						lo = mid + 1;
						if (pass == 0 && (lo & 1) != parity) lo++;
					}
				}
			}
			// What failing isn't monotonic in (a zigzagged int: 1002 fails,
			// 1001's neighbour -1001 doesn't): a few steps down past it.
			for (int k = 0; k < 64 && i < best->n && best->v[i] > 0; k++) {
				bool down = false;
				for (uint64_t d = 1; d <= 2 && d <= best->v[i] && !down; d++) {
					memcpy(cand, best->v, sizeof(uint64_t) * best->n);
					cand[i] -= d;
					down = try_(p, best, cand, best->n);
				}
				if (!down) break;
				improved = true, steps++;
			}
		}
	}
	ak_free(cand);
	return steps;
}

// ---- Reporting ----

// report writes the failing input t made as a JSON line asuka test reads
// ({"property": name, "seed", "case", "shrinks", "input": JSON, "shown":
// text, "fuzz", "corpus"}), runs it once more (what it prints shown) for
// its failure's message.
static void report(prop *p, choices *c, uint64_t seed, int64_t index, uint32_t steps, bool fuzz, const char *corpus) {
	tape t = {.c = {c->v, c->n, c->n}};
	ak_strb b = {0};
	ak_strb_cstr(&b, "{\"property\":", 12);
	ak_jstr(&b, p->name, strlen(p->name));
	if (corpus) {
		ak_strb_cstr(&b, ",\"corpus\":", 10);
		ak_jstr(&b, corpus, strlen(corpus));
	} else {
		char sd[24];
		int n = snprintf(sd, sizeof sd, "%llu", (unsigned long long)seed);
		ak_strb_cstr(&b, ",\"seed\":", 8);
		ak_strb_cstr(&b, sd, (size_t)n);
		ak_strb_cstr(&b, ",\"case\":", 8);
		ak_strb_int(&b, index);
		ak_strb_cstr(&b, ",\"shrinks\":", 11);
		ak_strb_int(&b, steps);
	}
	if (fuzz) ak_strb_cstr(&b, ",\"fuzz\":true", 12);
	ak_strb in = {0}, sh = {0};
	shown(p, &t, &in, true);
	shown(p, &t, &sh, false);
	ak_strb_cstr(&b, ",\"input\":", 9);
	ak_jstr(&b, (const char *)in.b.data, in.b.len);
	ak_strb_cstr(&b, ",\"shown\":", 9);
	ak_jstr(&b, (const char *)sh.b.data, sh.b.len);
	ak_buf_free(&in.b);
	ak_buf_free(&sh.b);
	// Its run again, what it prints said: the failure's message is its.
	void (*hook)(const char *, size_t) = ak_lib_print_hook;
	ak_lib_print_hook = NULL;
	tape r = {.in = c->v, .nin = c->n};
	run_tape(p, &r);
	ak_free(r.c.data);
	ak_lib_print_hook = hook;
	if (!failure[0]) snprintf(failure, sizeof failure, "it failed, then passed with the same input: it depends on more than its parameters");
	ak_strb_cstr(&b, "}", 1);
	ak_jline(&b);
}

// ---- The corpus: inputs kept as JSON ----

// skip_json moves past one JSON value (or what passes for one: NaN).
static const char *skip_json(const char *s, const char *end) {
	while (s < end && (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r')) s++;
	if (s >= end) return s;
	if (*s == '"') {
		for (s++; s < end && *s != '"'; s++)
			if (*s == '\\') s++;
		return s < end ? s + 1 : s;
	}
	if (*s == '[' || *s == '{') {
		int depth = 0;
		for (; s < end; s++) {
			if (*s == '"') {
				s = skip_json(s, end) - 1;
				continue;
			}
			if (*s == '[' || *s == '{') depth++;
			if (*s == ']' || *s == '}') {
				if (--depth == 0) return s + 1;
			}
		}
		return s;
	}
	while (s < end && *s != ',' && *s != '}' && *s != ']' && *s != ' ' && *s != '\n' && *s != '\r' && *s != '\t') s++;
	return s;
}

// corpus_args reads an input's parameters from its JSON object, by name;
// false, failure set, when it can't.
static bool corpus_args(prop *p, const char *text, size_t len, ak_val *args) {
	const char *s = text, *end = text + len;
	const char **at = ak_alloc(sizeof(char *) * (p->n ? p->n : 1));
	size_t *n = ak_alloc(sizeof(size_t) * (p->n ? p->n : 1));
	bool ok = true;
	while (s < end && *s != '{') s++;
	if (s < end) s++;
	while (s < end) {
		while (s < end && *s != '"' && *s != '}') s++;
		if (s >= end || *s == '}') break;
		const char *k = s + 1, *ke = skip_json(s, end) - 1;
		s = ke + 1;
		while (s < end && *s != ':') s++;
		s++;
		const char *v = s;
		s = skip_json(s, end);
		for (uint32_t i = 0; i < p->n; i++)
			if (strlen(p->names[i]) == (size_t)(ke - k) && memcmp(p->names[i], k, (size_t)(ke - k)) == 0) at[i] = v, n[i] = (size_t)(s - v);
		while (s < end && *s != ',' && *s != '}') s++;
		if (s < end && *s == ',') s++;
	}
	ak_json_types jt = p->types;
	jt.nonfinite = true;
	uint32_t made = 0;
	for (uint32_t i = 0; i < p->n && ok; i++, made++) {
		if (!at[i]) {
			snprintf(failure, sizeof failure, "its input has no %s", p->names[i]);
			ok = false;
			break;
		}
		args[i] = ak_json_decode(at[i], n[i], p->descs[i], &jt);
		if (ak_error_pending()) {
			void *e = ak_error_take();
			snprintf(failure, sizeof failure, "its input's %s: %s", p->names[i], ak_error_message(e)->data);
			ak_release(e);
			ok = false;
		}
	}
	if (!ok)
		for (uint32_t i = 0; i < made; i++) ak_release_val(args[i], ak_desc_kind(p->descs[i]));
	ak_free(at);
	ak_free(n);
	return ok;
}

static char *read_all(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *p = ak_alloc((size_t)len + 1);
	*n = fread(p, 1, (size_t)len, f);
	fclose(f);
	return p;
}

// run_corpus runs the inputs the corpus file lists for the test; false
// when one fails (reported).
static bool run_corpus(prop *p, const char *list) {
	if (!list) return true;
	size_t n;
	char *all = read_all(list, &n);
	if (!all) return true;
	bool ok = true;
	size_t nl = strlen(p->name);
	for (char *line = all; ok && line < all + n;) {
		char *eol = memchr(line, '\n', (size_t)(all + n - line));
		if (!eol) eol = all + n;
		*eol = 0;
		if ((size_t)(eol - line) > nl && memcmp(line, p->name, nl) == 0 && line[nl] == '\t') {
			const char *path = line + nl + 1;
			size_t len;
			char *text = read_all(path, &len);
			ak_val args[64];
			failure[0] = 0;
			if (text && corpus_args(p, text, len, args)) {
				ak_runner_run(p->f, args, p->n);
				if (failure[0]) {
					ak_strb b = {0};
					char msg[sizeof failure];
					snprintf(msg, sizeof msg, "%s", failure);
					ak_strb_cstr(&b, "{\"property\":", 12);
					ak_jstr(&b, p->name, strlen(p->name));
					ak_strb_cstr(&b, ",\"corpus\":", 10);
					ak_jstr(&b, path, strlen(path));
					ak_strb_cstr(&b, ",\"input\":", 9);
					ak_jstr(&b, text, len);
					ak_strb sh = {0};
					for (uint32_t i = 0; i < p->n; i++) {
						if (i) ak_strb_cstr(&sh, "\n", 1);
						ak_strb_cstr(&sh, p->names[i], strlen(p->names[i]));
						ak_strb_cstr(&sh, " = ", 3);
						ak_show(&sh, args[i], p->descs[i], &p->types);
					}
					ak_strb_cstr(&b, ",\"shown\":", 9);
					ak_jstr(&b, (const char *)sh.b.data, sh.b.len);
					ak_buf_free(&sh.b);
					ak_strb_cstr(&b, "}", 1);
					ak_jline(&b);
					snprintf(failure, sizeof failure, "%s", msg);
					ok = false;
				}
				release_args(p, args);
			} else if (failure[0]) {
				// The file's path, before what's wrong with it.
				size_t pl = strlen(path), fl = strlen(failure);
				if (pl + 2 + fl >= sizeof failure) fl = pl + 2 < sizeof failure ? sizeof failure - pl - 3 : 0;
				if (pl + 2 < sizeof failure) {
					memmove(failure + pl + 2, failure, fl);
					memcpy(failure, path, pl);
					memcpy(failure + pl, ": ", 2);
					failure[pl + 2 + fl] = 0;
				}
				ok = false;
			}
			ak_free(text);
		}
		line = eol + 1;
	}
	ak_free(all);
	return ok;
}

// ---- Fuzzing ----

// bucket is a count as AFL buckets them: 0, 1, 2, 3, 4-7, 8-15, 16-31,
// 32-127, 128+.
static uint8_t bucket(uint64_t n) {
	if (n < 4) return (uint8_t)n;
	if (n < 8) return 4;
	if (n < 16) return 5;
	if (n < 32) return 6;
	if (n < 128) return 7;
	return 8;
}

typedef struct {
	AK_VEC(choices) pool;  // the tapes that reached new code
	uint8_t *seen;         // per counter: the highest bucket a case reached
	uint32_t nseen;
	uint64_t *before;
} fuzzer;

// novel reports whether the case just run reached code, or reached it a
// number of times, no case before it did.
static bool novel(fuzzer *z) {
	uint32_t n;
	const uint64_t *c = ak_cover_counts(&n);
	if (n > z->nseen) {
		z->seen = ak_realloc(z->seen, n);
		memset(z->seen + z->nseen, 0, n - z->nseen);
		z->before = ak_realloc(z->before, sizeof(uint64_t) * n);
		memset(z->before + z->nseen, 0, sizeof(uint64_t) * (n - z->nseen));
		z->nseen = n;
	}
	bool found = false;
	for (uint32_t i = 0; i < n; i++) {
		uint8_t bk = bucket(c[i] - z->before[i]);
		if (bk > z->seen[i]) z->seen[i] = bk, found = true;
		z->before[i] = c[i];
	}
	return found;
}

// mark starts counting from the counters as they are.
static void mark(fuzzer *z) {
	uint32_t n;
	const uint64_t *c = ak_cover_counts(&n);
	if (n > z->nseen) {
		z->seen = ak_realloc(z->seen, n);
		memset(z->seen + z->nseen, 0, n - z->nseen);
		z->before = ak_realloc(z->before, sizeof(uint64_t) * n);
		z->nseen = n;
	}
	if (n) memcpy(z->before, c, sizeof(uint64_t) * n);
}

// mutate makes a tape from one the pool has: values changed, chunks
// deleted, repeated, spliced with another's.
static choices mutate(fuzzer *z, uint64_t *rng) {
	choices *from = &z->pool.data[splitmix(rng) % z->pool.len];
	uint32_t cap = from->n * 2 + 16;
	choices out = {ak_alloc(sizeof(uint64_t) * cap), from->n};
	if (from->n) memcpy(out.v, from->v, sizeof(uint64_t) * from->n);
	uint32_t times = 1 + (uint32_t)(splitmix(rng) % 4);
	for (uint32_t k = 0; k < times; k++) {
		uint32_t n = out.n;
		uint64_t r = splitmix(rng);
		switch (r % 6) {
		case 0:
			if (n) out.v[splitmix(rng) % n] = splitmix(rng);
			break;
		case 1:
			if (n) out.v[splitmix(rng) % n] = splitmix(rng) % 16;
			break;
		case 2:
			if (n) {
				uint32_t i = (uint32_t)(splitmix(rng) % n), len = 1 + (uint32_t)(splitmix(rng) % 8);
				if (i + len > n) len = n - i;
				if (n - i - len) memmove(out.v + i, out.v + i + len, sizeof(uint64_t) * (n - i - len));
				out.n -= len;
			}
			break;
		case 3:
			if (n && n + 8 <= cap) {
				uint32_t i = (uint32_t)(splitmix(rng) % n), len = 1 + (uint32_t)(splitmix(rng) % 8);
				if (i + len > n) len = n - i;
				memmove(out.v + i + len, out.v + i, sizeof(uint64_t) * (n - i));
				out.n += len;
			}
			break;
		case 4: {
			choices *other = &z->pool.data[splitmix(rng) % z->pool.len];
			uint32_t i = n ? (uint32_t)(splitmix(rng) % n) : 0, j = other->n ? (uint32_t)(splitmix(rng) % other->n) : 0;
			uint32_t len = other->n - j;
			if (i + len > cap) len = cap - i;
			if (len) memcpy(out.v + i, other->v + j, sizeof(uint64_t) * len);
			out.n = i + len;
			break;
		}
		default:
			if (n) {
				uint32_t i = (uint32_t)(splitmix(rng) % n);
				out.v[i] += (r >> 8) & 1 ? 1 : (uint64_t)-1;
			}
		}
	}
	return out;
}

static void progress(prop *p, const char *what, uint64_t cases, uint32_t interesting, int64_t start) {
	ak_strb b = {0};
	ak_strb_cstr(&b, "{\"", 2);
	ak_strb_cstr(&b, what, strlen(what));
	ak_strb_cstr(&b, "\":", 2);
	ak_jstr(&b, p->name, strlen(p->name));
	ak_strb_cstr(&b, ",\"cases\":", 9);
	ak_strb_int(&b, (int64_t)cases);
	ak_strb_cstr(&b, ",\"interesting\":", 15);
	ak_strb_int(&b, interesting);
	ak_strb_cstr(&b, ",\"seconds\":", 11);
	ak_strb_float(&b, (double)(ak_now_ns() - start) / 1e9);
	ak_strb_cstr(&b, "}", 1);
	ak_jline(&b);
}

static void fuzz(prop *p, const ak_prop_options *o, uint64_t seed) {
	fuzzer z = {0};
	uint64_t rng = seed;
	int64_t start = ak_now_ns(), last = start;
	uint64_t cases = 0;
	mark(&z);
	for (;;) {
		if (o->fuzz_cases && cases >= o->fuzz_cases) break;
		if (o->fuzz_ns && ak_now_ns() - start >= o->fuzz_ns) break;
		if (!o->fuzz_cases && !o->fuzz_ns) break;
		tape t = {.rng = splitmix(&rng)};
		choices m = {0};
		if (z.pool.len && splitmix(&rng) % 4) {
			m = mutate(&z, &rng);
			t.in = m.v, t.nin = m.n, t.extend = true;
		}
		bool fails = run_tape(p, &t);
		cases++;
		ak_free(m.v);
		if (fails) {
			choices best = {t.c.data, t.c.len};
			uint64_t before = p->runs;
			p->runs = 0;
			uint32_t steps = shrink(p, &best);
			p->runs += before;
			report(p, &best, seed, (int64_t)cases, steps, true, NULL);
			ak_free(best.v);
			break;
		}
		if (novel(&z) && z.pool.len < 512) {
			ak_vec_push(&z.pool, ((choices){t.c.data, t.c.len}));
		} else {
			ak_free(t.c.data);
		}
		if (ak_now_ns() - last >= 2000000000LL) {
			last = ak_now_ns();
			progress(p, "fuzzing", cases, z.pool.len, start);
		}
	}
	if (!failure[0]) progress(p, "fuzzed", cases, z.pool.len, start);
	for (uint32_t i = 0; i < z.pool.len; i++) ak_free(z.pool.data[i].v);
	ak_free(z.pool.data);
	ak_free(z.seen);
	ak_free(z.before);
}

// ---- Running one ----

static uint64_t fnv(const char *s) {
	uint64_t h = 0xcbf29ce484222325ULL;
	for (; *s; s++) h = (h ^ (unsigned char)*s) * 0x100000001b3ULL;
	return h;
}

void ak_prop_run(ak_module *m, uint32_t i, const ak_prop_options *o) {
	uint32_t fi;
	prop p = {.m = m, .name = ak_module_test(m, i, &fi)};
	p.f = ak_module_func(m, fi);
	p.n = ak_module_test_nparams(m, i);
	if (p.n > 64) {
		snprintf(failure, sizeof failure, "a property test takes at most 64 parameters");
		return;
	}
	p.names = ak_alloc(sizeof(char *) * (p.n ? p.n : 1));
	p.descs = ak_alloc(sizeof(char *) * (p.n ? p.n : 1));
	for (uint32_t k = 0; k < p.n; k++) p.descs[k] = ak_module_test_param(m, i, k, &p.names[k]);
	ak_runner_types(&p.types);
	failure[0] = 0;
	// What the cases print isn't said: a case that fails is run again,
	// and its output is.
	void (*hook)(const char *, size_t) = ak_lib_print_hook;
	ak_lib_print_hook = quiet;
	bool diags = ak_diag_enabled;
	uint64_t s = o->seed ^ fnv(p.name);
	uint64_t seed = splitmix(&s);
	if (run_corpus(&p, o->corpus)) {
		if (o->fuzz) {
			fuzz(&p, o, seed);
		} else {
			uint64_t rng = seed;
			for (uint32_t k = 0; k < o->cases; k++) {
				tape t = {.rng = splitmix(&rng)};
				bool fails = run_tape(&p, &t);
				ak_diag_enabled = false;  // the first case's say what they all would
				if (fails) {
					choices best = {t.c.data, t.c.len};
					p.runs = 0;
					uint32_t steps = shrink(&p, &best);
					ak_diag_enabled = diags;
					report(&p, &best, o->seed, (int64_t)k + 1, steps, false, NULL);
					ak_free(best.v);
					break;
				}
				ak_free(t.c.data);
			}
		}
	}
	ak_diag_enabled = diags;
	ak_lib_print_hook = hook;
	ak_free(p.names);
	ak_free(p.descs);
}
