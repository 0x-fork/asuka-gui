package cbuild

import (
	"os"
	"path/filepath"
)

// ProgramOptions say what ProgramExe builds a program's executable of
// (docs/programs.md: a package whose fn main is its entry, with no UI).
type ProgramOptions struct {
	// App is the program compiled to C (lang/cgen: its main()).
	App string
	// Host are a headless host's C sources (platform/desktop/program:
	// the main loop and asuka_desktop.h's host half, with the native
	// registry and packages' C), compiled with HostFlags; Deps are files
	// they include that aren't headers beside them.
	Host, HostFlags, Deps []string
	// Foreign is the C the program's externs call.
	Foreign *Foreign
	// Link are libraries and flags the link takes besides the C library.
	Link []string
	// Static links the C library in (Linux's musl): nothing to install
	// beside the executable.
	Static bool
	Debug  bool
}

// ProgramExe builds a program's executable for the toolchain's target at
// out: the core (without the fake host), the program and its host, the
// core's objects cached as Lib's are.
func (tc *Toolchain) ProgramExe(core string, o ProgramOptions, out string) error {
	srcs, err := Sources(core, false)
	if err != nil {
		return err
	}
	flags := append(append(Includes(core), tc.Flags...), "-ffunction-sections", "-fdata-sections", "-DAK_APP_COMPILED=1")
	cflags := append(append([]string{"-I" + filepath.Join(core, "include")}, tc.Flags...), "-ffunction-sections", "-fdata-sections")
	if o.Debug {
		flags = append(flags, "-g", "-O1", "-DAK_DEBUG=1")
		cflags = append(cflags, "-g", "-O1")
	} else {
		flags = append(flags, "-O2", "-DNDEBUG")
		cflags = append(cflags, "-O2", "-DNDEBUG")
	}
	if o.Foreign != nil {
		flags = append(flags, "-DAK_FFI=1")
	}
	hdrs, _ := headers(core)
	objs, err := tc.compileCached(core, "core", srcs, hdrs, flags, Static)
	if err != nil {
		return err
	}
	more, err := tc.compileCached(core, "app", []string{o.App}, hdrs, flags, Static)
	if err != nil {
		return err
	}
	objs = append(objs, more...)
	if o.Foreign != nil {
		more, err := tc.compileCached(core, "ffi", []string{o.Foreign.Glue}, hdrs, flags, Static)
		if err != nil {
			return err
		}
		objs = append(objs, more...)
		if more, err = tc.compileForeign(o.Foreign, cflags, Static); err != nil {
			return err
		}
		objs = append(objs, more...)
		o.Link = append(o.Link, o.Foreign.Link...)
	}
	if len(o.Host) > 0 {
		var cs, hh []string
		hh = append(hh, hdrs...)
		hh = append(hh, o.Deps...)
		for _, f := range o.Host {
			if filepath.Ext(f) == ".c" {
				cs = append(cs, f)
			} else {
				hh = append(hh, f)
			}
		}
		more, err := tc.compileCached(core, "host", cs, hh, append(append([]string{}, flags...), o.HostFlags...), Static)
		if err != nil {
			return err
		}
		objs = append(objs, more...)
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return err
	}
	ld := append([]string{}, tc.Flags...)
	if o.Static {
		ld = append(ld, "-static")
	}
	ld = append(ld, "-lm")
	switch tc.Target.OS {
	case "linux":
		ld = append(ld, "-lpthread", "-ldl")
	case "windows":
		// The console's program (no -mwindows); the core's sockets
		// (abi/dev.c), its arguments in UTF-16, its randomness, its crash
		// reports' stacks.
		ld = append(ld, "-lws2_32", "-lshell32", "-lbcrypt", "-ladvapi32", "-ldbghelp", "-lole32")
	}
	ld = append(ld, o.Link...)
	if !o.Debug {
		ld = append(ld, deadStrip(tc.Target)...)
	}
	tmp := tmpName(out)
	if err := tc.CC.Link(objs, tmp, ld); err != nil {
		os.Remove(tmp)
		return err
	}
	os.Remove(tmp[:len(tmp)-len(filepath.Ext(tmp))] + ".pdb")
	return os.Rename(tmp, out)
}

// Musl is the toolchain for a Linux program linked statically (musl, with
// Zig): an executable that runs on any Linux of its architecture.
func Musl(arch string) (*Toolchain, error) {
	z, err := Zig()
	if err != nil {
		return nil, err
	}
	return &Toolchain{Target: Target{OS: "linux", Arch: arch}, CC: z, Flags: []string{"-target", clangArch(arch) + "-linux-musl"},
		AR: []string{z.Path, "ar", "rcs"}}, nil
}
