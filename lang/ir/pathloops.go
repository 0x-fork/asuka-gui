package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Collection methods on a path (rfcs/0013, lang/LOWERING.md's
// "Reactivity"). count(where), any, all, find, index, filter, map and
// sorted(by) on a path below a state, in a tracked context (a binding,
// an effect, a let's memo), with a closure literal, are loops over the
// path: the length read with CLEN (or AGETP's), and the closure's body
// inlined with its parameter the element's path at the loop's index (a
// pathVar), so each of its reads is a path read: count(t => t.done)
// follows the dones and the length, not the titles. A read through the
// element follows a column (its step 'I'): that field path of every
// element, one node for them all, not one per element. No closure is
// made and no element is retained to call it. The elements a method
// returns are read whole: find's, filter's kept ones (each its own
// path), sorted(by)'s (all of them: the column of the whole elements).
// Elsewhere they're the builtins they are.

// A pathVar is a loop's element: the path of the array's (its root
// register, a cell's or an accessor's, its spelling and its arguments'
// registers) and the element's index, its last argument. Its spelling
// ends with the column's step (psEach): reads through it follow every
// element's.
type pathVar struct {
	root    int
	acc     bool
	spelled []byte
	args    []int
}

// pathVarRead reads a loop's element whole into dst: as one of every
// element's (each), or as this one, its own path (what a method returns).
func (b *fb) pathVarRead(pv *pathVar, k bc.Kind, dst int, each bool) int {
	ks := make([]bc.Kind, len(pv.args))
	for i, r := range pv.args {
		ks[i] = b.f.Kinds[r]
	}
	blk := b.regs(ks)
	for i, r := range pv.args {
		b.move(blk+i, r)
	}
	r := b.out(dst, k)
	read := op("CPATH")
	if pv.acc {
		read = op("AGETP")
	}
	spelled := append([]byte(nil), pv.spelled...)
	if !each {
		spelled[len(spelled)-1] = psIndex
	}
	b.emit(read, r, pv.root, b.str(spelled), blk)
	b.releaseRegs(blk, ks)
	return r
}

// tracked reports whether b's code runs where reads are followed: a
// binding, an effect (not effect(a)'s body), a file's initializer.
func (b *fb) tracked() bool {
	if b.ctx == nil {
		return false
	}
	switch b.ctx.Kind {
	case check.CtxBinding, check.CtxGlobal:
		return true
	case check.CtxEffect:
		return !b.ctx.Untracked
	}
	return false
}

// loopMethods are the methods lowered as loops on a path, by their
// closure's parameters.
var loopMethods = map[string]int{"count": 1, "any": 1, "all": 1, "find": 1, "index": 1, "filter": 1, "map": 1, "sorted": 2}

// pathMethod lowers e, a collection method's call on a path with a
// closure literal in a tracked context, as a loop; ok false otherwise.
func (b *fb) pathMethod(e *syntax.CallExpr, dst int) (int, bool) {
	call := b.l.info.Calls[e]
	if call == nil || call.Kind != check.CallBuiltinMethod || call.Method == nil || call.Method.Recv != "array" || call.Optional || call.Recv == nil || !b.tracked() {
		return 0, false
	}
	np, ok := loopMethods[call.Method.Name]
	if !ok || len(call.Args) != 1 || call.Args[0] == nil {
		return 0, false
	}
	lam, ok := unparen(call.Args[0]).(*syntax.LambdaExpr)
	if !ok || len(lam.Params) != np || b.l.info.BuilderLambdas[lam] != nil {
		return 0, false
	}
	body, ok := lam.Body.(syntax.Expr)
	if !ok {
		return 0, false
	}
	lctx := b.l.info.Ctxs[lam]
	if lctx == nil || len(lctx.Params) != np || lctx.Awaits {
		return 0, false
	}
	for _, v := range lctx.Params {
		if v == nil || v.Boxed() || v.Captured {
			return 0, false
		}
	}
	at, isArr := b.rawType(call.Recv).(*check.Array)
	if !isArr {
		return 0, false
	}
	// The array's path: a state, an accessor's, or a path below one.
	var root int
	var acc bool
	var spelled []byte
	var pre []int
	if p := b.l.prog.StatePathOf(call.Recv); p != nil {
		if p.Steps[len(p.Steps)-1].Kind != check.StepIndex && p.Steps[len(p.Steps)-1].Kind != check.StepField && p.Steps[len(p.Steps)-1].Kind != check.StepKey {
			return 0, false
		}
		if p.Steps[len(p.Steps)-1].Kind == check.StepKey {
			return 0, false // an optional's
		}
		var more []byte
		var args []syntax.Expr
		var kinds []bc.Kind
		root, acc, spelled, pre = b.pathParts(p)
		more, args, kinds = b.pathSteps(p.Steps)
		spelled = append(spelled, more...)
		// Its arguments, computed once, kept for the loop.
		for i, a := range args {
			r := b.temp(kinds[i])
			b.value(a, r)
			pre = append(pre, r)
		}
	} else if p := b.l.prog.PathRootOf(call.Recv); p != nil {
		root, acc, spelled, pre = b.pathParts(p)
	} else {
		return 0, false
	}
	elem := b.typ(at.Elem)
	if call.Method.Name == "sorted" {
		return b.sortedPath(e, call, lam, root, acc, spelled, pre, elem, dst), true
	}
	v := lctx.Params[0]
	res := b.rawType(e)
	// The length, followed.
	n := b.temp(bc.KInt)
	{
		ks := kindsOf(b, pre)
		blk := b.regs(ks)
		for i, r := range pre {
			b.move(blk+i, r)
		}
		if acc {
			b.emit(op("AGETP"), n, root, b.str(append(append([]byte(nil), spelled...), psLen)), blk)
		} else {
			b.emit(op("CLEN"), n, root, b.str(spelled), blk)
		}
		b.releaseRegs(blk, ks)
	}
	i := b.temp(bc.KInt)
	b.loadInt(i, 0)
	if b.pathVars == nil {
		b.pathVars = map[*check.Var]*pathVar{}
	}
	b.l.prog.SetAccessorVar(v)
	pv := &pathVar{root: root, acc: acc, spelled: append(append([]byte(nil), spelled...), psEach), args: append(append([]int(nil), pre...), i)}
	b.pathVars[v] = pv
	defer delete(b.pathVars, v)

	name := call.Method.Name
	var r int
	switch name {
	case "count":
		r = b.out(dst, bc.KInt)
		b.loadInt(r, 0)
	case "any", "all":
		r = b.out(dst, bc.KBool)
		b.loadInt(r, map[bool]int64{true: 1, false: 0}[name == "all"])
	case "find", "index":
		r = b.out(dst, bc.KRef)
		b.emit(op("KNONE"), r)
	case "filter", "map":
		r = b.out(dst, bc.KRef)
		rt, _ := res.(*check.Array)
		if rt == nil {
			return 0, false
		}
		k := b.l.kind(rt.Elem)
		sh := b.l.shape(check.Unopt(rt.Elem))
		if sh < 0 {
			sh = bc.NoShape
		}
		b.emit(op("NEWA"), r, int(k), sh, i, 0)
	}
	top, end := b.f.NewLabel(), b.f.NewLabel()
	more := b.temp(bc.KBool)
	b.f.Place(top)
	b.emit(op("LTI"), more, i, n)
	b.emitJump(op("JF"), more, end)
	mark := b.mark()
	x := b.value(body, -1)
	switch name {
	case "count":
		skip := b.f.NewLabel()
		b.emitJump(op("JF"), x, skip)
		b.emit(op("ADDIK"), r, r, 1)
		b.f.Place(skip)
	case "any":
		skip := b.f.NewLabel()
		b.emitJump(op("JF"), x, skip)
		b.loadInt(r, 1)
		b.jmp(end)
		b.f.Place(skip)
	case "all":
		skip := b.f.NewLabel()
		b.emitJump(op("JT"), x, skip)
		b.loadInt(r, 0)
		b.jmp(end)
		b.f.Place(skip)
	case "find":
		skip := b.f.NewLabel()
		b.emitJump(op("JF"), x, skip)
		el := b.pathVarRead(pv, b.kind(elem), -1, false) // what it returns, read whole
		b.toOptional(el, elem, res, r)
		b.jmp(end)
		b.f.Place(skip)
	case "index":
		skip := b.f.NewLabel()
		b.emitJump(op("JF"), x, skip)
		b.toOptional(i, check.Int, res, r)
		b.jmp(end)
		b.f.Place(skip)
	case "filter":
		skip := b.f.NewLabel()
		b.emitJump(op("JF"), x, skip)
		el := b.pathVarRead(pv, b.kind(elem), -1, false) // what it keeps, read whole
		b.appendReg(r, el, "arr.push")
		b.f.Place(skip)
	case "map":
		b.appendReg(r, x, "arr.push")
	}
	b.release(mark)
	b.emit(op("ADDIK"), i, i, 1)
	b.jmp(top)
	b.f.Place(end)
	return r, true
}

// sortedPath is sorted(by:) on a path: each element read whole (what it
// returns), then sorted as the builtin sorts.
func (b *fb) sortedPath(e *syntax.CallExpr, call *check.Call, lam *syntax.LambdaExpr, root int, acc bool, spelled []byte, pre []int, elem check.Type, dst int) int {
	_ = call
	n := b.temp(bc.KInt)
	ks := kindsOf(b, pre)
	{
		blk := b.regs(ks)
		for i, r := range pre {
			b.move(blk+i, r)
		}
		if acc {
			b.emit(op("AGETP"), n, root, b.str(append(append([]byte(nil), spelled...), psLen)), blk)
		} else {
			b.emit(op("CLEN"), n, root, b.str(spelled), blk)
		}
		b.releaseRegs(blk, ks)
	}
	i := b.temp(bc.KInt)
	b.loadInt(i, 0)
	pv := &pathVar{root: root, acc: acc, spelled: append(append([]byte(nil), spelled...), psEach), args: append(append([]int(nil), pre...), i)}
	all := b.temp(bc.KRef)
	k := b.kind(elem)
	sh := b.l.shape(check.Unopt(elem))
	if sh < 0 {
		sh = bc.NoShape
	}
	b.emit(op("NEWA"), all, int(k), sh, i, 0)
	top, end := b.f.NewLabel(), b.f.NewLabel()
	more := b.temp(bc.KBool)
	b.f.Place(top)
	b.emit(op("LTI"), more, i, n)
	b.emitJump(op("JF"), more, end)
	mark := b.mark()
	el := b.pathVarRead(pv, k, -1, true) // all of them: every element's column
	b.appendReg(all, el, "arr.push")
	b.release(mark)
	b.emit(op("ADDIK"), i, i, 1)
	b.jmp(top)
	b.f.Place(end)
	by := b.lambda(lam)
	r := b.out(dst, bc.KRef)
	b.callBTo("arr.sorted", r, all, by)
	return r
}

func kindsOf(b *fb, regs []int) []bc.Kind {
	ks := make([]bc.Kind, len(regs))
	for i, r := range regs {
		ks[i] = b.f.Kinds[r]
	}
	return ks
}
