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
		if o.Words() == 2 {
			v := uint32(int32(args[i]))
			a.Code = append(a.Code, uint16(v), uint16(v>>16))
			continue
		}
		if args[i] < -0x8000 || args[i] > 0xFFFF {
			panic(fmt.Sprintf("bytecode: %s's operand %d is %d, more than 16 bits", op, i, args[i]))
		}
		a.Code = append(a.Code, uint16(args[i]))
	}
	return pc
}

// Word32 is the 32-bit operand at code[at] (two words, the low first).
func Word32(code []uint16, at int) uint32 { return uint32(code[at]) | uint32(code[at+1])<<16 }

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
		at += o.Words()
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
			if o.Words() == 2 {
				v := Word32(f.Code, at)
				at += 2
				switch o {
				case Off:
					fmt.Fprintf(&b, "->%d", pc+op.Width()+int(int32(v)))
				case Func:
					if m != nil && int(v) < len(m.Funcs) {
						b.WriteString(m.Funcs[v].Name)
					} else {
						fmt.Fprintf(&b, "f%d", v)
					}
				case Shape:
					if v == uint32(0xFFFFFFFF) {
						b.WriteString("_")
					} else {
						fmt.Fprintf(&b, "s%d", v)
					}
				default:
					fmt.Fprintf(&b, "%d", v)
				}
				continue
			}
			switch o {
			case Reg:
				if f.Code[at] == None {
					b.WriteString("_")
				} else {
					fmt.Fprintf(&b, "r%d", f.Code[at])
				}
			case KindOp:
				b.WriteString(Kind(f.Code[at]).String())
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
