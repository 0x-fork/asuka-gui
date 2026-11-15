// Package compile is the Asuka compiler's driver: a package directory,
// the packages it imports (beside it, from the standard library), and
// the prelude, checked (lang/check) and lowered (lang/ir) into one
// bytecode module (lang/bytecode), with the diagnostics found.
package compile

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/ir"
	"github.com/egoist/asuka/lang/syntax"
)

// Options say how to compile.
type Options struct {
	// Packages are packages given as sources rather than directories, by
	// import path (asuka/http stubs in tests): file name → source.
	Packages map[string]map[string][]byte
	// MainTest adds the root package's fn main() as a test named main
	// (asuka run).
	MainTest bool
}

// Result is a compilation's outcome.
type Result struct {
	Module  *bytecode.Module // nil when there are errors
	Diags   []check.Diagnostic
	Program *check.Program
	Fset    *syntax.FileSet
	IR      *ir.Program
}

// Err is the diagnostics as an error, nil without any.
func (r *Result) Err() error {
	if len(r.Diags) == 0 {
		return nil
	}
	var b strings.Builder
	for i, d := range r.Diags {
		if i > 0 {
			b.WriteByte('\n')
		}
		b.WriteString(d.String())
	}
	return errors.New(b.String())
}

// a package to check: its files, parsed, and its imports, resolved.
type unit struct {
	path    string // its import path ("" for the root)
	dir     string
	files   []*syntax.SourceFile
	imports map[string]*unit // by the path as written
	std     map[string]bool
	pkg     *check.Package
	state   int // 0 new, 1 visiting, 2 done
}

type loader struct {
	fset  *syntax.FileSet
	opts  *Options
	units map[string]*unit // by directory (or overlay path)
	diags []check.Diagnostic
}

// Dir compiles the package in dir.
func Dir(dir string, opts *Options) (*Result, error) {
	if opts == nil {
		opts = &Options{}
	}
	l := &loader{fset: syntax.NewFileSet(), opts: opts, units: map[string]*unit{}}
	abs, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}
	root, err := l.loadDir("", abs)
	if err != nil {
		return nil, err
	}
	if len(root.files) == 0 {
		return nil, fmt.Errorf("no .asuka files in %s", dir)
	}
	return l.compile(root), nil
}

// Files compiles one package made of files (name → source).
func Files(files map[string][]byte, opts *Options) *Result {
	if opts == nil {
		opts = &Options{}
	}
	l := &loader{fset: syntax.NewFileSet(), opts: opts, units: map[string]*unit{}}
	root := l.loadSources("", ".", files)
	return l.compile(root)
}

func (l *loader) loadDir(path, dir string) (*unit, error) {
	if u, ok := l.units[dir]; ok {
		return u, nil
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	files := map[string][]byte{}
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".asuka") {
			continue
		}
		p := filepath.Join(dir, e.Name())
		src, err := os.ReadFile(p)
		if err != nil {
			return nil, err
		}
		files[p] = src
	}
	u := l.loadSources(path, dir, files)
	return u, nil
}

func (l *loader) loadSources(path, dir string, files map[string][]byte) *unit {
	key := dir
	if path != "" && l.opts.Packages[path] != nil {
		key = "pkg:" + path
	}
	if u, ok := l.units[key]; ok {
		return u
	}
	u := &unit{path: path, dir: dir, imports: map[string]*unit{}, std: map[string]bool{}}
	l.units[key] = u
	names := make([]string, 0, len(files))
	for n := range files {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		f, errs := syntax.ParseFile(l.fset, n, files[n])
		for _, e := range errs {
			l.diags = append(l.diags, check.Diagnostic{Pos: e.Pos, End: e.End, Msg: e.Msg})
		}
		u.files = append(u.files, f)
	}
	for _, f := range u.files {
		for _, imp := range f.Imports {
			ip := imp.PathString()
			if _, ok := u.imports[ip]; ok || u.std[ip] {
				continue
			}
			if src := l.opts.Packages[ip]; src != nil {
				u.imports[ip] = l.loadSources(ip, "pkg:"+ip, src)
				continue
			}
			switch {
			case imp.Relative:
				d := filepath.Join(u.dir, filepath.FromSlash(strings.TrimPrefix(ip, "./")))
				dep, err := l.loadDir(ip, d)
				if err != nil {
					pos := l.fset.Position(imp.Start)
					l.diags = append(l.diags, check.Diagnostic{Pos: pos, End: pos, Msg: fmt.Sprintf("can't read package %s: %v", ip, err)})
					continue
				}
				u.imports[ip] = dep
			default:
				// The standard library (the checker knows it) or a
				// dependency (not supported yet: the checker says so).
				u.std[ip] = true
			}
		}
	}
	return u
}

func (l *loader) compile(root *unit) *Result {
	c := check.NewChecker(l.fset)
	res := &Result{Fset: l.fset}
	var visit func(u *unit) bool
	visit = func(u *unit) bool {
		switch u.state {
		case 1:
			return false
		case 2:
			return true
		}
		u.state = 1
		imports := map[string]*check.Package{}
		for ip, dep := range u.imports {
			if !visit(dep) {
				pos := l.fset.Position(u.files[0].Start)
				l.diags = append(l.diags, check.Diagnostic{Pos: pos, End: pos, Msg: "import cycle through " + ip + ": packages can't import each other"})
				continue
			}
			imports[ip] = dep.pkg
		}
		name := "main"
		if u.path != "" {
			name = u.path[strings.LastIndexAny(u.path, "/.")+1:]
		}
		u.pkg = c.CheckPackage(u.path, name, u.files, imports)
		u.pkg.Dir = u.dir
		u.state = 2
		return true
	}
	visit(root)
	prog := c.Program(root.pkg)
	res.Program = prog
	res.Diags = append(append([]check.Diagnostic{}, l.diags...), prog.Diags...)
	if len(res.Diags) > 0 {
		return res
	}
	p, diags := ir.Lower(prog, &ir.Options{MainTest: l.opts.MainTest})
	res.Diags = append(res.Diags, diags...)
	if len(res.Diags) > 0 {
		return res
	}
	res.IR = p
	res.Module = p.Encode()
	return res
}
