package bytecode

import (
	"fmt"
	"strings"
)

// Asm appends instructions to a function's code.
type Asm struct {
	Code []uint16
}

// Emit appends op with its operands; an Off operand takes one int
// argument (the offset), which Emit splits into two words.
func (a *Asm) Emit(op Op, args ...int) int {
	pc := len(a.Code)
	a.Code = append(a.Code, uint16(op))
	ops := Ops[op].Operands
	if len(args) != len(ops) {
		panic(fmt.Sprintf("bytecode: %s takes %d operands, got %d", op, len(ops), len(args)))
	}
	for i, o := range ops {
		if o == Off {
			v := uint32(int32(args[i]))
			a.Code = append(a.Code, uint16(v), uint16(v>>16))
		} else {
			a.Code = append(a.Code, uint16(args[i]))
		}
	}
	return pc
}

// PatchJump sets the jump at pc (an instruction with an Off operand) to
// land on target.
func (a *Asm) PatchJump(pc, target int) {
	op := Op(a.Code[pc])
	at := pc + 1
	for _, o := range Ops[op].Operands {
		if o == Off {
			end := pc + op.Width()
			v := uint32(int32(target - end))
			a.Code[at], a.Code[at+1] = uint16(v), uint16(v>>16)
			return
		}
		at++
	}
	panic("bytecode: not a jump: " + op.String())
}

// Disasm lists code, an instruction per line.
func Disasm(m *Module, f *FuncDef) string {
	var b strings.Builder
	fmt.Fprintf(&b, "func %s (params %d, regs %d)\n", f.Name, f.NParams, len(f.Kinds))
	for pc := 0; pc < len(f.Code); {
		op := Op(f.Code[pc])
		if int(op) >= len(Ops) {
			fmt.Fprintf(&b, "%5d  ?%d\n", pc, op)
			pc++
			continue
		}
		fmt.Fprintf(&b, "%5d  %-8s", pc, op)
		at := pc + 1
		for i, o := range Ops[op].Operands {
			if i > 0 {
				b.WriteString(",")
			}
			b.WriteString(" ")
			switch o {
			case Off:
				v := int32(uint32(f.Code[at]) | uint32(f.Code[at+1])<<16)
				fmt.Fprintf(&b, "->%d", pc+op.Width()+int(v))
				at += 2
				continue
			case Reg:
				if f.Code[at] == None {
					b.WriteString("_")
				} else {
					fmt.Fprintf(&b, "r%d", f.Code[at])
				}
			case KindOp:
				b.WriteString(Kind(f.Code[at]).String())
			case Func:
				if m != nil && int(f.Code[at]) < len(m.Funcs) {
					b.WriteString(m.Funcs[f.Code[at]].Name)
				} else {
					fmt.Fprintf(&b, "f%d", f.Code[at])
				}
			case Built:
				if m != nil && int(f.Code[at]) < len(m.Builtins) {
					b.WriteString(m.Builtins[f.Code[at]])
				} else {
					fmt.Fprintf(&b, "b%d", f.Code[at])
				}
			case Imm:
				fmt.Fprintf(&b, "%d", int16(f.Code[at]))
			default:
				fmt.Fprintf(&b, "%d", f.Code[at])
			}
			at++
		}
		b.WriteString("\n")
		pc += op.Width()
	}
	return b.String()
}
