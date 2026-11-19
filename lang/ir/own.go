package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
)

// A type's own ==, hash, text and JSON (check.Own): its methods are
// put in its shape's methods under names the runtime finds them by
// (core/src/vm/load.c's own_hook), so that what compares, hashes or
// writes values without knowing their types (arrays', maps' and sets'
// ==, a map's keys, json.c) calls them: #eq, #hash, #show, and #json
// and #fromjson followed by a NUL and the descriptor of what toJSON gives
// (fromJSON takes); and its compare, #cmp, which orders it. Where the type is known as the code is compiled, ==
// and a value's text call its methods directly (ownEquals, describe).

// ownHooks puts n's own methods in its shape's, and its compare, which
// orders it (#cmp: what std/collections' B-tree orders its keys by,
// core/src/rt/tree.c).
func (l *Lowerer) ownHooks(shape int, n *check.Named) {
	if shape < 0 {
		return
	}
	if f := check.CompareMethod(n); f != nil {
		sd := &l.mod.Shapes[shape]
		if sd.Methods == nil {
			sd.Methods = map[string]uint32{}
		}
		sd.Methods["#cmp"] = uint32(l.funcIndex(f, nil, n))
	}
	o := check.OwnOf(n)
	if o == nil {
		return
	}
	sd := &l.mod.Shapes[shape]
	if sd.Methods == nil {
		sd.Methods = map[string]uint32{}
	}
	if o.Equals != nil {
		sd.Methods["#eq"] = uint32(l.funcIndex(o.Equals, nil, n))
	}
	if o.Hash != nil {
		sd.Methods["#hash"] = uint32(l.funcIndex(o.Hash, nil, n))
	}
	if o.Description != nil {
		sd.Methods["#show"] = uint32(l.funcIndex(o.Description, nil, n))
	}
	if o.JSON == nil {
		return
	}
	jd := l.desc(check.OwnSubst(o, n).Apply(o.JSON))
	if o.ToJSON != nil {
		f := uint32(l.funcIndex(o.ToJSON, nil, n))
		l.mod.Shapes[shape].Methods["#json\x00"+jd] = f
	}
	if o.FromJSON != nil {
		// A static fn: the type's arguments are its first type
		// parameters.
		f := uint32(l.funcIndex(o.FromJSON, check.FromJSONArgs(o, n), nil))
		l.mod.Shapes[shape].Methods["#fromjson\x00"+jd] = f
	}
}

// ownEquals is a == b (b != a: not) by t's own equals, when t has one,
// called directly; ok false when it hasn't.
func (b *fb) ownEquals(x, y int, t check.Type, not bool, dst int) (int, bool) {
	n, ok := t.(*check.Named)
	o := check.OwnOf(t)
	if !ok || o == nil || o.Equals == nil {
		return 0, false
	}
	b.l.shape(n) // its hooks, for the values it's in
	r := b.invoke(b.l.funcIndex(o.Equals, nil, n), []int{x, y}, bc.KBool, dst, false)
	if not {
		b.emit(op("NOT"), r, r)
	}
	return r, true
}

// ownDescription is v's text by t's own description, when t has one.
func (b *fb) ownDescription(v int, t check.Type) (int, bool) {
	n, ok := t.(*check.Named)
	o := check.OwnOf(t)
	if !ok || o == nil || o.Description == nil {
		return 0, false
	}
	return b.invoke(b.l.funcIndex(o.Description, nil, n), []int{v}, bc.KRef, -1, false), true
}
