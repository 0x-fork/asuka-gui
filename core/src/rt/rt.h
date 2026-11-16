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
// runner's -leaks. Atomic: workers (spawn) make objects too.
extern int64_t ak_obj_live;

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
	ak_obj h;
	uint32_t len;
	uint32_t hash;  // 0: not computed yet
	char data[];    // len bytes, then a NUL
} ak_str;

ak_str *ak_str_new(const char *s, size_t n);  // s NULL: n bytes the caller fills
static inline ak_str *ak_str_c(const char *s) { return ak_str_new(s, strlen(s)); }
ak_str *ak_str_empty(void);  // immortal
ak_str *ak_str_concat(ak_str *a, ak_str *b);
ak_str *ak_str_from_int(int64_t v);
ak_str *ak_str_from_float(double v);  // shortest round-trip form
ak_str *ak_str_from_bool(bool v);
bool ak_str_eq(const ak_str *a, const ak_str *b);
int ak_str_cmp(const ak_str *a, const ak_str *b);
uint32_t ak_str_hash(ak_str *s);
ak_str *ak_str_slice(ak_str *s, int64_t from, int64_t to);  // byte offsets, clamped
int64_t ak_str_count(const ak_str *s);   // characters (code points)
int64_t ak_str_find(const ak_str *s, const ak_str *sub);  // byte offset or -1
bool ak_str_has_prefix(const ak_str *s, const ak_str *p);
bool ak_str_has_suffix(const ak_str *s, const ak_str *p);
bool ak_str_contains(const ak_str *s, const ak_str *sub);
ak_str *ak_str_lower(ak_str *s);
ak_str *ak_str_upper(ak_str *s);
ak_str *ak_str_trim(ak_str *s);
ak_str *ak_str_replace(ak_str *s, const ak_str *old, const ak_str *new_);
struct ak_arr *ak_str_split(ak_str *s, const ak_str *sep);
bool ak_str_to_int(const ak_str *s, int64_t *out);
bool ak_str_to_float(const ak_str *s, double *out);

// A builder for interpolated strings: no intermediate strings.
typedef struct ak_strb {
	ak_buf b;
} ak_strb;
void ak_strb_str(ak_strb *b, const ak_str *s);
void ak_strb_cstr(ak_strb *b, const char *s, size_t n);
void ak_strb_int(ak_strb *b, int64_t v);
void ak_strb_float(ak_strb *b, double v);
void ak_strb_bool(ak_strb *b, bool v);
ak_str *ak_strb_finish(ak_strb *b);  // frees the builder's buffer

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
	uint32_t len, cap;
	const ak_shape *shape;  // the elements' shape when they're structs or enums
	ak_val *data;
} ak_arr;

ak_arr *ak_arr_new(ak_kind elem, const ak_shape *shape, uint32_t cap);
static inline ak_kind ak_arr_kind(const ak_arr *a) { return (ak_kind)a->h.aux; }
// ak_arr_mut makes *a unique (copying it if it's shared) and returns it.
ak_arr *ak_arr_mut(ak_arr **a);
void ak_arr_push(ak_arr **a, ak_val v);              // takes v's reference
void ak_arr_insert(ak_arr **a, int64_t i, ak_val v); // takes v's reference
ak_val ak_arr_remove_at(ak_arr **a, int64_t i);      // returns the reference
void ak_arr_set(ak_arr **a, int64_t i, ak_val v);    // takes v's reference
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
	uint32_t *slots;     // entry index + 1, 0 empty
	const ak_shape *vshape;
} ak_map;

ak_map *ak_map_new(ak_kind key, bool string_keys, ak_kind val, const ak_shape *vshape);
ak_map *ak_map_mut(ak_map **m);
bool ak_map_get(const ak_map *m, ak_val key, ak_val *out);  // borrowed
void ak_map_set(ak_map **m, ak_val key, ak_val v);          // takes both references
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

bool ak_val_eq(ak_val a, ak_val b, ak_kind k);
uint64_t ak_val_hash(ak_val v, ak_kind k);
bool ak_obj_eq(const ak_obj *a, const ak_obj *b);
// ak_deep_copy copies a value for another thread: every object it holds
// is new, with no references shared. Closures, cells and other runtime
// objects can't be copied (the checker keeps them out of spawn).
ak_val ak_deep_copy(ak_val v, ak_kind k);
// ak_freeze makes v and everything it holds immortal: a constant, never
// freed, shared by every thread without reference counting.
void ak_freeze(ak_val v, ak_kind k);

// ---- Errors ----
//
// Throwing sets the pending error and returns normally; every caller of
// something that throws checks ak_error_pending() and returns at once,
// so the error unwinds to a catch. Cancellation is an error too, one no
// boundary reports.

void ak_throw(void *err);  // takes the reference
void ak_throw_msg(const char *fmt, ...) AK_PRINTF(1, 2);
void ak_throw_cancel(void);
bool ak_error_pending(void);
void *ak_error_take(void);  // the pending error, cleared; the caller owns it
bool ak_is_cancel(const void *err);
ak_str *ak_error_message(const void *err);  // borrowed

extern const ak_shape ak_shape_error;  // the built-in Error(message: string)

#endif
