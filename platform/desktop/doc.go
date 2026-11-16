// Package desktop holds the C the Windows and Linux hosts share
// (RFC 0008): include/asuka_desktop.h, the contract between a host and
// the C its app links; src/native.c, the native packages' registry. Its
// tests build them with a test host (test/host.c) and run Asuka on the
// core, with no Go at run time.
package desktop

import (
	"embed"
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/internal/srcdir"
)

// Sources are the shared C, embedded in the asuka CLI so an installed CLI
// builds desktop apps without the repository.
//
//go:embed include src
var Sources embed.FS

var (
	once sync.Once
	dir  string
	err  error
)

// Dir is the directory of the shared C: ASUKA_DESKTOP if set; the
// repository's platform/desktop when the CLI runs from a checkout; else
// the embedded copy, written once into the user's cache.
func Dir() (string, error) {
	once.Do(func() {
		dir, err = srcdir.Tree{Env: "ASUKA_DESKTOP", Sub: filepath.Join("platform", "desktop"), Valid: valid, Files: Sources, Cache: "desktop-src"}.Dir()
	})
	return dir, err
}

func valid(d string) bool {
	_, err := os.Stat(filepath.Join(d, "include", "asuka_desktop.h"))
	return err == nil
}

// Registry is the registry's C source in dir (Dir's).
func Registry(dir string) string { return filepath.Join(dir, "src", "native.c") }

// Include is the include directory of asuka_desktop.h in dir.
func Include(dir string) string { return filepath.Join(dir, "include") }
