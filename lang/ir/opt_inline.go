package ir

import (
	"sort"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// Inlining: a call of a small function (CALL), or of a closure a CLOS
// made (the last write of its register on every way to the call, as in
// the same block, or before a loop it's called in), is replaced with a copy of
// the function's body, in registers of the caller's own: its
// parameters copied in as a call's frame takes them (references
// retained), its returns a copy of the result and a jump past the body,
// and its reference registers let go of after it, as its frame's are as
// it returns. The closure the call was made on is then often never read:
// it isn't made at all.
//
// An error thrown in the body, or a call made there, is in the inlined
// function as far as traces and diagnostics go: the caller's Inlines
// say where its body is, and the VM's traceback and lang/cgen's frame
// hooks name its frame with its own line, then the caller's at the
// call's.
//
// What isn't inlined: an async function or one that may suspend, a mut
// fn (its receiver goes back to the caller's register as it ends), one
// with handlers or peeks (RETRACK counts from its frame), one that calls
// itself or its caller, one bigger than inlineCode instructions or
// inlineRegs registers. Nor into a function that may suspend (its C is
// resumable), nor at a call in a handler's range when the callee has
// reference registers (an error caught there would keep what they hold
// until the caller ends).

const (
	inlineCode   = 40   // instructions
	inlineRegs   = 24   // registers
	inlineBudget = 2048 // the registers a caller may grow to
	inlineGrowth = 1024 // the instructions a caller may grow by, a round
	inlineRounds = 2    // a closure passed to an inlined function is inlined in the second
)

// snapshot is a function as it's copied: as it was once what it calls
// was inlined in it (and optimized), its own inlined bodies' ranges as
// instructions' indexes.
type snapshot struct {
	code    []Instr
	labels  []int
	kinds   []bc.Kind
	inlines []snapInline
}

type snapInline struct {
	start, end, fn int
	line           int32
}

func snap(g *Func) *snapshot {
	s := &snapshot{code: append([]Instr(nil), g.Code...), labels: append([]int(nil), g.labels...), kinds: append([]bc.Kind(nil), g.Kinds...)}
	for _, in := range g.Inlines {
		s.inlines = append(s.inlines, snapInline{g.labels[in.Start], g.labels[in.End], in.Func, in.Line})
	}
	return s
}

// inlinable reports whether g may be inlined anywhere.
func inlinable(g *Func) bool {
	if g.Flags&(bc.FAsync|bc.FMut) != 0 || len(g.Handlers) > 0 {
		return false
	}
	if len(g.Code) > inlineCode || len(g.Kinds) > inlineRegs {
		return false
	}
	for _, in := range g.Code {
		switch bc.Ops[in.Op].Name {
		case "AWAIT", "YIELD", "CATCH", "RETHROW", "UNTRACK", "RETRACK":
			return false
		}
	}
	return true
}

// suspends reports whether f may suspend (its C is resumable).
func suspends(f *Func) bool {
	if f.Flags&bc.FAsync != 0 {
		return true
	}
	for _, in := range f.Code {
		if n := bc.Ops[in.Op].Name; n == "AWAIT" || n == "YIELD" {
			return true
		}
	}
	return false
}

// inlineAll inlines what it may into every function, callees first (so
// what's copied is what they came to), running the passes again over
// the functions it changed.
func (p *Program) inlineAll() {
	snaps := map[int]*snapshot{}
	done := make([]bool, len(p.Funcs))
	var visit func(fi int)
	visit = func(fi int) {
		if done[fi] {
			return
		}
		done[fi] = true
		f := p.Funcs[fi]
		for _, in := range f.Code {
			if n := bc.Ops[in.Op].Name; n == "CALL" || n == "CLOS" {
				visit(int(in.Args[1]))
			}
		}
		if !suspends(f) {
			for round := 0; round < inlineRounds; round++ {
				o := &optimizer{p: p, f: f, n: len(f.Kinds)}
				o.cfg()
				if !o.inline(fi, snaps) {
					break
				}
				o.passes()
			}
		}
		if inlinable(f) {
			snaps[fi] = snap(f)
		}
	}
	for fi := range p.Funcs {
		visit(fi)
	}
}

// inlineSite is a call inline replaces: the function called, and for a
// closure the registers its captures are in.
type inlineSite struct {
	g    int
	caps int // a closure's captures' first register, -1: a CALL
	// copied: the CLOS (its index) whose captures, scalars, are copied
	// into registers of their own as it's made (their registers are
	// written before the call), which the body reads; -1: none.
	copied int
}

// sites finds the calls in f that inline may replace.
func (o *optimizer) sites(fi int, snaps map[int]*snapshot) map[int]inlineSite {
	f := o.f
	code := f.Code
	out := map[int]inlineSite{}
	calls := func(g int, callee int) bool {
		for _, in := range o.p.Funcs[g].Code {
			if in.Op == opCALL && (int(in.Args[1]) == callee || int(in.Args[1]) == g) {
				return true
			}
		}
		return false
	}
	ok := func(i, g int) bool {
		s := snaps[g]
		if s == nil || g == fi || calls(g, fi) {
			return false
		}
		if h := o.handlerOf[i]; h >= 0 {
			for _, k := range s.kinds {
				if k == bc.KRef {
					return false
				}
			}
		}
		return true
	}
	opCALLC := op("CALLC")
	var preds [][]int
	for i := range code {
		if code[i].Op == opCALLC && preds == nil {
			preds = o.preds()
		}
		in := &code[i]
		switch in.Op {
		case opCALL:
			g := int(in.Args[1])
			if int(in.Args[3]) == o.p.Funcs[g].NParams && ok(i, g) {
				out[i] = inlineSite{g: g, caps: -1, copied: -1}
			}
		case opCALLC:
			// The closure a CLOS made, the last write of its register on
			// every way here, its captures' registers not written since.
			c := int(in.Args[1])
			j, clean := o.madeBy(i, c, preds)
			if j < 0 {
				continue
			}
			w := &code[j]
			g := int(w.Args[1])
			caps, n := int(w.Args[2]), int(w.Args[3])
			gf := o.p.Funcs[g]
			if int(in.Args[3]) != gf.NParams || len(gf.CapKinds) != n || !ok(i, g) {
				continue
			}
			if clean {
				out[i] = inlineSite{g: g, caps: caps, copied: -1}
				continue
			}
			// Its captures' registers are written on the way (an argument
			// block's, reused): scalars are copied as it's made.
			scalars := true
			for _, k := range gf.CapKinds {
				scalars = scalars && k != bc.KRef
			}
			if scalars {
				out[i] = inlineSite{g: g, caps: -1, copied: j}
			}
		}
	}
	return out
}

// preds are each block's predecessors (by jumps, falls and throws).
func (o *optimizer) preds() [][]int {
	ps := make([][]int, len(o.blocks))
	for b := range o.blocks {
		for _, s := range o.blocks[b].succ() {
			ps[s] = append(ps[s], b)
		}
		for _, s := range o.blocks[b].exc {
			ps[s] = append(ps[s], b)
		}
	}
	return ps
}

// madeBy is the CLOS (its index) that made the closure in register c
// that the instruction at i calls: on every way to i, the last write of
// c is that one CLOS, and nothing on the way from it writes the
// registers it captured from (which an inlined body reads in place of
// its captures); -1 when there's no such CLOS. A closure made before a
// loop and called in it is one: the loop's body, a filter's or a map's
// step, then inlines it.
func (o *optimizer) madeBy(i, c int, preds [][]int) (int, bool) {
	clos, on := o.lastDef(i, c, preds)
	if clos < 0 || o.f.Code[clos].Op != op("CLOS") {
		return -1, false
	}
	code := o.f.Code
	caps, n := int(code[clos].Args[2]), int(code[clos].Args[3])
	for _, k := range on {
		for r := caps; r < caps+n; r++ {
			if o.writesReg(&code[k], r) {
				return clos, false
			}
		}
	}
	return clos, true
}

// inline replaces f's calls that it may with their callees' bodies,
// reporting whether it replaced any.
func (o *optimizer) inline(fi int, snaps map[int]*snapshot) bool {
	f := o.f
	sites := o.sites(fi, snaps)
	if len(sites) == 0 {
		return false
	}
	base := map[int]int{} // a callee's registers in f, one set per callee: its bodies never run at once
	nlabels := len(f.labels)
	type placed struct {
		l  Label
		at int
	}
	var places []placed
	label := func(at int) Label {
		l := f.NewLabel()
		places = append(places, placed{l, at})
		return l
	}
	// The copies of closures' captures their inlined bodies read.
	copies := map[int]int{} // a CLOS's index → its copies' first register
	for _, s := range sites {
		if s.copied >= 0 && copies[s.copied] == 0 {
			copies[s.copied] = len(f.Kinds)
			f.Kinds = append(f.Kinds, o.p.Funcs[s.g].CapKinds...)
		}
	}
	newIdx := make([]int, len(f.Code)+1)
	var out []Instr
	inlined := false
	for i, call := range f.Code {
		newIdx[i] = len(out)
		if c0, ok := copies[i]; ok {
			out = append(out, call)
			for k := range int(call.Args[3]) {
				out = append(out, Instr{Op: opMOV, Args: [5]int32{int32(c0 + k), call.Args[2] + int32(k)}, Line: call.Line})
			}
			continue
		}
		site, ok := sites[i]
		if ok && site.copied >= 0 {
			site.caps = copies[site.copied]
		}
		if !ok || len(out)-i > inlineGrowth {
			out = append(out, call)
			continue
		}
		s := snaps[site.g]
		g := o.p.Funcs[site.g]
		r0, ok := base[site.g]
		if !ok {
			if len(f.Kinds)+len(s.kinds) > inlineBudget {
				out = append(out, call)
				continue
			}
			r0 = len(f.Kinds)
			f.Kinds = append(f.Kinds, s.kinds...)
			base[site.g] = r0
		}
		inlined = true
		line := call.Line
		dst := call.Args[0]
		mov := func(k bc.Kind, d, s int32) Instr {
			if k == bc.KRef {
				return Instr{Op: opMOVR, Args: [5]int32{d, s}, Line: line}
			}
			return Instr{Op: opMOV, Args: [5]int32{d, s}, Line: line}
		}
		start := label(len(out))
		// The arguments, as a frame takes them.
		for p := 0; p < g.NParams; p++ {
			out = append(out, mov(s.kinds[p], int32(r0+p), call.Args[2]+int32(p)))
		}
		// The body, its labels its own.
		gl := map[int]Label{}
		for l, at := range s.labels {
			if at >= 0 {
				gl[l] = f.NewLabel()
			}
		}
		at := map[int][]Label{}
		for l, idx := range s.labels {
			if idx >= 0 {
				at[idx] = append(at[idx], gl[l])
			}
		}
		end := f.NewLabel()
		posOf := make([]int, len(s.code)+1)
		for j, in := range s.code {
			posOf[j] = len(out)
			for _, l := range at[j] {
				places = append(places, placed{l, len(out)})
			}
			name := bc.Ops[in.Op].Name
			switch name {
			case "RET", "RET0":
				if dst != bc.None {
					if name == "RET" {
						out = append(out, mov(f.Kinds[dst], dst, in.Args[0]+int32(r0)))
					} else if f.Kinds[dst] == bc.KRef {
						out = append(out, Instr{Op: opKNONE, Args: [5]int32{dst}, Line: line})
					} else {
						out = append(out, Instr{Op: opKSMALL, Args: [5]int32{dst, 0}, Line: line})
					}
				}
				out = append(out, Instr{Op: opJMP, Args: [5]int32{int32(end)}, Line: in.Line})
				continue
			case "CAP":
				out = append(out, Instr{Op: mov(g.CapKinds[in.Args[1]], 0, 0).Op, Args: [5]int32{in.Args[0] + int32(r0), int32(site.caps) + in.Args[1]}, Line: in.Line})
				continue
			}
			c := in
			for k, kind := range bc.Ops[in.Op].Operands {
				switch {
				case kind == bc.Reg && c.Args[k] != bc.None:
					c.Args[k] += int32(r0)
				case kind == bc.Off:
					c.Args[k] = int32(gl[int(c.Args[k])])
				}
			}
			out = append(out, c)
		}
		posOf[len(s.code)] = len(out)
		for _, l := range at[len(s.code)] {
			places = append(places, placed{l, len(out)})
		}
		places = append(places, placed{end, len(out)})
		// What was inlined in it, inside it.
		for _, in := range s.inlines {
			f.Inlines = append(f.Inlines, Inline{Start: label(posOf[in.start]), End: label(posOf[in.end]), Func: in.fn, Line: in.line})
		}
		// Its frame's references let go of, as it returns.
		for r, k := range s.kinds {
			if k == bc.KRef {
				out = append(out, Instr{Op: opCLR, Args: [5]int32{int32(r0 + r)}, Line: line})
			}
		}
		f.Inlines = append(f.Inlines, Inline{Start: start, End: label(len(out)), Func: site.g, Line: line})
		// Its arrays of sized integers (what it reads of them) are f's.
		f.Flags |= o.p.Funcs[site.g].Flags & bc.FSized
	}
	if !inlined {
		return false
	}
	newIdx[len(f.Code)] = len(out)
	for l := 0; l < nlabels; l++ {
		if f.labels[l] >= 0 {
			f.labels[l] = newIdx[f.labels[l]]
		}
	}
	for _, p := range places {
		f.labels[p.l] = p.at
	}
	f.Code = out
	o.n = len(f.Kinds)
	// Innermost first: an inner range is no longer than the one it's in.
	sort.SliceStable(f.Inlines, func(a, b int) bool {
		sa, ea := f.labels[f.Inlines[a].Start], f.labels[f.Inlines[a].End]
		sb, eb := f.labels[f.Inlines[b].Start], f.labels[f.Inlines[b].End]
		return ea-sa < eb-sb
	})
	return true
}
