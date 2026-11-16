package doc

import (
	"embed"
	"strings"
)

// The documentation of the standard library's packages the compiler
// provides (check.ProvidesStd: std/json, std/math, std/embed), which
// have no directory: their declarations as Asuka would say them, each
// with its ///, in builtin/<name>.asuka (TestBuiltinMatchesCompiler
// keeps them to what the compiler declares).
//
//go:embed builtin/*.asuka
var builtins embed.FS

// Builtin is the documentation of the compiler's package at path
// (std/json), and whether it has one.
func Builtin(path string) (*Package, bool) {
	name, ok := strings.CutPrefix(path, "std/")
	if !ok {
		return nil, false
	}
	src, err := builtins.ReadFile("builtin/" + name + ".asuka")
	if err != nil {
		return nil, false
	}
	p, err := FromSources(path, map[string][]byte{name + ".asuka": src})
	if err != nil {
		return nil, false
	}
	return p, true
}

// BuiltinPaths are the paths of the packages Builtin documents.
func BuiltinPaths() []string {
	es, _ := builtins.ReadDir("builtin")
	var out []string
	for _, e := range es {
		out = append(out, "std/"+strings.TrimSuffix(e.Name(), ".asuka"))
	}
	return out
}
