package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Paths (rfcs/0013, lang/LOWERING.md's "Reactivity"). A read of state
// is as narrow as what the code reads: an expression that's a path
// below a state (check.StatePath: fields, indexes, keys, members, a
// length) is read at once with CPATH or CLEN, which follow that path
// while a computation tracks reads, rather than CGET and the GETF/IDX/
// MGET chain after it, which follows the whole state. A write into part
// of a state puts it back with CPUTP and the place's path, its last
// step what the change was: the old value compared (o), what the
// builtin recorded (r), or anything below (a).
//
// A path is spelled as bytes (core/src/vm/path.h): a letter per step,
// a field's index after a field's, a kind after a key's; the indexes,
// keys and members come from consecutive registers, in order.

const (
	psField    = 'f'
	psOptField = 'F'
	psIndex    = 'i'
	psGet      = 'g'
	psKey      = 'k'
	psHas      = 'h'
	psValue    = 'v'
	psMember   = 'm'
	psChanged  = 'r'
	psAll      = 'a'
	psOld      = 'o'
)

// pathRead reads e when it's a path below a state, into dst; ok false
// when it isn't one.
func (b *fb) pathRead(e syntax.Expr, dst int) (int, bool) {
	p := b.l.prog.StatePathOf(e)
	if p == nil || p.Root != check.RootState {
		return 0, false
	}
	steps := p.Steps
	last := steps[len(steps)-1]
	length := last.Kind == check.StepLen
	if length {
		steps = steps[:len(steps)-1]
	}
	cell := b.pathCell(p)
	spelled, args, kinds := b.pathSteps(steps)
	blk := cell
	if len(kinds) > 0 {
		blk = b.regs(kinds)
		for i, a := range args {
			if a == nil {
				b.loadInt(blk+i, 0) // first(): the element at 0
				continue
			}
			b.value(a, blk+i)
		}
	}
	k := b.str(spelled)
	var r int
	switch {
	case length && last.Empty:
		n := b.temp(bc.KInt)
		b.emit(op("CLEN"), n, cell, k, blk)
		r = b.out(dst, bc.KBool)
		b.emit(op("EQI"), r, n, b.konst(0))
	case length:
		r = b.out(dst, bc.KInt)
		b.emit(op("CLEN"), r, cell, k, blk)
	default:
		r = b.out(dst, b.kind(b.rawType(e)))
		b.emit(op("CPATH"), r, cell, k, blk)
	}
	b.releaseRegs(blk, kinds)
	return r, true
}

// pathMapRead reads m[k], a path below a state whose map's values
// aren't optional, into r as it's stored (no optional made), jumping to
// miss when the map hasn't k: m[k] ?? d, if let v = m[k]. Both reads
// follow k's node. ok false when x isn't such a path.
func (b *fb) pathMapRead(x syntax.Expr, k bc.Kind, dst int, miss Label) (int, bool) {
	p := b.l.prog.StatePathOf(x)
	if p == nil || p.Root != check.RootState || p.Steps[len(p.Steps)-1].Kind != check.StepKey {
		return 0, false
	}
	m := p.Expr // what the key's step is of
	if len(p.Steps) > 1 {
		m = p.Steps[len(p.Steps)-2].Expr
	}
	if mt, ok := b.rawType(m).(*check.Map); !ok || check.IsOptional(b.typ(mt.Elem)) {
		return 0, false
	}
	cell := b.pathCell(p)
	spelled, args, kinds := b.pathSteps(p.Steps)
	blk := b.regs(kinds)
	for i, a := range args {
		b.value(a, blk+i)
	}
	has := b.temp(bc.KBool)
	spelled[len(spelled)-2] = psHas
	b.emit(op("CPATH"), has, cell, b.str(spelled), blk)
	r := b.out(dst, k)
	skip := b.f.NewLabel()
	b.emitJump(op("JT"), has, skip)
	for i, k := range kinds {
		if k == bc.KRef {
			b.emit(op("CLR"), blk+i) // the key, let go on this way out too
		}
	}
	b.jmp(miss)
	b.f.Place(skip)
	spelled[len(spelled)-2] = psValue
	b.emit(op("CPATH"), r, cell, b.str(spelled), blk)
	b.releaseRegs(blk, kinds)
	return r, true
}

// str is a path's spelling as a string constant.
func (b *fb) str(spelled []byte) int { return b.l.str(string(spelled)) }

// pathCell is the register holding a path's state's cell.
func (b *fb) pathCell(p *check.StatePath) int {
	switch {
	case p.Global != nil:
		return b.globalCell(p.Global)
	case p.Member != nil:
		if x, ok := p.Expr.(*syntax.MemberExpr); ok {
			return b.storeMemberRef(b.value(x.X, -1), p.Member)
		}
		return b.memberRef(p.Member, p.Expr)
	}
	b.l.errorf(p.Expr, "internal: a path's state")
	return b.valueReg(-1, bc.KRef)
}

// pathSteps spells steps, with their arguments' expressions (nil: an
// index of 0) and kinds.
func (b *fb) pathSteps(steps []check.PathStep) (spelled []byte, args []syntax.Expr, kinds []bc.Kind) {
	for _, s := range steps {
		switch s.Kind {
		case check.StepField, check.StepOptField:
			c := byte(psField)
			if s.Kind == check.StepOptField {
				c = psOptField
			}
			spelled = append(spelled, c, byte(s.Field.Index), byte(s.Field.Index>>8))
		case check.StepIndex:
			spelled = append(spelled, psIndex)
			args, kinds = append(args, s.Arg), append(kinds, bc.KInt)
		case check.StepGet:
			spelled = append(spelled, psGet)
			args, kinds = append(args, s.Arg), append(kinds, bc.KInt)
		case check.StepKey, check.StepHas, check.StepMember:
			c := byte(psKey)
			if s.Kind == check.StepHas {
				c = psHas
			} else if s.Kind == check.StepMember {
				c = psMember
			}
			k := b.kind(b.typeOf(s.Arg))
			spelled = append(spelled, c, byte(k))
			args, kinds = append(args, s.Arg), append(kinds, k)
		}
	}
	return spelled, args, kinds
}

// A putPath is how a change in place of a place below a state is put
// back (CPUTP): the place's steps and their arguments' registers, then
// what the change was.
type putPath struct {
	steps []byte
	args  []int
	keyed bool // the place's last step is a map's key
	exact bool // the place's path is the whole place's (not a prefix of it)
}

// placePath is the path of the place lv below its state; ok false when
// lv isn't in a state.
func (b *fb) placePath(lv *lvalue) (putPath, bool) {
	var chain []*lvalue
	for l := lv; l != nil; l = l.inner {
		chain = append(chain, l)
	}
	if chain[len(chain)-1].kind != lvCell {
		return putPath{}, false
	}
	p := putPath{exact: true}
	for i := len(chain) - 2; i >= 0; i-- {
		l := chain[i]
		switch l.kind {
		case lvField:
			if l.named != nil && l.named.Obj.IsError || len(p.steps) > 3*30 {
				p.exact = false
				return p, true
			}
			p.steps = append(p.steps, psField, byte(l.field.Index), byte(l.field.Index>>8))
			p.keyed = false
		case lvIndex:
			if l.isMap {
				p.steps = append(p.steps, psKey, byte(b.f.Kinds[l.idx]))
			} else {
				p.steps = append(p.steps, psIndex)
			}
			p.args = append(p.args, l.idx)
			p.keyed = l.isMap
		}
	}
	return p, true
}

// with is p with its last step: mode, and its registers (o's old value
// and, after a map's key, whether it was there).
func (p putPath) with(mode byte, regs ...int) putPath {
	if !p.exact {
		mode, regs = psAll, nil
	}
	q := putPath{steps: append(append([]byte(nil), p.steps...), mode), args: append(append([]int(nil), p.args...), regs...), keyed: p.keyed, exact: p.exact}
	return q
}

// A put is how updatePut puts a state back: as it went (ok), and when
// the change threw (anything may have changed; nil: CPUT).
type put struct {
	ok, err *putPath
}

// putFor is how a change of the place lv is put back: mode, with its
// registers, when lv is in a state; nil (CPUT) when it isn't.
func (b *fb) putFor(lv *lvalue, mode byte, regs ...int) *put {
	p, ok := b.placePath(lv)
	if !ok {
		return nil
	}
	okp, errp := p.with(mode, regs...), p.with(psAll)
	return &put{&okp, &errp}
}

// cputp puts cur back in the cell (moved), as p says.
func (b *fb) cputp(cell, cur int, p *putPath) {
	if p == nil {
		b.emit(op("CPUT"), cell, cur)
		return
	}
	ks := make([]bc.Kind, len(p.args))
	for i, a := range p.args {
		ks[i] = b.f.Kinds[a]
	}
	blk := cell
	if len(ks) > 0 {
		blk = b.regs(ks)
		for i, a := range p.args {
			b.move(blk+i, a)
		}
	}
	b.emit(op("CPUTP"), cell, cur, b.str(p.steps), blk)
	b.releaseRegs(blk, ks)
}
