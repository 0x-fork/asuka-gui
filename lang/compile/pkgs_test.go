package compile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestPackages(t *testing.T) {
	res, err := Dir("testdata/pkgs/app", nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	out, err := Run(testRunner(t), res.Module, true, "")
	want := "#1 a\nHI!\n1\nok packages"
	if strings.TrimSpace(out) != want || err != nil {
		t.Fatalf("got (%v):\n%s\nwant:\n%s", err, out, want)
	}
}

func TestPackageErrors(t *testing.T) {
	res := Files(map[string][]byte{"a.asuka": []byte(`
import ./missing
import asuka/nope
`)}, nil)
	got := res.Err()
	if got == nil || !strings.Contains(got.Error(), "can't read package ./missing") || !strings.Contains(got.Error(), "no package asuka/nope") {
		t.Fatalf("got %v", got)
	}
	res2, err := Dir("testdata/pkgs/app", nil)
	if err != nil {
		t.Fatal(err)
	}
	_ = res2
}

// A project's imports: the standard library's packages (std/), and a
// dependency asuka.toml names, resolved and locked on the first build.
func TestProjectImports(t *testing.T) {
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
	write("charts/asuka.toml", "name = \"@acme/line-charts\"\nversion = \"1.0.0\"\n")
	write("charts/charts.asuka", "pub fn twice(n: int) -> int = n * 2\n")
	write("app/asuka.toml", "name = \"app\"\n\n[dependencies]\n\"@acme/line-charts\" = \"file:../charts\"\n")
	write("app/app.asuka", `import asuka/store
import @acme/line-charts as charts

test "imports" {
  print(store.read("units", "metric"))
  print(charts.twice(21))
}
`)
	res, err := Dir(filepath.Join(root, "app"), nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if lock, err := os.ReadFile(filepath.Join(root, "app", "asuka.lock")); err != nil {
		t.Errorf("no asuka.lock: %v", err)
	} else if !strings.Contains(string(lock), `"file:../charts"`) {
		t.Errorf("asuka.lock should record the package from the project:\n%s", lock)
	}
	out, err := Run(testRunner(t), res.Module, true, "")
	// No platform here: the store keeps nothing, and reads the default.
	if want := "metric\n42\nok imports"; strings.TrimSpace(out) != want || err != nil {
		t.Fatalf("got (%v):\n%s\nwant:\n%s", err, out, want)
	}

	res = Files(map[string][]byte{"a.asuka": []byte("import @acme/charts\n")}, nil)
	if got := res.Err(); got == nil || !strings.Contains(got.Error(), "can't import @acme/charts: a dependency needs a project") {
		t.Errorf("a dependency outside a project: %v", got)
	}
}

// A standard library package's native fn is the runtime's builtin of
// its package and name (here str.len, time.sleep); others can't have one.
func TestNativeFn(t *testing.T) {
	res := Files(map[string][]byte{"a.asuka": []byte(`import asuka/str
import asuka/time

test "native fns" {
  print(str.len("héllo"))
  await time.sleep(5ms)
  let f = str.len
  print(f("ab"))
}
`)}, &Options{Packages: map[string]map[string][]byte{
		"asuka/str":  {"str.asuka": []byte("pub native fn len(s: string) -> int\n")},
		"asuka/time": {"time.asuka": []byte("pub native async fn sleep(d: Duration)\n")},
	}})
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	out, err := Run(testRunner(t), res.Module, true, "")
	if want := "5\n2\nok native fns"; strings.TrimSpace(out) != want || err != nil {
		t.Fatalf("got (%v):\n%s\nwant:\n%s", err, out, want)
	}

	res = Files(map[string][]byte{"a.asuka": []byte("native fn f() -> int\n")}, nil)
	if got := res.Err(); got == nil || !strings.Contains(got.Error(), "native fn is the standard library's") {
		t.Errorf("a native fn outside the standard library: %v", got)
	}
}
