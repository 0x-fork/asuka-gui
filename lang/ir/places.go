package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Places through ?. and a map's entry with a default (RFC 0011,
// lang/LOWERING.md's "Writes through ?."). A ?. in a place is an lvSome
// step: the value of the place before it, when it isn't none. A change
// of a place with one first reads the place up to its last ?. without
// taking anything out (probe), and jumps past the whole change when it
// meets none: a cell's readers aren't told of a change that didn't
// happen (CPUT always writes the signal). The change then takes the
// place out as any does, and tests again where it is (what ran in
// between, the value assigned, may have changed it).

// prepareRecv is the place x.X of x.f (or x?.f: its value when it isn't
// none).
func (b *fb) prepareRecv(x *syntax.MemberExpr) *lvalue {
	inner := b.prepare(x.X)
	if !x.Optional {
		return inner
	}
	return &lvalue{x: x.X, kind: lvSome, inner: inner, vt: b.placeType(x.X)}
}

// placeType is the type the place x holds: a ?. chain's member its own
// type (not the chain's optional), a map's entry its value's (m[k] is
// read as a V?, and holds a V).
func (b *fb) placeType(x syntax.Expr) check.Type {
	switch x := unparen(x).(type) {
	case *syntax.MemberExpr:
		if sel := b.l.info.Selections[x]; sel != nil && sel.Optional && sel.MemberType != nil {
			return b.typ(sel.MemberType)
		}
	case *syntax.IndexExpr:
		switch t := check.Unopt(b.rawType(x.X)).(type) {
		case *check.Map:
			return b.typ(t.Elem)
		case *check.Array:
			return b.typ(t.Elem)
		}
	}
	return b.rawType(x)
}

// hasSome reports a ?. in lv's path.
func hasSome(lv *lvalue) bool {
	for ; lv != nil; lv = lv.inner {
		if lv.kind == lvSome {
			return true
		}
	}
	return false
}

// placeReads reports a default in lv's path that reads state: the
// change then reads a cell rather than taking it out, so the default
// sees its value.
func (b *fb) placeReads(lv *lvalue) bool {
	for ; lv != nil; lv = lv.inner {
		if lv.def != nil && b.l.prog.Reactive(lv.def) {
			return true
		}
	}
	return false
}

// probe reads lv's path up to its last ?., jumping to skip when it
// meets none.
func (b *fb) probe(lv *lvalue, skip Label) {
	for l := lv; l != nil; l = l.inner {
		if l.kind == lvSome {
			b.readPlace(l, skip)
			return
		}
	}
}

// readPlace reads the place lv as it is, with its prepared indexes,
// taking nothing out; a ?. that meets none jumps to skip.
func (b *fb) readPlace(lv *lvalue, skip Label) int {
	switch lv.kind {
	case lvVar, lvBoxed:
		return b.readVar(lv.v, lv.x)
	case lvCell:
		t := b.temp(b.cellKind(lv))
		b.emit(op("CPEEK"), t, lv.cell)
		return t
	case lvField:
		return b.getField(b.readPlace(lv.inner, skip), lv.named, lv.field, -1)
	case lvIndex:
		x := b.readPlace(lv.inner, skip)
		if !lv.isMap {
			t := b.temp(b.kind(b.placeType(lv.x)))
			b.emit(op("IDX"), t, x, lv.idx)
			return t
		}
		if lv.def == nil {
			t := b.temp(bc.KRef)
			b.emit(op("MGET"), t, x, lv.idx)
			return t
		}
		t := b.temp(b.kind(b.placeType(lv.x)))
		found, miss := b.f.NewLabel(), b.f.NewLabel()
		b.emit(op("MGETJ"), t, x, lv.idx, int(miss))
		b.jmp(found)
		b.f.Place(miss)
		b.move(t, b.value(lv.def, t))
		b.f.Place(found)
		return t
	case lvSome:
		x := b.readPlace(lv.inner, skip)
		b.emitJump(op("JNONE"), x, skip)
		return b.unbox(x, check.Unopt(lv.vt), -1)
	}
	return b.value(lv.x, -1)
}

// someRaw reports whether lvSome's inner place holds an optional as it's
// changed: a map's entry is reached only when it's there (updateEntry),
// so it holds a V, an optional only when V is one.
func someRaw(lv *lvalue) bool {
	in := lv.inner
	if in.kind == lvIndex && in.isMap && in.def == nil {
		return check.IsOptional(lv.vt)
	}
	return true
}

// updateSome runs fn on the value of lvSome's place (in cur) when it
// isn't none: a scalar's unboxed, and boxed again after.
func (b *fb) updateSome(lv *lvalue, cur int, fn func(int)) {
	if !someRaw(lv) {
		fn(cur)
		return
	}
	skip := b.f.NewLabel()
	b.emitJump(op("JNONE"), cur, skip)
	vt := check.Unopt(lv.vt)
	if k := b.l.kind(vt); k != bc.KRef {
		t := b.temp(k)
		b.emit(op("UNBOX"), t, cur)
		fn(t)
		b.emit(op("BOX"), cur, t, int(k))
	} else {
		fn(cur)
	}
	b.f.Place(skip)
}

// updateEntry runs fn on the entry of the map in rx that the place lv
// names, and puts it back: when the map has it, or (m[k, default: v])
// v, evaluated only then. A map without it, and no default, is left as
// it is.
func (b *fb) updateEntry(lv *lvalue, rx int, fn func(int)) {
	m, _ := check.Unopt(b.rawType(lv.inner.x)).(*check.Map)
	k := bc.KRef
	if m != nil {
		k = b.kind(m.Elem)
	}
	t := b.temp(k)
	end := b.f.NewLabel()
	if lv.def != nil {
		found, miss := b.f.NewLabel(), b.f.NewLabel()
		b.emit(op("MGETJ"), t, rx, lv.idx, int(miss))
		b.jmp(found)
		b.f.Place(miss)
		b.move(t, b.value(lv.def, t))
		b.f.Place(found)
	} else {
		b.emit(op("MGETJ"), t, rx, lv.idx, int(end))
	}
	b.changePart(t, fn, func(v int) { b.emit(op("MSET"), rx, lv.idx, v) })
	b.f.Place(end)
}

// optionalChange runs change (which makes the change of lv: an
// assignment, a mut fn's or an inout builtin's call) when lv's path
// meets no none: nothing happens otherwise. skip, placed after, is where
// a none jumps.
func (b *fb) optionalChange(lv *lvalue, change func()) {
	if !hasSome(lv) {
		change()
		return
	}
	skip := b.f.NewLabel()
	b.probe(lv, skip)
	change()
	b.f.Place(skip)
}

// chainedIndex reads x[i] in a ?. chain (a?.items[0]), x of the
// optional type xt: none when x is, the element made optional.
func (b *fb) chainedIndex(e *syntax.IndexExpr, xt check.Type, dst int) int {
	r := b.out(dst, bc.KRef)
	none, end := b.f.NewLabel(), b.f.NewLabel()
	xv := b.value(e.X, -1)
	b.emitJump(op("JNONE"), xv, none)
	x := b.unbox(xv, check.Unopt(xt), -1)
	i := b.value(e.Index, -1)
	res := b.rawType(e)
	switch t := check.Unopt(xt).(type) {
	case *check.Map:
		if e.Default != nil {
			v := b.temp(b.kind(t.Elem))
			found, miss := b.f.NewLabel(), b.f.NewLabel()
			b.emit(op("MGETJ"), v, x, i, int(miss))
			b.jmp(found)
			b.f.Place(miss)
			b.move(v, b.value(e.Default, v))
			b.f.Place(found)
			b.toOptional(v, b.typ(t.Elem), res, r)
		} else {
			b.emit(op("MGET"), r, x, i)
		}
	case *check.Array:
		v := b.temp(b.kind(t.Elem))
		b.emit(op("IDX"), v, x, i)
		b.toOptional(v, b.typ(t.Elem), res, r)
	default:
		v := b.callB("bytes.at", bc.KInt, x, i)
		b.toOptional(v, check.Int, res, r)
	}
	b.jmp(end)
	b.f.Place(none)
	b.emit(op("KNONE"), r)
	b.f.Place(end)
	return r
}
