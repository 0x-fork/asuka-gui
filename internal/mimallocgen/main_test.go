package main

import (
	"path/filepath"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

// TestNamesRenamed compiles core/src/base/alloc.c, mimalloc in it, for
// this computer, as a release and as a development build: every name it
// defines is the core's (mimalloc_names.h is up to date), so an app's own
// mimalloc doesn't clash with it.
func TestNamesRenamed(t *testing.T) {
	cc, err := cbuild.Find()
	if err != nil {
		t.Skip(err)
	}
	core, _ := filepath.Abs("../../core")
	for _, cfg := range [][]string{{"-O2", "-DNDEBUG"}, {"-O1", "-DAK_DEBUG=1"}} {
		flags := append(append(append([]string{}, cbuild.Flags...), cbuild.Includes(core)...), cfg...)
		objs, err := cc.CompileWith([]string{filepath.Join(core, "src", "base", "alloc.c")}, t.TempDir(), flags)
		if err != nil {
			t.Fatal(err)
		}
		names, err := defined(objs[0])
		if err != nil {
			t.Fatal(err)
		}
		if len(names) < 5 {
			t.Fatalf("%v: only %v defined", cfg, names)
		}
		for _, n := range names {
			if !strings.HasPrefix(n, "ak_") {
				t.Errorf("%v: alloc.c defines %s: run go generate ./internal/mimallocgen", cfg, n)
			}
		}
	}
}
