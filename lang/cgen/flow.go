package cgen

import (
	"math"
	"math/bits"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// Facts across the control flow: what a check made known (an array
// unique, an index in its range, an element unique: plan.unique,
// inRange, uniqueElem) holds where every way in has it, not only along
// the straight path after it, and a loop's counter is known in range
// from its test, so a loop over 0..<xs.count() checks no index:
//
//   - an int register known not negative: a constant, a length, a sum of
//     ones not negative (a checked one, which throws rather than wraps;
//     a wrapping +1 of one known less than another int, which can't
//     reach past int's range), a jump's test against 0 (JGEIS...);
//   - a < b, an int's test's: JLTI's taken way, JLEI's other;
//   - n is the length of the array in c (LEN): it stays while neither is
//     written (a SETIF or a SETIDX changes an element, not the length);
//   - an int register's magnitude: |x| < 2^b. A length or an index an
//     instruction took is less than 2^32 (ak_arr's len is a uint32), a
//     constant its own; a sum or a difference of two known ones is less
//     than 2^(the larger b + 1), a product 2^(their sum). An add, a
//     subtract or a multiply whose result is known below 2^63 that way
//     can't overflow: it's not checked (sieve's j += i, of two indexes).
//     The fact stays until its register is written, whatever happens to
//     the array; where ways in disagree it's dropped (rather than taken
//     the larger, so a loop that grows it gets there at once);
//
// and i is in c's range when it's not negative and less than a length
// of c. Each fact is dropped where a register it names is written (an
// argument block a call may change included), uniqueness where a
// reference to the value is made or passed on.
//
// A loop whose first straight path (what runs every turn, from its
// test's jump) changes an element of an array in place gets the array
// made unique as it's entered (hoist): one check before the loop rather
// than one a turn. Its element at an index the loop doesn't change, in
// range there, likewise. Neither changes what the program does: a copy
// made sooner is the copy the loop would make, or one no one sees.
//
// None of it depends on how an array stores its elements (rt.h's
// ak_elem): a length and an index's range are the same for a packed
// [bool] or [u8], ak_arr_mut copies a packed array packed, and the
// elements made unique are structs, which are values. The instructions
// read and write the elements as their arrays store them (insn.go's
// loadElem, storeElem, elemRef).
//
// The analysis is forward, over the straight paths (places.go's), each
// path's facts those all its ways in have (a handler's or a resumption's
// none), to a fixed point.

type pair struct{ c, i int }

// facts is what's known at a point.
type facts struct {
	uniq   map[int]bool
	rng    map[pair]bool // {c, i}: i in c's range
	elem   map[pair]bool // {c, i}: c's element at i unique
	nonneg map[int]bool
	lt     map[pair]bool // {a, b}: a < b
	ln     map[pair]bool // {n, c}: n is c's length
	eqlen  map[pair]bool // {c, d} (and {d, c}): arrays as long (a copy)
	// cmp: a bool register's comparison, a < b (LTI's), for the jumps on
	// it; sum: an int register's value as another's plus a constant
	// (ADDIS, ADDIK), so that two registers computed alike are known
	// alike.
	cmp map[int]pair
	sum map[int]pair
	// mag: an int register known |x| < 2^mag[x].
	mag map[int]int
}

func newFacts() *facts {
	return &facts{uniq: map[int]bool{}, rng: map[pair]bool{}, elem: map[pair]bool{}, nonneg: map[int]bool{}, lt: map[pair]bool{}, ln: map[pair]bool{}, eqlen: map[pair]bool{}, cmp: map[int]pair{}, sum: map[int]pair{}, mag: map[int]int{}}
}

func (s *facts) copy() *facts {
	t := newFacts()
	for k := range s.uniq {
		t.uniq[k] = true
	}
	for k := range s.rng {
		t.rng[k] = true
	}
	for k := range s.elem {
		t.elem[k] = true
	}
	for k := range s.nonneg {
		t.nonneg[k] = true
	}
	for k := range s.lt {
		t.lt[k] = true
	}
	for k := range s.ln {
		t.ln[k] = true
	}
	for k := range s.eqlen {
		t.eqlen[k] = true
	}
	for k, v := range s.cmp {
		t.cmp[k] = v
	}
	for k, v := range s.sum {
		t.sum[k] = v
	}
	for k, v := range s.mag {
		t.mag[k] = v
	}
	return t
}

// meet keeps what both know; whether s changed.
func (s *facts) meet(t *facts) bool {
	changed := false
	for k := range s.uniq {
		if !t.uniq[k] {
			delete(s.uniq, k)
			changed = true
		}
	}
	for _, p := range []struct{ a, b map[pair]bool }{{s.rng, t.rng}, {s.elem, t.elem}, {s.lt, t.lt}, {s.ln, t.ln}, {s.eqlen, t.eqlen}} {
		for k := range p.a {
			if !p.b[k] {
				delete(p.a, k)
				changed = true
			}
		}
	}
	for k := range s.nonneg {
		if !t.nonneg[k] {
			delete(s.nonneg, k)
			changed = true
		}
	}
	for _, p := range []struct{ a, b map[int]pair }{{s.cmp, t.cmp}, {s.sum, t.sum}} {
		for k, v := range p.a {
			if w, ok := p.b[k]; !ok || w != v {
				delete(p.a, k)
				changed = true
			}
		}
	}
	// A magnitude the other way has larger is widened: to an index's (a
	// loop entered with a constant, whose counter is an index after), or
	// past it, dropped; so a counter that grows takes two turns of the
	// analysis, not one a bit.
	for k, b := range s.mag {
		c, ok := t.mag[k]
		switch {
		case !ok || c > idxMag:
			delete(s.mag, k)
			changed = true
		case c > b:
			s.mag[k] = idxMag
			changed = true
		}
	}
	return changed
}

// inRange reports whether i is known in c's range.
func (s *facts) inRange(c, i int) bool {
	if s.rng[pair{c, i}] {
		return true
	}
	if !s.nonneg[i] {
		return false
	}
	for p := range s.lt {
		if p.c != i {
			continue
		}
		if s.ln[pair{p.i, c}] {
			return true
		}
		for q := range s.eqlen {
			if q.c == c && s.ln[pair{p.i, q.i}] {
				return true
			}
		}
	}
	return false
}

// below reports whether a is known less than some int (so a + 1 doesn't
// wrap).
func (s *facts) below(a int) bool {
	for p := range s.lt {
		if p.c == a {
			return true
		}
	}
	return false
}

// forget drops what's known of r's value: it's written.
func (s *facts) forget(r int) {
	delete(s.uniq, r)
	delete(s.nonneg, r)
	delete(s.mag, r)
	for _, m := range []map[pair]bool{s.rng, s.elem, s.lt, s.ln, s.eqlen} {
		for p := range m {
			if p.c == r || p.i == r {
				delete(m, p)
			}
		}
	}
	for k, p := range s.cmp {
		if k == r || p.c == r || p.i == r {
			delete(s.cmp, k)
		}
	}
	for k, p := range s.sum {
		if k == r || p.c == r {
			delete(s.sum, k)
		}
	}
}

// share drops what's known of c's uniqueness: a reference to it is made
// or passed on (its length and its indexes' ranges stay: what holds it
// too can't change it in place).
func (s *facts) share(c int) {
	delete(s.uniq, c)
	s.forgetElems(c)
}

func (s *facts) forgetElems(c int) {
	for p := range s.elem {
		if p.c == c {
			delete(s.elem, p)
		}
	}
}

// hoist is what's made known as a loop is entered (on the way that falls
// into its first instruction): its arrays made unique, then their
// elements at the indexes (each {c, i}) when they're in range there.
type hoist struct {
	arrs  []int
	elems []pair
}

// apply makes h's facts known in s, those it can: what the C before the
// loop does (hoistC).
func (h *hoist) apply(s *facts) []pair {
	for _, c := range h.arrs {
		s.uniq[c] = true
	}
	var done []pair
	for _, p := range h.elems {
		if s.uniq[p.c] && s.inRange(p.c, p.i) {
			s.elem[p] = true
			done = append(done, p)
		}
	}
	return done
}

// flow runs the analysis over f's straight paths (starting at those
// instructions next doesn't reach, through next) and records what each
// array instruction may leave out (plan.unique, inRange, uniqueElem) and
// the loops' hoists (plan.hoists).
func (pl *plan) flow(m *bc.Module, fd *bc.FuncDef, ins []insn, next func(int) int, entries map[int]bool) {
	n := len(ins)
	at := make(map[int]int, n)
	for k, in := range ins {
		at[in.pc] = k
	}
	isNext := make([]bool, n)
	for k := range ins {
		if j := next(k); j >= 0 {
			isNext[j] = true
		}
	}
	var starts []int
	pathOf := make([]int, n) // a path start's number
	for k := range ins {
		pathOf[k] = -1
		if !isNext[k] {
			pathOf[k] = len(starts)
			starts = append(starts, k)
		}
	}
	paths := make([][]int, len(starts))
	for p, k0 := range starts {
		for k := k0; k >= 0; k = next(k) {
			paths[p] = append(paths[p], k)
		}
	}
	name := func(k int) string { return bc.Ops[ins[k].op].Name }

	// An IDXF whose element a SETIF changes further on its path, neither
	// the array's register nor the index's written between (nor the
	// array borrowed there): plan.changes.
	for _, path := range paths {
		for p, k := range path {
			in := ins[k]
			if name(k) != "IDXF" || pl.skip[k] || pl.borrow[k] || has(pl.active[k], in.args[1]) {
				continue
			}
			c, i := in.args[1], in.args[2]
			if in.args[0] == c || in.args[0] == i {
				continue
			}
			for _, q := range path[p+1:] {
				if pl.skip[q] {
					continue
				}
				a := ins[q].args
				if name(q) == "SETIF" && a[0] == c && a[1] == i {
					pl.changes[k] = true
					break
				}
				ws := writes(m, ins[q])
				if (has(ws, c) && name(q) != "SETIF") || has(ws, i) {
					break
				}
			}
		}
	}

	// The loops' hoists: a path whose start a later instruction jumps
	// back to, entered by falling into it, that changes an element of an
	// array in place.
	backTo := make([]bool, n)
	for k, in := range ins {
		for o, kind := range bc.Ops[in.op].Operands {
			if kind == bc.Off {
				if t, ok := at[in.args[o]]; ok && t <= k {
					backTo[t] = true
				}
			}
		}
	}
	falls := func(k int) bool {
		switch name(k) {
		case "JMP", "RET", "RET0", "THROW", "RETHROW":
			return false
		}
		return true
	}
	pl.hoists = map[int]*hoist{}
	for p, k0 := range starts {
		if !backTo[k0] || k0 == 0 || !falls(k0-1) || entries[ins[k0].pc] {
			continue
		}
		path := paths[p]
		// What the loop's instructions (from its start to its last jump
		// back) write.
		last := k0
		for k, in := range ins {
			for o, kind := range bc.Ops[in.op].Operands {
				if kind == bc.Off {
					if t, ok := at[in.args[o]]; ok && t == k0 && k > last {
						last = k
					}
				}
			}
		}
		written := map[int]bool{}
		for k := k0; k <= last; k++ {
			for _, r := range writes(m, ins[k]) {
				if (name(k) == "SETIDX" || name(k) == "SETIF") && r == ins[k].args[0] {
					continue
				}
				written[r] = true
			}
		}
		h := &hoist{}
		seen := map[int]bool{}
		seenE := map[pair]bool{}
		for _, k := range path {
			if pl.skip[k] {
				continue
			}
			a := ins[k].args
			c, i := -1, -1
			switch name(k) {
			case "SETIF":
				c, i = a[0], a[1]
			case "SETIDX":
				c = a[0]
			case "IDX":
				if pl.inplace[k] {
					c, i = a[1], a[2]
				} else if pl.moveOut[k] {
					c = a[1]
				}
			case "IDXF":
				if pl.changes[k] {
					c, i = a[1], a[2]
				}
			}
			if c < 0 || written[c] || pl.virt[c] || fd.Kinds[c] != bc.KRef {
				continue
			}
			if !seen[c] {
				seen[c] = true
				h.arrs = append(h.arrs, c)
			}
			if i >= 0 && !written[i] && !seenE[pair{c, i}] && (name(k) == "SETIF" || name(k) == "IDXF") {
				seenE[pair{c, i}] = true
				h.elems = append(h.elems, pair{c, i})
			}
		}
		if len(h.arrs) > 0 {
			pl.hoists[k0] = h
		}
	}

	// The fixed point: each path's facts in (nil: no way in seen yet).
	in := make([]*facts, len(starts))
	in[0] = newFacts()
	for p, k0 := range starts {
		if entries[ins[k0].pc] {
			in[p] = newFacts()
		}
	}
	type edge struct {
		to   int
		fact func(*facts)
	}
	// succs is where a path goes on to from its last instruction k, with
	// what each way makes known (a test's).
	succs := func(k int, out *facts) []edge {
		in := ins[k]
		a := in.args
		var es []edge
		target := -1
		for o, kind := range bc.Ops[in.op].Operands {
			if kind == bc.Off {
				if t, ok := at[a[o]]; ok {
					target = t
				}
			}
		}
		lt := func(x, y int) func(*facts) { return func(s *facts) { s.lt[pair{x, y}] = true } }
		nonneg := func(x int) func(*facts) { return func(s *facts) { s.nonneg[x] = true } }
		var taken, fall func(*facts)
		switch name(k) {
		case "JT":
			if p, ok := out.cmp[a[0]]; ok {
				taken = lt(p.c, p.i)
			}
		case "JF":
			if p, ok := out.cmp[a[0]]; ok {
				fall = lt(p.c, p.i)
			}
		case "JLTI":
			taken = lt(a[0], a[1])
		case "JLEI":
			fall = lt(a[1], a[0])
		case "JGEIS":
			if imm(in, 1) >= 0 {
				taken = nonneg(a[0])
			}
		case "JGTIS":
			if imm(in, 1) >= -1 {
				taken = nonneg(a[0])
			}
		case "JLTIS":
			if imm(in, 1) >= 0 {
				fall = nonneg(a[0])
			}
		case "JLEIS":
			if imm(in, 1) >= -1 {
				fall = nonneg(a[0])
			}
		}
		if target >= 0 {
			es = append(es, edge{target, taken})
		}
		if falls(k) && name(k) != "JMP" && k+1 < n {
			es = append(es, edge{k + 1, fall})
		}
		return es
	}
	work := []int{0}
	queued := make([]bool, len(starts))
	queued[0] = true
	for p := range starts {
		if in[p] != nil && !queued[p] {
			work = append(work, p)
			queued[p] = true
		}
	}
	for len(work) > 0 {
		p := work[0]
		work = work[1:]
		queued[p] = false
		s := in[p].copy()
		path := paths[p]
		for _, k := range path {
			pl.step(m, fd, ins, k, s, false)
		}
		for _, e := range succs(path[len(path)-1], s) {
			q := pathOf[e.to]
			if q < 0 || entries[ins[e.to].pc] {
				continue
			}
			t := s.copy()
			if e.fact != nil {
				e.fact(t)
			}
			if h := pl.hoists[e.to]; h != nil && e.to == path[len(path)-1]+1 && falls(path[len(path)-1]) {
				h.apply(t)
			}
			if in[q] == nil {
				in[q] = t
			} else if !in[q].meet(t) {
				continue
			}
			if !queued[q] {
				queued[q] = true
				work = append(work, q)
			}
		}
	}
	// Recorded: what each instruction relies on, from its path's facts in.
	for p, path := range paths {
		s := in[p]
		if s == nil {
			s = newFacts() // unreachable
		} else {
			s = s.copy()
		}
		for _, k := range path {
			pl.step(m, fd, ins, k, s, true)
		}
	}
	// The hoists' elements made unique where they're known in range on
	// the way in (what the C before the loop does): the facts at the end
	// of the path that falls into the loop, with what its last
	// instruction's test says on the way.
	for k0, h := range pl.hoists {
		var s *facts
		for p, path := range paths {
			if path[len(path)-1] != k0-1 || in[p] == nil {
				continue
			}
			s = in[p].copy()
			for _, k := range path {
				pl.step(m, fd, ins, k, s, false)
			}
			for _, e := range succs(k0-1, s) {
				if e.to == k0 && e.fact != nil {
					e.fact(s)
				}
			}
		}
		if s == nil {
			h.elems = nil
			continue
		}
		h.elems = h.apply(s)
	}
}

// step is what instruction k does to the facts s (and with record, what
// it may leave out because of them, into the plan).
func (pl *plan) step(m *bc.Module, fd *bc.FuncDef, ins []insn, k int, s *facts, record bool) {
	if pl.skip[k] {
		return
	}
	in := ins[k]
	a := in.args
	nm := bc.Ops[in.op].Name
	if record {
		switch nm {
		case "IDX":
			pl.inRange[k] = s.inRange(a[1], a[2])
			if pl.inplace[k] || pl.moveOut[k] {
				pl.unique[k] = s.uniq[a[1]]
				pl.uniqueElem[k] = s.elem[pair{a[1], a[2]}]
			}
		case "IDXF":
			pl.inRange[k] = s.inRange(a[1], a[2])
			if pl.changes[k] {
				pl.unique[k] = s.uniq[a[1]]
				pl.uniqueElem[k] = s.elem[pair{a[1], a[2]}]
			}
		case "SETIDX":
			pl.inRange[k] = s.inRange(a[0], a[1])
			pl.unique[k] = s.uniq[a[0]]
		case "SETIF":
			pl.inRange[k] = s.inRange(a[0], a[1])
			pl.unique[k] = s.uniq[a[0]]
			pl.uniqueElem[k] = s.elem[pair{a[0], a[1]}]
		case "ADDIS":
			// x + 1 of an x less than an int can't overflow.
			pl.noOverflow[k] = imm(in, 2) == 1 && s.below(a[1]) || magOf(m, nm, s, in) >= 0
		case "ADDI", "SUBI", "MULI", "SUBIS", "MULIS":
			pl.noOverflow[k] = magOf(m, nm, s, in) >= 0
		}
	}
	// Its result's magnitude, from its operands as they were.
	mag := magOf(m, nm, s, in)
	// What it computes, from its operands as they were.
	nonneg := false
	var lts []pair
	var cmp, sum *pair
	switch nm {
	case "MOV":
		if a[0] != a[1] && fd.Kinds[a[0]] == bc.KInt {
			sum = &pair{a[1], 0}
		}
	case "LTI":
		if a[0] != a[1] && a[0] != a[2] {
			cmp = &pair{a[1], a[2]}
		}
	case "ADDIS", "ADDIK":
		k := int(imm(in, 2))
		if a[0] != a[1] {
			sum = &pair{a[1], k}
		}
		// Computed as another was: what's known of it is known of it.
		for r, p := range s.sum {
			if p == (pair{a[1], k}) && r != a[0] {
				for q := range s.lt {
					if q.c == r {
						lts = append(lts, pair{a[0], q.i})
					}
				}
			}
		}
	}
	switch nm {
	case "KSMALL":
		nonneg = int16(a[1]) >= 0
	case "KINT":
		nonneg = m.Ints[a[1]] >= 0
	case "LEN":
		nonneg = true
	case "MOV":
		if fd.Kinds[a[0]] == bc.KInt && a[0] != a[1] {
			nonneg = s.nonneg[a[1]]
			for p := range s.lt {
				if p.c == a[1] {
					lts = append(lts, pair{a[0], p.i})
				}
				if p.i == a[1] {
					lts = append(lts, pair{p.c, a[0]})
				}
			}
		}
	case "ADDIS":
		nonneg = s.nonneg[a[1]] && imm(in, 2) >= 0 // checked: it throws rather than wraps
	case "ADDI", "MULI":
		nonneg = s.nonneg[a[1]] && s.nonneg[a[2]] // checked: they throw rather than wrap
	case "MULIS":
		nonneg = s.nonneg[a[1]] && imm(in, 2) >= 0
	case "ADDIK":
		// Wrapping: +1 of one less than an int can't wrap.
		nonneg = s.nonneg[a[1]] && int16(a[2]) == 1 && s.below(a[1])
	case "DIVIS", "MODIS":
		nonneg = s.nonneg[a[1]] && imm(in, 2) > 0
	}
	var lens []pair
	if nm == "MOV" && fd.Kinds[a[0]] == bc.KInt && a[0] != a[1] {
		for p := range s.ln {
			if p.c == a[1] {
				lens = append(lens, pair{a[0], p.i})
			}
		}
	}
	// A reference's copy is the same array: its lengths are the copy's
	// (which stay as the array is made unique, a copy as long).
	var arrLens []pair
	if nm == "MOVR" && a[0] != a[1] {
		for p := range s.ln {
			if p.i == a[1] {
				arrLens = append(arrLens, pair{p.c, a[0]})
			}
		}
	}
	// What it reads or passes on, then what it writes, then what it makes
	// known.
	for _, r := range regs(m, in) {
		if r < len(fd.Kinds) && fd.Kinds[r] == bc.KRef && !pl.pureRead(m, in, r) &&
			!((nm == "SETIDX" || nm == "SETIF") && r == a[0]) && !(nm == "IDX" && (pl.inplace[k] || pl.moveOut[k]) && r == a[1]) &&
			!(nm == "IDXF" && pl.changes[k] && r == a[1]) && !(nm == "SETF" && r == a[0]) {
			s.share(r)
		}
	}
	for _, r := range writes(m, in) {
		if (nm == "SETIDX" || nm == "SETF" || nm == "SETIF") && r == a[0] {
			continue
		}
		s.forget(r)
	}
	if mag >= 0 && a[0] != bc.None && fd.Kinds[a[0]] == bc.KInt {
		s.mag[a[0]] = mag
	}
	switch nm {
	case "KSMALL", "KINT", "LEN", "MOV", "ADDIS", "ADDI", "ADDIK", "DIVIS", "MODIS", "MULI", "MULIS":
		if a[0] == bc.None || fd.Kinds[a[0]] != bc.KInt {
			break
		}
		if nonneg {
			s.nonneg[a[0]] = true
		}
		for _, p := range lts {
			if p.c != p.i {
				s.lt[p] = true
			}
		}
		for _, p := range lens {
			s.ln[p] = true
		}
	}
	for _, p := range arrLens {
		s.ln[p] = true
	}
	// An index an instruction took (it threw otherwise) is in range: not
	// negative, less than the array's lengths.
	var c, i int = -1, -1
	switch nm {
	case "IDX", "IDXF":
		if a[0] != a[1] && a[0] != a[2] {
			c, i = a[1], a[2]
		}
	case "SETIDX", "SETIF":
		c, i = a[0], a[1]
	}
	if i >= 0 && fd.Kinds[i] == bc.KInt {
		// And of its copies.
		same := []int{i}
		for r, p := range s.sum {
			if p == (pair{i, 0}) {
				same = append(same, r)
			}
		}
		if p, ok := s.sum[i]; ok && p.i == 0 {
			same = append(same, p.c)
		}
		for _, x := range same {
			s.nonneg[x] = true
			if b, ok := s.mag[x]; !ok || b > idxMag {
				s.mag[x] = idxMag
			}
			for p := range s.ln {
				if p.i == c {
					s.lt[pair{x, p.c}] = true
				}
			}
		}
	}
	if cmp != nil {
		s.cmp[a[0]] = *cmp
	}
	if sum != nil {
		s.sum[a[0]] = *sum
	}
	if nm == "MOVR" && a[0] != a[1] {
		s.eqlen[pair{a[0], a[1]}] = true
		s.eqlen[pair{a[1], a[0]}] = true
	}
	switch nm {
	case "LEN":
		if a[0] != a[1] {
			s.ln[pair{a[0], a[1]}] = true
		}
	case "IDX":
		c, i, t := a[1], a[2], a[0]
		if t != c && t != i {
			s.rng[pair{c, i}] = true
			switch {
			case pl.inplace[k]:
				s.uniq[c] = true
				s.elem[pair{c, i}] = true
			case pl.moveOut[k]:
				s.uniq[c] = true
				s.forgetElems(c)
			case !pl.borrow[k]:
				s.forgetElems(c) // retained into t: shared
			}
		}
	case "IDXF":
		// The element read where it is, not retained: it stays as unique
		// as it was (made so, when the IDXF changes).
		if t, c, i := a[0], a[1], a[2]; t != c && t != i {
			s.rng[pair{c, i}] = true
			if pl.changes[k] {
				s.uniq[c] = true
				s.elem[pair{c, i}] = true
			}
		}
	case "SETIDX":
		c, i := a[0], a[1]
		s.uniq[c] = true
		s.rng[pair{c, i}] = true
		s.forgetElems(c)
	case "SETIF":
		// The array made unique, then its element in it.
		c, i := a[0], a[1]
		s.uniq[c] = true
		s.rng[pair{c, i}] = true
		s.elem[pair{c, i}] = true
	}
}

// pureRead reports whether in reads register r only as its value is
// read without being retained or passed on: its operand positions
// (srcPos, but MOVR's, which keeps it).
func (pl *plan) pureRead(m *bc.Module, in insn, r int) bool {
	nm := bc.Ops[in.op].Name
	if nm == "MOVR" {
		return false
	}
	pos := srcPos[nm]
	d := bc.Ops[in.op]
	ops := 0
	for o, kind := range d.Operands {
		if kind == bc.Reg && in.args[o] == r {
			ops++
			ok := false
			for _, p := range pos {
				ok = ok || p == o
			}
			if !ok {
				return false
			}
		}
	}
	return ops == len(filter(regs(m, in), r))
}

// idxMag is a length's and an index's magnitude: ak_arr's len is a
// uint32.
const idxMag = 32

// magOf is the magnitude (|x| < 2^b) of what in computes, from what s
// knows of its operands, or -1: unknown, or not below 2^63 (an add, a
// subtract or a multiply that might overflow; one of a result known
// that way can't).
func magOf(m *bc.Module, nm string, s *facts, in insn) int {
	a := in.args
	of := func(r int) int {
		if b, ok := s.mag[r]; ok {
			return b
		}
		return -1
	}
	k := func(v int64) int {
		if v == math.MinInt64 {
			return -1
		}
		if v < 0 {
			v = -v
		}
		return bits.Len64(uint64(v))
	}
	both := func(x, y int, f func(int, int) int) int {
		if x < 0 || y < 0 {
			return -1
		}
		if b := f(x, y); b <= 63 {
			return b
		}
		return -1
	}
	sum := func(x, y int) int { return max(x, y) + 1 }
	prod := func(x, y int) int { return x + y }
	switch nm {
	case "KSMALL":
		return k(int64(int16(a[1])))
	case "KINT":
		return k(m.Ints[a[1]])
	case "LEN":
		return idxMag
	case "MOV":
		if a[0] != a[1] {
			return of(a[1])
		}
	case "ADDI", "SUBI":
		return both(of(a[1]), of(a[2]), sum)
	case "MULI":
		return both(of(a[1]), of(a[2]), prod)
	case "ADDIS", "SUBIS", "ADDIK":
		return both(of(a[1]), k(int64(imm(in, 2))), sum)
	case "MULIS":
		return both(of(a[1]), k(int64(imm(in, 2))), prod)
	case "NEGI":
		return of(a[1])
	case "DIVIS":
		return of(a[1]) // |x / k| <= |x|
	case "MODIS":
		if b := of(a[1]); b >= 0 {
			return min(b, k(int64(imm(in, 2))))
		}
		return k(int64(imm(in, 2)))
	}
	return -1
}
