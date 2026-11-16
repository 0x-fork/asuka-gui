package desktop_test

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/internal/nativegen"
	"github.com/egoist/asuka/std"
)

// The standard library's C unit tests: each package's desktop/*_test.c
// (a main of its own) built with the package's portable C (its files for
// both platforms; neither platform's own nor the generated glue, which
// calls them), the registry and test/unit.c's host, under ASan and UBSan, and run: it
// passes when it exits 0. What a package's C works out without a
// platform (parsing, decoding, encoding) is tested so on every system.
func TestStdUnits(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("built with the POSIX test host's flags")
	}
	core, err := cbuild.CoreDir()
	if err != nil {
		t.Skip(err)
	}
	d, err := std.Dir()
	if err != nil {
		t.Fatal(err)
	}
	tests, _ := filepath.Glob(filepath.Join(d, "*", "desktop", "*_test.c"))
	if len(tests) == 0 {
		t.Fatal("no C tests")
	}
	desk, _ := filepath.Abs(".")
	for _, test := range tests {
		dir := filepath.Dir(test)
		name := filepath.Base(filepath.Dir(dir)) + "/" + strings.TrimSuffix(filepath.Base(test), ".c")
		t.Run(name, func(t *testing.T) {
			srcs := []string{test, filepath.Join(desk, "test", "unit.c"), filepath.Join(desk, "src", "native.c"), filepath.Join(desk, "src", "json.c"),
				filepath.Join(desk, "src", "linux.c")}
			for _, f := range nativegen.CSources(dir, "unit") {
				if !strings.HasSuffix(f, ".asuka.c") {
					srcs = append(srcs, filepath.Join(dir, f))
				}
			}
			flags := append(append([]string{}, cbuild.Flags...), "-I"+filepath.Join(desk, "include"), "-I"+dir, "-D_GNU_SOURCE")
			work := t.TempDir()
			exe, err := cbuild.Exe(core, srcs, flags, nil, true, work, "unit")
			if err != nil {
				t.Fatal(err)
			}
			cmd := exec.Command(exe)
			cmd.Dir = dir
			cmd.Env = append(os.Environ(), "ASUKA_DATA="+work, noLeaks)
			out, err := cmd.CombinedOutput()
			t.Logf("%s", out)
			if err != nil {
				t.Fatal(err)
			}
		})
	}
}

// The barcode readers (asuka_barcode.h: src/barcode*.c and the vendored
// quirc and libdmtx), on images of codes (testdata/barcode), under ASan
// and UBSan.
func TestBarcode(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("built with the POSIX test host's flags")
	}
	core, err := cbuild.CoreDir()
	if err != nil {
		t.Skip(err)
	}
	desk, _ := filepath.Abs(".")
	srcs := []string{filepath.Join(desk, "test", "barcode_test.c")}
	for _, f := range []string{"barcode.c", "barcode1d.c", "barcode_vendor.c"} {
		srcs = append(srcs, filepath.Join(desk, "src", f))
	}
	flags := append(append([]string{}, cbuild.Flags...), "-I"+filepath.Join(desk, "include"), "-D_GNU_SOURCE")
	work := t.TempDir()
	exe, err := cbuild.Exe(core, srcs, flags, nil, true, work, "barcode")
	if err != nil {
		t.Fatal(err)
	}
	out, err := exec.Command(exe, filepath.Join(desk, "testdata", "barcode")).CombinedOutput()
	t.Logf("%s", out)
	if err != nil {
		t.Fatal(err)
	}
}
