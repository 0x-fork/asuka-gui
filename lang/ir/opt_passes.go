package ir

import (
	"math"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// ---------------------------------------------------------------------
// Handlers, unreachable code, jumps

// dropDeadHandlers drops the handlers whose ranges have nothing that
// throws: what the lowering puts around a change in place, to put the
// place back as an error leaves, when nothing in it can fail.
func (o *optimizer) dropDeadHandlers() {
	f := o.f
	hs := f.Handlers[:0]
	for _, h := range f.Handlers {
		s, e, _ := o.handlerRange(h)
		live := false
		for i := s; i < e && i < len(f.Code); i++ {
			if o.throwsHere(&f.Code[i]) {
				live = true
				break
			}
		}
		if live {
			hs = append(hs, h)
		} else {
			o.changed = true
		}
	}
	f.Handlers = hs
}

// unreachable deletes what no path from the function's start reaches:
// a dropped handler's code, what follows a return.
func (o *optimizer) unreachable() {
	code := o.f.Code
	seen := make([]bool, len(code))
	stack := []int{0}
	for len(stack) > 0 {
		i := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		for i < len(code) && !seen[i] {
			seen[i] = true
			in := &code[i]
			info := &opInfos[in.Op]
			if h := o.handlerOf[i]; h >= 0 && o.throwsHere(in) {
				stack = append(stack, h)
			}
			if info.jump >= 0 {
				stack = append(stack, o.target(in))
			}
			if info.term {
				break
			}
			i++
		}
	}
	for i := range code {
		if !seen[i] && code[i].Op != opNOP {
			o.nop(i)
		}
	}
}

// targets marks the instructions a jump or a handler goes to.
func (o *optimizer) targets() []bool {
	code := o.f.Code
	t := make([]bool, len(code)+1)
	for i := range code {
		if opInfos[code[i].Op].jump >= 0 {
			t[o.target(&code[i])] = true
		}
	}
	for _, h := range o.f.Handlers {
		t[o.f.labels[h.Target]] = true
	}
	return t
}

// next is the first instruction from i on that isn't a NOP.
func (o *optimizer) next(i int) int {
	for i < len(o.f.Code) && o.f.Code[i].Op == opNOP {
		i++
	}
	return i
}

// inverse is the jump on the opposite condition: a's operands, with the
// result's order (swap: the registers' order is swapped).
var inverse = map[string]struct {
	op   string
	swap bool
}{
	"JT": {"JF", false}, "JF": {"JT", false}, "JNONE": {"JSOME", false}, "JSOME": {"JNONE", false},
	"JLTI": {"JLEI", true}, "JLEI": {"JLTI", true}, "JEQI": {"JNEI", false}, "JNEI": {"JEQI", false},
	"JLTIS": {"JGEIS", false}, "JGEIS": {"JLTIS", false}, "JLEIS": {"JGTIS", false}, "JGTIS": {"JLEIS", false},
	"JEQIS": {"JNEIS", false}, "JNEIS": {"JEQIS", false},
}

// threadJumps sends a jump to a jump where that one goes, deletes jumps
// to the next instruction, makes a jump to a return the return, and
// turns a conditional jump over a jump into the opposite one.
func (o *optimizer) threadJumps() {
	f := o.f
	code := f.Code
	targeted := o.targets()
	for i := range code {
		in := &code[i]
		info := &opInfos[in.Op]
		if info.jump < 0 {
			continue
		}
		j := int(info.jump)
		for hops := 0; hops < 16; hops++ {
			t := o.next(o.target(in))
			if t >= len(code) || code[t].Op != opJMP || t == i {
				break
			}
			if code[t].Args[0] == in.Args[j] {
				break
			}
			in.Args[j] = code[t].Args[0]
			o.changed = true
		}
		t := o.next(o.target(in))
		nx := o.next(i + 1)
		_, cond := inverse[bc.Ops[in.Op].Name]
		switch {
		case t == nx && (in.Op == opJMP || cond):
			o.nop(i)
		case in.Op == opJMP && t < len(code) && (code[t].Op == opRET || code[t].Op == opRET0):
			line := in.Line
			*in = code[t]
			in.Line = line
			o.changed = true
		case in.Op == opJMP && t < len(code) && t <= i && o.rotates(t, nx):
			// A loop's jump back to its test, which leaves the loop for
			// what follows the jump: the test, the other way round,
			// jumping back into the loop's body (one jump a turn, not
			// two).
			c := code[t]
			inv := inverse[bc.Ops[c.Op].Name]
			body := f.NewLabel()
			f.labels[body] = o.next(t + 1)
			jj := int(opInfos[c.Op].jump)
			c.Op = op(inv.op)
			if inv.swap {
				c.Args[0], c.Args[1] = c.Args[1], c.Args[0]
			}
			c.Args[jj] = int32(body)
			c.Line = in.Line
			*in = c
			o.changed = true
		case cond && nx < len(code) && code[nx].Op == opJMP && !targeted[nx] && t == o.next(nx+1):
			// JF c, L1; JMP L2; L1: — JT c, L2.
			inv := inverse[bc.Ops[in.Op].Name]
			in.Op = op(inv.op)
			if inv.swap {
				in.Args[0], in.Args[1] = in.Args[1], in.Args[0]
			}
			in.Args[j] = code[nx].Args[0]
			o.nop(nx)
		}
	}
}

// rotates reports whether the instruction at t is a conditional jump to
// nx (and not to the instruction after it): a loop's test, which a jump
// back to it at nx-1 can be replaced with.
func (o *optimizer) rotates(t, nx int) bool {
	c := &o.f.Code[t]
	if _, ok := inverse[bc.Ops[c.Op].Name]; !ok {
		return false
	}
	return o.next(o.target(c)) == nx && o.next(t+1) != nx
}

// hoistConstants moves a constant's load out of a loop, to the
// function's start, when it's its register's only write and no path
// reads the register before it (so every read reads the constant).
func (o *optimizer) hoistConstants() {
	f := o.f
	code := f.Code
	defs := make([]int, o.n)
	for i := range code {
		o.writes(&code[i], func(r int) { defs[r]++ })
	}
	// What's inside a loop: between a jump back and where it goes.
	inLoop := make([]bool, len(code))
	for q := range code {
		if opInfos[code[q].Op].jump >= 0 {
			if t := o.target(&code[q]); t <= q {
				for i := t; i <= q; i++ {
					inLoop[i] = true
				}
			}
		}
	}
	// A divisor's or a shift's constant stays by it: the C compiler
	// divides by a constant it sees cheaply (a multiply), by a register
	// it can't prove unchanged slowly.
	literal := make([]bool, o.n)
	for i := range code {
		switch bc.Ops[code[i].Op].Name {
		case "DIVI", "MODI", "SHL", "SHR":
			literal[code[i].Args[2]] = true
		}
	}
	live := o.liveness()
	var hoist []Instr
	for i := range code {
		in := &code[i]
		if !inLoop[i] || in.Op != opKSMALL && in.Op != opKINT && in.Op != opKFLOAT {
			continue
		}
		r := int(in.Args[0])
		if defs[r] != 1 || live[0].has(r) || literal[r] {
			continue
		}
		hoist = append(hoist, *in)
		o.nop(i)
	}
	if len(hoist) == 0 {
		return
	}
	f.Code = append(hoist, f.Code...)
	for l, i := range f.labels {
		if i > 0 {
			f.labels[l] = i + len(hoist)
		}
	}
}

// ---------------------------------------------------------------------
// Forward analyses: copies, constants, references known to be none

// flow runs a forward analysis over the blocks: state s is copied into
// each block's start (meet merges a predecessor's into it, reporting a
// change), and step moves it over an instruction. An error thrown at an
// instruction goes to its handler with the state before it.
type flow[S any] struct {
	entry func() S
	clone func(S) S
	// meet merges from into into (nil into: from's copy), reporting a change.
	meet func(into *S, from S, first bool) bool
	step func(i int, s S)
}

func runFlow[S any](o *optimizer, fl flow[S]) []S {
	nb := len(o.blocks)
	in := make([]S, nb)
	have := make([]bool, nb)
	in[0] = fl.entry()
	have[0] = true
	work := []int{0}
	queued := make([]bool, nb)
	queued[0] = true
	code := o.f.Code
	push := func(b int, s S) {
		if !have[b] {
			in[b] = fl.clone(s)
			have[b] = true
		} else if !fl.meet(&in[b], s, false) {
			return
		}
		if !queued[b] {
			queued[b] = true
			work = append(work, b)
		}
	}
	for len(work) > 0 {
		b := work[0]
		work = work[1:]
		queued[b] = false
		s := fl.clone(in[b])
		bl := &o.blocks[b]
		for i := bl.start; i < bl.end; i++ {
			if h := o.handlerOf[i]; h >= 0 && h < len(code) && o.throwsHere(&code[i]) {
				push(o.blockOf[h], s)
			}
			fl.step(i, s)
		}
		for _, sb := range bl.succ() {
			push(sb, s)
		}
	}
	for b := range in {
		if !have[b] {
			in[b] = fl.entry() // unreachable: anything goes
		}
	}
	return in
}

// replay runs step over block b's instructions from its in state,
// calling visit before each.
func replay[S any](o *optimizer, fl flow[S], in []S, b int, visit func(i int, s S)) {
	s := fl.clone(in[b])
	bl := &o.blocks[b]
	for i := bl.start; i < bl.end; i++ {
		visit(i, s)
		fl.step(i, s)
	}
}

// noneFlow: the reference registers known to hold none. At the start,
// every one but the parameters does (a frame's registers start none).
func (o *optimizer) noneFlow() flow[regset] {
	f := o.f
	return flow[regset]{
		entry: func() regset {
			s := newRegset(o.n)
			for r := f.NParams; r < o.n; r++ {
				if f.Kinds[r] == bc.KRef {
					s.add(r)
				}
			}
			return s
		},
		clone: func(s regset) regset { t := newRegset(o.n); t.copyFrom(s); return t },
		meet:  func(into *regset, from regset, _ bool) bool { return into.intersect(from) },
		step: func(i int, s regset) {
			in := &f.Code[i]
			name := bc.Ops[in.Op].Name
			wasNone := false
			if in.Op == opMOVR {
				wasNone = s.has(int(in.Args[1]))
			}
			o.writes(in, s.del)
			switch name {
			case "CLR", "KNONE", "THROW", "RETHROW":
				s.add(int(in.Args[0]))
			case "MOVR":
				if wasNone {
					s.add(int(in.Args[0]))
				}
			case "CPUT", "CPUTP":
				if r := int(in.Args[1]); f.Kinds[r] == bc.KRef {
					s.add(r)
				}
			}
		},
	}
}

// noneBefore is what's known none before each instruction: the
// analysis's state at each block's start, and a block's instructions'
// states replayed from it as they're asked for (one block's at a time,
// in a buffer reused).
func (o *optimizer) noneBefore() *noneSets {
	fl := o.noneFlow()
	return &noneSets{o: o, fl: fl, in: runFlow(o, fl), blk: -1, w: (o.n + 63) / 64}
}

type noneSets struct {
	o   *optimizer
	fl  flow[regset]
	in  []regset
	blk int
	w   int
	buf []uint64
}

// has reports whether register r is known to hold none before
// instruction i.
func (n *noneSets) has(i, r int) bool {
	o := n.o
	b := o.blockOf[i]
	bl := &o.blocks[b]
	if b != n.blk {
		n.blk = b
		size := (bl.end - bl.start) * n.w
		if cap(n.buf) < size {
			n.buf = make([]uint64, size)
		}
		n.buf = n.buf[:size]
		replay(o, n.fl, n.in, b, func(k int, s regset) { copy(n.buf[(k-bl.start)*n.w:], s) })
	}
	return n.buf[(i-bl.start)*n.w+r>>6]&(1<<(r&63)) != 0
}

// has reports whether f's code has an instruction of one of ops.
func (o *optimizer) has(ops ...bc.Op) bool {
	for i := range o.f.Code {
		for _, op := range ops {
			if o.f.Code[i].Op == op {
				return true
			}
		}
	}
	return false
}

// copyProp replaces a read of a register that's a copy of another (MOV
// or MOVR d, s, neither written since) with a read of that one: the copy
// is then often dead. A reference is read borrowed, so reading s is
// reading d; s isn't written while the copy holds, so nothing changed it
// in place. Only single operands are replaced, and the block of a call
// of one argument to a function that isn't a mut fn: a block of more
// registers is a block, and a mut fn's receiver goes back into its
// register.
func (o *optimizer) copyProp() {
	f := o.f
	if !o.has(opMOV, opMOVR) {
		return
	}
	type copies map[int32]int32 // d → s
	fl := flow[copies]{
		entry: func() copies { return copies{} },
		clone: func(c copies) copies {
			d := make(copies, len(c))
			for k, v := range c {
				d[k] = v
			}
			return d
		},
		meet: func(into *copies, from copies, _ bool) bool {
			changed := false
			for k, v := range *into {
				if w, ok := from[k]; !ok || w != v {
					delete(*into, k)
					changed = true
				}
			}
			return changed
		},
	}
	fl.step = func(i int, c copies) {
		in := &f.Code[i]
		o.writes(in, func(w int) {
			delete(c, int32(w))
			for k, v := range c {
				if v == int32(w) {
					delete(c, k)
				}
			}
		})
		// MOV of a reference moves it (its owner changes, nothing
		// retained): not a copy.
		if (in.Op == opMOV && f.Kinds[in.Args[0]] != bc.KRef || in.Op == opMOVR) && in.Args[0] != in.Args[1] {
			s := in.Args[1]
			if t, ok := c[s]; ok {
				s = t
			}
			if s != in.Args[0] {
				c[in.Args[0]] = s
			}
		}
	}
	in := runFlow(o, fl)
	for b := range o.blocks {
		replay(o, fl, in, b, func(i int, c copies) {
			if len(c) == 0 {
				return
			}
			ins := &f.Code[i]
			if ins.Op == opMOV && f.Kinds[ins.Args[0]] == bc.KRef {
				return // a move's source is the owner it moves from
			}
			for _, j := range opInfos[ins.Op].uses {
				r := ins.Args[j]
				if r == bc.None {
					continue
				}
				if s, ok := c[r]; ok {
					ins.Args[j] = s
					o.changed = true
				}
			}
			if (ins.Op == opCALL || ins.Op == opCALLI) && ins.Args[3] == 1 && o.p.notMut(ins) {
				if s, ok := c[ins.Args[2]]; ok {
					ins.Args[2] = s
					o.changed = true
				}
			}
		})
	}
}

// A konst is a scalar register's known value: an int's or a bool's, or
// a float's bits.
type konst struct {
	i int64
	f float64
}

// constants propagates the scalar registers' known values, folds what
// they compute (not what would throw: an overflow throws as it runs),
// decides branches on them, and gives the operations that take one an
// immediate (ADDIS and the jumps that compare with one, selectOps).
func (o *optimizer) constants() {
	f := o.f
	type consts map[int32]konst
	fl := flow[consts]{
		entry: func() consts { return consts{} },
		clone: func(c consts) consts {
			d := make(consts, len(c))
			for k, v := range c {
				d[k] = v
			}
			return d
		},
		meet: func(into *consts, from consts, _ bool) bool {
			changed := false
			for k, v := range *into {
				if w, ok := from[k]; !ok || w != v && !(math.IsNaN(w.f) && math.IsNaN(v.f)) {
					delete(*into, k)
					changed = true
				}
			}
			return changed
		},
	}
	fl.step = func(i int, c consts) {
		in := &f.Code[i]
		v, ok := o.eval(in, c)
		o.writes(in, func(w int) { delete(c, int32(w)) })
		if ok {
			c[in.Args[0]] = v
		}
	}
	in := runFlow(o, fl)
	o.konsts = make([][2]*konst, len(f.Code))
	kslab := make([]konst, 0, 64)
	for b := range o.blocks {
		replay(o, fl, in, b, func(i int, c consts) {
			ins := &f.Code[i]
			name := bc.Ops[ins.Op].Name
			get := func(j int) (konst, bool) {
				r := ins.Args[j]
				if r == bc.None || f.Kinds[r] == bc.KRef {
					return konst{}, false
				}
				v, ok := c[r]
				return v, ok
			}
			// A branch decided.
			if taken, known := o.decided(ins, get); known {
				if taken {
					l := ins.Args[opInfos[ins.Op].jump]
					*ins = Instr{Op: opJMP, Args: [5]int32{l}, Line: ins.Line}
					o.changed = true
				} else {
					o.nop(i)
				}
				return
			}
			// A value computed from known ones.
			if v, ok := o.eval(ins, c); ok && ins.Op != opKSMALL && ins.Op != opKINT && ins.Op != opKFLOAT {
				d := ins.Args[0]
				*ins = o.load(d, v, ins.Line)
				o.changed = true
				return
			}
			switch name {
			case "ADDI", "SUBI", "MULI", "DIVI", "MODI", "WADDI", "WSUBI", "EQI", "NEI", "LTI", "LEI":
				var k [2]*konst
				for j := 1; j <= 2; j++ {
					if v, ok := get(j); ok {
						if len(kslab) == cap(kslab) {
							kslab = make([]konst, 0, 2*cap(kslab))
						}
						kslab = append(kslab, v)
						k[j-1] = &kslab[len(kslab)-1]
					}
				}
				o.konsts[i] = k
			}
		})
	}
}

// load is an instruction putting v in register d, of d's kind.
func (o *optimizer) load(d int32, v konst, line int32) Instr {
	if o.f.Kinds[d] == bc.KFloat {
		return Instr{Op: opKFLOAT, Args: [5]int32{d, int32(o.floatConst(v.f))}, Line: line}
	}
	if v.i >= math.MinInt16 && v.i <= math.MaxInt16 {
		return Instr{Op: opKSMALL, Args: [5]int32{d, int32(uint16(int16(v.i)))}, Line: line}
	}
	return Instr{Op: opKINT, Args: [5]int32{d, int32(o.intConst(v.i))}, Line: line}
}

func (o *optimizer) intConst(v int64) int {
	m := o.p.Mod
	if o.p.ints == nil {
		o.p.ints = map[int64]int{}
		for i, x := range m.Ints {
			if _, ok := o.p.ints[x]; !ok {
				o.p.ints[x] = i
			}
		}
	}
	if i, ok := o.p.ints[v]; ok {
		return i
	}
	m.Ints = append(m.Ints, v)
	o.p.ints[v] = len(m.Ints) - 1
	return len(m.Ints) - 1
}

func (o *optimizer) floatConst(v float64) int {
	m := o.p.Mod
	b := math.Float64bits(v)
	if o.p.floats == nil {
		o.p.floats = map[uint64]int{}
		for i, x := range m.Floats {
			if _, ok := o.p.floats[math.Float64bits(x)]; !ok {
				o.p.floats[math.Float64bits(x)] = i
			}
		}
	}
	if i, ok := o.p.floats[b]; ok {
		return i
	}
	m.Floats = append(m.Floats, v)
	o.p.floats[b] = len(m.Floats) - 1
	return len(m.Floats) - 1
}

// eval is the value in computes into its register (Args[0]), when its
// operands are known and it doesn't throw: what the VM's code for it
// computes, bit for bit.
func (o *optimizer) eval(in *Instr, c map[int32]konst) (konst, bool) {
	f := o.f
	name := bc.Ops[in.Op].Name
	a := in.Args
	get := func(j int) (konst, bool) {
		r := a[j]
		if r == bc.None || int(r) >= o.n || f.Kinds[r] == bc.KRef {
			return konst{}, false
		}
		v, ok := c[r]
		return v, ok
	}
	if opInfos[in.Op].def != 0 || a[0] == bc.None || f.Kinds[a[0]] == bc.KRef {
		return konst{}, false
	}
	b2 := func() (x, y konst, ok bool) {
		x, ok1 := get(1)
		y, ok2 := get(2)
		return x, y, ok1 && ok2
	}
	bv := func(b bool) konst {
		if b {
			return konst{i: 1}
		}
		return konst{}
	}
	switch name {
	case "KSMALL":
		return konst{i: int64(int16(a[1]))}, true
	case "KINT":
		return konst{i: o.p.Mod.Ints[a[1]]}, true
	case "KFLOAT":
		return konst{f: o.p.Mod.Floats[a[1]]}, true
	case "MOV":
		return get(1)
	case "ADDI", "SUBI", "MULI":
		x, y, ok := b2()
		if !ok {
			return konst{}, false
		}
		var r int64
		var of bool
		switch name {
		case "ADDI":
			r = x.i + y.i
			of = (x.i >= 0) == (y.i >= 0) && (r >= 0) != (x.i >= 0)
		case "SUBI":
			r = x.i - y.i
			of = (x.i >= 0) != (y.i >= 0) && (r >= 0) != (x.i >= 0)
		case "MULI":
			r = x.i * y.i
			of = x.i != 0 && (r/x.i != y.i || x.i == -1 && y.i == math.MinInt64 || y.i == -1 && x.i == math.MinInt64)
		}
		return konst{i: r}, !of
	case "DIVI", "MODI":
		x, y, ok := b2()
		if !ok || y.i == 0 || y.i == -1 && x.i == math.MinInt64 && name == "DIVI" {
			return konst{}, false
		}
		if y.i == -1 {
			if name == "MODI" {
				return konst{}, true
			}
			return konst{i: -x.i}, true
		}
		if name == "DIVI" {
			return konst{i: x.i / y.i}, true
		}
		return konst{i: x.i % y.i}, true
	case "NEGI":
		x, ok := get(1)
		return konst{i: -x.i}, ok && x.i != math.MinInt64
	case "WADDI", "WSUBI", "WMULI", "BAND", "BOR", "BXOR", "EQI", "NEI", "LTI", "LEI":
		x, y, ok := b2()
		if !ok {
			return konst{}, false
		}
		switch name {
		case "WADDI":
			return konst{i: x.i + y.i}, true
		case "WSUBI":
			return konst{i: x.i - y.i}, true
		case "WMULI":
			return konst{i: x.i * y.i}, true
		case "BAND":
			return konst{i: x.i & y.i}, true
		case "BOR":
			return konst{i: x.i | y.i}, true
		case "BXOR":
			return konst{i: x.i ^ y.i}, true
		case "EQI":
			return bv(x.i == y.i), true
		case "NEI":
			return bv(x.i != y.i), true
		case "LTI":
			return bv(x.i < y.i), true
		default:
			return bv(x.i <= y.i), true
		}
	case "ADDIK":
		x, ok := get(1)
		return konst{i: x.i + int64(int16(a[2]))}, ok
	case "ADDIS", "SUBIS", "MULIS":
		x, ok := get(1)
		if !ok {
			return konst{}, false
		}
		k := int64(int16(a[2]))
		switch name {
		case "ADDIS":
			r := x.i + k
			return konst{i: r}, !((x.i >= 0) == (k >= 0) && (r >= 0) != (x.i >= 0))
		case "SUBIS":
			r := x.i - k
			return konst{i: r}, !((x.i >= 0) != (k >= 0) && (r >= 0) != (x.i >= 0))
		default:
			r := x.i * k
			return konst{i: r}, x.i == 0 || r/x.i == k && !(k == -1 && x.i == math.MinInt64)
		}
	case "DIVIK", "MODIK":
		x, ok := get(1)
		k := o.p.Mod.Ints[a[2]]
		if name == "DIVIK" {
			return konst{i: x.i / k}, ok
		}
		return konst{i: x.i % k}, ok
	case "DIVIS", "MODIS":
		x, ok := get(1)
		k := int64(int16(a[2]))
		if name == "DIVIS" {
			return konst{i: x.i / k}, ok
		}
		return konst{i: x.i % k}, ok
	case "BNOT":
		x, ok := get(1)
		return konst{i: ^x.i}, ok
	case "NOT":
		x, ok := get(1)
		return bv(x.i == 0), ok
	case "SHL", "SHR":
		x, y, ok := b2()
		if !ok || y.i < 0 {
			return konst{}, false
		}
		if name == "SHR" {
			if y.i >= 64 {
				if x.i < 0 {
					return konst{i: -1}, true
				}
				return konst{}, true
			}
			return konst{i: x.i >> uint(y.i)}, true
		}
		if y.i >= 64 {
			return konst{}, true
		}
		return konst{i: int64(uint64(x.i) << uint(y.i))}, true
	case "ADDF", "SUBF", "MULF", "DIVF", "EQF", "NEF", "LTF", "LEF":
		x, y, ok := b2()
		if !ok {
			return konst{}, false
		}
		switch name {
		case "ADDF":
			return konst{f: x.f + y.f}, true
		case "SUBF":
			return konst{f: x.f - y.f}, true
		case "MULF":
			return konst{f: x.f * y.f}, true
		case "DIVF":
			return konst{f: x.f / y.f}, true
		case "EQF":
			return bv(x.f == y.f), true
		case "NEF":
			return bv(x.f != y.f), true
		case "LTF":
			return bv(x.f < y.f), true
		default:
			return bv(x.f <= y.f), true
		}
	case "NEGF":
		x, ok := get(1)
		return konst{f: -x.f}, ok
	case "I2F":
		x, ok := get(1)
		return konst{f: float64(x.i)}, ok
	case "ADDN", "SUBN", "MULN", "DIVN", "MODN", "POWN", "POWI", "SHLN", "SHRN", "LTU", "LEU":
		// The sized integers': folded only when they don't throw (an
		// overflow at the type's width throws as it runs).
		x, y, ok := b2()
		if !ok {
			return konst{}, false
		}
		t := int(a[3])
		if name == "POWI" {
			t = 0x33
		}
		r, ok := sizedEval(name, x.i, y.i, t)
		return konst{i: r}, ok
	case "NEGN", "WRAP", "CONVN", "CLAMPN":
		x, ok := get(1)
		if !ok {
			return konst{}, false
		}
		r, ok := sizedEval(name, x.i, 0, int(a[2]))
		return konst{i: r}, ok
	case "U2F":
		x, ok := get(1)
		return konst{f: float64(uint64(x.i))}, ok
	case "F2I":
		x, ok := get(1)
		if !ok {
			return konst{}, false
		}
		d := x.f
		switch {
		case d != d:
			return konst{}, true
		case d >= 9.2233720368547758e18:
			return konst{i: math.MaxInt64}, true
		case d <= -9.2233720368547758e18:
			return konst{i: math.MinInt64}, true
		}
		return konst{i: int64(d)}, true
	}
	return konst{}, false
}

// decided reports whether a conditional jump's operands are known, and
// then whether it's taken.
func (o *optimizer) decided(in *Instr, get func(j int) (konst, bool)) (taken, known bool) {
	name := bc.Ops[in.Op].Name
	k := int64(int16(in.Args[1]))
	switch name {
	case "JT", "JF":
		x, ok := get(0)
		return (x.i != 0) == (name == "JT"), ok
	case "JLTI", "JLEI", "JEQI", "JNEI":
		x, ok1 := get(0)
		y, ok2 := get(1)
		if !ok1 || !ok2 {
			return false, false
		}
		switch name {
		case "JLTI":
			return x.i < y.i, true
		case "JLEI":
			return x.i <= y.i, true
		case "JEQI":
			return x.i == y.i, true
		}
		return x.i != y.i, true
	case "JLTIS", "JLEIS", "JGTIS", "JGEIS", "JEQIS", "JNEIS":
		x, ok := get(0)
		if !ok {
			return false, false
		}
		switch name {
		case "JLTIS":
			return x.i < k, true
		case "JLEIS":
			return x.i <= k, true
		case "JGTIS":
			return x.i > k, true
		case "JGEIS":
			return x.i >= k, true
		case "JEQIS":
			return x.i == k, true
		}
		return x.i != k, true
	}
	return false, false
}

// ---------------------------------------------------------------------
// Instruction selection

func fits16(v int64) bool { return v >= math.MinInt16 && v <= math.MaxInt16 }

// fusedJump is the jump a comparison cmp of x and y (kx, ky: their
// values when known) and the jump on its result jt (JT, or JF) come to.
func fusedJump(cmp string, jt bool, x, y int32, kx, ky *konst) (string, int32, int32, bool) {
	// The comparison the jump is taken on: x op y.
	opn := cmp
	if !jt {
		switch cmp {
		case "LTI": // !(x < y): y <= x
			opn, x, y, kx, ky = "LEI", y, x, ky, kx
		case "LEI":
			opn, x, y, kx, ky = "LTI", y, x, ky, kx
		case "EQI":
			opn = "NEI"
		case "NEI":
			opn = "EQI"
		}
	}
	reg := map[string]string{"LTI": "JLTI", "LEI": "JLEI", "EQI": "JEQI", "NEI": "JNEI"}[opn]
	imm := func(v int64) int32 { return int32(uint16(int16(v))) }
	if ky != nil && fits16(ky.i) {
		return map[string]string{"LTI": "JLTIS", "LEI": "JLEIS", "EQI": "JEQIS", "NEI": "JNEIS"}[opn], x, imm(ky.i), true
	}
	if kx != nil && fits16(kx.i) {
		// k op y: y op' k.
		return map[string]string{"LTI": "JGTIS", "LEI": "JGEIS", "EQI": "JEQIS", "NEI": "JNEIS"}[opn], y, imm(kx.i), true
	}
	return reg, x, y, false
}

// selectOps gives an int operation with a known operand that fits in 16
// bits its immediate form, and fuses a comparison into the jump on it
// when nothing else reads its result.
func (o *optimizer) selectOps() {
	f := o.f
	code := f.Code
	if len(o.konsts) != len(code) {
		return
	}
	targeted := o.targets()
	// What a comparison or a NOT may fuse into: liveness says, and only
	// then is it worked out.
	fusable := false
	for i := 0; i+1 < len(code) && !fusable; i++ {
		switch bc.Ops[code[i].Op].Name {
		case "EQI", "NEI", "LTI", "LEI", "NOT":
			n := bc.Ops[code[o.next(i+1)].Op].Name
			fusable = n == "JT" || n == "JF"
		}
	}
	var live []regset
	if fusable {
		live = o.liveness()
	}
	after := map[int]regset{}
	cur := newRegset(o.n)
	for b := range o.blocks {
		if !fusable {
			break
		}
		o.liveOut(live, b, cur)
		o.walkBack(live, b, cur, func(i int, l regset) {
			if bc.Ops[code[i].Op].Name == "JT" || bc.Ops[code[i].Op].Name == "JF" {
				after[i] = append(regset(nil), l...)
			}
		})
	}
	imm := func(v int64) int32 { return int32(uint16(int16(v))) }
	for i := range code {
		in := &code[i]
		k := o.konsts[i]
		name := bc.Ops[in.Op].Name
		switch name {
		case "ADDI", "MULI":
			// Commutative: the known one either side.
			if k[1] == nil && k[0] != nil {
				in.Args[1], in.Args[2] = in.Args[2], in.Args[1]
				k[0], k[1] = k[1], k[0]
			}
			fallthrough
		case "SUBI":
			if k[1] != nil && fits16(k[1].i) {
				in.Op = op(name[:3] + "IS")
				in.Args[2] = imm(k[1].i)
				o.changed = true
			}
		case "DIVI", "MODI":
			if k[1] != nil && k[1].i != 0 && k[1].i != -1 {
				if fits16(k[1].i) {
					in.Op = op(name[:3] + "IS")
					in.Args[2] = imm(k[1].i)
				} else {
					// The divisor a constant of the pool's: no load before
					// it, and a literal in the C (a multiply, not a divide).
					in.Op = op(name[:3] + "IK")
					in.Args[2] = int32(o.intConst(k[1].i))
				}
				o.changed = true
			}
		case "WADDI", "WSUBI":
			if name == "WADDI" && k[1] == nil && k[0] != nil {
				in.Args[1], in.Args[2] = in.Args[2], in.Args[1]
				k[0], k[1] = k[1], k[0]
			}
			if k[1] != nil {
				v := k[1].i
				if name == "WSUBI" {
					v = -v
				}
				if fits16(v) && !(name == "WSUBI" && k[1].i == math.MinInt64) {
					in.Op = op("ADDIK")
					in.Args[2] = imm(v)
					o.changed = true
				}
			}
		case "NOT":
			// NOT c, x; JT c: JF x.
			j := o.next(i + 1)
			if j >= len(code) || targeted[j] {
				break
			}
			jn := bc.Ops[code[j].Op].Name
			c := in.Args[0]
			if (jn != "JT" && jn != "JF") || code[j].Args[0] != c || after[j] == nil || after[j].has(int(c)) {
				break
			}
			code[j].Op = op(inverse[jn].op)
			code[j].Args[0] = in.Args[1]
			o.nop(i)
		case "EQI", "NEI", "LTI", "LEI":
			j := o.next(i + 1)
			if j >= len(code) || targeted[j] {
				break
			}
			jn := bc.Ops[code[j].Op].Name
			c := in.Args[0]
			if (jn != "JT" && jn != "JF") || code[j].Args[0] != c || after[j] == nil || after[j].has(int(c)) {
				break
			}
			fop, x, y, _ := fusedJump(name, jn == "JT", in.Args[1], in.Args[2], k[0], k[1])
			l := code[j].Args[1]
			code[j] = Instr{Op: op(fop), Args: [5]int32{x, y, l}, Line: code[j].Line}
			o.nop(i)
		}
	}
	o.konsts = nil
}

// ---------------------------------------------------------------------
// Copies folded into what they copy, dead code

// coalesce folds a copy of a register just written into the write:
// "OP t, ...; MOV d, t" is "OP d, ..." when nothing reads t after. A
// reference is folded only when t held none before (so no value is kept
// that the lowering let go of).
func (o *optimizer) coalesce() {
	f := o.f
	if !o.has(opMOV, opMOVR) {
		return
	}
	code := f.Code
	targeted := o.targets()
	live := o.liveness()
	none := o.noneBefore()
	cur := newRegset(o.n)
	for b := range o.blocks {
		o.liveOut(live, b, cur)
		o.walkBack(live, b, cur, func(i int, after regset) {
			in := &code[i]
			if in.Op != opMOV && in.Op != opMOVR || targeted[i] {
				return
			}
			d, t := int(in.Args[0]), int(in.Args[1])
			if d == t || after.has(t) || in.Op == opMOV && f.Kinds[t] == bc.KRef {
				// (MOV of a reference moves it, and its destination's old
				// value isn't let go of: a register moved from.)
				return
			}
			// The write of t this copies, earlier in the block: what's
			// between mustn't read or write d or t, nor throw to a
			// handler that reads d (which would then see it written).
			j := i - 1
			for ; j >= o.blocks[b].start; j-- {
				c := &code[j]
				if c.Op == opNOP {
					continue
				}
				if kill(c) == t || c.Op == op("MGETJ") && int(c.Args[0]) == t {
					break
				}
				if o.readsReg(c, d) || o.writesReg(c, d) || o.readsReg(c, t) || o.writesReg(c, t) {
					return
				}
				if h := o.handlerOf[j]; h >= 0 && o.throwsHere(c) && live[o.blockOf[h]].has(d) {
					return
				}
			}
			if j < o.blocks[b].start {
				return
			}
			w := &code[j]
			info := &opInfos[w.Op]
			def := info.def
			if def < 0 && w.Op == op("MGETJ") {
				def = info.maydef
			}
			if def < 0 || int(w.Args[def]) != t {
				return
			}
			if o.readsReg(w, d) && !info.alias {
				return
			}
			if o.writesOther(w, t, d) {
				return
			}
			if f.Kinds[t] == bc.KRef && (!none.has(j, t) || !o.releasedNext(i, t, d)) {
				return
			}
			w.Args[def] = int32(d)
			o.nop(i)
		})
	}
}

// releasedNext reports whether t, copied into d at instruction i, is
// let go of (CLR, or written) later in the block, before d is written
// and with t not read: then d holds the value at least as long as t did,
// and t's write folded into d keeps it exactly as long. (The run time's
// cursors borrow what they read: a value must never be let go of sooner
// than the lowering lets it go.)
func (o *optimizer) releasedNext(i, t, d int) bool {
	code := o.f.Code
	for k := i + 1; k < o.blocks[o.blockOf[i]].end; k++ {
		c := &code[k]
		if c.Op == opNOP {
			continue
		}
		if o.readsReg(c, t) || o.writesReg(c, d) || o.throwsHere(c) {
			return false
		}
		if o.writesReg(c, t) {
			return kill(c) == t
		}
	}
	return false
}

// writesOther reports whether in writes d other than as its result t.
func (o *optimizer) writesOther(in *Instr, t, d int) bool {
	found := false
	o.writes(in, func(r int) {
		if r == d {
			found = true
		}
	})
	return found
}

// deadCode deletes what computes a value nothing reads: a scalar's, or
// a new value (a struct, an array...) into a reference register that
// held none: nothing reads it, so nothing borrows it, and nothing held
// is let go. A copy of a reference stays: the copy may be what keeps
// the value alive for what borrowed it. It also deletes copies of a
// register into itself, and CLR of one known none.
// fresh are the instructions whose result is a new value, not one
// another register or object holds too.
var fresh = map[bc.Op]bool{op("NEWS"): true, op("NEWA"): true, op("NEWE"): true, op("NEWM"): true, op("CLOS"): true, op("BOX"): true}

func (o *optimizer) deadCode() {
	f := o.f
	code := f.Code
	live := o.liveness()
	none := o.noneBefore()
	cur := newRegset(o.n)
	for b := range o.blocks {
		o.liveOut(live, b, cur)
		bl := &o.blocks[b]
		for i := bl.end - 1; i >= bl.start; i-- {
			in := &code[i]
			info := &opInfos[in.Op]
			dead := false
			switch {
			case (in.Op == opMOV || in.Op == opMOVR) && in.Args[0] == in.Args[1]:
				dead = true
			case (in.Op == opCLR || in.Op == opKNONE) && none.has(i, int(in.Args[0])):
				dead = true
			case info.pure && !info.throws:
				if d := kill(in); d >= 0 && !cur.has(d) {
					dead = f.Kinds[d] != bc.KRef || none.has(i, d) && fresh[in.Op]
				}
			}
			if dead {
				o.nop(i)
				continue
			}
			if r := kill(in); r >= 0 {
				cur.del(r)
			}
			o.reads(in, cur.add)
			o.throwLive(i, live, cur)
		}
	}
}

// copyPairs deletes a copy of a reference that nothing reads before it's
// let go of: "MOVR d, s ... CLR d", s not written between (so s holds
// the value as long as d did) and d none before (so nothing of d's is
// kept longer). An argument's copy, a let's of a value another register
// holds, often comes to that once their reads read the original.
func (o *optimizer) copyPairs() {
	f := o.f
	if !o.has(opMOVR) {
		return
	}
	code := f.Code
	live := o.liveness()
	none := o.noneBefore()
	for i := range code {
		in := &code[i]
		if in.Op != opMOVR || in.Args[0] == in.Args[1] {
			continue
		}
		d, s := int(in.Args[0]), int(in.Args[1])
		if !none.has(i, d) {
			continue
		}
		end := o.blocks[o.blockOf[i]].end
		k := i + 1
		for ; k < end; k++ {
			c := &code[k]
			if c.Op == opNOP {
				continue
			}
			if (c.Op == opCLR || c.Op == opKNONE) && int(c.Args[0]) == d {
				o.nop(i)
				o.nop(k)
				break
			}
			if o.readsReg(c, d) || o.writesReg(c, d) || o.writesReg(c, s) {
				break
			}
			if h := o.handlerOf[k]; h >= 0 && o.throwsHere(c) && live[o.blockOf[h]].has(d) {
				break
			}
		}
		if k == end && o.unreadAcross(i, d, s) {
			// Let go of in other blocks (an inlined call's argument,
			// cleared after its body): the copy goes, its clears clear
			// none.
			o.nop(i)
		}
	}
}

// unreadAcross reports whether nothing reads d on the ways on from i
// (after it) before d is written again, nor writes s there (so s holds
// d's value as long as d would have).
func (o *optimizer) unreadAcross(i, d, s int) bool {
	code := o.f.Code
	seen := make([]bool, len(o.blocks))
	type span struct{ b, from int }
	work := []span{{o.blockOf[i], i + 1}}
	for len(work) > 0 {
		sp := work[len(work)-1]
		work = work[:len(work)-1]
		bl := &o.blocks[sp.b]
		killed := false
		for k := sp.from; k < bl.end; k++ {
			c := &code[k]
			if opInfos[c.Op].unknown || o.readsReg(c, d) || o.writesReg(c, s) {
				return false
			}
			if o.writesReg(c, d) {
				killed = true
				break
			}
		}
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
	return true
}

// compactRegs renumbers the registers the code still names into as few
// as there are, in order (so a block of them stays one), parameters
// first: a frame's registers are cleared as it's entered and its
// references let go of as it's left, the fewer the cheaper.
func (o *optimizer) compactRegs() {
	f := o.f
	used := make([]bool, o.n)
	for r := 0; r < f.NParams && r < o.n; r++ {
		used[r] = true
	}
	mark := func(r int) {
		if r >= 0 && r < o.n {
			used[r] = true
		}
	}
	for i := range f.Code {
		in := &f.Code[i]
		for j, kind := range bc.Ops[in.Op].Operands {
			if kind == bc.Reg && in.Args[j] != bc.None {
				mark(int(in.Args[j]))
			}
		}
		if s, n := o.blockRange(in); n > 0 {
			if opInfos[in.Op].blockKind == blockRest {
				// Its count is in a path: what's after its base stays.
				for r := s; r < o.n; r++ {
					mark(r)
				}
			} else {
				for r := s; r < s+n; r++ {
					mark(r)
				}
			}
		}
	}
	to := make([]int32, o.n)
	var kinds []bc.Kind
	for r := 0; r < o.n; r++ {
		if used[r] {
			to[r] = int32(len(kinds))
			kinds = append(kinds, f.Kinds[r])
		} else {
			to[r] = -1
		}
	}
	if len(kinds) == o.n {
		return
	}
	for i := range f.Code {
		in := &f.Code[i]
		for j, kind := range bc.Ops[in.Op].Operands {
			if kind != bc.Reg || in.Args[j] == bc.None {
				continue
			}
			r := in.Args[j]
			if r >= 0 && int(r) < o.n && to[r] >= 0 {
				in.Args[j] = to[r]
			} else {
				in.Args[j] = 0 // an empty block's base: never read
			}
		}
	}
	f.Kinds = kinds
	o.n = len(kinds)
	o.changed = true
}

// cseOps are the instructions whose scalar result is a function of their
// operands' registers (and what those hold, which changes only as the
// registers are written: a struct's fields with SETF of it, an array's
// elements with SETIDX of it): two alike, with nothing written between,
// compute the same value. One that throws throws in the first.
var cseOps = func() map[bc.Op]bool {
	m := map[bc.Op]bool{}
	for _, n := range splitNames("ADDI SUBI MULI DIVI MODI NEGI WADDI WSUBI WMULI ADDIK EQI NEI LTI LEI BAND BOR BXOR BNOT SHL SHR " +
		"ADDF SUBF MULF DIVF MODF NEGF EQF NEF LTF LEF I2F F2I NOT ISNONE LTS EQ NE LEN ECASE ISSHAPE " +
		"ADDIS SUBIS MULIS DIVIS MODIS DIVIK MODIK GETF GETP IDX IDXF") {
		m[op(n)] = true
	}
	return m
}()

// cse replaces a scalar computed again in a block, from registers not
// written since, with a copy of the first result (copyProp then reads
// that): b.vx * b.vx reads the field once.
func (o *optimizer) cse() {
	f := o.f
	code := f.Code
	type key struct {
		op   bc.Op
		args [4]int32
	}
	for b := range o.blocks {
		bl := &o.blocks[b]
		seen := map[key]int32{}
		for i := bl.start; i < bl.end; i++ {
			in := &code[i]
			d := kill(in)
			if cseOps[in.Op] && d >= 0 && f.Kinds[d] != bc.KRef && opInfos[in.Op].block < 0 {
				var k key
				k.op = in.Op
				copy(k.args[:], in.Args[1:])
				if e, ok := seen[k]; ok && int(e) != d && f.Kinds[e] == f.Kinds[d] {
					*in = Instr{Op: opMOV, Args: [5]int32{int32(d), e}, Line: in.Line}
					o.changed = true
				}
			}
			// What it writes invalidates what read or held it.
			o.writes(in, func(w int) {
				for k, e := range seen {
					if int(e) == w {
						delete(seen, k)
						continue
					}
					n := len(bc.Ops[k.op].Operands) - 1
					for j := 0; j < n; j++ {
						if bc.Ops[k.op].Operands[j+1] == bc.Reg && int(k.args[j]) == w {
							delete(seen, k)
							break
						}
					}
				}
			})
			if in.Op != opMOV && cseOps[in.Op] && d >= 0 && f.Kinds[d] != bc.KRef && !o.readsReg(in, d) {
				var k key
				k.op = in.Op
				copy(k.args[:], in.Args[1:])
				seen[k] = int32(d)
			}
		}
	}
}
