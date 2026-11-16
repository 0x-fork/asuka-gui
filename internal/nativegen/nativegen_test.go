package nativegen

import (
	"flag"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

var update = flag.Bool("update", false, "write the pantry package's generated files")

// Names as each language writes them.
func TestNames(t *testing.T) {
	for in, want := range map[string]string{"Level": "level", "ID": "id", "URLString": "urlString", "HTTPServer": "httpServer", "X": "x", "isOn": "isOn"} {
		if got := lowerCamel(in); got != want {
			t.Errorf("lowerCamel(%s) = %s, want %s", in, got, want)
		}
	}
	if got := KotlinPackage("github.com/acme/go-battery/native"); got != "com.github.acme.go_battery.native_" {
		t.Errorf("KotlinPackage: %s", got)
	}
	if got := SwiftClass("example.com/a-b/c"); got != "AsukaModule_example_com_a_b_c" {
		t.Errorf("SwiftClass: %s", got)
	}
	if got := upperSnake("LowPower"); got != "LOW_POWER" {
		t.Errorf("upperSnake: %s", got)
	}
}

// A generated file checked out with CRLF line ends (git's core.autocrlf,
// on Windows) is the same file: Write leaves it.
func TestSameText(t *testing.T) {
	gen := []byte("// Code generated\npackage x\n\nfunc F() {}\n")
	if !sameText([]byte(strings.ReplaceAll(string(gen), "\n", "\r\n")), gen) || !sameText(gen, gen) {
		t.Error("a CRLF checkout of the file differs")
	}
	if sameText([]byte("package y\r\n"), []byte("package x\n")) {
		t.Error("another file is the same")
	}
}

// The pantry package (testdata/pantry) declares every kind of value, a
// view, errors, an async method and objects: its generated Swift, Kotlin,
// JavaScript and C are what's committed (-update writes them), beside
// its implementations (cmd/asuka's Mac build and the GUI tests' plugin
// app use them).
func TestPantry(t *testing.T) {
	p, err := LoadAsuka("testdata/pantry")
	if err != nil {
		t.Fatal(err)
	}
	if p == nil {
		t.Fatal("no native declarations")
	}
	if p.Path != "@asuka/pantry" || len(p.Interfaces) != 2 || len(p.Views) != 1 || len(p.Errors) != 2 || len(p.Structs) != 1 || len(p.Enums) != 1 {
		t.Fatalf("model: path %s, %d interfaces, %d views, %d errors, %d structs, %d enums", p.Path, len(p.Interfaces), len(p.Views), len(p.Errors), len(p.Structs), len(p.Enums))
	}
	cook := p.Interfaces[0].Methods[3]
	if cook.Name != "Cook" || !cook.Context || cook.Params[1].Type.Kind != Func {
		t.Fatalf("cook: %+v", cook)
	}
	if !p.Interfaces[1].MainThread || !p.Interfaces[1].Methods[1].Close {
		t.Fatalf("shelf: %+v", p.Interfaces[1])
	}
	if *update {
		if _, err := Write(p); err != nil {
			t.Fatal(err)
		}
	}
	files, err := Files(p)
	if err != nil {
		t.Fatal(err)
	}
	if len(files) != 6 {
		t.Errorf("%d files, want Swift's, Kotlin's, JavaScript's (its module and its declarations') and the desktop's C (a header and a source)", len(files))
	}
	for rel, b := range files {
		old, _ := os.ReadFile(filepath.Join(p.Dir, rel))
		if string(old) != string(b) {
			t.Errorf("stale (go test ./internal/nativegen -update): %s", rel)
		}
	}
	// The desktop's C compiles for Windows and Linux (Zig cross-compiles;
	// platform/desktop's test runs it).
	if got := CSources("testdata/pantry/desktop", "linux"); strings.Join(got, " ") != "pantry.asuka.c pantry.c" {
		t.Errorf("the C for Linux: %v", got)
	}
	zig, err := exec.LookPath("zig")
	if err != nil {
		t.Log("no zig: the C isn't cross-compiled")
		return
	}
	out := t.TempDir()
	for _, target := range []string{"x86_64-windows-gnu", "x86_64-linux-gnu", "aarch64-linux-gnu"} {
		for _, f := range []string{"pantry.asuka.c", "pantry.c"} {
			c := exec.Command(zig, "cc", "-target", target, "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-c",
				"-o", filepath.Join(out, target+"-"+f+".o"),
				"-I", "../../platform/desktop/include", "-I", "../../core/include", "testdata/pantry/desktop/"+f)
			if out, err := c.CombinedOutput(); err != nil {
				t.Errorf("%s %s: %v\n%s", target, f, err, out)
			}
		}
	}
}

// What can't cross says where and why.
func TestAsukaLoadErrors(t *testing.T) {
	for _, c := range []struct{ src, want string }{
		{"native interface X { fn m(c: [X]) }", "isn't in an array"},
		{"native interface X { fn m(m: [int: string]) }", "keys are strings"},
		{"native interface X { fn m() -> fn() }", "as a parameter only"},
		{"native interface X { fn m(t: http.Request) }", "own types"},
		{"enum E { a(int) }\nnative interface X { fn m(e: E) }", "without payloads"},
		{"native interface X { fn m(u: Unknown) }", "Unknown can't cross"},
		{"native interface X { fn m(f: fn(Ref)) }", "a Ref crosses as a parameter only"},
		{"native interface X { fn m() -> Ref }", "a Ref crosses as a parameter only"},
		{"struct S { v: Ref }\nnative interface X { fn m(s: S) }", "a Ref crosses as a parameter only"},
	} {
		dir := t.TempDir()
		os.WriteFile(filepath.Join(dir, "x.asuka"), []byte(c.src), 0o644)
		_, err := LoadAsuka(dir)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s: %v, want %q", c.src, err, c.want)
		}
	}
}

// docs/native.md's C example (and its translations') compiles for Windows
// and Linux against its Battery's generated header.
func TestDocsCExample(t *testing.T) {
	zig, err := exec.LookPath("zig")
	if err != nil {
		t.Skip("no zig")
	}
	for _, page := range []string{"../../docs/native.md", "../../docs/zh/native.md", "../../docs/ja/native.md"} {
		b, err := os.ReadFile(page)
		if err != nil {
			t.Fatal(err)
		}
		block := func(lang, contains string) string {
			for _, part := range strings.Split(string(b), "```"+lang+"\n")[1:] {
				code, _, _ := strings.Cut(part, "```")
				if strings.Contains(code, contains) {
					return code
				}
			}
			t.Fatalf("%s: no %s block with %q", page, lang, contains)
			return ""
		}
		dir := filepath.Join(t.TempDir(), "battery")
		os.MkdirAll(filepath.Join(dir, "desktop"), 0o755)
		os.WriteFile(filepath.Join(dir, "asuka.toml"), []byte("name = \"battery\"\nversion = \"0.1.0\"\n"), 0o644)
		os.WriteFile(filepath.Join(dir, "battery.asuka"), []byte(block("asuka", "native interface Battery")), 0o644)
		os.WriteFile(filepath.Join(dir, "desktop", "battery.c"), []byte(block("c", "battery_make_Battery")), 0o644)
		p, err := LoadAsuka(dir)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := Write(p); err != nil {
			t.Fatal(err)
		}
		for _, target := range []string{"x86_64-windows-gnu", "x86_64-linux-gnu"} {
			for _, f := range []string{"battery.c", "battery.asuka.c"} {
				args := []string{"cc", "-target", target, "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-c",
					"-o", filepath.Join(dir, target+f+".o"), "-I", "../../platform/desktop/include", "-I", "../../core/include", filepath.Join(dir, "desktop", f)}
				if strings.Contains(target, "linux") {
					args = append(args, "-D_GNU_SOURCE")
				}
				if out, err := exec.Command(zig, args...).CombinedOutput(); err != nil {
					t.Errorf("%s %s %s: %v\n%s", page, target, f, err, out)
				}
			}
		}
	}
}

// The generated module exports the declarations' module before it
// imports the package's own JavaScript, so that JavaScript's imports of
// an enum or an error are made before it runs; the declarations' module
// imports nothing.
func TestJSDeclarationsFirst(t *testing.T) {
	p, err := LoadAsuka("testdata/pantry")
	if err != nil {
		t.Fatal(err)
	}
	js := string(GenerateJS(p))
	exp := strings.Index(js, `export * from "./pantry.decl.asuka.js";`)
	imp := strings.Index(js, `import * as impl from "./pantry.js";`)
	if exp < 0 || imp < 0 || exp > imp {
		t.Fatalf("the declarations' export (at %d) isn't before the package's import (at %d):\n%s", exp, imp, js)
	}
	decl := string(GenerateJSDecl(p))
	if strings.Contains(decl, "import ") {
		t.Errorf("the declarations' module imports:\n%s", decl)
	}
	for _, n := range JSExports(p) {
		if !strings.Contains(decl, "export const "+n+" ") && !strings.Contains(decl, "export class "+n+" ") {
			t.Errorf("the declarations' module doesn't export %s", n)
		}
		if strings.Contains(js, "export const "+n+" ") || strings.Contains(js, "export class "+n+" ") {
			t.Errorf("the module makes %s itself", n)
		}
	}
}

// A package's JavaScript that imports what the generated module doesn't
// export (an error class of another spelling) fails generation, with
// what it exports.
func TestJSImportsChecked(t *testing.T) {
	p, err := LoadAsuka("testdata/pantry")
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, "web"), 0o755); err != nil {
		t.Fatal(err)
	}
	p.Dir = dir
	have := JSExports(p)
	if len(have) == 0 {
		t.Fatal("pantry declares no enums or errors")
	}
	ok := "import { " + have[0] + " as First } from \"./pantry.asuka.js\";\nexport function makeShelf() {}\n"
	os.WriteFile(filepath.Join(dir, "web", "pantry.js"), []byte(ok), 0o644)
	if err := CheckJSImports(p); err != nil {
		t.Fatalf("a good import: %v", err)
	}
	bad := "import {\n  " + have[0] + ",\n  PantrysError,\n} from './pantry.asuka.js';\n"
	os.WriteFile(filepath.Join(dir, "web", "pantry.js"), []byte(bad), 0o644)
	err = CheckJSImports(p)
	if err == nil || !strings.Contains(err.Error(), "web/pantry.js imports PantrysError from pantry.asuka.js") || !strings.Contains(err.Error(), have[0]) {
		t.Fatalf("a bad import: %v", err)
	}
	if _, err := Files(p); err == nil {
		t.Fatal("Files took a bad import")
	}
}
