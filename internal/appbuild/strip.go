package appbuild

import (
	"fmt"
	"maps"
	"slices"

	"github.com/egoist/asuka/lang/bytecode"
)

// StripTests returns m without its test blocks, and without what only
// they use: the functions only tests reach, and the builtins only those
// call (test.mount, test.tap: the fake host's, which an app's core, a
// release's or a player's, hasn't). What's left is what the module's
// init, its app and its shapes' methods reach, in their order. m is
// unchanged.
func StripTests(m *bytecode.Module) *bytecode.Module {
	if len(m.Tests) == 0 {
		return m
	}
	out := *m
	out.Tests = nil
	// The functions reached, from the roots, through the code's function
	// operands (calls, closures).
	keep := make([]bool, len(m.Funcs))
	var work []uint32
	reach := func(f uint32) {
		if int(f) < len(keep) && !keep[f] {
			keep[f] = true
			work = append(work, f)
		}
	}
	reach(m.Init)
	if m.App >= 0 {
		reach(uint32(m.App))
	}
	if m.Main >= 0 {
		reach(uint32(m.Main))
	}
	for _, s := range m.Shapes {
		for _, f := range s.Methods {
			reach(f)
		}
	}
	for len(work) > 0 {
		f := work[len(work)-1]
		work = work[:len(work)-1]
		eachOperand(m.Funcs[f].Code, func(o bytecode.Operand, at int, code []uint16) {
			if o == bytecode.Func {
				reach(bytecode.Word32(code, at))
			}
		})
	}
	funcs := make([]uint32, len(m.Funcs)) // old → new
	out.Funcs = nil
	for i, f := range m.Funcs {
		if keep[i] {
			funcs[i] = uint32(len(out.Funcs))
			f.Code = slices.Clone(f.Code)
			out.Funcs = append(out.Funcs, f)
		}
	}
	// The builtins the functions left call, renumbered.
	builtins := map[uint16]uint16{}
	out.Builtins = nil
	for i := range out.Funcs {
		eachOperand(out.Funcs[i].Code, func(o bytecode.Operand, at int, code []uint16) {
			switch o {
			case bytecode.Func:
				nf := funcs[bytecode.Word32(code, at)]
				code[at], code[at+1] = uint16(nf), uint16(nf>>16)
			case bytecode.Built:
				b := code[at]
				nb, ok := builtins[b]
				if !ok {
					nb = uint16(len(out.Builtins))
					builtins[b] = nb
					out.Builtins = append(out.Builtins, m.Builtins[b])
				}
				code[at] = nb
			}
		})
	}
	out.Init = funcs[m.Init]
	if m.App >= 0 {
		out.App = int32(funcs[m.App])
	}
	if m.Main >= 0 {
		out.Main = int32(funcs[m.Main])
	}
	out.Shapes = slices.Clone(m.Shapes)
	for i, s := range out.Shapes {
		if s.Methods != nil {
			ms := maps.Clone(s.Methods)
			for k, f := range ms {
				ms[k] = funcs[f]
			}
			out.Shapes[i].Methods = ms
		}
	}
	return &out
}

// eachOperand calls f with each operand of code's instructions: its
// kind and where it is (a two-word operand's first word).
func eachOperand(code []uint16, f func(o bytecode.Operand, at int, code []uint16)) {
	for pc := 0; pc < len(code); {
		op := bytecode.Op(code[pc])
		if int(op) >= len(bytecode.Ops) {
			panic(fmt.Sprintf("appbuild: no opcode %d", op))
		}
		at := pc + 1
		for _, o := range bytecode.Ops[op].Operands {
			f(o, at, code)
			at += o.Words()
		}
		pc += op.Width()
	}
}
