package nativegen

import (
	"flag"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

var update = flag.Bool("update", false, "write the kitchen package's generated files")

// The kitchen package (testdata/kitchen) declares every kind of value,
// a view, errors, an async method and objects: its generated files, for
// Go and each project it has (Swift, Android, web), are what's
// committed (-update writes them), and its Go compiles on each platform.
func TestKitchen(t *testing.T) {
	p, err := Load("testdata/kitchen")
	if err != nil {
		t.Fatal(err)
	}
	if *update {
		if _, err := Write(p); err != nil {
			t.Fatal(err)
		}
	}
	stale, err := Stale(p)
	if err != nil {
		t.Fatal(err)
	}
	files, _ := Files(p)
	for rel, b := range files {
		old, _ := os.ReadFile(filepath.Join(p.Dir, rel))
		if string(old) != string(b) {
			stale = append(stale, rel)
		}
	}
	if len(stale) > 0 {
		t.Fatalf("stale (go test ./internal/nativegen -update): %v", stale)
	}
	if len(files) != 4 {
		t.Errorf("%d files, want Go's, Swift's, Kotlin's and JavaScript's", len(files))
	}
	for _, goos := range []string{"darwin", "windows", "linux", "js"} {
		c := exec.Command("go", "vet", "./testdata/kitchen")
		c.Env = append(os.Environ(), "GOOS="+goos, "CGO_ENABLED=0")
		if goos == "js" {
			c.Env = append(c.Env, "GOARCH=wasm")
		}
		if out, err := c.CombinedOutput(); err != nil {
			t.Errorf("%s: %v\n%s", goos, err, out)
		}
	}
}

// What can't cross says where it is and why.
func TestLoadErrors(t *testing.T) {
	for _, c := range []struct{ src, want string }{
		{"//asuka:native\ntype X struct{}", "is for an interface"},
		{"//asuka:native\ntype x interface{ M() }", "is exported"},
		{"//asuka:native\ntype X interface{ M(c chan int) }", "can't cross"},
		{"//asuka:native\ntype X interface{ M(m map[int]string) }", "a map's keys are strings"},
		{"//asuka:native\ntype X interface{ M() (int, int) }", "second result is an error"},
		{"//asuka:native\ntype X interface{ M(s string, ctx context.Context) }", "is the first parameter"},
		{"//asuka:native\ntype X interface{ M() func() }", "a parameter only"},
		{"//asuka:native\ntype X interface{ M(s ...string) }", "isn't variadic"},
		{"//asuka:native\ntype X interface{ M(o []X) }", "not in a slice"},
		{"type S struct{ a int }\n//asuka:native\ntype X interface{ M(s S) }", "exported fields only"},
		{"//asuka:native\ntype X interface{ M(t time.Month) }", "a type of another package"},
		{"type F float64\n//asuka:native\ntype X interface{ M(f F) }", "over a string or an integer"},
		{"//asuka:view\ntype Dial struct{ A int }", "named <View>Props"},
		{"//asuka:view\ntype DialProps struct{ On func() int }", "an event returns nothing"},
		{"var ErrX = native.NewError(fmt.Sprint(1))\n//asuka:native\ntype X interface{ M() }", "a constant string"},
	} {
		// A package of this module's (go list resolves its imports).
		dir, err := os.MkdirTemp("testdata", "bad")
		if err != nil {
			t.Fatal(err)
		}
		defer os.RemoveAll(dir)
		src := "package x\n\nimport (\n\t\"context\"\n\t\"fmt\"\n\t\"time\"\n\n\t\"github.com/egoist/asuka/native\"\n)\n\nvar _ = context.Background\nvar _ = fmt.Sprint\nvar _ time.Time\nvar _ = native.NewError\n\n" + c.src + "\n"
		os.WriteFile(filepath.Join(dir, "x.go"), []byte(src), 0o644)
		_, err = Load(dir)
		if err == nil || !strings.Contains(err.Error(), c.want) || !strings.Contains(err.Error(), "x.go:") {
			t.Errorf("%s:\n  %v, want %q at its line", c.src, err, c.want)
		}
	}
}

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

// The pantry package (testdata/pantry) declares in Asuka what the kitchen
// declares in Go: its generated Swift, Kotlin and JavaScript are what's
// committed (-update writes them).
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
	if len(files) != 5 {
		t.Errorf("%d files, want Swift's, Kotlin's, JavaScript's and the desktop's C (a header and a source; no Go client: Asuka's compiler calls it)", len(files))
	}
	for rel, b := range files {
		old, _ := os.ReadFile(filepath.Join(p.Dir, rel))
		if string(old) != string(b) {
			t.Errorf("stale (go test ./internal/nativegen -update): %s", rel)
		}
	}
	if _, err := LoadAsuka("testdata/kitchen"); err != nil {
		t.Fatal(err)
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
	} {
		dir := t.TempDir()
		os.WriteFile(filepath.Join(dir, "x.asuka"), []byte(c.src), 0o644)
		_, err := LoadAsuka(dir)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s: %v, want %q", c.src, err, c.want)
		}
	}
}
