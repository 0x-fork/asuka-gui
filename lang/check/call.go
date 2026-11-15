package check

import (
	"github.com/egoist/asuka/lang/syntax"
)

func (c *Checker) call(e *syntax.CallExpr, want Type) Type {
	if len(e.Handlers) > 0 {
		if _, ok := c.componentOf(e.Fun); !ok {
			c.errorf(e.Handlers[0], "on after a block handles a component's event: %s isn't one", exprName(e.Fun))
		}
	}
	switch fn := e.Fun.(type) {
	case *syntax.Ident:
		o := c.lookup(fn.Name)
		if o == nil {
			c.errorf(fn, "unknown name %s%s", fn.Name, c.suggest(fn.Name))
			c.checkArgsLoosely(e)
			return Invalid
		}
		c.info.Uses[fn] = o
		switch o := o.(type) {
		case *Function:
			return c.funcCall(e, o, nil, nil, want)
		case *Builtin:
			return c.builtinCall(e, o, want)
		case *TypeName:
			return c.typeCall(e, o, want)
		case *Member:
			if o.Kind == FnMember {
				c.envUse(o.Owner, fn, o.name)
				call := c.funcCallInfo(e, o.Fn, nil, nil, want)
				call.SelfCall = true
				return call.Result
			}
			t := c.useMember(o, fn)
			return c.closureCall(e, fn, t)
		case *selfMember:
			if o.method == nil {
				return c.closureCall(e, fn, c.ident(fn))
			}
			if c.ctx == nil || c.ctx.Self == nil {
				c.errorf(fn, "%s is a method of %s: call it on a value", fn.Name, o.owner.name)
				return Invalid
			}
			c.useVar(c.ctx.Self, fn)
			call := c.funcCallInfo(e, o.method, nil, c.ctx.Self.Type, want)
			call.SelfCall = true
			return call.Result
		default:
			return c.closureCall(e, fn, c.ident(fn))
		}
	case *syntax.MemberExpr:
		// A package's function, builtin or type.
		if id, ok := fn.X.(*syntax.Ident); ok {
			if pn, ok := c.lookup(id.Name).(*PkgName); ok {
				c.info.Uses[id] = pn
				o := pn.Pkg.Scope.LookupLocal(fn.Name.Name)
				if o == nil {
					c.errorf(fn.Name, "package %s has no %s", pn.Pkg.Path, fn.Name.Name)
					c.checkArgsLoosely(e)
					return Invalid
				}
				if !c.exported(pn.Pkg, o) {
					c.errorf(fn.Name, "%s isn't pub in package %s", fn.Name.Name, pn.Pkg.Path)
				}
				c.info.Uses[fn.Name] = o
				c.info.Selections[fn] = &Selection{Kind: SelPkg, Obj: o}
				switch o := o.(type) {
				case *Function:
					return c.funcCall(e, o, nil, nil, want)
				case *Builtin:
					return c.builtinCall(e, o, want)
				case *TypeName:
					return c.typeCall(e, o, want)
				}
				return c.closureCall(e, fn, c.member(fn, nil))
			}
		}
		// Type.case(…).
		if tn := c.staticType(fn.X); tn != nil {
			c.record(fn.X, Invalid)
			c.resolveType(tn)
			if k := tn.Case(fn.Name.Name); k != nil {
				return c.caseCall(e, tn, k, want)
			}
			c.errorf(fn.Name, "%s has no case %s", tn.name, fn.Name.Name)
			c.checkArgsLoosely(e)
			return Invalid
		}
		return c.methodCall(e, fn, want)
	case *syntax.ImplicitMember:
		w := Unopt(want)
		if n, ok := w.(*Named); ok && n.Obj.Kind == EnumType {
			if k := n.Obj.Case(fn.Name.Name); k != nil {
				return c.caseCall(e, n.Obj, k, n)
			}
			c.errorf(fn, "%s has no case %s", typeString(n), fn.Name.Name)
		} else if w == nil {
			c.errorf(fn, "can't tell what .%s is here: write its type, Type.%s(…)", fn.Name.Name, fn.Name.Name)
		} else if !IsInvalid(w) {
			c.errorf(fn, ".%s(…) makes an enum's case, and a %s isn't an enum", fn.Name.Name, typeString(w))
		}
		c.checkArgsLoosely(e)
		return Invalid
	}
	return c.closureCall(e, e.Fun, c.expr(e.Fun, nil))
}

func exprName(e syntax.Expr) string {
	switch e := e.(type) {
	case *syntax.Ident:
		return e.Name
	case *syntax.MemberExpr:
		return exprName(e.X) + "." + e.Name.Name
	}
	return "this"
}

// componentOf reports whether a callee names a component.
func (c *Checker) componentOf(fun syntax.Expr) (*TypeName, bool) {
	var o Object
	switch f := fun.(type) {
	case *syntax.Ident:
		o = c.lookup(f.Name)
	case *syntax.MemberExpr:
		if id, ok := f.X.(*syntax.Ident); ok {
			if pn, ok := c.lookup(id.Name).(*PkgName); ok {
				o = pn.Pkg.Scope.LookupLocal(f.Name.Name)
			}
		}
	}
	if tn, ok := o.(*TypeName); ok && tn.Kind == ComponentType {
		return tn, true
	}
	return nil, false
}

// checkArgsLoosely checks a bad call's arguments, for their own errors.
func (c *Checker) checkArgsLoosely(e *syntax.CallExpr) {
	if e.Args != nil {
		for _, a := range e.Args.List {
			if _, ok := a.Value.(*syntax.LambdaExpr); ok {
				continue
			}
			c.expr(a.Value, nil)
		}
	}
}

// ---------------------------------------------------------------------
// Arguments

// matchArgs matches a call's arguments to params: positional ones first,
// then by name. It returns the argument per parameter (nil for one left
// to its default).
func (c *Checker) matchArgs(e *syntax.CallExpr, params []*Param, what string) []syntax.Expr {
	out := make([]syntax.Expr, len(params))
	var list []*syntax.Arg
	if e.Args != nil {
		list = e.Args.List
	}
	named := false
	pos := 0
	for _, a := range list {
		if a.Label == nil {
			if named {
				c.errorf(a, "a positional argument after a named one: put it first")
				continue
			}
			if pos >= len(params) {
				c.errorf(a, "too many arguments: %s takes %s", what, plural(len(params), "parameter", "parameters"))
				c.expr(a.Value, nil)
				continue
			}
			out[pos] = a.Value
			pos++
			continue
		}
		named = true
		i := paramIndex(params, a.Label.Name)
		if i < 0 {
			c.errorf(a.Label, "%s has no parameter %s%s", what, a.Label.Name, paramSuggest(params, a.Label.Name))
			c.expr(a.Value, nil)
			continue
		}
		if out[i] != nil {
			c.errorf(a.Label, "%s is given twice", a.Label.Name)
			continue
		}
		out[i] = a.Value
	}
	for i, p := range params {
		if out[i] == nil && p.Default == nil && !p.optional() {
			at := syntax.Node(e)
			if e.Args != nil {
				at = e.Args
			}
			c.errorf(at, "%s needs %s: %s", what, p.Name, typeString(p.Type))
		}
	}
	return out
}

// optional: a parameter that may be left out with no default: a
// builtin's optional argument.
func (p *Param) optional() bool { return p.Var != nil && p.Var.Kind == optionalParam }

const optionalParam VarKind = 200

func paramIndex(ps []*Param, name string) int {
	for i, p := range ps {
		if p.Name == name {
			return i
		}
	}
	return -1
}

func paramSuggest(ps []*Param, name string) string {
	for _, p := range ps {
		if editDistance(p.Name, name) <= 2 {
			return " (did you mean " + p.Name + "?)"
		}
	}
	return ""
}

// ---------------------------------------------------------------------
// Inference

// unify binds the free type parameters in p so that p fits a.
func (c *Checker) unify(p, a Type, s Subst, free map[*TypeParam]bool) bool {
	if IsInvalid(a) || a == Never {
		return true
	}
	switch x := p.(type) {
	case *TypeParam:
		if !free[x] {
			return Identical(p, a)
		}
		if a == NoneType {
			return true
		}
		if b, ok := s[x]; ok && b != nil {
			if Identical(b, a) {
				return true
			}
			if IsOptional(b) && Identical(Unopt(b), a) {
				return true
			}
			if IsOptional(a) && Identical(Unopt(a), b) {
				s[x] = a
				return true
			}
			return false
		}
		s[x] = a
		return true
	case *Array:
		y, ok := a.(*Array)
		return ok && c.unify(x.Elem, y.Elem, s, free)
	case *Map:
		y, ok := a.(*Map)
		return ok && c.unify(x.Key, y.Key, s, free) && c.unify(x.Elem, y.Elem, s, free)
	case *Optional:
		if a == NoneType {
			return true
		}
		if y, ok := a.(*Optional); ok {
			return c.unify(x.Elem, y.Elem, s, free)
		}
		return c.unify(x.Elem, a, s, free)
	case *Tuple:
		y, ok := a.(*Tuple)
		if !ok || len(y.Elems) != len(x.Elems) {
			return false
		}
		for i := range x.Elems {
			if !c.unify(x.Elems[i], y.Elems[i], s, free) {
				return false
			}
		}
		return true
	case *FuncType:
		y, ok := a.(*FuncType)
		if !ok || len(y.Params) != len(x.Params) {
			return false
		}
		for i := range x.Params {
			if !c.unify(x.Params[i], y.Params[i], s, free) {
				return false
			}
		}
		return c.unify(x.Result, y.Result, s, free)
	case *Task:
		y, ok := a.(*Task)
		return ok && c.unify(x.Elem, y.Elem, s, free)
	case *Named:
		y, ok := a.(*Named)
		if !ok || y.Obj != x.Obj || len(y.Args) != len(x.Args) {
			return false
		}
		for i := range x.Args {
			if !c.unify(x.Args[i], y.Args[i], s, free) {
				return false
			}
		}
		return true
	}
	return true
}

// mentionsFree reports whether t mentions a free type parameter.
func mentionsFree(t Type, free map[*TypeParam]bool, s Subst) bool {
	if len(free) == 0 {
		return false
	}
	t = s.Apply(t)
	found := false
	var walk func(Type)
	walk = func(t Type) {
		switch x := t.(type) {
		case *TypeParam:
			if free[x] {
				found = true
			}
		case *Array:
			walk(x.Elem)
		case *Map:
			walk(x.Key)
			walk(x.Elem)
		case *Optional:
			walk(x.Elem)
		case *Tuple:
			for _, e := range x.Elems {
				walk(e)
			}
		case *FuncType:
			for _, p := range x.Params {
				walk(p)
			}
			walk(x.Result)
		case *Task:
			walk(x.Elem)
		case *Named:
			for _, a := range x.Args {
				walk(a)
			}
		}
	}
	walk(t)
	return found
}

// inferArgs checks args against params whose types mention the free
// type parameters, binding them as it goes; it returns the bindings.
// Lambdas go last, so that their parameters' types are known.
func (c *Checker) inferArgs(args []syntax.Expr, params []*Param, s Subst, free map[*TypeParam]bool, want, result Type, what string) {
	savedFree := c.free
	c.free = free
	defer func() { c.free = savedFree }()
	// What's expected of the result binds first: json.decode, Box(…) as
	// a Box<int>.
	if want != nil && mentionsFree(result, free, s) {
		w := want
		if !IsOptional(result) {
			w = Unopt(want)
		}
		c.unify(result, w, s, free)
	}
	checked := make([]bool, len(args))
	types := make([]Type, len(args))
	for pass := 0; pass < 2; pass++ {
		for i, a := range args {
			if a == nil || checked[i] {
				continue
			}
			_, isLambda := a.(*syntax.LambdaExpr)
			if isLambda && pass == 0 {
				continue
			}
			pt := params[i].Type
			if !mentionsFree(pt, free, s) {
				c.assign(a, s.Apply(pt), "argument "+params[i].Name+" of "+what)
				checked[i] = true
				continue
			}
			pw := s.Apply(pt)
			w := pw
			if !isLambda && mentionsFree(w, free, s) {
				// What isn't known yet doesn't steer the argument.
				w = nil
			}
			got := c.expr(a, w)
			types[i] = got
			if !c.unify(pw, got, s, free) {
				c.errorf(a, "argument %s of %s is a %s, which doesn't fit %s", params[i].Name, what, typeString(got), typeString(s.Apply(pt)))
			}
			checked[i] = true
		}
	}
	for i, a := range args {
		if a == nil || types[i] == nil {
			continue
		}
		pt := s.Apply(params[i].Type)
		if mentionsFree(pt, free, s) {
			continue
		}
		if !c.convert(a, types[i], pt) {
			c.errorf(a, "argument %s of %s is a %s, not a %s", params[i].Name, what, typeString(types[i]), typeString(pt))
		}
	}
}

// ---------------------------------------------------------------------
// Functions and methods

func (c *Checker) funcCall(e *syntax.CallExpr, f *Function, recv syntax.Expr, recvType Type, want Type) Type {
	return c.funcCallInfo(e, f, recv, recvType, want).Result
}

func (c *Checker) funcCallInfo(e *syntax.CallExpr, f *Function, recv syntax.Expr, recvType Type, want Type) *Call {
	c.funcSig(f)
	c.noteCall(f)
	what := f.name
	params := f.Sig.Params
	args := c.matchArgs(e, params, what)
	c.trailingClosure(e, params, args)
	s := Subst{}
	if n, ok := recvType.(*Named); ok {
		for k, v := range n.Subst() {
			s[k] = v
		}
	}
	free := map[*TypeParam]bool{}
	for _, tp := range f.TypeParams {
		free[tp] = true
	}
	c.inferArgs(args, params, s, free, want, f.Sig.Result, what)
	call := &Call{Kind: CallFunc, Func: f, Args: args, Params: params, Recv: recv, RecvType: recvType}
	if f.Recv != nil {
		call.Kind = CallMethod
	}
	for _, tp := range f.TypeParams {
		t := s[tp]
		if t == nil {
			c.errorf(e, "can't tell %s's type %s from its arguments: pass them, or say what's expected (let x: T = %s(…))", what, tp.Name, what)
			t = Invalid
		} else if !satisfies(t, tp) {
			c.errorf(e, "%s's %s must be %s, and %s isn't", what, tp.Name, tp.Constraint, typeString(t))
		}
		call.TypeArgs = append(call.TypeArgs, t)
	}
	call.Result = s.Apply(f.Sig.Result)
	c.info.Calls[e] = call
	c.callEffects(e, what, f.Sig.Async, f.Sig.Throws)
	return call
}

// satisfies reports whether t meets the parameter's constraint.
func satisfies(t Type, p *TypeParam) bool {
	switch p.Constraint {
	case "Equatable":
		return IsEquatable(t)
	case "Comparable":
		return IsComparable(t)
	case "Hashable":
		return IsHashable(t)
	}
	return true
}

// trailingClosure makes a call's trailing block its last argument when
// that's a function: withTimeout(5s) { … }.
func (c *Checker) trailingClosure(e *syntax.CallExpr, params []*Param, args []syntax.Expr) {
	if e.Block == nil {
		return
	}
	n := len(params)
	if n == 0 || args[n-1] != nil {
		c.errorf(e.Block, "a block after this call has nowhere to go: it takes no function last")
		return
	}
	if _, ok := params[n-1].Type.(*FuncType); !ok {
		c.errorf(e.Block, "a block after this call has nowhere to go: its last parameter isn't a function")
		return
	}
	lam := &syntax.LambdaExpr{Span: e.Block.Span, Body: e.Block}
	args[n-1] = lam
	c.info.Trailing[e] = lam
}

// callEffects checks that a call to an async or throwing function is
// awaited or tried.
func (c *Checker) callEffects(at syntax.Node, what string, async, throws bool) {
	if async {
		if c.inAwait == 0 {
			if c.ctx != nil && c.ctx.Async {
				c.errorf(at, "%s is async: await it (await %s(…)), or start it with spawn", what, what)
			} else {
				c.errorf(at, "%s is async, and this can't wait: %s", what, c.asyncHint())
			}
		}
	}
	if throws && c.inTry == 0 {
		c.errorf(at, "%s can throw: call it with try (try %s(…)), or try? for none on an error", what, what)
		c.mayThrow(at)
	}
}

func (c *Checker) closureCall(e *syntax.CallExpr, fun syntax.Expr, t Type) Type {
	if IsInvalid(t) {
		c.checkArgsLoosely(e)
		return Invalid
	}
	ft, ok := t.(*FuncType)
	if !ok {
		c.errorf(fun, "%s is a %s, not a function", exprName(fun), typeString(t))
		c.checkArgsLoosely(e)
		return Invalid
	}
	var args []syntax.Expr
	if e.Args != nil {
		for i, a := range e.Args.List {
			if a.Label != nil {
				c.errorf(a.Label, "a function value's arguments have no names")
			}
			if i < len(ft.Params) {
				c.assign(a.Value, ft.Params[i], "argument "+itoa(i+1))
			} else {
				c.expr(a.Value, nil)
			}
			args = append(args, a.Value)
		}
	}
	if len(args) != len(ft.Params) {
		c.errorf(e, "%s takes %s, not %d", exprName(fun), plural(len(ft.Params), "argument", "arguments"), len(args))
	}
	c.info.Calls[e] = &Call{Kind: CallClosure, Recv: fun, Args: args, Result: ft.Result, RecvType: ft}
	c.callEffects(e, exprName(fun), ft.Async, ft.Throws)
	if c.ctx != nil {
		c.ctx.readsState = true // what it does isn't known here
	}
	return ft.Result
}

// methodCall is x.name(…) on a value.
func (c *Checker) methodCall(e *syntax.CallExpr, fn *syntax.MemberExpr, want Type) Type {
	name := fn.Name.Name
	var xt Type
	if implicitRoot(fn.X) {
		xt = c.expr(fn.X, want)
	} else {
		xt = c.expr(fn.X, nil)
	}
	if IsInvalid(xt) {
		c.checkArgsLoosely(e)
		return Invalid
	}
	optional := false
	if fn.Optional {
		if !IsOptional(xt) {
			c.errorf(fn, "%s isn't optional: use . (?. is for optionals)", typeString(xt))
		}
		xt = Unopt(xt)
		optional = true
	} else if IsOptional(xt) && lookupMethod(xt, name) == nil {
		c.errorf(fn, "%s may be none: use ?. (x?.%s()), or unwrap it first", typeString(xt), name)
		c.checkArgsLoosely(e)
		return Invalid
	}
	res := c.methodCall1(e, fn, xt, name, want)
	if call := c.info.Calls[e]; call != nil {
		call.Optional = optional
	}
	if optional && !IsInvalid(res) && res != Void {
		return MakeOptional(res)
	}
	return res
}

func (c *Checker) methodCall1(e *syntax.CallExpr, fn *syntax.MemberExpr, xt Type, name string, want Type) Type {
	if n, ok := xt.(*Named); ok {
		switch n.Obj.Kind {
		case StructType, EnumType:
			if f := n.Obj.Methods[name]; f != nil {
				return c.funcCall(e, f, fn.X, xt, want)
			}
			if n.Obj.Kind == StructType {
				if fl := n.Obj.Field(name); fl != nil {
					if ft, ok := n.Subst().Apply(fl.Type).(*FuncType); ok {
						c.info.Selections[fn] = &Selection{Kind: SelField, Field: fl, Recv: xt}
						c.record(fn, ft)
						return c.closureCall(e, fn, ft)
					}
				}
			}
		case StoreType:
			if m := n.Obj.Comp.Members[name]; m != nil {
				if n.Obj.Pkg != c.pkg && !m.Pub {
					c.errorf(fn.Name, "%s isn't pub in store %s", name, n.Obj.name)
				}
				if m.Kind == FnMember {
					c.spawnState(fn, n.Obj.name+"."+name)
					if c.ctx != nil {
						c.ctx.readsState = true
					}
					return c.funcCall(e, m.Fn, fn.X, xt, want)
				}
				t := c.valueMember(fn, xt, name)
				c.record(fn, t)
				return c.closureCall(e, fn, t)
			}
		case InterfaceType:
			for _, m := range n.Obj.Iface.Methods {
				if m.Name == name {
					args := c.matchArgs(e, m.Sig.Params, name)
					for i, a := range args {
						if a != nil {
							c.assign(a, m.Sig.Params[i].Type, "argument "+m.Sig.Params[i].Name+" of "+name)
						}
					}
					c.info.Calls[e] = &Call{Kind: CallIface, Recv: fn.X, RecvType: xt, Args: args, Params: m.Sig.Params, Result: m.Sig.Result, Bound: map[string]Type{"name": nil}}
					c.info.Selections[fn] = &Selection{Kind: SelMethod, Name: name, Recv: xt}
					c.callEffects(e, name, m.Sig.Async, m.Sig.Throws)
					if c.ctx != nil {
						c.ctx.readsState = true
					}
					return m.Sig.Result
				}
			}
		}
	}
	if m := lookupMethodArity(xt, name, argCount(e)); m != nil {
		return c.builtinMethodCall(e, fn, xt, m, want)
	}
	if lookupMethod(xt, name) != nil {
		m := lookupMethod(xt, name)
		c.errorf(e, "%s takes %s", name, plural(len(m.Params), "argument", "arguments"))
		c.checkArgsLoosely(e)
		return Invalid
	}
	c.errorf(fn.Name, "%s has no method %s%s", typeString(xt), name, c.memberSuggest(xt, name))
	c.checkArgsLoosely(e)
	return Invalid
}

func argCount(e *syntax.CallExpr) int {
	if e.Args == nil {
		return 0
	}
	return len(e.Args.List)
}

// builtinMethodCall is a built-in type's method: [T].map, string.trim.
func (c *Checker) builtinMethodCall(e *syntax.CallExpr, fn *syntax.MemberExpr, xt Type, m *Method, want Type) Type {
	params := m.params()
	args := c.matchArgs(e, params, m.Name)
	s := m.bind(xt)
	free := map[*TypeParam]bool{tvU: true}
	c.inferArgs(args, params, s, free, want, m.Result, m.Name)
	if why := m.requires(s); why != "" {
		c.errorf(fn.Name, "%s %s", m.Name, why)
	}
	bound := map[string]Type{}
	for tp, t := range s {
		bound[tp.Name] = t
	}
	if m.Result != nil && mentionsFree(m.Result, free, s) {
		c.errorf(e, "can't tell what %s returns", m.Name)
	}
	res := s.Apply(m.Result)
	c.info.Calls[e] = &Call{Kind: CallBuiltinMethod, Method: m, Recv: fn.X, RecvType: xt, Args: args, Params: params, Result: res, Bound: bound}
	c.info.Selections[fn] = &Selection{Kind: SelBuiltinMethod, Name: m.Name, Recv: xt}
	if m.Inout {
		c.mutates(fn.X, m.Name)
	}
	c.callEffects(e, m.Name, m.Async, m.Throws)
	return res
}

// mutates checks that x, which a method changes, can be changed.
func (c *Checker) mutates(x syntax.Expr, what string) {
	c.lvalue(x, "call "+what+" on")
}

// ---------------------------------------------------------------------
// Types called: structs, stores, components, cases

func (c *Checker) typeCall(e *syntax.CallExpr, tn *TypeName, want Type) Type {
	c.resolveType(tn)
	switch tn.Kind {
	case StructType:
		return c.structCall(e, tn, want)
	case StoreType:
		return c.storeCall(e, tn)
	case ComponentType:
		return c.componentCall(e, tn)
	case InterfaceType:
		if tn == c.errT {
			ps := []*Param{{Name: "message", Type: String}}
			args := c.matchArgs(e, ps, "Error")
			if args[0] != nil {
				c.assign(args[0], String, "the message")
			}
			c.info.Calls[e] = &Call{Kind: CallBuiltin, Builtin: errorBuiltin, Args: args, Params: ps, Result: c.errT.Type}
			return c.errT.Type
		}
		c.errorf(e.Fun, "%s is an interface: make a struct or an enum that has it", tn.name)
	case EnumType:
		c.errorf(e.Fun, "%s is an enum: make one of its cases, %s.name", tn.name, tn.name)
	case BasicType:
		switch tn.Type {
		case Float, Int:
			return c.builtinCall(e, &Builtin{object: object{name: tn.name}, ID: map[Type]BuiltinID{Float: BFloat, Int: BInt}[tn.Type]}, want)
		}
		c.errorf(e.Fun, "%s is a type, not a function", tn.name)
	default:
		c.errorf(e.Fun, "%s is a type, not a function", tn.name)
	}
	c.checkArgsLoosely(e)
	return Invalid
}

var errorBuiltin = &Builtin{object: object{name: "Error"}, ID: BError}

// fieldParams are a struct's fields as a constructor's parameters.
func fieldParams(tn *TypeName) []*Param {
	ps := make([]*Param, len(tn.Fields))
	for i, f := range tn.Fields {
		ps[i] = &Param{Name: f.Name, Type: f.Type, Default: f.Default}
	}
	return ps
}

func (c *Checker) structCall(e *syntax.CallExpr, tn *TypeName, want Type) Type {
	if e.Block != nil {
		c.errorf(e.Block, "%s is a struct: its fields go in parentheses, %s(name: value)", tn.name, tn.name)
	}
	params := fieldParams(tn)
	args := c.matchArgs(e, params, tn.name)
	s := Subst{}
	free := map[*TypeParam]bool{}
	for _, tp := range tn.TypeParams {
		free[tp] = true
	}
	c.inferArgs(args, params, s, free, want, tn.Type, tn.name)
	n := &Named{Obj: tn}
	for _, tp := range tn.TypeParams {
		t := s[tp]
		if t == nil {
			c.errorf(e, "can't tell %s's %s: pass its fields, or say what's expected (let x: %s<…> = …)", tn.name, tp.Name, tn.name)
			t = Invalid
		}
		n.Args = append(n.Args, t)
	}
	var t Type = n
	if len(n.Args) == 0 {
		t = tn.Type
	}
	c.info.Calls[e] = &Call{Kind: CallStruct, Type: n, Args: args, Params: params, Result: t}
	return t
}

func (c *Checker) caseCall(e *syntax.CallExpr, tn *TypeName, k *Case, want Type) Type {
	if len(k.Params) == 0 {
		c.errorf(e, "case %s of %s has no payload: write it without parentheses", k.Name, tn.name)
		return tn.Type
	}
	params := make([]*Param, len(k.Params))
	for i, p := range k.Params {
		name := k.Labels[i]
		if name == "" {
			name = "_" + itoa(i)
		}
		params[i] = &Param{Name: name, Type: p}
	}
	args := c.matchArgs(e, params, tn.name+"."+k.Name)
	s := Subst{}
	free := map[*TypeParam]bool{}
	for _, tp := range tn.TypeParams {
		free[tp] = true
	}
	c.inferArgs(args, params, s, free, want, tn.Type, tn.name+"."+k.Name)
	n := &Named{Obj: tn}
	for _, tp := range tn.TypeParams {
		t := s[tp]
		if t == nil {
			c.errorf(e, "can't tell %s's %s: say what's expected (let x: %s<…> = …)", tn.name, tp.Name, tn.name)
			t = Invalid
		}
		n.Args = append(n.Args, t)
	}
	var t Type = n
	if len(n.Args) == 0 {
		t = tn.Type
	}
	c.info.Calls[e] = &Call{Kind: CallCase, Type: n, Case: k, Args: args, Params: params, Result: t}
	return t
}

func (c *Checker) storeCall(e *syntax.CallExpr, tn *TypeName) Type {
	if e.Block != nil {
		c.errorf(e.Block, "%s is a store: its parameters go in parentheses", tn.name)
	}
	var params []*Param
	for _, m := range tn.Comp.Params {
		params = append(params, &Param{Name: m.name, Type: m.Type, Default: m.Default})
	}
	args := c.matchArgs(e, params, tn.name)
	for i, a := range args {
		if a != nil {
			c.bindingArg(a, params[i].Type, "argument "+params[i].Name+" of "+tn.name)
		}
	}
	c.info.Calls[e] = &Call{Kind: CallStore, Type: tn.Type.(*Named), Args: args, Params: params, Result: tn.Type}
	return tn.Type
}

// bindingArg checks an argument the callee follows (an accessor): it's
// a binding of its own.
func (c *Checker) bindingArg(a syntax.Expr, want Type, what string) {
	saved := c.ctx
	c.enter(CtxBinding, a, "binding")
	c.assign(a, want, what)
	c.leave(saved)
}

// ---------------------------------------------------------------------
// Lambdas and spawn

func (c *Checker) lambda(e *syntax.LambdaExpr, want Type) Type {
	wf, _ := Unopt(want).(*FuncType)
	saved := c.ctx
	savedTry, savedAwait, savedHandled, savedLoops, savedView := c.inTry, c.inAwait, c.handled, c.loops, c.view
	c.inTry, c.inAwait, c.handled, c.loops, c.view = 0, 0, 0, 0, false
	ctx := c.enter(CtxLambda, e, "closure")
	ft := &FuncType{Result: Void}
	if wf != nil && len(wf.Params) != len(e.Params) {
		c.errorf(e, "this closure takes %s, where one taking %s is expected", plural(len(e.Params), "parameter", "parameters"), plural(len(wf.Params), "parameter", "parameters"))
		wf = nil
	}
	for i, p := range e.Params {
		var t Type
		switch {
		case p.Type != nil:
			t = c.typeExpr(p.Type)
		case wf != nil && !mentionsFree(wf.Params[i], c.free, nil):
			t = wf.Params[i]
		default:
			c.errorf(p.Name, "can't tell %s's type: write it, (%s: T) => …", p.Name.Name, p.Name.Name)
			t = Invalid
		}
		ft.Params = append(ft.Params, t)
		ctx.Params = append(ctx.Params, c.declareVar(p.Name, t, false))
	}
	if wf != nil {
		ctx.Async, ctx.Throws = wf.Async, wf.Throws
		ft.Async, ft.Throws = wf.Async, wf.Throws
	} else {
		ctx.Async, ctx.Throws = true, true
	}
	infer := wf == nil || mentionsFree(wf.Result, c.free, nil)
	if !infer {
		ctx.Result = wf.Result
	} else {
		ctx.inferRes = true
	}
	switch body := e.Body.(type) {
	case syntax.Expr:
		if infer {
			ft.Result = c.expr(body, nil)
			if ft.Result == NoneType {
				ft.Result = Invalid
			}
		} else if ctx.Result == Void {
			c.expr(body, nil)
		} else {
			c.assign(body, ctx.Result, "the closure's result")
			ft.Result = ctx.Result
		}
	case *syntax.Block:
		c.stmts(body.Items)
		if infer {
			ft.Result = Void
			for _, r := range ctx.retTypes {
				if j := c.join(ft.Result, r); j != nil && ft.Result != Void {
					ft.Result = j
				} else {
					ft.Result = r
				}
			}
			if len(ctx.retTypes) > 0 && !c.terminates(body.Items) {
				c.errorPos(body.Rbrace, "this closure returns a value, but can reach its end without one")
			}
		} else {
			ft.Result = ctx.Result
			if ctx.Result != Void && !c.terminates(body.Items) {
				c.errorPos(body.Rbrace, "this closure must return a %s: it can reach its end without a return", typeString(ctx.Result))
			}
		}
	}
	if wf == nil {
		ft.Async = ctx.Awaits
		ft.Throws = ctx.threw
	}
	ctx.Result = ft.Result
	c.leave(saved)
	c.inTry, c.inAwait, c.handled, c.loops, c.view = savedTry, savedAwait, savedHandled, savedLoops, savedView
	return ft
}

func (c *Checker) spawn(e *syntax.SpawnExpr) Type {
	saved := c.ctx
	savedTry, savedAwait, savedHandled, savedLoops, savedView := c.inTry, c.inAwait, c.handled, c.loops, c.view
	c.inTry, c.inAwait, c.handled, c.loops, c.view = 0, 0, 0, 0, false
	ctx := c.enter(CtxSpawn, e, "spawn")
	ctx.Throws = true
	ctx.inferRes = true
	ctx.Comp = nil
	items := e.Body.Items
	var res Type = Void
	if n := len(items); n > 0 {
		if es, ok := items[n-1].(*syntax.ExprStmt); ok && !hasReturnValue(items) {
			c.stmts(items[:n-1])
			res = c.expr(es.X, nil)
			c.info.Ctxs[es] = ctx // the block's value is its last expression's
		} else {
			c.stmts(items)
			for _, r := range ctx.retTypes {
				res = r
			}
		}
	}
	if !sendable(res) {
		c.errorf(e, "spawn's result is a %s, which can't cross threads: return values only", typeString(res))
	}
	ctx.Result = res
	c.leave(saved)
	c.inTry, c.inAwait, c.handled, c.loops, c.view = savedTry, savedAwait, savedHandled, savedLoops, savedView
	c.spawns = append(c.spawns, ctx)
	return &Task{res}
}

func hasReturnValue(items []syntax.Item) bool {
	found := false
	for _, it := range items {
		syntax.Inspect(it, func(n syntax.Node) bool {
			switch n := n.(type) {
			case *syntax.LambdaExpr, *syntax.SpawnExpr:
				return false
			case *syntax.ReturnStmt:
				if n.Value != nil {
					found = true
				}
			}
			return !found
		})
	}
	return found
}

// sendable reports whether values of t can cross to another thread:
// values only, no state, stores, views, tasks or closures.
func sendable(t Type) bool {
	return sendable1(t, map[*TypeName]bool{})
}

func sendable1(t Type, seen map[*TypeName]bool) bool {
	switch x := t.(type) {
	case *Basic:
		return x != View && x != TestApp
	case *Array:
		return sendable1(x.Elem, seen)
	case *Map:
		return sendable1(x.Elem, seen)
	case *Optional:
		return sendable1(x.Elem, seen)
	case *Tuple:
		for _, e := range x.Elems {
			if !sendable1(e, seen) {
				return false
			}
		}
		return true
	case *FuncType, *Task, *Loading:
		return false
	case *TypeParam:
		return true
	case *Named:
		if seen[x.Obj] {
			return true
		}
		seen[x.Obj] = true
		s := x.Subst()
		switch x.Obj.Kind {
		case StructType:
			for _, f := range x.Obj.Fields {
				if !sendable1(s.Apply(f.Type), seen) {
					return false
				}
			}
			return true
		case EnumType:
			if x.Obj == loadingT {
				return false
			}
			for _, k := range x.Obj.Cases {
				for _, p := range k.Params {
					if !sendable1(s.Apply(p), seen) {
						return false
					}
				}
			}
			return true
		case InterfaceType:
			return true
		}
		return false
	}
	return true
}

func itoa(i int) string {
	const digits = "0123456789"
	if i < 10 && i >= 0 {
		return digits[i : i+1]
	}
	neg := i < 0
	if neg {
		i = -i
	}
	var b []byte
	for i > 0 {
		b = append([]byte{digits[i%10]}, b...)
		i /= 10
	}
	if neg {
		b = append([]byte{'-'}, b...)
	}
	return string(b)
}
