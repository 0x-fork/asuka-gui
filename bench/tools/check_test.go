// Package tools benchmarks the toolchain's Go side: asuka check on the
// repository's biggest packages (every platform, as asuka check does) and
// on the smallest app (what any check costs before its package's own
// code), for go test -bench with -cpuprofile and -memprofile.
//
//	go test ./bench/tools -bench . -benchmem
//	go test ./bench/tools -bench Check/std/base -cpuprofile cpu.out
package tools

import (
	"path/filepath"
	"runtime"
	"testing"

	"github.com/egoist/asuka/lang/compile"
)

func root() string {
	_, f, _, _ := runtime.Caller(0)
	return filepath.Join(filepath.Dir(f), "..", "..")
}

var packages = []string{"examples/counter", "std/base", "std/template", "examples/usage", "examples/gallery", "std/net"}

func BenchmarkCheck(b *testing.B) {
	for _, p := range packages {
		b.Run(p, func(b *testing.B) {
			dir := filepath.Join(root(), p)
			for i := 0; i < b.N; i++ {
				res, err := compile.DirPlatforms(dir, nil, nil)
				if err != nil {
					b.Fatal(err)
				}
				_ = res
			}
		})
	}
}

// BenchmarkEditorCheck is what the language server runs as a file
// changes: the package checked (no lowering) for the default platform.
func BenchmarkEditorCheck(b *testing.B) {
	for _, p := range packages {
		b.Run(p, func(b *testing.B) {
			dir := filepath.Join(root(), p)
			for i := 0; i < b.N; i++ {
				if _, err := compile.Dir(dir, &compile.Options{CheckOnly: true}); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}
