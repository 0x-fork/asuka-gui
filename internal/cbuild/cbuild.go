// Package cbuild builds Asuka's C core (core/) with a C compiler run as a
// program: Zig's (zig cc), which cross-compiles to every target, or the
// system's. Nothing here uses cgo.
package cbuild

import (
	"bytes"
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

// Find returns the compiler to use: $CC if set, else zig cc, else cc.
func Find() (Compiler, error) {
	if cc := os.Getenv("CC"); cc != "" {
		f := strings.Fields(cc)
		return Compiler{Path: f[0], Args: f[1:]}, nil
	}
	if p, err := exec.LookPath("zig"); err == nil {
		return Compiler{Path: p, Args: []string{"cc"}}, nil
	}
	for _, name := range []string{"cc", "clang", "gcc"} {
		if p, err := exec.LookPath(name); err == nil {
			return Compiler{Path: p}, nil
		}
	}
	return Compiler{}, fmt.Errorf("cbuild: no C compiler: install Zig (https://ziglang.org) or set CC")
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
// returns the objects. extra flags come after Flags.
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
			args := append(append(append([]string{}, c.Args...), Flags...), extra...)
			args = append(args, "-c", src, "-o", objs[i])
			errs[i] = c.run(args)
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
