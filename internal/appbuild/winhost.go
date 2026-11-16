package appbuild

import (
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/internal/srcdir"
	"github.com/egoist/asuka/platform/desktop"
	"github.com/egoist/asuka/platform/windows"
)

// The C hosts' sources (RFC 0008): the repository's, or the CLI's
// embedded copies.
var (
	windowsTree = srcdir.Tree{Env: "ASUKA_WINDOWS_HOST", Sub: filepath.Join("platform", "windows"), Valid: func(d string) bool {
		_, err := os.Stat(filepath.Join(d, "src", "host.h"))
		return err == nil
	}, Files: windows.Sources, Cache: "windows-src"}
	desktopTree = srcdir.Tree{Env: "ASUKA_DESKTOP_C", Sub: filepath.Join("platform", "desktop"), Valid: func(d string) bool {
		_, err := os.Stat(filepath.Join(d, "include", "desktop.h"))
		return err == nil
	}, Files: desktop.Sources, Cache: "desktop-src"}
)

// WindowsOptions say how a Windows executable is built.
type WindowsOptions struct {
	// App are an app's C sources (lang/cgen's app.c, its packages' glue):
	// none for asuka dev's player, which loads the app's bytecode.
	App []string
	// Natives are native packages' Windows C (a package's windows/*.c),
	// compiled with the host's headers.
	Natives []string
	// Resources is a COFF object with the executable's resources (its
	// icon, manifest and version).
	Resources string
	// Console makes it a console program (the player, tests: asuka dev
	// reads its output); else a GUI program with no console window.
	Console bool
	Debug   bool
	// Tests are a test program's C sources (platform/windows/test), with
	// the host in place of its main.
	Tests []string
}

// WindowsLibs are the system libraries the Windows host links.
var WindowsLibs = []string{
	"-lgdiplus", "-lcomctl32", "-luxtheme", "-ldwmapi", "-lole32", "-loleaut32", "-luuid", "-lshell32", "-lshlwapi",
	"-luser32", "-lgdi32", "-ladvapi32", "-lwinhttp", "-lws2_32", "-lmsimg32", "-lwindowscodecs", "-limm32", "-lversion",
	"-lcomdlg32", "-lwinmm", "-lcredui", "-ld3d11", "-ldxgi", "-luiautomationcore", "-lpropsys", "-lbcrypt", "-ldbghelp",
}

// WindowsHost builds the Windows host's executable for t at out: the host
// (platform/windows), the C the desktop hosts share (platform/desktop) and
// the core, with the app compiled in (o.App) or none (asuka dev's player).
func WindowsHost(t cbuild.Target, o WindowsOptions, out string) error {
	tc, err := cbuild.ForTarget(t)
	if err != nil {
		return err
	}
	core, err := cbuild.CoreDir()
	if err != nil {
		return err
	}
	win, err := windowsTree.Dir()
	if err != nil {
		return err
	}
	dsk, err := desktopTree.Dir()
	if err != nil {
		return err
	}
	var host []string
	for _, d := range []string{filepath.Join(win, "src"), filepath.Join(dsk, "src")} {
		m, _ := filepath.Glob(filepath.Join(d, "*.c"))
		host = append(host, m...)
	}
	if len(o.Tests) > 0 {
		// The tests' own main: the host's is left out.
		kept := host[:0]
		for _, s := range host {
			if filepath.Base(s) != "main.c" || !strings.HasPrefix(s, filepath.Join(win, "src")) {
				kept = append(kept, s)
			}
		}
		host = append(kept, o.Tests...)
	}
	host = append(host, o.Natives...)
	sort.Strings(host)
	flags := []string{"-I" + filepath.Join(win, "src"), "-I" + filepath.Join(dsk, "include"), "-DUNICODE", "-D_UNICODE"}
	if len(o.Tests) > 0 {
		flags = append(flags, "-I"+filepath.Join(win, "test"), "-DWH_TEST=1")
	}
	p := cbuild.Program{Host: host, HostFlags: flags, Extra: o.App, Debug: o.Debug}
	if o.Resources != "" {
		p.Objects = append(p.Objects, o.Resources)
	}
	if mingw(tc) {
		p.Link = append(p.Link, "-municode")
		if o.Console {
			p.Link = append(p.Link, "-mconsole")
		} else {
			p.Link = append(p.Link, "-mwindows")
		}
	} else if o.Console {
		p.Link = append(p.Link, "-Wl,/subsystem:console")
	} else {
		p.Link = append(p.Link, "-Wl,/subsystem:windows", "-Wl,/entry:wWinMainCRTStartup")
	}
	p.Link = append(p.Link, WindowsLibs...)
	return tc.Program(core, p, out)
}

// mingw reports whether tc builds against MinGW-w64 (Zig's Windows target,
// a MinGW clang or gcc) rather than the Windows SDK (clang for MSVC).
func mingw(tc *cbuild.Toolchain) bool {
	if tc.CC.IsZig() {
		return true
	}
	out, err := cbuildOutput(tc.CC.Path, append(append([]string{}, tc.CC.Args...), "-dumpmachine")...)
	return err == nil && (strings.Contains(out, "mingw") || strings.Contains(out, "gnu"))
}

func cbuildOutput(name string, args ...string) (string, error) {
	out, err := exec.Command(name, args...).Output()
	return strings.TrimSpace(string(out)), err
}
