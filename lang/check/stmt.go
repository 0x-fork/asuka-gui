package check

import (
	"strings"

	"github.com/egoist/asuka/lang/syntax"
)

func (c *Checker) stmts(items []syntax.Item) {
	for _, it := range items {
		c.stmt(it)
	}
}

// block checks a block of statements in a scope of its own.
func (c *Checker) block(b *syntax.Block) {
	if b == nil {
		return
	}
	c.openScope()
	c.stmts(b.Items)
	c.closeScope()
}

func (c *Checker) stmt(it syntax.Item) {
	switch s := it.(type) {
	case *syntax.LetDecl:
		c.letStmt(s)
	case *syntax.AssignStmt:
		c.assignStmt(s)
	case *syntax.IfStmt:
		c.openScope()
		c.cond(s.Cond)
		c.block(s.Then)
		c.closeScope()
		switch el := s.Else.(type) {
		case *syntax.IfStmt:
			c.stmt(el)
		case *syntax.Block:
			c.block(el)
		}
	case *syntax.ForStmt:
		c.forStmt(s, false)
	case *syntax.WhileStmt:
		c.openScope()
		c.cond(s.Cond)
		c.loops++
		c.block(s.Body)
		c.loops--
		c.closeScope()
	case *syntax.MatchStmt:
		c.matchStmt(s)
	case *syntax.DoStmt:
		c.doStmt(s)
	case *syntax.ReturnStmt:
		c.returnStmt(s)
	case *syntax.ThrowStmt:
		t := c.expr(s.Value, c.errT.Type)
		if !IsInvalid(t) && !IsErrorType(t) {
			c.errorf(s.Value, "throw takes an error, and a %s isn't one (declare it: struct %s: Error, or throw Error(\"…\"))", typeString(t), typeString(t))
		}
		c.mayThrow(s)
	case *syntax.BranchStmt:
		if c.loops == 0 {
			c.errorf(s, "%s outside a loop", s.Tok)
		}
	case *syntax.DeferStmt:
		if c.inDefer > 0 {
			c.errorf(s, "a defer inside a defer: put the code in the first one")
		}
		c.inDefer++
		savedLoops := c.loops
		c.loops = 0
		c.block(s.Body)
		c.loops = savedLoops
		c.inDefer--
	case *syntax.EmitStmt:
		c.emitStmt(s)
	case *syntax.ExprStmt:
		t := c.expr(s.X, nil)
		if t == View {
			if call, ok := s.X.(*syntax.CallExpr); ok && c.info.Elements[call] != nil {
				c.errorf(s, "a view here does nothing: views go in a component's body")
			}
		}
	case *syntax.BadItem:
	case *syntax.FuncDecl:
		c.errorf(s, "a fn is declared at the top of a file, or in a component's or a store's body; here, use a closure: let %s = (…) => …", s.Name.Name)
	default:
		c.errorf(it, "%s isn't a statement: %s", itemName(it), notStatementHint(it))
	}
}

func itemName(it syntax.Item) string {
	switch it := it.(type) {
	case *syntax.PropDecl:
		return "prop"
	case *syntax.EventDecl:
		return "event"
	case *syntax.StateDecl:
		return "state"
	case *syntax.UseDecl:
		return "use"
	case *syntax.ProvideDecl:
		return "provide"
	case *syntax.TaskDecl:
		return "task"
	case *syntax.EffectDecl:
		return "effect"
	case *syntax.StyleDecl:
		return "a style"
	case *syntax.PropSet:
		return it.Name.Name + ": …"
	case *syntax.OnHandler:
		return "on " + it.Name.Name
	case *syntax.BindItem:
		return "bind"
	case *syntax.StyleUse:
		return "style"
	case *syntax.SlotItem:
		return it.Name.Name + " { … }"
	}
	return "this"
}

func notStatementHint(it syntax.Item) string {
	switch it.(type) {
	case *syntax.StateDecl:
		return "state belongs to a component, a store or a file; here a value that changes is var"
	case *syntax.PropSet, *syntax.OnHandler, *syntax.BindItem, *syntax.StyleUse, *syntax.SlotItem:
		return "it goes in an element's block"
	}
	return "it goes in a component's or a store's body"
}

func (c *Checker) letStmt(d *syntax.LetDecl) {
	var t Type
	if d.Value == nil {
		c.errorf(d, "a let needs a value")
		return
	}
	if d.Type != nil {
		want := c.typeExpr(d.Type)
		t = c.assign(d.Value, want, "the value")
	} else {
		t = c.valueType(c.expr(d.Value, nil), d.Value)
	}
	c.info.Lets[d] = t
	c.bindIrrefutable(d.Pattern, t, d.Keyword == syntax.VAR)
}

// bindIrrefutable binds a let's or a for's pattern: names, _ and
// tuples of them.
func (c *Checker) bindIrrefutable(p syntax.Pattern, t Type, mutable bool) {
	switch p := p.(type) {
	case *syntax.BindPat:
		c.declareVar(p.Name, t, mutable)
	case *syntax.WildcardPat:
	case *syntax.TuplePat:
		tt, ok := t.(*Tuple)
		if !ok || len(tt.Elems) != len(p.Elems) {
			if !IsInvalid(t) {
				c.errorf(p, "a %s isn't a tuple of %d", typeString(t), len(p.Elems))
			}
			for _, e := range p.Elems {
				c.bindIrrefutable(e, Invalid, mutable)
			}
			return
		}
		for i, e := range p.Elems {
			c.bindIrrefutable(e, tt.Elems[i], mutable)
		}
	default:
		c.errorf(p, "a let binds names (let x = …, let (a, b) = …); to match, use if let or match")
	}
}

func (c *Checker) assignStmt(s *syntax.AssignStmt) {
	lt := c.lvalue(s.Target, "assign to")
	if ix, ok := unparen(s.Target).(*syntax.IndexExpr); ok {
		if _, isMap := c.info.Types[ix.X].(*Map); isMap {
			// m[k] reads a V?, and stores a V.
			switch s.Op {
			case syntax.ASSIGN, syntax.COALESCE_ASSIGN:
				c.assign(s.Value, Unopt(lt), "the value")
			default:
				c.expr(s.Value, nil)
				c.errorf(s, "m[k] may be none: write m[k] = (m[k] ?? 0) %s …", strings.TrimSuffix(s.Op.String(), "="))
			}
			return
		}
	}
	switch s.Op {
	case syntax.ASSIGN:
		c.assign(s.Value, lt, "the value")
	case syntax.COALESCE_ASSIGN:
		if !IsOptional(lt) && !IsInvalid(lt) {
			c.errorf(s.Target, "%s isn't optional: ??= has nothing to do", typeString(lt))
		}
		c.assign(s.Value, lt, "the value")
	default:
		vt := c.expr(s.Value, numOnly(lt))
		if IsInvalid(lt) || IsInvalid(vt) {
			return
		}
		op := strings.TrimSuffix(s.Op.String(), "=")
		switch {
		case IsNumeric(lt) && Identical(lt, vt):
		case lt == String && vt == String && s.Op == syntax.ADD_ASSIGN:
		case lt == Duration && vt == Duration && (s.Op == syntax.ADD_ASSIGN || s.Op == syntax.SUB_ASSIGN):
		case lt == Duration && vt == Int && (s.Op == syntax.MUL_ASSIGN || s.Op == syntax.QUO_ASSIGN):
		default:
			if _, ok := lt.(*Array); ok && Identical(lt, vt) && s.Op == syntax.ADD_ASSIGN {
				return
			}
			c.errorf(s, "%s= doesn't take a %s and a %s", op, typeString(lt), typeString(vt))
		}
	}
}

// lvalue checks x as something assigned to (or changed in place) and
// returns its type.
func (c *Checker) lvalue(x syntax.Expr, what string) Type {
	t := c.expr(x, nil)
	if c.ctx != nil {
		switch c.ctx.Kind {
		case CtxBinding, CtxGlobal, CtxBuilder, CtxComponent, CtxStore, CtxApp:
			c.errorf(x, "can't %s %s here: what the UI follows is computed, not changed; change state in a handler (on press { … }), a fn, a task or an effect", what, exprName(x))
			return t
		}
	}
	c.checkLvalue(x, what)
	return t
}

func (c *Checker) checkLvalue(x syntax.Expr, what string) {
	switch x := x.(type) {
	case *syntax.ParenExpr:
		c.checkLvalue(x.X, what)
	case *syntax.Ident:
		switch o := c.info.Uses[x].(type) {
		case *Var:
			if o.Kind != LocalVar || !o.Mutable {
				hint := "it's a let: make it a var"
				if o.Kind == RowItem {
					hint = "it's a row's item: change the state it comes from"
				} else if o.Kind == SelfVar {
					hint = "self is a value"
				} else if o.Ctx != nil && o.Ctx.Kind == CtxLambda && contains(o.Ctx.Params, o) || isParam(o) {
					hint = "parameters are values: copy it into a var"
				}
				c.errorf(x, "can't %s %s: %s", what, x.Name, hint)
				return
			}
			o.Assigned = true
			if o.Ctx != c.ctx {
				o.AssignedInClosure = true
				for cc := c.ctx; cc != nil; cc = cc.Parent {
					if cc.Kind == CtxSpawn {
						c.errorf(x, "spawn can't change %s: it runs on another thread; return a value instead", x.Name)
						break
					}
				}
			}
		case *Member:
			switch o.Kind {
			case StateMember:
			case PropMember, ParamMember:
				c.errorf(x, "can't %s %s: it's the caller's (a prop); keep state of your own, or emit an event", what, x.Name)
			case LetMember:
				c.errorf(x, "can't %s %s: it's derived (a let); change what it reads", what, x.Name)
			default:
				c.errorf(x, "can't %s %s", what, x.Name)
			}
		case *Global:
			if o.Kind != StateGlobal {
				c.errorf(x, "can't %s %s: it's a let; a value that changes is state", what, x.Name)
			}
		case *selfMember:
			c.errorf(x, "can't %s %s: a struct's methods can't change it (it's a value); return a new one", what, x.Name)
		case nil:
		default:
			c.errorf(x, "can't %s %s", what, x.Name)
		}
	case *syntax.MemberExpr:
		sel := c.info.Selections[x]
		if sel == nil {
			return
		}
		if sel.Optional {
			c.errorf(x, "can't %s through ?.: unwrap it first (if let)", what)
			return
		}
		switch sel.Kind {
		case SelField:
			c.checkLvalue(x.X, what)
		case SelStoreMember:
			if sel.Member.Kind != StateMember {
				c.errorf(x, "can't %s %s: it isn't state", what, x.Name.Name)
			}
		case SelPkg:
			if g, ok := sel.Obj.(*Global); !ok || g.Kind != StateGlobal {
				c.errorf(x, "can't %s %s: only a package's state changes", what, exprName(x))
			}
		default:
			c.errorf(x, "can't %s %s", what, exprName(x))
		}
	case *syntax.IndexExpr:
		c.checkLvalue(x.X, what)
	case *syntax.SelfExpr:
		c.errorf(x, "can't %s self: a struct's methods can't change it (it's a value); return a new one", what)
	default:
		c.errorf(x, "can't %s this: only vars, state, and their fields and elements", what)
	}
}

func isParam(v *Var) bool {
	if v.Ctx == nil {
		return false
	}
	return contains(v.Ctx.Params, v)
}

// cond checks an if's or a while's condition: a bool, or let pattern =
// value.
func (c *Checker) cond(e syntax.Expr) {
	lc, ok := e.(*syntax.LetCond)
	if !ok {
		c.assign(e, Bool, "the condition")
		return
	}
	t := c.expr(lc.Value, nil)
	c.letCondPattern(lc, t, LocalVar)
}

func (c *Checker) letCondPattern(lc *syntax.LetCond, t Type, kind VarKind) {
	if bp, ok := lc.Pattern.(*syntax.BindPat); ok {
		if !IsOptional(t) && !IsInvalid(t) {
			c.errorf(lc.Value, "%s isn't optional: if let unwraps an optional (use let)", typeString(t))
		}
		v := c.declareVar(bp.Name, Unopt(t), false)
		v.Kind = kind
		return
	}
	c.pattern(lc.Pattern, t, kind)
}

func (c *Checker) forStmt(s *syntax.ForStmt, view bool) {
	fi := &ForInfo{Pat: s.Pattern, View: view}
	c.info.Fors[s] = fi
	if s.Await.IsValid() {
		c.forAwait(s, fi, view)
		return
	}
	seq := unparen(s.Seq)
	saved := c.ctx
	if view {
		c.enter(CtxBinding, s.Seq, "items")
	}
	switch {
	case isRange(seq):
		b := seq.(*syntax.BinaryExpr)
		c.assign(b.X, Int, "a range's start")
		c.assign(b.Y, Int, "a range's end")
		c.record(seq, &Array{Int})
		fi.Kind, fi.Seq, fi.End, fi.Incl, fi.Elem = ForRange, b.X, b.Y, b.Op == syntax.RANGE, Int
		if view {
			fi.Kind, fi.Seq = ForArray, seq
		}
	case isEnumerated(seq):
		call := seq.(*syntax.CallExpr)
		c.expr(call, nil)
		fi.Kind, fi.Seq = ForEnumerated, call.Fun.(*syntax.MemberExpr).X
		if a, ok := c.info.Types[fi.Seq].(*Array); ok {
			fi.Elem = a.Elem
		} else {
			fi.Elem = Invalid
		}
	default:
		t := c.expr(s.Seq, nil)
		fi.Seq = s.Seq
		switch u := t.(type) {
		case *Array:
			fi.Kind, fi.Elem = ForArray, u.Elem
		case *Map:
			fi.Kind, fi.Elem = ForMap, &Tuple{[]Type{u.Key, u.Elem}}
			if view {
				c.errorf(s.Seq, "a for in a view goes over an array: use the map's keys() or values()")
			}
		default:
			if t == String {
				c.errorf(s.Seq, "a string isn't a list: go over its characters, s.chars()")
			} else if !IsInvalid(t) {
				c.errorf(s.Seq, "for goes over an array, a map or a range, not a %s", typeString(t))
			}
			fi.Kind, fi.Elem = ForArray, Invalid
		}
	}
	if view {
		c.leave(saved)
	}
	// The key, in a view: a function of the item.
	if s.Key != nil {
		if !view {
			c.errorf(s.Key, "key is for a for in a view")
		}
		saved := c.ctx
		kc := c.enter(CtxLambda, s.Key, "key")
		kc.Result = nil
		c.forBindings(fi, s.Pattern, LocalVar, kc)
		kt := c.expr(s.Key, nil)
		if !IsInvalid(kt) && !IsHashable(kt) {
			c.errorf(s.Key, "a key is an int, a string or an enum without payloads, not a %s", typeString(kt))
		}
		kc.Result = kt
		c.leave(saved)
	}
	if view {
		saved := c.ctx
		row := c.enter(CtxBuilder, s.Body, "row")
		c.forBindings(fi, s.Pattern, RowItem, row)
		savedView := c.view
		c.view = true
		c.viewItems(s.Body.Items, nil)
		c.view = savedView
		c.leave(saved)
		return
	}
	c.openScope()
	c.forBindings(fi, s.Pattern, LocalVar, nil)
	c.loops++
	c.block(s.Body)
	c.loops--
	c.closeScope()
}

// forBindings declares a for's item (and index, or key and value).
func (c *Checker) forBindings(fi *ForInfo, p syntax.Pattern, kind VarKind, ctx *FuncCtx) {
	bind := func(p syntax.Pattern, t Type) *Var {
		switch p := p.(type) {
		case *syntax.BindPat:
			v := c.declareVar(p.Name, t, false)
			v.Kind = kind
			if ctx != nil {
				ctx.Params = append(ctx.Params, v)
			}
			return v
		case *syntax.WildcardPat:
			v := &Var{object: object{"_", p.Start}, Type: t, Kind: kind, Ctx: c.ctx}
			if ctx != nil {
				ctx.Params = append(ctx.Params, v)
			}
			return v
		case *syntax.TuplePat:
			if kind != LocalVar {
				c.errorf(p, "a row's item is one name: for x in xs")
				return nil
			}
			v := &Var{object: object{"_", p.Start}, Type: t, Ctx: c.ctx}
			c.bindIrrefutable(p, t, false)
			return v
		}
		c.errorf(p, "for binds a name, _ or a tuple of names")
		return nil
	}
	switch fi.Kind {
	case ForEnumerated, ForMap:
		tp, ok := p.(*syntax.TuplePat)
		if !ok || len(tp.Elems) != 2 {
			if fi.Kind == ForMap {
				c.errorf(p, "a map gives (key, value): for (k, v) in m")
			} else {
				c.errorf(p, "enumerated gives (index, item): for (i, x) in xs.enumerated()")
			}
			return
		}
		var kt Type = Int
		vt := fi.Elem
		if fi.Kind == ForMap {
			if tt, ok := fi.Elem.(*Tuple); ok {
				kt, vt = tt.Elems[0], tt.Elems[1]
			}
		}
		fi.Index = bind(tp.Elems[0], kt)
		fi.Item = bind(tp.Elems[1], vt)
		if ctx != nil && ctx.Kind == CtxBuilder {
			// The row closure takes (item, index).
			ctx.Params[len(ctx.Params)-2], ctx.Params[len(ctx.Params)-1] = ctx.Params[len(ctx.Params)-1], ctx.Params[len(ctx.Params)-2]
		}
	default:
		fi.Item = bind(p, fi.Elem)
	}
}

func isRange(e syntax.Expr) bool {
	b, ok := e.(*syntax.BinaryExpr)
	return ok && (b.Op == syntax.RANGE || b.Op == syntax.RANGELT)
}

func isEnumerated(e syntax.Expr) bool {
	call, ok := e.(*syntax.CallExpr)
	if !ok || argCount(call) != 0 {
		return false
	}
	m, ok := call.Fun.(*syntax.MemberExpr)
	return ok && m.Name.Name == "enumerated" && !m.Optional
}

func (c *Checker) returnStmt(s *syntax.ReturnStmt) {
	if c.inDefer > 0 {
		c.errorf(s, "return in a defer: a defer runs as its block ends, and can't change how")
	}
	ctx := c.ctx
	if ctx == nil {
		return
	}
	switch ctx.Kind {
	case CtxComponent, CtxStore, CtxBinding, CtxBuilder, CtxGlobal, CtxApp:
		c.errorf(s, "return in a component's body: a body is declarations, not code")
		return
	}
	if ctx.inferRes {
		if s.Value == nil {
			ctx.retTypes = append(ctx.retTypes, Void)
		} else {
			ctx.retTypes = append(ctx.retTypes, c.valueType(c.expr(s.Value, nil), s.Value))
		}
		return
	}
	res := ctx.Result
	if res == nil {
		res = Void
	}
	switch {
	case s.Value == nil && res != Void:
		c.errorf(s, "return needs a %s", typeString(res))
	case s.Value != nil && res == Void:
		c.expr(s.Value, nil)
		c.errorf(s.Value, "%s returns nothing: drop the value, or declare a result (-> T)", ctxLabel(ctx))
	case s.Value != nil:
		c.assign(s.Value, res, "the result")
	}
}

func ctxLabel(ctx *FuncCtx) string {
	switch ctx.Kind {
	case CtxFunc, CtxMethod:
		return "fn " + ctx.Name
	case CtxHandler:
		return "a handler"
	case CtxTask:
		return "a task"
	case CtxEffect:
		return "an effect"
	case CtxTest:
		return "a test"
	}
	return "this closure"
}

func (c *Checker) emitStmt(s *syntax.EmitStmt) {
	comp := c.ctx.Comp
	if comp == nil || comp.Kind != ComponentType {
		c.errorf(s, "emit raises a component's event, and this isn't in a component")
		c.emitArgs(s, nil)
		return
	}
	m := comp.Comp.Members[s.Name.Name]
	if m == nil || m.Kind != EventMember {
		c.errorf(s.Name, "%s has no event %s: declare it, event %s", comp.name, s.Name.Name, s.Name.Name)
		c.emitArgs(s, nil)
		return
	}
	c.envUse(comp, s, m.name)
	c.info.Emits[s] = m
	c.emitArgs(s, m.Params)
}

func (c *Checker) emitArgs(s *syntax.EmitStmt, params []*Param) {
	var list []*syntax.Arg
	if s.Args != nil {
		list = s.Args.List
	}
	if params != nil && len(list) != len(params) {
		c.errorf(s, "event %s takes %s", s.Name.Name, plural(len(params), "argument", "arguments"))
	}
	for i, a := range list {
		if params != nil && i < len(params) {
			c.assign(a.Value, params[i].Type, "argument "+params[i].Name)
		} else {
			c.expr(a.Value, nil)
		}
	}
}

// ---------------------------------------------------------------------
// do/catch

func (c *Checker) doStmt(s *syntax.DoStmt) {
	all := false
	for _, cl := range s.Catches {
		if c.catchAll(cl) {
			all = true
		}
	}
	if len(s.Catches) == 0 {
		c.errorf(s, "do needs a catch")
	}
	if all {
		c.handled++
	}
	c.block(s.Body)
	if all {
		c.handled--
	}
	for i, cl := range s.Catches {
		if i < len(s.Catches)-1 && c.info.Catches[cl] != nil && c.info.Catches[cl].All {
			c.errorf(s.Catches[i+1], "this catch is never reached: the one before catches everything")
		}
		c.openScope()
		c.catchClause(cl)
		c.block(cl.Body)
		c.closeScope()
	}
}

func (c *Checker) catchAll(cl *syntax.CatchClause) bool {
	if cl.Pattern == nil {
		return true
	}
	if _, ok := cl.Pattern.(*syntax.BindPat); ok {
		if cl.Type == nil {
			return true
		}
		if nt, ok := cl.Type.(*syntax.NamedType); ok && len(nt.Path) == 1 && nt.Path[0].Name == "Error" {
			return true
		}
	}
	if _, ok := cl.Pattern.(*syntax.WildcardPat); ok {
		return true
	}
	return false
}

func (c *Checker) catchClause(cl *syntax.CatchClause) {
	ci := &CatchInfo{}
	c.info.Catches[cl] = ci
	switch p := cl.Pattern.(type) {
	case nil, *syntax.WildcardPat:
		ci.All = true
	case *syntax.BindPat:
		t := c.errT.Type
		if cl.Type != nil {
			t = c.typeExpr(cl.Type)
			if n, ok := t.(*Named); ok && n.Obj == c.errT {
				ci.All = true
			} else if n, ok := t.(*Named); ok && n.Obj.IsError {
				ci.Type = n.Obj
			} else if !IsInvalid(t) {
				c.errorf(cl.Type, "%s isn't an error type: catch e: T takes a type declared : Error", typeString(t))
			}
		} else {
			ci.All = true
		}
		ci.Var = c.declareVar(p.Name, t, false)
	case *syntax.CasePat:
		if p.Implicit || len(p.Path) < 2 {
			c.errorf(p, "write the error's type: catch MailError.offline")
			return
		}
		tn := c.casePathType(p.Path[:len(p.Path)-1])
		if tn == nil {
			return
		}
		if !tn.IsError {
			c.errorf(p, "%s isn't an error type (declare it: enum %s: Error)", tn.name, tn.name)
			return
		}
		k := tn.Case(p.Path[len(p.Path)-1].Name)
		if k == nil {
			c.errorf(p, "%s has no case %s", tn.name, p.Path[len(p.Path)-1].Name)
			return
		}
		ci.Type, ci.Case = tn, k
		c.info.Cases[p] = k
		c.caseArgs(p, k, tn.Type)
	default:
		c.errorf(cl.Pattern, "catch takes a name (catch e), a typed one (catch e: T) or a case (catch T.case)")
	}
}

// casePathType resolves the type part of a qualified case.
func (c *Checker) casePathType(path []*syntax.Ident) *TypeName {
	var o Object
	if len(path) == 1 {
		o = c.lookup(path[0].Name)
	} else if len(path) == 2 {
		if pn, ok := c.lookup(path[0].Name).(*PkgName); ok {
			o = pn.Pkg.Scope.LookupLocal(path[1].Name)
		}
	}
	tn, ok := o.(*TypeName)
	if !ok || tn.Kind != EnumType && tn.Kind != StructType {
		c.errorf(path[0], "%s isn't an enum", joinPath(path))
		return nil
	}
	c.info.Uses[path[len(path)-1]] = tn
	return tn
}

// ---------------------------------------------------------------------
// Patterns

// pattern checks a (refutable) pattern against values of type t,
// declaring its names.
func (c *Checker) pattern(p syntax.Pattern, t Type, kind VarKind) {
	switch p := p.(type) {
	case *syntax.WildcardPat, *syntax.BadPat:
	case *syntax.BindPat:
		v := c.declareVar(p.Name, t, false)
		v.Kind = kind
	case *syntax.LitPat:
		lt := c.expr(p.Value, t)
		if !IsInvalid(lt) && !IsInvalid(t) && !c.convert(p.Value, lt, t) && !(lt == NoneType && IsOptional(t)) {
			c.errorf(p, "a %s can't be a %s", typeString(t), typeString(lt))
		}
	case *syntax.TuplePat:
		tt, ok := t.(*Tuple)
		if !ok || len(tt.Elems) != len(p.Elems) {
			if !IsInvalid(t) {
				c.errorf(p, "a %s isn't a tuple of %d", typeString(t), len(p.Elems))
			}
			for _, e := range p.Elems {
				c.pattern(e, Invalid, kind)
			}
			return
		}
		for i, e := range p.Elems {
			c.pattern(e, tt.Elems[i], kind)
		}
	case *syntax.OrPat:
		for _, a := range p.Alts {
			before := len(c.scope.names)
			c.pattern(a, t, kind)
			if len(c.scope.names) != before {
				c.errorf(a, "a | pattern can't bind names: match the cases in arms of their own")
			}
		}
	case *syntax.CasePat:
		et := Unopt(t)
		var tn *TypeName
		if p.Implicit {
			n, ok := et.(*Named)
			if !ok || n.Obj.Kind != EnumType {
				if !IsInvalid(t) {
					c.errorf(p, ".%s: a %s isn't an enum", p.Path[0].Name, typeString(t))
				}
				return
			}
			tn = n.Obj
		} else {
			tn = c.casePathType(p.Path[:len(p.Path)-1])
			if tn == nil {
				return
			}
			if n, ok := et.(*Named); !ok || n.Obj != tn {
				if !IsInvalid(t) {
					c.errorf(p, "a %s can't be a %s", typeString(t), tn.name)
				}
				return
			}
		}
		name := p.Path[len(p.Path)-1].Name
		k := tn.Case(name)
		if k == nil {
			c.errorf(p, "%s has no case %s", tn.name, name)
			return
		}
		c.info.Cases[p] = k
		c.caseArgs(p, k, et)
	}
}

// caseArgs checks a case pattern's payload patterns.
func (c *Checker) caseArgs(p *syntax.CasePat, k *Case, t Type) {
	if !p.Lparen.IsValid() {
		return
	}
	if len(p.Args) != len(k.Params) {
		c.errorf(p, "case %s has %s", k.Name, plural(len(k.Params), "payload value", "payload values"))
		return
	}
	var s Subst
	if n, ok := t.(*Named); ok {
		s = n.Subst()
	}
	kind := LocalVar
	if c.view {
		kind = ViewMemo
	}
	for i, a := range p.Args {
		pt := s.Apply(k.Params[i])
		if pt == nil {
			pt = c.errT.Type
		}
		c.pattern(a, pt, kind)
	}
}

// irrefutable reports a pattern that matches anything of its type.
func irrefutable(p syntax.Pattern) bool {
	switch p := p.(type) {
	case *syntax.WildcardPat, *syntax.BindPat:
		return true
	case *syntax.TuplePat:
		for _, e := range p.Elems {
			if !irrefutable(e) {
				return false
			}
		}
		return true
	}
	return false
}

// exhaustive reports whether arms cover every value of t, and what they
// miss.
func (c *Checker) exhaustive(t Type, arms []*syntax.MatchArm) (bool, []string) {
	covered := map[string]bool{}
	none := false
	var add func(p syntax.Pattern) bool
	add = func(p syntax.Pattern) bool {
		switch p := p.(type) {
		case *syntax.WildcardPat, *syntax.BindPat:
			return true
		case *syntax.TuplePat:
			return irrefutable(p)
		case *syntax.OrPat:
			for _, a := range p.Alts {
				if add(a) {
					return true
				}
			}
		case *syntax.CasePat:
			all := true
			for _, a := range p.Args {
				if !irrefutable(a) {
					all = false
				}
			}
			if all {
				covered[p.Path[len(p.Path)-1].Name] = true
			}
		case *syntax.LitPat:
			switch v := p.Value.(type) {
			case *syntax.BoolLit:
				covered[strconvBool(v.Value)] = true
			case *syntax.NoneLit:
				none = true
			}
		}
		return false
	}
	for _, a := range arms {
		if a.Guard != nil {
			continue
		}
		if add(a.Pattern) {
			return true, nil
		}
	}
	if IsInvalid(t) {
		return true, nil
	}
	var missing []string
	if IsOptional(t) && !none {
		missing = append(missing, "none")
	}
	switch u := Unopt(t).(type) {
	case *Named:
		if u.Obj.Kind == EnumType {
			for _, k := range u.Obj.Cases {
				if !covered[k.Name] {
					missing = append(missing, "."+k.Name)
				}
			}
			return len(missing) == 0, missing
		}
	case *Basic:
		if u == Bool {
			for _, b := range []string{"true", "false"} {
				if !covered[b] {
					missing = append(missing, b)
				}
			}
			return len(missing) == 0, missing
		}
	}
	return false, append(missing, "the rest")
}

func strconvBool(b bool) string {
	if b {
		return "true"
	}
	return "false"
}

func (c *Checker) matchStmt(s *syntax.MatchStmt) {
	t := c.expr(s.Subject, nil)
	mi := &MatchInfo{Subject: t}
	c.info.Matches[s] = mi
	for _, a := range s.Arms {
		c.openScope()
		c.pattern(a.Pattern, t, LocalVar)
		if a.Guard != nil {
			c.assign(a.Guard, Bool, "the guard")
		}
		switch b := a.Body.(type) {
		case *syntax.Block:
			c.block(b)
		case syntax.Item:
			c.stmt(b)
		}
		c.closeScope()
	}
	ok, missing := c.exhaustive(t, s.Arms)
	mi.Exhaustive = ok
	if !ok {
		c.errorf(s, "match doesn't cover %s: add arms for them, or _ => { }", strings.Join(missing, ", "))
	}
}

func (c *Checker) matchExpr(e *syntax.MatchExpr, want Type) Type {
	t := c.expr(e.Subject, nil)
	mi := &MatchInfo{Subject: t}
	c.info.Matches[e] = mi
	var res Type
	for _, a := range e.Arms {
		c.openScope()
		c.pattern(a.Pattern, t, LocalVar)
		if a.Guard != nil {
			c.assign(a.Guard, Bool, "the guard")
		}
		var at Type
		switch b := a.Body.(type) {
		case *syntax.Block:
			at = c.valueBlock(b, want)
		case syntax.Expr:
			if want != nil {
				at = c.assign(b, want, "the value")
			} else {
				at = c.expr(b, nil)
			}
		}
		c.closeScope()
		if res == nil {
			res = at
		} else if j := c.join(res, at); j != nil {
			res = j
		} else {
			c.errorf(a, "this arm is a %s, the ones before a %s: make them the same type", typeString(at), typeString(res))
		}
	}
	ok, missing := c.exhaustive(t, e.Arms)
	mi.Exhaustive = ok
	if !ok {
		c.errorf(e, "match doesn't cover %s: add arms for them, or _ =>", strings.Join(missing, ", "))
	}
	if want != nil {
		return want
	}
	if res == nil {
		return Invalid
	}
	return res
}

// terminates reports whether control can't reach the end of items.
func (c *Checker) terminates(items []syntax.Item) bool {
	if len(items) == 0 {
		return false
	}
	switch s := items[len(items)-1].(type) {
	case *syntax.ReturnStmt, *syntax.ThrowStmt:
		return true
	case *syntax.IfStmt:
		if s.Else == nil || !c.terminates(s.Then.Items) {
			return false
		}
		switch el := s.Else.(type) {
		case *syntax.IfStmt:
			return c.terminates([]syntax.Item{el})
		case *syntax.Block:
			return c.terminates(el.Items)
		}
	case *syntax.MatchStmt:
		if mi := c.info.Matches[s]; mi == nil || !mi.Exhaustive {
			return false
		}
		for _, a := range s.Arms {
			switch b := a.Body.(type) {
			case *syntax.Block:
				if !c.terminates(b.Items) {
					return false
				}
			case syntax.Item:
				if !c.terminates([]syntax.Item{b}) {
					return false
				}
			}
		}
		return true
	case *syntax.DoStmt:
		if !c.terminates(s.Body.Items) {
			return false
		}
		for _, cl := range s.Catches {
			if !c.terminates(cl.Body.Items) {
				return false
			}
		}
		return true
	case *syntax.WhileStmt:
		if b, ok := s.Cond.(*syntax.BoolLit); ok && b.Value && !hasBreak(s.Body.Items) {
			return true
		}
	case *syntax.ExprStmt:
		if call, ok := s.X.(*syntax.CallExpr); ok {
			if ci := c.info.Calls[call]; ci != nil && ci.Kind == CallBuiltin && ci.Builtin.ID == BPanic {
				return true
			}
		}
	}
	return false
}

func hasBreak(items []syntax.Item) bool {
	found := false
	for _, it := range items {
		syntax.Inspect(it, func(n syntax.Node) bool {
			switch n := n.(type) {
			case *syntax.ForStmt, *syntax.WhileStmt, *syntax.LambdaExpr:
				return false
			case *syntax.BranchStmt:
				if n.Tok == syntax.BREAK {
					found = true
				}
			}
			return !found
		})
	}
	return found
}

// forAwait is for await x in s: each value the stream sends, as it
// comes. It waits, and throws what the stream's producer throws.
func (c *Checker) forAwait(s *syntax.ForStmt, fi *ForInfo, view bool) {
	fi.Kind, fi.Seq = ForStream, s.Seq
	if view {
		c.errorf(s, "a view can't wait: read the stream in a task { }, into state")
	}
	if c.ctx == nil || !c.ctx.Async {
		c.errorf(s, "for await waits, and this can't: %s", c.asyncHint())
	} else {
		c.ctx.Awaits = true
	}
	t := c.expr(s.Seq, nil)
	if st, ok := t.(*Stream); ok {
		fi.Elem = st.Elem
	} else {
		if !IsInvalid(t) {
			c.errorf(s.Seq, "for await goes over a stream, not a %s (for goes over arrays)", typeString(t))
		}
		fi.Elem = Invalid
	}
	if s.Key != nil {
		c.errorf(s.Key, "key is for a for in a view")
	}
	c.mayThrow(s.Seq)
	c.openScope()
	c.forBindings(fi, s.Pattern, LocalVar, nil)
	c.loops++
	c.block(s.Body)
	c.loops--
	c.closeScope()
}
