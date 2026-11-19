package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Fused pipelines: a Seq whose source and steps the lowering sees (a
// range's or an array's lazy(), then map, filter, take...), read by a
// consumer (reduce, sum, count, toArray, first, find, any, all), is a
// loop rather than the runtime's chain of ak_seqs (core/src/rt/seq.c):
// each element goes through every step, in the steps' order, before the
// next is taken from the source, and a take that has its count stops
// the loop before the source is read again, as the runtime's cursors
// do; the consumer's result is what the runtime's would be. The steps'
// and the consumer's closures are made once, before the loop, and
// called in it (CALLC): a closure literal's call is then inlined by the
// optimizer (opt_inline.go), so a pipeline of literals is one loop of
// their bodies, no closure made, no call, no allocation but its result.
//
// An array's map, filter, reduce, count(where:), any, all, find and
// index(where:) with a closure are loops of their own the same way
// (eager: each one's loop runs to its end before the next method's).

// fuseSteps are the steps a fused pipeline lowers, by their builtins.
var fuseSteps = map[string]bool{
	"seq.map": true, "seq.filter": true, "seq.compact_map": true, "seq.take": true, "seq.skip": true,
	"seq.take_while": true, "seq.skip_while": true, "seq.on_each": true,
}

// fuseConsumers are the consumers that end one, and whether each takes
// a closure (a pipeline without steps is fused only when its consumer
// does: the runtime reads a bare range or array without calling
// anything).
var fuseConsumers = map[string]bool{
	"seq.reduce": true, "seq.sum": false, "seq.len": false, "seq.count_where": true, "seq.to_array": false,
	"seq.first": false, "seq.find": true, "seq.any": true, "seq.all": true,
}

// arrayLoops are the array methods with a closure lowered as loops, by
// their builtins.
var arrayLoops = map[string]bool{
	"arr.map": true, "arr.filter": true, "arr.reduce": true, "arr.count": true, "arr.any": true,
	"arr.all": true, "arr.find": true, "arr.index": true, "arr.compact_map": true,
}

// heldBlock is a closure's captures' argument block, held to the end of
// the loop that calls it, so that its calls can be inlined (they read
// the captures there).
type heldBlock struct {
	base int
	ks   []bc.Kind
	clos int // the closure
}

// release lets go of what a loop held: its closures and their captures.
func (b *fb) releaseHeld(held []heldBlock) {
	for _, h := range held {
		b.emit(op("CLR"), h.clos)
		b.releaseRegs(h.base, h.ks)
	}
}

// heldArgs is args, a closure literal among them made with its
// captures' block held (b.holdCaps) rather than let go of at once.
func (b *fb) heldArgs(call *check.Call, held *[]heldBlock) []int {
	var vals []int
	for i, a := range call.Args {
		if a == nil {
			vals = append(vals, b.args(&check.Call{Args: []syntax.Expr{nil}, Params: call.Params[i : i+1]}, nil)...)
			continue
		}
		if lam, ok := unparen(a).(*syntax.LambdaExpr); ok && b.l.info.BuilderLambdas[lam] == nil {
			saved := b.holdCaps
			b.holdCaps = held
			vals = append(vals, b.value(a, -1))
			b.holdCaps = saved
			continue
		}
		vals = append(vals, b.value(a, -1))
	}
	return vals
}

// callClosure calls the closure in fn with args into r (bc.None: its
// result unused).
func (b *fb) callClosure(r, fn int, args ...int) {
	ks := kindsOf(b, args)
	blk := b.regs(ks)
	for i, v := range args {
		b.move(blk+i, v)
	}
	b.emit(op("CALLC"), r, fn, blk, len(args))
	b.releaseRegs(blk, ks)
}

// seqChain is a consumer's pipeline: its source (an array's lazy(), the
// call) and its steps, first first; ok false when it isn't one fuse
// lowers.
func (b *fb) seqChain(call *check.Call) (src *check.Call, steps []*check.Call, ok bool) {
	x := call.Recv
	for {
		ce, isCall := unparen(x).(*syntax.CallExpr)
		if !isCall {
			return nil, nil, false
		}
		c := b.l.info.Calls[ce]
		if c == nil || c.Kind != check.CallBuiltinMethod || c.Method == nil || c.Optional || c.Recv == nil || b.l.info.ImplicitSelf[ce] != nil {
			return nil, nil, false
		}
		switch {
		case fuseSteps[c.Method.Builtin]:
			steps = append([]*check.Call{c}, steps...)
			x = c.Recv
		case c.Method.Builtin == "seq.of_array":
			if _, isArr := b.rawType(c.Recv).(*check.Array); !isArr {
				return nil, nil, false
			}
			return c, steps, true
		default:
			return nil, nil, false
		}
	}
}

// fuseSeq lowers call, a Seq's consumer, as a loop over its pipeline
// (fuse's): ok false when it isn't one.
func (b *fb) fuseSeq(e *syntax.CallExpr, call *check.Call, dst int) (int, bool) {
	m := call.Method
	takesClosure, isConsumer := fuseConsumers[m.Builtin]
	if !isConsumer || call.Optional || call.Recv == nil {
		return 0, false
	}
	src, steps, ok := b.seqChain(call)
	if !ok || len(steps) == 0 && !takesClosure {
		return 0, false
	}
	if st, isSeq := b.rawType(call.Recv).(*check.Seq); m.Builtin == "seq.sum" && (!isSeq || b.typ(st.Elem) != check.Int && b.typ(st.Elem) != check.Float) {
		return 0, false // a sized integer's sum, checked at its type: the runtime's
	}
	var held []heldBlock
	// The source, the steps' arguments, then the consumer's: in the
	// order the chain evaluates them.
	var lo, hi, arr int
	incl, isRange := false, false
	if r, ok := unparen(src.Recv).(*syntax.BinaryExpr); ok && (r.Op == syntax.RANGE || r.Op == syntax.RANGELT) {
		if b.typeOf(r.X) != check.Int || b.typeOf(r.Y) != check.Int {
			return 0, false // a sized integer's range: the runtime's
		}
		isRange, incl = true, r.Op == syntax.RANGE
		lo = b.temp(bc.KInt)
		b.value(r.X, lo)
		hi = b.temp(bc.KInt)
		b.value(r.Y, hi)
	} else {
		arr = b.value(src.Recv, -1)
	}
	type step struct {
		name  string
		fn    int // the closure; take's and skip's count
		state int // a take's or a skip's counter, a skipWhile's flag
		out   int // the element it gives (map, compactMap)
		t     check.Type
	}
	elemT := b.typ(b.rawType(src.Recv).(*check.Array).Elem)
	if isRange {
		elemT = check.Int
	}
	var ss []step
	t := elemT
	for _, sc := range steps {
		s := step{name: sc.Method.Builtin}
		vals := b.heldArgs(sc, &held)
		s.fn = vals[0]
		switch s.name {
		case "seq.map":
			t = b.typ(b.typ(sc.Result).(*check.Seq).Elem)
			s.out = b.temp(b.kind(t))
		case "seq.compact_map":
			t = b.typ(b.typ(sc.Result).(*check.Seq).Elem)
			s.out = b.temp(bc.KRef)
		case "seq.take", "seq.skip":
			s.state = b.temp(bc.KInt)
			b.loadInt(s.state, 0)
		case "seq.skip_while":
			s.state = b.temp(bc.KBool)
			b.loadInt(s.state, 0)
		}
		s.t = t
		ss = append(ss, s)
	}
	cvals := b.heldArgs(call, &held)
	// The loop's calls are the consumer's, as the runtime's are.
	b.at(e)
	res := b.typ(call.Result)
	rk := b.l.kind(res)
	// The result, in a register of its own until the loop's end (dst
	// may be a register the loop reads: the source's local).
	r := b.temp(rk)
	switch m.Builtin {
	case "seq.reduce":
		b.move(r, cvals[0])
	case "seq.sum":
		if b.kind(t) == bc.KFloat {
			b.emit(op("KFLOAT"), r, b.l.floatConst(0))
		} else {
			b.loadInt(r, 0)
		}
	case "seq.len", "seq.count_where":
		b.loadInt(r, 0)
	case "seq.any":
		b.loadInt(r, 0)
	case "seq.all":
		b.loadInt(r, 1)
	case "seq.first", "seq.find":
		b.emit(op("KNONE"), r)
	case "seq.to_array":
		b.move(r, b.zeroValue(res)) // an empty array of the result's
	}
	// The loop: i over the range, or the array's indexes.
	i := b.temp(bc.KInt)
	n := hi
	if isRange {
		b.move(i, lo)
	} else {
		n = b.temp(bc.KInt)
		b.emit(op("LEN"), n, arr)
		b.loadInt(i, 0)
	}
	cond := b.temp(bc.KBool)
	x := b.temp(b.kind(elemT))
	top, next, end := b.f.NewLabel(), b.f.NewLabel(), b.f.NewLabel()
	if incl {
		b.emit(op("LEI"), cond, i, n)
		b.emitJump(op("JF"), cond, end)
	}
	b.f.Place(top)
	// A take that has its count ends the loop before the source is read.
	for _, s := range ss {
		if s.name == "seq.take" {
			b.emit(op("LTI"), cond, s.state, s.fn)
			b.emitJump(op("JF"), cond, end)
		}
	}
	if !incl {
		b.emit(op("LTI"), cond, i, n)
		b.emitJump(op("JF"), cond, end)
	}
	if isRange {
		b.emit(op("MOV"), x, i)
	} else {
		b.emit(op("IDX"), x, arr, i)
	}
	cur := x
	for _, s := range ss {
		switch s.name {
		case "seq.map":
			b.callClosure(s.out, s.fn, cur)
			cur = s.out
		case "seq.compact_map":
			b.callClosure(s.out, s.fn, cur)
			b.emitJump(op("JNONE"), s.out, next)
			cur = b.unbox(s.out, s.t, -1)
		case "seq.filter":
			b.callClosure(cond, s.fn, cur)
			b.emitJump(op("JF"), cond, next)
		case "seq.take":
			b.emit(op("ADDIK"), s.state, s.state, 1)
		case "seq.skip":
			skip := b.f.NewLabel()
			b.emit(op("LTI"), cond, s.state, s.fn)
			b.emitJump(op("JF"), cond, skip)
			b.emit(op("ADDIK"), s.state, s.state, 1)
			b.jmp(next)
			b.f.Place(skip)
		case "seq.take_while":
			b.callClosure(cond, s.fn, cur)
			b.emitJump(op("JF"), cond, end)
		case "seq.skip_while":
			pass := b.f.NewLabel()
			b.emitJump(op("JT"), s.state, pass)
			b.callClosure(cond, s.fn, cur)
			b.emitJump(op("JT"), cond, next)
			b.loadInt(s.state, 1)
			b.f.Place(pass)
		case "seq.on_each":
			b.callClosure(bc.None, s.fn, cur)
		}
	}
	b.consume(m.Builtin, r, cur, t, res, cvals, cond, next, end)
	b.f.Place(next)
	if incl {
		b.emit(op("EQI"), cond, i, n)
		b.emitJump(op("JT"), cond, end)
	}
	b.emit(op("ADDIK"), i, i, 1)
	b.jmp(top)
	b.f.Place(end)
	b.releaseHeld(held)
	if rk == bc.KVoid {
		return -1, true
	}
	return b.copyTo(r, dst), true
}

// consume is a fused loop's consumer's step: what it does with the
// element x (of type t) in r, its result (res); next goes on to the
// next element, end ends the loop.
func (b *fb) consume(name string, r, x int, t, res check.Type, vals []int, cond int, next, end Label) {
	switch name {
	case "seq.reduce", "arr.reduce":
		b.callClosure(r, vals[1], r, x)
	case "seq.sum":
		if b.kind(t) == bc.KFloat {
			b.emit(op("ADDF"), r, r, x)
		} else {
			b.emit(op("ADDI"), r, r, x) // checked, as the runtime's sum and +
		}
	case "seq.len":
		b.emit(op("ADDIK"), r, r, 1)
	case "seq.count_where", "arr.count":
		b.callClosure(cond, vals[0], x)
		b.emitJump(op("JF"), cond, next)
		b.emit(op("ADDIK"), r, r, 1)
	case "seq.any", "arr.any":
		b.callClosure(cond, vals[0], x)
		b.emitJump(op("JF"), cond, next)
		b.loadInt(r, 1)
		b.jmp(end)
	case "seq.all", "arr.all":
		b.callClosure(cond, vals[0], x)
		b.emitJump(op("JT"), cond, next)
		b.loadInt(r, 0)
		b.jmp(end)
	case "seq.first":
		b.toOptional(x, t, res, r)
		b.jmp(end)
	case "seq.find", "arr.find":
		b.callClosure(cond, vals[0], x)
		b.emitJump(op("JF"), cond, next)
		b.toOptional(x, t, res, r)
		b.jmp(end)
	case "seq.to_array", "arr.filter":
		b.appendReg(r, x, "arr.append")
	}
}

// arrayLoop lowers call, an array's method with a closure (arrayLoops),
// as a loop over the array: ok false when it isn't one.
func (b *fb) arrayLoop(e *syntax.CallExpr, call *check.Call, recv *int, dst int) (int, bool) {
	m := call.Method
	if !arrayLoops[m.Builtin] || call.Optional || call.Recv == nil && recv == nil || m.Recv != "array" {
		return 0, false
	}
	if m.Builtin == "arr.count" && len(call.Args) != 1 {
		return 0, false
	}
	at, ok := b.rawType(call.Recv).(*check.Array)
	if !ok {
		return 0, false
	}
	elemT := b.typ(at.Elem)
	var held []heldBlock
	arr := b.recvValue(call, recv)
	vals := b.heldArgs(call, &held)
	b.at(e)
	res := b.typ(call.Result)
	rk := b.l.kind(res)
	r := b.temp(rk)
	fn := vals[0]
	switch m.Builtin {
	case "arr.reduce":
		b.move(r, vals[0])
		fn = vals[1]
	case "arr.count", "arr.any":
		b.loadInt(r, 0)
	case "arr.all":
		b.loadInt(r, 1)
	case "arr.find", "arr.index":
		b.emit(op("KNONE"), r)
	case "arr.map", "arr.filter", "arr.compact_map":
		ut := elemT
		if m.Builtin != "arr.filter" {
			ut = b.typ(res.(*check.Array).Elem)
		}
		sh := b.l.shape(check.Unopt(ut))
		if sh < 0 {
			sh = bc.NoShape
		}
		b.emit(op("NEWA"), r, int(b.l.kind(ut)), sh, b.null(), 0)
	}
	i := b.temp(bc.KInt)
	n := b.temp(bc.KInt)
	b.emit(op("LEN"), n, arr)
	if m.Builtin == "arr.map" {
		// As long as the array: its room made once.
		b.appendReg(r, n, "arr.reserve")
	}
	b.loadInt(i, 0)
	cond := b.temp(bc.KBool)
	x := b.temp(b.kind(elemT))
	top, next, end := b.f.NewLabel(), b.f.NewLabel(), b.f.NewLabel()
	b.f.Place(top)
	b.emit(op("LTI"), cond, i, n)
	b.emitJump(op("JF"), cond, end)
	b.emit(op("IDX"), x, arr, i)
	switch m.Builtin {
	case "arr.map":
		y := b.temp(b.kind(res.(*check.Array).Elem))
		b.callClosure(y, fn, x)
		b.appendReg(r, y, "arr.append")
	case "arr.compact_map":
		y := b.temp(bc.KRef)
		b.callClosure(y, fn, x)
		b.emitJump(op("JNONE"), y, next)
		b.appendReg(r, b.unbox(y, b.typ(res.(*check.Array).Elem), -1), "arr.append")
	case "arr.filter":
		b.callClosure(cond, fn, x)
		b.emitJump(op("JF"), cond, next)
		b.consume(m.Builtin, r, x, elemT, res, vals, cond, next, end)
	case "arr.index":
		b.callClosure(cond, fn, x)
		b.emitJump(op("JF"), cond, next)
		b.toOptional(i, check.Int, res, r)
		b.jmp(end)
	case "arr.reduce":
		b.callClosure(r, fn, r, x)
	default:
		b.consume(m.Builtin, r, x, elemT, res, vals, cond, next, end)
	}
	b.f.Place(next)
	b.emit(op("ADDIK"), i, i, 1)
	b.jmp(top)
	b.f.Place(end)
	b.releaseHeld(held)
	return b.copyTo(r, dst), true
}
