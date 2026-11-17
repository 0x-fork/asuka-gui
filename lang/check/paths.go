package check

import "github.com/egoist/asuka/lang/syntax"

// Paths (rfcs/0013). A read of state follows its path: user.name
// follows user's name, todos[i].done that element's done,
// todos.count() the length, cart[sku] that key, picked.contains(id)
// that member. A read is the longest path its expression spells: the
// lowering, which goes from the outside in, asks for the path of each
// expression it meets, and reads one that's a path at once (CPATH, or
// AGETP through an accessor), its prefixes with it.
//
// What stops a path: a fn's argument, an interpolation, an operator, a
// comparison, a match's subject, an enum's payload, an error's fields,
// a default (m[k, default: v]), an index or a key that calls something
// (it's computed before the state is read, so it mustn't see it
// change), and a ?. chain past its first optional (x?.a.b's b).

// StepKind is what a path's step is.
type StepKind uint8

const (
	StepField    StepKind = iota + 1 // .f (Field)
	StepOptField                     // x?.f: none when x is
	StepIndex                        // xs[i]: out of range throws
	StepGet                          // xs.get(i), xs.first() (Arg nil: 0): optional
	StepKey                          // m[k], m.get(k): optional
	StepHas                          // m.has(k): a bool, last
	StepMember                       // s.contains(x): a bool, last
	StepLen                          // xs.count(), xs.isEmpty() (Empty): last
)

// A PathStep is one step of a path.
type PathStep struct {
	Kind  StepKind
	Field *Field      // StepField, StepOptField: its Index (a tuple's element too)
	Arg   syntax.Expr // the index, the key, the member
	Empty bool        // StepLen: isEmpty(), the length compared with 0
	Expr  syntax.Expr // the expression the path ends at, with this step
}

// PathRoot is where a path starts.
type PathRoot uint8

const (
	// RootState: a state's cell: a component's or a store's state
	// (Member), a file's (Global), or a store's read through it (a
	// SelStoreMember: Expr's X is the store).
	RootState PathRoot = iota + 1
	// RootAcc: what an accessor reads: a component's parameter or prop,
	// a for's row's item, a let that's a path (Member.Path).
	RootAcc
)

// A StatePath is a state, or what an accessor reads, then steps.
type StatePath struct {
	Root   PathRoot
	Expr   syntax.Expr // the root's expression: an Ident, or a MemberExpr of a store's member
	Member *Member     // the state, parameter, prop or let (nil: a global's, a row's item)
	Global *Global
	Var    *Var // a row's item
	Steps  []PathStep
}

// StatePathOf is the path e reads with at least one step, or nil.
func (p *Program) StatePathOf(e syntax.Expr) *StatePath { return p.checker.path(e) }

// PathRootOf is what e is, when it's a path's root alone (a state, a
// parameter, a row's item): a path of no steps; nil otherwise.
func (p *Program) PathRootOf(e syntax.Expr) *StatePath { return p.checker.pathRoot(e) }

func (c *Checker) path(e syntax.Expr) *StatePath {
	var steps []PathStep
	x := e
	for {
		var s PathStep
		cur := x
		switch n := x.(type) {
		case *syntax.ParenExpr:
			x = n.X
			continue
		case *syntax.MemberExpr:
			sel := c.info.Selections[n]
			if sel == nil {
				return nil
			}
			if sel.Kind != SelField && sel.Kind != SelTupleElem {
				return c.finishPath(x, steps)
			}
			if named, ok := Unopt(sel.Recv).(*Named); ok && named.Obj.IsError {
				return nil
			}
			xt := c.info.Types[n.X]
			switch {
			case sel.Optional && IsOptional(xt):
				s = PathStep{Kind: StepOptField, Field: sel.Field}
			case IsOptional(xt) || sel.Optional:
				return nil
			default:
				s = PathStep{Kind: StepField, Field: sel.Field}
			}
			x = n.X
		case *syntax.IndexExpr:
			if n.Default != nil || IsOptional(c.info.Types[n.X]) || !simpleArg(c, n.Index) {
				return nil
			}
			switch t := c.info.Types[n.X].(type) {
			case *Array:
				s = PathStep{Kind: StepIndex, Arg: n.Index}
			case *Map:
				if !keyKind(t.Key) {
					return nil
				}
				s = PathStep{Kind: StepKey, Arg: n.Index}
			default:
				return nil
			}
			x = n.X
		case *syntax.CallExpr:
			call := c.info.Calls[n]
			if call == nil || call.Kind != CallBuiltinMethod || call.Optional || call.Method == nil || call.Recv == nil {
				return nil
			}
			if IsOptional(c.info.Types[call.Recv]) {
				return nil
			}
			var arg syntax.Expr
			if len(call.Args) > 0 {
				arg = call.Args[0]
				if arg == nil || !simpleArg(c, arg) {
					return nil
				}
			}
			m := call.Method
			switch m.Recv + "." + m.Name {
			case "array.count", "map.count", "set.count":
				if arg != nil {
					return nil // count(where): a closure's (methods' loops, ir)
				}
				s = PathStep{Kind: StepLen}
			case "array.isEmpty", "map.isEmpty", "set.isEmpty":
				s = PathStep{Kind: StepLen, Empty: true}
			case "array.first":
				s = PathStep{Kind: StepGet}
			case "array.get":
				s = PathStep{Kind: StepGet, Arg: arg}
			case "map.get":
				if mt, ok := c.info.Types[call.Recv].(*Map); !ok || !keyKind(mt.Key) {
					return nil
				}
				s = PathStep{Kind: StepKey, Arg: arg}
			case "map.has":
				if mt, ok := c.info.Types[call.Recv].(*Map); !ok || !keyKind(mt.Key) {
					return nil
				}
				s = PathStep{Kind: StepHas, Arg: arg}
			case "set.contains":
				if st, ok := c.info.Types[call.Recv].(*Set); !ok || !keyKind(st.Elem) {
					return nil
				}
				s = PathStep{Kind: StepMember, Arg: arg}
			default:
				return nil
			}
			x = call.Recv
		default:
			return c.finishPath(x, steps)
		}
		s.Expr = cur
		steps = append(steps, s)
	}
}

// finishPath is the path from root, the steps found (outermost first),
// or nil when root isn't one's root or there are none.
func (c *Checker) finishPath(root syntax.Expr, steps []PathStep) *StatePath {
	if len(steps) == 0 {
		return nil
	}
	p := c.pathRoot(root)
	if p == nil {
		return nil
	}
	// Found outermost first: reversed, the root's step first.
	out := make([]PathStep, len(steps))
	for i, s := range steps {
		out[len(steps)-1-i] = s
	}
	p.Steps = out
	return p
}

func (c *Checker) pathRoot(e syntax.Expr) *StatePath {
	switch n := unparenExpr(e).(type) {
	case *syntax.Ident:
		switch o := c.info.Uses[n].(type) {
		case *Member:
			if o.Owner != nil && o.Owner.Comp != nil && o.Owner.Comp.Worker {
				return nil
			}
			switch o.Kind {
			case StateMember:
				return &StatePath{Root: RootState, Expr: n, Member: o}
			case ParamMember, PropMember:
				if o.IsSlot() {
					return nil
				}
				return &StatePath{Root: RootAcc, Expr: n, Member: o}
			case LetMember:
				if o.Path {
					return &StatePath{Root: RootAcc, Expr: n, Member: o}
				}
			}
		case *Global:
			if o.Kind == StateGlobal {
				return &StatePath{Root: RootState, Expr: n, Global: o}
			}
		case *Var:
			if o.Kind == RowItem && !o.Boxed() {
				return &StatePath{Root: RootAcc, Expr: n, Var: o}
			}
		}
	case *syntax.MemberExpr:
		sel := c.info.Selections[n]
		if sel == nil {
			return nil
		}
		if sel.Optional {
			return nil
		}
		switch sel.Kind {
		case SelStoreMember:
			m := sel.Member
			if m.Owner != nil && m.Owner.Comp != nil && m.Owner.Comp.Worker {
				return nil
			}
			switch {
			case m.Kind == StateMember:
				return &StatePath{Root: RootState, Expr: n, Member: m}
			case m.Kind == ParamMember:
				return &StatePath{Root: RootAcc, Expr: n, Member: m}
			case m.Kind == LetMember && m.Path:
				return &StatePath{Root: RootAcc, Expr: n, Member: m}
			}
		case SelPkg:
			if g, ok := sel.Obj.(*Global); ok && g.Kind == StateGlobal {
				return &StatePath{Root: RootState, Expr: n, Global: g}
			}
		}
	}
	return nil
}

func unparenExpr(e syntax.Expr) syntax.Expr {
	for {
		p, ok := e.(*syntax.ParenExpr)
		if !ok {
			return e
		}
		e = p.X
	}
}

// keyKind reports whether t's values are keys a path's node holds: ints
// (and what's stored as one) or strings.
func keyKind(t Type) bool {
	switch t := t.(type) {
	case *Basic:
		return t == Int || t == String || t == Bool
	case *Named:
		return t.Obj.Kind == EnumType && !t.Obj.RefEnum()
	}
	return false
}

// simpleArg reports whether e, an index or a key, calls nothing: what
// it reads is read before the state is, so it mustn't change it.
func simpleArg(c *Checker, e syntax.Expr) bool {
	ok := true
	syntax.Inspect(e, func(n syntax.Node) bool {
		if !ok {
			return false
		}
		switch n.(type) {
		case *syntax.CallExpr, *syntax.LambdaExpr, *syntax.SpawnExpr, *syntax.IfExpr, *syntax.MatchExpr, *syntax.CatchExpr, *syntax.JumpExpr, *syntax.UnwrapExpr:
			ok = false
		case *syntax.UnaryExpr:
			if u := n.(*syntax.UnaryExpr); u.Op == syntax.AWAIT {
				ok = false
			}
		case *syntax.BinaryExpr:
			if c.info.Operators[n.(*syntax.BinaryExpr)] != nil {
				ok = false // a type's operator: a method's call
			}
		}
		return ok
	})
	return ok
}
