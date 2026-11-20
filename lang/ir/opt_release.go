package ir

import (
	mbits "math/bits"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// Values let go of after their last read.
//
// A statement's temp holds its value until the statement ends, or longer:
// until its register is written again (the lowering clears only some of
// them: dropTemps); a local holds its until its block ends. In
//
//	total += text.split(" ").count() + text.lines().count() + text.fields().count()
//
// that's the three arrays at once, each a string a piece of a 12 MB
// text, and in
//
//	for s in ints { total += s.toInt() ?? 0 }
//	for i in 0..<n { floats.push("{i.float() / 7.0}") }
//
// ints' million strings while floats' are made: Go's collector frees
// each once it's unreachable. releaseTemps clears such a register (CLR)
// where it's dead (nothing reads it on any way on, an error's to a
// handler included, before it's written again) and something that may
// allocate comes before what would let it go anyway (a call, a string or
// a value made): right after the instruction that reads it last, or, a
// loop's, as the way out of it starts (a block it's dead in that a block
// it's live out of goes to). Only a temp (no name's), or a local whose
// type is data alone (plainData: numbers, strings, the arrays, maps and
// structs of them): what letting go of a native object, a closure or a
// task does (its drop closes, stops, releases) stays where its block
// ends, as it's always been. The lowering records where each register
// is one (Func.tempSpans).
//
// It runs first, on the code as lowered, which the spans index, and
// keeps clear of what the C's places borrow (lang/cgen/places.go): it
// clears no register an element or field was just read out of (IDX,
// GETF, ...: the read may be a pointer borrowed from it). Releasing
// earlier changes only when a value is freed, and for the better for a
// change in place: one less reference to what's changed.
func (o *optimizer) releaseTemps() {
	f := o.f
	spans := f.tempSpans
	f.tempSpans = nil
	if len(spans) == 0 || len(f.Code) == 0 {
		return
	}
	code := f.Code
	n := len(code)
	// spans by register, for covered: few each.
	byReg := make([][]tempSpan, o.n)
	for _, s := range spans {
		if s.reg >= 0 && s.reg < o.n && f.Kinds[s.reg] == bc.KRef {
			byReg[s.reg] = append(byReg[s.reg], s)
		}
	}
	covered := func(r, i int) bool {
		for _, s := range byReg[r] {
			if s.start <= i && i < s.end {
				return true
			}
		}
		return false
	}
	o.cfg()
	live := o.liveness()
	// before and after: the clears to put before and after each
	// instruction.
	before := make([][]int32, n)
	after := make([][]int32, n)
	count := 0
	// After a last read.
	cur := newRegset(o.n)
	for b := range o.blocks {
		o.liveOut(live, b, cur)
		bl := &o.blocks[b]
		o.walkBack(live, b, cur, func(i int, out regset) {
			in := &code[i]
			info := &opInfos[in.Op]
			if info.unknown || info.term || info.jump >= 0 || borrowsFrom[in.Op] {
				return
			}
			o.reads(in, func(r int) {
				if r >= o.n || len(byReg[r]) == 0 || out.has(r) || has32(after[i], int32(r)) || !covered(r, i) || o.writesReg(in, r) {
					return
				}
				if o.worthClearing(i+1, bl.end, r) {
					after[i] = append(after[i], int32(r))
					count++
				}
			})
		})
	}
	// As a block it's dead in starts, come to from one it's live out of
	// (a loop's way out). Not a handler's (its first instruction takes
	// the error).
	handler := make([]bool, n+1)
	for _, h := range f.Handlers {
		if t := f.labels[h.Target]; t >= 0 && t <= n {
			handler[t] = true
		}
	}
	out := newRegset(o.n)
	for p := range o.blocks {
		o.liveOut(live, p, out)
		for _, sb := range o.blocks[p].succ() {
			bl := &o.blocks[sb]
			start := bl.start
			if handler[start] || code[start].Op == opCATCH {
				continue
			}
			for w, bits := range out {
				for bits &^= live[sb][w]; bits != 0; bits &= bits - 1 {
					r := w*64 + bits64(bits)
					if r >= o.n || len(byReg[r]) == 0 || has32(before[start], int32(r)) || !covered(r, start) {
						continue
					}
					if o.worthClearing(start, bl.end, r) {
						before[start] = append(before[start], int32(r))
						count++
					}
				}
			}
		}
	}
	if count == 0 {
		return
	}
	moved := make([]Instr, 0, n+count)
	newIdx := make([]int, n+1)
	for i := range code {
		// A label at i is at its clears before it: a jump there clears.
		newIdx[i] = len(moved)
		for _, r := range before[i] {
			moved = append(moved, Instr{Op: opCLR, Args: [5]int32{r}, Line: code[i].Line})
		}
		moved = append(moved, code[i])
		for _, r := range after[i] {
			moved = append(moved, Instr{Op: opCLR, Args: [5]int32{r}, Line: code[i].Line})
		}
	}
	newIdx[n] = len(moved)
	f.Code = moved
	for l, i := range f.labels {
		if i >= 0 {
			f.labels[l] = newIdx[i]
		}
	}
	o.changed = true
}

// bits64 is the index of x's lowest set bit.
func bits64(x uint64) int { return mbits.TrailingZeros64(x) }

func has32(s []int32, r int32) bool {
	for _, x := range s {
		if x == r {
			return true
		}
	}
	return false
}

// borrowsFrom are the reads whose result C code may borrow from the
// register read (an element's, a field's, a box's value), not retain: a
// clear of that register right after would free what it borrowed (the
// C's places then retain it instead).
var borrowsFrom = map[bc.Op]bool{
	op("IDX"): true, op("IDXF"): true, op("GETF"): true, op("GETP"): true, op("UNBOX"): true,
}

// allocs are the instructions that make a value without calling: what
// a dead temp held through would add to.
var allocs = map[bc.Op]bool{
	op("TOSTR"): true, op("CONCAT"): true, op("INTERP"): true, op("BOX"): true,
	op("NEWS"): true, op("NEWA"): true, op("NEWE"): true, op("NEWM"): true, op("CLOS"): true,
}

// worthClearing reports whether clearing r before code[from:end) (the
// rest of its block) lets its value go before something may allocate:
// a call or a value made comes before r is written or cleared there, or
// r isn't, and it goes on to other blocks.
func (o *optimizer) worthClearing(from, end, r int) bool {
	code := o.f.Code
	for k := from; k < end; k++ {
		c := &code[k]
		if c.Op == opNOP {
			continue
		}
		info := &opInfos[c.Op]
		if info.unknown || !info.pure || allocs[c.Op] {
			return true
		}
		if o.writesReg(c, r) {
			return false
		}
	}
	return true
}
