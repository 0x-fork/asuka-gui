package check

import (
	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// A struct's or an enum's own ==, hash, text and JSON: by default a
// struct is equal, hashes, prints and is written as JSON by its fields
// (an enum by its case and payload), and a type says otherwise with
// methods of names the language calls, as compare is <:
//
//	fn equals(other: T) -> bool       ==, !=, a map's keys, a set's elements, contains
//	fn hash() -> int                  a map's keys, a set's elements (with equals: agreeing with it)
//	fn description() -> string        interpolation, print, a snapshot's text
//	fn toJSON() -> J                  json.encode, and how it's written inside others' text
//	static fn fromJSON(json: J) throws -> T   json.decode
//
// J is any type JSON can say. They're the type's, declared in its body
// (not an extension's), neither async nor mut, nor private, and an enum's
// only when it has payloads (one without is compared, hashed and written
// by its case; its description is its own, though).

// Own is what a type says itself.
type Own struct {
	Equals      *Function
	Hash        *Function
	Description *Function
	ToJSON      *Function
	FromJSON    *Function // a static fn
	JSON        Type      // what toJSON gives and fromJSON takes
	// Ext: an extension of the type's own package that says them for
	// the types it extends (extend<T: Equatable> Heap<T>): for those
	// whose arguments meet its constraints (OwnOf); in what OwnOf
	// merges, the one toJSON and fromJSON are of.
	Ext *Extension
}

// ownNames are the methods' names the language calls.
var ownNames = []string{"equals", "hash", "description", "toJSON"}

// OwnOf is what t says itself: its type's own methods, or what the
// extensions of the type's package give it where its arguments meet
// their constraints (Equatable, Hashable, Comparable), each method the
// first's that gives it (toJSON and fromJSON together); nil when it says
// nothing.
func OwnOf(t Type) *Own {
	n, ok := t.(*Named)
	if !ok {
		return nil
	}
	if n.Obj.Own != nil || len(n.Obj.OwnExts) == 0 {
		return n.Obj.Own
	}
	var out *Own
	for _, o := range n.Obj.OwnExts {
		if !extMeets(o.Ext, n) {
			continue
		}
		if out == nil {
			out = &Own{}
		}
		if out.Equals == nil && o.Equals != nil {
			out.Equals, out.Hash = o.Equals, o.Hash // they agree: one's together
		}
		if out.Hash == nil && o.Hash != nil && out.Equals == nil {
			out.Hash = o.Hash
		}
		if out.Description == nil {
			out.Description = o.Description
		}
		if out.ToJSON == nil && o.ToJSON != nil {
			out.ToJSON, out.JSON, out.Ext = o.ToJSON, o.JSON, o.Ext
		}
		if out.FromJSON == nil && o.FromJSON != nil {
			out.FromJSON = o.FromJSON // its Ext is its fn's (FromJSONArgs)
			if out.JSON == nil {
				out.JSON, out.Ext = o.JSON, o.Ext
			}
		}
	}
	return out
}

// extMeets reports whether the extension e (of a type of its package,
// constrained by built-in constraints only) extends n.
func extMeets(e *Extension, n *Named) bool {
	s := Subst{}
	if !matchType(e.Target, n, e.TypeParams, s) {
		return false
	}
	for _, tp := range e.TypeParams {
		a := s[tp]
		for _, b := range tp.Builtins {
			switch {
			case a == nil:
				return false
			case b == "Equatable" && !IsEquatable(a), b == "Hashable" && !IsHashable(a), b == "Comparable" && !IsComparable(a):
				return false
			}
		}
	}
	return true
}

// resolveOwn reads t's own methods, and says where one's signature isn't
// what the language calls (special-method).
func (c *Checker) resolveOwn(t *TypeName) {
	if t.Kind != StructType && t.Kind != EnumType || t.Extern {
		return
	}
	if o := c.ownMethods(t, t.Type, t.Methods, t.Statics, len(t.TypeParams)); o != nil {
		t.Own = o
	}
}

// resolveOwnExt reads the own methods of an extension of a type its own
// package declares (extend<T: Equatable> Heap<T> { fn equals … }): the
// type's for the types it extends.
func (c *Checker) resolveOwnExt(e *Extension) {
	n, ok := e.Target.(*Named)
	if !ok || n.Obj.Pkg != e.Pkg || n.Obj.Kind != StructType && n.Obj.Kind != EnumType {
		return
	}
	has := false
	for _, name := range ownNames {
		has = has || e.Methods[name] != nil
	}
	if !has && e.Statics["fromJSON"] == nil {
		return
	}
	for _, tp := range e.TypeParams {
		if len(tp.Ifaces) > 0 {
			c.errorf(e.Decl, diag.SpecialMethod, "an extension that says how %s compares, hashes, prints or is JSON takes Equatable, Hashable and Comparable constraints only (the types it's for are known wherever the type is)", n.Obj.name)
			return
		}
	}
	if n.Obj.Own != nil {
		c.errorf(e.Decl, diag.SpecialMethod, "%s says how it compares, hashes, prints or is JSON in its own body: an extension can't say it again", n.Obj.name)
		return
	}
	if o := c.ownMethods(n.Obj, n, e.Methods, e.Statics, len(e.TypeParams)); o != nil {
		o.Ext = e
		n.Obj.OwnExts = append(n.Obj.OwnExts, o)
	}
}

// ownMethods reads what methods (and statics) say of self, a t, whose
// type parameters number params; nil when they say nothing.
func (c *Checker) ownMethods(t *TypeName, self Type, methods, statics map[string]*Function, params int) *Own {
	var own Own
	some := false
	payloads := t.Kind == StructType || t.RefEnum()
	bad := func(f *Function, format string, args ...any) {
		c.errorf(f.Decl.Name, diag.SpecialMethod, format, args...)
	}
	for _, name := range ownNames {
		f := methods[name]
		if f == nil || f.Decl == nil {
			continue
		}
		c.funcSig(f)
		s := f.Sig
		if s == nil {
			continue
		}
		want := map[string]string{
			"equals":      "fn equals(other: " + typeString(self) + ") -> bool",
			"hash":        "fn hash() -> int",
			"description": "fn description() -> string",
			"toJSON":      "fn toJSON() -> J, J a type JSON can say",
		}[name]
		switch {
		case s.Async || s.Throws || s.Mut || len(f.TypeParams) > params:
			bad(f, "%s is what the language calls for %s: neither async, throws nor mut (%s)", name, ownUse(name), want)
			continue
		case f.Decl.Private.IsValid():
			bad(f, "%s is what the language calls for %s, everywhere: it can't be private", name, ownUse(name))
			continue
		case !payloads && name != "description":
			bad(f, "%s is an enum without payloads, compared, hashed and written by its case: it has no %s of its own", t.name, name)
			continue
		}
		switch name {
		case "equals":
			if len(s.Params) != 1 || !Identical(s.Params[0].Type, self) || s.Result != Bool {
				bad(f, "equals is ==: %s", want)
				continue
			}
			own.Equals = f
		case "hash":
			if len(s.Params) != 0 || s.Result != Int {
				bad(f, "hash is how %s hashes as a map's key: %s", t.name, want)
				continue
			}
			own.Hash = f
		case "description":
			if len(s.Params) != 0 || s.Result != String {
				bad(f, "description is how %s prints: %s", t.name, want)
				continue
			}
			own.Description = f
		case "toJSON":
			if len(s.Params) != 0 || s.Result == Void {
				bad(f, "toJSON is how %s is written as JSON: %s", t.name, want)
				continue
			}
			if why := jsonable1(s.Result, map[*TypeName]bool{t: true}, func(*TypeParam) bool { return true }); why != "" {
				bad(f, "toJSON gives %s, which JSON can't say: %s", aType(s.Result), why)
				continue
			}
			own.ToJSON, own.JSON = f, s.Result
		}
		some = true
	}
	if f := statics["fromJSON"]; f != nil && f.Decl != nil {
		c.funcSig(f)
		s := f.Sig
		want := "static fn fromJSON(json: J) throws -> " + typeString(self)
		switch {
		case s == nil:
		case s.Async || f.Decl.Private.IsValid() || len(f.TypeParams) > params:
			bad(f, "fromJSON is what json.decode calls: not async nor private, nor generic but in %s's parameters (%s)", t.name, want)
		case !payloads:
			bad(f, "%s is an enum without payloads, read by its case's name: it has no fromJSON of its own", t.name)
		case len(s.Params) != 1 || !Identical(s.Result, self):
			bad(f, "fromJSON is how json.decode makes %s: %s", aType(self), want)
		case own.JSON != nil && !Identical(own.JSON, s.Params[0].Type):
			bad(f, "fromJSON takes what toJSON gives: %s, not %s", typeString(own.JSON), typeString(s.Params[0].Type))
		default:
			if why := jsonable1(s.Params[0].Type, map[*TypeName]bool{t: true}, func(*TypeParam) bool { return true }); why != "" {
				bad(f, "fromJSON takes %s, which JSON can't say: %s", aType(s.Params[0].Type), why)
				break
			}
			own.FromJSON = f
			if own.JSON == nil {
				own.JSON = s.Params[0].Type
			}
			some = true
		}
	}
	if own.Hash != nil && own.Equals == nil && !IsEquatable(self) {
		c.errorf(own.Hash.Decl.Name, diag.SpecialMethod, "%s hashes, and isn't Equatable by its fields (%s): declare fn equals(other: %s) -> bool too", t.name, whyNotEquatable(self), typeString(self))
	}
	if !some {
		return nil
	}
	return &own
}

// ownUse says what the language calls an own method for.
func ownUse(name string) string {
	switch name {
	case "equals":
		return "=="
	case "hash":
		return "a map's keys"
	case "description":
		return "a value's text"
	}
	return "JSON"
}

// ownSyntax keeps syntax imported for what reads Own's methods' places.
var _ = syntax.NoPos

// missingJSON says why a value of t can't be read (or written) as JSON
// because a type in it has its own JSON one way only: a toJSON and no
// fromJSON, or the other way; "" when nothing in it does.
func missingJSON(t Type, read bool) string {
	return missingJSON1(t, read, map[*TypeName]bool{})
}

func missingJSON1(t Type, read bool, seen map[*TypeName]bool) string {
	switch x := t.(type) {
	case *Array:
		return missingJSON1(x.Elem, read, seen)
	case *Optional:
		return missingJSON1(x.Elem, read, seen)
	case *Map:
		return missingJSON1(x.Elem, read, seen)
	case *Set:
		return missingJSON1(x.Elem, read, seen)
	case *Named:
		if seen[x.Obj] {
			return ""
		}
		seen[x.Obj] = true
		s := x.Subst()
		if o := OwnOf(x); o != nil && (o.ToJSON != nil || o.FromJSON != nil) {
			switch {
			case read && o.FromJSON == nil:
				return x.Obj.name + " is written by its toJSON, and has no static fn fromJSON to read it"
			case !read && o.ToJSON == nil:
				return x.Obj.name + " is read by its fromJSON, and has no fn toJSON to write it"
			}
			return missingJSON1(s.Apply(o.JSON), read, seen)
		}
		switch x.Obj.Kind {
		case StructType:
			for _, f := range x.Obj.Fields {
				if why := missingJSON1(s.Apply(f.Type), read, seen); why != "" {
					return "its field " + f.Name + ": " + why
				}
			}
		case EnumType:
			for _, k := range x.Obj.Cases {
				for _, p := range k.Params {
					if why := missingJSON1(s.Apply(p), read, seen); why != "" {
						return "its case " + k.Name + ": " + why
					}
				}
			}
		}
	}
	return ""
}

// FromJSONArgs are the type arguments o's fromJSON is compiled with for
// n: n's own, or, an extension's, its type parameters as n binds them.
func FromJSONArgs(o *Own, n *Named) []Type {
	e := o.FromJSON.Ext
	if e == nil {
		return n.Args
	}
	s := Subst{}
	matchType(e.Target, n, e.TypeParams, s)
	var out []Type
	for _, tp := range e.TypeParams {
		out = append(out, s[tp])
	}
	return out
}

// OwnSubst is what o's types (JSON) are for n.
func OwnSubst(o *Own, n *Named) Subst {
	if o.Ext == nil {
		return n.Subst()
	}
	s := Subst{}
	matchType(o.Ext.Target, n, o.Ext.TypeParams, s)
	return s
}
