package appbuild

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/lang/bytecode"
)

// Uses are the compile's: a project compiles once for both until its
// files change, and a compile that doesn't say (a test's) has the
// program checked alone.
func TestUsesFromCompile(t *testing.T) {
	dir := t.TempDir()
	write := func(s string) {
		if err := os.WriteFile(filepath.Join(dir, "app.asuka"), []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("import asuka/location\n\nasync fn here() {\n  let _ = try? await location.current()\n}\n\napp { Text(\"a\") }\n")
	old := Compile
	defer func() { Compile = old }()
	compiles := 0
	Compile = func(dir string, deps map[string]string, main bool) (*bytecode.Module, *Uses, []string, error) {
		compiles++
		return old(dir, deps, main)
	}
	p := &Project{Dir: dir}
	if u := p.Uses(); !u.Ref("asuka/location", "current") || compiles != 1 {
		t.Fatalf("uses %v, %d compiles", u.Refs, compiles)
	}
	if _, diags, err := p.Compile(); err != nil || len(diags) > 0 || compiles != 1 {
		t.Fatalf("compiled again: %d (%v %v)", compiles, diags, err)
	}
	// A save (of another size: the stamp has it): compiled again, its
	// uses the new ones.
	write("import asuka/location\n\nasync fn always() {\n  let _ = try? await location.requestAlways()\n}\n\napp { Text(\"a\") }\n")
	p.Compile()
	if u := p.Uses(); !u.Ref("asuka/location", "requestAlways") || u.Ref("asuka/location", "current") || compiles != 2 {
		t.Fatalf("after a save: %v, %d compiles", u.Refs, compiles)
	}
	// A test's compile, which doesn't say: checked.
	Compile = func(dir string, deps map[string]string, main bool) (*bytecode.Module, *Uses, []string, error) {
		return nil, nil, nil, nil
	}
	q := &Project{Dir: dir}
	if u := q.Uses(); !u.Ref("asuka/location", "requestAlways") {
		t.Fatalf("checked: %v", u.Refs)
	}
}
