package appbuild

import (
	"os"
	"path/filepath"
	"slices"
	"testing"

	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/target"
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
	write("import platform/location\n\nasync fn here() {\n  let _ = try? await location.current()\n}\n\napp { Text(\"a\") }\n")
	old := Compile
	defer func() { Compile = old }()
	compiles := 0
	Compile = func(dir string, deps map[string]string, platform target.Platform, tests bool) (*bytecode.Module, *Uses, []check.Diagnostic, error) {
		compiles++
		return old(dir, deps, platform, tests)
	}
	p := &Project{Dir: dir}
	if u := p.Uses(""); !u.Ref("platform/location", "current") || compiles != 1 {
		t.Fatalf("uses %v, %d compiles", u.Refs, compiles)
	}
	if _, diags, err := p.Compile(""); err != nil || len(diags) > 0 || compiles != 1 {
		t.Fatalf("compiled again: %d (%v %v)", compiles, diags, err)
	}
	// A save (of another size: the stamp has it): compiled again, its
	// uses the new ones.
	write("import platform/location\n\nasync fn always() {\n  let _ = try? await location.requestAlways()\n}\n\napp { Text(\"a\") }\n")
	p.Compile("")
	if u := p.Uses(""); !u.Ref("platform/location", "requestAlways") || u.Ref("platform/location", "current") || compiles != 2 {
		t.Fatalf("after a save: %v, %d compiles", u.Refs, compiles)
	}
	// A test's compile, which doesn't say: checked.
	Compile = func(string, map[string]string, target.Platform, bool) (*bytecode.Module, *Uses, []check.Diagnostic, error) {
		return nil, nil, nil, nil
	}
	q := &Project{Dir: dir}
	if u := q.Uses(""); !u.Ref("platform/location", "requestAlways") {
		t.Fatalf("checked: %v", u.Refs)
	}
}

// A build has none of the package's test files and test blocks, nor what
// only they import (its uses: the native code an app links); its test
// build has them.
func TestBuildWithoutTests(t *testing.T) {
	dir := t.TempDir()
	files := map[string]string{
		"app.asuka":      "app { Text(\"a\") }\n\ntest \"shown\" { expect(fixture == \"FIXTURE\") }\n",
		"app_test.asuka": "import platform/location\n\nlet fixture = \"FIXTURE\"\n\ntest \"here\" { let _ = try? await location.current() }\n",
	}
	for n, s := range files {
		if err := os.WriteFile(filepath.Join(dir, n), []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	p := &Project{Dir: dir}
	m, diags, err := p.Compile("")
	if err != nil || len(diags) > 0 {
		t.Fatal(err, diags)
	}
	if len(m.Tests) != 0 || slices.Contains(m.Strings, "FIXTURE") {
		t.Errorf("a build has tests: %d, %q", len(m.Tests), m.Strings)
	}
	if u := p.Uses(""); u.Imports["platform/location"] {
		t.Errorf("a build uses what only a test file imports: %v", u.Imports)
	}
	m, diags, err = p.CompileTests("")
	if err != nil || len(diags) > 0 {
		t.Fatal(err, diags)
	}
	if len(m.Tests) != 2 || !slices.Contains(m.Strings, "FIXTURE") {
		t.Errorf("a test build: %d tests, %q", len(m.Tests), m.Strings)
	}
}
