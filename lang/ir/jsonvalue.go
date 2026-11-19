package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// jsonValue is e (of type from) as std/json's Value (to), where one is
// expected (check's toJSONValue): an int, a float, a bool or a string is
// its case, made here; none .null; anything else what JSON writes of it,
// read back as a Value, made directly from the value by its descriptor
// (CALLB json.value: core/src/rt/json.c's ak_json_value, no text).
func (b *fb) jsonValue(e syntax.Expr, from check.Type, to *check.Named, dst int) int {
	name := ""
	switch {
	case from == check.NoneType:
		b.expr(e, -1)
		return b.caseValue(to, to.Obj.Case("null"), dst)
	case from == check.Int || check.IsSized(from) && from != check.U64:
		name = "int" // the sized ones are canonical ints
	case from == check.Float:
		name = "float"
	case from == check.Bool:
		name = "bool"
	case from == check.String:
		name = "string"
	}
	v := b.expr(e, -1)
	if name == "" {
		return b.copyTo(b.callB("json.value", bc.KRef, v, b.kstr(b.l.desc(from)), b.kstr(b.l.desc(to))), dst)
	}
	k := to.Obj.Case(name)
	ks := []bc.Kind{b.f.Kinds[v]}
	blk := b.regs(ks)
	b.move(blk, v)
	r := b.out(dst, bc.KRef)
	b.emit(op("NEWE"), r, b.l.shape(to), k.Index+caseBase(to), blk)
	b.releaseRegs(blk, ks)
	return r
}
