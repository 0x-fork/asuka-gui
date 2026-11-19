// ops.h: the instructions whose bodies are more than an expression, as
// inline functions: the interpreter (interp.c) runs them, and the C that
// lang/cgen compiles a module to calls the same ones, so both backends
// mean the same thing by each instruction. R is a frame's registers;
// kinds its registers' kinds. A function that returns bool returns false
// when it threw (ak_throw: the caller unwinds).
#ifndef AK_VM_OPS_H
#define AK_VM_OPS_H

#include "vm/vm_int.h"

static inline void ak_op_setref(ak_val *R, uint16_t a, void *p) {
	void *old = R[a].p;
	R[a].p = p;
	ak_release(old);
}

// ak_op_store puts a result in register a (releasing what it held).
static inline void ak_op_store(const uint8_t *kinds, ak_val *R, uint16_t a, ak_val v) {
	if (a == 0xFFFF) return;
	if (kinds[a] == AK_K_REF) ak_op_setref(R, a, v.p);
	else R[a] = v;
}

// ak_op_overflow throws the error an int's +, -, * or - of the smallest
// int (or its / -1) gives when the result doesn't fit: as dividing by
// zero, a catch takes it. Out of line: the checks' paths stay short (not
// inline as well: GCC rejects noinline on an inline function; unused: a
// file may include this without an int operation).
__attribute__((cold, noinline, unused)) static bool ak_op_overflow(void) {
	ak_throw_msg("integer overflow");
	return false;
}

// ak_addi, ak_subi, ak_muli and ak_negi are ADDI, SUBI, MULI and NEGI
// on values: the operation, checked (the CPU's overflow flag, one
// predicted branch), its result in *r; false (*r as it was) when it
// overflowed and threw. ak_op_addi... are the same on a frame's
// registers, the interpreter's.
static inline bool ak_addi(int64_t *r, int64_t b, int64_t c) {
	int64_t v;
	if (__builtin_expect(__builtin_add_overflow(b, c, &v), 0)) return ak_op_overflow();
	*r = v;
	return true;
}

static inline bool ak_subi(int64_t *r, int64_t b, int64_t c) {
	int64_t v;
	if (__builtin_expect(__builtin_sub_overflow(b, c, &v), 0)) return ak_op_overflow();
	*r = v;
	return true;
}

static inline bool ak_muli(int64_t *r, int64_t b, int64_t c) {
	int64_t v;
	if (__builtin_expect(__builtin_mul_overflow(b, c, &v), 0)) return ak_op_overflow();
	*r = v;
	return true;
}

static inline bool ak_negi(int64_t *r, int64_t b) {
	int64_t v;
	if (__builtin_expect(__builtin_sub_overflow((int64_t)0, b, &v), 0)) return ak_op_overflow();
	*r = v;
	return true;
}

// ak_op_division throws division by zero (cold, as ak_op_overflow).
__attribute__((cold, noinline, unused)) static bool ak_op_division(void) {
	ak_throw_msg("division by zero");
	return false;
}

static inline bool ak_divi(int64_t *r, int64_t x, int64_t y, bool mod) {
	if (__builtin_expect(y == 0, 0)) return ak_op_division();
	if (y == -1) {
		// x % -1 is 0; x / -1 is -x, which the smallest int hasn't.
		if (mod) *r = 0;
		else if (x == INT64_MIN) return ak_op_overflow();
		else *r = -x;
	} else {
		*r = mod ? x % y : x / y;
	}
	return true;
}

// ak_shift is x << n, or x >> n (arithmetic) when right: a count of 64
// or more shifts every bit out (0, or -1 for >> of a negative), as Go's
// do; a negative count throws.
static inline bool ak_shift(int64_t *r, int64_t x, int64_t n, bool right) {
	if (__builtin_expect(n < 0, 0)) {
		ak_throw_msg("negative shift amount");
		return false;
	}
	if (right) *r = n >= 64 ? (x < 0 ? -1 : 0) : x >> n;
	else *r = n >= 64 ? 0 : (int64_t)((uint64_t)x << n);
	return true;
}

static inline bool ak_op_addi(ak_val *R, uint16_t a, uint16_t b, uint16_t c) { return ak_addi(&R[a].i, R[b].i, R[c].i); }
static inline bool ak_op_subi(ak_val *R, uint16_t a, uint16_t b, uint16_t c) { return ak_subi(&R[a].i, R[b].i, R[c].i); }
static inline bool ak_op_muli(ak_val *R, uint16_t a, uint16_t b, uint16_t c) { return ak_muli(&R[a].i, R[b].i, R[c].i); }
static inline bool ak_op_negi(ak_val *R, uint16_t a, uint16_t b) { return ak_negi(&R[a].i, R[b].i); }
static inline bool ak_op_divi(ak_val *R, uint16_t a, uint16_t b, uint16_t c, bool mod) { return ak_divi(&R[a].i, R[b].i, R[c].i, mod); }
static inline bool ak_op_shift(ak_val *R, uint16_t a, uint16_t b, uint16_t c, bool right) { return ak_shift(&R[a].i, R[b].i, R[c].i, right); }

static inline int64_t ak_op_f2i(double d) {
	return d != d ? 0 : d >= 9.2233720368547758e18 ? INT64_MAX : d <= -9.2233720368547758e18 ? INT64_MIN : (int64_t)d;
}

static inline ak_str *ak_op_tostr(ak_val v, uint16_t k) {
	switch (k) {
	case AK_K_INT: return ak_str_from_int(v.i);
	case AK_K_FLOAT: return ak_str_from_float(v.f);
	case AK_K_BOOL: return ak_str_from_bool(v.i != 0);
	default: return ak_retain(v.p ? v.p : ak_str_empty());
	}
}

static inline ak_str *ak_op_interp(const ak_val *R, uint16_t b, uint16_t n, const ak_str *kinds) {
	ak_strb sb = {0};
	for (uint16_t i = 0; i < n; i++) {
		ak_val v = R[b + i];
		switch (i < kinds->len ? kinds->data[i] : AK_K_REF) {
		case AK_K_INT: ak_strb_int(&sb, v.i); break;
		case AK_K_FLOAT: ak_strb_float(&sb, v.f); break;
		case AK_K_BOOL: ak_strb_bool(&sb, v.i != 0); break;
		default:
			if (v.p) ak_strb_str(&sb, v.p);
			break;
		}
	}
	return ak_strb_finish(&sb);
}

void ak_vm_closure_drop(ak_closure *c);

static inline ak_closure *ak_op_clos(const ak_func *g, const ak_val *caps) {
	ak_closure *c = ak_closure_new(ak_vm_closure_invoke, g, g->ncap, g->capkinds);
	for (uint16_t i = 0; i < g->ncap; i++) {
		c->caps[i] = caps[i];
		ak_retain_val(c->caps[i], (ak_kind)g->capkinds[i]);
	}
	ak_module_retain(g->mod);
	c->drop = ak_vm_closure_drop;
	return c;
}

static inline void ak_op_cap(ak_val *R, uint16_t a, const ak_closure *c, uint16_t i) {
	ak_val v = c->caps[i];
	if (c->capkinds[i] == AK_K_REF) ak_op_setref(R, a, ak_retain(v.p));
	else R[a] = v;
}

// ak_vbox_new is VBOX's box: a var's, holding v (retained).
static inline ak_box *ak_vbox_new(ak_val v, ak_kind k) {
	ak_box *b = ak_box_new(k, v);
	ak_retain_val(b->v, k);
	b->h.flags = 1;
	return b;
}

static inline void ak_op_vbox(ak_val *R, uint16_t a, ak_val v, uint16_t k) { ak_op_setref(R, a, ak_vbox_new(v, (ak_kind)k)); }

static inline void ak_op_vget(ak_val *R, uint16_t a, const ak_box *b) {
	if (b->h.aux == AK_K_REF) ak_op_setref(R, a, ak_retain(b->v.p));
	else R[a] = b->v;
}

static inline void ak_op_vset(ak_box *b, ak_val v) {
	if (b->h.aux == AK_K_REF) {
		void *old = b->v.p;
		b->v.p = ak_retain(v.p);
		ak_release(old);
	} else {
		b->v = v;
	}
}

// VTAKE: a box's reference moved out, none left in it, so a change of it
// in place copies nothing; VSET puts it back.
static inline void ak_op_vtake(ak_val *R, uint16_t a, ak_box *b) {
	ak_op_setref(R, a, b->v.p);
	b->v.p = NULL;
}

static inline ak_struct *ak_op_news(const ak_shape *s, const ak_val *f) {
	ak_struct *o = ak_struct_new(s);
	for (uint16_t i = 0; i < s->nfields; i++) {
		o->f[i] = f[i];
		ak_retain_val(o->f[i], (ak_kind)s->kinds[i]);
	}
	return o;
}

// ak_op_isshape is ISSHAPE: whether obj (an interface's value, or none)
// is a struct or an enum of shape s, its type: a type test.
static inline bool ak_op_isshape(const void *obj, const ak_shape *s) {
	const ak_obj *o = obj;
	if (!o) return false;
	if (o->tag == AK_TAG_STRUCT) return ((const ak_struct *)o)->shape == s;
	if (o->tag == AK_TAG_ENUM) return ((const ak_enum *)o)->shape == s;
	return false;
}

static inline void ak_op_getf(ak_val *R, uint16_t a, const ak_struct *o, uint16_t i) {
	ak_val v = o->f[i];
	if (o->shape->kinds[i] == AK_K_REF) ak_op_setref(R, a, ak_retain(v.p));
	else R[a] = v;
}

static inline void ak_op_setf(ak_val *R, uint16_t a, uint16_t i, ak_val v) {
	ak_struct *o = ak_struct_mut((ak_struct **)&R[a].p);
	if (o->shape->kinds[i] == AK_K_REF) {
		void *old = o->f[i].p;
		o->f[i].p = ak_retain(v.p);
		ak_release(old);
	} else {
		o->f[i] = v;
	}
}

static inline ak_enum *ak_op_newe(const ak_shape *s, uint16_t ci, const ak_val *f) {
	ak_enum *e = ak_enum_new(s, ci);
	const ak_case *c = &s->cases[ci];
	for (uint16_t i = 0; i < c->nfields; i++) {
		e->f[i] = f[i];
		ak_retain_val(e->f[i], (ak_kind)c->kinds[i]);
	}
	return e;
}

static inline void ak_op_getp(ak_val *R, uint16_t a, const ak_enum *e, uint16_t i) {
	const ak_case *c = &e->shape->cases[ak_enum_case(e)];
	ak_val v = e->f[i];
	if (c->kinds[i] == AK_K_REF) ak_op_setref(R, a, ak_retain(v.p));
	else R[a] = v;
}

static inline ak_arr *ak_op_newa(ak_kind k, const ak_shape *s, const ak_val *items, uint16_t n) {
	ak_arr *arr = ak_arr_new(k, s, n);
	for (uint16_t i = 0; i < n; i++) {
		arr->data[i] = items[i];
		ak_retain_val(arr->data[i], k);
	}
	arr->len = n;
	return arr;
}

// ak_op_range throws an index out of arr's range (cold: the checks'
// paths stay short).
// The checks themselves are inline: one unsigned compare (a negative
// index is a huge one).
__attribute__((cold, noinline, unused)) static bool ak_op_range(const ak_arr *arr, int64_t i) {
	ak_throw_msg("index %lld out of range (length %u)", (long long)i, arr->len);
	return false;
}

static inline bool ak_op_idx(ak_val *R, uint16_t a, const ak_arr *arr, int64_t i) {
	if (AK_UNLIKELY((uint64_t)i >= (uint64_t)arr->len)) return ak_op_range(arr, i);
	ak_val v = arr->data[i];
	if (ak_arr_kind(arr) == AK_K_REF) ak_op_setref(R, a, ak_retain(v.p));
	else R[a] = v;
	return true;
}

// SETIDX of an array this register holds alone changes it in place
// here (ak_arr_set's work for a unique array: the old element let go
// of); a shared one is copied first by ak_arr_set.
static inline bool ak_op_setidx(ak_val *R, uint16_t a, int64_t i, ak_val v) {
	ak_arr **ap = (ak_arr **)&R[a].p;
	ak_arr *arr = *ap;
	if (AK_UNLIKELY((uint64_t)i >= (uint64_t)arr->len)) return ak_op_range(arr, i);
	ak_kind k = ak_arr_kind(arr);
	ak_retain_val(v, k);
	if (AK_LIKELY(ak_unique(arr))) {
		ak_release_val(arr->data[i], k);
		arr->data[i] = v;
	} else {
		ak_arr_set(ap, i, v);
	}
	return true;
}

static inline int64_t ak_op_len(const ak_obj *o) {
	if (!o) return 0;
	switch (o->tag) {
	case AK_TAG_ARR: return ((const ak_arr *)o)->len;
	case AK_TAG_MAP: return ((const ak_map *)o)->live;
	case AK_TAG_STR: return ((const ak_str *)o)->len;
	case AK_TAG_BYTES: return ((const ak_bytes *)o)->len;
	}
	return 0;
}

static inline ak_map *ak_op_newm(ak_kind kk, ak_kind vk, const ak_val *kv, uint16_t n) {
	ak_map *m = ak_map_new(kk, kk == AK_K_REF, vk, NULL);
	for (uint16_t i = 0; i < n; i++) {
		ak_val k = kv[2 * i], v = kv[2 * i + 1];
		ak_retain_val(k, kk);
		ak_retain_val(v, vk);
		ak_map_set(&m, k, v);
	}
	return m;
}

static inline void *ak_op_mget(const ak_map *m, ak_val key) {
	ak_val v;
	if (!ak_map_get(m, key, &v)) return NULL;
	ak_kind vk = (ak_kind)(m->h.aux >> 4);
	return vk == AK_K_REF ? ak_retain(v.p) : (void *)ak_box_new(vk, v);
}

// ak_op_mgetj puts m[key] in register a, of the map's value kind (a
// scalar as it is: no box); false when m has no key.
static inline bool ak_op_mgetj(ak_val *R, uint16_t a, const ak_map *m, ak_val key) {
	ak_val v;
	if (!ak_map_get(m, key, &v)) return false;
	if ((ak_kind)(m->h.aux >> 4) == AK_K_REF) ak_op_setref(R, a, ak_retain(v.p));
	else R[a] = v;
	return true;
}

// ak_mset is MSET on the map *mp: k and v retained as its kinds say.
static inline void ak_mset(ak_map **mp, ak_val k, ak_val v) {
	ak_kind kk = (ak_kind)((*mp)->h.aux & 0xf), vk = (ak_kind)((*mp)->h.aux >> 4);
	ak_retain_val(k, kk);
	ak_retain_val(v, vk);
	ak_map_set(mp, k, v);
}

static inline void ak_op_mset(ak_val *R, uint16_t a, ak_val k, ak_val v) { ak_mset((ak_map **)&R[a].p, k, v); }

// ak_op_task starts the closure as a task (or, a C closure, takes the
// future it returns).
static inline ak_val ak_op_task(ak_closure *c) {
	if (c->invoke == ak_vm_closure_invoke) return (ak_val){.p = ak_vm_task_start(c->fn, c, NULL, 0)};
	return c->invoke(c, NULL, 0);
}

// ---- What the optimizer makes (lang/ir's passes) ----
//
// Each is what the instructions it replaces did: the same results, the
// same errors with the same messages.

// ADDIS, SUBIS and MULIS (an int and a constant) are ak_addi, ak_subi
// and ak_muli with the constant as c: checked as ADDI, SUBI and MULI
// are. ak_op_addk... are the interpreter's, on a frame's registers.
static inline bool ak_op_addk(ak_val *R, uint16_t a, int64_t x, int64_t k) { return ak_addi(&R[a].i, x, k); }
static inline bool ak_op_subk(ak_val *R, uint16_t a, int64_t x, int64_t k) { return ak_subi(&R[a].i, x, k); }
static inline bool ak_op_mulk(ak_val *R, uint16_t a, int64_t x, int64_t k) { return ak_muli(&R[a].i, x, k); }

// ak_op_idxf is IDXF: the field f of the struct at index i of arr (IDX,
// then GETF, with no reference to the element taken).
static inline bool ak_op_idxf(ak_val *R, uint16_t a, const ak_arr *arr, int64_t i, uint16_t f) {
	if (AK_UNLIKELY((uint64_t)i >= (uint64_t)arr->len)) return ak_op_range(arr, i);
	ak_op_getf(R, a, arr->data[i].p, f);
	return true;
}

// ak_setif is SETIF on the array *ap: the field f of the struct at index
// i set to v (retained as the field's kind says), the array and then the
// struct copied first if they're shared (what IDX, SETIDX of none, SETF
// and SETIDX back did, with no reference to the element taken).
static inline bool ak_setif(ak_arr **ap, int64_t i, uint16_t f, ak_val v) {
	if (AK_UNLIKELY((uint64_t)i >= (uint64_t)(*ap)->len)) return ak_op_range(*ap, i);
	ak_arr *arr = ak_arr_mut(ap);
	ak_struct *o = ak_struct_mut((ak_struct **)&arr->data[i].p);
	if (o->shape->kinds[f] == AK_K_REF) {
		void *old = o->f[f].p;
		o->f[f].p = ak_retain(v.p);
		ak_release(old);
	} else {
		o->f[f] = v;
	}
	return true;
}

static inline bool ak_op_setif(ak_val *R, uint16_t a, int64_t i, uint16_t f, ak_val v) { return ak_setif((ak_arr **)&R[a].p, i, f, v); }

// ak_op_exit releases a frame's reference registers but keep (-1: none).
static inline void ak_op_exit(ak_val *R, const uint8_t *kinds, uint16_t n, int keep) {
	for (uint16_t i = 0; i < n; i++)
		if (kinds[i] == AK_K_REF && (int)i != keep) ak_release(R[i].p);
}

// ---- Functions compiled to C (lang/cgen) ----
//
// A synchronous function's direct body takes its arguments as C values
// and returns its result with whether it threw: ak_ret, two machine
// registers on the ABIs' calls (x0 and x1, rax and rdx), so a compiled
// caller tests a register for the error rather than the thread's
// pending one. It also takes the depth of the direct calls it's in and
// the thread's stack limit, so that its check of both costs no
// thread-local read.
typedef struct {
	ak_val v;
	uintptr_t e;  // it threw (the error is pending)
} ak_ret;

// AK_CG_DEPTH is how deep compiled calls go before a stack overflow:
// as deep as the VM's frames go (interp.c's MAX_FRAMES).
#define AK_CG_DEPTH 10000

// ak_cg_stack_low is ak_stack_low with the thread's limit given: the C
// stack's pointer below it. On the web, the engine's stack is asked too
// (base.h).
static inline bool ak_cg_stack_low(uintptr_t lim) {
#if defined(__wasm__)
	AK_UNUSED(lim);
	return ak_stack_low();
#else
	return (uintptr_t)__builtin_frame_address(0) < lim;
#endif
}

// ---- Functions that may suspend, compiled to C (ak_async_body) ----
//
// Such a function's frame is on the coroutine, as the interpreter's is,
// and its pc means what the interpreter's does: where it resumes. While
// its body runs, its registers are the body's C locals; when it leaves
// the frame waiting (it suspends, or a call it made goes on in the run
// loop), they go back into the frame.

// ak_op_enter moves frame fi's registers into R, the body's, leaving the
// frame's empty: nothing else releases them while the body runs.
static inline void ak_op_enter(ak_co *co, uint32_t fi, ak_val *R, uint16_t n) {
	if (!n) return;  // R is NULL then
	ak_val *S = co->regs + co->frames[fi].base;
	memcpy(R, S, sizeof(ak_val) * n);
	memset(S, 0, sizeof(ak_val) * n);
}

// ak_op_leave moves R back into frame fi, which resumes at pc.
static inline void ak_op_leave(ak_co *co, uint32_t fi, const ak_val *R, uint16_t n, uint32_t pc) {
	if (n) memcpy(co->regs + co->frames[fi].base, R, sizeof(ak_val) * n);
	co->frames[fi].pc = pc;
}

// ak_op_pushes reports whether calling closure c from a function that
// may suspend pushes a frame on its coroutine (the interpreter's CALLC,
// a compiled async function's) rather than calling it from C.
static inline bool ak_op_pushes(const ak_co *co, const ak_closure *c) {
	if (c->invoke != ak_vm_closure_invoke) return false;
	const ak_func *g = c->fn;
	return !g->native && !((g->flags & AK_F_ASYNC) && co->task == NULL);
}

// ak_op_acall runs the frame a compiled async function just pushed: its
// body, directly, when it's compiled too and the direct calls aren't
// deep (popping its frame when it returned or threw); else it's left to
// the run loop (AK_RUN_PUSHED), as is the rest of a call that suspended.
static inline int ak_op_acall(ak_co *co, ak_val *v) {
	const ak_func *g = co->frames[co->nframes - 1].fn;
	if (!g->resume || ak_vm_adepth >= AK_VM_ADEPTH_MAX) return AK_RUN_PUSHED;
	ak_vm_adepth++;
	int s = g->resume(co, v);
	ak_vm_adepth--;
	if (s == AK_RUN_RET || s == AK_RUN_THROW) ak_vm_pop_frame(co);
	return s;
}

#endif
