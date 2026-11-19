package cgen

import (
	bc "github.com/egoist/asuka/lang/bytecode"
)

// Stack strings: escape analysis for the strings a function makes.
//
// A string an INTERP (a + b + c, an interpolation) or a CONCAT (a + b)
// makes, whose value goes nowhere but into what reads it in place, is
// built in a buffer in the function's C frame (rt.h's ak_strbuf: 63
// bytes and its NUL) when it fits, rather than allocated and freed: Go's
// concatstrings writes a result that doesn't escape into the caller's
// 32-byte tmpBuf, and Swift promotes what doesn't escape to the stack,
// alike. A longer one is made on the heap, as any.
//
// The value is followed forward, over the function's control flow
// (handlers' edges too), as the set of registers that may hold it
// (holders): a copy (MOV, MOVR) adds its destination, a write of any
// other kind takes the register out. Every instruction that reads a
// holder must be one that reads a string where it is and keeps nothing:
// its length (LEN, byteCount), ==, <, compare, contains, hasPrefix,
// hasSuffix, find, count, a map's lookup by it, an interpolation of it
// into another string, print, a test of none. Anything else (a return,
// a store into an array, a field, a global or a box, a capture, a call
// of a function or a closure, a builtin that may keep its argument, a
// throw, a mut fn's receiver, which goes back to its caller) is an
// escape: the string is made on the heap. And the buffer is
// written again only where no holder of its last value is left: a
// string made in a loop whose value a register carries into the next
// turn is made on the heap.
//
// The string is immortal (rt.h's AK_RC_IMMORTAL: a release, at a CLR,
// a return or an error's unwinding, does nothing) and flagged
// AK_STR_STACK, so that a retain of it, which would be a keep this
// analysis missed, panics in the tests' builds (AK_TEST); a
// copy into another register (MOVR) retains with ak_retain_local, which
// allows it. A function that may suspend makes none (its registers move
// to a frame on the heap).

// maxStackStrs is how many strings a function builds on its stack at
// most (each a buffer of 80 bytes in its frame).
const maxStackStrs = 8

// readsInPlace are the builtins that read their string arguments where
// they are and keep none of them (lib.c's and the intrinsics').
var readsInPlace = map[string]bool{
	"print": true, "print.text": true, "hash": true,
	"str.len": true, "str.bytes_len": true, "str.contains": true, "str.has_prefix": true, "str.has_suffix": true,
	"str.find": true, "str.find_last": true, "str.is_empty": true, "str.to_int": true, "str.to_float": true,
	"str.equals": true, "str.compare": true,
}

// cfg is a function's instructions' successors (falling through,
// jumping) and the handlers each may throw to.
func cfg(f *bc.FuncDef, ins []insn) (succ, handlerOf [][]int) {
	n := len(ins)
	at := make(map[int]int, n)
	for k, in := range ins {
		at[in.pc] = k
	}
	succ = make([][]int, n)
	handlerOf = make([][]int, n)
	for k, x := range ins {
		for i, kind := range bc.Ops[x.op].Operands {
			if kind == bc.Off {
				if t, ok := at[x.args[i]]; ok {
					succ[k] = append(succ[k], t)
				}
			}
		}
		switch bc.Ops[x.op].Name {
		case "JMP", "RET", "RET0", "THROW", "RETHROW":
		default:
			if k+1 < n {
				succ[k] = append(succ[k], k+1)
			}
		}
	}
	for _, h := range f.Handlers {
		t, ok := at[int(h.Target)]
		if !ok {
			continue
		}
		for k, x := range ins {
			if x.pc >= int(h.Start) && x.pc < int(h.End) {
				handlerOf[k] = append(handlerOf[k], t)
			}
		}
	}
	return succ, handlerOf
}

// stackStrs are the instructions (by index) whose string is made in a
// buffer of the frame (stackStr: S<k>_), and the MOVRs that may copy one
// (stackCopy: retained with ak_retain_local).
func (g *gen) stackStrs(ins []insn) (stack, copies map[int]bool) {
	stack, copies = map[int]bool{}, map[int]bool{}
	if g.async {
		return
	}
	f := g.f
	nr := len(f.Kinds)
	var succ, handlerOf [][]int
	for p, x := range ins {
		if len(stack) == maxStackStrs {
			break
		}
		a := x.args
		switch bc.Ops[x.op].Name {
		case "INTERP":
		case "CONCAT":
			if a[0] == a[1] {
				continue // s += x: appended in place
			}
		default:
			continue
		}
		if !g.isRef(a[0]) || g.pl.skip[p] {
			continue
		}
		if succ == nil {
			succ, handlerOf = cfg(f, ins)
		}
		if moves, ok := g.escapes(ins, succ, handlerOf, p, nr); ok {
			stack[p] = true
			for k := range moves {
				copies[k] = true
			}
		}
	}
	return
}

// escapes follows the string instruction p makes (its holders, before
// each instruction) to where nothing holds it: ok when it never escapes
// and isn't made again while held; moves are the MOVRs that copy it.
func (g *gen) escapes(ins []insn, succ, handlerOf [][]int, p, nr int) (moves map[int]bool, ok bool) {
	n := len(ins)
	in := make([]regset, n)
	moves = map[int]bool{}
	var work []int
	queued := make([]bool, n)
	reach := func(t int, s regset) {
		if empty(s) {
			return
		}
		if in[t] == nil {
			in[t] = newRegset(nr)
		}
		if in[t].union(s) && !queued[t] {
			queued[t] = true
			work = append(work, t)
		}
	}
	// A mut fn's receiver (register 0) goes back to its caller as it
	// returns: a string put there escapes.
	mutSelf := g.f.Flags&bc.FMut != 0 && g.f.NParams > 0
	if mutSelf && ins[p].args[0] == 0 {
		return nil, false
	}
	start := newRegset(nr)
	start.add(ins[p].args[0])
	for _, t := range succ[p] {
		reach(t, start)
	}
	for len(work) > 0 {
		k := work[len(work)-1]
		work = work[:len(work)-1]
		queued[k] = false
		s := in[k]
		if k == p {
			// Made again while a holder of the last one is left.
			return nil, false
		}
		out, isMove, allowed := g.holderStep(ins[k], k, s)
		if !allowed || mutSelf && out.has(0) {
			return nil, false
		}
		if isMove {
			moves[k] = true
		}
		for _, t := range succ[k] {
			reach(t, out)
		}
		for _, t := range handlerOf[k] {
			// An error: the registers as they were before it, or after.
			u := append(regset(nil), s...)
			u.union(out)
			reach(t, u)
		}
	}
	return moves, true
}

func empty(s regset) bool {
	for _, w := range s {
		if w != 0 {
			return false
		}
	}
	return true
}

// blockBase is the operand that's the first register of an
// instruction's block (blockOf).
var blockBase = map[string]int{
	"CALL": 2, "CALLB": 2, "CALLC": 2, "CALLU": 2, "CALLI": 2, "CLOS": 2, "NEWS": 2,
	"NEWE": 3, "NEWA": 3, "NEWM": 3, "INTERP": 1, "CPATH": 3, "CLEN": 3, "CPUTP": 3, "ACCP": 3, "AGETP": 3,
}

// rmw are the instructions whose operand 0 is read too (changed in
// place), not only written.
var rmw = map[string]bool{"SETF": true, "SETIDX": true, "SETIF": true, "MSET": true, "CONCAT": true}

// holderStep is what instruction x (k) does to the set s of registers
// that may hold the string: the set after it; whether it's a MOVR copying
// one; and whether every read of a holder it makes is one in place.
func (g *gen) holderStep(x insn, k int, s regset) (out regset, isMove, ok bool) {
	m := g.m
	d := bc.Ops[x.op]
	name := d.Name
	a := x.args
	out = append(regset(nil), s...)
	held := func(r int) bool { return r != bc.None && r >= 0 && r < len(g.f.Kinds) && s.has(r) }
	ws := writes(m, x)
	if g.pl.skip[k] {
		// Code that isn't written: nothing of it may name a holder.
		for _, r := range append(regs(m, x), ws...) {
			if held(r) {
				return nil, false, false
			}
		}
		return out, false, true
	}
	// Its operands that read a holder, but operand 0 when it's only
	// written.
	dest0 := len(d.Operands) > 0 && d.Operands[0] == bc.Reg && has(ws, a[0]) && !rmw[name]
	b, nb := blockOf(m, x)
	base, isBlock := blockBase[name]
	for o, kind := range d.Operands {
		if kind != bc.Reg || !held(a[o]) || (o == 0 && dest0) || (isBlock && o == base) {
			// (a block's first register: the block's, below)
			continue
		}
		switch {
		case (name == "MOV" || name == "MOVR") && o == 1:
		case (name == "EQ" || name == "NE" || name == "LTS") && (o == 1 || o == 2):
		case (name == "ISNONE" || name == "LEN") && o == 1:
		case (name == "JNONE" || name == "JSOME") && o == 0:
		case (name == "MGET" || name == "MGETJ") && o == 2:
		default:
			return nil, false, false
		}
	}
	// Its block: an interpolation's parts, a builtin's arguments that
	// it reads in place.
	reads := name == "INTERP" || name == "CALLB" && readsInPlace[m.Builtins[a[1]]]
	for r := b; r < b+nb; r++ {
		if held(r) && !reads {
			return nil, false, false
		}
	}
	if (name == "MOV" || name == "MOVR") && held(a[1]) {
		if a[0] != a[1] {
			out.add(a[0])
		}
		return out, name == "MOVR", true
	}
	for _, r := range ws {
		if reads && r >= b && r < b+nb {
			continue // a builtin's block, copied back as it was
		}
		if r >= 0 && r < len(g.f.Kinds) {
			out.del(r)
		}
	}
	return out, false, true
}
