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

	linuxOnce sync.Once
	linuxDir  string
	linuxErr  error
)

func isLinuxHost(d string) bool {
	_, err := os.Stat(filepath.Join(d, "src", "gtk.h"))
	return err == nil
}

// LinuxHostDir is platform/linux: the Linux host's C.
func LinuxHostDir() (string, error) {
	linuxOnce.Do(func() { linuxDir, linuxErr = linuxTree.Dir() })
	return linuxDir, linuxErr
}

// LinuxHost is the Linux host's C: its sources (platform/linux's, with
// the C the desktop hosts share, desktop.HostShared) and the headers they
// include (which key their objects' cache), and their include flags. The
// native registry and asuka_linux.h's C come with the app's native
// packages (appbuild.DesktopNatives).
func LinuxHost() (srcs, flags []string, err error) {
	l, err := LinuxHostDir()
	if err != nil {
		return nil, nil, err
	}
	d, err := desktop.Dir()
	if err != nil {
		return nil, nil, err
	}
	cs, _ := filepath.Glob(filepath.Join(l, "src", "*.c"))
	hs, _ := filepath.Glob(filepath.Join(l, "src", "*.h"))
	srcs = append(append(append(cs, hs...), filepath.Join(l, "src", "syms.def")), desktop.HostShared(d)...)
	return srcs, []string{"-I" + filepath.Join(l, "src"), "-I" + desktop.Include(d)}, nil
}
