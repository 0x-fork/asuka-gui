package appbuild

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/egoist/asuka/internal/cachedir"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/cgen"
	"github.com/egoist/asuka/platform/desktop"
)

// AppC writes m compiled to C (lang/cgen) under the user's cache, at a
// path of its content's (the core's build caches its object by it), and
// returns the path.
func AppC(m *bytecode.Module) (string, error) {
	if m == nil || m.App < 0 && m.Main < 0 {
		return "", fmt.Errorf("appbuild: the module has no app (an app block) and no fn main")
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

func cacheDir() string { return cachedir.Root() }

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

// LinuxExe builds a Linux app's executable for t at out: the C host
// (platform/linux, with platform/desktop's shared C), the native registry
// and the C of the native packages in dirs (DesktopNatives), and the core,
// linked into one executable, with m compiled to C (lang/cgen) for a
// release, or with m nil asuka dev's player, which loads the bytecode
// asuka dev sends (built for debugging, as Lib's player is).
func LinuxExe(t cbuild.Target, m *bytecode.Module, dirs []string, out string) error {
	host, err := linuxHost()
	if err != nil {
		return err
	}
	natives, err := DesktopNatives(t, dirs)
	if err != nil {
		return err
	}
	return DesktopExe(t, m, host, natives, out)
}

// linuxHost is the Linux host's C (cbuild.LinuxHost: the checkout's, or
// the CLI's own copy).
func linuxHost() (*DesktopC, error) {
	srcs, flags, err := cbuild.LinuxHost()
	if err != nil {
		return nil, err
	}
	return &DesktopC{Sources: srcs, Flags: flags}, nil
}

// MacShared compiles the C the desktop hosts share that the Mac's host
// links too (desktop.MacShared: the route state, flyouts' places, Asuka's
// own words) for arch, into objects cached by their sources.
func MacShared(arch string, debug bool) ([]string, error) {
	d, err := desktop.Dir()
	if err != nil {
		return nil, err
	}
	tc, err := cbuild.ForTarget(cbuild.Target{OS: "darwin", Arch: arch})
	if err != nil {
		return nil, err
	}
	var srcs, hdrs []string
	for _, f := range desktop.MacShared(d) {
		if strings.HasSuffix(f, ".c") {
			srcs = append(srcs, f)
		} else {
			hdrs = append(hdrs, f)
		}
	}
	flags := []string{"-I" + desktop.Include(d), "-ffunction-sections", "-fdata-sections"}
	if debug {
		flags = append(flags, "-g", "-O1")
	} else {
		flags = append(flags, "-O2", "-DNDEBUG")
	}
	return tc.Objects("mac-shared", srcs, hdrs, flags)
}
