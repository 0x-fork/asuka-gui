package windows_test

import (
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

// TestPortable runs the host's C that needs no Windows (test/*_test.c:
// RTF), built with this computer's compiler under ASan and UBSan.
func TestPortable(t *testing.T) {
	cc, err := cbuild.Find()
	if err != nil {
		t.Skip(err)
	}
	exe := filepath.Join(t.TempDir(), "portable_test")
	args := append(append([]string{}, cc.Args...), cbuild.Flags...)
	args = append(args, "-Isrc", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
		"src/rtf.c", "test/rtf_test.c", "-o", exe)
	if out, err := exec.Command(cc.Path, args...).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	if out, err := exec.Command(exe).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
}
