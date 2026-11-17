package cbuild

import (
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/core"
	"github.com/egoist/asuka/internal/srcdir"
)

var (
	coreOnce     sync.Once
	coreDir      string
	coreEmbedded bool
	coreErr      error
)

// CoreDir is the directory of the C core's sources to build from:
// ASUKA_CORE if set; the repository's core/ when the CLI runs from a
// checkout (go run, go test); else the sources embedded in the CLI,
// written once into the user's cache under their hash.
func CoreDir() (string, error) {
	coreOnce.Do(func() { coreDir, coreEmbedded, coreErr = coreTree.Find() })
	if coreErr == nil && coreEmbedded && !isCore(coreDir) {
		// The cache evicted it (a long asuka dev): written again.
		return coreTree.Extract()
	}
	return coreDir, coreErr
}

// CoreEmbedded reports whether CoreDir is the copy embedded in the CLI
// (an installed CLI's), which its prebuilt libraries were built from.
func CoreEmbedded() bool {
	CoreDir()
	return coreEmbedded
}

// LinkSource is core/link/link.c in core: what each build links in beside
// the core, compiled with the build's defines (AK_APP_COMPILED, AK_FFI).
func LinkSource(core string) string { return filepath.Join(core, "link", "link.c") }

var coreTree = srcdir.Tree{Env: "ASUKA_CORE", Sub: "core", Valid: isCore, Files: core.Sources, Cache: "core-src"}

// extract writes the embedded core into the cache.
func extract() (string, error) { return coreTree.Extract() }

func isCore(d string) bool {
	_, err1 := os.Stat(filepath.Join(d, "include", "asuka.h"))
	_, err2 := os.Stat(filepath.Join(d, "src", "vm", "vm.h"))
	return err1 == nil && err2 == nil
}
