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
	"sync/atomic"
	"time"
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

// IsGCC reports whether the compiler is GCC (not clang, which defines
// __GNUC__ too), asking it once.
func (c Compiler) IsGCC() bool {
	key := c.Path + " " + strings.Join(c.Args, " ")
	gccMu.Lock()
	defer gccMu.Unlock()
	if v, ok := gccSeen[key]; ok {
		return v
	}
	args := append(append([]string{}, c.Args...), "-dM", "-E", "-x", "c", os.DevNull)
	out, err := exec.Command(c.Path, args...).Output()
	v := err == nil && strings.Contains(string(out), "__GNUC__") && !strings.Contains(string(out), "__clang__")
	gccSeen[key] = v
	return v
}

var (
	gccMu   sync.Mutex
	gccSeen = map[string]bool{}
)

// GCCFlags are what GCC is given beyond Flags: its format-truncation
// warning (-Wall's at -O2) guesses at the lengths of strings it can't
// know, and -Werror would make a guess an error.
var GCCFlags = []string{"-Wno-format-truncation"}

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
			tmp := tmpName(objs[i])
			args := append(append(append([]string{}, c.Args...), Flags...), extra...)
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
func Runner(core string, sanitize bool) (string, error) { return RunnerWith(core, nil, sanitize, "") }

// RunnerWith is Runner with more C sources built in (an app compiled to
// C, lang/cgen, which asuka-run -aot runs), linked into dir, a directory
// of the caller's that it removes: the core's objects come from the
// cache, compiled once per version of its sources, and the extra ones,
// which differ from run to run, are compiled into dir, so the cache
// doesn't grow with each. With no extra sources (dir ""), the runner
// itself is cached.
func RunnerWith(core string, extra []string, sanitize bool, dir string) (string, error) {
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
	flags := append(Includes(core), "-g", "-O1")
	link := []string{"-lm"}
	if runtime.GOOS == "linux" {
		link = append(link, "-lpthread", "-ldl")
	}
	if sanitize && runtime.GOOS != "windows" {
		flags = append(flags, "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer", "-DAK_TEST=1")
		link = append(link, "-fsanitize=address,undefined")
	} else {
		flags = append(flags, "-O2")
	}
	h := sha256.New()
	fmt.Fprintln(h, cc.Path, cc.Args, flags, link)
	hdrs, _ := headers(core)
	for _, f := range append(append([]string{}, srcs...), hdrs...) {
		b, err := os.ReadFile(f)
		if err != nil {
			return "", err
		}
		fmt.Fprintln(h, f, len(b))
		h.Write(b)
	}
	sum := hex.EncodeToString(h.Sum(nil))[:16]
	cached := cacheDir("runner-" + sum)
	name := "asuka-run"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	exe := filepath.Join(cached, name)
	if len(extra) == 0 {
		if _, err := os.Stat(exe); err == nil {
			return exe, nil
		}
	}
	objs, err := cc.Compile(srcs, filepath.Join(cached, "obj"), flags)
	if err != nil {
		return "", err
	}
	if len(extra) > 0 {
		if dir == "" {
			return "", fmt.Errorf("cbuild: a runner with extra sources needs a directory of the caller's")
		}
		more, err := cc.Compile(extra, filepath.Join(dir, "obj"), flags)
		if err != nil {
			return "", err
		}
		exe = filepath.Join(dir, name)
		return exe, cc.Link(append(objs, more...), exe, link)
	}
	tmp := tmpName(exe)
	if err := cc.Link(objs, tmp, link); err != nil {
		return "", err
	}
	return exe, os.Rename(tmp, exe)
}

// cacheDir is the build cache's directory name (one per version of what
// it builds: its sources and flags), marked as used now. A cache keeps a
// directory per version, so once per process the directories nothing has
// used for a week go (an old checkout's, a branch's).
func cacheDir(name string) string {
	cache, err := os.UserCacheDir()
	if err != nil {
		cache = os.TempDir()
	}
	root := filepath.Join(cache, "asuka")
	dir := filepath.Join(root, name)
	if err := os.MkdirAll(dir, 0o755); err == nil {
		now := time.Now()
		os.Chtimes(dir, now, now)
	}
	pruneOnce.Do(func() { go prune(root, time.Now().Add(-7*24*time.Hour)) })
	return dir
}

var pruneOnce sync.Once

// prune removes root's build directories (core-, app-, runner-) last
// used before cutoff.
func prune(root string, cutoff time.Time) {
	entries, err := os.ReadDir(root)
	if err != nil {
		return
	}
	for _, e := range entries {
		n := e.Name()
		if !e.IsDir() || !(strings.HasPrefix(n, "core-") || strings.HasPrefix(n, "app-") || strings.HasPrefix(n, "runner-")) {
			continue
		}
		if info, err := e.Info(); err == nil && info.ModTime().Before(cutoff) {
			os.RemoveAll(filepath.Join(root, n))
		}
	}
}

var tmpSeq atomic.Uint64

// tmpName is a name of its own beside path, to write then rename into
// place: builds running at once, in this process or others, share the
// cache safely.
func tmpName(path string) string {
	return fmt.Sprintf("%s.%d.%d.tmp", path, os.Getpid(), tmpSeq.Add(1))
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
