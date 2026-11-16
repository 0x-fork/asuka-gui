package appbuild

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/cgen"
)

// AppC writes m compiled to C (lang/cgen) under the user's cache, at a
// path of its content's (the core's build caches its object by it), and
// returns the path.
func AppC(m *bytecode.Module) (string, error) {
	if m == nil || m.App < 0 {
		return "", fmt.Errorf("appbuild: the module has no app (an app block)")
	}
	src, err := cgen.Generate(m)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(src)
	dir := filepath.Join(cacheDir(), "app-"+hex.EncodeToString(sum[:])[:16])
	p := filepath.Join(dir, "app.c")
	if _, err := os.Stat(p); err == nil {
		return p, nil
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	tmp := fmt.Sprintf("%s.%d.tmp", p, os.Getpid())
	if err := os.WriteFile(tmp, src, 0o644); err != nil {
		return "", err
	}
	return p, os.Rename(tmp, p)
}

func cacheDir() string {
	d, err := os.UserCacheDir()
	if err != nil {
		d = os.TempDir()
	}
	return filepath.Join(d, "asuka")
}

// Lib builds the core for t into out (kind: cbuild.Static for iOS,
// Shared for Android and the desktops, Module for the web): with m
// compiled into it, a release's (asuka_run starts it), or with m nil, a
// dev player's, which loads the bytecode asuka dev sends
// (asuka_dev_connect). A player is built for debugging (assertions on,
// optimized less); a release isn't.
func Lib(t cbuild.Target, kind cbuild.LibKind, m *bytecode.Module, out string) error {
	var c *bytecode.C
	if m != nil {
		c = m.C
	}
	return lib(t, kind, m, c, out)
}

// PlayerLib builds a dev player's core for t into out, as Lib with no
// module does, with the C c calls (the app's externs', from its last
// compile; nil for none) compiled in, which the module the player loads
// calls through its ffi.* builtins.
func PlayerLib(t cbuild.Target, kind cbuild.LibKind, c *bytecode.C, out string) error {
	return lib(t, kind, nil, c, out)
}

func lib(t cbuild.Target, kind cbuild.LibKind, m *bytecode.Module, c *bytecode.C, out string) error {
	core, err := cbuild.CoreDir()
	if err != nil {
		return err
	}
	tc, err := cbuild.ForTarget(t)
	if err != nil {
		return err
	}
	var extra []string
	if m != nil {
		src, err := AppC(m)
		if err != nil {
			return err
		}
		extra = append(extra, src)
	}
	ffi, err := Foreign(c, t)
	if err != nil {
		return err
	}
	return tc.LibWith(core, kind, cbuild.LibOptions{Extra: extra, Foreign: ffi, Debug: m == nil}, out)
}

// MacArchs are a Mac app's architectures: a universal binary's.
var MacArchs = []string{"arm64", "amd64"}

// MacLib builds libasuka.dylib for every Mac (MacArchs) into out, one
// universal binary (lipo), as Lib.
func MacLib(m *bytecode.Module, out string) error {
	return MacPlayerLib(m, nil, out)
}

// MacPlayerLib is MacLib for a player (m nil) with the C c calls.
func MacPlayerLib(m *bytecode.Module, c *bytecode.C, out string) error {
	work, err := os.MkdirTemp("", "asuka-mac-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(work)
	var parts []string
	for _, arch := range MacArchs {
		p := filepath.Join(work, arch, "libasuka.dylib")
		cc := c
		if m != nil {
			cc = m.C
		}
		if err := lib(cbuild.Target{OS: "darwin", Arch: arch}, cbuild.Shared, m, cc, p); err != nil {
			return err
		}
		parts = append(parts, p)
	}
	return lipo(parts, out)
}

func lipo(parts []string, out string) error {
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return err
	}
	args := append([]string{"lipo", "-create", "-output", out}, parts...)
	if b, err := exec.Command("xcrun", args...).CombinedOutput(); err != nil {
		return fmt.Errorf("lipo: %v\n%s", err, b)
	}
	return nil
}
