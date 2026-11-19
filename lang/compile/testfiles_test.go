package compile

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// Test files (name_test.asuka) and test blocks are a test build's, and
// only its root package's: a build has none of them, nor what only they
// import, and an imported package's never come with it.
func TestTestFilesOnlyInTestBuilds(t *testing.T) {
	root := t.TempDir()
	write := func(p, s string) {
		p = filepath.Join(root, filepath.FromSlash(p))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	// A library whose test file imports another package and declares a
	// constant: neither is part of what imports it.
	write("lib/lib.asuka", "pub fn twice(n: int) -> int = n * 2\n")
	write("lib/lib_test.asuka", `import ../helper
let libFixture = "LIB_TEST_FIXTURE"
test "twice" { expect(twice(helper.three()) == 6 && libFixture != "") }
`)
	write("helper/helper.asuka", `pub let helperConst = "HELPER_ONLY_TESTS_IMPORT"
pub fn three() -> int = 3
`)
	write("app/app.asuka", `import ../lib
fn main() { print(lib.twice(2)) }
test "in a regular file" { expect(appFixture == "APP_TEST_FIXTURE") }
`)
	write("app/app_test.asuka", `let appFixture = "APP_TEST_FIXTURE"
test "fixture" { expect(appFixture.count() > 0) }
`)
	// A test file for one platform is that platform's.
	write("app/only_linux_test.asuka", `let linuxFixture = "LINUX_FIXTURE"
test "linux" { expect(linuxFixture != "") }
`)
	has := func(res *Result, s string) bool {
		return slices.ContainsFunc(res.Module.Strings, func(x string) bool { return strings.Contains(x, s) })
	}
	pkgs := func(res *Result) []string {
		var out []string
		for _, p := range res.Program.Packages {
			out = append(out, p.Path)
		}
		return out
	}

	build, err := Dir(filepath.Join(root, "app"), &Options{NoTests: true, Platform: "ios"})
	if err != nil {
		t.Fatal(err)
	}
	if err := build.Err(); err != nil {
		t.Fatal(err)
	}
	if len(build.Module.Tests) != 0 {
		t.Errorf("a build has %d tests", len(build.Module.Tests))
	}
	for _, s := range []string{"APP_TEST_FIXTURE", "LIB_TEST_FIXTURE", "HELPER_ONLY_TESTS_IMPORT", "LINUX_FIXTURE"} {
		if has(build, s) {
			t.Errorf("a build has %q", s)
		}
	}
	for _, p := range pkgs(build) {
		if strings.Contains(p, "helper") {
			t.Errorf("a build imports %s, which only a test file of a package it imports does", p)
		}
	}

	// The test build: the root's test files and blocks, not lib's.
	tests, err := Dir(filepath.Join(root, "app"), &Options{Platform: "ios"})
	if err != nil {
		t.Fatal(err)
	}
	if err := tests.Err(); err != nil {
		t.Fatal(err)
	}
	if len(tests.Module.Tests) != 2 || !has(tests, "APP_TEST_FIXTURE") || has(tests, "LIB_TEST_FIXTURE") || has(tests, "LINUX_FIXTURE") {
		t.Errorf("a test build for iOS: %d tests, strings %q", len(tests.Module.Tests), tests.Module.Strings)
	}
	linux, err := Dir(filepath.Join(root, "app"), &Options{Platform: "linux"})
	if err != nil {
		t.Fatal(err)
	}
	if err := linux.Err(); err != nil {
		t.Fatal(err)
	}
	if len(linux.Module.Tests) != 3 || !has(linux, "LINUX_FIXTURE") {
		t.Errorf("a test build for Linux: %d tests", len(linux.Module.Tests))
	}

	// The library's own test build has its test file and what it imports.
	lib, err := Dir(filepath.Join(root, "lib"), &Options{Platform: "ios"})
	if err != nil {
		t.Fatal(err)
	}
	if err := lib.Err(); err != nil {
		t.Fatal(err)
	}
	if len(lib.Module.Tests) != 1 || !has(lib, "LIB_TEST_FIXTURE") {
		t.Errorf("lib's test build: %d tests", len(lib.Module.Tests))
	}
}
