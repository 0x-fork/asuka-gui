// Package compile is the Asuka compiler's driver: a package directory,
// the packages it imports (beside it, from the standard library), and
// the prelude, checked (lang/check) and lowered (lang/ir) into one
// bytecode module (lang/bytecode), with the diagnostics found.
package compile

import (
	"errors"
	"fmt"
	"os"
	pathpkg "path"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"github.com/egoist/asuka/internal/pkgs"
	"github.com/egoist/asuka/internal/toml"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/ir"
	"github.com/egoist/asuka/lang/syntax"
	"github.com/egoist/asuka/std"
)

// Options say how to compile.
type Options struct {
	// Packages are packages given as sources rather than directories, by
	// import path (asuka/http stubs in tests): file name → source.
	Packages map[string]map[string][]byte
	// MainTest adds the root package's fn main() as a test named main
	// (asuka run).
	MainTest bool
	// Resolve finds a dependency: the package that from (a dependency's
	// ID; "" for the root package) imports as path (@acme/charts). Dir
	// resolves through the project's asuka.lock when it's nil.
	Resolve func(from, path string) (id, dir string, err error)
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
	id      string // a dependency's ID in asuka.lock ("" for the project's own)
	dir     string
	files   []*syntax.SourceFile
	imports map[string]*unit // by the path as written; nil: it failed (reported)
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
	abs, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}
	if opts.Resolve == nil {
		o := *opts
		o.Resolve = pkgs.NewImports(abs).Resolve
		opts = &o
	}
	l := &loader{fset: syntax.NewFileSet(), opts: opts, units: map[string]*unit{}}
	root, err := l.loadDir("", "", abs)
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
	root := l.loadSources("", "", ".", files)
	return l.compile(root)
}

func (l *loader) loadDir(path, id, dir string) (*unit, error) {
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
	u := l.loadSources(path, id, dir, files)
	return u, nil
}

func (l *loader) loadSources(path, id, dir string, files map[string][]byte) *unit {
	key := dir
	if path != "" && l.opts.Packages[path] != nil {
		key = "pkg:" + path
	}
	if u, ok := l.units[key]; ok {
		return u
	}
	u := &unit{path: path, id: id, dir: dir, imports: map[string]*unit{}, std: map[string]bool{}}
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
				u.imports[ip] = l.loadSources(ip, "", "pkg:"+ip, src)
				continue
			}
			var dep *unit
			var err error
			switch {
			case imp.Relative:
				rel := strings.TrimPrefix(ip, "./")
				d := filepath.Join(u.dir, filepath.FromSlash(rel))
				// Its path names it apart from another package's ./util.
				p := ip
				if base := u.path; base != "" {
					p = pathpkg.Join(base, rel)
				}
				dep, err = l.loadDir(p, u.id, d)
			case check.ProvidesStd(ip):
				u.std[ip] = true // the checker provides it
				continue
			case strings.HasPrefix(ip, "asuka/"):
				d, ok := std.Package(ip)
				if !ok {
					// The checker says what the standard library has.
					u.std[ip] = true
					continue
				}
				dep, err = l.loadDir(ip, "", d)
			default:
				if l.opts.Resolve == nil {
					err = fmt.Errorf("a dependency needs a project (asuka.toml)")
					break
				}
				var id, d string
				if id, d, err = l.opts.Resolve(u.id, ip); err == nil {
					dep, err = l.loadDir(ip, id, d)
				}
			}
			if err != nil {
				pos := l.fset.Position(imp.Start)
				what := "can't import"
				if imp.Relative {
					what = "can't read package"
				}
				l.diags = append(l.diags, check.Diagnostic{Pos: pos, End: pos, Msg: fmt.Sprintf("%s %s: %v", what, ip, err)})
				u.imports[ip] = nil // reported: the checker skips it
				continue
			}
			u.imports[ip] = dep
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
			if dep == nil {
				imports[ip] = nil
				continue
			}
			if !visit(dep) {
				pos := l.fset.Position(u.files[0].Start)
				l.diags = append(l.diags, check.Diagnostic{Pos: pos, End: pos, Msg: "import cycle through " + ip + ": packages can't import each other"})
				continue
			}
			imports[ip] = dep.pkg
		}
		name := "main"
		if u.path != "" {
			name = u.path[strings.LastIndexByte(u.path, '/')+1:]
		}
		u.pkg = c.CheckPackage(u.path, name, u.files, imports)
		u.pkg.Dir = u.dir
		u.pkg.NativeClass = nativeClass(u)
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
	accent, accentFlags, err := appAccent(root.dir)
	if err != nil {
		pos := syntax.Position{Filename: filepath.Join(root.dir, "asuka.toml"), Line: 1, Column: 1}
		res.Diags = append(res.Diags, check.Diagnostic{Pos: pos, End: pos, Msg: err.Error()})
		return res
	}
	p, diags := ir.Lower(prog, &ir.Options{MainTest: l.opts.MainTest, Accent: accent, AccentFlags: accentFlags})
	res.Diags = append(res.Diags, diags...)
	if len(res.Diags) > 0 {
		return res
	}
	res.IR = p
	res.Module = p.Encode()
	return res
}

// appAccent is the app's accent color from its asuka.toml (accent =
// "#rrggbb" or "#rrggbbaa"), as the wire's Color (0: none, the
// platform's), and its flags (1: android.dynamicColor, the wallpaper's
// colors where the system has them). The module's init gives them to the
// core, which sends them to the host as the app mounts, so asuka dev's
// player shows each app's.
func appAccent(dir string) (uint64, uint8, error) {
	if dir == "" || strings.HasPrefix(dir, "pkg:") {
		return 0, 0, nil
	}
	b, err := os.ReadFile(filepath.Join(dir, "asuka.toml"))
	if err != nil {
		return 0, 0, nil
	}
	var m struct {
		Accent  string `json:"accent"`
		Android struct {
			DynamicColor bool `json:"dynamicColor"`
		} `json:"android"`
	}
	if toml.Unmarshal(b, &m) != nil {
		return 0, 0, nil // the CLI reports the manifest's own errors
	}
	var flags uint8
	if m.Android.DynamicColor {
		flags = 1
	}
	if m.Accent == "" {
		return 0, flags, nil
	}
	h := strings.TrimPrefix(m.Accent, "#")
	if len(h) == 6 {
		h += "ff"
	}
	v, err := strconv.ParseUint(h, 16, 32)
	if len(h) != 8 || err != nil {
		return 0, 0, fmt.Errorf("asuka.toml: accent %q (want #rrggbb or #rrggbbaa)", m.Accent)
	}
	return 2<<32 | v, flags, nil // ops.Color's RGBA
}

// nativeClass is what a package's platforms' code registers its native
// interfaces under, as internal/nativegen names it: asuka.toml's name,
// else its directory's.
func nativeClass(u *unit) string {
	if strings.HasPrefix(u.dir, "pkg:") {
		return u.path
	}
	if b, err := os.ReadFile(filepath.Join(u.dir, "asuka.toml")); err == nil {
		var m struct {
			Name string `json:"name"`
		}
		if toml.Unmarshal(b, &m) == nil && m.Name != "" {
			return m.Name
		}
	}
	return filepath.Base(u.dir)
}
