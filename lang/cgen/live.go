package cgen

import (
	"fmt"
	"strings"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// What a return lets go of. A function's end releases its reference
// registers (out_), each a test of NULL and, when it isn't, a release:
// in a function of a hundred of them (a big match, an interpreter's
// eval), every return pays for all of them. A return instead releases
// those that may hold a reference there (maybeRefs: written with one on
// some way to it, and not cleared, moved or borrowed since), and
// returns at once. The end (an error's unwinding, the code that falls
// off it) still releases them all.

// regset is a set of registers.
type regset []uint64

func newRegset(n int) regset           { return make(regset, (n+63)/64) }
func (s regset) has(r int) bool       { return s[r/64]&(1<<(r%64)) != 0 }
func (s regset) add(r int)            { s[r/64] |= 1 << (r % 64) }
func (s regset) del(r int)            { s[r/64] &^= 1 << (r % 64) }
func (s regset) union(t regset) bool { // whether s grew
	grew := false
	for i := range s {
		if n := s[i] | t[i]; n != s[i] {
			s[i], grew = n, true
		}
	}
	return grew
}

// maybeRefs is, before each instruction (by index), the reference
// registers that may hold a reference there: more than may is safe (a
// release of none does nothing), fewer would leak.
func (g *gen) maybeRefs(ins []insn) []regset {
	f := g.f
	n := len(ins)
	nr := len(f.Kinds)
	at := make(map[int]int, n)
	for k, in := range ins {
		at[in.pc] = k
	}
	in := make([]regset, n+1)
	for k := range in {
		in[k] = newRegset(nr)
	}
	// Parameters it retains as it starts.
	for p := 0; p < int(f.NParams); p++ {
		if f.Kinds[p] == bc.KRef && !g.borrowed[p] {
			in[0].add(p)
		}
	}
	// A handler is come to from anywhere in its range.
	handlerOf := make([][]int, n)
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
	out := func(k int, s regset) regset {
		o := append(regset(nil), s...)
		x := ins[k]
		a := x.args
		name := bc.Ops[x.op].Name
		if g.pl.skip[k] {
			return o
		}
		none := func(r int) {
			if r >= 0 && r < nr {
				o.del(r)
			}
		}
		some := func(r int) {
			if r >= 0 && r < nr && f.Kinds[r] == bc.KRef {
				o.add(r)
			}
		}
		switch {
		case name == "CLR" || name == "KNONE":
			none(a[0])
			return o
		case name == "MOV" && g.isRef(a[0]):
			if s.has(a[1]) {
				some(a[0])
			} else {
				none(a[0])
			}
			return o
		case name == "THROW" || name == "RETHROW" || name == "RET":
			none(a[0])
			return o
		case g.pl.borrow[k] || name == "IDX" && g.pl.inplace[k]:
			// Its register stays none: the pointer is v<r>.
			none(a[0])
			return o
		}
		for _, r := range writes(g.m, x) {
			if (name == "SETIDX" || name == "SETF" || name == "SETIF") && r == a[0] {
				continue
			}
			some(r)
		}
		if g.pl.moveIn[k] {
			v := a[2]
			if name == "SETIF" {
				v = a[3]
			}
			none(v)
		}
		return o
	}
	work := []int{0}
	queued := make([]bool, n)
	seen := make([]bool, n) // reached: its successors seen at least once
	queued[0], seen[0] = true, true
	for len(work) > 0 {
		k := work[len(work)-1]
		work = work[:len(work)-1]
		queued[k] = false
		o := out(k, in[k])
		var succ []int
		x := ins[k]
		name := bc.Ops[x.op].Name
		for i, kind := range bc.Ops[x.op].Operands {
			if kind == bc.Off {
				if t, ok := at[x.args[i]]; ok {
					succ = append(succ, t)
				}
			}
		}
		switch name {
		case "JMP", "RET", "RET0", "THROW", "RETHROW":
		default:
			if k+1 < n {
				succ = append(succ, k+1)
			}
		}
		for _, t := range succ {
			if (in[t].union(o) || !seen[t]) && !queued[t] {
				seen[t], queued[t] = true, true
				work = append(work, t)
			}
		}
		for _, t := range handlerOf[k] {
			grew := in[t].union(in[k])
			grew = in[t].union(o) || grew || !seen[t]
			if grew && !queued[t] {
				seen[t] = true
				queued[t] = true
				work = append(work, t)
			}
		}
	}
	return in[:n]
}

// retRelease is C releasing what may hold a reference before instruction
// k (but keep, -1: none), for a return there.
func (g *gen) retRelease(k, keep int) string {
	var s strings.Builder
	set := g.maybe[k]
	for r, kind := range g.f.Kinds {
		if kind == bc.KRef && r != keep && !g.borrowed[r] && set.has(r) {
			fmt.Fprintf(&s, "ak_release(%s.p); ", g.r(r))
		}
	}
	return s.String()
}
