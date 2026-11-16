package cbuild

import (
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/internal/srcdir"
	"github.com/egoist/asuka/platform/desktop"
	"github.com/egoist/asuka/platform/linux"
)

// The C desktop hosts' sources: the repository's when the CLI runs from a
// checkout, else the copies embedded in it, extracted once.
var (
	linuxTree   = srcdir.Tree{Env: "ASUKA_LINUX_HOST", Sub: filepath.Join("platform", "linux"), Valid: isLinuxHost, Files: linux.Sources, Cache: "linux-src"}
	desktopTree = srcdir.Tree{Env: "ASUKA_DESKTOP_C", Sub: filepath.Join("platform", "desktop"), Valid: isDesktopC, Files: desktop.Sources, Cache: "desktop-src"}

	linuxOnce, desktopOnce sync.Once
	linuxDir, desktopDir   string
	linuxErr, desktopErr   error
)

func isLinuxHost(d string) bool {
	_, err := os.Stat(filepath.Join(d, "src", "syms.def"))
	return err == nil
}

func isDesktopC(d string) bool {
	_, err := os.Stat(filepath.Join(d, "include", "desktop.h"))
	return err == nil
}

// LinuxHostDir is platform/linux: the Linux host's C.
func LinuxHostDir() (string, error) {
	linuxOnce.Do(func() { linuxDir, linuxErr = linuxTree.Dir() })
	return linuxDir, linuxErr
}

// DesktopDir is platform/desktop: the C the desktop hosts share.
func DesktopDir() (string, error) {
	desktopOnce.Do(func() { desktopDir, desktopErr = desktopTree.Dir() })
	return desktopDir, desktopErr
}

// LinuxHost is the Linux host's build: its sources (platform/linux's and
// platform/desktop's) and their include directories.
func LinuxHost() (HostSources, error) {
	l, err := LinuxHostDir()
	if err != nil {
		return HostSources{}, err
	}
	d, err := DesktopDir()
	if err != nil {
		return HostSources{}, err
	}
	srcs, _ := filepath.Glob(filepath.Join(l, "src", "*.c"))
	more, _ := filepath.Glob(filepath.Join(d, "src", "*.c"))
	var keep []string
	for _, f := range append(srcs, more...) {
		if filepath.Base(f) == "check_syms.c" {
			continue // the declarations' check, not the host
		}
		keep = append(keep, f)
	}
	hdrs, _ := filepath.Glob(filepath.Join(l, "src", "*.h"))
	hdrs = append(hdrs, filepath.Join(l, "src", "syms.def"))
	dh, _ := filepath.Glob(filepath.Join(d, "include", "*.h"))
	return HostSources{
		Sources:  keep,
		Headers:  append(hdrs, dh...),
		Includes: []string{"-I" + filepath.Join(l, "src"), "-I" + filepath.Join(d, "include")},
		Link:     []string{"-lm", "-lpthread", "-ldl"},
	}, nil
}
