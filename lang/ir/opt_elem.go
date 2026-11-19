package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
)

// elemFields reads and changes the fields of an array's struct element
// in the array, not through a copy of the element: "xs[i].f" and
// "xs[i].f = v" (and op=) without the element taken out and put back.
//
// A read, IDX e, A, I then GETF x, e, f (each read of e a GETF, e dead
// after them) is IDXF x, A, I, f. A change, as the lowering makes it:
//
//	IDX e, A, I; SETIDX A, I, none; ...GETF x, e, f, SETF e, f, v...; SETIDX A, I, e
//
// is IDXF and SETIF, which changes the element where it is (copying the
// array, then the element, when they're shared: what SETIDX of none and
// SETF did). Either way nothing between them may throw, jump, or touch
// A, I or e otherwise; the first replacing instruction checks the index
// as IDX did, where IDX did (what's before it is pure); and e, no longer
// written, held none (so no value it held is kept longer).
func (o *optimizer) elemFields() {
	f := o.f
	code := f.Code
	if !o.has(op("IDX")) {
		return
	}
	targeted := o.targets()
	live := o.liveness()
	none := o.noneBefore()
	liveOut := make([]regset, len(o.blocks))
	for b := range o.blocks {
		liveOut[b] = newRegset(o.n)
		o.liveOut(live, b, liveOut[b])
	}
	var (
		opIDX, opGETF, opSETF, opSETIDX = op("IDX"), op("GETF"), op("SETF"), op("SETIDX")
		opIDXF, opSETIF                 = op("IDXF"), op("SETIF")
	)
	for j := range code {
		in := &code[j]
		if in.Op != opIDX {
			continue
		}
		e, A, I := int(in.Args[0]), int(in.Args[1]), int(in.Args[2])
		if f.Kinds[e] != bc.KRef || e == A || e == I || !none.has(j, e) {
			continue
		}
		b := o.blockOf[j]
		end := o.blocks[b].end
		h := o.handlerOf[j]
		if h >= 0 && live[o.blockOf[h]].has(e) {
			continue
		}
		// What follows: the uses, and where the run of them ends (stop).
		var gets, sets []int
		change := -1 // the SETIDX A, I, none
		back := -1   // the SETIDX A, I, e
		ok := true
		stop := end
	scan:
		for k := j + 1; k < end; k++ {
			c := &code[k]
			if c.Op == opNOP {
				continue
			}
			if targeted[k] || o.handlerOf[k] != h {
				stop = k
				break
			}
			switch {
			case c.Op == opGETF && int(c.Args[1]) == e:
				if x := int(c.Args[0]); x == e || x == A || x == I {
					ok = false
					break scan
				}
				gets = append(gets, k)
				continue
			case c.Op == opSETF && int(c.Args[0]) == e:
				if change < 0 || int(c.Args[2]) == e {
					ok = false
					break scan
				}
				sets = append(sets, k)
				continue
			case c.Op == opSETIDX && int(c.Args[0]) == A && int(c.Args[1]) == I && change < 0 && k == o.next(j+1):
				if z := int(c.Args[2]); z == e || f.Kinds[z] != bc.KRef || !none.has(k, z) {
					ok = false
					break scan
				}
				change = k
				continue
			case c.Op == opSETIDX && int(c.Args[0]) == A && int(c.Args[1]) == I && int(c.Args[2]) == e && change >= 0:
				back = k
				stop = k + 1
				break scan
			}
			// Anything else mustn't touch e, A or I, throw, or have an
			// effect a moved bounds check could come after: else the run
			// ends here.
			info := &opInfos[c.Op]
			if o.readsReg(c, e) || o.writesReg(c, e) || o.writesReg(c, A) || o.writesReg(c, I) || info.throws || !info.pure || info.jump >= 0 || info.term {
				stop = k
				break
			}
			if change >= 0 && o.readsReg(c, A) {
				// A's element is none while it's taken out.
				ok = false
				break
			}
		}
		if !ok || len(gets)+len(sets) == 0 || (change >= 0) != (back >= 0) {
			continue
		}
		// e dead where the run stopped: after the put back, or at stop.
		if !o.deadFrom(stop, e, b, liveOut[b]) {
			continue
		}
		first := true
		line := in.Line
		for k := j + 1; k < stop; k++ {
			c := &code[k]
			switch {
			case k == change || k == back:
				o.nop(k)
			case c.Op == opGETF && int(c.Args[1]) == e && contains(gets, k):
				*c = Instr{Op: opIDXF, Args: [5]int32{c.Args[0], int32(A), int32(I), c.Args[2]}, Line: c.Line}
			case c.Op == opSETF && contains(sets, k):
				*c = Instr{Op: opSETIF, Args: [5]int32{int32(A), int32(I), c.Args[1], c.Args[2]}, Line: c.Line}
			default:
				continue
			}
			if first && c.Op != opNOP {
				c.Line = line // the bounds check's line: IDX's
				first = false
			}
		}
		o.nop(j)
	}
}

func contains(xs []int, x int) bool {
	for _, y := range xs {
		if y == x {
			return true
		}
	}
	return false
}

// deadFrom reports whether register r is read before it's written from
// instruction k on (to block b's end, then what's live out of it): false
// when it is.
func (o *optimizer) deadFrom(k, r, b int, out regset) bool {
	code := o.f.Code
	for ; k < o.blocks[b].end; k++ {
		c := &code[k]
		if o.readsReg(c, r) {
			return false
		}
		if kill(c) == r {
			return true
		}
		if o.throwsHere(c) {
			// What the handler (or the caller) reads, it reads as r is
			// now: e's old value.
			return false
		}
	}
	return !out.has(r)
}
