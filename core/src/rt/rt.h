// rt.h: the values Asuka programs compute with, shared by the bytecode
// VM (core/src/vm) and the C that release builds compile to (lang/cgen).
//
// A value is 8 bytes (ak_val). Its type is known statically, so a value
// carries no tag: the compiler knows whether a slot holds an int, a
// float, a bool or a reference to a heap object (ak_obj), and emits the
// matching operations. Heap objects are reference counted; values with
// value semantics (strings, bytes, arrays, maps, structs, tuples, enums
// with payloads, boxed optionals) are copied on write: a mutation of an
// object someone else also holds copies it first, so nobody else sees it.
//
// Everything here runs on the main thread, except the functions that say
// otherwise (ak_deep_copy, used to cross into spawn's worker threads).
#ifndef AK_RT_H
#define AK_RT_H

#include "base/base.h"

typedef union ak_val {
	int64_t i;  // int, bool (0/1), Duration (ns), Color, payloadless enums
	double f;   // float
	void *p;    // a heap object, NULL for none
} ak_val;

// How a slot is stored: what the runtime needs to release, copy, compare
// and hash a value without knowing its full type.
typedef enum ak_kind {
	AK_K_VOID = 0,
	AK_K_INT,
	AK_K_FLOAT,
	AK_K_BOOL,
	AK_K_REF,  // a heap object or NULL
} ak_kind;

// AK_K_U64TEXT is never a slot's kind (a u64 is an int's 64 bits): what
// TOSTR is told of a u64, which it writes unsigned.
#define AK_K_U64TEXT 5

// ---- Integers ----
//
// The integer types, by their code (lang/check's IntCode): log2 of their
// bytes, 4 more when they're unsigned. A value of one is held as an int,
// sign- or zero-extended from its width (canonical: a u8 is 0 to 255), a
// u64 as its 64 bits (past int's largest, a negative int64).
typedef enum ak_int_type {
	AK_I8,
	AK_I16,
	AK_I32,
	AK_I64,  // int
	AK_U8,
	AK_U16,
	AK_U32,
	AK_U64,
} ak_int_type;

extern const char *const ak_int_names[8];  // "i8" … "u64", "int" for AK_I64

static inline bool ak_int_signed(int t) { return t < AK_U8; }

// ak_int_wrap is v's low bits at type t, sign- or zero-extended.
static inline int64_t ak_int_wrap(int64_t v, int t) {
	switch (t) {
	case AK_I8: return (int8_t)v;
	case AK_I16: return (int16_t)v;
	case AK_I32: return (int32_t)v;
	case AK_U8: return (uint8_t)v;
	case AK_U16: return (uint16_t)v;
	case AK_U32: return (uint32_t)v;
	default: return v;
	}
}

// ak_int_min and ak_int_max are type t's range (a u64's largest as its
// bits: -1).
static inline int64_t ak_int_min(int t) { return ak_int_signed(t) ? (t == AK_I64 ? INT64_MIN : -((int64_t)1 << ((8 << t) - 1))) : 0; }
static inline int64_t ak_int_max(int t) {
	if (t == AK_U64 || t == AK_I64) return t == AK_U64 ? -1 : INT64_MAX;
	return ak_int_signed(t) ? ((int64_t)1 << ((8 << t) - 1)) - 1 : ((int64_t)1 << (8 << (t & 3))) - 1;
}

// ak_int_fits reports whether v, a value of type from, is one of type to.
static inline bool ak_int_fits(int64_t v, int from, int to) {
	if (v < 0 && (from == AK_U64 || to == AK_U64)) return from == to;  // a u64 past int's largest, or a negative
	return ak_int_wrap(v, to) == v;
}

// ak_int_clamp is v, of type from, as the nearest value of type to.
static inline int64_t ak_int_clamp(int64_t v, int from, int to) {
	if (ak_int_fits(v, from, to)) return v;
	return v < 0 && from != AK_U64 ? ak_int_min(to) : ak_int_max(to);
}

// ak_int_lt orders two values of type t.
static inline bool ak_int_lt(int64_t a, int64_t b, int t) { return t == AK_U64 ? (uint64_t)a < (uint64_t)b : a < b; }

// ak_int_mul is a * b at type t, false when it overflows.
static inline bool ak_int_mul(int64_t a, int64_t b, int t, int64_t *out) {
	if (t == AK_U64) {
		uint64_t u;
		if (__builtin_mul_overflow((uint64_t)a, (uint64_t)b, &u)) return false;
		*out = (int64_t)u;
		return true;
	}
	return !__builtin_mul_overflow(a, b, out) && ak_int_wrap(*out, t) == *out;
}

// ak_int_pow is b ** e at type t (e as unsigned: a count), false when it
// overflows. By squaring: one multiplication per bit of e, the last
// square not taken (it would overflow where the result doesn't).
static inline bool ak_int_pow(int64_t b, uint64_t e, int t, int64_t *out) {
	int64_t r = 1;
	if (b == 0 || b == 1) {
		*out = e == 0 ? 1 : b;
		return true;
	}
	if (b == -1 && t != AK_U64) {
		*out = e & 1 ? -1 : 1;
		return true;
	}
	for (;;) {
		if ((e & 1) && !ak_int_mul(r, b, t, &r)) return false;
		e >>= 1;
		if (!e) break;
		if (!ak_int_mul(b, b, t, &b)) return false;
	}
	*out = r;
	return true;
}

// ak_int_parse reads text (n bytes) whole as an integer of type t in
// radix (2 to 36, letters in either case), an optional sign first: false
// when it isn't one, or doesn't fit.
bool ak_int_parse(const char *text, size_t n, int radix, int t, int64_t *out);
// ak_int_text writes v, of type t, in radix (2 to 36, lowercase) into
// buf (66 bytes hold any): its length.
int ak_int_text(char *buf, int64_t v, int t, int radix);
// ak_int_throw_fit throws that v, of type from, doesn't fit type to.
void ak_int_throw_fit(int64_t v, int from, int to);

// A heap object's tag.
typedef enum ak_tag {
	AK_TAG_STR = 1,
	AK_TAG_BYTES,
	AK_TAG_ARR,
	AK_TAG_MAP,
	AK_TAG_STRUCT,   // structs, tuples, errors
	AK_TAG_ENUM,     // a case of an enum with payloads
	AK_TAG_BOX,      // an optional scalar that isn't none
	AK_TAG_CLOSURE,
	AK_TAG_CELL,     // a state cell (vm, ui)
	AK_TAG_MEMO,
	AK_TAG_ACC,      // an accessor: what a component's parameter reads
	AK_TAG_STORE,
	AK_TAG_TASK,
	AK_TAG_VIEW,
	AK_TAG_NATIVE,   // native/native.h's objects
	AK_TAG_OPAQUE,   // runtime objects with their own destructor
} ak_tag;

typedef struct ak_obj {
	uint32_t rc;
	uint8_t tag;
	uint8_t flags;
	uint16_t aux;  // per tag: an array's element kind, a box's kind
} ak_obj;

#define AK_RC_IMMORTAL 0xffffffffu

void ak_destroy(ak_obj *o);

// ak_obj_live counts the objects made and not yet destroyed (immortal
// ones aside: constants, never destroyed), so tests can tell that what code made is freed: a
// runner's -leaks. Atomic: workers (spawn) make objects too. Counted in a
// build with assertions (NDEBUG unset), as ak_alloc_count is.
extern int64_t ak_obj_live;
#ifndef NDEBUG
#define ak_obj_count(d) __atomic_add_fetch(&ak_obj_live, (d), __ATOMIC_RELAXED)
#else
#define ak_obj_count(d) ((void)0)
#endif

static inline void *ak_retain(void *p) {
	if (p) {
		ak_obj *o = (ak_obj *)p;
		if (o->rc != AK_RC_IMMORTAL) o->rc++;
	}
	return p;
}

static inline void ak_release(void *p) {
	if (p) {
		ak_obj *o = (ak_obj *)p;
		if (o->rc != AK_RC_IMMORTAL && --o->rc == 0) ak_destroy(o);
	}
}

static inline void ak_release_val(ak_val v, ak_kind k) {
	if (k == AK_K_REF) ak_release(v.p);
}

static inline void ak_retain_val(ak_val v, ak_kind k) {
	if (k == AK_K_REF) ak_retain(v.p);
}

// ak_unique reports whether the caller holds the only reference, so it
// may change the object in place.
static inline bool ak_unique(const void *p) { return ((const ak_obj *)p)->rc == 1; }

void *ak_obj_new(ak_tag tag, size_t size);  // rc 1, zeroed past the header
void ak_make_immortal(void *p);

// ---- Shapes: the layout of a struct, a tuple or an enum's cases ----

typedef struct ak_shape ak_shape;

typedef struct ak_case {
	const char *name;
	uint16_t nfields;
	const uint8_t *kinds;          // ak_kind per payload field
	const ak_shape *const *shapes; // per field, the shape of a struct/enum field, or NULL
	const char *const *names;      // field labels, or NULL
} ak_case;

struct ak_shape {
	const char *name;
	uint16_t nfields;  // struct: its fields; enum: 0
	const uint8_t *kinds;
	const ak_shape *const *shapes;
	const char *const *names;
	uint16_t ncases;   // enums with payloads
	const ak_case *cases;
	bool is_error;     // declared `: Error`; field 0 is message when it has one
	void *singletons;  // runtime: payloadless cases of a payload enum (ak_enum **)
};

// ---- Strings ----

typedef struct ak_str {
	ak_obj h;       // h.flags: AK_STR_* (what's known of its bytes)
	uint32_t len;
	uint32_t hash;  // 0: not computed yet
	char data[];    // len bytes, then a NUL
} ak_str;

// What a string's h.flags cache of its bytes, as its hash is cached:
// worked out when first asked for (ak_str_ascii), and before it becomes
// immortal (ak_str_hash), so worker threads only read them. Code that
// changes a string's bytes after it's made (it's the caller's, fresh
// from ak_str_new(NULL, n)) does so before anything reads them.
#define AK_STR_KNOWN 1  // AK_STR_ASCII says whether it's ASCII
#define AK_STR_ASCII 2  // every byte below 0x80: a character a byte

ak_str *ak_str_new(const char *s, size_t n);  // s NULL: n bytes the caller fills
static inline ak_str *ak_str_c(const char *s) { return ak_str_new(s, strlen(s)); }
ak_str *ak_str_empty(void);  // immortal
ak_str *ak_str_concat(ak_str *a, ak_str *b);
ak_str *ak_str_from_int(int64_t v);
ak_str *ak_str_from_uint(uint64_t v);
ak_str *ak_str_from_float(double v);  // shortest round-trip form
// ak_float_text writes v as ak_str_from_float does into buf (32 bytes
// room); its length. No allocation.
int ak_float_text(char *buf, double v);
ak_str *ak_str_from_bool(bool v);
bool ak_str_eq(const ak_str *a, const ak_str *b);
int ak_str_cmp(const ak_str *a, const ak_str *b);
uint32_t ak_str_hash(ak_str *s);
// ak_hash_bytes: n bytes' hash, wyhash's (final version 4): eight bytes
// a multiply, well mixed in every bit. Pure: any thread.
uint64_t ak_hash_bytes(const void *p, size_t n);
ak_str *ak_str_slice(ak_str *s, int64_t from, int64_t to);  // byte offsets, clamped
int64_t ak_str_count(const ak_str *s);   // characters (code points)
bool ak_str_ascii(const ak_str *s);      // whether every byte is ASCII (cached)
// ak_str_char_offset: the byte offset of character c (code point), 0 for
// c <= 0, len past the last.
int64_t ak_str_char_offset(const ak_str *s, int64_t c);
int64_t ak_str_find(const ak_str *s, const ak_str *sub);  // byte offset or -1
int64_t ak_str_find_last(const ak_str *s, const ak_str *sub);  // byte offset or -1
// ak_mem_find: where needle (nn bytes) first is in h (hn bytes), or -1:
// in time linear in hn (a rare byte's memchr, the two-way algorithm
// when that finds too many false starts).
int64_t ak_mem_find(const void *h, size_t hn, const void *needle, size_t nn);
int64_t ak_mem_find_last(const void *h, size_t hn, const void *needle, size_t nn);
bool ak_str_has_prefix(const ak_str *s, const ak_str *p);
bool ak_str_has_suffix(const ak_str *s, const ak_str *p);
bool ak_str_contains(const ak_str *s, const ak_str *sub);
ak_str *ak_str_lower(ak_str *s);
ak_str *ak_str_upper(ak_str *s);
ak_str *ak_str_fold(ak_str *s);  // Unicode's full case folding
// Without case (full case folding, rt/unicode.h; no normalization):
int ak_str_cmp_fold(const ak_str *a, const ak_str *b);
bool ak_str_eq_fold(const ak_str *a, const ak_str *b);
bool ak_str_has_prefix_fold(const ak_str *s, const ak_str *p);
bool ak_str_has_suffix_fold(const ak_str *s, const ak_str *p);
bool ak_str_contains_fold(const ak_str *s, const ak_str *sub);
ak_str *ak_str_trim(ak_str *s);
ak_str *ak_str_replace(ak_str *s, const ak_str *old, const ak_str *new_);
ak_str *ak_str_replace_n(ak_str *s, const ak_str *old, const ak_str *new_, int64_t count);  // the first count (all: < 0)
struct ak_arr *ak_str_split(ak_str *s, const ak_str *sep);
// ak_str_split_n: at most count parts (all: < 0), the last the rest; an
// empty sep, the characters (count aside: lib.c's split cuts them).
struct ak_arr *ak_str_split_n(ak_str *s, const ak_str *sep, int64_t count);
bool ak_str_to_int(const ak_str *s, int64_t *out);
bool ak_str_to_float(const ak_str *s, double *out);

// A builder for interpolated strings: no intermediate strings. Zero is
// an empty one; what it holds is in small until it outgrows it, then in
// b, so a short string costs one allocation, the result's.
typedef struct ak_strb {
	ak_buf b;
	uint32_t n;      // bytes in small, while b.data is NULL
	char small[116];
} ak_strb;
void ak_strb_str(ak_strb *b, const ak_str *s);
void ak_strb_cstr(ak_strb *b, const char *s, size_t n);
void ak_strb_int(ak_strb *b, int64_t v);
void ak_strb_uint(ak_strb *b, uint64_t v);
void ak_strb_float(ak_strb *b, double v);
void ak_strb_bool(ak_strb *b, bool v);
void ak_strb_char(ak_strb *b, char c);
// ak_strb_room: room for n more bytes at the returned pointer, which
// ak_strb_wrote(b, k) then says k of were written.
char *ak_strb_room(ak_strb *b, size_t n);
static inline void ak_strb_wrote(ak_strb *b, size_t k) {
	if (b->b.data) b->b.len += k;
	else b->n += (uint32_t)k;
}
static inline size_t ak_strb_len(const ak_strb *b) { return b->b.data ? b->b.len : b->n; }
// ak_strb_buf: what b holds, all of it in b->b (moved there from small),
// for code that reads or takes the buffer itself; b goes on taking more.
ak_buf *ak_strb_buf(ak_strb *b);
ak_str *ak_strb_finish(ak_strb *b);  // frees the builder's buffer
void ak_strb_free(ak_strb *b);       // drops what it holds

// ---- Bytes ----

typedef struct ak_bytes {
	ak_obj h;
	uint32_t len;
	uint8_t data[];
} ak_bytes;

ak_bytes *ak_bytes_new(const void *p, size_t n);  // p NULL: n bytes, uninitialized

// ---- Arrays ----

typedef struct ak_arr {
	ak_obj h;  // h.aux: the element kind; h.flags: 1 when elements are strings (for equality)
	// Its buffer: front free slots, then cap slots from data (its len
	// elements, then free ones). Changes at the start move data within
	// it, so an array is a double-ended queue: pushing and popping at
	// either end is O(1), amortized (arr.c).
	uint32_t len, cap, front;
	const ak_shape *shape;  // the elements' shape when they're structs or enums
	ak_val *data;
} ak_arr;

ak_arr *ak_arr_new(ak_kind elem, const ak_shape *shape, uint32_t cap);
static inline ak_kind ak_arr_kind(const ak_arr *a) { return (ak_kind)a->h.aux; }
// ak_arr_mut makes *a unique (copying it if it's shared) and returns it.
ak_arr *ak_arr_mut(ak_arr **a);
void ak_arr_push(ak_arr **a, ak_val v);              // takes v's reference
void ak_arr_reserve(ak_arr **a, uint32_t n);         // room for n more, made unique
void ak_arr_insert(ak_arr **a, int64_t i, ak_val v); // takes v's reference
ak_val ak_arr_remove_at(ak_arr **a, int64_t i);      // returns the reference
void ak_arr_set(ak_arr **a, int64_t i, ak_val v);    // takes v's reference
static inline ak_val *ak_arr_base(const ak_arr *a) { return a->data ? a->data - a->front : NULL; }  // what's allocated
bool ak_arr_check(const ak_arr *a, int64_t i);       // in range
ak_arr *ak_arr_slice(ak_arr *a, int64_t from, int64_t to);
ak_arr *ak_arr_concat(ak_arr *a, ak_arr *b);
ak_arr *ak_arr_copy(const ak_arr *a);

// ---- Maps: insertion ordered, keys int or string ----

#define AK_MAP_STRKEYS 1  // h.flags: the keys are strings
#define AK_MAP_SET 2      // h.flags: a set, its values void (ak_set_*)

typedef struct ak_map {
	ak_obj h;  // h.aux: key kind | value kind << 4; h.flags: AK_MAP_STRKEYS, AK_MAP_SET
	uint32_t len, cap;   // entries used, entries allocated (includes deleted)
	uint32_t live;       // entries not deleted
	uint32_t nslots;     // index slots (a power of 2)
	ak_val *keys, *vals;
	uint8_t *dead;
	uint64_t *slots;     // map.c's index: the key's hash << 32 | entry + 1 (0 empty)
	const ak_shape *vshape;
} ak_map;

ak_map *ak_map_new(ak_kind key, bool string_keys, ak_kind val, const ak_shape *vshape);
ak_map *ak_map_mut(ak_map **m);
bool ak_map_get(const ak_map *m, ak_val key, ak_val *out);  // borrowed
void ak_map_set(ak_map **m, ak_val key, ak_val v);          // takes both references
// ak_map_put_new sets key's value unless it has one (then releasing key
// and v, the map as it was): whether it was new. One probe.
bool ak_map_put_new(ak_map **m, ak_val key, ak_val v);
bool ak_map_delete(ak_map **m, ak_val key);
ak_arr *ak_map_keys(const ak_map *m);
ak_arr *ak_map_values(const ak_map *m);

// ---- Sets: a map whose values are void, insertion ordered ----
//
// A Set<T> is an ak_map of T's keys (ints, or strings) to nothing,
// flagged AK_MAP_SET: it's copied on write, compared, hashed, frozen and
// copied across threads as a map is, and iterates in insertion order
// (an element removed and inserted again goes last).

ak_map *ak_set_new(ak_kind elem, bool string_elems);
static inline bool ak_map_is_set(const ak_map *m) { return (m->h.flags & AK_MAP_SET) != 0; }
bool ak_set_has(const ak_map *s, ak_val x);      // x borrowed
bool ak_set_insert(ak_map **s, ak_val x);        // takes x's reference; whether x was new
bool ak_set_remove(ak_map **s, ak_val x);        // x borrowed; whether it was there
ak_map *ak_set_union(const ak_map *a, const ak_map *b);         // a's elements, then b's new ones
ak_map *ak_set_intersection(const ak_map *a, const ak_map *b);  // a's that b has, in a's order
ak_map *ak_set_subtracting(const ak_map *a, const ak_map *b);   // a's that b hasn't
bool ak_set_is_subset(const ak_map *a, const ak_map *b);        // every element of a is b's
ak_map *ak_set_from_arr(const ak_arr *a);  // its distinct elements, first occurrence first

// ---- Structs, tuples and enums ----

typedef struct ak_struct {
	ak_obj h;
	const ak_shape *shape;
	ak_val f[];
} ak_struct;

ak_struct *ak_struct_new(const ak_shape *s);  // fields zero; the caller fills them
ak_struct *ak_struct_mut(ak_struct **s);

typedef struct ak_enum {
	ak_obj h;  // h.aux: the case
	const ak_shape *shape;
	ak_val f[];
} ak_enum;

ak_enum *ak_enum_new(const ak_shape *s, uint16_t c);  // a payloadless case: a shared immortal
static inline uint16_t ak_enum_case(const ak_enum *e) { return e->h.aux; }

typedef struct ak_box {
	ak_obj h;  // h.aux: the kind
	ak_val v;
} ak_box;

ak_box *ak_box_new(ak_kind k, ak_val v);

// ---- Closures ----

typedef struct ak_closure ak_closure;

// A closure's code: the VM's functions and compiled C functions alike.
// It is called with its arguments (borrowed) and returns its result
// (owned). An error is raised with ak_throw; the caller checks
// ak_error_pending().
typedef ak_val (*ak_invoke_fn)(ak_closure *c, ak_val *args, uint32_t nargs);

struct ak_closure {
	ak_obj h;
	ak_invoke_fn invoke;
	void (*drop)(ak_closure *c);  // after its captures are released; may be NULL
	const void *fn;               // the VM's function, or C code's data
	const uint8_t *capkinds;
	uint16_t ncap;
	ak_val caps[];
};

ak_closure *ak_closure_new(ak_invoke_fn invoke, const void *fn, uint16_t ncap, const uint8_t *capkinds);
static inline ak_val ak_call(ak_closure *c, ak_val *args, uint32_t nargs) { return c->invoke(c, args, nargs); }

// Objects with their own destructor (runtime objects: cells, tasks,
// views).
typedef struct ak_opaque {
	ak_obj h;
	void (*drop)(struct ak_opaque *o);
} ak_opaque;

void *ak_opaque_new(ak_tag tag, size_t size, void (*drop)(ak_opaque *));

// ---- Generic operations, by kind ----
//
// Each walks a value however deep it nests (a list as an enum, a million
// links long) in bounded C stack: a spawn's thread has 512 KB. One
// nested less deep than a walk's frames on the C stack takes no
// allocation (obj.c); releasing never allocates.

bool ak_val_eq(ak_val a, ak_val b, ak_kind k);
// ak_val_hash hashes as ak_val_eq compares: equal values hash the same.
uint64_t ak_val_hash(ak_val v, ak_kind k);
bool ak_obj_eq(const ak_obj *a, const ak_obj *b);
// ak_deep_copy copies a value for another thread: every object it holds
// is new, with no references shared. Closures, cells and other runtime
// objects can't be copied (the checker keeps them out of spawn).
ak_val ak_deep_copy(ak_val v, ak_kind k);
// ak_freeze makes v and everything it holds immortal: a constant, shared
// by every thread without reference counting. Each object it makes
// immortal goes into made (when not NULL), whose owner (the module whose
// init froze it) frees them with ak_frozen_free once nothing can reach
// them; without one, they're never freed.
typedef AK_VEC(ak_obj *) ak_frozen;
void ak_freeze(ak_val v, ak_kind k, ak_frozen *made);
void ak_frozen_free(ak_frozen *made);
// A constant made on first use, which threads (spawn's workers) may ask
// for at once: ak_constant_get is what *slot holds (NULL: not made yet);
// ak_constant_put freezes v, just made, and puts it in *slot unless
// another thread put one there first (v is then freed), and returns the
// one *slot holds, whole on every thread.
static inline void *ak_constant_get(void *const *slot) { return __atomic_load_n(slot, __ATOMIC_ACQUIRE); }
void *ak_constant_put(void **slot, void *v);

// ---- What a builtin changed in place ----
//
// The builtins that change an array, a map or a set in place (push,
// insert, removeAt, removeWhere, pop, clear, sort, a map's set and
// remove, a set's insert and remove) record what they changed, for the
// write that puts it back in its state (vm/path.h's ak_path_put): it
// notifies the readers of what changed, not of the rest. A key is
// recorded as its hash (ak_val_hash): a reader of another key with the
// same hash runs too, which is harmless. Each thread has its own; the
// put that reads it clears it.

enum {
	AK_CHANGE_UNKNOWN = 0,  // nothing recorded: anything may have changed
	AK_CHANGE_NONE,         // nothing changed
	AK_CHANGE_RANGE,        // an array's elements from lo to hi (hi -1: to the end)
	AK_CHANGE_KEY,          // a map's value at a key, or a set's member
};

typedef struct ak_change {
	uint8_t what;
	bool len;  // the length changed
	int64_t lo, hi;
	uint64_t hash;  // the key's
} ak_change;

extern AK_TLS ak_change ak_last_change;

static inline void ak_change_none(void) { ak_last_change = (ak_change){.what = AK_CHANGE_NONE}; }
static inline void ak_change_range(int64_t lo, int64_t hi, bool len) {
	ak_last_change = (ak_change){.what = AK_CHANGE_RANGE, .len = len, .lo = lo, .hi = hi};
}
void ak_change_key(ak_val key, ak_kind kk, bool len);

// ---- Errors ----
//
// Throwing sets the pending error and returns normally; every caller of
// something that throws checks ak_error_pending() and returns at once,
// so the error unwinds to a catch. Cancellation is an error too, one no
// boundary reports.

void ak_throw(void *err);  // takes the reference
void ak_throw_msg(const char *fmt, ...) AK_PRINTF(1, 2);
void ak_throw_cancel(void);
// The thread's pending error (NULL: none): read inline, as every call
// that may throw is followed by the test.
extern AK_TLS void *ak_pending_error;
static inline bool ak_error_pending(void) { return ak_pending_error != NULL; }
// ak_error_slot is where this thread's pending error is: *slot is
// ak_error_pending's, read without a call or a TLS lookup (the VM's
// dispatch loop takes it once). It's valid as long as the thread is.
void *const *ak_error_slot(void);
void *ak_error_take(void);  // the pending error, cleared; the caller owns it
bool ak_is_cancel(const void *err);
// ak_throw_hook, when set, sees each error as it's thrown (a program
// notes where: abi/program.c). Any thread.
extern void (*ak_throw_hook)(void *err);
// ak_frame_hook, when set, hears a function compiled to C (lang/cgen)
// left by an error, from line: the frames a program's trace has that the
// VM's stack doesn't. The error's path only.
extern void (*ak_frame_hook)(const char *fn, const char *file, uint32_t line);
// ak_retrace_hook, when set, hears that the error pending was thrown by
// the instruction the VM's innermost frame now says (its pc saved after
// the throw: an overflow's, an index's): a trace taken as it was thrown
// is taken again.
extern void (*ak_retrace_hook)(void);
ak_str *ak_error_message(const void *err);  // borrowed

extern const ak_shape ak_shape_error;  // the built-in Error(message: string)

#endif
