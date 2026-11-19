package check

import (
	"path/filepath"
	"strings"

	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// private: a declaration, a type's member too, its file's only (Rust's
// privacy, a file being the module). The file's own code (its types'
// methods, its functions, its extensions) uses it as any; other files,
// of its package or not, can't name it: not-pub, as a package's
// declarations that aren't pub are to other packages.

// IsPrivate reports whether o is declared private, and where.
func IsPrivate(o Object) (syntax.Pos, bool) {
	switch o := o.(type) {
	case *Function:
		if o.Decl != nil && o.Decl.Private.IsValid() {
			return o.pos, true
		}
	case *Global:
		if d, ok := o.Decl.(*syntax.LetDecl); ok && d.Private.IsValid() {
			return o.pos, true
		}
	case *TypeName:
		if privateDecl(o.Decl) {
			return o.pos, true
		}
	}
	return syntax.NoPos, false
}

func privateDecl(n syntax.Node) bool {
	switch d := n.(type) {
	case *syntax.StructDecl:
		return d.Private.IsValid()
	case *syntax.EnumDecl:
		return d.Private.IsValid()
	case *syntax.InterfaceDecl:
		return d.Private.IsValid()
	case *syntax.TypeAliasDecl:
		return d.Private.IsValid()
	}
	return false
}

// sameFile reports whether at may use what's declared private at decl:
// at is in its file, or in a test file of its package (name_test.asuka,
// in the same directory), whose tests see the package's insides.
func (c *Checker) sameFile(decl syntax.Pos, at syntax.Node) bool {
	if at == nil {
		return false
	}
	df, af := c.fset.File(decl), c.fset.File(at.Range().Start)
	if df == af {
		return true
	}
	if df == nil || af == nil || !strings.HasSuffix(af.Name(), "_test.asuka") {
		return false
	}
	return filepath.Dir(df.Name()) == filepath.Dir(af.Name())
}

// fileName is the name of the file decl is in, as an error says it.
func (c *Checker) fileName(decl syntax.Pos) string {
	if f := c.fset.File(decl); f != nil {
		return filepath.Base(f.Name())
	}
	return "its file"
}

// hiddenObj reports (and says) o, a private declaration, named at at in
// another file.
func (c *Checker) hiddenObj(at syntax.Node, name string, o Object) bool {
	pos, ok := IsPrivate(o)
	if !ok || c.sameFile(pos, at) {
		return false
	}
	c.errorf(at, diag.NotPub, "%s is private to %s", name, c.fileName(pos))
	return true
}

// hiddenMember reports (and says) a type's private member (a field, a
// method, a static fn, a constant) used at at in another file than the
// one it's declared in.
func (c *Checker) hiddenMember(at syntax.Node, private bool, decl syntax.Pos, owner, name string) bool {
	if !private || c.sameFile(decl, at) {
		return false
	}
	c.errorf(at, diag.NotPub, "%s.%s is private to %s", owner, name, c.fileName(decl))
	return true
}

// privateFn reports whether f is private.
func privateFn(f *Function) bool { return f != nil && f.Decl != nil && f.Decl.Private.IsValid() }

// privateConst reports whether a type's or an extension's constant g is
// private.
func privateConst(g *Global) bool {
	d, ok := g.Decl.(*syntax.LetDecl)
	return ok && d.Private.IsValid()
}

// fnMember reports (and says) a private method or static fn f of
// owner's used at at in another file.
func (c *Checker) hiddenFn(at syntax.Node, owner string, f *Function) bool {
	return c.hiddenMember(at, privateFn(f), f.pos, owner, f.name)
}

// hiddenConst is hiddenFn's for a type's constant.
func (c *Checker) hiddenConst(at syntax.Node, owner string, g *Global) bool {
	return c.hiddenMember(at, privateConst(g), g.pos, owner, g.name)
}

// hiddenField is hiddenFn's for a struct's field.
func (c *Checker) hiddenField(at syntax.Node, owner string, f *Field) bool {
	return c.hiddenMember(at, f.Private, f.Pos, owner, f.Name)
}
