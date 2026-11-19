package cgen

import (
	"fmt"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// optInsn writes the C for the instructions lang/ir's optimizer makes
// (bytecode's "What the optimizer makes"), with ops.h's helpers, the
// interpreter's own: false for another instruction. h is where an error
// goes.
func (g *gen) optInsn(f *bc.FuncDef, in insn, h string) bool {
	a := in.args
	at := lineAt(f, in.pc)
	w := func(format string, args ...any) { g.p("\t"+format+"\n", args...) }
	k := func(j int) int16 { return int16(uint16(a[j])) }
	switch name := bc.Ops[in.op].Name; name {
	case "ADDIS", "SUBIS", "MULIS":
		fn := map[string]string{"ADDIS": "ak_op_addk", "SUBIS": "ak_op_subk", "MULIS": "ak_op_mulk"}[name]
		w("if (!%s(R, %d, R[%d].i, %d)) { at_ = %d; goto %s; }", fn, a[0], a[1], k(2), at, h)
	case "DIVIS":
		w("R[%d].i = R[%d].i / %d;", a[0], a[1], k(2))
	case "MODIS":
		w("R[%d].i = R[%d].i %% %d;", a[0], a[1], k(2))
	case "JLTI", "JLEI", "JEQI", "JNEI":
		c := map[string]string{"JLTI": "<", "JLEI": "<=", "JEQI": "==", "JNEI": "!="}[name]
		w("if (R[%d].i %s R[%d].i) goto L%d;", a[0], c, a[1], a[2])
	case "JLTIS", "JLEIS", "JGTIS", "JGEIS", "JEQIS", "JNEIS":
		c := map[string]string{"JLTIS": "<", "JLEIS": "<=", "JGTIS": ">", "JGEIS": ">=", "JEQIS": "==", "JNEIS": "!="}[name]
		w("if (R[%d].i %s %d) goto L%d;", a[0], c, k(1), a[2])
	case "IDXF":
		w("if (!ak_op_idxf(R, %d, R[%d].p, R[%d].i, %d)) { at_ = %d; goto %s; }", a[0], a[1], a[2], a[3], at, h)
	case "SETIF":
		w("if (!ak_op_setif(R, %d, R[%d].i, %d, R[%d])) { at_ = %d; goto %s; }", a[0], a[1], a[2], a[3], at, h)
	default:
		return false
	}
	return true
}

// inlined is where an error in instruction pc goes first when pc is in a
// function's body the optimizer inlined (bytecode.Inline): a trampoline
// that tells ak_frame_hook of each inlined frame it leaves, innermost
// first, with its line, then goes on to h with the line of the call
// (what a call's frame would have said). The trampoline is written by
// the returned func, after the instruction; h is returned as it is when
// pc isn't inlined.
func (g *gen) inlined(f *bc.FuncDef, pc int, h string) (string, func()) {
	var ins []bc.Inline
	for _, in := range f.Inlines {
		if pc >= int(in.Start) && pc < int(in.End) {
			ins = append(ins, in)
		}
	}
	if len(ins) == 0 {
		return h, func() {}
	}
	label := fmt.Sprintf("I%d", pc)
	return label, func() {
		g.p("\tif (0) {\n%s:\n", label)
		for i, in := range ins {
			callee := &g.m.Funcs[in.Func]
			if i > 0 {
				g.p("\t\tat_ = %d;\n", ins[i-1].Line)
			}
			g.p("\t\tif (ak_frame_hook) ak_frame_hook(%s, %s, at_);\n", cbytes(callee.Name), cbytes(callee.File))
		}
		// (Used or not: the instruction may not throw.)
		g.p("\t\tat_ = %d;\n\t\tgoto %s;\n\t}\n\tif (0) goto %s;\n", ins[len(ins)-1].Line, h, label)
	}
}
