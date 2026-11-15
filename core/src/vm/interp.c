#include "vm/vm_int.h"

#include <stdio.h>

ak_co ak_vm_main;
ak_co *ak_vm_cur = &ak_vm_main;

#define MAX_FRAMES 10000

// ---- Frames ----

bool ak_vm_push(ak_co *co, const ak_func *f, ak_closure *clos, ak_val *args, uint32_t nargs, uint16_t dst, bool boundary, uint32_t callpc) {
	if (co->nframes >= MAX_FRAMES) {
		ak_throw_msg("stack overflow in %s", f->name->data);
		return false;
	}
	if (nargs != f->nparams) ak_panic("vm: %s takes %u arguments, called with %u", f->name->data, f->nparams, nargs);
	uint32_t base = co->top;
	if (base + f->nregs > co->cap) {
		uint32_t c = co->cap ? co->cap * 2 : 1024;
		while (c < base + f->nregs) c *= 2;
		// args may point into the registers being moved.
		ptrdiff_t off = (args && co->regs && args >= co->regs && args < co->regs + co->cap) ? args - co->regs : -1;
		co->regs = ak_realloc(co->regs, sizeof(ak_val) * c);
		co->cap = c;
		if (off >= 0) args = co->regs + off;
	}
	ak_val *R = co->regs + base;
	memset(R, 0, sizeof(ak_val) * f->nregs);
	for (uint32_t i = 0; i < nargs; i++) {
		R[i] = args[i];
		ak_retain_val(R[i], (ak_kind)f->kinds[i]);
	}
	co->top = base + f->nregs;
	if (co->nframes == co->fcap) {
		co->fcap = co->fcap ? co->fcap * 2 : 64;
		co->frames = ak_realloc(co->frames, sizeof(ak_frame) * co->fcap);
	}
	if (clos) ak_retain(clos);
	co->frames[co->nframes++] = (ak_frame){.fn = f, .clos = clos, .pc = 0, .callpc = callpc, .base = base, .dst = dst, .boundary = boundary};
	return true;
}

// pop releases the top frame's registers (but the one at keep, moved
// out) and its closure.
static void pop(ak_co *co, int keep) {
	ak_frame *fr = &co->frames[co->nframes - 1];
	ak_val *R = co->regs + fr->base;
	const ak_func *f = fr->fn;
	for (uint32_t i = 0; i < f->nregs; i++)
		if (f->kinds[i] == AK_K_REF && (int)i != keep) ak_release(R[i].p);
	co->top = fr->base;
	ak_closure *c = fr->clos;
	co->nframes--;
	if (c) ak_release(c);
}

static int64_t find_handler(const ak_func *f, uint32_t pc) {
	// The innermost handler is the one listed first (the compiler emits
	// inner ranges before outer ones).
	for (uint32_t i = 0; i < f->nhandlers; i++)
		if (pc >= f->handlers[i].start && pc < f->handlers[i].end) return f->handlers[i].target;
	return -1;
}

static uint32_t line_of(const ak_func *f, uint32_t pc) {
	uint32_t line = 0;
	for (uint32_t i = 0; i < f->nlines && f->lines[i].pc <= pc; i++) line = f->lines[i].line;
	return line;
}

void ak_vm_traceback(char *buf, size_t cap) {
	size_t n = 0;
	buf[0] = 0;
	ak_co *co = ak_vm_cur;
	for (uint32_t i = co->nframes; i-- > 0 && n < cap;) {
		ak_frame *fr = &co->frames[i];
		int w = snprintf(buf + n, cap - n, "%s (%s:%u)\n", fr->fn->name->data, fr->fn->file->data, line_of(fr->fn, fr->pc ? fr->pc - 1 : 0));
		if (w < 0) break;
		n += (size_t)w;
	}
}

// ---- Interface calls ----

static const ak_func *method(void *obj, ak_str *name) {
	const ak_obj *o = obj;
	const ak_shape *s = NULL;
	if (o && o->tag == AK_T_STRUCT) s = ((const ak_struct *)o)->shape;
	else if (o && o->tag == AK_T_ENUM) s = ((const ak_enum *)o)->shape;
	if (!s || s == &ak_shape_error) return NULL;
	// Shapes the VM loaded carry their methods and module.
	const ak_vm_shape *vs = (const ak_vm_shape *)s;
	for (uint32_t i = 0; i < vs->nmethods; i++)
		if (vs->methods[i].name == name || ak_str_eq(vs->methods[i].name, name)) return &vs->mod->funcs[vs->methods[i].func];
	return NULL;
}

// ---- The interpreter ----

static ak_str *interp_str(ak_val *R, uint16_t b, uint16_t n, const ak_str *kinds) {
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

static inline void setref(ak_val *R, uint16_t a, void *p) {
	void *old = R[a].p;
	R[a].p = p;
	ak_release(old);
}

static inline uint32_t off32(const uint16_t *c) { return (uint32_t)c[0] | (uint32_t)c[1] << 16; }

// store puts a result in register a of the frame's function (releasing
// what it held), or releases it when a is 0xFFFF.
static inline void store(const ak_func *f, ak_val *R, uint16_t a, ak_val v) {
	if (a == 0xFFFF) return;  // the caller released it if it was a reference
	if (f->kinds[a] == AK_K_REF) setref(R, a, v.p);
	else R[a] = v;
}

bool ak_vm_run(ak_co *co, ak_val *out) {
	ak_co *prev = ak_vm_cur;
	ak_vm_cur = co;
	ak_frame *fr;
	const ak_func *F;
	const uint16_t *code;
	ak_val *R;
	ak_module *M;
	uint32_t pc, ipc = 0;

#define LOAD() do { \
		fr = &co->frames[co->nframes - 1]; \
		F = fr->fn; \
		code = F->code; \
		R = co->regs + fr->base; \
		M = F->mod; \
		pc = fr->pc; \
	} while (0)
	// After anything that may run more VM code (and grow the stacks).
#define RELOAD() do { \
		fr = &co->frames[co->nframes - 1]; \
		R = co->regs + fr->base; \
	} while (0)
#define CHECK_ERR() do { if (AK_UNLIKELY(ak_error_pending())) goto unwind; } while (0)

	LOAD();
	for (;;) {
		ipc = pc;
		uint16_t op = code[pc];
		const uint16_t *a = code + pc + 1;
		pc += ak_op_width[op];
		switch (op) {
		case AK_OP_NOP:
			break;
		case AK_OP_MOV:
			R[a[0]] = R[a[1]];
			break;
		case AK_OP_MOVR:
			if (a[0] != a[1]) setref(R, a[0], ak_retain(R[a[1]].p));
			break;
		case AK_OP_CLR:
			setref(R, a[0], NULL);
			break;
		case AK_OP_KINT:
			R[a[0]].i = M->ints[a[1]];
			break;
		case AK_OP_KSMALL:
			R[a[0]].i = (int16_t)a[1];
			break;
		case AK_OP_KFLOAT:
			R[a[0]].f = M->floats[a[1]];
			break;
		case AK_OP_KSTR:
			setref(R, a[0], ak_retain(M->strings[a[1]]));
			break;
		case AK_OP_KNONE:
			setref(R, a[0], NULL);
			break;
		case AK_OP_GGET:
			if (a[2] == AK_K_REF) setref(R, a[0], ak_retain(M->globals[a[1]].p));
			else R[a[0]] = M->globals[a[1]];
			break;
		case AK_OP_GSET:
			if (a[2] == AK_K_REF) {
				void *old = M->globals[a[0]].p;
				M->globals[a[0]].p = ak_retain(R[a[1]].p);
				ak_release(old);
			} else {
				M->globals[a[0]] = R[a[1]];
			}
			break;

		case AK_OP_ADDI: R[a[0]].i = (int64_t)((uint64_t)R[a[1]].i + (uint64_t)R[a[2]].i); break;
		case AK_OP_SUBI: R[a[0]].i = (int64_t)((uint64_t)R[a[1]].i - (uint64_t)R[a[2]].i); break;
		case AK_OP_MULI: R[a[0]].i = (int64_t)((uint64_t)R[a[1]].i * (uint64_t)R[a[2]].i); break;
		case AK_OP_DIVI:
		case AK_OP_MODI: {
			int64_t x = R[a[1]].i, y = R[a[2]].i;
			if (y == 0) {
				ak_throw_msg("division by zero");
				goto unwind;
			}
			if (y == -1) R[a[0]].i = op == AK_OP_DIVI ? (int64_t)(0 - (uint64_t)x) : 0;
			else R[a[0]].i = op == AK_OP_DIVI ? x / y : x % y;
			break;
		}
		case AK_OP_NEGI: R[a[0]].i = (int64_t)(0 - (uint64_t)R[a[1]].i); break;
		case AK_OP_ADDIK: R[a[0]].i = (int64_t)((uint64_t)R[a[1]].i + (uint64_t)(int64_t)(int16_t)a[2]); break;
		case AK_OP_EQI: R[a[0]].i = R[a[1]].i == R[a[2]].i; break;
		case AK_OP_NEI: R[a[0]].i = R[a[1]].i != R[a[2]].i; break;
		case AK_OP_LTI: R[a[0]].i = R[a[1]].i < R[a[2]].i; break;
		case AK_OP_LEI: R[a[0]].i = R[a[1]].i <= R[a[2]].i; break;
		case AK_OP_BAND: R[a[0]].i = R[a[1]].i & R[a[2]].i; break;
		case AK_OP_BOR: R[a[0]].i = R[a[1]].i | R[a[2]].i; break;

		case AK_OP_ADDF: R[a[0]].f = R[a[1]].f + R[a[2]].f; break;
		case AK_OP_SUBF: R[a[0]].f = R[a[1]].f - R[a[2]].f; break;
		case AK_OP_MULF: R[a[0]].f = R[a[1]].f * R[a[2]].f; break;
		case AK_OP_DIVF: R[a[0]].f = R[a[1]].f / R[a[2]].f; break;
		case AK_OP_MODF: R[a[0]].f = fmod(R[a[1]].f, R[a[2]].f); break;
		case AK_OP_NEGF: R[a[0]].f = -R[a[1]].f; break;
		case AK_OP_EQF: R[a[0]].i = R[a[1]].f == R[a[2]].f; break;
		case AK_OP_NEF: R[a[0]].i = R[a[1]].f != R[a[2]].f; break;
		case AK_OP_LTF: R[a[0]].i = R[a[1]].f < R[a[2]].f; break;
		case AK_OP_LEF: R[a[0]].i = R[a[1]].f <= R[a[2]].f; break;
		case AK_OP_I2F: R[a[0]].f = (double)R[a[1]].i; break;
		case AK_OP_F2I: {
			double d = R[a[1]].f;
			R[a[0]].i = d != d ? 0 : d >= 9.2233720368547758e18 ? INT64_MAX : d <= -9.2233720368547758e18 ? INT64_MIN : (int64_t)d;
			break;
		}
		case AK_OP_NOT: R[a[0]].i = !R[a[1]].i; break;

		case AK_OP_EQ: R[a[0]].i = ak_val_eq(R[a[1]], R[a[2]], (ak_kind)a[3]); break;
		case AK_OP_NE: R[a[0]].i = !ak_val_eq(R[a[1]], R[a[2]], (ak_kind)a[3]); break;
		case AK_OP_ISNONE: R[a[0]].i = R[a[1]].p == NULL; break;

		case AK_OP_CONCAT: setref(R, a[0], ak_str_concat(R[a[1]].p, R[a[2]].p)); break;
		case AK_OP_TOSTR: {
			ak_str *s;
			switch (a[2]) {
			case AK_K_INT: s = ak_str_from_int(R[a[1]].i); break;
			case AK_K_FLOAT: s = ak_str_from_float(R[a[1]].f); break;
			case AK_K_BOOL: s = ak_str_from_bool(R[a[1]].i != 0); break;
			default: s = ak_retain(R[a[1]].p ? R[a[1]].p : ak_str_empty()); break;
			}
			setref(R, a[0], s);
			break;
		}
		case AK_OP_INTERP:
			setref(R, a[0], interp_str(R, a[1], a[2], M->strings[a[3]]));
			break;
		case AK_OP_LTS: R[a[0]].i = ak_str_cmp(R[a[1]].p, R[a[2]].p) < 0; break;

		case AK_OP_JMP: pc += off32(a); break;
		case AK_OP_JT: if (R[a[0]].i) pc += off32(a + 1); break;
		case AK_OP_JF: if (!R[a[0]].i) pc += off32(a + 1); break;
		case AK_OP_JNONE: if (!R[a[0]].p) pc += off32(a + 1); break;
		case AK_OP_JSOME: if (R[a[0]].p) pc += off32(a + 1); break;

		case AK_OP_CALL: {
			const ak_func *g = &M->funcs[a[1]];
			fr->pc = pc;
			if (!ak_vm_push(co, g, NULL, R + a[2], a[3], a[0], false, ipc)) goto unwind;
			LOAD();
			break;
		}
		case AK_OP_CALLC: {
			ak_closure *c = R[a[1]].p;
			if (!c) ak_panic("vm: calling none");
			fr->pc = pc;
			if (c->invoke == ak_vm_closure_invoke && !(((const ak_func *)c->fn)->flags & AK_F_ASYNC && co->task == NULL)) {
				// A VM closure: call it in this loop.
				if (!ak_vm_push(co, c->fn, c, R + a[2], a[3], a[0], false, ipc)) goto unwind;
				LOAD();
			} else {
				ak_val v = c->invoke(c, R + a[2], a[3]);
				RELOAD();
				CHECK_ERR();
				store(F, R, a[0], v);
			}
			break;
		}
		case AK_OP_CALLB: {
			fr->pc = pc;
			ak_val v = M->builtins[a[1]](R + a[2], a[3]);
			RELOAD();
			CHECK_ERR();
			store(F, R, a[0], v);
			break;
		}
		case AK_OP_CALLI: {
			ak_str *name = M->strings[a[1]];
			void *obj = R[a[2]].p;
			const ak_func *g = method(obj, name);
			if (!g) ak_panic("vm: no method %s", name->data);
			fr->pc = pc;
			if (!ak_vm_push(co, g, NULL, R + a[2], a[3], a[0], false, ipc)) goto unwind;
			LOAD();
			break;
		}
		case AK_OP_RET:
		case AK_OP_RET0: {
			ak_val v = {0};
			int keep = -1;
			if (op == AK_OP_RET) {
				v = R[a[0]];
				if (F->kinds[a[0]] == AK_K_REF) keep = a[0];
			}
			ak_kind k = op == AK_OP_RET ? (ak_kind)F->kinds[a[0]] : AK_K_VOID;
			bool boundary = fr->boundary;
			uint16_t dst = fr->dst;
			pop(co, keep);
			if (boundary || co->nframes == 0) {
				*out = v;
				ak_vm_cur = prev;
				return false;
			}
			LOAD();
			if (dst == 0xFFFF) ak_release_val(v, k);
			else store(F, R, dst, v);
			break;
		}
		case AK_OP_THROW: {
			void *e = R[a[0]].p;
			R[a[0]].p = NULL;
			ak_throw(e);
			goto unwind;
		}
		case AK_OP_CATCH:
			setref(R, a[0], ak_error_take());
			break;
		case AK_OP_RETHROW: {
			void *e = R[a[0]].p;
			R[a[0]].p = NULL;
			ak_throw(e);
			// Handlers of the same function don't catch it again: look
			// from the caller on.
			goto unwind_frame;
		}

		case AK_OP_CLOS: {
			const ak_func *g = &M->funcs[a[1]];
			ak_closure *c = ak_closure_new(ak_vm_closure_invoke, g, g->ncap, g->capkinds);
			c->drop = NULL;
			for (uint16_t i = 0; i < g->ncap; i++) {
				c->caps[i] = R[a[2] + i];
				ak_retain_val(c->caps[i], (ak_kind)g->capkinds[i]);
			}
			ak_module_retain(g->mod);
			extern void ak_vm_closure_drop(ak_closure *);
			c->drop = ak_vm_closure_drop;
			setref(R, a[0], c);
			break;
		}
		case AK_OP_CAP: {
			ak_closure *c = fr->clos;
			ak_val v = c->caps[a[1]];
			if (c->capkinds[a[1]] == AK_K_REF) setref(R, a[0], ak_retain(v.p));
			else R[a[0]] = v;
			break;
		}
		case AK_OP_VBOX: {
			ak_box *b = ak_box_new((ak_kind)a[2], R[a[1]]);
			ak_retain_val(b->v, (ak_kind)a[2]);
			b->h.flags = 1;  // a var box: its value is released with it
			setref(R, a[0], b);
			break;
		}
		case AK_OP_VGET: {
			ak_box *b = R[a[1]].p;
			if (b->h.aux == AK_K_REF) setref(R, a[0], ak_retain(b->v.p));
			else R[a[0]] = b->v;
			break;
		}
		case AK_OP_VSET: {
			ak_box *b = R[a[0]].p;
			if (b->h.aux == AK_K_REF) {
				void *old = b->v.p;
				b->v.p = ak_retain(R[a[1]].p);
				ak_release(old);
			} else {
				b->v = R[a[1]];
			}
			break;
		}

		case AK_OP_NEWS: {
			const ak_shape *s = &M->shapes[a[1]].s;
			ak_struct *o = ak_struct_new(s);
			for (uint16_t i = 0; i < s->nfields; i++) {
				o->f[i] = R[a[2] + i];
				ak_retain_val(o->f[i], (ak_kind)s->kinds[i]);
			}
			setref(R, a[0], o);
			break;
		}
		case AK_OP_GETF: {
			ak_struct *o = R[a[1]].p;
			ak_val v = o->f[a[2]];
			if (o->shape->kinds[a[2]] == AK_K_REF) setref(R, a[0], ak_retain(v.p));
			else R[a[0]] = v;
			break;
		}
		case AK_OP_SETF: {
			ak_struct **op_ = (ak_struct **)&R[a[0]].p;
			ak_struct *o = ak_struct_mut(op_);
			if (o->shape->kinds[a[1]] == AK_K_REF) {
				void *old = o->f[a[1]].p;
				o->f[a[1]].p = ak_retain(R[a[2]].p);
				ak_release(old);
			} else {
				o->f[a[1]] = R[a[2]];
			}
			break;
		}
		case AK_OP_NEWE: {
			const ak_shape *s = &M->shapes[a[1]].s;
			ak_enum *e = ak_enum_new(s, a[2]);
			const ak_case *c = &s->cases[a[2]];
			for (uint16_t i = 0; i < c->nfields; i++) {
				e->f[i] = R[a[3] + i];
				ak_retain_val(e->f[i], (ak_kind)c->kinds[i]);
			}
			setref(R, a[0], e);
			break;
		}
		case AK_OP_ECASE: R[a[0]].i = ak_enum_case(R[a[1]].p); break;
		case AK_OP_GETP: {
			ak_enum *e = R[a[1]].p;
			const ak_case *c = &e->shape->cases[ak_enum_case(e)];
			ak_val v = e->f[a[2]];
			if (c->kinds[a[2]] == AK_K_REF) setref(R, a[0], ak_retain(v.p));
			else R[a[0]] = v;
			break;
		}
		case AK_OP_BOX: setref(R, a[0], ak_box_new((ak_kind)a[2], R[a[1]])); break;
		case AK_OP_UNBOX: R[a[0]] = ((ak_box *)R[a[1]].p)->v; break;

		case AK_OP_NEWA: {
			ak_kind k = (ak_kind)a[1];
			const ak_shape *s = a[2] == 0xFFFF ? NULL : &M->shapes[a[2]].s;
			ak_arr *arr = ak_arr_new(k, s, a[4]);
			for (uint16_t i = 0; i < a[4]; i++) {
				arr->data[i] = R[a[3] + i];
				ak_retain_val(arr->data[i], k);
			}
			arr->len = a[4];
			setref(R, a[0], arr);
			break;
		}
		case AK_OP_IDX: {
			ak_arr *arr = R[a[1]].p;
			int64_t i = R[a[2]].i;
			if (!ak_arr_check(arr, i)) {
				ak_throw_msg("index %lld out of range (length %u)", (long long)i, arr->len);
				goto unwind;
			}
			ak_val v = arr->data[i];
			if (ak_arr_kind(arr) == AK_K_REF) setref(R, a[0], ak_retain(v.p));
			else R[a[0]] = v;
			break;
		}
		case AK_OP_SETIDX: {
			ak_arr **ap = (ak_arr **)&R[a[0]].p;
			int64_t i = R[a[1]].i;
			if (!ak_arr_check(*ap, i)) {
				ak_throw_msg("index %lld out of range (length %u)", (long long)i, (*ap)->len);
				goto unwind;
			}
			ak_val v = R[a[2]];
			ak_retain_val(v, ak_arr_kind(*ap));
			ak_arr_set(ap, i, v);
			break;
		}
		case AK_OP_LEN: {
			ak_obj *o = R[a[1]].p;
			int64_t n = 0;
			if (o) {
				switch (o->tag) {
				case AK_T_ARR: n = ((ak_arr *)o)->len; break;
				case AK_T_MAP: n = ((ak_map *)o)->live; break;
				case AK_T_STR: n = ((ak_str *)o)->len; break;
				case AK_T_BYTES: n = ((ak_bytes *)o)->len; break;
				}
			}
			R[a[0]].i = n;
			break;
		}
		case AK_OP_NEWM: {
			ak_kind kk = (ak_kind)a[1], vk = (ak_kind)a[2];
			ak_map *m = ak_map_new(kk, kk == AK_K_REF, vk, NULL);
			for (uint16_t i = 0; i < a[4]; i++) {
				ak_val k = R[a[3] + 2 * i], v = R[a[3] + 2 * i + 1];
				ak_retain_val(k, kk);
				ak_retain_val(v, vk);
				ak_map_set(&m, k, v);
			}
			setref(R, a[0], m);
			break;
		}
		case AK_OP_MGET: {
			ak_map *m = R[a[1]].p;
			ak_val v;
			void *res = NULL;
			if (ak_map_get(m, R[a[2]], &v)) {
				ak_kind vk = (ak_kind)(m->h.aux >> 4);
				res = vk == AK_K_REF ? ak_retain(v.p) : ak_box_new(vk, v);
			}
			setref(R, a[0], res);
			break;
		}
		case AK_OP_MSET: {
			ak_map **mp = (ak_map **)&R[a[0]].p;
			ak_kind kk = (ak_kind)((*mp)->h.aux & 0xf), vk = (ak_kind)((*mp)->h.aux >> 4);
			ak_val k = R[a[1]], v = R[a[2]];
			ak_retain_val(k, kk);
			ak_retain_val(v, vk);
			ak_map_set(mp, k, v);
			break;
		}

		case AK_OP_CELL:
		case AK_OP_CELLK: {
			ak_val v = R[a[1]];
			ak_retain_val(v, (ak_kind)a[2]);
			fr->pc = pc;
			ak_val c = ak_vm_cell_new(v, (ak_kind)a[2], op == AK_OP_CELLK ? M->strings[a[3]] : NULL);
			RELOAD();
			setref(R, a[0], c.p);
			break;
		}
		case AK_OP_CGET:
		case AK_OP_CPEEK: {
			fr->pc = pc;
			ak_val v = ak_vm_cell_get(R[a[1]].p, op == AK_OP_CGET);
			RELOAD();
			store(F, R, a[0], v);
			break;
		}
		case AK_OP_CSET: {
			ak_val v = R[a[1]];
			ak_retain_val(v, (ak_kind)F->kinds[a[1]]);
			fr->pc = pc;
			ak_vm_cell_set(R[a[0]].p, v);
			RELOAD();
			CHECK_ERR();
			break;
		}
		case AK_OP_CTAKE:
			store(F, R, a[0], ak_vm_cell_take(R[a[1]].p));
			break;
		case AK_OP_CPUT: {
			ak_val v = R[a[1]];
			if (F->kinds[a[1]] == AK_K_REF) R[a[1]].p = NULL;  // moved
			fr->pc = pc;
			ak_vm_cell_put(R[a[0]].p, v);
			RELOAD();
			CHECK_ERR();
			break;
		}
		case AK_OP_MEMO: {
			fr->pc = pc;
			ak_val m = ak_vm_memo_new(R[a[1]].p, (ak_kind)a[2]);
			RELOAD();
			setref(R, a[0], m.p);
			break;
		}
		case AK_OP_MREAD: {
			fr->pc = pc;
			ak_val v = ak_vm_memo_get(R[a[1]].p);
			RELOAD();
			CHECK_ERR();
			store(F, R, a[0], v);
			break;
		}
		case AK_OP_ACCV: {
			ak_val v = R[a[1]];
			ak_retain_val(v, (ak_kind)a[2]);
			setref(R, a[0], ak_vm_acc_value(v, (ak_kind)a[2]).p);
			break;
		}
		case AK_OP_ACCF:
			setref(R, a[0], ak_vm_acc_func(R[a[1]].p, (ak_kind)a[2]).p);
			break;
		case AK_OP_ACCC:
			setref(R, a[0], ak_vm_acc_cell(R[a[1]].p).p);
			break;
		case AK_OP_AGET: {
			fr->pc = pc;
			ak_val v = ak_vm_acc_get(R[a[1]].p);
			RELOAD();
			CHECK_ERR();
			store(F, R, a[0], v);
			break;
		}
		case AK_OP_UNTRACK: {
			fr->pc = pc;
			ak_val v = ak_vm_untrack(R[a[1]].p, R + a[2], a[3]);
			RELOAD();
			CHECK_ERR();
			store(F, R, a[0], v);
			break;
		}
		case AK_OP_EFFECT:
		case AK_OP_RENDER:
			fr->pc = pc;
			ak_vm_effect(R[a[0]].p, op == AK_OP_RENDER);
			RELOAD();
			CHECK_ERR();
			break;
		case AK_OP_CLEANUP:
			ak_vm_cleanup(R[a[0]].p);
			break;
		case AK_OP_BATCH:
			fr->pc = pc;
			ak_vm_batch(a[0] != 0);
			RELOAD();
			CHECK_ERR();
			break;

		case AK_OP_TASK: {
			ak_closure *c = R[a[1]].p;
			fr->pc = pc;
			// A VM closure starts a task; a C closure returns its future.
			ak_val f = c->invoke == ak_vm_closure_invoke ? (ak_val){.p = ak_vm_task_start(c->fn, c, NULL, 0)} : c->invoke(c, NULL, 0);
			RELOAD();
			CHECK_ERR();
			setref(R, a[0], f.p);
			break;
		}
		case AK_OP_AWAIT: {
			ak_future *f = R[a[1]].p;
			ak_task *t = co->task;
			if (t && t->cancelled) {
				ak_throw_cancel();
				goto unwind;
			}
			if (f->state == AK_FUT_PENDING) {
				if (!t) ak_panic("vm: await outside an async context, in %s", F->name->data);
				for (uint32_t i = co->nframes; i-- > 1;)
					if (co->frames[i].boundary) ak_panic("vm: await in a synchronous call from C, in %s", F->name->data);
				// Suspend; AWAIT runs again when resumed, with f done.
				fr->pc = ipc;
				t->awaiting = ak_retain(f);
				ak_future_wait(f, ak_vm_task_resume, t);
				ak_vm_cur = prev;
				return true;
			}
			ak_val v = ak_future_take(f);
			CHECK_ERR();
			store(F, R, a[0], v);
			break;
		}
		case AK_OP_YIELD: {
			ak_task *t = co->task;
			if (!t) ak_panic("vm: yield outside a task");
			fr->pc = pc;
			ak_ready(ak_vm_task_resume, t);
			ak_vm_cur = prev;
			return true;
		}
		default:
			ak_panic("vm: bad opcode %u in %s at %u", op, F->name->data, ipc);
		}
		continue;

	unwind:
		// An error is pending: find a handler, in this frame, then its
		// callers'.
		{
			int64_t h = find_handler(F, ipc);
			if (h >= 0) {
				pc = (uint32_t)h;
				continue;
			}
		}
	unwind_frame:
		for (;;) {
			bool boundary = fr->boundary;
			uint32_t callpc = fr->callpc;
			pop(co, -1);
			if (boundary || co->nframes == 0) {
				*out = (ak_val){0};
				ak_vm_cur = prev;
				return false;
			}
			LOAD();
			// The call didn't return: its destination keeps what it had.
			ipc = callpc;
			int64_t h = find_handler(F, ipc);
			if (h >= 0) {
				pc = (uint32_t)h;
				break;
			}
		}
	}
}

// ---- Calls from C ----

void ak_vm_closure_drop(ak_closure *c) { ak_module_release(((const ak_func *)c->fn)->mod); }

static ak_val call_sync(const ak_func *f, ak_closure *clos, ak_val *args, uint32_t nargs) {
	ak_co *co = ak_vm_cur;
	if (!ak_vm_push(co, f, clos, args, nargs, 0xFFFF, true, 0)) return (ak_val){0};
	ak_val out;
	bool suspended = ak_vm_run(co, &out);
	if (suspended) ak_panic("vm: %s suspended in a synchronous call", f->name->data);
	return out;
}

ak_val ak_vm_closure_invoke(ak_closure *c, ak_val *args, uint32_t nargs) {
	const ak_func *f = c->fn;
	if (f->flags & AK_F_ASYNC) return (ak_val){.p = ak_vm_task_start(f, c, args, nargs)};
	return call_sync(f, c, args, nargs);
}

ak_val ak_vm_call(const ak_func *f, ak_val *args, uint32_t nargs) {
	if (f->flags & AK_F_ASYNC) {
		// Its result, if it finished; its error, if it failed; else none
		// while it runs on as a task.
		ak_future *fut = ak_vm_task_start(f, NULL, args, nargs);
		ak_val v = {0};
		if (fut->state != AK_FUT_PENDING) v = ak_future_take(fut);
		ak_release(fut);
		return v;
	}
	return call_sync(f, NULL, args, nargs);
}

ak_closure *ak_vm_closure(const ak_func *f) {
	ak_closure *c = ak_closure_new(ak_vm_closure_invoke, f, 0, NULL);
	ak_module_retain(f->mod);
	c->drop = ak_vm_closure_drop;
	return c;
}

void ak_vm_run_ready(void) { ak_sched_run(); }
bool ak_vm_has_ready(void) { return ak_sched_has_ready(); }
