package ir

import (
	"fmt"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/prelude"
	"github.com/egoist/asuka/lang/syntax"
)

// ---------------------------------------------------------------------
// Functions and methods

// lowerFunc lowers a declared function or method (recv: the receiver's
// type, for a struct's or an enum's).
func (b *fb) lowerFunc(f *check.Function, recv check.Type) {
	ctx := f.Ctx
	if ctx == nil {
		b.emit(op("RET0"))
		return
	}
	b.ctx = ctx
	b.at(f.Decl)
	if f.Recv != nil {
		switch f.Recv.Kind {
		case check.StructType, check.EnumType:
			r := b.newReg(b.l.kind(recv))
			if ctx.Self != nil {
				b.vars[ctx.Self] = r
			}
		default:
			b.envReg = b.newReg(bc.KRef)
			b.comp = f.Recv
		}
	}
	var params []int
	for _, p := range f.Sig.Params {
		params = append(params, b.newReg(b.kind(p.Type)))
	}
	b.openScope()
	for i, p := range f.Sig.Params {
		if p.Var == nil {
			continue
		}
		if p.Var.Boxed() {
			b.declare(p.Var, params[i])
		} else {
			b.vars[p.Var] = params[i]
		}
	}
	if f.Sig.Async || ctx.Awaits {
		b.f.Flags |= bc.FAsync
	}
	if f.Sig.Throws {
		b.f.Flags |= bc.FThrows
	}
	d := f.Decl
	if d.Value != nil {
		k := b.f.Ret
		if k == bc.KVoid {
			b.exprStmt(d.Value)
			b.emit(op("RET0"))
		} else {
			mark := b.mark()
			r := b.value(d.Value, -1)
			b.ret(r, k)
			b.release(mark)
		}
	} else {
		b.stmts(d.Body.Items)
		b.ret(-1, b.f.Ret)
	}
	b.closeScope()
}

// ---------------------------------------------------------------------
// Components and stores

// compFunc is the function of a component (or a store): its arguments
// are its header parameters, then its props, then its events' handlers.
func (l *Lowerer) compFunc(t *check.TypeName) int {
	key := fmt.Sprintf("comp|%p", t)
	if i, ok := l.funcs[key]; ok {
		return i
	}
	c := t.Comp
	n := len(c.Params) + len(c.Props) + len(c.Events)
	b := l.newFunc(t.Name(), l.fileOf(t.Decl), n, bc.KRef)
	l.funcs[key] = b.idx
	l.work = append(l.work, func() { b.lowerComponent(t) })
	return b.idx
}

func (b *fb) lowerComponent(t *check.TypeName) {
	comp := t.Comp
	ctx := comp.Ctx
	b.ctx = ctx
	b.comp = t
	b.at(t.Decl)
	for _, list := range [][]*check.Member{comp.Params, comp.Props, comp.Events} {
		for _, m := range list {
			b.memberRegs[m] = b.newReg(bc.KRef)
		}
	}
	b.openScope()
	// The env's box: closures made before the env is whole capture it.
	none := b.temp(bc.KRef)
	b.emit(op("KNONE"), none)
	b.envBox = b.local(bc.KRef)
	b.emit(op("VBOX"), b.envBox, none, int(bc.KRef))
	for _, m := range comp.Order {
		mark := b.mark()
		b.at(m.Decl)
		switch m.Kind {
		case check.ParamMember, check.PropMember:
			if m.Default == nil {
				break
			}
			r := b.memberRegs[m]
			skip := b.f.NewLabel()
			b.emitJump(op("JSOME"), r, skip)
			if m.IsSlot() {
				b.move(r, b.builder(m.Default))
			} else {
				b.move(r, b.accessor(m.Default, m.Type))
			}
			b.f.Place(skip)
		case check.StateMember:
			k := b.kind(m.Type)
			var init int
			if m.Default != nil {
				init = b.value(m.Default, -1)
			}
			init = b.valueReg(init, k)
			r := b.local(bc.KRef)
			b.emit(op("CELLK"), r, init, int(k), b.l.str(t.Name()+"."+m.Name()))
			b.memberRegs[m] = r
		case check.LetMember:
			switch {
			case isLoad(m.Default):
				r := b.local(bc.KRef)
				b.move(r, b.load(m))
				b.memberRegs[m] = r
			case m.Memo:
				clos := b.thunk(b.l.info.Ctxs[m.Default], m.Default, m.Type)
				r := b.local(bc.KRef)
				b.emit(op("MEMO"), r, clos, int(b.kind(m.Type)))
				b.memberRegs[m] = r
			default:
				r := b.local(b.kind(m.Type))
				b.value(m.Default, r)
				b.memberRegs[m] = r
			}
		case check.UseMember:
			r := b.local(bc.KRef)
			key := b.kstr(b.l.typeKey(b.typ(m.Type)))
			b.callBTo(uiUse, r, key)
			b.memberRegs[m] = r
		}
		b.release(mark)
	}
	// The env, whole.
	shape := b.l.envShapeOf(t)
	var ks []bc.Kind
	var regs []int
	for _, m := range comp.Order {
		if m.Kind == check.FnMember {
			continue
		}
		ks = append(ks, b.l.memberKind(m))
		regs = append(regs, b.memberRegs[m])
	}
	blk := b.regs(ks)
	for i, r := range regs {
		b.move(blk+i, r)
	}
	env := b.local(bc.KRef)
	b.emit(op("NEWS"), env, shape, blk)
	b.releaseRegs(blk, ks)
	b.emit(op("VSET"), b.envBox, env)
	b.envReg = env
	// What it provides, its effects and tasks.
	for _, it := range comp.Body.Items {
		mark := b.mark()
		b.at(it)
		switch d := it.(type) {
		case *syntax.ProvideDecl:
			v := b.value(d.Value, -1)
			key := b.kstr(b.l.typeKey(b.rawType(d.Value)))
			b.callBTo(uiProvide, bc.None, key, v)
		case *syntax.EffectDecl:
			ectx := b.l.info.Ctxs[d]
			clos := b.makeClosure(closure{name: "effect", ctxs: []*check.FuncCtx{ectx}, ret: bc.KVoid, throws: true, body: func(c *fb) {
				c.ctx = ectx
				c.block(d.Body)
				c.emit(op("RET0"))
			}})
			b.emit(op("EFFECT"), clos)
		case *syntax.TaskDecl:
			b.taskDecl(d, comp.Store)
		}
		b.release(mark)
	}
	if comp.Store {
		b.emit(op("RET"), env)
		b.closeScopeNoClear()
		return
	}
	var views []syntax.Item
	for _, it := range comp.Body.Items {
		switch it.(type) {
		case *syntax.PropDecl, *syntax.EventDecl, *syntax.StateDecl, *syntax.LetDecl, *syntax.UseDecl,
			*syntax.FuncDecl, *syntax.ProvideDecl, *syntax.TaskDecl, *syntax.EffectDecl, *syntax.BadItem:
		default:
			views = append(views, it)
		}
	}
	v := b.viewsOf(views)
	r := b.newReg(bc.KRef)
	b.move(r, v)
	b.emit(op("RET"), r)
	b.closeScopeNoClear()
}

// closeScopeNoClear ends a scope whose code returned already.
func (b *fb) closeScopeNoClear() {
	n := len(b.scopes)
	for _, r := range b.scopes[n-1] {
		k := b.f.Kinds[r]
		b.free[k] = append(b.free[k], r)
	}
	b.scopes = b.scopes[:n-1]
}

// taskDecl starts a task: once (a component's starts when it mounts),
// or, with dependencies, an effect that restarts it when they change.
func (b *fb) taskDecl(d *syntax.TaskDecl, store bool) {
	bodyCtx := b.l.info.Ctxs[d.Body]
	body := func(c *fb) int {
		return c.makeClosure(closure{name: "task", ctxs: []*check.FuncCtx{bodyCtx}, ret: bc.KVoid, throws: true, body: func(c2 *fb) {
			c2.ctx = bodyCtx
			c2.block(d.Body)
			c2.emit(op("RET0"))
			if bodyCtx.Awaits {
				c2.f.Flags |= bc.FAsync
			}
		}})
	}
	if len(d.Deps) == 0 {
		clos := body(b)
		if store {
			f := b.temp(bc.KRef)
			b.emit(op("TASK"), f, clos)
		} else {
			b.callBTo(uiTask, bc.None, clos)
		}
		return
	}
	depsCtx := b.l.info.Ctxs[d]
	clos := b.makeClosure(closure{name: "task deps", ctxs: []*check.FuncCtx{depsCtx, bodyCtx}, ret: bc.KVoid, throws: true, body: func(c *fb) {
		c.ctx = depsCtx
		for _, x := range d.Deps {
			c.value(x, -1)
		}
		t := body(c)
		f := c.temp(bc.KRef)
		c.emit(op("TASK"), f, t)
		c.emit(op("RET0"))
	}})
	b.emit(op("EFFECT"), clos)
}

// load makes a component's let x = load f(…): the runtime's loader of a
// thunk that starts the call as a task.
func (b *fb) load(m *check.Member) int {
	u := m.Default.(*syntax.UnaryExpr)
	ctx := b.l.info.Ctxs[m.Default]
	inner := b.typ(b.l.info.Types[u.X])
	thunk := b.makeClosure(closure{name: "load", ctxs: []*check.FuncCtx{ctx}, ret: bc.KRef, throws: true, body: func(c *fb) {
		c.ctx = ctx
		f := c.taskOf(u.X, -1, inner, false)
		c.emit(op("RET"), f)
	}})
	return b.callB(uiLoad, bc.KRef, thunk)
}

// accessor passes e to a component or store: a value when it reads
// nothing reactive; the cell, memo or accessor it reads alone; or a
// thunk.
func (b *fb) accessor(e syntax.Expr, t check.Type) int {
	k := b.kind(t)
	r := b.temp(bc.KRef)
	if !b.l.prog.Reactive(e) {
		v := b.value(e, -1)
		b.emit(op("ACCV"), r, b.valueReg(v, k), int(k))
		return r
	}
	if ref := b.readsAlone(e); ref >= 0 {
		b.emit(op("ACCC"), r, ref)
		return r
	}
	ctx := b.l.info.Ctxs[e]
	clos := b.thunk(ctx, e, t)
	b.emit(op("ACCF"), r, clos, int(k))
	return r
}

// readsAlone is the cell, memo or accessor e reads when it's that read
// alone (count, a prop, a row's item), or -1.
func (b *fb) readsAlone(e syntax.Expr) int {
	if _, conv := b.l.info.Convs[e]; conv {
		return -1
	}
	id, ok := unparen(e).(*syntax.Ident)
	if !ok {
		return -1
	}
	switch o := b.l.info.Uses[id].(type) {
	case *check.Member:
		switch o.Kind {
		case check.StateMember, check.ParamMember, check.PropMember:
			if !o.IsSlot() {
				return b.memberRef(o, e)
			}
		case check.LetMember:
			if o.Memo && !isLoad(o.Default) {
				return b.memberRef(o, e)
			}
		}
	case *check.Global:
		if o.Kind == check.StateGlobal || o.Kind == check.MemoGlobal {
			return b.globalCell(o)
		}
	case *check.Var:
		if (o.Kind == check.RowItem || o.Kind == check.ViewMemo) && !o.Boxed() {
			if r, ok := b.vars[o]; ok {
				return r
			}
		}
	}
	return -1
}

func (b *fb) storeCall(e *syntax.CallExpr, call *check.Call, dst int) int {
	t := call.Type.Obj
	idx := b.l.compFunc(t)
	var vals []int
	for i, a := range call.Args {
		if a == nil {
			r := b.temp(bc.KRef)
			b.emit(op("KNONE"), r)
			vals = append(vals, r)
			continue
		}
		vals = append(vals, b.accessor(a, call.Params[i].Type))
	}
	return b.invoke(idx, vals, bc.KRef, dst, false)
}

// ---------------------------------------------------------------------
// The app

func (b *fb) lowerApp(d *syntax.AppDecl) {
	b.at(d)
	b.openScope()
	v := b.viewsOf(d.Body.Items)
	r := b.newReg(bc.KRef)
	b.move(r, v)
	b.emit(op("RET"), r)
	b.closeScopeNoClear()
}

// ---------------------------------------------------------------------
// Views

// viewsOf builds view items: one view, or a fragment of them.
func (b *fb) viewsOf(items []syntax.Item) int {
	if len(items) == 1 {
		if v, opt := b.viewItem(items[0]); !opt {
			return v
		}
	}
	frag := b.callB(uiFragment, bc.KRef)
	for _, it := range items {
		v, opt := b.viewItem(it)
		if v < 0 {
			continue
		}
		skip := b.f.NewLabel()
		if opt {
			b.emitJump(op("JNONE"), v, skip)
		}
		b.callBTo(uiAdd, bc.None, frag, v)
		b.f.Place(skip)
	}
	return frag
}

// viewItem builds a view item; opt: the view may be none.
func (b *fb) viewItem(it syntax.Item) (int, bool) {
	b.at(it)
	switch s := it.(type) {
	case *syntax.ExprStmt:
		return b.viewExpr(s.X)
	case *syntax.IfStmt:
		return b.showView(s), false
	case *syntax.ForStmt:
		return b.forView(s), false
	case *syntax.MatchStmt:
		return b.matchView(s), false
	}
	b.l.errorf(it, "can't lower this view item")
	return -1, false
}

// viewExpr builds a view-typed expression: an element, a component, a
// slot (its builder called).
func (b *fb) viewExpr(x syntax.Expr) (int, bool) {
	t := b.rawType(x)
	if id, ok := unparen(x).(*syntax.Ident); ok {
		if m, ok := b.l.info.Uses[id].(*check.Member); ok && m.IsSlot() {
			bl := b.memberRef(m, x)
			r := b.temp(bc.KRef)
			if check.IsOptional(m.Type) {
				skip := b.f.NewLabel()
				b.emit(op("KNONE"), r)
				b.emitJump(op("JNONE"), bl, skip)
				b.emit(op("CALLC"), r, bl, 0, 0)
				b.f.Place(skip)
				return r, true
			}
			b.emit(op("CALLC"), r, bl, 0, 0)
			return r, false
		}
	}
	return b.value(x, -1), check.IsOptional(t)
}

// builder is a closure building the view x, where it's placed.
func (b *fb) builder(x syntax.Expr) int {
	ctx := b.l.info.Ctxs[x]
	return b.makeClosure(closure{name: "builder", ctxs: []*check.FuncCtx{ctx}, ret: bc.KRef, throws: true, body: func(c *fb) {
		if ctx != nil {
			c.ctx = ctx
		}
		v, opt := c.viewExpr(x)
		if opt {
			skip := c.f.NewLabel()
			c.emitJump(op("JSOME"), v, skip)
			c.move(v, c.callB(uiFragment, bc.KRef))
			c.f.Place(skip)
		}
		c.emit(op("RET"), v)
	}})
}

// builderItems is a closure building view items.
func (b *fb) builderItems(ctx *check.FuncCtx, items []syntax.Item, extra ...*check.FuncCtx) int {
	ctxs := append([]*check.FuncCtx{ctx}, extra...)
	return b.makeClosure(closure{name: "builder", ctxs: ctxs, ret: bc.KRef, throws: true, body: func(c *fb) {
		if ctx != nil {
			c.ctx = ctx
		}
		v := c.viewsOf(items)
		c.emit(op("RET"), v)
	}})
}

func (b *fb) showView(s *syntax.IfStmt) int {
	var cond, then int
	if lc, ok := s.Cond.(*syntax.LetCond); ok {
		condCtx := b.l.info.Ctxs[s.Cond]
		thenCtx := b.l.info.Ctxs[s.Then]
		cond = b.makeClosure(closure{name: "condition", ctxs: []*check.FuncCtx{condCtx}, ret: bc.KBool, throws: true, body: func(c *fb) {
			c.ctx = condCtx
			v := c.value(lc.Value, -1)
			r := c.temp(bc.KBool)
			c.emit(op("ISNONE"), r, v)
			c.emit(op("NOT"), r, r)
			c.emit(op("RET"), r)
		}})
		bp, _ := lc.Pattern.(*syntax.BindPat)
		var u *check.Var
		if bp != nil {
			u, _ = b.l.info.Defs[bp.Name].(*check.Var)
		}
		vt := check.Unopt(b.rawType(lc.Value))
		then = b.makeClosure(closure{name: "then", ctxs: []*check.FuncCtx{thenCtx, condCtx}, ret: bc.KRef, throws: true, body: func(c *fb) {
			c.ctx = thenCtx
			if u != nil {
				k := c.l.kind(vt)
				m := c.makeClosure(closure{name: "unwrap", ctxs: []*check.FuncCtx{condCtx}, ret: k, throws: true, body: func(c2 *fb) {
					c2.ctx = condCtx
					v := c2.value(lc.Value, -1)
					c2.ret(c2.unwrap(v, vt, -1, lc), k)
				}})
				r := c.newReg(bc.KRef)
				c.emit(op("MEMO"), r, m, int(k))
				c.vars[u] = r
			}
			v := c.viewsOf(s.Then.Items)
			c.emit(op("RET"), v)
		}})
	} else {
		cond = b.thunk(b.l.info.Ctxs[s.Cond], s.Cond, check.Bool)
		then = b.builderItems(b.l.info.Ctxs[s.Then], s.Then.Items)
	}
	var els int
	switch e := s.Else.(type) {
	case *syntax.Block:
		els = b.builderItems(b.l.info.Ctxs[e], e.Items)
	case *syntax.IfStmt:
		els = b.builderItems(b.l.info.Ctxs[e], []syntax.Item{e})
	default:
		els = b.temp(bc.KRef)
		b.emit(op("KNONE"), els)
	}
	return b.callB(uiShow, bc.KRef, cond, then, els)
}

func (b *fb) forView(s *syntax.ForStmt) int {
	fi := b.l.info.Fors[s]
	itemsCtx := b.l.info.Ctxs[s.Seq]
	seqT := b.typ(&check.Array{Elem: fi.Elem})
	items := b.makeClosure(closure{name: "items", ctxs: []*check.FuncCtx{itemsCtx}, ret: bc.KRef, throws: true, body: func(c *fb) {
		c.ctx = itemsCtx
		v := c.value(fi.Seq, -1)
		c.emit(op("RET"), v)
	}})
	_ = seqT
	var key int
	if s.Key != nil {
		kctx := b.l.info.Ctxs[s.Key]
		var item *check.Var
		if len(kctx.Params) > 0 {
			item = kctx.Params[0]
			if fi.Kind == check.ForEnumerated && len(kctx.Params) > 1 {
				item = kctx.Params[1]
			}
		}
		kt := kctx.Result
		kk := b.kind(kt)
		key = b.makeClosure(closure{name: "key", ctxs: []*check.FuncCtx{kctx}, params: []*check.Var{item}, pkinds: []bc.Kind{b.kind(fi.Elem)}, ret: kk, throws: true, body: func(c *fb) {
			c.ctx = kctx
			c.ret(c.value(s.Key, -1), kk)
		}})
	} else {
		key = b.temp(bc.KRef)
		b.emit(op("KNONE"), key)
	}
	rctx := b.l.info.Ctxs[s.Body]
	params := make([]*check.Var, 2)
	copy(params, rctx.Params)
	row := b.makeClosure(closure{name: "row", ctxs: []*check.FuncCtx{rctx}, params: params, ret: bc.KRef, throws: true, body: func(c *fb) {
		c.ctx = rctx
		v := c.viewsOf(s.Body.Items)
		c.emit(op("RET"), v)
	}})
	return b.callB(uiFor, bc.KRef, items, key, row)
}

func (b *fb) matchView(s *syntax.MatchStmt) int {
	subjCtx := b.l.info.Ctxs[s.Subject]
	st := b.rawType(s.Subject)
	sel := b.makeClosure(closure{name: "selector", ctxs: []*check.FuncCtx{subjCtx}, ret: bc.KInt, throws: true, body: func(c *fb) {
		c.ctx = subjCtx
		v := c.value(s.Subject, -1)
		r := c.temp(bc.KInt)
		for i, a := range s.Arms {
			next := c.f.NewLabel()
			c.testPattern(a.Pattern, v, st, next)
			c.loadInt(r, int64(i))
			c.emit(op("RET"), r)
			c.f.Place(next)
		}
		c.loadInt(r, -1)
		c.emit(op("RET"), r)
	}})
	ctxs := []*check.FuncCtx{subjCtx}
	for _, a := range s.Arms {
		ctxs = append(ctxs, b.l.info.Ctxs[a])
	}
	arm := &check.Var{}
	build := b.makeClosure(closure{name: "arms", ctxs: ctxs, params: []*check.Var{arm}, pkinds: []bc.Kind{bc.KInt}, ret: bc.KRef, throws: true, body: func(c *fb) {
		c.ctx = subjCtx
		c.f.Kinds[0] = bc.KInt
		for i, a := range s.Arms {
			next := c.f.NewLabel()
			want := c.konst(int64(i))
			eq := c.temp(bc.KBool)
			c.emit(op("EQI"), eq, 0, want)
			c.emitJump(op("JF"), eq, next)
			actx := b.l.info.Ctxs[a]
			c.ctx = actx
			// Each name the pattern binds: a memo of its part of the
			// subject.
			for _, v := range patternVars(a.Pattern, c.l.info) {
				v := v
				k := c.kind(v.Type)
				m := c.makeClosure(closure{name: "payload", ctxs: []*check.FuncCtx{subjCtx}, ret: k, throws: true, body: func(c2 *fb) {
					c2.ctx = subjCtx
					sv := c2.value(s.Subject, -1)
					c2.ret(c2.extract(a.Pattern, v, sv, st), k)
				}})
				r := c.newReg(bc.KRef)
				c.emit(op("MEMO"), r, m, int(k))
				c.vars[v] = r
			}
			var view int
			switch body := a.Body.(type) {
			case *syntax.Block:
				view = c.viewsOf(body.Items)
			case syntax.Item:
				view = c.viewsOf([]syntax.Item{body})
			}
			c.emit(op("RET"), view)
			c.f.Place(next)
		}
		c.emit(op("RET"), c.callB(uiFragment, bc.KRef))
	}})
	return b.callB(uiSwitch, bc.KRef, sel, build)
}

// patternVars lists the names a pattern binds.
func patternVars(p syntax.Pattern, info *check.Info) []*check.Var {
	var out []*check.Var
	syntax.Inspect(p, func(n syntax.Node) bool {
		if bp, ok := n.(*syntax.BindPat); ok {
			if v, ok := info.Defs[bp.Name].(*check.Var); ok {
				out = append(out, v)
			}
		}
		return true
	})
	return out
}

// extract is the part of v (which matched p) that p binds to target.
func (b *fb) extract(p syntax.Pattern, target *check.Var, v int, t check.Type) int {
	switch p := p.(type) {
	case *syntax.BindPat:
		if b.l.info.Defs[p.Name] == target {
			return v
		}
	case *syntax.TuplePat:
		tt, _ := t.(*check.Tuple)
		for i, e := range p.Elems {
			if tt == nil || !containsVar(e, target, b.l.info) {
				continue
			}
			r := b.temp(b.l.kind(tt.Elems[i]))
			b.emit(op("GETF"), r, v, i)
			return b.extract(e, target, r, tt.Elems[i])
		}
	case *syntax.CasePat:
		k := b.l.info.Cases[p]
		et := check.Unopt(t)
		x := v
		if check.IsOptional(t) {
			x = b.unbox(v, et, -1)
		}
		n, _ := et.(*check.Named)
		for i, a := range p.Args {
			if !containsVar(a, target, b.l.info) {
				continue
			}
			r := b.payload(x, n, k, i)
			return b.extract(a, target, r, b.payloadType(n, k, i))
		}
	}
	return v
}

func containsVar(p syntax.Pattern, v *check.Var, info *check.Info) bool {
	for _, x := range patternVars(p, info) {
		if x == v {
			return true
		}
	}
	return false
}

// ---------------------------------------------------------------------
// Elements and components used

// element builds an element's or a component's view.
func (b *fb) element(e *syntax.CallExpr) int {
	ec := b.l.info.Elements[e]
	if ec == nil {
		return b.valueReg(-1, bc.KRef)
	}
	if ec.Comp.Comp.Native {
		return b.builtinElement(ec)
	}
	return b.componentCall(ec)
}

func (b *fb) builtinElement(ec *check.ElementCall) int {
	name := ec.Comp.Name()
	el := prelude.ElementID(name)
	if el == 0 {
		b.l.errorf(ec.Comp.Decl, "%s isn't an element the runtime has", name)
		return b.valueReg(-1, bc.KRef)
	}
	v := b.local(bc.KRef)
	b.callBTo(uiNew, v, b.konst(int64(el)))
	for i, m := range ec.Comp.Comp.Params {
		if a := ec.Args[i]; a != nil {
			b.setProp(v, name, m, a)
		}
	}
	// A prop set twice: the last one wins.
	last := map[*check.Member]int{}
	for i, p := range ec.Props {
		last[p.Member] = i
	}
	for i, p := range ec.Props {
		if last[p.Member] == i {
			b.setProp(v, name, p.Member, p.Value)
		}
	}
	for _, h := range ec.Handlers {
		ev := prelude.EventID(name, h.Event.Name())
		clos := b.handler(h.Handler, h.Event)
		b.callBTo(uiOn, bc.None, v, b.konst(int64(ev)), clos)
	}
	for _, bd := range ec.Binds {
		b.bind(v, name, bd)
	}
	for _, s := range ec.Slots {
		key := prelude.KeyID(name, s.Prop.Name())
		view := b.viewsOf(s.Block.Items)
		b.callBTo(uiSlot, bc.None, v, b.konst(int64(key)), view)
	}
	for _, ch := range ec.Children {
		mark := b.mark()
		cv, opt := b.viewItem(ch)
		if cv >= 0 {
			skip := b.f.NewLabel()
			if opt {
				b.emitJump(op("JNONE"), cv, skip)
			}
			b.callBTo(uiAdd, bc.None, v, cv)
			b.f.Place(skip)
		}
		b.release(mark)
	}
	return v
}

// setProp sets an element's key: once when its value reads nothing
// reactive, else as a binding.
func (b *fb) setProp(v int, el string, m *check.Member, x syntax.Expr) {
	mark := b.mark()
	defer b.release(mark)
	key := prelude.KeyID(el, m.Name())
	if key == 0 {
		b.l.errorf(x, "%s has no key %s in the runtime", el, m.Name())
		return
	}
	k := b.konst(int64(key))
	if b.l.prog.Reactive(x) {
		t := b.l.thunkType(x, m.Type)
		clos := b.thunk(b.l.info.Ctxs[x], x, t)
		b.callBTo(uiBind, bc.None, v, k, clos)
		return
	}
	val := b.value(x, -1)
	b.callBTo(uiSet, bc.None, v, k, val)
}

// thunkType is the type a binding of x returns.
func (l *Lowerer) thunkType(x syntax.Expr, want check.Type) check.Type {
	if t, ok := l.info.Convs[x]; ok {
		return t
	}
	if t := l.info.Types[x]; t != nil {
		return t
	}
	return want
}

// handler makes an on handler's closure: it takes the event's values.
func (b *fb) handler(h *syntax.OnHandler, ev *check.Member) int {
	ctx := b.l.info.Ctxs[h]
	vars := b.l.info.Binds[h]
	params := make([]*check.Var, len(ev.Params))
	pkinds := make([]bc.Kind, len(ev.Params))
	for i, p := range ev.Params {
		pkinds[i] = b.kind(p.Type)
		if i < len(vars) {
			params[i] = vars[i]
		}
	}
	return b.makeClosure(closure{name: "on " + h.Name.Name, ctxs: []*check.FuncCtx{ctx}, params: params, pkinds: pkinds, ret: bc.KVoid, throws: true, body: func(c *fb) {
		c.ctx = ctx
		c.block(h.Body)
		c.emit(op("RET0"))
		if ctx.Awaits {
			c.f.Flags |= bc.FAsync
		}
	}})
}

// bind makes bind name: target: a binding of the key to target, and a
// handler of the control's change writing it.
func (b *fb) bind(v int, el string, bd *check.BindArg) {
	mark := b.mark()
	defer b.release(mark)
	key := prelude.KeyID(el, bd.Prop.Name())
	readCtx := b.l.info.Ctxs[bd.Item]
	clos := b.thunk(readCtx, bd.Target, bd.Prop.Type)
	b.callBTo(uiBind, bc.None, v, b.konst(int64(key)), clos)
	if bd.Event == nil {
		return
	}
	wctx := b.l.info.Ctxs[bd.Target]
	params := make([]*check.Var, len(bd.Event.Params))
	pkinds := make([]bc.Kind, len(bd.Event.Params))
	for i, p := range bd.Event.Params {
		pkinds[i] = b.kind(p.Type)
	}
	if len(params) > 0 && len(wctx.Params) > 0 {
		params[0] = wctx.Params[0]
	}
	h := b.makeClosure(closure{name: "bind " + bd.Prop.Name(), ctxs: []*check.FuncCtx{wctx}, params: params, pkinds: pkinds, ret: bc.KVoid, throws: true, body: func(c *fb) {
		c.ctx = wctx
		if params[0] != nil {
			lv := c.prepare(bd.Target)
			c.set(lv, c.vars[params[0]])
		}
		c.emit(op("RET0"))
	}})
	ev := prelude.EventID(el, bd.Event.Name())
	b.callBTo(uiOn, bc.None, v, b.konst(int64(ev)), h)
}

// componentCall calls a component's function: accessors of its
// parameters and props, builders of its slots, its events' handlers.
func (b *fb) componentCall(ec *check.ElementCall) int {
	comp := ec.Comp.Comp
	idx := b.l.compFunc(ec.Comp)
	var vals []int
	none := func() int {
		r := b.temp(bc.KRef)
		b.emit(op("KNONE"), r)
		return r
	}
	for i, m := range comp.Params {
		a := ec.Args[i]
		switch {
		case a == nil:
			vals = append(vals, none())
		case m.IsSlot():
			vals = append(vals, b.builder(a))
		default:
			vals = append(vals, b.accessor(a, m.Type))
		}
	}
	for _, m := range comp.Props {
		var val syntax.Expr
		for _, p := range ec.Props {
			if p.Member == m {
				val = p.Value
			}
		}
		var slot *check.SlotArg
		for _, s := range ec.Slots {
			if s.Prop == m {
				slot = s
			}
		}
		switch {
		case val != nil && m.IsSlot():
			vals = append(vals, b.builder(val))
		case val != nil:
			vals = append(vals, b.accessor(val, m.Type))
		case slot != nil:
			vals = append(vals, b.builderItems(b.l.info.Ctxs[slot.Block], slot.Block.Items))
		case m.Name() == "children" && len(ec.Children) > 0:
			vals = append(vals, b.builderItems(b.l.info.Ctxs[ec.Children[0]], ec.Children))
		default:
			vals = append(vals, none())
		}
	}
	for _, m := range comp.Events {
		var h *check.HandlerArg
		for _, x := range ec.Handlers {
			if x.Event == m {
				h = x
			}
		}
		if h != nil {
			vals = append(vals, b.handler(h.Handler, m))
		} else {
			vals = append(vals, none())
		}
	}
	return b.invoke(idx, vals, bc.KRef, -1, false)
}
