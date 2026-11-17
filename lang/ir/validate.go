package ir

import (
	"fmt"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// operandKinds says, for the opcodes whose registers have one kind,
// which: per Reg operand, in order (0: any kind).
var operandKinds = map[string][]bc.Kind{
	"ADDI": {bc.KInt, bc.KInt, bc.KInt}, "SUBI": {bc.KInt, bc.KInt, bc.KInt}, "MULI": {bc.KInt, bc.KInt, bc.KInt},
	"DIVI": {bc.KInt, bc.KInt, bc.KInt}, "MODI": {bc.KInt, bc.KInt, bc.KInt}, "NEGI": {bc.KInt, bc.KInt},
	"ADDIK": {bc.KInt, bc.KInt}, "BXOR": {bc.KInt, bc.KInt, bc.KInt}, "BNOT": {bc.KInt, bc.KInt},
	"SHL": {bc.KInt, bc.KInt, bc.KInt}, "SHR": {bc.KInt, bc.KInt, bc.KInt}, "LTI": {bc.KBool, bc.KInt, bc.KInt}, "LEI": {bc.KBool, bc.KInt, bc.KInt},
	"ADDF": {bc.KFloat, bc.KFloat, bc.KFloat}, "SUBF": {bc.KFloat, bc.KFloat, bc.KFloat}, "MULF": {bc.KFloat, bc.KFloat, bc.KFloat},
	"DIVF": {bc.KFloat, bc.KFloat, bc.KFloat}, "MODF": {bc.KFloat, bc.KFloat, bc.KFloat}, "NEGF": {bc.KFloat, bc.KFloat},
	"EQF": {bc.KBool, bc.KFloat, bc.KFloat}, "NEF": {bc.KBool, bc.KFloat, bc.KFloat},
	"LTF": {bc.KBool, bc.KFloat, bc.KFloat}, "LEF": {bc.KBool, bc.KFloat, bc.KFloat},
	"I2F": {bc.KFloat, bc.KInt}, "F2I": {bc.KInt, bc.KFloat}, "KFLOAT": {bc.KFloat},
	"KSTR": {bc.KRef}, "KNONE": {bc.KRef}, "MOVR": {bc.KRef, bc.KRef}, "CLR": {bc.KRef},
	"ISNONE": {bc.KBool, bc.KRef}, "CONCAT": {bc.KRef, bc.KRef, bc.KRef}, "TOSTR": {bc.KRef, 0},
	"INTERP": {bc.KRef, 0}, "LTS": {bc.KBool, bc.KRef, bc.KRef}, "JNONE": {bc.KRef}, "JSOME": {bc.KRef}, "ISSHAPE": {bc.KBool, bc.KRef},
	"CALLC": {0, bc.KRef}, "THROW": {bc.KRef}, "CATCH": {bc.KRef}, "RETHROW": {bc.KRef},
	"CLOS": {bc.KRef}, "VBOX": {bc.KRef}, "VGET": {0, bc.KRef}, "VSET": {bc.KRef}, "VTAKE": {bc.KRef, bc.KRef},
	"NEWS": {bc.KRef}, "GETF": {0, bc.KRef}, "SETF": {bc.KRef}, "NEWE": {bc.KRef}, "ECASE": {bc.KInt, bc.KRef},
	"GETP": {0, bc.KRef}, "BOX": {bc.KRef}, "UNBOX": {0, bc.KRef}, "NEWA": {bc.KRef}, "IDX": {0, bc.KRef, bc.KInt},
	"SETIDX": {bc.KRef, bc.KInt}, "LEN": {bc.KInt, bc.KRef}, "NEWM": {bc.KRef}, "MGET": {bc.KRef, bc.KRef}, "MGETJ": {0, bc.KRef},
	"MSET": {bc.KRef}, "CELL": {bc.KRef}, "CELLK": {bc.KRef}, "CGET": {0, bc.KRef}, "CPEEK": {0, bc.KRef},
	"CSET": {bc.KRef}, "CTAKE": {0, bc.KRef}, "CPUT": {bc.KRef}, "MEMO": {bc.KRef, bc.KRef}, "MREAD": {0, bc.KRef},
	"ACCV": {bc.KRef}, "ACCF": {bc.KRef, bc.KRef}, "ACCC": {bc.KRef, bc.KRef}, "AGET": {0, bc.KRef},
	"EFFECT": {bc.KRef}, "RENDER": {bc.KRef}, "CLEANUP": {bc.KRef}, "TASK": {bc.KRef, bc.KRef},
	"AWAIT": {0, bc.KRef}, "NOT": {bc.KBool, bc.KBool},
}

// discards are the opcodes whose result register may be none (bc.None):
// the value is let go of. No other operand names none.
var discards = map[string]bool{
	"CALL": true, "CALLC": true, "CALLB": true, "CALLI": true, "CGET": true, "CPEEK": true,
	"CTAKE": true, "MREAD": true, "AGET": true, "UNTRACK": true, "AWAIT": true,
}

// Validate checks what the lowering promises of its functions: each
// register an instruction names exists (and, where the opcode says,
// has its kind), each label is placed, handler ranges nest innermost
// first, and parameters are the first registers.
func (p *Program) Validate() error {
	for _, f := range p.Funcs {
		if err := f.validate(p); err != nil {
			return fmt.Errorf("%s: %v", f.Name, err)
		}
	}
	return nil
}

func (f *Func) validate(p *Program) error {
	if f.NParams > len(f.Kinds) {
		return fmt.Errorf("%d parameters, %d registers", f.NParams, len(f.Kinds))
	}
	for i, l := range f.labels {
		if l < 0 {
			used := false
			for _, in := range f.Code {
				for j, o := range bc.Ops[in.Op].Operands {
					if o == bc.Off && int(in.Args[j]) == i {
						used = true
					}
				}
			}
			for _, h := range f.Handlers {
				if int(h.Start) == i || int(h.End) == i || int(h.Target) == i {
					used = true
				}
			}
			if used {
				return fmt.Errorf("label %d isn't placed", i)
			}
		}
	}
	for pc, in := range f.Code {
		def := bc.Ops[in.Op]
		want := operandKinds[def.Name]
		ri := 0
		for j, o := range def.Operands {
			if o != bc.Reg {
				continue
			}
			r := int(in.Args[j])
			if r == bc.None && (j != 0 || !discards[def.Name]) {
				return fmt.Errorf("instruction %d (%s): operand %d names no register", pc, def.Name, j)
			}
			if r == bc.None || emptyBlock(def.Name, j, in.Args) {
				ri++
				continue
			}
			// The base of an empty block of registers (a call with no
			// arguments, a closure with no captures) names none.
			if j+1 < len(def.Operands) && def.Operands[j+1] == bc.Imm && j > 0 && in.Args[j+1] == 0 {
				ri++
				continue
			}
			if r < 0 || r >= len(f.Kinds) {
				return fmt.Errorf("instruction %d (%s): register r%d out of %d", pc, def.Name, r, len(f.Kinds))
			}
			if ri < len(want) && want[ri] != 0 && f.Kinds[r] != want[ri] {
				return fmt.Errorf("instruction %d (%s): r%d is %s, not %s", pc, def.Name, r, f.Kinds[r], want[ri])
			}
			ri++
		}
		if def.Name == "CALL" {
			callee := p.Funcs[in.Args[1]]
			n := int(in.Args[3])
			if n != callee.NParams {
				return fmt.Errorf("instruction %d: calls %s with %d arguments, it takes %d", pc, callee.Name, n, callee.NParams)
			}
			for a := 0; a < n; a++ {
				if k, want := f.Kinds[int(in.Args[2])+a], callee.Kinds[a]; k != want {
					return fmt.Errorf("instruction %d: argument %d of %s is %s, it takes %s", pc, a, callee.Name, k, want)
				}
			}
			if d := int(in.Args[0]); d != bc.None && f.Kinds[d] != callee.Ret {
				return fmt.Errorf("instruction %d: %s returns %s into a %s register", pc, callee.Name, callee.Ret, f.Kinds[d])
			}
		}
		if def.Name == "CLOS" {
			callee := p.Funcs[in.Args[1]]
			n := int(in.Args[3])
			if n != len(callee.CapKinds) {
				return fmt.Errorf("instruction %d: closes %s over %d captures, it has %d", pc, callee.Name, n, len(callee.CapKinds))
			}
			for a := 0; a < n; a++ {
				if k, want := f.Kinds[int(in.Args[2])+a], callee.CapKinds[a]; k != want {
					return fmt.Errorf("instruction %d: capture %d of %s is %s, it takes %s", pc, a, callee.Name, k, want)
				}
			}
		}
		if (def.Name == "AWAIT" || def.Name == "YIELD") && f.Flags&bc.FAsync == 0 {
			// The backends run a function that suspends as one: resumable
			// in C, and what it calls pushed on its task.
			return fmt.Errorf("instruction %d: %s in a function that isn't async", pc, def.Name)
		}
		if def.Name == "RET" && f.Kinds[in.Args[0]] != f.Ret {
			return fmt.Errorf("instruction %d: returns a %s, the function a %s", pc, f.Kinds[in.Args[0]], f.Ret)
		}
	}
	// Ranges either nest, an inner one listed first, or don't overlap.
	at := func(l Label) int { return f.labels[l] }
	for i, h := range f.Handlers {
		for _, g := range f.Handlers[i+1:] {
			hs, he, gs, ge := at(h.Start), at(h.End), at(g.Start), at(g.End)
			inner := hs >= gs && he <= ge
			outer := gs >= hs && ge <= he
			apart := he <= gs || ge <= hs
			if outer && !inner || !inner && !apart {
				return fmt.Errorf("handlers [%d,%d) and [%d,%d) aren't listed innermost first", hs, he, gs, ge)
			}
		}
	}
	return nil
}

// emptyBlock reports whether operand j is the base of a block of no
// registers (a call with no arguments, a closure capturing nothing): no
// register is read there, whatever it says.
func emptyBlock(op string, j int, args [5]int32) bool {
	switch op {
	case "CALL", "CALLC", "CALLB", "CLOS", "CALLI":
		return j == 2 && args[3] == 0
	}
	return false
}
