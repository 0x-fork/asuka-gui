// Package appbuild makes what each platform runs an Asuka app with, from
// its compiled module (lang/bytecode): the C core built for a target with
// the app compiled to C (a release, lang/cgen) or without it (a dev
// player, which loads the bytecode asuka dev sends), the desktop hosts'
// executable (cmd/asuka-host) and its settings, the web's page, and the
// runs of a module's tests on the fake host (asuka-run). The CLI
// (cmd/asuka) packages them for each platform.
package appbuild

import (
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/pkgs"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/compile"
	"github.com/egoist/asuka/lang/syntax"
	"github.com/egoist/asuka/std"
)

// Compile compiles the app (or package) in dir, with its dependencies
// (deps: package ID → directory, from internal/pkgs), to a module; its
// diagnostics are "file:line:col: message". An error with no diagnostics
// is one that isn't the source's (a file that can't be read). main adds
// the package's fn main() as a test named main (asuka run). It's
// lang/compile; tests replace it.
var Compile = compileDir

// DevLang and DevDirection are asuka dev's --lang and --dir: the language
// and the direction compiled into the module for this run, on whatever
// platform runs it ("": the user's, the language's).
var DevLang, DevDirection string

// Warnings receives what's worth saying that doesn't stop a build (the
// catalogs' missing translations), "file:line:col: warning: message".
var Warnings io.Writer = os.Stderr

func compileDir(dir string, deps map[string]string, main bool) (*bytecode.Module, []string, error) {
	// Imports resolve through the project's asuka.lock (compile.Dir's
	// own), which Load made ready.
	res, err := compile.Dir(dir, &compile.Options{MainTest: main, Lang: DevLang, Direction: DevDirection})
	if err != nil {
		return nil, nil, err
	}
	if Warnings != nil {
		for _, w := range res.Warnings {
			pos := w.Pos
			if r, err := filepath.Rel(dir, pos.Filename); err == nil && !strings.HasPrefix(r, "..") {
				pos.Filename = r
			}
			fmt.Fprintf(Warnings, "%s: warning: %s\n", pos, w.Msg)
		}
	}
	if len(res.Diags) > 0 {
		diags := make([]string, len(res.Diags))
		for i, d := range res.Diags {
			diags[i] = d.String()
		}
		return nil, diags, nil
	}
	return res.Module, nil, nil
}

// Project is an app or a package: its directory and its dependencies'.
type Project struct {
	Dir string
	// Deps maps each dependency's package ID (asuka.lock's), and the name
	// asuka.toml gives it when that's another, to its directory.
	Deps map[string]string
	// Local are the dependencies that are directories of their own
	// (file:), which a change to is a change to the app.
	Local []string
	// File, for a project of one file (asuka run file.asuka), is that file.
	File string
	// uses is what Uses last found.
	uses *Uses
}

// Load reads the project in dir (or the one file dir names): its
// dependencies resolved and fetched (asuka.lock; pkgs.Install) when
// asuka.toml has any.
func Load(dir string) (*Project, error) {
	abs, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}
	st, err := os.Stat(abs)
	if err != nil {
		return nil, err
	}
	p := &Project{Dir: abs, Deps: map[string]string{}}
	if !st.IsDir() {
		if !strings.HasSuffix(abs, ".asuka") {
			return nil, fmt.Errorf("%s: not an .asuka file", dir)
		}
		p.Dir, p.File = filepath.Dir(abs), abs
		return p, nil
	}
	m, err := pkgs.ReadManifest(abs)
	if err != nil {
		return nil, err
	}
	if len(m.Dependencies) == 0 {
		return p, nil
	}
	c := pkgs.NewClient(pkgs.LoadNPMRC(abs))
	l, err := pkgs.Install(c, abs, false)
	if err != nil {
		return nil, err
	}
	dirs, err := pkgs.Dirs(c, l, abs)
	if err != nil {
		return nil, err
	}
	for id, d := range dirs {
		p.Deps[id] = d
	}
	for key, id := range l.Root {
		if d, ok := dirs[id]; ok && key != id {
			p.Deps[key] = d
		}
	}
	for _, lp := range l.Packages {
		if strings.HasPrefix(lp.Source, "file:") {
			p.Local = append(p.Local, dirs[lp.ID])
		}
	}
	sort.Strings(p.Local)
	return p, nil
}

// Compile compiles the project (Compile).
func (p *Project) Compile() (*bytecode.Module, []string, error) {
	if p.File != "" {
		// One file: a directory of its own, so that what's beside it
		// isn't compiled with it.
		tmp, err := os.MkdirTemp("", "asuka-run-")
		if err != nil {
			return nil, nil, err
		}
		defer os.RemoveAll(tmp)
		b, err := os.ReadFile(p.File)
		if err != nil {
			return nil, nil, err
		}
		if err := os.WriteFile(filepath.Join(tmp, filepath.Base(p.File)), b, 0o644); err != nil {
			return nil, nil, err
		}
		m, diags, err := Compile(tmp, p.Deps, true)
		for i, d := range diags {
			diags[i] = strings.ReplaceAll(d, filepath.Join(tmp, filepath.Base(p.File)), p.File)
		}
		if m != nil && m.C != nil && len(m.C.Dirs) > 0 {
			// Its C is the [c] of the asuka.toml beside it.
			c := *m.C
			c.Dirs = append([]string{p.Dir}, c.Dirs[1:]...)
			m.C = &c
		}
		return m, diags, err
	}
	return Compile(p.Dir, p.Deps, false)
}

// WatchRoots are the directories whose .asuka files make the app: its
// own and its local dependencies'.
func (p *Project) WatchRoots() []string {
	return append([]string{p.Dir}, p.Local...)
}

// skipDir reports whether a directory is no part of a package's sources:
// hidden ones, build outputs, platforms' projects.
func skipDir(name string) bool {
	return strings.HasPrefix(name, ".") || name == "dist" || name == "build" || name == "node_modules" || name == "testdata"
}

// Sources are the .asuka files of the project and its dependencies.
func (p *Project) Sources() []string {
	if p.File != "" {
		return []string{p.File}
	}
	var out []string
	roots := []string{p.Dir}
	for _, d := range p.Deps {
		roots = append(roots, d)
	}
	slices.Sort(roots)
	for _, r := range slices.Compact(roots) {
		filepath.WalkDir(r, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return nil
			}
			if d.IsDir() {
				if path != r && skipDir(d.Name()) {
					return filepath.SkipDir
				}
				return nil
			}
			if strings.HasSuffix(path, ".asuka") {
				out = append(out, path)
			}
			return nil
		})
	}
	return out
}

// Uses is what the program (the project and every package it imports,
// all the way down) imports and refers to, as the checker resolved it,
// which decides what a platform's build needs: a package's manifest's
// needs (asuka.toml), and those of its members (its [uses.<member>]
// tables), the parts of a host that link frameworks of their own.
type Uses struct {
	// Imports are the packages imported anywhere in the program, by
	// import path (asuka/location, @acme/charts).
	Imports map[string]bool
	// Refs are the members referred to from outside the package that
	// declares them, anywhere in the program (a dependency that calls
	// location.requestAlways counts as the app would): "<import
	// path>.<name>" for a package's function, type, store, state or let
	// (asuka/location.requestAlways), "<import path>.<Type>.<name>" for a
	// member of its type (asuka/icloud.Value.set); the prelude's by name
	// alone (Video). Test blocks' don't count: they aren't the app's.
	Refs map[string]bool
}

// Import reports whether the program imports pkg.
func (u *Uses) Import(pkg string) bool { return u.Imports[pkg] }

// Ref reports whether the program refers to pkg's member (pkg "": the
// prelude's), from outside pkg.
func (u *Uses) Ref(pkg, member string) bool {
	if pkg == "" {
		return u.Refs[member]
	}
	return u.Refs[pkg+"."+member]
}

// Uses checks the project's program (nothing more than the checker: no
// lowering) for what it imports and refers to. A program with errors
// counts for what the checker resolved.
func (p *Project) Uses() *Uses {
	u := &Uses{Imports: map[string]bool{}, Refs: map[string]bool{}}
	p.uses = u
	dir := p.Dir
	if p.File != "" {
		// One file: a directory of its own, as Compile does.
		tmp, err := os.MkdirTemp("", "asuka-uses-")
		if err != nil {
			return u
		}
		defer os.RemoveAll(tmp)
		b, err := os.ReadFile(p.File)
		if err != nil {
			return u
		}
		if err := os.WriteFile(filepath.Join(tmp, filepath.Base(p.File)), b, 0o644); err != nil {
			return u
		}
		dir = tmp
	}
	res, err := compile.Dir(dir, &compile.Options{CheckOnly: true, NoCatalogs: true})
	if err != nil || res.Program == nil {
		return u
	}
	usesOf(res.Program, u)
	return u
}

// usesOf adds prog's imports and its packages' references to each
// other's members to u.
func usesOf(prog *check.Program, u *Uses) {
	info := prog.Info
	for _, pkg := range prog.Packages {
		if pkg.Prelude || pkg == prog.Root {
			continue
		}
		u.Imports[pkg.Path] = true
	}
	// ref notes that from refers to name, of decl (nil: the prelude) or
	// of its type owner.
	ref := func(from, decl *check.Package, owner *check.TypeName, name string) {
		if decl == from || decl == nil && owner == nil {
			return
		}
		if owner != nil {
			if owner.Pkg == from {
				return
			}
			decl, name = owner.Pkg, owner.Name()+"."+name
		}
		if decl == nil || decl.Prelude {
			u.Refs[name] = true
			return
		}
		if decl.Path == "" || strings.HasPrefix(decl.Path, ".") {
			return // the app's own
		}
		u.Refs[decl.Path+"."+name] = true
	}
	named := func(t check.Type) *check.TypeName {
		for {
			switch x := t.(type) {
			case *check.Named:
				return x.Obj
			case *check.Optional:
				t = x.Elem
			default:
				return nil
			}
		}
	}
	object := func(from *check.Package, o check.Object) {
		switch o := o.(type) {
		case *check.Function:
			if o.Recv != nil {
				ref(from, nil, o.Recv, o.Name())
			} else {
				ref(from, o.Pkg, nil, o.Name())
			}
		case *check.TypeName:
			ref(from, o.Pkg, nil, o.Name())
		case *check.Global:
			if o.Owner != nil {
				ref(from, nil, o.Owner, o.Name())
			} else {
				ref(from, o.Pkg, nil, o.Name())
			}
		case *check.Member:
			if o.Owner != nil {
				ref(from, nil, o.Owner, o.Name())
			}
		}
	}
	for _, pkg := range prog.Packages {
		if pkg.Prelude {
			continue
		}
		for _, f := range pkg.Files {
			syntax.Inspect(f, func(n syntax.Node) bool {
				switch n := n.(type) {
				case *syntax.TestDecl:
					return false
				case *syntax.Ident:
					if o := info.Uses[n]; o != nil {
						object(pkg, o)
					}
				case *syntax.CallExpr:
					if e := info.Elements[n]; e != nil && e.Comp != nil {
						object(pkg, e.Comp)
					}
				case *syntax.MemberExpr:
					if sel := info.Selections[n]; sel != nil {
						switch sel.Kind {
						case check.SelField, check.SelCase, check.SelIfaceField:
							ref(pkg, nil, named(sel.Recv), sel.Name)
						case check.SelPkg:
						default:
							if sel.Obj != nil {
								object(pkg, sel.Obj)
							}
						}
					}
				}
				return true
			})
		}
	}
}

// ImportList is u's imports, sorted.
func (u *Uses) ImportList() []string {
	out := make([]string, 0, len(u.Imports))
	for k := range u.Imports {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

// Std are the standard library's packages the project imports, with
// those they import: import path (asuka/network) → directory (package
// std's). The compiler's own (asuka/json, asuka/math) aren't directories,
// and aren't here.
func (p *Project) Std() map[string]string {
	u := p.uses
	if u == nil {
		u = p.Uses()
	}
	out := map[string]string{}
	for imp := range u.Imports {
		if dir, ok := std.Package(imp); ok {
			out[imp] = dir
		}
	}
	return out
}
