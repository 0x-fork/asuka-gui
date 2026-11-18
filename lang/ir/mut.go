package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Methods that change: a mut fn's call (RFC 0011, lang/LOWERING.md's
// "Methods that change") is an update of its receiver's place, as
// x.f = v and the inout builtins are. The receiver is moved into the
// call's first argument register, so it stays unique and the body's
// SETF and SETIDX change it in place; the callee (bc.FMut) moves it back
// there as it returns or throws, and the caller takes it back.

// mutCall lowers a mut fn's call on a place: its receiver's (Recv), or
// self's for a method of the enclosing type called by name.
func (b *fb) mutCall(e *syntax.CallExpr, call *check.Call, dst int, discard bool) int {
	var lv *lvalue
	if call.SelfCall {
		lv = &lvalue{x: e, kind: lvVar, v: b.ctx.Self}
	} else {
		lv = b.prepare(call.Recv)
	}
	return b.mutCallOn(lv, e, call, dst, discard, nil)
}

// optionalMutCall is x?.f(…) of a mut fn or an inout builtin: nothing
// when x is none (its arguments aren't evaluated), and the call's
// result made optional.
func (b *fb) optionalMutCall(e *syntax.CallExpr, call *check.Call, dst int, discard bool) int {
	lv := &lvalue{x: call.Recv, kind: lvSome, inner: b.prepare(call.Recv), vt: b.placeType(call.Recv)}
	res := b.rawType(e)
	r := -1
	var after func(v int)
	if res != check.Void && !discard {
		r = b.out(dst, bc.KRef)
		b.emit(op("KNONE"), r)
		from := check.Unopt(res)
		if call.Result != nil {
			from = b.subst.Apply(call.Result)
		}
		after = func(v int) {
			if v >= 0 {
				b.toOptional(v, from, res, r)
			}
		}
	}
	b.optionalChange(lv, func() {
		if call.Mut {
			b.mutCallOn(lv, e, call, -1, r < 0, after)
			return
		}
		b.inout, b.inoutAfter = lv, after
		b.builtinMethod(e, call, nil, -1)
	})
	return r
}

// mutCallOn calls mut fn call on the place lv: its arguments first,
// then the place reached (a cell taken out), the call, and the place put
// back; after, when given, is run with the result where the call is
// made (a ?. may skip it).
func (b *fb) mutCallOn(lv *lvalue, e *syntax.CallExpr, call *check.Call, dst int, discard bool, after func(int)) int {
	target, k, reads := b.mutTarget(call)
	var vals []int
	mark := b.mark()
	if call.Kind == check.CallIface {
		vals = b.args(call, nil)
	} else {
		vals = b.args(call, target.subst)
	}
	b.dropTemps(mark, vals...)
	r := bc.None
	if k != bc.KVoid && !discard {
		r = b.out(dst, k)
	}
	b.updateWith(lv, reads || b.placeReads(lv), func(cur int) {
		b.callMut(cur, vals, func(blk, n int) {
			if target.idx >= 0 {
				b.emit(op("CALL"), r, target.idx, blk, n)
			} else {
				b.emit(op("CALLI"), r, b.l.str(call.Name), blk, n)
			}
		})
		if after != nil {
			after(r)
		}
	})
	if r == bc.None {
		return -1
	}
	return r
}

// mutFunc is what a mut call calls: a function (idx), or an interface
// value's method through CALLI (idx -1).
type mutFunc struct {
	idx   int
	subst check.Subst
}

// mutTarget resolves a mut call: the function, its result's kind, and
// whether its body may read state (then a cell is read rather than taken
// out, so that it sees the value as it was: the change copies it).
func (b *fb) mutTarget(call *check.Call) (mutFunc, bc.Kind, bool) {
	if call.Kind == check.CallIface {
		k := b.l.kind(b.typ(call.Result))
		if n, ok := b.typ(check.Unopt(call.RecvType)).(*check.Named); ok && (n.Obj.Kind == check.StructType || n.Obj.Kind == check.EnumType) {
			if f := b.l.method(n, call.Name); f != nil {
				return mutFunc{b.l.funcIndex(f, nil, n), n.Subst()}, k, readsState(f)
			}
		}
		// An interface's value: what its method reads isn't known here.
		return mutFunc{idx: -1}, k, true
	}
	f := call.Func
	var rt check.Type
	if call.RecvType != nil && !call.SelfCall {
		rt = b.typ(check.Unopt(call.RecvType))
	} else if b.ctx.Self != nil {
		rt = b.typ(b.ctx.Self.Type)
	}
	var targs []check.Type
	for _, t := range call.TypeArgs {
		targs = append(targs, b.typ(t))
	}
	s := b.calleeSubst(f, targs, rt)
	return mutFunc{b.l.funcIndex(f, targs, rt), s}, b.l.kind(s.Apply(f.Sig.Result)), readsState(f)
}

func readsState(f *check.Function) bool { return f.Ctx == nil || f.Ctx.ReadsState() }

// callMut calls (emit, given the argument block and its length) with
// cur, the receiver's own register, moved into the block's first
// register, and moves it back after: when the call returns, and when it
// throws (what the callee changed before stays).
func (b *fb) callMut(cur int, vals []int, emit func(blk, n int)) {
	ks := []bc.Kind{b.f.Kinds[cur]}
	for _, v := range vals {
		ks = append(ks, b.f.Kinds[v])
	}
	blk := b.regs(ks)
	for i, v := range vals {
		b.move(blk+1+i, v)
	}
	ref := ks[0] == bc.KRef
	b.emit(op("MOV"), blk, cur)
	if ref {
		b.emit(op("MOV"), cur, b.null())
	}
	back := func() {
		b.emit(op("MOV"), cur, blk)
		if ref {
			b.emit(op("MOV"), blk, b.null())
		}
	}
	b.protect(func() { emit(blk, len(ks)) }, func(int) { back() })
	back()
	b.releaseRegs(blk, ks)
}

// selfPlace is a method's self, or its field name, as a place: a mut
// fn's body changes them.
func (b *fb) selfPlace(x syntax.Expr) *lvalue {
	if b.ctx == nil || b.ctx.Self == nil {
		return nil
	}
	self := &lvalue{x: x, kind: lvVar, v: b.ctx.Self}
	id, ok := x.(*syntax.Ident)
	if !ok {
		return self
	}
	n, ok := b.typ(b.ctx.Self.Type).(*check.Named)
	if !ok || n.Obj.Kind != check.StructType {
		return nil
	}
	f := n.Obj.Field(id.Name)
	if f == nil {
		return nil
	}
	return &lvalue{x: x, kind: lvField, inner: self, field: f, named: n}
}
