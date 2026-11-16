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
			fmt.Fprintf(Warnings, "%s: warning: %s\n", w.Pos, w.Msg)
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

// Uses is what the project's sources import and name: the packages
// (asuka/location, @acme/charts) and the identifiers (WebView, Video),
// which decide what a platform's build needs (permissions, the parts of
// a host that link frameworks of their own). Files that don't parse
// count for what they declare before the error.
type Uses struct {
	Imports map[string]bool
	Names   map[string]bool
}

// Imports reports whether the project imports pkg.
func (u *Uses) Import(pkg string) bool { return u.Imports[pkg] }

// Uses reads the project's sources for what they import and name.
func (p *Project) Uses() *Uses {
	u := &Uses{Imports: map[string]bool{}, Names: map[string]bool{}}
	fset := syntax.NewFileSet()
	for _, f := range p.Sources() {
		b, err := os.ReadFile(f)
		if err != nil {
			continue
		}
		file, _ := syntax.ParseFile(fset, f, b)
		if file == nil {
			continue
		}
		for _, im := range file.Imports {
			if !im.Relative {
				u.Imports[im.PathString()] = true
			}
		}
		syntax.Inspect(file, func(n syntax.Node) bool {
			if id, ok := n.(*syntax.Ident); ok {
				u.Names[id.Name] = true
			}
			return true
		})
	}
	return u
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
	out := map[string]string{}
	var queue []string
	for imp := range p.Uses().Imports {
		queue = append(queue, imp)
	}
	fset := syntax.NewFileSet()
	for len(queue) > 0 {
		imp := queue[0]
		queue = queue[1:]
		if _, done := out[imp]; done {
			continue
		}
		dir, ok := std.Package(imp)
		if !ok {
			continue
		}
		out[imp] = dir
		files, _ := filepath.Glob(filepath.Join(dir, "*.asuka"))
		for _, f := range files {
			b, err := os.ReadFile(f)
			if err != nil {
				continue
			}
			if file, _ := syntax.ParseFile(fset, f, b); file != nil {
				for _, im := range file.Imports {
					if !im.Relative {
						queue = append(queue, im.PathString())
					}
				}
			}
		}
	}
	return out
}
