package appbuild

import (
	"bytes"
	"os"
	"path/filepath"
	"runtime"
)

// Checkout is the Asuka repository this CLI runs from (go run, go test,
// go install from a checkout), "" for an installed release.
func Checkout() string {
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		return ""
	}
	root := filepath.Dir(filepath.Dir(filepath.Dir(file)))
	if b, err := os.ReadFile(filepath.Join(root, "go.mod")); err == nil && bytes.Contains(b, []byte("module github.com/egoist/asuka\n")) {
		if _, err := os.Stat(filepath.Join(root, "core", "include", "asuka.h")); err == nil {
			return root
		}
	}
	return ""
}
