package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
)

// C from Asuka (LOWERING.md "C from Asuka"): an extern fn's body is a
// CALLB of the builtin the glue registers (check.ExternBuiltin), with
// its parameters as they are: the glue converts them to C's types and
// the result back. A function returning an extern struct passes one
// more argument, a template of the struct (a frozen global made in
// init), whose shape the glue makes the result with.

// externBody is an extern fn's body.
func (b *fb) externBody(f *check.Function, params []int) {
	ext := f.Extern
	args := params
	if ext.Result.Kind == check.CStruct {
		t := b.temp(bc.KRef)
		b.emit(op("GGET"), t, b.l.externTemplate(ext.Result.Named), int(bc.KRef))
		args = append(append([]int{}, params...), t)
	}
	k := b.f.Ret
	if k == bc.KVoid {
		if len(b.f.Kinds) == 0 {
			b.newReg(bc.KInt) // the empty argument block's register
		}
		b.callBTo(ext.Builtin, bc.None, args...)
		b.emit(op("RET0"))
		return
	}
	b.ret(b.callB(ext.Builtin, k, args...), k)
}

type pendingExtern struct {
	global int
	t      *check.TypeName
}

// externTemplate is the global holding a zero value of the extern
// struct t, made in init.
func (l *Lowerer) externTemplate(t *check.TypeName) int {
	key := "extern template|" + l.typeKey(t.Type)
	if g, ok := l.enumNames[key]; ok {
		return g
	}
	g := len(l.mod.Globals)
	l.mod.Globals = append(l.mod.Globals, bc.KRef)
	l.enumNames[key] = g
	l.pendingExtern = append(l.pendingExtern, pendingExtern{g, t})
	return g
}

func (l *Lowerer) finishExternTemplates() {
	b := l.stringsFn
	for _, p := range l.pendingExtern {
		mark := b.mark()
		types := make([]check.Type, len(p.t.Fields))
		vals := make([]int, len(p.t.Fields))
		for i, f := range p.t.Fields {
			types[i] = f.Type
			vals[i] = b.zero(l.kind(f.Type))
		}
		s := b.newStruct(l.shape(p.t.Type), types, vals, -1)
		s = b.freeze(s, bc.KRef)
		b.emit(op("GSET"), p.global, s, int(bc.KRef))
		b.release(mark)
	}
	l.pendingExtern = nil
}
