// Package std is Asuka's standard library: the packages apps import as
// std/<name> (std/http, std/store), each a directory here with its
// .asuka files and, for a native package, its platforms' code. The CLI
// carries them, so an installed CLI needs no checkout. It resolves the
// platform SDK's packages too (platform/<name>, package sdk's
// directories): an import of either is a package the CLI carries.
package std

import (
	"embed"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"github.com/egoist/asuka/internal/srcdir"
	"github.com/egoist/asuka/sdk"
)

// Files are the packages, embedded.
//
//go:embed *
var Files embed.FS

// The import paths' prefixes: the standard library's (std/json) and the
// platform SDK's (platform/camera). A package of an app's or a
// registry's can't be named under either.
const (
	Prefix         = "std/"
	PlatformPrefix = "platform/"
)

var (
	once     sync.Once
	dir      string
	embedded bool
	err      error
)

// Dir is the standard library's directory: ASUKA_STD if set; the
// repository's std/ when the CLI runs from a checkout; else the copy
// embedded in the CLI, written once into the user's cache.
func Dir() (string, error) {
	once.Do(func() {
		dir, embedded, err = srcdir.Tree{Env: "ASUKA_STD", Sub: "std", Valid: isStd, Files: Files, Cache: "std"}.Find()
	})
	return dir, err
}

func isStd(d string) bool {
	_, err := os.Stat(filepath.Join(d, "std.go"))
	return err == nil
}

// Reserved reports whether path is in the standard library's or the
// platform SDK's namespace (std/..., platform/...), which only the
// packages the CLI carries have.
func Reserved(path string) bool {
	return strings.HasPrefix(path, Prefix) || strings.HasPrefix(path, PlatformPrefix)
}

// Package is the directory of the package imported as path (std/http,
// platform/camera), and whether the standard library or the platform SDK
// has it.
func Package(path string) (string, bool) {
	var d string
	var err error
	name, ok := strings.CutPrefix(path, Prefix)
	if ok {
		d, err = Dir()
	} else if name, ok = strings.CutPrefix(path, PlatformPrefix); ok {
		d, err = sdk.Dir()
	}
	if !ok || err != nil || name == "" || strings.ContainsAny(name, `/\.`) {
		return "", false
	}
	p := filepath.Join(d, name)
	if _, err := os.Stat(filepath.Join(p, "asuka.toml")); err != nil {
		return "", false
	}
	return p, true
}

// Packages are the standard library's and the platform SDK's packages:
// import path → directory.
func Packages() (map[string]string, error) {
	out := map[string]string{}
	for _, t := range []struct {
		prefix string
		dir    func() (string, error)
	}{{Prefix, Dir}, {PlatformPrefix, sdk.Dir}} {
		d, err := t.dir()
		if err != nil {
			return nil, err
		}
		ms, _ := filepath.Glob(filepath.Join(d, "*", "asuka.toml"))
		for _, m := range ms {
			p := filepath.Dir(m)
			out[t.prefix+filepath.Base(p)] = p
		}
	}
	return out, nil
}

// PackageDirs are Packages' directories, sorted.
func PackageDirs() ([]string, error) {
	ps, err := Packages()
	if err != nil {
		return nil, err
	}
	var out []string
	for _, d := range ps {
		out = append(out, d)
	}
	sort.Strings(out)
	return out, nil
}

// Glob is filepath.Glob of pattern in each of the standard library's and
// the platform SDK's packages (desktop/*_test.c), sorted.
func Glob(pattern string) ([]string, error) {
	ds, err := PackageDirs()
	if err != nil {
		return nil, err
	}
	var out []string
	for _, d := range ds {
		ms, err := filepath.Glob(filepath.Join(d, pattern))
		if err != nil {
			return nil, err
		}
		out = append(out, ms...)
	}
	sort.Strings(out)
	return out, nil
}

// Embedded reports whether Dir is the copy embedded in the CLI (an
// installed CLI's), which its prebuilt libraries were built from.
func Embedded() bool {
	Dir()
	return embedded
}
