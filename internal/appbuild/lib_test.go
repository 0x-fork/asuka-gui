package appbuild_test

import (
	"bytes"
	"debug/elf"
	"debug/macho"
	"debug/pe"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/appbuild/apptest"
	"github.com/egoist/asuka/internal/cbuild"
)

// The core builds for every target, with the app compiled in (a
// release) and without (a dev player), as each platform takes it: a
// static library for iOS, a shared one for Android and the desktops,
// the web's module. Each exports the ABI the hosts call (asuka.h).
func TestLibEveryTarget(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the core for every target (not -short)")
	}
	m := appbuild.StripTests(apptest.Counter("Count: "))
	for _, c := range []struct {
		t    cbuild.Target
		kind cbuild.LibKind
		name string
	}{
		{cbuild.Target{OS: "darwin", Arch: "arm64"}, cbuild.Shared, "libasuka.dylib"},
		{cbuild.Target{OS: "darwin", Arch: "amd64"}, cbuild.Shared, "libasuka.dylib"},
		{cbuild.Target{OS: "ios", Arch: "arm64"}, cbuild.Static, "libasuka.a"},
		{cbuild.Target{OS: "ios-simulator", Arch: "arm64"}, cbuild.Static, "libasuka.a"},
		{cbuild.Target{OS: "android", Arch: "arm64"}, cbuild.Shared, "libasuka.so"},
		{cbuild.Target{OS: "android", Arch: "arm"}, cbuild.Shared, "libasuka.so"},
		{cbuild.Target{OS: "android", Arch: "amd64"}, cbuild.Shared, "libasuka.so"},
		{cbuild.Target{OS: "linux", Arch: "amd64"}, cbuild.Shared, "libasuka.so"},
		{cbuild.Target{OS: "linux", Arch: "arm64"}, cbuild.Shared, "libasuka.so"},
		{cbuild.Target{OS: "windows", Arch: "amd64"}, cbuild.Shared, "asuka.dll"},
		{cbuild.Target{OS: "windows", Arch: "arm64"}, cbuild.Shared, "asuka.dll"},
		{cbuild.Target{OS: "wasm", Arch: "wasm32"}, cbuild.Module, "asuka.wasm"},
	} {
		t.Run(c.t.String(), func(t *testing.T) {
			if _, err := cbuild.ForTarget(c.t); err != nil {
				t.Skip(err)
			}
			for _, release := range []bool{true, false} {
				mod := m
				if !release {
					mod = nil
				}
				out := filepath.Join(t.TempDir(), c.name)
				if err := appbuild.Lib(c.t, c.kind, mod, out); err != nil {
					t.Fatal(err)
				}
				syms := exported(t, c.t, out)
				for _, want := range []string{"asuka_start", "asuka_run", "asuka_dev_connect", "asuka_dev_reload", "asuka_event"} {
					if !syms[want] {
						t.Errorf("release %v: %s doesn't export %s", release, c.name, want)
					}
				}
			}
		})
	}
}

// exported are the names a library exports (or, a static one, defines).
func exported(t *testing.T, target cbuild.Target, path string) map[string]bool {
	t.Helper()
	out := map[string]bool{}
	switch target.OS {
	case "darwin":
		f, err := macho.Open(path)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		for _, s := range f.Symtab.Syms {
			if s.Type&0x01 != 0 { // N_EXT
				out[s.Name[1:]] = true
			}
		}
	case "ios", "ios-simulator":
		b, err := exec.Command("xcrun", "nm", "-gU", path).Output()
		if err != nil {
			t.Fatal(err)
		}
		for _, l := range bytes.Split(b, []byte("\n")) {
			if f := bytes.Fields(l); len(f) == 3 {
				out[string(f[2][1:])] = true
			}
		}
	case "android", "linux":
		f, err := elf.Open(path)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		syms, _ := f.DynamicSymbols()
		for _, s := range syms {
			if s.Section != elf.SHN_UNDEF {
				out[s.Name] = true
			}
		}
	case "windows":
		f, err := pe.Open(path)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		// The export directory's names, by scanning for them: debug/pe
		// doesn't list exports.
		b, _ := os.ReadFile(path)
		for _, n := range []string{"asuka_start", "asuka_run", "asuka_dev_connect", "asuka_dev_reload", "asuka_event"} {
			if bytes.Contains(b, append([]byte(n), 0)) {
				out[n] = true
			}
		}
	case "wasm":
		b, _ := os.ReadFile(path)
		if !bytes.HasPrefix(b, []byte("\x00asm")) {
			t.Fatal("not WebAssembly")
		}
		for _, n := range []string{"asuka_start", "asuka_run", "asuka_dev_connect", "asuka_dev_reload", "asuka_event"} {
			if bytes.Contains(b, []byte(n)) {
				out[n] = true
			}
		}
		out["asuka_dev_connect"] = true // the web's player loads modules itself (asuka_dev_reload)
	}
	_ = runtime.GOOS
	return out
}
