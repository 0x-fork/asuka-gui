package appbuild

import (
	"fmt"
	"os"
	"path/filepath"
	"slices"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/internal/nativegen"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/platform/desktop"
	"github.com/egoist/asuka/std"
)

// Units are the C a release of the CLI prebuilds for t (internal/
// prebuilt), each a static library per configuration: the core's and the
// std and sdk packages' own C (their [c]: std/sqlite's SQLite)
// everywhere; on the Mac, the C its host shares with the desktops'; on
// Linux and Windows, the host's, the native registry's and every std and
// sdk package's desktop C.
func Units(t cbuild.Target) ([]cbuild.Unit, error) {
	core, err := cbuild.CoreDir()
	if err != nil {
		return nil, err
	}
	units := []cbuild.Unit{cbuild.CoreUnit(core)}
	for _, d := range StdCDirs() {
		u, _, err := cbuild.StdC(d, t, std.Embedded())
		if err != nil {
			return nil, err
		}
		units = append(units, u)
	}
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

// StdCDirs are the std and sdk packages with C of their own (their
// asuka.toml's [c]: std/sqlite).
func StdCDirs() []string {
	ds, err := std.PackageDirs()
	if err != nil {
		return nil
	}
	var out []string
	for _, d := range ds {
		if cbuild.HasC(d) {
			out = append(out, d)
		}
	}
	return out
}

// SQLiteExports are what the web's SQLite module exports to its worker
// (platform/web/src/sqlite-engine.js): asq's entry point and version, and
// allocation for the worker to pass bytes in.
var SQLiteExports = []string{"asq_call", "asq_version", "malloc", "free"}

// SQLiteWasm is std/sqlite's C for the web (its [c.web]: SQLite with
// asq_wasm.c's system) linked into the WebAssembly module the page's
// SQLite worker runs, asqlite.wasm, which a web build of an app that
// imports std/sqlite carries: from its library (prebuilt, or built into
// the cache), linked once into the cache. debug: a dev build's.
func SQLiteWasm(debug bool) (string, error) {
	dir, ok := std.Package(std.Prefix + "sqlite")
	if !ok {
		return "", fmt.Errorf("appbuild: no std/sqlite")
	}
	t := cbuild.Target{OS: "wasm", Arch: "wasm32"}
	u, _, err := cbuild.StdC(dir, t, std.Embedded())
	if err != nil {
		return "", err
	}
	tc, err := cbuild.ForTarget(t)
	if err != nil {
		return "", err
	}
	return tc.WasmModule(u, SQLiteExports, debug, "asqlite.wasm")
}

// UsesSQLite reports whether c (a module's) imports std/sqlite: its build
// links SQLite (on the web, carries SQLiteWasm).
func UsesSQLite(c *bytecode.C) bool {
	if c == nil {
		return false
	}
	dir, ok := std.Package(std.Prefix + "sqlite")
	return ok && slices.Contains(c.Std, dir)
}
