package ir

import (
	"strings"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// out is where an expression's value goes: dst when it's given, else a
// temp of kind k.
func (b *fb) out(dst int, k bc.Kind) int {
	if dst >= 0 {
		return dst
	}
	return b.temp(k)
}

// value lowers e as it's used: with its implicit conversion. It returns
// the register holding the value (dst when given; else maybe a local's
// own register, which the caller must not change).
func (b *fb) value(e syntax.Expr, dst int) int {
	conv, ok := b.l.info.Convs[e]
	if !ok {
		return b.expr(e, dst)
	}
	to := b.typ(conv)
	from := b.rawType(e)
	inner := check.Unopt(to)
	if n, ok := inner.(*check.Named); ok && n.Obj.Kind == check.InterfaceType {
		b.l.implement(check.Unopt(from), n)
	}
	// What's converted when compiling: lengths and edges.
	if inner == check.Length && from != check.Length {
		r := b.temp(bc.KInt)
		if check.IsConstNumber(e) {
			b.loadInt(r, check.PackLength(1, check.ConstNumber(e)))
		} else {
			x := b.expr(e, -1)
			if b.f.Kinds[x] == bc.KInt {
				f := b.temp(bc.KFloat)
				b.emit(op("I2F"), f, x)
				x = f
			}
			b.callBTo("length.points", r, x)
		}
		return b.toOptional(r, check.Length, to, dst)
	}
	if b.isEdges(inner) && !b.isEdges(from) {
		r := b.edges(e)
		return b.toOptional(r, inner, to, dst)
	}
	var r int
	if check.IsOptional(to) && !check.IsOptional(from) && from != check.NoneType {
		r = b.expr(e, -1)
		return b.toOptional(r, from, to, dst)
	}
	r = b.expr(e, dst)
	return r
}

// toOptional makes r (a from) a to, an optional when it's one.
func (b *fb) toOptional(r int, from, to check.Type, dst int) int {
	if !check.IsOptional(to) {
		if dst >= 0 && dst != r {
			b.move(dst, r)
			return dst
		}
		return r
	}
	k := b.l.kind(from)
	if k == bc.KRef {
		if dst >= 0 && dst != r {
			b.move(dst, r)
			return dst
		}
		return r
	}
	o := b.out(dst, bc.KRef)
	b.emit(op("BOX"), o, r, int(k))
	return o
}

func (b *fb) isEdges(t check.Type) bool {
	n, ok := t.(*check.Named)
	return ok && n.Obj.Pkg != nil && n.Obj.Pkg.Prelude && n.Obj.Name() == "Edges"
}

// edges makes Edges from a number or [v, h] / [t, r, b, l].
func (b *fb) edges(e syntax.Expr) int {
	var t, r, bo, l int
	num := func(x syntax.Expr) int {
		v := b.expr(x, -1)
		if b.f.Kinds[v] == bc.KInt {
			f := b.temp(bc.KFloat)
			b.emit(op("I2F"), f, v)
			return f
		}
		return v
	}
	if a, ok := e.(*syntax.ArrayExpr); ok {
		if len(a.Elems) == 2 {
			t, r = num(a.Elems[0]), num(a.Elems[1])
			bo, l = t, r
		} else {
			t, r, bo, l = num(a.Elems[0]), num(a.Elems[1]), num(a.Elems[2]), num(a.Elems[3])
		}
	} else {
		t = num(e)
		r, bo, l = t, t, t
	}
	edges := b.l.prog.Prelude.Scope.LookupLocal("Edges").(*check.TypeName)
	shape := b.l.shape(edges.Type)
	ks := repeatKind(bc.KFloat, 6)
	blk := b.regs(ks)
	for i, v := range []int{t, r, bo, l} {
		b.move(blk+i, v)
	}
	z := b.temp(bc.KFloat)
	b.emit(op("KFLOAT"), z, b.l.floatConst(0))
	b.move(blk+4, z)
	b.move(blk+5, z)
	o := b.temp(bc.KRef)
	b.emit(op("NEWS"), o, shape, blk)
	b.releaseRegs(blk, ks)
	return o
}

// expr lowers e, its own type (no conversion).
func (b *fb) expr(e syntax.Expr, dst int) int {
	b.at(e)
	switch e := e.(type) {
	case *syntax.ParenExpr:
		return b.value(e.X, dst)
	case *syntax.Ident:
		return b.ident(e, dst)
	case *syntax.BasicLit:
		return b.basicLit(e, dst)
	case *syntax.StringLit:
		return b.stringLit(e, dst)
	case *syntax.BoolLit:
		r := b.out(dst, bc.KBool)
		v := 0
		if e.Value {
			v = 1
		}
		b.emit(op("KSMALL"), r, v)
		return r
	case *syntax.NoneLit:
		r := b.out(dst, bc.KRef)
		b.emit(op("KNONE"), r)
		return r
	case *syntax.SelfExpr:
		if _, ok := b.l.info.StoreSelf[e]; ok {
			return b.copyTo(b.env(e), dst)
		}
		return b.copyTo(b.readVar(b.ctx.Self, e), dst)
	case *syntax.ImplicitMember:
		return b.selection(e, nil, dst)
	case *syntax.MemberExpr:
		return b.selection(e, e.X, dst)
	case *syntax.CallExpr:
		return b.call(e, dst)
	case *syntax.IndexExpr:
		x := b.value(e.X, -1)
		i := b.value(e.Index, -1)
		if _, ok := b.rawType(e.X).(*check.Map); ok {
			r := b.out(dst, bc.KRef)
			b.emit(op("MGET"), r, x, i)
			return r
		}
		r := b.out(dst, b.kind(b.l.info.Types[e]))
		b.emit(op("IDX"), r, x, i)
		return r
	case *syntax.UnwrapExpr:
		x := b.value(e.X, -1)
		return b.unwrap(x, b.rawType(e), dst, e)
	case *syntax.UnaryExpr:
		return b.unary(e, dst)
	case *syntax.SpawnExpr:
		return b.spawn(e, dst)
	case *syntax.BinaryExpr:
		return b.binary(e, dst)
	case *syntax.LambdaExpr:
		return b.copyTo(b.lambda(e), dst)
	case *syntax.TupleExpr:
		if r, ok := b.hoist(e, dst); ok {
			return r
		}
		t := b.rawType(e).(*check.Tuple)
		var vals []int
		for _, x := range e.Elems {
			vals = append(vals, b.value(x, -1))
		}
		return b.newStruct(b.l.shape(t), t.Elems, vals, dst)
	case *syntax.ArrayExpr:
		if r, ok := b.hoist(e, dst); ok {
			return r
		}
		at, _ := b.rawType(e).(*check.Array)
		if at == nil {
			return b.valueReg(-1, bc.KRef)
		}
		k := b.l.kind(at.Elem)
		ks := repeatKind(k, len(e.Elems))
		var vals []int
		for _, x := range e.Elems {
			vals = append(vals, b.value(x, -1))
		}
		blk := b.regs(ks)
		for i, v := range vals {
			b.move(blk+i, v)
		}
		r := b.out(dst, bc.KRef)
		sh := b.l.shape(check.Unopt(at.Elem))
		if sh < 0 {
			sh = bc.None
		}
		b.emit(op("NEWA"), r, int(k), sh, blk, len(vals))
		b.releaseRegs(blk, ks)
		return r
	case *syntax.MapExpr:
		if r, ok := b.hoist(e, dst); ok {
			return r
		}
		mt, _ := b.rawType(e).(*check.Map)
		if mt == nil {
			return b.valueReg(-1, bc.KRef)
		}
		kk, vk := b.l.kind(mt.Key), b.l.kind(mt.Elem)
		var ks []bc.Kind
		var vals []int
		for _, en := range e.Entries {
			ks = append(ks, kk, vk)
			vals = append(vals, b.value(en.Key, -1), b.value(en.Value, -1))
		}
		blk := b.regs(ks)
		for i, v := range vals {
			b.move(blk+i, v)
		}
		r := b.out(dst, bc.KRef)
		b.emit(op("NEWM"), r, int(kk), int(vk), blk, len(e.Entries))
		b.releaseRegs(blk, ks)
		return r
	case *syntax.IfExpr:
		return b.ifExpr(e, dst)
	case *syntax.MatchExpr:
		return b.matchExpr(e, dst)
	case *syntax.JumpExpr:
		return b.jumpExpr(e, dst)
	case *syntax.CatchExpr:
		return b.catchExpr(e, dst)
	}
	b.l.errorf(e, "can't lower this expression yet")
	return b.valueReg(-1, b.kind(b.l.info.Types[e]))
}

// copyTo copies r into dst when dst is given.
func (b *fb) copyTo(r, dst int) int {
	if dst >= 0 && r != dst {
		b.move(dst, r)
		return dst
	}
	return r
}

// newStruct makes a struct (or a tuple) of shape from vals.
func (b *fb) newStruct(shape int, types []check.Type, vals []int, dst int) int {
	ks := make([]bc.Kind, len(vals))
	for i, t := range types {
		ks[i] = b.l.kind(t)
	}
	blk := b.regs(ks)
	for i, v := range vals {
		b.move(blk+i, b.valueReg(v, ks[i]))
	}
	r := b.out(dst, bc.KRef)
	b.emit(op("NEWS"), r, shape, blk)
	b.releaseRegs(blk, ks)
	return r
}

// unwrap is the value of the optional x (t is the value's type); none
// throws.
func (b *fb) unwrap(x int, t check.Type, dst int, n syntax.Node) int {
	ok := b.f.NewLabel()
	b.emitJump(op("JSOME"), x, ok)
	b.throwMsg("unwrapped none (" + b.where(n) + ")")
	b.f.Place(ok)
	return b.unbox(x, t, dst)
}

// unbox is the value in the optional x, known not none.
func (b *fb) unbox(x int, t check.Type, dst int) int {
	k := b.l.kind(t)
	if k == bc.KRef {
		return b.copyTo(x, dst)
	}
	r := b.out(dst, k)
	b.emit(op("UNBOX"), r, x)
	return r
}

func (b *fb) where(n syntax.Node) string {
	p := b.l.fset.Position(n.Range().Start)
	name := p.Filename
	if i := strings.LastIndexByte(name, '/'); i >= 0 {
		name = name[i+1:]
	}
	return name + ":" + itoa(p.Line)
}

// throwMsg throws a runtime error with a message.
func (b *fb) throwMsg(msg string) {
	m := b.kstr(msg)
	e := b.callB("error", bc.KRef, m)
	b.emit(op("THROW"), e)
}

// ---------------------------------------------------------------------
// Names

func (b *fb) ident(e *syntax.Ident, dst int) int {
	switch o := b.l.info.Uses[e].(type) {
	case *check.Var:
		if o.Fn != nil {
			return b.copyTo(b.localFnValue(o.Fn, e), dst)
		}
		return b.copyTo(b.readVar(o, e), dst)
	case *check.Member:
		return b.copyTo(b.readMember(o, e), dst)
	case *check.Global:
		return b.readGlobal(o, dst)
	case *check.Function:
		return b.copyTo(b.funcValue(o), dst)
	}
	if sm := b.selfField(e); sm >= 0 {
		return b.copyTo(sm, dst)
	}
	b.l.errorf(e, "internal: can't lower %s", e.Name)
	return b.valueReg(-1, bc.KRef)
}

// selfField reads a field of self named by its bare name in a method.
func (b *fb) selfField(e *syntax.Ident) int {
	if b.ctx == nil || b.ctx.Self == nil {
		return -1
	}
	n, ok := b.typ(b.ctx.Self.Type).(*check.Named)
	if !ok || n.Obj.Kind != check.StructType {
		return -1
	}
	f := n.Obj.Field(e.Name)
	if f == nil {
		return -1
	}
	self := b.readVar(b.ctx.Self, e)
	return b.getField(self, n, f, -1)
}

// getField reads struct value x's field f.
func (b *fb) getField(x int, t *check.Named, f *check.Field, dst int) int {
	k := b.l.kind(t.Subst().Apply(f.Type))
	r := b.out(dst, k)
	if t.Obj.IsError {
		b.emit(op("GETP"), r, x, f.Index)
	} else {
		b.emit(op("GETF"), r, x, f.Index)
	}
	return r
}

// funcValue is a declared function as a closure.
func (b *fb) funcValue(f *check.Function) int {
	idx := b.l.funcIndex(f, nil, nil)
	r := b.temp(bc.KRef)
	b.emit(op("CLOS"), r, idx, 0, 0)
	return r
}

func (b *fb) readGlobal(g *check.Global, dst int) int {
	slot := b.l.global(g)
	k := b.l.kind(g.Type)
	switch g.Kind {
	case check.StateGlobal:
		cell := b.temp(bc.KRef)
		b.emit(op("GGET"), cell, slot, int(bc.KRef))
		r := b.out(dst, k)
		b.emit(op("CGET"), r, cell)
		return r
	case check.MemoGlobal:
		m := b.temp(bc.KRef)
		b.emit(op("GGET"), m, slot, int(bc.KRef))
		r := b.out(dst, k)
		b.emit(op("MREAD"), r, m)
		return r
	}
	r := b.out(dst, k)
	b.emit(op("GGET"), r, slot, int(k))
	return r
}

// globalCell is a state global's cell (or a memo global's memo).
func (b *fb) globalCell(g *check.Global) int {
	r := b.temp(bc.KRef)
	b.emit(op("GGET"), r, b.l.global(g), int(bc.KRef))
	return r
}

// ---------------------------------------------------------------------
// Literals

func (b *fb) basicLit(e *syntax.BasicLit, dst int) int {
	t := b.l.info.Types[e]
	switch e.Kind {
	case syntax.INT:
		v, _ := check.ParseInt(e.Value)
		if t == check.Float {
			r := b.out(dst, bc.KFloat)
			b.emit(op("KFLOAT"), r, b.l.floatConst(float64(v)))
			return r
		}
		r := b.out(dst, bc.KInt)
		b.loadInt(r, v)
		return r
	case syntax.FLOAT:
		v, _ := check.ParseFloat(e.Value)
		r := b.out(dst, bc.KFloat)
		b.emit(op("KFLOAT"), r, b.l.floatConst(v))
		return r
	case syntax.DURATION:
		v, _ := check.ParseDuration(e.Value)
		r := b.out(dst, bc.KInt)
		b.loadInt(r, v)
		return r
	case syntax.COLOR:
		v, _ := check.ParseColor(e.Value)
		r := b.out(dst, bc.KInt)
		b.loadInt(r, v)
		return r
	case syntax.LENGTH:
		v, _ := check.ParseLength(e.Value)
		r := b.out(dst, bc.KInt)
		b.loadInt(r, v)
		return r
	}
	return b.valueReg(-1, bc.KInt)
}

// literalText is a string literal's text, escapes resolved.
func literalText(seg string, raw, multiline bool) string {
	s, _ := check.Unescape(seg, raw)
	return s
}

func (b *fb) stringLit(e *syntax.StringLit, dst int) int {
	segs := e.Segments
	if e.Multiline {
		// Dedent the text as one, keeping the interpolations' places.
		const mark = "\x00\x01"
		joined := strings.Join(segs, mark)
		segs = strings.Split(check.Dedent(joined), mark)
	}
	if len(e.Interps) == 0 {
		r := b.out(dst, bc.KRef)
		b.emit(op("KSTR"), r, b.l.str(literalText(segs[0], e.Raw, e.Multiline)))
		return r
	}
	// INTERP over a block: text, value, text, ...
	var ks []bc.Kind
	var vals []int
	for i, x := range e.Interps {
		if s := literalText(segs[i], e.Raw, e.Multiline); s != "" {
			ks = append(ks, bc.KRef)
			vals = append(vals, b.kstr(s))
		}
		v, k := b.stringPart(x)
		ks = append(ks, k)
		vals = append(vals, v)
	}
	if s := literalText(segs[len(segs)-1], e.Raw, e.Multiline); s != "" {
		ks = append(ks, bc.KRef)
		vals = append(vals, b.kstr(s))
	}
	blk := b.regs(ks)
	for i, v := range vals {
		b.move(blk+i, v)
	}
	kinds := make([]byte, len(ks))
	for i, k := range ks {
		kinds[i] = byte(k)
	}
	r := b.out(dst, bc.KRef)
	b.emit(op("INTERP"), r, blk, len(ks), b.l.str(string(kinds)))
	b.releaseRegs(blk, ks)
	return r
}

// stringPart is an interpolated value: a scalar INTERP prints itself, or
// a string.
func (b *fb) stringPart(x syntax.Expr) (int, bc.Kind) {
	t := b.typeOf(x)
	v := b.value(x, -1)
	switch t {
	case check.Int, check.Float, check.Bool:
		return v, b.l.kind(t)
	}
	return b.describe(v, t), bc.KRef
}

// describe is the text of v, a t: what interpolation and print show.
func (b *fb) describe(v int, t check.Type) int {
	switch t {
	case check.String:
		return v
	case check.Int, check.Float, check.Bool, check.Color, check.Length:
		r := b.temp(bc.KRef)
		b.emit(op("TOSTR"), r, v, int(b.l.kind(t)))
		return r
	case check.Duration:
		// In milliseconds: 300ms.
		ms := b.temp(bc.KInt)
		k := b.konst(1_000_000)
		b.emit(op("DIVI"), ms, v, k)
		s := b.temp(bc.KRef)
		b.emit(op("TOSTR"), s, ms, int(bc.KInt))
		unit := b.kstr("ms")
		r := b.temp(bc.KRef)
		b.emit(op("CONCAT"), r, s, unit)
		return r
	}
	switch x := t.(type) {
	case *check.Optional:
		r := b.temp(bc.KRef)
		none, end := b.f.NewLabel(), b.f.NewLabel()
		b.emitJump(op("JNONE"), v, none)
		in := b.unbox(v, x.Elem, -1)
		b.move(r, b.describe(in, x.Elem))
		b.jmp(end)
		b.f.Place(none)
		b.emit(op("KSTR"), r, b.l.str("none"))
		b.f.Place(end)
		return r
	case *check.Named:
		if x.Obj.Kind == check.EnumType && !x.Obj.RefEnum() {
			var names []string
			for _, k := range x.Obj.Cases {
				names = append(names, k.Name)
			}
			g := b.l.frozenStrings(names, "cases."+b.l.typeKey(x))
			arr := b.temp(bc.KRef)
			b.emit(op("GGET"), arr, g, int(bc.KRef))
			r := b.temp(bc.KRef)
			b.emit(op("IDX"), r, arr, v)
			return r
		}
		if x.Obj.IsError || x.Obj.Kind == check.InterfaceType && b.isErrorIface(x) {
			return b.errorMessage(v, x)
		}
	}
	// The rest as JSON.
	d := b.l.desc(t)
	if strings.Contains(d, "-") {
		return b.kstr("<" + t.String() + ">")
	}
	ds := b.kstr(d)
	z := b.konst(0)
	return b.callB("json.encode", bc.KRef, v, ds, z)
}

func (b *fb) isErrorIface(n *check.Named) bool {
	return n.Obj.Name() == "Error" && n.Obj.Pkg == nil
}

// errorMessage is an error's message: a user error's message method, a
// runtime error's message field.
func (b *fb) errorMessage(e int, t *check.Named) int {
	r := b.temp(bc.KRef)
	if t.Obj.IsError {
		b.emit(op("CALLI"), r, b.l.str("message"), b.argBlock1(e), 1)
		b.l.shape(t)
		return r
	}
	c := b.temp(bc.KInt)
	b.emit(op("ECASE"), c, e)
	user, end := b.f.NewLabel(), b.f.NewLabel()
	b.emitJump(op("JT"), c, user)
	b.callBTo("error.message", r, e)
	b.jmp(end)
	b.f.Place(user)
	blk := b.regs([]bc.Kind{bc.KRef})
	b.move(blk, e)
	b.emit(op("CALLI"), r, b.l.str("message"), blk, 1)
	b.releaseRegs(blk, []bc.Kind{bc.KRef})
	b.f.Place(end)
	return r
}

// argBlock1 copies r into a block of one (for CALLI's object).
func (b *fb) argBlock1(r int) int {
	blk := b.temp(bc.KRef)
	b.move(blk, r)
	return blk
}

// ---------------------------------------------------------------------
// Members

func (b *fb) selection(e syntax.Expr, x syntax.Expr, dst int) int {
	sel := b.l.info.Selections[e]
	if sel == nil {
		b.l.errorf(e, "internal: no selection")
		return b.valueReg(-1, bc.KRef)
	}
	resT := b.rawType(e)
	switch sel.Kind {
	case check.SelCase:
		return b.caseValue(b.typ(sel.Recv), sel.Case, dst)
	case check.SelConst:
		return b.readGlobal(sel.Global, dst)
	case check.SelColor:
		r := b.out(dst, bc.KInt)
		b.loadInt(r, sel.Value)
		return r
	case check.SelLengthAuto:
		r := b.out(dst, bc.KInt)
		b.loadInt(r, 0)
		return r
	case check.SelPkg:
		switch o := sel.Obj.(type) {
		case *check.Global:
			return b.readGlobal(o, dst)
		case *check.Function:
			return b.copyTo(b.funcValue(o), dst)
		}
		b.l.errorf(e, "internal: package member")
		return b.valueReg(-1, bc.KRef)
	}
	// A member of a value.
	xv := b.value(x, -1)
	recv := b.typ(sel.Recv)
	if sel.Optional {
		// x?.name: none when x is.
		r := b.out(dst, bc.KRef)
		none, end := b.f.NewLabel(), b.f.NewLabel()
		b.emitJump(op("JNONE"), xv, none)
		in := b.unbox(xv, recv, -1)
		v := b.member(e, sel, in, recv, -1)
		b.toOptional(v, check.Unopt(resT), resT, r)
		b.jmp(end)
		b.f.Place(none)
		b.emit(op("KNONE"), r)
		b.f.Place(end)
		return r
	}
	return b.member(e, sel, xv, recv, dst)
}

// member reads a member of value x (of type recv).
func (b *fb) member(e syntax.Expr, sel *check.Selection, x int, recv check.Type, dst int) int {
	switch sel.Kind {
	case check.SelBuiltinField:
		return b.builtinField(sel, x, b.rawType(e), dst)
	case check.SelField:
		return b.getField(x, recv.(*check.Named), sel.Field, dst)
	case check.SelStoreMember:
		ref := b.storeMemberRef(x, sel.Member)
		m := sel.Member
		k := b.l.kind(memberTypeIn(recv, m))
		switch m.Kind {
		case check.StateMember:
			r := b.out(dst, k)
			b.emit(op("CGET"), r, ref)
			return r
		case check.LetMember:
			if m.Memo && !isLoad(m.Default) {
				r := b.out(dst, k)
				b.emit(op("MREAD"), r, ref)
				return r
			}
		case check.ParamMember:
			r := b.out(dst, k)
			b.emit(op("AGET"), r, ref)
			return r
		}
		return b.copyTo(ref, dst)
	case check.SelIfaceField:
		r := b.out(dst, b.kind(sel.Field.Type))
		blk := b.regs([]bc.Kind{bc.KRef})
		b.move(blk, x)
		b.emit(op("CALLI"), r, b.l.str(sel.Name), blk, 1)
		b.releaseRegs(blk, []bc.Kind{bc.KRef})
		return r
	case check.SelErrorMessage:
		n, _ := recv.(*check.Named)
		return b.copyTo(b.errorMessage(x, n), dst)
	case check.SelLoading:
		return b.copyTo(b.callB(uiLoadValue, bc.KRef, x), dst)
	}
	b.l.errorf(e, "internal: can't lower member")
	return b.valueReg(-1, bc.KRef)
}

// caseValue is a payloadless case of enum t.
func (b *fb) caseValue(t check.Type, k *check.Case, dst int) int {
	n, ok := t.(*check.Named)
	if !ok {
		return b.valueReg(-1, bc.KInt)
	}
	if !n.Obj.RefEnum() {
		r := b.out(dst, bc.KInt)
		b.loadInt(r, int64(k.Index))
		return r
	}
	r := b.out(dst, bc.KRef)
	b.emit(op("NEWE"), r, b.l.shape(n), k.Index+caseBase(n), 0)
	return r
}

// ---------------------------------------------------------------------
// Operators

func (b *fb) unary(e *syntax.UnaryExpr, dst int) int {
	switch e.Op {
	case syntax.NOT:
		x := b.value(e.X, -1)
		r := b.out(dst, bc.KBool)
		b.emit(op("NOT"), r, x)
		return r
	case syntax.SUB:
		if lit, ok := e.X.(*syntax.BasicLit); ok && lit.Kind == syntax.INT && b.rawType(e) == check.Int {
			v, _ := check.ParseInt(lit.Value)
			r := b.out(dst, bc.KInt)
			b.loadInt(r, -v)
			return r
		}
		x := b.value(e.X, -1)
		t := b.rawType(e)
		if t == check.Float {
			r := b.out(dst, bc.KFloat)
			b.emit(op("NEGF"), r, x)
			return r
		}
		r := b.out(dst, bc.KInt)
		b.emit(op("NEGI"), r, x)
		return r
	case syntax.AWAIT:
		xt := b.rawType(e.X)
		if tk, ok := xt.(*check.Task); ok {
			f := b.value(e.X, -1)
			k := b.l.kind(tk.Elem)
			r := bc.None
			if k != bc.KVoid {
				r = b.out(dst, k)
			} else {
				r = b.temp(bc.KRef)
			}
			b.emit(op("AWAIT"), r, f)
			if k == bc.KVoid {
				return -1
			}
			return r
		}
		return b.value(e.X, dst)
	case syntax.TRY:
		if e.Optional {
			return b.tryOptional(e, dst)
		}
		return b.value(e.X, dst)
	case syntax.LOAD:
		b.l.errorf(e, "load is a component's let")
		return b.valueReg(-1, bc.KRef)
	}
	return b.valueReg(-1, bc.KRef)
}

func (b *fb) binary(e *syntax.BinaryExpr, dst int) int {
	switch e.Op {
	case syntax.LAND, syntax.LOR:
		r := b.out(dst, bc.KBool)
		end := b.f.NewLabel()
		b.value(e.X, r)
		if e.Op == syntax.LAND {
			b.emitJump(op("JF"), r, end)
		} else {
			b.emitJump(op("JT"), r, end)
		}
		b.value(e.Y, r)
		b.f.Place(end)
		return r
	case syntax.COALESCE:
		res := b.rawType(e)
		x := b.value(e.X, -1)
		xt := b.rawType(e.X)
		if check.IsOptional(res) {
			r := b.out(dst, bc.KRef)
			end := b.f.NewLabel()
			b.move(r, x)
			b.emitJump(op("JSOME"), r, end)
			b.value(e.Y, r)
			b.f.Place(end)
			return r
		}
		k := b.l.kind(res)
		r := b.out(dst, k)
		other, end := b.f.NewLabel(), b.f.NewLabel()
		b.emitJump(op("JNONE"), x, other)
		b.unbox(x, check.Unopt(xt), r)
		b.jmp(end)
		b.f.Place(other)
		b.value(e.Y, r)
		b.f.Place(end)
		return r
	case syntax.RANGE, syntax.RANGELT:
		from := b.value(e.X, -1)
		to := b.value(e.Y, -1)
		if e.Op == syntax.RANGE {
			t := b.temp(bc.KInt)
			b.emit(op("ADDIK"), t, to, 1)
			to = t
		}
		return b.copyTo(b.callB("arr.range", bc.KRef, from, to), dst)
	}
	xt, yt := b.typeOf(e.X), b.typeOf(e.Y)
	x := b.value(e.X, -1)
	y := b.value(e.Y, -1)
	return b.binop(e.Op, x, y, xt, yt, b.rawType(e), dst)
}

// binop applies a binary operator to registers x and y.
func (b *fb) binop(o syntax.Token, x, y int, xt, yt, res check.Type, dst int) int {
	switch o {
	case syntax.EQL, syntax.NEQ:
		r := b.out(dst, bc.KBool)
		if xt == check.NoneType || yt == check.NoneType {
			v := x
			if xt == check.NoneType {
				v = y
			}
			b.emit(op("ISNONE"), r, v)
			if o == syntax.NEQ {
				b.emit(op("NOT"), r, r)
			}
			return r
		}
		k := b.f.Kinds[x]
		switch k {
		case bc.KInt, bc.KBool:
			b.emit(op(map[bool]string{true: "EQI", false: "NEI"}[o == syntax.EQL]), r, x, y)
		case bc.KFloat:
			b.emit(op(map[bool]string{true: "EQF", false: "NEF"}[o == syntax.EQL]), r, x, y)
		default:
			if b.f.Kinds[y] != bc.KRef {
				// T? == T: box the T.
				y = b.toOptional(y, yt, check.MakeOptional(yt), -1)
			}
			b.emit(op(map[bool]string{true: "EQ", false: "NE"}[o == syntax.EQL]), r, x, y, int(bc.KRef))
		}
		return r
	case syntax.LSS, syntax.LEQ, syntax.GTR, syntax.GEQ:
		r := b.out(dst, bc.KBool)
		if o == syntax.GTR || o == syntax.GEQ {
			x, y = y, x
			if o == syntax.GTR {
				o = syntax.LSS
			} else {
				o = syntax.LEQ
			}
		}
		switch b.f.Kinds[x] {
		case bc.KFloat:
			b.emit(op(map[bool]string{true: "LTF", false: "LEF"}[o == syntax.LSS]), r, x, y)
		case bc.KRef:
			if o == syntax.LSS {
				b.emit(op("LTS"), r, x, y)
			} else {
				b.emit(op("LTS"), r, y, x)
				b.emit(op("NOT"), r, r)
			}
		default:
			b.emit(op(map[bool]string{true: "LTI", false: "LEI"}[o == syntax.LSS]), r, x, y)
		}
		return r
	}
	k := b.l.kind(res)
	switch {
	case res == check.String:
		r := b.out(dst, bc.KRef)
		b.emit(op("CONCAT"), r, x, y)
		return r
	case k == bc.KRef:
		return b.copyTo(b.callB("arr.concat", bc.KRef, x, y), dst)
	case k == bc.KFloat:
		r := b.out(dst, bc.KFloat)
		b.emit(op(map[syntax.Token]string{syntax.ADD: "ADDF", syntax.SUB: "SUBF", syntax.MUL: "MULF", syntax.QUO: "DIVF", syntax.REM: "MODF"}[o]), r, x, y)
		return r
	}
	r := b.out(dst, bc.KInt)
	b.emit(op(map[syntax.Token]string{syntax.ADD: "ADDI", syntax.SUB: "SUBI", syntax.MUL: "MULI", syntax.QUO: "DIVI", syntax.REM: "MODI"}[o]), r, x, y)
	return r
}

// ---------------------------------------------------------------------
// if and match as values

func (b *fb) valueBlock(bl *syntax.Block, dst int) {
	b.openScope()
	n := len(bl.Items)
	if n > 0 {
		b.stmts(bl.Items[:n-1])
		if es, ok := bl.Items[n-1].(*syntax.ExprStmt); ok {
			b.value(es.X, dst)
		} else {
			b.stmt(bl.Items[n-1])
		}
	}
	b.closeScope()
}

func (b *fb) ifExpr(e *syntax.IfExpr, dst int) int {
	r := b.out(dst, b.kind(b.l.info.Types[e]))
	b.openScope()
	els, end := b.f.NewLabel(), b.f.NewLabel()
	b.condJump(e.Cond, els)
	b.valueBlock(e.Then, r)
	b.closeScope()
	b.jmp(end)
	b.f.Place(els)
	switch el := e.Else.(type) {
	case *syntax.IfExpr:
		b.value(el, r)
	case *syntax.Block:
		b.valueBlock(el, r)
	}
	b.f.Place(end)
	return r
}

func (b *fb) matchExpr(e *syntax.MatchExpr, dst int) int {
	r := b.out(dst, b.kind(b.l.info.Types[e]))
	subj := b.value(e.Subject, -1)
	st := b.rawType(e.Subject)
	end := b.f.NewLabel()
	for _, a := range e.Arms {
		next := b.f.NewLabel()
		b.openScope()
		b.testPattern(a.Pattern, subj, st, next)
		b.bindPattern(a.Pattern, subj, st)
		if a.Guard != nil {
			g := b.value(a.Guard, -1)
			b.emitJump(op("JF"), g, next)
		}
		switch body := a.Body.(type) {
		case *syntax.Block:
			b.valueBlock(body, r)
		case syntax.Expr:
			b.value(body, r)
		}
		b.closeScope()
		b.jmp(end)
		b.f.Place(next)
	}
	b.throwMsg("no arm of the match matched (" + b.where(e) + ")")
	b.f.Place(end)
	return r
}
