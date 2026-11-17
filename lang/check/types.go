// Package check is the Asuka compiler's middle: it resolves names over
// packages (a directory of .asuka files is one; the prelude is visible
// everywhere), gives every expression a type, checks the rules of
// docs/language/ (declarative component bodies, await only where
// something may wait, try for what throws, spawn's captures, exhaustive
// matches) and records what the lowering (lang/ir) needs: what each name
// and member is, what each call calls with which arguments, the implicit
// conversions, what closures capture.
//
// It reports every problem it finds as a Diagnostic (file:line:col and a
// message that says what's wrong and what would fix it), and carries on.
package check

import (
	"strconv"
	"strings"
)

// A Type is an Asuka type.
type Type interface {
	String() string
}

// BasicKind names the types built into the compiler.
type BasicKind uint8

const (
	KInvalid  BasicKind = iota // after an error: compatible with everything, reported once
	KVoid                      // no value
	KInt                       // 64-bit integers
	KFloat                     // 64-bit floating point
	KBool                      //
	KString                    // UTF-8 text
	KBytes                     // raw bytes
	KDuration                  // nanoseconds
	KColor                     // ops.Color
	KLength                    // a packed length: points, percent or auto
	KNone                      // the type of the none literal: any optional's
	KView                      // what an element or a component makes
	KTestApp                   // what mount returns
	KGraphics                  // a Canvas's drawing context (the UI runtime's)
	KPath                      // an outline a Shape or a Graphics draws (the UI runtime's)
	KRef                       // a view's handle (ref: r, X as r): a cell holding the view, or none
	KNever                     // what never returns (throw, panic)
	KPointer                   // C's void *, opaque (RFC 0008)
)

// Basic is a built-in type without parameters.
type Basic struct {
	Kind BasicKind
	name string
}

func (b *Basic) String() string { return b.name }

// The built-in types.
var (
	Invalid  = &Basic{KInvalid, "invalid"}
	Void     = &Basic{KVoid, "void"}
	Int      = &Basic{KInt, "int"}
	Float    = &Basic{KFloat, "float"}
	Bool     = &Basic{KBool, "bool"}
	String   = &Basic{KString, "string"}
	Bytes    = &Basic{KBytes, "bytes"}
	Duration = &Basic{KDuration, "Duration"}
	Color    = &Basic{KColor, "Color"}
	Length   = &Basic{KLength, "Length"}
	NoneType = &Basic{KNone, "none"}
	View     = &Basic{KView, "View"}
	TestApp  = &Basic{KTestApp, "TestApp"}
	Graphics = &Basic{KGraphics, "Graphics"}
	Path     = &Basic{KPath, "Path"}
	Ref      = &Basic{KRef, "Ref"}
	Never    = &Basic{KNever, "never"}
	Pointer  = &Basic{KPointer, "Pointer"}
)

// Array is [Elem].
type Array struct{ Elem Type }

func (a *Array) String() string { return "[" + a.Elem.String() + "]" }

// Set is Set<Elem>: distinct Hashable values, in insertion order.
type Set struct{ Elem Type }

func (s *Set) String() string { return "Set<" + s.Elem.String() + ">" }

// Map is [Key: Elem].
type Map struct{ Key, Elem Type }

func (m *Map) String() string { return "[" + m.Key.String() + ": " + m.Elem.String() + "]" }

// Tuple is (A, B, ...).
type Tuple struct{ Elems []Type }

func (t *Tuple) String() string {
	var b strings.Builder
	b.WriteByte('(')
	for i, e := range t.Elems {
		if i > 0 {
			b.WriteString(", ")
		}
		b.WriteString(e.String())
	}
	b.WriteByte(')')
	return b.String()
}

// Optional is Elem?: an Elem, or none.
type Optional struct{ Elem Type }

func (o *Optional) String() string {
	if _, ok := o.Elem.(*FuncType); ok {
		return "(" + o.Elem.String() + ")?"
	}
	return o.Elem.String() + "?"
}

// FuncType is a function's type: [async] fn(Params) [throws] -> Result.
type FuncType struct {
	Params []Type
	Result Type
	Async  bool
	Throws bool
}

func (f *FuncType) String() string {
	var b strings.Builder
	if f.Async {
		b.WriteString("async ")
	}
	b.WriteString("fn(")
	for i, p := range f.Params {
		if i > 0 {
			b.WriteString(", ")
		}
		b.WriteString(p.String())
	}
	b.WriteByte(')')
	if f.Throws {
		b.WriteString(" throws")
	}
	if f.Result != nil && f.Result != Void {
		b.WriteString(" -> " + f.Result.String())
	}
	return b.String()
}

// Task is Task<Elem>: work that runs on its own (an async call, spawn)
// and its result.
type Task struct{ Elem Type }

func (t *Task) String() string { return "Task<" + t.Elem.String() + ">" }

// Stream is Stream<T>: values that come over time, read with for await.
type Stream struct{ Elem Type }

func (t *Stream) String() string { return "Stream<" + t.Elem.String() + ">" }

// Named is a declared type (a struct, an enum, an interface, a store, a
// component) with its type arguments.
type Named struct {
	Obj  *TypeName
	Args []Type
}

func (n *Named) String() string {
	s := n.Obj.name
	if n.Obj.Pkg != nil && n.Obj.Pkg.qualify {
		s = n.Obj.Pkg.Name + "." + s
	}
	if len(n.Args) > 0 {
		var b strings.Builder
		b.WriteString(s)
		b.WriteByte('<')
		for i, a := range n.Args {
			if i > 0 {
				b.WriteString(", ")
			}
			b.WriteString(a.String())
		}
		b.WriteByte('>')
		return b.String()
	}
	return s
}

// TypeParam is a generic function's or type's parameter.
type TypeParam struct {
	Name string
	// Builtins are the built-in constraints it names: Equatable,
	// Comparable, Hashable.
	Builtins []string
	// Ifaces are the program's interfaces it names: a type argument has
	// their methods and fields, which T's values are called with,
	// resolved per instantiation.
	Ifaces  []*Named
	written string // the constraints as written: Shape & Hashable
	Index   int
	id      int // unique, for inference
	// obj is the name it's declared as, at its own position: the one
	// object its uses resolve to, in its declaration's scopes.
	obj *TypeName
}

func (t *TypeParam) String() string { return t.Name }

// Constraint is the parameter's constraints as written (Shape &
// Hashable), "" for none.
func (t *TypeParam) Constraint() string { return t.written }

// Has reports a built-in constraint: Equatable, Comparable or Hashable.
func (t *TypeParam) Has(builtin string) bool {
	for _, b := range t.Builtins {
		if b == builtin {
			return true
		}
	}
	return false
}

// Untyped integer literals take float where a float is expected; their
// type until then is Int. That's handled by expected types, not here.

// ---------------------------------------------------------------------
// Comparing and substituting

// Identical reports whether a and b are the same type.
func Identical(a, b Type) bool {
	if a == b {
		return true
	}
	switch x := a.(type) {
	case *Array:
		y, ok := b.(*Array)
		return ok && Identical(x.Elem, y.Elem)
	case *Map:
		y, ok := b.(*Map)
		return ok && Identical(x.Key, y.Key) && Identical(x.Elem, y.Elem)
	case *Set:
		y, ok := b.(*Set)
		return ok && Identical(x.Elem, y.Elem)
	case *Optional:
		y, ok := b.(*Optional)
		return ok && Identical(x.Elem, y.Elem)
	case *Tuple:
		y, ok := b.(*Tuple)
		if !ok || len(x.Elems) != len(y.Elems) {
			return false
		}
		for i := range x.Elems {
			if !Identical(x.Elems[i], y.Elems[i]) {
				return false
			}
		}
		return true
	case *FuncType:
		y, ok := b.(*FuncType)
		if !ok || len(x.Params) != len(y.Params) || x.Async != y.Async || x.Throws != y.Throws || !Identical(x.Result, y.Result) {
			return false
		}
		for i := range x.Params {
			if !Identical(x.Params[i], y.Params[i]) {
				return false
			}
		}
		return true
	case *Task:
		y, ok := b.(*Task)
		return ok && Identical(x.Elem, y.Elem)
	case *Stream:
		y, ok := b.(*Stream)
		return ok && Identical(x.Elem, y.Elem)
	case *Named:
		y, ok := b.(*Named)
		if !ok || x.Obj != y.Obj || len(x.Args) != len(y.Args) {
			return false
		}
		for i := range x.Args {
			if !Identical(x.Args[i], y.Args[i]) {
				return false
			}
		}
		return true
	}
	return false
}

// Subst maps type parameters to types.
type Subst map[*TypeParam]Type

// Apply replaces s's type parameters in t.
func (s Subst) Apply(t Type) Type {
	if len(s) == 0 || t == nil {
		return t
	}
	switch x := t.(type) {
	case *TypeParam:
		if r, ok := s[x]; ok && r != nil {
			return r
		}
	case *Array:
		if e := s.Apply(x.Elem); e != x.Elem {
			return &Array{e}
		}
	case *Map:
		k, e := s.Apply(x.Key), s.Apply(x.Elem)
		if k != x.Key || e != x.Elem {
			return &Map{k, e}
		}
	case *Set:
		if e := s.Apply(x.Elem); e != x.Elem {
			return &Set{e}
		}
	case *Optional:
		if e := s.Apply(x.Elem); e != x.Elem {
			return MakeOptional(e)
		}
	case *Tuple:
		var out []Type
		for i, e := range x.Elems {
			if r := s.Apply(e); r != e && out == nil {
				out = append([]Type{}, x.Elems[:i]...)
				out = append(out, r)
			} else if out != nil {
				out = append(out, r)
			}
		}
		if out != nil {
			return &Tuple{out}
		}
	case *FuncType:
		changed := false
		ps := make([]Type, len(x.Params))
		for i, p := range x.Params {
			ps[i] = s.Apply(p)
			changed = changed || ps[i] != p
		}
		r := s.Apply(x.Result)
		if changed || r != x.Result {
			return &FuncType{ps, r, x.Async, x.Throws}
		}
	case *Task:
		if e := s.Apply(x.Elem); e != x.Elem {
			return &Task{e}
		}
	case *Stream:
		if e := s.Apply(x.Elem); e != x.Elem {
			return &Stream{e}
		}
	case *Named:
		var args []Type
		for i, a := range x.Args {
			if r := s.Apply(a); r != a && args == nil {
				args = append([]Type{}, x.Args[:i]...)
				args = append(args, r)
			} else if args != nil {
				args = append(args, r)
			}
		}
		if args != nil {
			return &Named{x.Obj, args}
		}
	}
	return t
}

// MakeOptional is t?: an optional of an optional is the one optional.
func MakeOptional(t Type) Type {
	if _, ok := t.(*Optional); ok {
		return t
	}
	if t == NoneType || t == Invalid {
		return t
	}
	return &Optional{t}
}

// Unopt is t without its optional.
func Unopt(t Type) Type {
	if o, ok := t.(*Optional); ok {
		return o.Elem
	}
	return t
}

// IsOptional reports whether t is an optional.
func IsOptional(t Type) bool {
	_, ok := t.(*Optional)
	return ok
}

// HasTypeParams reports whether t mentions a type parameter.
func HasTypeParams(t Type) bool {
	switch x := t.(type) {
	case *TypeParam:
		return true
	case *Array:
		return HasTypeParams(x.Elem)
	case *Map:
		return HasTypeParams(x.Key) || HasTypeParams(x.Elem)
	case *Set:
		return HasTypeParams(x.Elem)
	case *Optional:
		return HasTypeParams(x.Elem)
	case *Tuple:
		for _, e := range x.Elems {
			if HasTypeParams(e) {
				return true
			}
		}
	case *FuncType:
		for _, p := range x.Params {
			if HasTypeParams(p) {
				return true
			}
		}
		return HasTypeParams(x.Result)
	case *Task:
		return HasTypeParams(x.Elem)
	case *Stream:
		return HasTypeParams(x.Elem)
	case *Named:
		for _, a := range x.Args {
			if HasTypeParams(a) {
				return true
			}
		}
	}
	return false
}

// Subst of a named type: its type parameters to its arguments.
func (n *Named) Subst() Subst {
	if len(n.Args) == 0 {
		return nil
	}
	s := Subst{}
	for i, p := range n.Obj.TypeParams {
		if i < len(n.Args) {
			s[p] = n.Args[i]
		}
	}
	return s
}

// ---------------------------------------------------------------------
// What types are

// IsNumeric reports int or float.
func IsNumeric(t Type) bool { return t == Int || t == Float }

// IsInvalid reports a type an error already explained.
func IsInvalid(t Type) bool { return t == Invalid || t == nil }

// AsNamed returns t as a declared type of the kind.
func AsNamed(t Type, k TypeKind) *Named {
	if n, ok := t.(*Named); ok && n.Obj.Kind == k {
		return n
	}
	return nil
}

// IsErrorType reports whether t is an error: Error, or a type declared
// one (: Error).
func IsErrorType(t Type) bool {
	if n, ok := t.(*Named); ok {
		return n.Obj.IsError || n.Obj.isErrorIface
	}
	return false
}

// whyNotEquatable says why values of t, which isn't Equatable, can't be
// compared: the part of it that can't.
func whyNotEquatable(t Type) string {
	return whyNotEquatable1(t, map[*TypeName]bool{})
}

func whyNotEquatable1(t Type, seen map[*TypeName]bool) string {
	switch x := t.(type) {
	case *FuncType:
		return "functions can't be"
	case *Array:
		return "its elements are " + typeString(x.Elem) + ", and " + whyNotEquatable1(x.Elem, seen)
	case *Map:
		return "its values are " + typeString(x.Elem) + ", and " + whyNotEquatable1(x.Elem, seen)
	case *Optional:
		return whyNotEquatable1(x.Elem, seen)
	case *Tuple:
		for _, e := range x.Elems {
			if !IsEquatable(e) {
				return "it holds " + aType(e) + ", and " + whyNotEquatable1(e, seen)
			}
		}
	case *TypeParam:
		return "constrain it, <" + x.Name + ": Equatable>"
	case *Named:
		if seen[x.Obj] {
			return "it holds itself" // the reason is the other part's, said on its way
		}
		seen[x.Obj] = true
		s := x.Subst()
		switch x.Obj.Kind {
		case StructType:
			// A field that holds the type itself isn't why: the other
			// fields are.
			for _, f := range x.Obj.Fields {
				if ft := s.Apply(f.Type); !IsEquatable(ft) && !mentions(ft, x.Obj) {
					return "its field " + f.Name + " is " + aType(ft) + ", and " + whyNotEquatable1(ft, seen)
				}
			}
		case EnumType:
			for _, c := range x.Obj.Cases {
				for _, p := range c.Params {
					if pt := s.Apply(p); !IsEquatable(pt) && !mentions(pt, x.Obj) {
						return "its case " + c.Name + " holds " + aType(pt) + ", and " + whyNotEquatable1(pt, seen)
					}
				}
			}
		case StoreType:
			return "a store is a reference, not a value: compare what it holds"
		case InterfaceType:
			return "an interface's values may be of any type"
		}
	}
	return "its values aren't"
}

// mentions reports whether t names the type o (in an optional, an
// array, a map, a tuple or a type argument).
func mentions(t Type, o *TypeName) bool {
	switch x := t.(type) {
	case *Optional:
		return mentions(x.Elem, o)
	case *Array:
		return mentions(x.Elem, o)
	case *Map:
		return mentions(x.Key, o) || mentions(x.Elem, o)
	case *Set:
		return mentions(x.Elem, o)
	case *Tuple:
		for _, e := range x.Elems {
			if mentions(e, o) {
				return true
			}
		}
	case *Named:
		if x.Obj == o {
			return true
		}
		for _, a := range x.Args {
			if mentions(a, o) {
				return true
			}
		}
	}
	return false
}

// IsEquatable reports whether values of t can be compared with ==. A
// struct or an enum that holds itself (a list, a tree) is when its
// other parts are.
func IsEquatable(t Type) bool {
	return isEquatable(t, map[*TypeName]bool{})
}

func isEquatable(t Type, seen map[*TypeName]bool) bool {
	switch x := t.(type) {
	case *Basic:
		return x != Void && x != View && x != TestApp && x != Never && x != Graphics && x != Path && x != Ref
	case *Array:
		return isEquatable(x.Elem, seen)
	case *Map:
		return isEquatable(x.Elem, seen)
	case *Set:
		return true // its elements are Hashable
	case *Optional:
		return isEquatable(x.Elem, seen)
	case *Tuple:
		for _, e := range x.Elems {
			if !isEquatable(e, seen) {
				return false
			}
		}
		return true
	case *TypeParam:
		return len(x.Builtins) > 0
	case *Named:
		if seen[x.Obj] {
			return true // as it is, if the rest is (checked on the way here)
		}
		switch x.Obj.Kind {
		case ExternType:
			return true // the same pointer
		case StructType:
			seen[x.Obj] = true
			defer delete(seen, x.Obj)
			s := x.Subst()
			for _, f := range x.Obj.Fields {
				if !isEquatable(s.Apply(f.Type), seen) {
					return false
				}
			}
			return true
		case EnumType:
			seen[x.Obj] = true
			defer delete(seen, x.Obj)
			s := x.Subst()
			for _, c := range x.Obj.Cases {
				for _, p := range c.Params {
					if !isEquatable(s.Apply(p), seen) {
						return false
					}
				}
			}
			return true
		}
	}
	return false
}

// IsComparable reports whether values of t can be ordered with <.
func IsComparable(t Type) bool {
	switch t {
	case Int, Float, String, Duration:
		return true
	}
	if p, ok := t.(*TypeParam); ok {
		return p.Has("Comparable")
	}
	return false
}

// IsHashable reports whether t can be a map's key: the runtime's maps
// take ints (and what's stored as one: bools, payloadless enums,
// durations, colors) and strings.
func IsHashable(t Type) bool {
	switch t {
	case Int, Bool, String, Duration, Color:
		return true
	}
	if p, ok := t.(*TypeParam); ok {
		return p.Has("Hashable")
	}
	if n := AsNamed(t, EnumType); n != nil {
		return !n.Obj.RefEnum()
	}
	return false
}

// hashableHint says how a type parameter that isn't Hashable can be.
func hashableHint(t Type) string {
	if p, ok := t.(*TypeParam); ok {
		return ": constrain it, <" + p.Name + ": Hashable>"
	}
	return ""
}

// typeString is t for messages: quoted the way the docs write types.
func typeString(t Type) string {
	if t == nil {
		return "nothing"
	}
	return t.String()
}

func plural(n int, one, many string) string {
	if n == 1 {
		return "1 " + one
	}
	return strconv.Itoa(n) + " " + many
}

// aType is t for messages, after its article: "an int", "a string".
func aType(t Type) string {
	s := typeString(t)
	if s != "" && strings.ContainsRune("aeioAEIO", rune(s[0])) {
		return "an " + s
	}
	return "a " + s
}
