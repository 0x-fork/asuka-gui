package check

import (
	"fmt"
	"sort"
	"strings"

	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// Extensions (RFC 0011): extend T { … } adds fns, mut fns, static fns
// and constants to a type, built in or declared, the package's own or
// another's. Its methods are functions with the receiver first, called
// as a method's (CALL, not CALLI); its static fns and constants are the
// type's, found as Type.name and .name where a T is expected. They're
// visible in the package that declares them, and their pub ones in the
// packages that import it: two of one name an importer sees are an
// error where it's used.

// An Extension is an extend declaration.
type Extension struct {
	Pkg        *Package
	Decl       *syntax.ExtendDecl
	TypeParams []*TypeParam
	Target     Type
	Methods    map[string]*Function // fns and mut fns: Function.Ext is the extension
	Statics    map[string]*Function // static fns: Function.Static the type's name too
	Consts     map[string]*Global
	Conforms   []*TypeName
	Order      []*Function // its methods, in source order
}

// IsExtMethod reports whether f is an extension's method (not a static
// fn): it takes its receiver first.
func (f *Function) IsExtMethod() bool { return f.Ext != nil && f.Static == nil }

// collectExtend records an extend declaration; its members are declared
// once the package's types are (extMembers).
func (c *Checker) collectExtend(p *Package, d *syntax.ExtendDecl) {
	if d.Pub.IsValid() {
		c.errorPos(d.Pub, diag.InvalidModifier, "pub goes on an extension's members, each: pub fn, pub let")
	}
	e := &Extension{Pkg: p, Decl: d, Methods: map[string]*Function{}, Statics: map[string]*Function{}, Consts: map[string]*Global{}}
	for _, m := range d.Members {
		c.fileScopes[m] = c.fileScopes[d]
	}
	p.Exts = append(p.Exts, e)
}

// extScope is the scope an extension's insides are checked in: its
// file's, with its type parameters.
func (c *Checker) extScope(e *Extension) *Scope {
	s := NewScope(c.declScope(e.Decl))
	for _, p := range e.TypeParams {
		s.Insert(p.object())
	}
	return s
}

// typeNameOf is the name a type is declared or built in as (Color,
// string, Todo), nil for one written with brackets ([T], [K: V]).
func (c *Checker) typeNameOf(t Type) *TypeName {
	switch x := t.(type) {
	case *Named:
		return x.Obj
	case *Basic:
		return c.universe.LookupType(x.name)
	}
	return nil
}

// extMembers resolves an extension's type and declares its members.
func (c *Checker) extMembers(e *Extension) {
	d := e.Decl
	savedScope, savedCtx := c.scope, c.ctx
	c.scope = NewScope(c.declScope(d))
	c.ctx = nil
	defer func() { c.scope, c.ctx = savedScope, savedCtx }()
	e.TypeParams = c.typeParams(d.TypeParams)
	c.scope = c.extScope(e)
	e.Target = c.typeExpr(d.Type)
	if IsInvalid(e.Target) {
		return
	}
	if why := extensible(e.Target); why != "" {
		c.errorf(d.Type, diag.InvalidExtension, "can't extend %s: %s", typeString(e.Target), why)
		e.Target = Invalid
		return
	}
	for _, tp := range e.TypeParams {
		if !mentionsParam(e.Target, tp) {
			c.errorf(d.Type, diag.InvalidExtension, "extend's %s isn't in the type it extends (%s): name it there, extend<%s> [%s]", tp.Name, typeString(e.Target), tp.Name, tp.Name)
		}
	}
	tn := c.typeNameOf(e.Target)
	for _, m := range d.Members {
		switch m := m.(type) {
		case *syntax.Field:
			c.errorf(m.Name, diag.MisplacedDeclaration, "an extension adds no fields: a type's fields are its declaration's; a value made of others is a fn, fn %s() -> %s = …", m.Name.Name, syntaxTypeString(m.Type))
		case *syntax.LetDecl:
			c.noLetElse(m)
			c.extConst(e, tn, m)
		case *syntax.FuncDecl:
			c.extFunc(e, tn, m)
		}
	}
	for _, ct := range d.Conforms {
		ty := c.typeExpr(ct)
		n, ok := ty.(*Named)
		if !ok || n.Obj.Kind != InterfaceType || n.Obj.isErrorIface || n.Obj.Native {
			if !IsInvalid(ty) {
				c.errorf(ct, diag.InvalidConformance, "%s isn't an interface an extension can give: extend T: Interface", typeString(ty))
			}
			continue
		}
		e.Conforms = append(e.Conforms, n.Obj)
		if en := AsNamed(e.Target, EnumType); en != nil && !contains(en.Obj.Conforms, n.Obj) {
			// An enum that has an interface is stored as a reference
			// (RefEnum), wherever it's used: the program is compiled whole.
			en.Obj.Conforms = append(en.Obj.Conforms, n.Obj)
		}
	}
}

// extensible says why t can't be extended, "" when it can.
func extensible(t Type) string {
	switch x := t.(type) {
	case *Basic:
		switch x {
		case Void, View, Ref, Pointer, TestApp:
			return "it isn't a type of values an extension adds to"
		}
		return ""
	case *Array, *Map, *Set:
		return ""
	case *Named:
		switch x.Obj.Kind {
		case StructType, EnumType:
			return ""
		case InterfaceType:
			return "an interface's methods are its body's"
		}
		return "only structs, enums and the built-in types of values are extended"
	case *Optional:
		return "extend the type, and call its methods with ?."
	case *TypeParam:
		return "extend a type: extend<T> [T]"
	}
	return "only structs, enums and the built-in types of values are extended"
}

func syntaxTypeString(t syntax.Type) string {
	var b strings.Builder
	syntax.Fprint(&b, nil, t)
	return b.String()
}

// owns says what of a type's own the name is ("a method", "a field",
// …), "" when it's none.
func (c *Checker) owns(t Type, name string) string {
	if n, ok := t.(*Named); ok {
		switch {
		case n.Obj.Field(name) != nil:
			return "a field"
		case n.Obj.Methods[name] != nil:
			return "a method"
		case n.Obj.Case(name) != nil:
			return "a case"
		case n.Obj.Consts[name] != nil:
			return "a constant"
		case n.Obj.Statics[name] != nil:
			return "a static fn"
		}
		return ""
	}
	if lookupField(t, name) != nil {
		return "a property"
	}
	if lookupMethod(t, name) != nil {
		return "a method"
	}
	if t == Color {
		if _, ok := SemanticColor(name); ok {
			return "a semantic color"
		}
	}
	if t == Length && name == "auto" {
		return "a constant"
	}
	return ""
}

// extDeclared reports a name an extension declares twice, or that's the
// type's own.
func (c *Checker) extDeclared(e *Extension, id *syntax.Ident) bool {
	name := id.Name
	if e.Methods[name] != nil || e.Statics[name] != nil || e.Consts[name] != nil {
		c.errorf(id, diag.DuplicateDeclaration, "%s is declared twice in this extension", name)
		return true
	}
	if what := c.owns(e.Target, name); what != "" {
		c.errorf(id, diag.ExtensionConflict, "%s has %s %s of its own: an extension adds names, it doesn't replace them", typeString(e.Target), what, name)
		return true
	}
	return false
}

func (c *Checker) extFunc(e *Extension, tn *TypeName, d *syntax.FuncDecl) {
	if c.extDeclared(e, d.Name) {
		return
	}
	f := &Function{object: object{d.Name.Name, d.Name.Start}, Pkg: e.Pkg, Decl: d, Ext: e}
	c.info.Defs[d.Name] = f
	switch {
	case d.Native.IsValid() || d.Extern.IsValid():
		c.errorf(d.Name, diag.MisplacedDeclaration, "an extension's fns have bodies: a native or extern fn is declared at the top of a file")
		return
	case d.Static.IsValid():
		if tn == nil {
			c.errorf(d.Name, diag.InvalidExtension, "a static fn of %s can't be named (%s.%s): extend a type with a name for it", typeString(e.Target), typeString(e.Target), d.Name.Name)
			return
		}
		f.Static = tn
		e.Statics[f.name] = f
		e.Pkg.Funcs = append(e.Pkg.Funcs, f)
	default:
		e.Methods[f.name] = f
		e.Order = append(e.Order, f)
	}
}

func (c *Checker) extConst(e *Extension, tn *TypeName, d *syntax.LetDecl) {
	bp, ok := d.Pattern.(*syntax.BindPat)
	if !ok {
		c.errorf(d.Pattern, diag.InvalidDeclaration, "an extension's constant is let name = value")
		return
	}
	if d.Keyword == syntax.VAR {
		c.errorf(d, diag.MisplacedDeclaration, "an extension adds no state: its constants are let")
		return
	}
	if c.extDeclared(e, bp.Name) {
		return
	}
	if tn == nil {
		c.errorf(bp.Name, diag.InvalidExtension, "a constant of %s can't be named (.%s): extend a type with a name for it", typeString(e.Target), bp.Name.Name)
		return
	}
	if len(e.TypeParams) > 0 {
		c.errorf(bp.Name, diag.MisplacedDeclaration, "a generic extension has no constants: they'd need its type parameters")
		return
	}
	g := &Global{object: object{bp.Name.Name, bp.Name.Start}, Pkg: e.Pkg, Kind: ConstGlobal, Decl: d, Value: d.Value, Ext: e, Pub: d.Pub.IsValid()}
	c.info.Defs[bp.Name] = g
	e.Consts[g.name] = g
	e.Pkg.Globals = append(e.Pkg.Globals, g)
}

// extBodies checks an extension's methods, and the interfaces it says
// the type has.
func (c *Checker) extBodies(e *Extension) {
	for _, f := range e.Order {
		c.funcSig(f)
	}
	for _, f := range e.Order {
		c.funcBody(f)
	}
	if IsInvalid(e.Target) {
		return
	}
	for _, i := range e.Conforms {
		if why := c.implementsMembers(e.Target, &Named{Obj: i}); why != "" {
			c.errorf(e.Decl.Type, diag.InterfaceNotSatisfied, "%s declares %s but %s", typeString(e.Target), i.name, why)
		}
	}
}

// Name is the extension's type as written, for messages.
func (e *Extension) Name() string { return typeString(e.Target) }

// ---------------------------------------------------------------------
// Lookups

// visibleExts are the extensions the package being checked sees: its
// own, and its imports'.
func (c *Checker) visibleExts() []*Extension {
	if c.pkg == nil {
		return nil
	}
	exts := append([]*Extension{}, c.pkg.Exts...)
	for _, p := range c.pkg.Imports {
		exts = append(exts, p.Exts...)
	}
	return exts
}

// visible reports whether the package being checked sees a member of
// e's (pub: declared so).
func (c *Checker) visible(e *Extension, pub bool) bool {
	return e.Pkg == c.pkg || pub
}

// extMatch matches t against e's type: the substitution of e's type
// parameters it takes, ok false when t isn't of it. why says which
// constraint t's arguments don't meet, when that's what failed.
func (c *Checker) extMatch(e *Extension, t Type) (s Subst, why string, ok bool) {
	if IsInvalid(e.Target) {
		return nil, "", false
	}
	s = Subst{}
	if !matchType(e.Target, t, e.TypeParams, s) {
		return nil, "", false
	}
	for _, tp := range e.TypeParams {
		a := s[tp]
		if a == nil {
			return nil, "", false
		}
		if failed, w := c.satisfies(a, tp); failed != "" {
			return nil, fmt.Sprintf("its %s must be %s, and %s isn't", tp.Name, failed, typeString(a)) + optColon(w), false
		}
	}
	return s, "", true
}

// openGeneric reports whether t is a generic type as it's declared, its
// own parameters its arguments (Box<T> in Box): Box.name, before its
// arguments are known.
func openGeneric(t Type) bool {
	n, ok := t.(*Named)
	if !ok || len(n.Args) == 0 || len(n.Args) != len(n.Obj.TypeParams) {
		return false
	}
	for i, a := range n.Args {
		if a != Type(n.Obj.TypeParams[i]) {
			return false
		}
	}
	return true
}

func optColon(why string) string {
	if why == "" {
		return ""
	}
	return ": " + why
}

// matchType matches the type pattern p, whose free parameters are
// params, against t, binding them in s.
func matchType(p, t Type, params []*TypeParam, s Subst) bool {
	if tp, ok := p.(*TypeParam); ok && containsParam(params, tp) {
		if b := s[tp]; b != nil {
			return Identical(b, t)
		}
		s[tp] = t
		return true
	}
	switch x := p.(type) {
	case *Array:
		y, ok := t.(*Array)
		return ok && matchType(x.Elem, y.Elem, params, s)
	case *Set:
		y, ok := t.(*Set)
		return ok && matchType(x.Elem, y.Elem, params, s)
	case *Map:
		y, ok := t.(*Map)
		return ok && matchType(x.Key, y.Key, params, s) && matchType(x.Elem, y.Elem, params, s)
	case *Optional:
		y, ok := t.(*Optional)
		return ok && matchType(x.Elem, y.Elem, params, s)
	case *Tuple:
		y, ok := t.(*Tuple)
		if !ok || len(x.Elems) != len(y.Elems) {
			return false
		}
		for i := range x.Elems {
			if !matchType(x.Elems[i], y.Elems[i], params, s) {
				return false
			}
		}
		return true
	case *Named:
		y, ok := t.(*Named)
		if !ok || x.Obj != y.Obj || len(x.Args) != len(y.Args) {
			return false
		}
		for i := range x.Args {
			if !matchType(x.Args[i], y.Args[i], params, s) {
				return false
			}
		}
		return true
	case *FuncType:
		y, ok := t.(*FuncType)
		if !ok || len(x.Params) != len(y.Params) || x.Async != y.Async || x.Throws != y.Throws {
			return false
		}
		for i := range x.Params {
			if !matchType(x.Params[i], y.Params[i], params, s) {
				return false
			}
		}
		return matchType(x.Result, y.Result, params, s)
	}
	return Identical(p, t)
}

func containsParam(ps []*TypeParam, p *TypeParam) bool {
	for _, x := range ps {
		if x == p {
			return true
		}
	}
	return false
}

// ExtSubst is the substitution of f's extension's type parameters a call
// on a receiver of type recv takes: the lowering's, per instantiation.
func ExtSubst(f *Function, recv Type) Subst {
	s := Subst{}
	if f.Ext != nil {
		matchType(f.Ext.Target, recv, f.Ext.TypeParams, s)
	}
	return s
}

// An extHit is what an extension lookup found.
type extHit struct {
	f *Function
	g *Global
	s Subst
}

// extLookup finds what the extensions the package sees give t under
// name: kind "method", "static" or "const". More than one, from
// different extensions, is an error at at; one whose constraints t
// doesn't meet says so (why), when it's all there is.
func (c *Checker) extLookup(t Type, name, kind string, at syntax.Node) (extHit, string) {
	var hits []extHit
	var exts []*Extension
	why := ""
	for _, e := range c.visibleExts() {
		var f *Function
		var g *Global
		switch kind {
		case "method":
			f = e.Methods[name]
		case "static":
			f = e.Statics[name]
		case "const":
			g = e.Consts[name]
		}
		if f == nil && g == nil {
			continue
		}
		pub := g != nil && g.Pub || f != nil && f.Decl.Pub.IsValid()
		if !c.visible(e, pub) {
			continue
		}
		if f != nil && privateFn(f) && !c.sameFile(f.pos, at) || g != nil && privateConst(g) && !c.sameFile(g.pos, at) {
			continue // the extension's file's only
		}
		s, w, ok := c.extMatch(e, t)
		if !ok && kind != "method" && openGeneric(t) {
			// Box.of(…) of extend<T: Comparable> Box<T>: the type's
			// arguments aren't known yet; the call infers them, and
			// checks the extension's constraints.
			if n, isN := e.Target.(*Named); isN && n.Obj == t.(*Named).Obj {
				s, w, ok = Subst{}, "", true
			}
		}
		if !ok {
			if w != "" {
				why = name + " is " + e.Name() + "'s, and " + w
			}
			continue
		}
		hits = append(hits, extHit{f, g, s})
		exts = append(exts, e)
	}
	if len(hits) > 1 && at != nil {
		var where []string
		for _, e := range exts {
			where = append(where, pkgDisplay(e.Pkg))
		}
		sort.Strings(where)
		if where[0] == where[len(where)-1] {
			c.errorf(at, diag.ExtensionConflict, "%s is extended twice for %s in %s: keep one", name, typeString(t), where[0])
		} else {
			c.errorf(at, diag.ExtensionConflict, "%s is extended by %s; import one", name, strings.Join(where, " and "))
		}
	}
	if len(hits) == 0 {
		return extHit{}, why
	}
	return hits[0], ""
}

func pkgDisplay(p *Package) string {
	if p.Path == "" {
		return "this package"
	}
	return p.Path
}

// extMethodCall is x.name(…) of an extension's method f, its type
// parameters s.
func (c *Checker) extMethodCall(e *syntax.CallExpr, fn *syntax.MemberExpr, xt Type, hit extHit, want Type) Type {
	c.info.Uses[fn.Name] = hit.f
	c.info.Selections[fn] = &Selection{Kind: SelMethod, Name: fn.Name.Name, Recv: xt, Obj: hit.f}
	return c.funcCallInfo(e, hit.f, fn.X, xt, want).Result
}

// ---------------------------------------------------------------------
// self's methods by their bare names

// implicitSelf is a bare call or name in a method's body (an extension's
// or a type's) that's self's method or property: the type's built-in
// ones and those extensions give it (a struct's own fields and methods
// are in scope already). It's checked, and lowered, as self.name.
func (c *Checker) implicitSelf(id *syntax.Ident, call bool) syntax.Expr {
	if c.ctx == nil || c.ctx.Self == nil {
		return nil
	}
	switch c.lookup(id.Name).(type) {
	case *Var, *selfMember, *Member:
		return nil
	}
	t := c.ctx.Self.Type
	name := id.Name
	n, _ := t.(*Named)
	switch {
	case call && n != nil && n.Obj.Methods[name] != nil:
	case call && lookupMethod(t, name) != nil:
	case !call && lookupField(t, name) != nil:
	case call:
		if hit, _ := c.extLookup(t, name, "method", nil); hit.f == nil {
			return nil
		}
	default:
		return nil
	}
	self := &syntax.SelfExpr{Span: syntax.Span{Start: id.Start, End: id.Start}}
	return &syntax.MemberExpr{Span: id.Span, X: self, Name: id}
}

// ExtImplKey is the key of Info.ExtImpls for type t's method name.
func ExtImplKey(t *Named, name string) string {
	return fmt.Sprintf("%p|%s.%s", t.Obj, typeString(t), name)
}

// extImpl records that extension method f gives t its method name where
// t has an interface: a shape has one table of methods in a program, so
// two extensions that give it one name are an error.
func (c *Checker) extImpl(t *Named, name string, f *Function) {
	key := ExtImplKey(t, name)
	old := c.info.ExtImpls[key]
	switch {
	case old == nil:
		c.info.ExtImpls[key] = f
	case old != f:
		a, b := pkgDisplay(old.Pkg), pkgDisplay(f.Pkg)
		if a == b {
			c.errorf(f.Decl.Name, diag.ExtensionConflict, "%s is extended twice for %s, and both make it an interface's: keep one", name, typeString(t))
		} else {
			c.errorf(f.Decl.Name, diag.ExtensionConflict, "%s is extended by %s and %s, and both make %s an interface's: a type has one %s in a program", name, a, b, typeString(t), name)
		}
	}
}

// ExtMethod is the extension's method that gives t its method name where
// it has an interface, nil for none.
func (p *Program) ExtMethod(t *Named, name string) *Function {
	return p.Info.ExtImpls[ExtImplKey(t, name)]
}

// VisibleExtensions are the extensions pkg's code sees: its own, and its
// imports' (their pub members), for editors.
func VisibleExtensions(pkg *Package) []*Extension {
	if pkg == nil {
		return nil
	}
	exts := append([]*Extension{}, pkg.Exts...)
	for _, p := range pkg.Imports {
		exts = append(exts, p.Exts...)
	}
	return exts
}

// Extends reports whether e extends type t (its constraints aside).
func (e *Extension) Extends(t Type) bool {
	if e.Target == nil || IsInvalid(e.Target) {
		return false
	}
	return matchType(e.Target, t, e.TypeParams, Subst{})
}

// SeenFrom reports whether pkg's code sees e's member (pub: declared so).
func (e *Extension) SeenFrom(pkg *Package, pub bool) bool { return e.Pkg == pkg || pub }
