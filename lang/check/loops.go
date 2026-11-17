package check

import (
	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// Loops' names and iteration over the program's own types (RFC 0011).

// A LoopName is a loop's name: for … as name.
type LoopName struct {
	object
	Loop syntax.Node // the *ForStmt or *WhileStmt
}

// A loopLabel is a loop named for … as name (or while … as name), in
// effect while its body is checked.
type loopLabel struct {
	obj     *LoopName
	id      *syntax.Ident
	loop    syntax.Node
	ctx     *FuncCtx
	inDefer int
	used    bool
}

// loopBody checks a loop's body, the loop named id (nil: unnamed).
func (c *Checker) loopBody(loop syntax.Node, id *syntax.Ident, body func()) {
	var l *loopLabel
	if id != nil {
		for _, o := range c.labels {
			if o.id.Name == id.Name && o.ctx == c.ctx {
				c.errorf(id, diag.LoopLabel, "%s names a loop around this one already: give this one another name", id.Name)
			}
		}
		l = &loopLabel{obj: &LoopName{object: object{id.Name, id.Start}, Loop: loop}, id: id, loop: loop, ctx: c.ctx, inDefer: c.inDefer}
		c.info.Defs[id] = l.obj
		c.labels = append(c.labels, l)
	}
	c.loops++
	body()
	c.loops--
	if l != nil {
		c.labels = c.labels[:len(c.labels)-1]
		if !l.used {
			c.errorf(id, diag.LoopLabel, "%s names this loop, and no break or continue says it: drop as %s", id.Name, id.Name)
		}
	}
}

// branchLabel resolves break's or continue's name: the loop it leaves,
// or goes on with.
func (c *Checker) branchLabel(tok syntax.Token, id *syntax.Ident) {
	for i := len(c.labels) - 1; i >= 0; i-- {
		l := c.labels[i]
		if l.id.Name != id.Name {
			continue
		}
		l.used = true
		c.info.Uses[id] = l.obj
		switch {
		case l.ctx != c.ctx:
			c.errorf(id, diag.LoopLabel, "%s %s is in a closure: it can't leave the loop around the closure; return a value that says to, and %s there", tok, id.Name, tok)
		case l.inDefer != c.inDefer:
			c.errorf(id, diag.LoopLabel, "%s %s is in a defer, which runs to its end", tok, id.Name)
		default:
			c.info.Labels[id] = l.loop
		}
		return
	}
	c.errorf(id, diag.LoopLabel, "no loop around this is named %s: name one, for … as %s", id.Name, id.Name)
}

// forIter checks for x in v over a value of the program's types: an
// iterator, whose type has next() -> T? (a mut fn, as Iterator<T>
// asks), or a sequence, whose iter() returns one. The loop changes a
// copy of the iterator, a hidden var, as a mut fn's call: false when t
// is neither.
func (c *Checker) forIter(s *syntax.ForStmt, fi *ForInfo, t Type) bool {
	elem, ok := c.iteratorElem(t)
	var iter Type
	if !ok {
		sig, s := c.methodOf(t, "iter")
		if sig == nil || len(sig.Params) > 0 && sig.Params[0].Default == nil || sig.Async {
			return false
		}
		iter = s.Apply(sig.Result)
		if elem, ok = c.iteratorElem(iter); !ok {
			return false
		}
	}
	// The hidden vars: the sequence (for iter()), and the iterator.
	hidden := func(name string, t Type) (*Var, *syntax.Ident) {
		id := &syntax.Ident{Span: s.Seq.Range(), Name: name}
		v := c.declareVar(id, t, true)
		return v, id
	}
	fi.Kind, fi.Elem = ForIter, elem
	if iter != nil {
		v, id := hidden("\x00seq", t)
		fi.SeqVar = v
		fi.IterCall = &syntax.CallExpr{Span: s.Seq.Range(), Fun: &syntax.MemberExpr{Span: s.Seq.Range(), X: id, Name: &syntax.Ident{Span: s.Seq.Range(), Name: "iter"}}}
		c.expr(fi.IterCall, nil)
		t = iter
	}
	v, id := hidden("\x00iter", t)
	fi.IterVar = v
	fi.Next = &syntax.CallExpr{Span: s.Seq.Range(), Fun: &syntax.MemberExpr{Span: s.Seq.Range(), X: id, Name: &syntax.Ident{Span: s.Seq.Range(), Name: "next"}}}
	c.expr(fi.Next, nil)
	return true
}

// iteratorElem is what an iterator of type t gives: its next()'s
// result, without the optional.
func (c *Checker) iteratorElem(t Type) (Type, bool) {
	sig, s := c.methodOf(t, "next")
	if sig == nil || sig.Async || len(sig.Params) > 0 && sig.Params[0].Default == nil {
		return nil, false
	}
	r := s.Apply(sig.Result)
	if !IsOptional(r) {
		return nil, false
	}
	return Unopt(r), true
}

// methodOf is t's method name's signature, read with the substitution
// it takes (its type's arguments, its extension's): its own, an
// extension's, an interface's, a type parameter's constraints'.
func (c *Checker) methodOf(t Type, name string) (*Signature, Subst) {
	switch x := t.(type) {
	case *Named:
		switch x.Obj.Kind {
		case StructType, EnumType:
			if f := x.Obj.Methods[name]; f != nil {
				c.funcSig(f)
				return f.Sig, x.Subst()
			}
		case InterfaceType:
			if m, _ := ifaceMethod(x, name); m != nil {
				return m.Sig, nil
			}
			return nil, nil
		}
	case *TypeParam:
		if m, _ := paramMethod(x, name); m != nil {
			return m.Sig, nil
		}
		return nil, nil
	}
	if hit, _ := c.extLookup(t, name, "method", nil); hit.f != nil {
		c.funcSig(hit.f)
		return hit.f.Sig, hit.s
	}
	return nil, nil
}
