package windows_test

import (
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/platform/desktop"
)

// TestSharedC runs platform/desktop/test/windows_test.c in Windows: the
// shared C's helpers that need Windows itself (asuka_windows.h's),
// built with the shared C and the unit tests' host
// (platform/desktop/test/unit.c); it passes when it exits 0.
func TestSharedC(t *testing.T) {
	w := onWindows(t)
	zig(t)
	desk, err := desktop.Dir()
	if err != nil {
		t.Fatal(err)
	}
	natives, err := appbuild.DesktopNatives(target(w), nil)
	if err != nil {
		t.Fatal(err)
	}
	host := &appbuild.DesktopC{
		Sources: []string{filepath.Join(desk, "test", "windows_test.c"), filepath.Join(desk, "test", "unit.c")},
		Flags:   []string{"-I" + filepath.Join(desk, "include")},
	}
	local, guest := w.job(t)
	if err := appbuild.DesktopExe(target(w), nil, host, natives, filepath.Join(local, "shared_test.exe")); err != nil {
		t.Fatal(err)
	}
	out, err := w.run(t, guest, guest+`\shared_test.exe`, nil)
	t.Logf("%s", out)
	if err != nil {
		t.Fatal(err)
	}
}
