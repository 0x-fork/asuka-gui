// Package bytecode is the format the Asuka compiler writes and the C
// core's VM (core/src/vm) runs: a module of constants, shapes and
// functions whose code is a stream of 16-bit words.
//
// The opcodes are defined once, here; go generate writes the C VM's
// header (core/src/vm/opcodes_gen.h) from them.
//
// Registers. A function has NRegs registers; its parameters are the
// first ones. Each register holds one kind of value for the whole
// function (the compiler allocates them per kind), and the function's
// RefMask says which hold references: those own their value. An
// instruction that writes a reference register releases what it held;
// one that reads a reference borrows it. Leaving a function releases its
// reference registers.
//
// Encoding. An instruction is its opcode's word followed by its
// operands, one word each except for Off (a jump: a signed 32-bit offset
// from the instruction after the jump) and the indexes of a module's
// tables (Const, Func, Shape, Global: unsigned 32-bit), two words each,
// the low one first. A destination of 0xFFFF discards the result; a call
// whose result is a reference always has a register for it (so it's
// released), and only a call to a bytecode function (CALL, CALLI) may
// discard one.
package bytecode

//go:generate go run ./gen

// Operand is what an instruction's operand is.
type Operand uint8

const (
	Reg    Operand = iota + 1 // a register
	Imm                       // a 16-bit immediate (signed where it says)
	Const                     // a constant pool index (ints, floats, strings: the opcode says): 32-bit
	Func                      // a function index: 32-bit
	Shape                     // a shape index: 32-bit
	Built                     // a builtin index (Module.Builtins)
	KindOp                    // an ak_kind
	Off                       // a jump offset: 32-bit
	Global                    // a module global slot: 32-bit
)

// Op is an opcode.
type Op uint16

// OpDef describes an opcode.
type OpDef struct {
	Name     string
	Operands []Operand
	Doc      string
}

// Ops lists every opcode; an Op is its index.
var Ops = []OpDef{
	{"NOP", nil, "does nothing"},

	// Moves and constants.
	{"MOV", []Operand{Reg, Reg}, "a = b, a scalar"},
	{"MOVR", []Operand{Reg, Reg}, "a = b, a reference: retained"},
	{"CLR", []Operand{Reg}, "releases a reference register and sets it to none"},
	{"KINT", []Operand{Reg, Const}, "a = Ints[k]"},
	{"KSMALL", []Operand{Reg, Imm}, "a = a signed 16-bit immediate (ints, bools)"},
	{"KFLOAT", []Operand{Reg, Const}, "a = Floats[k]"},
	{"KSTR", []Operand{Reg, Const}, "a = Strings[k], an immortal string"},
	{"KNONE", []Operand{Reg}, "a = none (a reference register)"},
	{"GGET", []Operand{Reg, Global, KindOp}, "a = globals[g] (retained when a reference)"},
	{"GSET", []Operand{Global, Reg, KindOp}, "globals[g] = b (retained; the old one released)"},

	// Ints.
	{"ADDI", []Operand{Reg, Reg, Reg}, "a = b + c; throws on overflow"},
	{"SUBI", []Operand{Reg, Reg, Reg}, "a = b - c; throws on overflow"},
	{"MULI", []Operand{Reg, Reg, Reg}, "a = b * c; throws on overflow"},
	{"DIVI", []Operand{Reg, Reg, Reg}, "a = b / c, truncated; throws on c == 0 and on overflow (the smallest int / -1)"},
	{"MODI", []Operand{Reg, Reg, Reg}, "a = b % c; throws on c == 0"},
	{"NEGI", []Operand{Reg, Reg}, "a = -b; throws on overflow (-b of the smallest int)"},
	{"WADDI", []Operand{Reg, Reg, Reg}, "a = b + c, wrapping (&+)"},
	{"WSUBI", []Operand{Reg, Reg, Reg}, "a = b - c, wrapping (&-)"},
	{"WMULI", []Operand{Reg, Reg, Reg}, "a = b * c, wrapping (&*)"},
	{"ADDIK", []Operand{Reg, Reg, Imm}, "a = b + a signed immediate, wrapping: the compiler's counters, which can't overflow"},
	{"EQI", []Operand{Reg, Reg, Reg}, "a = b == c (ints, bools, payloadless enums)"},
	{"NEI", []Operand{Reg, Reg, Reg}, "a = b != c"},
	{"LTI", []Operand{Reg, Reg, Reg}, "a = b < c"},
	{"LEI", []Operand{Reg, Reg, Reg}, "a = b <= c"},
	{"BAND", []Operand{Reg, Reg, Reg}, "a = b & c"},
	{"BOR", []Operand{Reg, Reg, Reg}, "a = b | c"},
	{"BXOR", []Operand{Reg, Reg, Reg}, "a = b ^ c"},
	{"BNOT", []Operand{Reg, Reg}, "a = ~b"},
	{"SHL", []Operand{Reg, Reg, Reg}, "a = b << c (0 when c >= 64); throws on c < 0"},
	{"SHR", []Operand{Reg, Reg, Reg}, "a = b >> c, arithmetic (0 or -1 when c >= 64); throws on c < 0"},

	// Floats.
	{"ADDF", []Operand{Reg, Reg, Reg}, "a = b + c"},
	{"SUBF", []Operand{Reg, Reg, Reg}, "a = b - c"},
	{"MULF", []Operand{Reg, Reg, Reg}, "a = b * c"},
	{"DIVF", []Operand{Reg, Reg, Reg}, "a = b / c"},
	{"MODF", []Operand{Reg, Reg, Reg}, "a = fmod(b, c)"},
	{"NEGF", []Operand{Reg, Reg}, "a = -b"},
	{"EQF", []Operand{Reg, Reg, Reg}, "a = b == c"},
	{"NEF", []Operand{Reg, Reg, Reg}, "a = b != c"},
	{"LTF", []Operand{Reg, Reg, Reg}, "a = b < c"},
	{"LEF", []Operand{Reg, Reg, Reg}, "a = b <= c"},
	{"I2F", []Operand{Reg, Reg}, "a = float(b)"},
	{"F2I", []Operand{Reg, Reg}, "a = int(b), truncated toward zero; saturates"},

	// Bools.
	{"NOT", []Operand{Reg, Reg}, "a = !b"},

	// Any kind.
	{"EQ", []Operand{Reg, Reg, Reg, KindOp}, "a = b == c, compared as their kind (references deeply)"},
	{"NE", []Operand{Reg, Reg, Reg, KindOp}, "a = b != c"},
	{"ISNONE", []Operand{Reg, Reg}, "a = b is none (a reference register)"},

	// Strings.
	{"CONCAT", []Operand{Reg, Reg, Reg}, "a = b + c"},
	{"TOSTR", []Operand{Reg, Reg, KindOp}, "a = the string of b, of kind k (int, float, bool; a string: retained)"},
	{"INTERP", []Operand{Reg, Reg, Imm, Const}, "a = the concatenation of n registers from b; Strings[k] holds their kinds, one byte each (a string's kind is REF)"},
	{"LTS", []Operand{Reg, Reg, Reg}, "a = b < c, strings in byte order"},

	// Control.
	{"JMP", []Operand{Off}, "jumps"},
	{"JT", []Operand{Reg, Off}, "jumps when a is true"},
	{"JF", []Operand{Reg, Off}, "jumps when a is false"},
	{"JNONE", []Operand{Reg, Off}, "jumps when a (a reference) is none"},
	{"JSOME", []Operand{Reg, Off}, "jumps when a (a reference) isn't none"},
	{"CALL", []Operand{Reg, Func, Reg, Imm}, "a = f(n arguments from register b); a throwing f unwinds to the nearest handler"},
	{"CALLC", []Operand{Reg, Reg, Reg, Imm}, "a = the closure b called with n arguments from register c"},
	{"CALLB", []Operand{Reg, Built, Reg, Imm}, "a = the builtin called with n arguments from register b"},
	{"RET", []Operand{Reg}, "returns a (a reference is moved out, not retained)"},
	{"RET0", nil, "returns nothing"},
	{"THROW", []Operand{Reg}, "throws the error in a (moved)"},
	{"CATCH", []Operand{Reg}, "a = the error being handled (at a handler's start)"},
	{"RETHROW", []Operand{Reg}, "throws a again unless it's handled: the end of a catch that didn't match"},

	// Closures.
	{"CLOS", []Operand{Reg, Func, Reg, Imm}, "a = a closure of f capturing n registers from b (as f's capture kinds say)"},
	{"CAP", []Operand{Reg, Imm}, "a = the running closure's capture i (retained when a reference)"},
	{"VBOX", []Operand{Reg, Reg, KindOp}, "a = a mutable box holding b: a var that closures capture and change"},
	{"VGET", []Operand{Reg, Reg}, "a = the box b's value (retained when a reference)"},
	{"VSET", []Operand{Reg, Reg}, "the box a = b (retained; the old one released)"},
	{"CALLI", []Operand{Reg, Const, Reg, Imm}, "a = the method Strings[k] of the object in register c (an interface value), called with it and n-1 more arguments from c"},

	// Structs and tuples.
	{"NEWS", []Operand{Reg, Shape, Reg}, "a = a struct of shape s from its fields' registers from b"},
	{"GETF", []Operand{Reg, Reg, Imm}, "a = b.fields[i] (retained when a reference)"},
	{"SETF", []Operand{Reg, Imm, Reg}, "a.fields[i] = c, a copied first if shared"},

	// Enums with payloads.
	{"NEWE", []Operand{Reg, Shape, Imm, Reg}, "a = case c of enum s with its payload from registers from b"},
	{"ECASE", []Operand{Reg, Reg}, "a = the case of enum value b"},
	{"GETP", []Operand{Reg, Reg, Imm}, "a = b's payload field i"},

	// Optional scalars.
	{"BOX", []Operand{Reg, Reg, KindOp}, "a = b boxed (an optional scalar that isn't none)"},
	{"UNBOX", []Operand{Reg, Reg}, "a = the scalar in box b"},

	// Arrays.
	{"NEWA", []Operand{Reg, KindOp, Shape, Reg, Imm}, "a = an array of kind k (elements of shape s, 0xFFFFFFFF none) from n registers from b"},
	{"IDX", []Operand{Reg, Reg, Reg}, "a = b[c]; throws out of range"},
	{"SETIDX", []Operand{Reg, Reg, Reg}, "a[b] = c, a copied first if shared; throws out of range"},
	{"LEN", []Operand{Reg, Reg}, "a = the length of b (an array, a map, a string's bytes)"},

	// Maps.
	{"NEWM", []Operand{Reg, KindOp, KindOp, Reg, Imm}, "a = a map (key kind, value kind) from n key/value register pairs from b"},
	{"MGET", []Operand{Reg, Reg, Reg}, "a = m[k] as an optional (none when missing; scalars boxed)"},
	{"MSET", []Operand{Reg, Reg, Reg}, "a[b] = c, a copied first if shared"},
	{"MGETJ", []Operand{Reg, Reg, Reg, Off}, "a = m[k] (b[c]) as the map's value kind, nothing boxed; jumps when b has no c"},

	// State: cells, memos, accessors.
	{"CELL", []Operand{Reg, Reg, KindOp}, "a = a new state cell holding b, owned by the current owner"},
	{"CELLK", []Operand{Reg, Reg, KindOp, Const}, "a = the state cell kept under Strings[k] across reloads, or a new one holding b"},
	{"CGET", []Operand{Reg, Reg}, "a = the cell b's value, tracked (retained when a reference)"},
	{"CPEEK", []Operand{Reg, Reg}, "a = the cell b's value, untracked"},
	{"CSET", []Operand{Reg, Reg}, "the cell a = b; notifies when it changed"},
	{"CTAKE", []Operand{Reg, Reg}, "a = the cell b's value moved out (for a mutation in place); CPUT puts it back"},
	{"CPUT", []Operand{Reg, Reg}, "the cell a = b (moved), notifying"},
	{"MEMO", []Operand{Reg, Reg, KindOp}, "a = a memo computing the closure b"},
	{"MREAD", []Operand{Reg, Reg}, "a = the memo b's value, tracked"},
	{"ACCV", []Operand{Reg, Reg, KindOp}, "a = an accessor of a constant value b"},
	{"ACCF", []Operand{Reg, Reg, KindOp}, "a = an accessor computing the closure b"},
	{"ACCC", []Operand{Reg, Reg}, "a = an accessor reading the cell or memo b"},
	{"AGET", []Operand{Reg, Reg}, "a = what accessor b reads, tracked"},
	{"UNTRACK", []Operand{Reg, Reg, Reg, Imm}, "a = the closure b called untracked with n arguments from c"},
	{"EFFECT", []Operand{Reg}, "runs the closure a as an effect of the current owner"},
	{"RENDER", []Operand{Reg}, "runs the closure a as a render effect"},
	{"CLEANUP", []Operand{Reg}, "runs the closure a when the current owner is disposed or its effect re-runs"},
	{"BATCH", []Operand{Imm}, "1 begins a batch, 0 ends it (flushing)"},

	// Async.
	{"TASK", []Operand{Reg, Reg}, "a = a task running the async closure b (no arguments), owned by the current owner"},
	{"AWAIT", []Operand{Reg, Reg}, "a = the result of task or future b, suspending until it has one; throws its error"},
	{"YIELD", nil, "suspends until the next turn of the main loop"},
}

// Byname maps an opcode's name to it.
var Byname = func() map[string]Op {
	m := make(map[string]Op, len(Ops))
	for i, d := range Ops {
		m[d.Name] = Op(i)
	}
	return m
}()

// Words is the number of words an operand takes: two for a jump's
// offset and a table's index (a program's constants, functions, shapes
// and globals may be more than 16 bits count), one for the rest.
func (o Operand) Words() int {
	switch o {
	case Off, Const, Func, Shape, Global:
		return 2
	}
	return 1
}

// Width is the number of words an instruction of op takes.
func (op Op) Width() int {
	n := 1
	for _, o := range Ops[op].Operands {
		n += o.Words()
	}
	return n
}

func (op Op) String() string {
	if int(op) < len(Ops) {
		return Ops[op].Name
	}
	return "?"
}
