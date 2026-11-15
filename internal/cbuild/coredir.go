package cbuild

import (
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/core"
	"github.com/egoist/asuka/internal/srcdir"
)

var (
	coreOnce sync.Once
	coreDir  string
	coreErr  error
)

// CoreDir is the directory of the C core's sources to build from:
// ASUKA_CORE if set; the repository's core/ when the CLI runs from a
// checkout (go run, go test); else the sources embedded in the CLI,
// written once into the user's cache under their hash.
func CoreDir() (string, error) {
	coreOnce.Do(func() { coreDir, coreErr = findCore() })
	return coreDir, coreErr
}

var coreTree = srcdir.Tree{Env: "ASUKA_CORE", Sub: "core", Valid: isCore, Files: core.Sources, Cache: "core-src"}

func findCore() (string, error) { return coreTree.Dir() }

// extract writes the embedded core into the cache.
func extract() (string, error) { return coreTree.Extract() }

func isCore(d string) bool {
	_, err1 := os.Stat(filepath.Join(d, "include", "asuka.h"))
	_, err2 := os.Stat(filepath.Join(d, "src", "vm", "vm.h"))
	return err1 == nil && err2 == nil
}
