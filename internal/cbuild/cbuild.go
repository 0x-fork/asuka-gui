// Package cbuild builds Asuka's C core (core/) with a C compiler run as a
// program: the system's (cc: Xcode's clang, gcc or clang on Linux,
// clang on Windows) by default, or Zig's (zig cc), opted into, which
// cross-compiles to every target from any system. Nothing here uses cgo.
package cbuild

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
)

// Compiler is a C compiler: its program and the arguments that come
// before every command line (zig's "cc", a target).
type Compiler struct {
	Path string
	Args []string
}

// Find returns the compiler to use for this system:
//
//   - ASUKA_CC=zig: Zig's (zig cc);
//   - ASUKA_CC or CC set to a command: that one ("clang -fcolor-diagnostics");
//   - else the system's: cc, clang or gcc, the first found.
func Find() (Compiler, error) {
	cc := os.Getenv("ASUKA_CC")
	if cc == "" {
		cc = os.Getenv("CC")
	}
	if cc == "zig" {
		return Zig()
	}
	if cc != "" {
		f := strings.Fields(cc)
		p, err := exec.LookPath(f[0])
		if err != nil {
			return Compiler{}, fmt.Errorf("cbuild: the C compiler %q (ASUKA_CC or CC) isn't installed", f[0])
		}
		return Compiler{Path: p, Args: f[1:]}, nil
	}
	for _, name := range []string{"cc", "clang", "gcc"} {
		if p, err := exec.LookPath(name); err == nil {
			return Compiler{Path: p}, nil
		}
	}
	return Compiler{}, fmt.Errorf("cbuild: no C compiler: install your system's (Xcode's command line tools, build-essential, LLVM) or Zig with ASUKA_CC=zig")
}

// Zig returns Zig's C compiler, for builds that cross-compile (Windows
// or Linux from a Mac, the web without an LLVM that targets wasm) or
// when asked for (ASUKA_CC=zig).
func Zig() (Compiler, error) {
	p, err := exec.LookPath("zig")
	if err != nil {
		return Compiler{}, fmt.Errorf("cbuild: zig isn't installed (https://ziglang.org/download)")
	}
	return Compiler{Path: p, Args: []string{"cc"}}, nil
}

// IsZig reports whether c is Zig's.
func (c Compiler) IsZig() bool {
	return len(c.Args) > 0 && c.Args[0] == "cc" && strings.Contains(filepath.Base(c.Path), "zig")
}

// Flags are the flags every file of the core is compiled with.
var Flags = []string{"-std=c17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-missing-field-initializers"}

// Sources returns core's C files under dir (core/src, and core/host/fake
// when fake is true), sorted.
func Sources(core string, fake bool) ([]string, error) {
	var out []string
	dirs := []string{filepath.Join(core, "src")}
	if fake {
		dirs = append(dirs, filepath.Join(core, "host", "fake"))
	}
	for _, d := range dirs {
		err := filepath.WalkDir(d, func(p string, e fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if !e.IsDir() && strings.HasSuffix(p, ".c") {
				out = append(out, p)
			}
			return nil
		})
		if err != nil && !os.IsNotExist(err) {
			return nil, err
		}
	}
	sort.Strings(out)
	return out, nil
}

// Includes are the include directories of the core.
func Includes(core string) []string {
	return []string{"-I" + filepath.Join(core, "include"), "-I" + filepath.Join(core, "src"), "-I" + filepath.Join(core, "host")}
}

// Compile compiles each source to an object in out, in parallel, and
// returns the objects. extra flags come after Flags. An object already
// in out is kept (out is a cache keyed by the sources and flags), and
// each is written under a name of its own then renamed into place, so
// builds running at once share the cache safely.
func (c Compiler) Compile(srcs []string, out string, extra []string) ([]string, error) {
	return c.compile(srcs, out, append(append([]string{}, Flags...), extra...))
}

// CompileWith is Compile with flags alone, not the core's: C that isn't
// the core's (an app's [c] sources), whose warnings are its own.
func (c Compiler) CompileWith(srcs []string, out string, flags []string) ([]string, error) {
	return c.compile(srcs, out, flags)
}

func (c Compiler) compile(srcs []string, out string, flags []string) ([]string, error) {
	if err := os.MkdirAll(out, 0o755); err != nil {
		return nil, err
	}
	objs := make([]string, len(srcs))
	errs := make([]error, len(srcs))
	sem := make(chan struct{}, runtime.NumCPU())
	var wg sync.WaitGroup
	for i, src := range srcs {
		name := strings.NewReplacer("/", "_", "\\", "_", ":", "_").Replace(src)
		objs[i] = filepath.Join(out, strings.TrimSuffix(name, ".c")+".o")
		wg.Add(1)
		go func(i int, src string) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			if _, err := os.Stat(objs[i]); err == nil {
				return
			}
			tmp := fmt.Sprintf("%s.%d.tmp", objs[i], os.Getpid())
			args := append(append([]string{}, c.Args...), flags...)
			args = append(args, "-c", src, "-o", tmp)
			if errs[i] = c.run(args); errs[i] == nil {
				errs[i] = os.Rename(tmp, objs[i])
			}
		}(i, src)
	}
	wg.Wait()
	var msgs []string
	for _, e := range errs {
		if e != nil {
			msgs = append(msgs, e.Error())
		}
	}
	if msgs != nil {
		return nil, fmt.Errorf("%s", strings.Join(msgs, "\n"))
	}
	return objs, nil
}

// Link links objects into the executable exe.
func (c Compiler) Link(objs []string, exe string, extra []string) error {
	args := append(append([]string{}, c.Args...), objs...)
	args = append(args, extra...)
	args = append(args, "-o", exe)
	return c.run(args)
}

func (c Compiler) run(args []string) error {
	cmd := exec.Command(c.Path, args...)
	var out bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &out
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s %s: %v\n%s", filepath.Base(c.Path), strings.Join(args, " "), err, out.String())
	}
	return nil
}

// Runner builds asuka-run (core/runner: the VM, the core and the fake
// host) into a cache directory, once per version of the sources, and
// returns its path. sanitize builds it with ASan and UBSan (tests).
func Runner(core string, sanitize bool) (string, error) { return RunnerWith(core, nil, sanitize) }

// RunnerWith is Runner with more C sources built in: an app compiled to
// C (lang/cgen), which asuka-run -aot runs.
func RunnerWith(core string, extra []string, sanitize bool) (string, error) {
	return RunnerFor(core, RunnerOptions{Extra: extra, Sanitize: sanitize})
}

// RunnerOptions say what asuka-run is built with.
type RunnerOptions struct {
	Extra    []string // more of the core's C: an app compiled to C
	Foreign  *Foreign // the C an app's externs call
	Sanitize bool     // ASan and UBSan (tests)
}

// RunnerFor is Runner built as o says.
func RunnerFor(core string, o RunnerOptions) (string, error) {
	cc, err := Find()
	if err != nil {
		return "", err
	}
	srcs, err := Sources(core, true)
	if err != nil {
		return "", err
	}
	runner, _ := filepath.Glob(filepath.Join(core, "runner", "*.c"))
	srcs = append(srcs, runner...)
	srcs = append(srcs, o.Extra...)
	flags := append(Includes(core), "-g", "-O1")
	link := []string{"-lm"}
	var opt []string // what C of the app's gets of the core's flags too
	if o.Sanitize && runtime.GOOS != "windows" {
		opt = []string{"-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer"}
		flags = append(append(flags, opt...), "-DAK_TEST=1")
		link = append(link, "-fsanitize=address,undefined")
	} else {
		opt = []string{"-O2"}
		flags = append(flags, "-O2")
	}
	f := o.Foreign
	if f != nil {
		srcs = append(srcs, f.Glue)
		flags = append(flags, "-DAK_FFI=1")
		link = append(link, f.Link...)
	}
	h := sha256.New()
	fmt.Fprintln(h, cc.Path, cc.Args, flags, link)
	hdrs, _ := headers(core)
	for _, f := range append(srcs, hdrs...) {
		b, err := os.ReadFile(f)
		if err != nil {
			return "", err
		}
		fmt.Fprintln(h, f, len(b))
		h.Write(b)
	}
	if f != nil {
		if err := f.hash(h); err != nil {
			return "", err
		}
	}
	sum := hex.EncodeToString(h.Sum(nil))[:16]
	cache, err := os.UserCacheDir()
	if err != nil {
		cache = os.TempDir()
	}
	dir := filepath.Join(cache, "asuka", "runner-"+sum)
	exe := filepath.Join(dir, "asuka-run")
	if runtime.GOOS == "windows" {
		exe += ".exe"
	}
	if _, err := os.Stat(exe); err == nil {
		return exe, nil
	}
	objs, err := cc.Compile(srcs, filepath.Join(dir, "obj"), flags)
	if err != nil {
		return "", err
	}
	if f != nil {
		more, err := f.compileUnits(cc, filepath.Join(dir, "c"), append(append([]string{"-g"}, opt...), "-I"+filepath.Join(core, "include")))
		if err != nil {
			return "", err
		}
		objs = append(objs, more...)
	}
	tmp := fmt.Sprintf("%s.%d.tmp", exe, os.Getpid())
	if err := cc.Link(objs, tmp, link); err != nil {
		return "", err
	}
	return exe, os.Rename(tmp, exe)
}

func headers(core string) ([]string, error) {
	var out []string
	err := filepath.WalkDir(core, func(p string, e fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !e.IsDir() && strings.HasSuffix(p, ".h") {
			out = append(out, p)
		}
		return nil
	})
	sort.Strings(out)
	return out, err
}
