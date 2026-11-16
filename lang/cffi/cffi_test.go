package cffi_test

import (
	"bytes"
	"flag"
	"os"
	"strings"
	"testing"

	"github.com/egoist/asuka/lang/compile"
)

var update = flag.Bool("update", false, "rewrite testdata/*.golden")

// TestGlue compares the glue of testdata/sqlite.asuka with its golden
// file (go test -update rewrites it). lang/compile's conformance suite
// (testdata/run/ffi.asuka) compiles and runs glue like it.
func TestGlue(t *testing.T) {
	src, err := os.ReadFile("testdata/sqlite.asuka")
	if err != nil {
		t.Fatal(err)
	}
	res := compile.Files(map[string][]byte{"sqlite.asuka": src}, &compile.Options{MainTest: true})
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if res.Module.C == nil {
		t.Fatal("no C")
	}
	got := res.Module.C.Glue
	if *update {
		if err := os.WriteFile("testdata/sqlite.golden", got, 0o644); err != nil {
			t.Fatal(err)
		}
		return
	}
	want, err := os.ReadFile("testdata/sqlite.golden")
	if err != nil {
		t.Fatalf("%v (go test -update writes it)", err)
	}
	if !bytes.Equal(got, want) {
		t.Errorf("the glue differs from testdata/sqlite.golden (go test -update):\n%s", got)
	}
	// What the bytecode calls is what the glue registers.
	for _, b := range []string{"ffi.main.db_open", "ffi.main.db_info", "ffi.main.db_blob"} {
		if !strings.Contains(string(got), `"`+b+`"`) {
			t.Errorf("the glue doesn't register %s", b)
		}
		found := false
		for _, n := range res.Module.Builtins {
			found = found || n == b
		}
		if !found {
			t.Errorf("the module doesn't call %s", b)
		}
	}
}

// TestNoExterns: a program without externs has no C.
func TestNoExterns(t *testing.T) {
	res := compile.Files(map[string][]byte{"a.asuka": []byte("fn f() -> int = 1\n")}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if res.Module.C != nil {
		t.Fatalf("C: %s", res.Module.C.Glue)
	}
}
