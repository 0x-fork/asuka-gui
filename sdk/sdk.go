// Package sdk is Asuka's platform SDK: the packages apps import as
// platform/<name> (platform/camera, platform/biometric), each a directory
// here with its .asuka files and, for a native package, its platforms'
// code: what wraps a device, an operating system's service or a store.
// The language's own library, the same everywhere, is package std's
// (std/<name>), which resolves both. The CLI carries them, so an
// installed CLI needs no checkout.
package sdk

import (
	"embed"
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/internal/srcdir"
)

// Files are the packages, embedded.
//
//go:embed *
var Files embed.FS

var (
	once     sync.Once
	dir      string
	embedded bool
	err      error
)

// Dir is the platform SDK's directory: ASUKA_SDK if set; the
// repository's sdk/ when the CLI runs from a checkout; else the copy
// embedded in the CLI, written once into the user's cache.
func Dir() (string, error) {
	once.Do(func() {
		dir, embedded, err = srcdir.Tree{Env: "ASUKA_SDK", Sub: "sdk", Valid: isSDK, Files: Files, Cache: "sdk"}.Find()
	})
	return dir, err
}

func isSDK(d string) bool {
	_, err := os.Stat(filepath.Join(d, "sdk.go"))
	return err == nil
}

// Embedded reports whether Dir is the copy embedded in the CLI (an
// installed CLI's), which its prebuilt libraries were built from.
func Embedded() bool {
	Dir()
	return embedded
}
