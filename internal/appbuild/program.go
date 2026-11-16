package appbuild

import (
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"sort"
	"strings"
	"sync"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/internal/pkgs"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/compile"
	"github.com/egoist/asuka/platform/desktop"
)

// Programs (docs/programs.md): a package whose fn main is its entry, a
// command-line tool. A project is one (asuka new --bin), or has some
// beside its app or library, each in a directory of its own named in
// asuka.toml:
//
//	[bin.notes]
//	path = "cli"

// Bin is one of a project's programs.
type Bin struct {
	Name string // the executable's
	Dir  string // its package's directory
}

// bins reads a project's [bin] tables.
func bins(dir string, m *pkgs.Manifest) ([]Bin, error) {
	var out []Bin
	for name, b := range m.Bin {
		if !validBinName(name) {
			return nil, fmt.Errorf("asuka.toml: [bin.%s]: a program's name is letters, digits, - and _", name)
		}
		if b.Path == "" {
			return nil, fmt.Errorf("asuka.toml: [bin.%s] needs path, its package's directory (path = \"cli\")", name)
		}
		if filepath.IsAbs(b.Path) {
			return nil, fmt.Errorf("asuka.toml: [bin.%s]: path is relative to the project", name)
		}
		d := filepath.Join(dir, filepath.FromSlash(b.Path))
		if d == dir {
			return nil, fmt.Errorf("asuka.toml: [bin.%s]: path is a directory of its own, not the project's (a project that's a program needs no [bin])", name)
		}
		out = append(out, Bin{Name: name, Dir: d})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out, nil
}

func validBinName(s string) bool {
	if s == "" || s[0] == '-' || s[0] == '.' {
		return false
	}
	for _, c := range s {
		if !(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '-' || c == '_' || c == '.') {
			return false
		}
	}
	return true
}

// binRoots maps a [bin] program's directory to its project's, which its
// compile resolves its dependencies and reads its catalogs through.
var binRoots sync.Map

func binRoot(dir string) string {
	if r, ok := binRoots.Load(filepath.Clean(dir)); ok {
		return r.(string)
	}
	return ""
}

// Bin is the project's program named name, as a project of its own: its
// directory, the project's dependencies.
func (p *Project) Bin(name string) (*Project, error) {
	for _, b := range p.Bins {
		if b.Name == name {
			if _, err := os.Stat(b.Dir); err != nil {
				return nil, fmt.Errorf("[bin.%s]: %v", name, err)
			}
			binRoots.Store(filepath.Clean(b.Dir), p.Dir)
			return &Project{Dir: b.Dir, Deps: p.Deps, Local: p.Local, Root: p.Dir}, nil
		}
	}
	if len(p.Bins) == 0 {
		return nil, fmt.Errorf("--bin %s: asuka.toml has no [bin] programs", name)
	}
	return nil, fmt.Errorf("--bin %s: asuka.toml's programs are %s", name, binNames(p.Bins))
}

func binNames(bs []Bin) string {
	var names []string
	for _, b := range bs {
		names = append(names, b.Name)
	}
	return strings.Join(names, ", ")
}

// ProgramName is the name a project's program runs as: its [bin]'s, or
// the project's (asuka.toml's name, else its directory's).
func (p *Project) ProgramName() string {
	if p.Root != "" {
		for _, b := range mustBins(p.Root) {
			if filepath.Clean(b.Dir) == filepath.Clean(p.Dir) {
				return b.Name
			}
		}
	}
	if p.File != "" {
		return strings.TrimSuffix(filepath.Base(p.File), ".asuka")
	}
	if m, err := pkgs.ReadManifest(p.Dir); err == nil && m.Name != "" {
		n := m.Name
		if i := strings.LastIndexByte(n, '/'); i >= 0 {
			n = n[i+1:]
		}
		return n
	}
	return filepath.Base(p.Dir)
}

func mustBins(dir string) []Bin {
	m, err := pkgs.ReadManifest(dir)
	if err != nil {
		return nil
	}
	b, _ := bins(dir, m)
	return b
}

// ProgramOptions say how a program runs on the VM (asuka run).
type ProgramOptions struct {
	Name           string   // what os.program() says
	Args           []string // os.args()
	Dir            string   // its working directory ("": this one)
	Trim           string   // what an error's trace names files relative to
	Env            []string // more of its environment (KEY=value)
	Stdin          io.Reader
	Stdout, Stderr io.Writer
}

// RunProgram runs m's fn main on the VM (asuka-run -main): the program
// with its arguments, its standard streams the given ones, its exit code
// returned. An error is the runner's own failure.
func RunProgram(m *bytecode.Module, o ProgramOptions) (int, error) {
	r, err := StartProgram(m, o)
	if err != nil {
		return 0, err
	}
	return r.Wait()
}

// A Run is a program running on the VM (StartProgram).
type Run struct {
	Cmd     *exec.Cmd
	cleanup func()
}

// Wait waits for it to end: its exit code.
func (r *Run) Wait() (int, error) {
	defer r.cleanup()
	err := r.Cmd.Wait()
	var exit *exec.ExitError
	if errors.As(err, &exit) && exit.ExitCode() >= 0 {
		return exit.ExitCode(), nil
	}
	if err != nil {
		return 0, fmt.Errorf("asuka-run: %v", err)
	}
	return 0, nil
}

// StartProgram starts m's fn main on the VM, as RunProgram does, and
// returns as it runs.
func StartProgram(m *bytecode.Module, o ProgramOptions) (*Run, error) {
	if m.Main < 0 {
		return nil, errors.New("not a program: it has no fn main")
	}
	work, err := os.MkdirTemp("", "asuka-run-")
	if err != nil {
		return nil, err
	}
	exe, err := RunnerFor(m, work)
	if err != nil {
		os.RemoveAll(work)
		return nil, err
	}
	p, cleanup, err := writeModule(StripTests(m))
	if err != nil {
		os.RemoveAll(work)
		return nil, err
	}
	name := o.Name
	if name == "" {
		name = "main"
	}
	args := []string{"-main", "-program", name}
	if o.Trim != "" {
		args = append(args, "-trim", filepath.Clean(o.Trim))
	}
	args = append(append(args, p, "--"), o.Args...)
	cmd := exec.Command(exe, args...)
	cmd.Env = append(append(os.Environ(), compile.PluginEnv(m)...), o.Env...)
	cmd.Dir = o.Dir
	cmd.Stdin, cmd.Stdout, cmd.Stderr = o.Stdin, o.Stdout, o.Stderr
	if err := cmd.Start(); err != nil {
		cleanup()
		os.RemoveAll(work)
		return nil, err
	}
	return &Run{Cmd: cmd, cleanup: func() { cleanup(); os.RemoveAll(work) }}, nil
}

// ProgramBuild says how ProgramExe builds a program's executable.
type ProgramBuild struct {
	// Name and Version are the program's (a headless host's
	// asuka_desktop_app_name and _version).
	Name, Version string
	// Natives are the directories of the packages it uses with C for the
	// desktop (Windows, Linux): with any, it runs on the headless host.
	Natives []string
	// Plugins: it loads plugins' libraries (std/sqlite's), which a
	// static C library can't.
	Plugins bool
	Debug   bool
	// Root is what an error's trace names its files relative to (the
	// project's directory): not the build machine's paths.
	Root string
}

// ProgramExe builds a program's executable for t at out: the core with m
// compiled to C (its main), statically linked as far as the platform
// allows (Linux's musl, with Zig, when nothing it does needs the
// system's libraries loaded), and with the headless host
// (platform/desktop/program) when its packages have C for Windows or
// Linux.
func ProgramExe(t cbuild.Target, m *bytecode.Module, o ProgramBuild, out string) error {
	core, err := cbuild.CoreDir()
	if err != nil {
		return err
	}
	s := StripTests(m)
	if o.Root != "" {
		s.Funcs = slices.Clone(s.Funcs)
		for i := range s.Funcs {
			if r, err := filepath.Rel(o.Root, s.Funcs[i].File); err == nil && filepath.IsLocal(r) {
				s.Funcs[i].File = filepath.ToSlash(r)
			}
		}
	}
	app, err := AppC(s)
	if err != nil {
		return err
	}
	ffi, err := Foreign(m.C, t)
	if err != nil {
		return err
	}
	po := cbuild.ProgramOptions{App: app, Foreign: ffi, Debug: o.Debug}
	if len(o.Natives) > 0 && t.OS != "darwin" {
		natives, err := DesktopNatives(t, o.Natives)
		if err != nil {
			return err
		}
		d, err := desktop.Dir()
		if err != nil {
			return err
		}
		po.Host = append(append([]string{}, natives.Sources...), desktop.Headless(d))
		po.Deps = natives.Deps
		po.HostFlags = append(append([]string{}, natives.Flags...),
			"-DASUKA_PROGRAM_NAME="+cQuote(o.Name), "-DASUKA_PROGRAM_VERSION="+cQuote(o.Version))
		po.Link = natives.Libs
	}
	var tc *cbuild.Toolchain
	if t.OS == "linux" && len(po.Host) == 0 && ffi == nil && !o.Plugins {
		if z, err := cbuild.Musl(t.Arch); err == nil {
			tc, po.Static = z, true
		}
	}
	if tc == nil {
		if tc, err = cbuild.ForTarget(t); err != nil {
			return err
		}
	}
	return tc.ProgramExe(core, po, out)
}

// cQuote is s as a C string literal, for a -D flag.
func cQuote(s string) string {
	var b strings.Builder
	b.WriteByte('"')
	for _, c := range []byte(s) {
		switch {
		case c == '"' || c == '\\':
			b.WriteByte('\\')
			b.WriteByte(c)
		case c < 0x20 || c >= 0x7f:
			fmt.Fprintf(&b, "\\%03o", c)
		default:
			b.WriteByte(c)
		}
	}
	b.WriteByte('"')
	return b.String()
}
