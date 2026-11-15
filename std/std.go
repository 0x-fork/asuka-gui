// Package std is Asuka's standard library: the packages apps import as
// asuka/<name> (asuka/http, asuka/store), each a directory here with its
// .asuka files and, for a native package, its platforms' code. The CLI
// carries them, so an installed CLI needs no checkout.
package std

import (
	"embed"
	"os"
	"path/filepath"
	"strings"
	"sync"

	"github.com/egoist/asuka/internal/srcdir"
)

// Files are the packages, embedded.
//
//go:embed *
var Files embed.FS

var (
	once sync.Once
	dir  string
	err  error
)

// Dir is the standard library's directory: ASUKA_STD if set; the
// repository's std/ when the CLI runs from a checkout; else the copy
// embedded in the CLI, written once into the user's cache.
func Dir() (string, error) {
	once.Do(func() {
		dir, err = srcdir.Tree{Env: "ASUKA_STD", Sub: "std", Valid: isStd, Files: Files, Cache: "std"}.Dir()
	})
	return dir, err
}

func isStd(d string) bool {
	_, err := os.Stat(filepath.Join(d, "std.go"))
	return err == nil
}

// Package is the directory of the package imported as path
// (asuka/http), and whether the standard library has it.
func Package(path string) (string, bool) {
	name, ok := strings.CutPrefix(path, "asuka/")
	if !ok || name == "" || strings.ContainsAny(name, `/\.`) {
		return "", false
	}
	d, err := Dir()
	if err != nil {
		return "", false
	}
	p := filepath.Join(d, name)
	if _, err := os.Stat(filepath.Join(p, "asuka.toml")); err != nil {
		return "", false
	}
	return p, true
}
