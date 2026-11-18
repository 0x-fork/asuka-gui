package check

import (
	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// What asuka test adds to test blocks: property tests (a test's
// parameters, whose values it generates), benches and their measure
// block, keep, and snapshots (docs/testing.md).

// testParams declares a property test's parameters in its scope: each of
// a type asuka test generates values of (Generable).
func (c *Checker) testParams(t *Test) {
	if t.Decl == nil || t.Decl.Params == nil {
		return
	}
	for _, p := range t.Decl.Params.List {
		var pt Type = Invalid
		if p.Type != nil {
			pt = c.typeExpr(p.Type)
		}
		if p.Default != nil {
			c.errorf(p.Default, diag.TestParameter, "a test's parameter has no default: asuka test gives it every value it generates")
		}
		if why := Generable(pt); why != "" {
			c.errorf(p.Type, diag.TestParameter, "asuka test can't generate %s for %s: %s", aType(pt), p.Name.Name, why)
		}
		v := c.declareVar(p.Name, pt, false)
		t.Params = append(t.Params, v)
		c.ctx.Params = append(c.ctx.Params, v)
	}
}

// Generable says why asuka test can't generate values of t ("" when it
// can): ints, floats, bools, strings, bytes, durations, and the arrays,
// maps from strings, sets, optionals, structs and enums of those, which
// it also reads back from a fuzzing corpus's JSON.
func Generable(t Type) string { return generable(t, map[*TypeName]bool{}) }

func generable(t Type, seen map[*TypeName]bool) string {
	switch x := t.(type) {
	case *Basic:
		switch x {
		case Int, Float, Bool, String, Bytes, Duration, Invalid:
			return ""
		}
		return "it's a " + x.name
	case *Array:
		return generable(x.Elem, seen)
	case *Optional:
		return generable(x.Elem, seen)
	case *Set:
		return generable(x.Elem, seen)
	case *Map:
		if x.Key != String {
			return "a map's keys are strings, as JSON's are"
		}
		return generable(x.Elem, seen)
	case *Named:
		if seen[x.Obj] {
			return "" // recursive: the generator stops it deep enough
		}
		seen[x.Obj] = true
		defer delete(seen, x.Obj)
		if x.Obj.IsError {
			return "it's an error"
		}
		if IsJSONValue(x) {
			return "it's any JSON"
		}
		s := x.Subst()
		switch x.Obj.Kind {
		case StructType:
			for _, f := range x.Obj.Fields {
				if why := generable(s.Apply(f.Type), seen); why != "" {
					return "its field " + f.Name + ": " + why
				}
			}
			return ""
		case EnumType:
			if x.Obj == loadingT {
				return "it's a Loading"
			}
			for _, k := range x.Obj.Cases {
				for _, p := range k.Params {
					if why := generable(s.Apply(p), seen); why != "" {
						return "its case " + k.Name + ": " + why
					}
				}
			}
			return ""
		}
		return "it's a " + x.Obj.name
	case *Tuple:
		return "a tuple: take its parts as parameters of their own"
	}
	return "it's " + aType(t)
}

// benchMeasure is a bench's measure block: one, at the top of its body
// (others are reported where measureStmt checks them).
func (c *Checker) benchMeasure(t *Test) *syntax.MeasureStmt {
	if t.Bench == nil {
		return nil
	}
	for _, it := range t.Bench.Body.Items {
		if m, ok := it.(*syntax.MeasureStmt); ok {
			if t.Measure != nil {
				c.errorf(m, diag.MisplacedStatement, "a bench measures one part: it has a measure block already")
				continue
			}
			t.Measure = m
		}
	}
	return t.Measure
}

// measureStmt checks measure { … }: its bench's, at the top of its body.
func (c *Checker) measureStmt(s *syntax.MeasureStmt) {
	if s != c.measure || c.ctx == nil || c.ctx.Kind != CtxTest {
		c.errorf(s, diag.MisplacedStatement, "measure { … } is a bench's: at the top of its body, after what sets it up")
	}
	c.block(s.Body)
}

// testingBuiltin checks keep, expectSnapshot and expectInlineSnapshot.
func (c *Checker) testingBuiltin(e *syntax.CallExpr, b *Builtin, call *Call, args []syntax.Expr) {
	call.Args = args
	call.Result = Void
	switch b.ID {
	case BKeep:
		// keep(x) is x: a benchmark's result, kept from being optimized
		// out (as Rust's black_box, Go's b.Loop).
		if len(args) != 1 {
			c.errorf(e, diag.ArgumentCount, "keep takes one value: keep(sorted(xs))")
			return
		}
		t := c.expr(args[0], nil)
		if t == Void {
			c.errorf(args[0], diag.NoValue, "keep keeps a value, and this has none: call it on what the work computes")
			t = Invalid
		}
		call.Result = t
	case BSnapshot, BInline:
		name := "expectSnapshot"
		if b.ID == BInline {
			name = "expectInlineSnapshot"
		}
		if len(args) < 1 || len(args) > 2 || b.ID == BSnapshot && len(args) != 1 {
			if b.ID == BSnapshot {
				c.errorf(e, diag.ArgumentCount, "expectSnapshot takes one value: expectSnapshot(app.dump())")
			} else {
				c.errorf(e, diag.ArgumentCount, "expectInlineSnapshot takes a value, and its snapshot: expectInlineSnapshot(total, \"42\")")
			}
		}
		if len(args) > 0 {
			t := c.expr(args[0], nil)
			if why := Printable(t); why != "" {
				c.errorf(args[0], diag.TypeMismatch, "%s can't write %s down: %s", name, aType(t), why)
			}
		}
		if b.ID == BInline && len(args) > 1 {
			s, ok := unparenExpr(args[1]).(*syntax.StringLit)
			if !ok || len(s.Interps) > 0 {
				c.errorf(args[1], diag.InlineSnapshot, "an inline snapshot is a string written in the call, which asuka test --update rewrites: not a value it computes")
			}
			c.assign(args[1], String, "the snapshot")
		}
	}
}

// Printable says why a snapshot can't write t down ("" when it can):
// what it prints of a value is its type's (docs/testing.md#snapshots).
func Printable(t Type) string { return printable(t, map[*TypeName]bool{}) }

func printable(t Type, seen map[*TypeName]bool) string {
	switch x := t.(type) {
	case *Basic:
		switch x {
		case Int, Float, Bool, String, Bytes, Duration, Color, Invalid:
			return ""
		}
		return "it's a " + x.name
	case *Array:
		return printable(x.Elem, seen)
	case *Optional:
		return printable(x.Elem, seen)
	case *Set:
		return printable(x.Elem, seen)
	case *Map:
		if why := printable(x.Key, seen); why != "" {
			return why
		}
		return printable(x.Elem, seen)
	case *Tuple:
		for _, e := range x.Elems {
			if why := printable(e, seen); why != "" {
				return why
			}
		}
		return ""
	case *Named:
		if seen[x.Obj] {
			return ""
		}
		seen[x.Obj] = true
		defer delete(seen, x.Obj)
		if x.Obj.IsError {
			return "it's an error: snapshot its message"
		}
		s := x.Subst()
		switch x.Obj.Kind {
		case StructType:
			for _, f := range x.Obj.Fields {
				if why := printable(s.Apply(f.Type), seen); why != "" {
					return "its field " + f.Name + ": " + why
				}
			}
			return ""
		case EnumType:
			if x.Obj == loadingT {
				return "it's a Loading"
			}
			for _, k := range x.Obj.Cases {
				for _, p := range k.Params {
					if why := printable(s.Apply(p), seen); why != "" {
						return "its case " + k.Name + ": " + why
					}
				}
			}
			return ""
		}
		return "it's a " + x.Obj.name
	}
	return "it's " + aType(t)
}
