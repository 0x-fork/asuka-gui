package appbuild

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/internal/nativegen"
	"github.com/egoist/asuka/internal/toml"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/platform/desktop"
)

// DesktopC is the C a Windows or Linux app's executable is built from
// besides the core and the app (RFC 0008): its host's, the native
// registry's (platform/desktop) and its native packages' (each one's
// desktop/, nativegen.CSources), with their include directories and the
// libraries they link.
type DesktopC struct {
	Sources []string // .c files (and the headers they include, for the cache)
	Deps    []string // what the .c files include besides (a vendored library's sources), for the cache
	Flags   []string // -I…
	Libs    []string // -l…
}

// DesktopNatives is the registry and the C for t of the native packages
// in dirs (their directories; a package with no C for t.OS adds
// nothing). A package's asuka.toml names the system libraries its C links
// with on each platform:
//
//	[windows]
//	libraries = ["ole32", "winhttp"]
//	[linux]
//	libraries = []   # GTK, GLib and GIO are resolved at run time, as the host does
func DesktopNatives(t cbuild.Target, dirs []string) (*DesktopC, error) {
	d, err := desktop.Dir()
	if err != nil {
		return nil, err
	}
	c := &DesktopC{
		Sources: append(desktop.Shared(d), filepath.Join(desktop.Include(d), "asuka_desktop.h")),
		Flags:   []string{"-I" + desktop.Include(d)},
		Libs:    desktop.Libs(t.OS),
	}
	if t.OS == "linux" {
		c.Flags = append(c.Flags, "-D_GNU_SOURCE") // POSIX's and GNU's declarations, beside C17's
	}
	seen := map[string]bool{}
	sorted := append([]string{}, dirs...)
	sort.Strings(sorted)
	for _, pkg := range sorted {
		dir := filepath.Join(pkg, nativegen.DesktopDir)
		srcs := nativegen.CSources(dir, t.OS)
		if len(srcs) == 0 || seen[dir] {
			continue
		}
		seen[dir] = true
		for _, s := range srcs {
			c.Sources = append(c.Sources, filepath.Join(dir, s))
		}
		hs, _ := filepath.Glob(filepath.Join(dir, "*.h"))
		c.Sources = append(c.Sources, hs...)
		c.Deps = append(c.Deps, vendored(dir)...)
		c.Flags = append(c.Flags, "-I"+dir)
		libs, err := packageLibs(pkg, t.OS)
		if err != nil {
			return nil, err
		}
		for _, l := range libs {
			c.Libs = append(c.Libs, "-l"+l)
		}
	}
	return c, nil
}

// vendored are the files of the libraries a package's C vendors, in
// desktop/third_party/ (each with its license): the package's own C
// includes their sources, so they count in the cache's key but are never
// compiled alone.
func vendored(dir string) []string {
	var out []string
	filepath.WalkDir(filepath.Join(dir, "third_party"), func(p string, d os.DirEntry, err error) error {
		if err == nil && !d.IsDir() {
			out = append(out, p)
		}
		return nil
	})
	return out
}

// packageLibs are the libraries a package's asuka.toml says its C links
// with on goos.
func packageLibs(dir, goos string) ([]string, error) {
	b, err := os.ReadFile(filepath.Join(dir, "asuka.toml"))
	if err != nil {
		return nil, nil
	}
	type libs struct {
		Libraries []string `json:"libraries"`
	}
	var m struct {
		Windows libs `json:"windows"`
		Linux   libs `json:"linux"`
	}
	if err := toml.Unmarshal(b, &m); err != nil {
		return nil, fmt.Errorf("%s: %v", filepath.Join(dir, "asuka.toml"), err)
	}
	if goos == "windows" {
		return m.Windows.Libraries, nil
	}
	return m.Linux.Libraries, nil
}

// DesktopExe builds a Windows or Linux app's executable for t at out:
// host's C, the core, the native C and m compiled to C (a release's), or
// with m nil a dev player's, which loads the bytecode asuka dev sends.
func DesktopExe(t cbuild.Target, m *bytecode.Module, host, natives *DesktopC, out string) error {
	core, err := cbuild.CoreDir()
	if err != nil {
		return err
	}
	tc, err := cbuild.ForTarget(t)
	if err != nil {
		return err
	}
	var srcs, deps, flags, libs []string
	for _, c := range []*DesktopC{host, natives} {
		if c == nil {
			continue
		}
		srcs = append(srcs, c.Sources...)
		deps = append(deps, c.Deps...)
		flags = append(flags, c.Flags...)
		libs = append(libs, c.Libs...)
	}
	if m != nil {
		app, err := AppC(m)
		if err != nil {
			return err
		}
		srcs = append(srcs, app)
		flags = append(flags, "-DAK_APP_COMPILED=1")
	}
	return tc.Exe(core, srcs, deps, flags, libs, m == nil, out)
}
