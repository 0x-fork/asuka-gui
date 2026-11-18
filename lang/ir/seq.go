package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Lazy sequences (check.Seq, core/src/rt/seq.c) and ordering by a key.

// forSeq is for x in s over a Seq: a cursor over it (seq.iter), moved on
// each turn (seq.next: false at the end), its element taken (seq.value).
// The cursor is a local: leaving the loop, by break, return or a throw,
// lets go of it.
func (b *fb) forSeq(s *syntax.ForStmt, fi *check.ForInfo) {
	b.openScope()
	defer b.closeScope()
	it := b.local(bc.KRef)
	if fi.SeqVar != nil {
		// A value whose iter() gives the Seq.
		b.declare(fi.SeqVar, b.value(s.Seq, -1))
		b.callBTo("seq.iter", it, b.value(fi.IterCall, -1))
	} else {
		b.callBTo("seq.iter", it, b.value(fi.Seq, -1))
	}
	top, end := b.f.Here(), b.f.NewLabel()
	mark := b.mark()
	more := b.temp(bc.KBool)
	b.callBTo("seq.next", more, it)
	b.emitJump(op("JF"), more, end)
	b.openScope()
	x := b.temp(b.kind(fi.Elem))
	b.callBTo("seq.value", x, it)
	if _, ok := fi.Pat.(*syntax.TuplePat); ok {
		b.bindLet(fi.Pat, x, b.typ(fi.Elem))
	} else {
		b.declare(fi.Item, x)
	}
	b.loops = append(b.loops, loop{brk: end, cont: top, defers: len(b.defers), node: s})
	b.block(s.Body)
	b.loops = b.loops[:len(b.loops)-1]
	b.closeScope()
	b.release(mark)
	b.jmp(top)
	b.f.Place(end)
}

// seqExtra appends what a Seq's method (or one making or ordering by a
// key) takes after its arguments: Extra "seqProto", an empty array of
// the resulting Seq's elements, whose kind and shape the runtime reads
// (a tuple's, to make one); "proto", an empty value of the result,
// which the consumer fills.
func (b *fb) seqExtra(m *check.Method, res check.Type, vals []int) []int {
	switch m.Extra {
	case "seqProto":
		if st, ok := res.(*check.Seq); ok {
			return append(vals, b.zeroValue(&check.Array{Elem: st.Elem}))
		}
	case "proto":
		return append(vals, b.zeroValue(res))
	}
	return vals
}

// keyCompare is, for an ordering by a key (Needs ComparableU), the key
// type's compare method as a closure, or none when the key is ordered
// as it is (a number, a string, an enum's case).
func (b *fb) keyCompare(call *check.Call) int {
	if n, ok := b.typ(call.Bound["U"]).(*check.Named); ok {
		if f := check.CompareMethod(n); f != nil {
			cl := b.temp(bc.KRef)
			b.emit(op("CLOS"), cl, b.l.funcIndex(f, nil, n), 0, 0)
			return cl
		}
	}
	return b.null()
}

// lazyRange is (a..<b).lazy() and (a..b).lazy(): the numbers, made as
// they're read (no array).
func (b *fb) lazyRange(call *check.Call, dst int) (int, bool) {
	x := call.Recv
	for {
		p, ok := x.(*syntax.ParenExpr)
		if !ok {
			break
		}
		x = p.X
	}
	r, ok := x.(*syntax.BinaryExpr)
	if !ok || r.Op != syntax.RANGE && r.Op != syntax.RANGELT {
		return 0, false
	}
	from := b.value(r.X, -1)
	to := b.value(r.Y, -1)
	var incl int64
	if r.Op == syntax.RANGE {
		incl = 1
	}
	proto := b.zeroValue(&check.Array{Elem: check.Int})
	return b.copyTo(b.callB("seq.range", bc.KRef, from, to, b.konst(incl), proto), dst), true
}

// seqGen is Seq(…), a generator (check's seqCall): its form is its first
// parameter's name.
func (b *fb) seqGen(call *check.Call) int {
	st := b.typ(call.Result).(*check.Seq)
	proto := b.zeroValue(&check.Array{Elem: st.Elem})
	vals := b.args(call, nil)
	switch call.Params[0].Name {
	case "first":
		return b.callB("seq.successors", bc.KRef, b.valueReg(vals[0], b.l.kind(st.Elem)), vals[1], proto)
	case "state":
		sk := b.l.kind(b.typ(call.Bound["S"]))
		return b.callB("seq.unfold", bc.KRef, b.valueReg(vals[0], sk), b.konst(int64(sk)), vals[1], proto)
	case "repeating":
		return b.callB("seq.repeat", bc.KRef, b.valueReg(vals[0], b.l.kind(st.Elem)), vals[1], proto)
	default: // from
		return b.callB("seq.from", bc.KRef, vals[0], vals[1], proto)
	}
}

// orderedElem is what sorted(), min() and max() order: an array's or a
// Seq's elements.
func orderedElem(t check.Type) check.Type {
	switch x := t.(type) {
	case *check.Array:
		return x.Elem
	case *check.Seq:
		return x.Elem
	}
	return nil
}
