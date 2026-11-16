// The shared desktop C's tests (test/desktop_test.c), built with this
// computer's compiler and run here: what platform/windows and
// platform/linux share, checked on any system.
package desktop_test

import (
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

func TestC(t *testing.T) {
	cc, err := cbuild.Find()
	if err != nil {
		t.Skip(err)
	}
	srcs, _ := filepath.Glob("src/*.c")
	srcs = append(srcs, "test/desktop_test.c")
	exe := filepath.Join(t.TempDir(), "desktop_test")
	args := append(append([]string{}, cc.Args...), cbuild.Flags...)
	args = append(args, "-Iinclude", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined")
	args = append(args, srcs...)
	args = append(args, "-o", exe)
	if out, err := exec.Command(cc.Path, args...).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	out, err := exec.Command(exe, t.TempDir()).CombinedOutput()
	if err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
}
