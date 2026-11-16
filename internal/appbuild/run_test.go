package appbuild_test

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/appbuild/apptest"
	"github.com/egoist/asuka/internal/cbuild"
)

func needCC(t *testing.T) {
	t.Helper()
	if _, err := cbuild.Find(); err != nil {
		t.Skip(err)
	}
}

// A module's tests run on the fake host: each reported, a failure with
// where its test block is.
func TestRunTests(t *testing.T) {
	needCC(t)
	m := apptest.Counter("Count: ")
	apptest.Failing(m, "Count: ")
	var out strings.Builder
	s, err := appbuild.RunTests(m, appbuild.TestOptions{}, &out)
	if err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if len(s.Results) != 2 || !s.Results[0].OK || s.Results[1].OK || s.Failed() != 1 {
		t.Fatalf("results %+v\n%s", s.Results, out.String())
	}
	want := "ok   Add counts\nFAIL shows seven (app.asuka:18)\n     the screen doesn't show \"Count: 7\""
	if !strings.HasPrefix(out.String(), want) {
		t.Fatalf("got:\n%s\nwant it to start:\n%s", out.String(), want)
	}
	// A pattern picks tests.
	out.Reset()
	s, err = appbuild.RunTests(m, appbuild.TestOptions{Pattern: "Add", Platform: "android"}, &out)
	if err != nil || len(s.Results) != 1 || s.Failed() != 0 {
		t.Fatalf("%v %+v\n%s", err, s, out.String())
	}
}

// ShowApp prints what the app's screen shows.
func TestShowApp(t *testing.T) {
	needCC(t)
	var out strings.Builder
	if err := appbuild.ShowApp(apptest.Counter("Total: "), &out); err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if !strings.Contains(out.String(), `Text 0,0 390x20 "Total: 0"`) || !strings.Contains(out.String(), `"Add"`) {
		t.Fatalf("the screen:\n%s", out.String())
	}
}

// Compile is the compiler: a project's diagnostics as file:line:col
// messages, its module once there are none; one file (asuka run) runs
// its fn main() as a test named main.
func TestCompile(t *testing.T) {
	dir := t.TempDir()
	write := func(name, src string) {
		if err := os.WriteFile(filepath.Join(dir, name), []byte(src), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("app.asuka", "component C { Txt(\"hi\") }\napp { C() }\n")
	p, err := appbuild.Load(dir)
	if err != nil {
		t.Fatal(err)
	}
	m, diags, err := p.Compile()
	if err != nil || m != nil || len(diags) != 1 || !strings.Contains(diags[0], "app.asuka:1:") {
		t.Fatalf("a mistake: %v %v %v", m, diags, err)
	}
	write("app.asuka", "component C { Text(\"hi\") }\napp { C() }\nfn main() { print(\"main\") }\n")
	if m, diags, err = p.Compile(); err != nil || m == nil || len(diags) != 0 || len(m.Tests) != 0 {
		t.Fatalf("an app: %v %v %v", m, diags, err)
	}
	f, err := appbuild.Load(filepath.Join(dir, "app.asuka"))
	if err != nil {
		t.Fatal(err)
	}
	if m, _, err = f.Compile(); err != nil || m == nil || len(m.Tests) != 1 {
		t.Fatalf("one file, with main: %v %v", m, err)
	}
}

// A module without its tests keeps what its app and init reach, and
// calls none of the test builtins; it runs as before.
func TestStripTests(t *testing.T) {
	needCC(t)
	m := apptest.Counter("Count: ")
	apptest.Failing(m, "Count: ")
	s := appbuild.StripTests(m)
	if len(s.Tests) != 0 || len(s.Funcs) != len(m.Funcs)-2 || len(m.Tests) != 2 {
		t.Fatalf("funcs %d of %d, tests %d", len(s.Funcs), len(m.Funcs), len(s.Tests))
	}
	for _, b := range s.Builtins {
		if strings.HasPrefix(b, "test.") {
			t.Errorf("calls %s", b)
		}
	}
	if s.Funcs[s.App].Name != "Counter" || s.Funcs[s.Init].Name != "init" {
		t.Errorf("app %s, init %s", s.Funcs[s.App].Name, s.Funcs[s.Init].Name)
	}
	var out strings.Builder
	if err := appbuild.ShowApp(s, &out); err != nil || !strings.Contains(out.String(), `"Count: 0"`) {
		t.Fatalf("%v\n%s", err, out.String())
	}
}

// A host built with an app's native packages' Go links them in: their
// servers register as the host starts.
func TestHostNatives(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the host (not -short)")
	}
	if appbuild.Checkout() == "" {
		t.Skip("not a checkout")
	}
	if _, err := exec.LookPath("go"); err != nil {
		t.Skip(err)
	}
	pkg := filepath.Join(t.TempDir(), "probe", "desktop")
	os.MkdirAll(pkg, 0o755)
	os.WriteFile(filepath.Join(pkg, "probe.go"), []byte(`package probe

import (
	"context"

	"github.com/egoist/asuka/native"
)

func init() {
	native.RegisterServer(&native.Server{
		Class: "acme/probe-class",
		New:   func(uint32) (any, error) { return nil, native.ErrClosed },
		Call: func(context.Context, *native.Session, any, uint32, *native.Reader, *native.Writer) error {
			return native.ErrClosed
		},
	})
}
`), 0o644)
	out := filepath.Join(t.TempDir(), "host")
	target := cbuild.Target{OS: runtime.GOOS, Arch: runtime.GOARCH}
	if err := appbuild.Host(target, out, appbuild.HostOptions{Natives: []string{pkg}}); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(out)
	if err != nil || !bytes.Contains(b, []byte("acme/probe-class")) {
		t.Fatalf("the host doesn't have the package's Go (%v)", err)
	}
}
