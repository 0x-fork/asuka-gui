package ir

import (
	"fmt"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Workers (lang/LOWERING.md's "Workers"): a worker's state is its env, a
// struct of its parameters' and lets' values and its vars' boxes, made by
// its init function on its thread; its fns take the env first, as a
// store's do. W(args) is CALLB worker.new of a closure capturing the
// arguments that calls the init; w.f(args) from outside is CALLB
// worker.call of a closure capturing the arguments that takes the env and
// calls f, then AWAIT of the future it gives.

// workerInit is the function that makes t's env: its parameters', then
// its members' values in order.
func (l *Lowerer) workerInit(t *check.TypeName) int {
	key := fmt.Sprintf("worker|%p", t)
	if i, ok := l.funcs[key]; ok {
		return i
	}
	b := l.newFunc(t.Name()+".init", l.fileOf(t.Decl), len(t.Comp.Params), bc.KRef)
	b.f.Flags |= bc.FThrows
	l.funcs[key] = b.idx
	l.work = append(l.work, func() { b.lowerWorker(t) })
	return b.idx
}

func (b *fb) lowerWorker(t *check.TypeName) {
	comp := t.Comp
	b.ctx = comp.Ctx
	b.comp = t
	b.at(t.Decl)
	for _, m := range comp.Params {
		b.memberRegs[m] = b.newReg(b.kind(m.Type))
	}
	b.openScope()
	// The env's box: what a closure made before the env is whole reads
	// through (lowerComponent's).
	none := b.temp(bc.KRef)
	b.emit(op("KNONE"), none)
	b.envBox = b.local(bc.KRef)
	b.emit(op("VBOX"), b.envBox, none, int(bc.KRef))
	for _, m := range comp.Order {
		mark := b.mark()
		b.at(m.Decl)
		switch m.Kind {
		case check.LetMember:
			r := b.local(b.kind(m.Type))
			b.value(m.Default, r)
			b.memberRegs[m] = r
		case check.VarMember:
			k := b.kind(m.Type)
			v := b.valueReg(b.value(m.Default, -1), k)
			r := b.local(bc.KRef)
			b.emit(op("VBOX"), r, v, int(k))
			b.memberRegs[m] = r
		}
		b.release(mark)
	}
	env := b.publishEnv(t, true)
	b.emit(op("RET"), env)
	b.closeScopeNoClear()
}

// workerMember is a worker's member's value, read in its body or fns: a
// var's from its box, a parameter's or a let's as it is.
func (b *fb) workerMember(m *check.Member, n syntax.Node) int {
	ref := b.memberRef(m, n)
	if m.Kind != check.VarMember {
		return ref
	}
	r := b.temp(b.kind(m.Type))
	b.emit(op("VGET"), r, ref)
	return r
}

// workerNew is W(args): the worker, its thread started with a closure of
// the arguments that makes its env.
func (b *fb) workerNew(call *check.Call, dst int) int {
	t := call.Type.Obj
	init := b.l.workerInit(t)
	vals := b.args(call, nil)
	extra, vars := b.captureRegs(vals)
	clos := b.makeClosure(closure{name: t.Name() + ".new", extra: extra, ret: bc.KRef, throws: true, body: func(c *fb) {
		regs := make([]int, len(vars))
		for i, v := range vars {
			regs[i] = c.vars[v]
		}
		c.ret(c.invoke(init, regs, bc.KRef, -1, false), bc.KRef)
	}})
	return b.copyTo(b.callB("worker.new", bc.KRef, clos), dst)
}

// workerCall is w.f(args) from outside: f run on w's thread with the
// arguments, its result awaited.
func (b *fb) workerCall(e *syntax.CallExpr, call *check.Call, recv *int, dst int) int {
	var w int
	if recv != nil {
		w = *recv
	} else {
		w = b.value(call.Recv, -1)
	}
	return b.workerCallRegs(call, w, b.args(call, nil), dst)
}

// workerCallRegs is workerCall with the worker and the arguments in
// registers.
func (b *fb) workerCallRegs(call *check.Call, w int, vals []int, dst int) int {
	f := call.Func
	idx := b.l.funcIndex(f, nil, nil)
	t := b.typ(call.Result)
	k := b.l.kind(t)
	extra, vars := b.captureRegs(vals)
	clos := b.makeClosure(closure{name: f.Recv.Name() + "." + f.Name() + ".call", params: []*check.Var{nil}, extra: extra, ret: k, throws: true, body: func(c *fb) {
		regs := []int{0} // the env: its parameter
		for _, v := range vars {
			regs = append(regs, c.vars[v])
		}
		c.ret(c.invoke(idx, regs, k, -1, false), k)
	}})
	fut := b.callB("worker.call", bc.KRef, w, clos, b.konst(int64(k)))
	if k == bc.KVoid {
		b.emit(op("AWAIT"), bc.None, fut)
		return -1
	}
	r := b.out(dst, k)
	b.emit(op("AWAIT"), r, fut)
	return r
}

// captureRegs makes a closure's captures of registers: each a var of its
// own, which the closure's body finds in its vars.
func (b *fb) captureRegs(regs []int) ([]extraCap, []*check.Var) {
	extra := make([]extraCap, len(regs))
	vars := make([]*check.Var, len(regs))
	for i, r := range regs {
		v := &check.Var{}
		vars[i] = v
		extra[i] = extraCap{v, r}
	}
	return extra, vars
}
