package appbuild

import (
	"os"
	"path/filepath"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/internal/nativegen"
	"github.com/egoist/asuka/platform/desktop"
	"github.com/egoist/asuka/std"
)

// Units are the C a release of the CLI prebuilds for t (internal/
// prebuilt), each a static library per configuration: the core's
// everywhere; on the Mac, the C its host shares with the desktops'; on
// Linux and Windows, the host's, the native registry's and every std and
// sdk package's.
func Units(t cbuild.Target) ([]cbuild.Unit, error) {
	core, err := cbuild.CoreDir()
	if err != nil {
		return nil, err
	}
	units := []cbuild.Unit{cbuild.CoreUnit(core)}
	switch t.OS {
	case "darwin":
		u, err := MacSharedUnit()
		if err != nil {
			return nil, err
		}
		units = append(units, u)
	case "linux", "windows":
		if t.OS == "linux" {
			u, err := cbuild.LinuxHost(core)
			if err != nil {
				return nil, err
			}
			units = append(units, u)
		} else {
			win, embedded, err := windowsTree.Find()
			if err != nil {
				return nil, err
			}
			dsk, err := desktop.Dir()
			if err != nil {
				return nil, err
			}
			units = append(units, windowsHostUnit(core, win, dsk, embedded))
		}
		natives, err := DesktopNatives(t, StdDesktopDirs())
		if err != nil {
			return nil, err
		}
		for _, u := range natives.Units {
			if t.OS == "windows" && u.Name == "desktop" {
				continue // the Windows host's already
			}
			units = append(units, u)
		}
	}
	return units, nil
}

// StdDesktopDirs are the std and sdk packages with C for the desktops
// (their desktop/).
func StdDesktopDirs() []string {
	ds, err := std.PackageDirs()
	if err != nil {
		return nil
	}
	var out []string
	for _, d := range ds {
		if st, err := os.Stat(filepath.Join(d, nativegen.DesktopDir)); err == nil && st.IsDir() {
			out = append(out, d)
		}
	}
	return out
}
