package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
)

// Scalar replacement: a struct or a tuple of scalars only (no reference
// fields) made (NEWS) and only read by its fields (GETF) before its
// register is written again is never made: each GETF reads the register
// the field was made from, which nothing writes on the way. A small
// function returning (int, bool), inlined, then allocates nothing:
// find's (index, found) in a B-tree's search.
//
// A field that's a reference would be let go of sooner without the
// struct that held it: such structs are left as they are.

func (o *optimizer) scalarStructs() {
	f := o.f
	code := f.Code
	opNEWS, opGETF := op("NEWS"), op("GETF")
	if !o.has(opNEWS) {
		return
	}
	var preds [][]int
	for j := range code {
		in := &code[j]
		if in.Op != opNEWS {
			continue
		}
		sh := &o.p.Mod.Shapes[in.Args[1]]
		scalars := len(sh.Fields) > 0
		for _, fd := range sh.Fields {
			if fd.Kind == bc.KRef || fd.Kind == bc.KVoid {
				scalars = false
			}
		}
		if !scalars {
			continue
		}
		r, blk := int(in.Args[0]), int(in.Args[2])
		// What reads it, from here until it's written again.
		reads, ok := o.readsFrom(j, r)
		if !ok || len(reads) == 0 {
			continue
		}
		if preds == nil {
			preds = o.preds()
		}
		for _, k := range reads {
			g := &code[k]
			if g.Op != opGETF || int(g.Args[1]) != r || int(g.Args[0]) == r {
				ok = false
				break
			}
			def, on := o.lastDef(k, r, preds)
			fr := blk + int(g.Args[2])
			if def != j || int(g.Args[2]) >= len(sh.Fields) {
				ok = false
				break
			}
			for _, x := range on {
				if o.writesReg(&code[x], fr) {
					ok = false
				}
			}
			if !ok {
				break
			}
		}
		if !ok {
			continue
		}
		for _, k := range reads {
			g := &code[k]
			fr := blk + int(g.Args[2])
			*g = Instr{Op: opMOV, Args: [5]int32{g.Args[0], int32(fr)}, Line: g.Line}
		}
		*in = Instr{Op: opCLR, Args: [5]int32{int32(r)}, Line: in.Line}
		o.changed = true
	}
}

// readsFrom is the instructions that read register r on the ways on
// from j (after it) before r is written again; ok false when one may
// read it without naming it (an instruction the table doesn't
// describe).
func (o *optimizer) readsFrom(j, r int) ([]int, bool) {
	code := o.f.Code
	var out []int
	seen := make([]bool, len(o.blocks))
	type span struct{ b, from int }
	work := []span{{o.blockOf[j], j + 1}}
	for len(work) > 0 {
		s := work[len(work)-1]
		work = work[:len(work)-1]
		bl := &o.blocks[s.b]
		killed := false
		for k := s.from; k < bl.end; k++ {
			in := &code[k]
			if opInfos[in.Op].unknown {
				return nil, false
			}
			if o.readsReg(in, r) {
				out = append(out, k)
			}
			if o.writesReg(in, r) {
				killed = true
				break
			}
		}
		// A throw before the write goes to a handler that may read it.
		next := append([]int(nil), bl.exc...)
		if !killed {
			next = append(next, bl.succ()...)
		}
		for _, b := range next {
			if !seen[b] {
				seen[b] = true
				work = append(work, span{b, o.blocks[b].start})
			}
		}
	}
	return out, true
}

// lastDef is the instruction that last wrote register r on every way
// to i (-1 when the ways differ, or r comes in as it was), and the
// instructions on those ways after it.
func (o *optimizer) lastDef(i, r int, preds [][]int) (int, []int) {
	code := o.f.Code
	def := -1
	var on []int
	seen := make([]bool, len(o.blocks))
	type span struct{ b, from int }
	work := []span{{o.blockOf[i], i - 1}}
	for len(work) > 0 {
		s := work[len(work)-1]
		work = work[:len(work)-1]
		bl := &o.blocks[s.b]
		found := false
		for k := s.from; k >= bl.start; k-- {
			if !o.writesReg(&code[k], r) {
				on = append(on, k)
				continue
			}
			if def >= 0 && def != k {
				return -1, nil
			}
			def, found = k, true
			break
		}
		if found {
			continue
		}
		if bl.start == 0 || len(preds[s.b]) == 0 {
			return -1, nil
		}
		for _, p := range preds[s.b] {
			if !seen[p] {
				seen[p] = true
				work = append(work, span{p, o.blocks[p].end - 1})
			}
		}
	}
	return def, on
}
