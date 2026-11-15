package core_test

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/cgen"
)

// The whole stack on a desktop: the Counter compiled to C (lang/cgen),
// linked with the core into the library the app's executable loads
// (libasuka), run by the platform's real host (cmd/asuka-host: AppKit,
// through host/bridge), which presses Add and dumps its view tree.
func TestDesktopCounter(t *testing.T) {
	if runtime.GOOS != "darwin" || testing.Short() {
		t.Skip("the macOS host, on a Mac (not -short)")
	}
	m, body := counterModule(t)
	m.App = int32(body)
	src, err := cgen.Generate(m)
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	app := filepath.Join(dir, "app.c")
	if err := os.WriteFile(app, src, 0o644); err != nil {
		t.Fatal(err)
	}
	tc, err := cbuild.ForTarget(cbuild.Host())
	if err != nil {
		t.Skip(err)
	}
	root, _ := filepath.Abs(".")
	lib := filepath.Join(dir, "libasuka.dylib")
	if err := tc.Lib(root, cbuild.Shared, []string{app}, false, lib); err != nil {
		t.Fatal(err)
	}
	exe := filepath.Join(dir, "counter")
	if out, err := exec.Command("go", "build", "-o", exe, "../cmd/asuka-host").CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	snap := filepath.Join(dir, "c.png")
	cmd := exec.Command(exe)
	cmd.Env = append(os.Environ(), "ASUKA_CORE_LIB="+lib, "ASUKA_SCRIPT=tap:Add", "ASUKA_SNAPSHOT="+snap, "ASUKA_DUMP=1")
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	dump, err := os.ReadFile(snap + ".txt")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(dump), "Count: 1") {
		t.Fatalf("after tapping Add, the window shows:\n%s", dump)
	}
	t.Logf("%s", dump)
}
