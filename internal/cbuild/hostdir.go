package cbuild

import (
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/internal/srcdir"
	"github.com/egoist/asuka/platform/desktop"
	"github.com/egoist/asuka/platform/linux"
)

// The Linux host's sources: the repository's when the CLI runs from a
// checkout, else the copies embedded in it, extracted once.
var (
	linuxTree = srcdir.Tree{Env: "ASUKA_LINUX_HOST", Sub: filepath.Join("platform", "linux"), Valid: isLinuxHost, Files: linux.Sources, Cache: "linux-src"}

	linuxOnce     sync.Once
	linuxDir      string
	linuxEmbedded bool
	linuxErr      error
)

func isLinuxHost(d string) bool {
	_, err := os.Stat(filepath.Join(d, "src", "gtk.h"))
	return err == nil
}

// LinuxHostDir is platform/linux: the Linux host's C.
func LinuxHostDir() (string, error) {
	linuxOnce.Do(func() { linuxDir, linuxEmbedded, linuxErr = linuxTree.Find() })
	return linuxDir, linuxErr
}

// LinuxHost is the Linux host's C as a unit: platform/linux's, with the
// C the desktop hosts share (desktop.HostShared), compiled as the core is
// with its include directories. The native registry and asuka_linux.h's C
// come with the app's native packages (appbuild.DesktopNatives).
func LinuxHost(core string) (Unit, error) {
	l, err := LinuxHostDir()
	if err != nil {
		return Unit{}, err
	}
	d, err := desktop.Dir()
	if err != nil {
		return Unit{}, err
	}
	srcs, _ := filepath.Glob(filepath.Join(l, "src", "*.c"))
	srcs = append(srcs, desktop.HostShared(d)...)
	flags := append(append(append([]string{}, Flags...), Includes(core)...), "-I"+filepath.Join(l, "src"), "-I"+desktop.Include(d), "-D_GNU_SOURCE")
	return Unit{Name: "linux-host", Sources: srcs, Flags: flags, Embedded: linuxEmbedded && desktop.Embedded() && CoreEmbedded()}, nil
}
