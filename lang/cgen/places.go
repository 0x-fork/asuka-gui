package cgen

import (
	bc "github.com/egoist/asuka/lang/bytecode"
)

// Places: how the C reaches elements and fields without the reference
// counting the bytecode's general form does.
//
// The bytecode reads a field of an array's element (xs[i].x) by
// retaining the element into a register (IDX), reading the field (GETF)
// and releasing it (CLR); it changes one (xs[i].x = v) by taking the
// element out of the array (IDX, then SETIDX of none: so that it's
// unique and changed in place), changing it (SETF) and putting it back
// (SETIDX, CLR), with a handler that puts it back if the change throws
// (lang/LOWERING.md, Changing values in place). The C does the same
// with less:
//
//   - a borrowed read: an element or a field read into a register that
//     only reads it (its fields, elements, length) before it's cleared,
//     on a straight path that calls nothing and changes nothing the
//     value is in, is a C pointer borrowed from its container (v<r>):
//     no retain, no release, and the register stays none, so an error
//     on the way releases nothing it doesn't own;
//   - a change in place: an element taken out, changed by SETFs and
//     put back with nothing on the way that can throw (so the handler
//     that puts it back is never reached) is changed where it is: the
//     array made unique, the element made unique in it, its fields set;
//   - moves: an element or a field taken out (read, then none put in
//     its place) is moved out, and one put back and cleared is moved
//     in: no retain and release of it.
//
// lang/ir's optimizer makes some of these single instructions: IDXF
// (IDX, GETF) reads an element's field, and SETIF (the take-out, SETF,
// the put back) changes one in place. They're planned as the forms they
// replace are: IDXF's result borrowed as a field's is, SETIF's array and
// element made unique and its index checked only where nothing on the
// path before has (facts), its value moved in when it's cleared after.
//
// Each is decided per instruction before the C is written (plan), on
// the bytecode's straight paths: from an instruction to the next while
// the next has no other way in (a label another jump targets, a
// handler, a resumption).

// plan is what the place analysis decided for a function's
// instructions, by index.
type plan struct {
	// borrow: the instruction (IDX, GETF, GETP, IDXF) reads into its register
	// a borrowed pointer, v<r>, not the register.
	borrow map[int]bool
	// inplace: the IDX is a change in place's: the element made unique
	// in the array, borrowed as v<r>.
	inplace map[int]bool
	// active: the registers borrowed as v<r> at the instruction (read
	// through it).
	active map[int][]int
	// skip: the instruction does nothing (a borrowed register's CLR, a
	// change in place's take-out and put-back).
	skip map[int]bool
	// direct: a SETF of a register borrowed for a change in place: set
	// in place, the struct unique already.
	direct map[int]bool
	// moveOut: the IDX or GETF moves the value out of its place (the
	// next instruction, which puts none there, is skipped); moveIn: the
	// SETIDX, SETF or SETIF moves its register into the place (the next,
	// its CLR, is skipped).
	moveOut, moveIn map[int]bool
	// virt: the registers some instruction borrows (v<r> declared).
	virt map[int]bool
	// changes: the IDXF reads a field of an element that a SETIF further
	// on its straight path changes: it makes the array and the element
	// unique, as a change in place's IDX does, so that what follows reads
	// and writes them with nothing checked (and reads after the element
	// is made unique, as the change in place's GETFs do).
	changes map[int]bool
	// What's known before an IDX, SETIDX, SETIF or IDXF on its straight path
	// (facts): its array is unique (unique), its index is in its range
	// (inRange), the element there is unique (uniqueElem); its checks
	// are left out.
	unique, inRange, uniqueElem map[int]bool
}

// srcPos are the operands of an instruction that a borrowed register
// may be: read, never kept without a retain of its own.
var srcPos = map[string][]int{
	"GETF": {1}, "GETP": {1}, "IDX": {1}, "LEN": {1}, "ECASE": {1}, "ISNONE": {1}, "UNBOX": {1}, "MOVR": {1},
	"IDXF": {1},
}

// pathOps are the instructions a borrowed register's path may have:
// none of them calls anything (that could change or free what it's
// borrowed from), or changes a value in place.
var pathOps = map[string]bool{
	"NOP": true, "MOV": true, "MOVR": true, "CLR": true, "KINT": true, "KSMALL": true, "KFLOAT": true, "KSTR": true, "KNONE": true,
	"ADDI": true, "SUBI": true, "MULI": true, "DIVI": true, "MODI": true, "NEGI": true, "WADDI": true, "WSUBI": true, "WMULI": true,
	"ADDIK": true, "EQI": true, "NEI": true, "LTI": true, "LEI": true, "BAND": true, "BOR": true, "BXOR": true, "BNOT": true,
	"SHL": true, "SHR": true, "ADDF": true, "SUBF": true, "MULF": true, "DIVF": true, "MODF": true, "NEGF": true,
	"EQF": true, "NEF": true, "LTF": true, "LEF": true, "I2F": true, "F2I": true, "NOT": true,
	"GETF": true, "GETP": true, "IDX": true, "LEN": true, "ECASE": true, "ISNONE": true, "UNBOX": true, "JMP": true,
	// The optimizer's (lang/ir).
	"ADDIS": true, "SUBIS": true, "MULIS": true, "DIVIS": true, "MODIS": true, "DIVIK": true, "MODIK": true, "IDXF": true,
}

// quiet are the instructions that never throw, which a change in
// place's path may have (besides its SETFs).
var quiet = map[string]bool{
	"NOP": true, "MOV": true, "KINT": true, "KSMALL": true, "KFLOAT": true,
	"WADDI": true, "WSUBI": true, "WMULI": true, "ADDIK": true, "EQI": true, "NEI": true, "LTI": true, "LEI": true,
	"BAND": true, "BOR": true, "BXOR": true, "BNOT": true,
	"ADDF": true, "SUBF": true, "MULF": true, "DIVF": true, "MODF": true, "NEGF": true,
	"EQF": true, "NEF": true, "LTF": true, "LEF": true, "I2F": true, "F2I": true, "NOT": true,
	"GETF": true, "JMP": true,
	// The optimizer's divisions by a constant, never 0 or -1.
	"DIVIS": true, "MODIS": true, "DIVIK": true, "MODIK": true,
}

// branches are the instructions that jump or go on: a straight path
// ends at them.
var branches = map[string]bool{
	"JT": true, "JF": true, "JNONE": true, "JSOME": true, "MGETJ": true,
	// The optimizer's comparisons fused with their jumps.
	"JLTI": true, "JLEI": true, "JEQI": true, "JNEI": true,
	"JLTIS": true, "JLEIS": true, "JGTIS": true, "JGEIS": true, "JEQIS": true, "JNEIS": true,
}

// writes are the registers in writes: its destination (operand 0, a
// register, for every instruction but those that only read it), what a
// move takes out of (THROW's, CPUT's value), and a call's argument
// block, which the callee may change.
func writes(m *bc.Module, in insn) []int {
	d := bc.Ops[in.op]
	a := in.args
	var out []int
	switch d.Name {
	case "JT", "JF", "JNONE", "JSOME", "RET", "VSET", "CSET", "EFFECT", "RENDER", "CLEANUP", "GSET",
		"JLTI", "JLEI", "JEQI", "JNEI", "JLTIS", "JLEIS", "JGTIS", "JGEIS", "JEQIS", "JNEIS":
	case "CPUT", "CPUTP":
		out = append(out, a[1])
	default:
		if len(d.Operands) > 0 && d.Operands[0] == bc.Reg && a[0] != bc.None {
			out = append(out, a[0])
		}
	}
	if b, n := callBlock(m, in); n > 0 {
		for r := b; r < b+n; r++ {
			out = append(out, r)
		}
	}
	return out
}

// regs are every register in uses: its operands and its argument block,
// if it has one (a call's, a struct's fields, a closure's captures...).
func regs(m *bc.Module, in insn) []int {
	d := bc.Ops[in.op]
	a := in.args
	var out []int
	for k, o := range d.Operands {
		if o == bc.Reg && a[k] != bc.None {
			out = append(out, a[k])
		}
	}
	b, n := 0, 0
	switch d.Name {
	case "CALL", "CALLB", "CALLC", "CALLU", "CALLI", "CLOS":
		b, n = a[2], a[3]
	case "NEWS":
		b, n = a[2], len(m.Shapes[a[1]].Fields)
	case "NEWE":
		if sh := &m.Shapes[a[1]]; a[2] < len(sh.Cases) {
			b, n = a[3], len(sh.Cases[a[2]].Fields)
		}
	case "NEWA":
		b, n = a[3], a[4]
	case "NEWM":
		b, n = a[3], 2*a[4]
	case "INTERP":
		b, n = a[1], a[2]
	case "CPATH", "CLEN", "CPUTP", "ACCP", "AGETP":
		b, n = a[3], pathArgs(m.Strings[a[2]])
	}
	for r := b; r < b+n; r++ {
		out = append(out, r)
	}
	return out
}

func has(rs []int, r int) bool {
	for _, x := range rs {
		if x == r {
			return true
		}
	}
	return false
}

// analyze plans f's places (ins, decoded; entries: the pcs control may
// come in at besides falling or jumping there: handlers, resumptions).
func analyze(m *bc.Module, f *bc.FuncDef, ins []insn, entries map[int]bool) *plan {
	pl := &plan{borrow: map[int]bool{}, inplace: map[int]bool{}, active: map[int][]int{}, skip: map[int]bool{},
		direct: map[int]bool{}, moveOut: map[int]bool{}, moveIn: map[int]bool{}, virt: map[int]bool{},
		unique: map[int]bool{}, inRange: map[int]bool{}, uniqueElem: map[int]bool{}, changes: map[int]bool{}}
	n := len(ins)
	at := make(map[int]int, n) // pc → index
	for k, in := range ins {
		at[in.pc] = k
	}
	// How many ways each instruction is come to.
	preds := make([]int, n)
	for k, in := range ins {
		name := bc.Ops[in.op].Name
		if k+1 < n {
			switch name {
			case "JMP", "RET", "RET0", "THROW", "RETHROW":
			default:
				preds[k+1]++
			}
		}
		for o, kind := range bc.Ops[in.op].Operands {
			if kind == bc.Off {
				if t, ok := at[in.args[o]]; ok {
					preds[t]++
				}
			}
		}
	}
	for pc := range entries {
		if t, ok := at[pc]; ok {
			preds[t] += 2
		}
	}
	// next is the instruction a straight path goes on to from k, -1
	// when it ends (a branch, a return, a throw, an instruction come to
	// another way too).
	next := func(k int) int {
		in := ins[k]
		j := -1
		switch nm := bc.Ops[in.op].Name; {
		case nm == "JMP":
			if t, ok := at[in.args[0]]; ok {
				j = t
			}
		case branches[nm], nm == "RET", nm == "RET0", nm == "THROW", nm == "RETHROW":
		default:
			if k+1 < n {
				j = k + 1
			}
		}
		if j < 0 || preds[j] != 1 {
			return -1
		}
		return j
	}
	// Registers always none: references only ever cleared (lowering's
	// none, which a take-out puts in a place).
	none := make([]bool, len(f.Kinds))
	for r, k := range f.Kinds {
		none[r] = k == bc.KRef && r >= int(f.NParams)
	}
	for _, in := range ins {
		name := bc.Ops[in.op].Name
		for _, r := range writes(m, in) {
			if name != "KNONE" && name != "CLR" {
				none[r] = false
			}
		}
	}
	isNone := func(r int) bool { return r != bc.None && r < len(none) && none[r] }
	name := func(k int) string { return bc.Ops[ins[k].op].Name }
	src := func(k, r int) bool {
		// r is only in ins[k]'s source operands (srcPos), and in no
		// block.
		in := ins[k]
		pos := srcPos[name(k)]
		d := bc.Ops[in.op]
		for o, kind := range d.Operands {
			if kind == bc.Reg && in.args[o] == r {
				ok := false
				for _, p := range pos {
					if p == o {
						ok = true
					}
				}
				if !ok {
					return false
				}
			}
		}
		// In a block (not an operand)?
		cnt := 0
		for _, x := range regs(m, in) {
			if x == r {
				cnt++
			}
		}
		ops := 0
		for o, kind := range d.Operands {
			if kind == bc.Reg && in.args[o] == r {
				ops++
			}
		}
		return cnt == ops
	}
	taken := map[int]bool{} // instructions a change in place or a move has claimed

	// Changes in place, and their moves.
	for k := 0; k+1 < n; k++ {
		in := ins[k]
		if name(k) != "IDX" || taken[k] {
			continue
		}
		t, c, i := in.args[0], in.args[1], in.args[2]
		if t == bc.None || f.Kinds[t] != bc.KRef || t == c || t == i {
			continue
		}
		k1 := k + 1
		if preds[k1] != 1 || name(k1) != "SETIDX" {
			continue
		}
		if a := ins[k1].args; a[0] != c || a[1] != i || !isNone(a[2]) {
			continue
		}
		// The take-out; a change in place if its path is quiet and ends
		// with the put back.
		var path []int
		ok := false
		kp := -1
		for j := next(k1); j >= 0; j = next(j) {
			nm := name(j)
			a := ins[j].args
			if nm == "SETIDX" && a[0] == c && a[1] == i && a[2] == t {
				kp = j
				break
			}
			if nm == "SETF" && a[0] == t && a[2] != t && !has(regs(m, ins[j]), c) && !has(regs(m, ins[j]), i) {
				path = append(path, j)
				continue
			}
			if !quiet[nm] {
				break
			}
			ws := writes(m, ins[j])
			if has(ws, t) || has(ws, c) || has(ws, i) {
				break
			}
			rs := regs(m, ins[j])
			if has(rs, c) || (has(rs, t) && !(nm == "GETF" && src(j, t))) {
				break
			}
			path = append(path, j)
		}
		if kp >= 0 {
			kc := kp + 1
			if kc < n && preds[kc] == 1 && name(kc) == "CLR" && ins[kc].args[0] == t {
				ok = true
				pl.inplace[k] = true
				pl.virt[t] = true
				pl.skip[k1], pl.skip[kp], pl.skip[kc] = true, true, true
				for _, j := range path {
					if name(j) == "SETF" {
						pl.direct[j] = true
					}
					pl.active[j] = append(pl.active[j], t)
				}
				taken[k], taken[k1], taken[kp], taken[kc] = true, true, true, true
			}
		}
		if !ok {
			pl.moveOut[k] = true
			pl.skip[k1] = true
			taken[k], taken[k1] = true, true
		}
	}
	// Fields taken out (GETF, SETF of none), and puts back (SETIDX or
	// SETF of a register, then its CLR).
	for k := 0; k+1 < n; k++ {
		if taken[k] || taken[k+1] || preds[k+1] != 1 {
			continue
		}
		a, b := ins[k].args, ins[k+1].args
		switch name(k) {
		case "GETF":
			t, s, fi := a[0], a[1], a[2]
			if t != bc.None && f.Kinds[t] == bc.KRef && t != s && name(k+1) == "SETF" && b[0] == s && b[1] == fi && isNone(b[2]) {
				pl.moveOut[k] = true
				pl.skip[k+1] = true
				taken[k], taken[k+1] = true, true
			}
		case "SETIDX", "SETF", "SETIF":
			v := a[2]
			if name(k) == "SETIF" {
				v = a[3]
			}
			if f.Kinds[v] == bc.KRef && v != a[0] && (name(k) == "SETF" || v != a[1]) && name(k+1) == "CLR" && b[0] == v {
				pl.moveIn[k] = true
				pl.skip[k+1] = true
				taken[k], taken[k+1] = true, true
			}
		}
	}
	// Borrowed reads.
	for k := 0; k < n; k++ {
		if taken[k] {
			continue
		}
		nm := name(k)
		if nm != "IDX" && nm != "GETF" && nm != "GETP" && nm != "IDXF" {
			continue
		}
		in := ins[k]
		t, c := in.args[0], in.args[1]
		if t == bc.None || f.Kinds[t] != bc.KRef || t == c || ((nm == "IDX" || nm == "IDXF") && t == in.args[2]) {
			continue
		}
		var path []int
		end := -1 // the CLR that ends it, or the instruction that writes t
		for j := next(k); j >= 0; j = next(j) {
			nj := name(j)
			if nj == "CLR" && ins[j].args[0] == t {
				end = j
				break
			}
			if !pathOps[nj] || taken[j] {
				break
			}
			ws := writes(m, ins[j])
			rs := regs(m, ins[j])
			if has(ws, c) || (has(rs, c) && !src(j, c)) {
				break
			}
			if has(rs, t) && !src(j, t) {
				break
			}
			if has(ws, t) {
				// Written: its last read (if it reads t) is this one.
				if has(rs, t) {
					path = append(path, j)
				}
				end = j
				break
			}
			path = append(path, j)
		}
		if end < 0 {
			continue
		}
		pl.borrow[k] = true
		pl.virt[t] = true
		for _, j := range path {
			pl.active[j] = append(pl.active[j], t)
		}
		if name(end) == "CLR" && ins[end].args[0] == t {
			// Its register is none already: the CLR does nothing (and
			// may be on another borrowed register's path).
			pl.skip[end] = true
		}
	}
	pl.facts(m, f, ins, next)
	return pl
}

// facts finds, along each straight path, what a check before has made
// known (plan.unique, inRange, uniqueElem): an array that was made
// unique stays so until a reference to it is made or it's passed on (a
// unique array is held by its register alone, so a call that isn't
// given it can't reach it, nor its unique elements); an index checked
// stays in range while neither the array's register nor the index's is
// written and the array isn't changed but by its elements; an element
// made unique stays so until an element of the array is read into a
// register (retained: it may be the same) or replaced.
func (pl *plan) facts(m *bc.Module, fd *bc.FuncDef, ins []insn, next func(int) int) {
	n := len(ins)
	isNext := make([]bool, n)
	for k := range ins {
		if j := next(k); j >= 0 {
			isNext[j] = true
		}
	}
	type pair struct{ c, i int }
	// What a register's value may be read as without being retained or
	// passed on: its operand positions (srcPos, but MOVR's, which keeps
	// it).
	pure := func(in insn, r int) bool {
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
	for start := range ins {
		if isNext[start] {
			continue
		}
		uniq := map[int]bool{}
		rng := map[pair]bool{}
		elem := map[pair]bool{}
		forget := func(r int) {
			delete(uniq, r)
			for p := range rng {
				if p.c == r || p.i == r {
					delete(rng, p)
				}
			}
			for p := range elem {
				if p.c == r || p.i == r {
					delete(elem, p)
				}
			}
		}
		forgetElems := func(c int) {
			for p := range elem {
				if p.c == c {
					delete(elem, p)
				}
			}
		}
		var path []int
		for k := start; k >= 0; k = next(k) {
			path = append(path, k)
		}
		// An IDXF whose element a SETIF changes further on, neither the
		// array's register nor the index's written between (nor the array
		// borrowed there).
		for p, k := range path {
			in := ins[k]
			if bc.Ops[in.op].Name != "IDXF" || pl.skip[k] || pl.borrow[k] || has(pl.active[k], in.args[1]) {
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
				if bc.Ops[ins[q].op].Name == "SETIF" && a[0] == c && a[1] == i {
					pl.changes[k] = true
					break
				}
				ws := writes(m, ins[q])
				if (has(ws, c) && bc.Ops[ins[q].op].Name != "SETIF") || has(ws, i) {
					break
				}
			}
		}
		for _, k := range path {
			in := ins[k]
			a := in.args
			nm := bc.Ops[in.op].Name
			if pl.skip[k] {
				continue
			}
			// What it relies on.
			switch nm {
			case "IDX":
				p := pair{a[1], a[2]}
				pl.inRange[k] = rng[p]
				if pl.inplace[k] || pl.moveOut[k] {
					pl.unique[k] = uniq[a[1]]
					pl.uniqueElem[k] = elem[p]
				}
			case "IDXF":
				p := pair{a[1], a[2]}
				pl.inRange[k] = rng[p]
				if pl.changes[k] {
					pl.unique[k] = uniq[a[1]]
					pl.uniqueElem[k] = elem[p]
				}
			case "SETIDX":
				p := pair{a[0], a[1]}
				pl.inRange[k] = rng[p]
				pl.unique[k] = uniq[a[0]]
			case "SETIF":
				p := pair{a[0], a[1]}
				pl.inRange[k] = rng[p]
				pl.unique[k] = uniq[a[0]]
				pl.uniqueElem[k] = elem[p]
			}
			// What it does to what's known: first what it reads or
			// passes on, then what it writes, then what it makes known.
			rs := regs(m, in)
			for _, r := range rs {
				if r < len(fd.Kinds) && fd.Kinds[r] == bc.KRef && !pure(in, r) && !((nm == "SETIDX" || nm == "SETIF") && r == a[0]) && !(nm == "IDX" && (pl.inplace[k] || pl.moveOut[k]) && r == a[1]) && !(nm == "IDXF" && pl.changes[k] && r == a[1]) && !(nm == "SETF" && r == a[0]) {
					forget(r)
				}
			}
			for _, r := range writes(m, in) {
				if (nm == "SETIDX" || nm == "SETF" || nm == "SETIF") && r == a[0] {
					continue
				}
				forget(r)
			}
			switch nm {
			case "IDX":
				c, i, t := a[1], a[2], a[0]
				if t != c && t != i {
					rng[pair{c, i}] = true
					switch {
					case pl.inplace[k]:
						uniq[c] = true
						elem[pair{c, i}] = true
					case pl.moveOut[k]:
						uniq[c] = true
						forgetElems(c)
					case !pl.borrow[k]:
						forgetElems(c) // retained into t: shared
					}
				}
			case "IDXF":
				// The element read where it is, not retained: it stays
				// as unique as it was (made so, when the IDXF changes).
				if t, c, i := a[0], a[1], a[2]; t != c && t != i {
					rng[pair{c, i}] = true
					if pl.changes[k] {
						uniq[c] = true
						elem[pair{c, i}] = true
					}
				}
			case "SETIDX":
				c, i := a[0], a[1]
				uniq[c] = true
				rng[pair{c, i}] = true
				forgetElems(c)
			case "SETIF":
				// The array made unique, then its element in it.
				c, i := a[0], a[1]
				uniq[c] = true
				rng[pair{c, i}] = true
				elem[pair{c, i}] = true
			}
		}
	}
}

func filter(rs []int, r int) []int {
	var out []int
	for _, x := range rs {
		if x == r {
			out = append(out, x)
		}
	}
	return out
}
