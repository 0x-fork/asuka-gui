package cbuild

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/egoist/asuka/internal/androidsdk"
)

// Target is what the core is built for.
type Target struct {
	// OS: darwin, ios, ios-simulator, android, linux, windows, wasm.
	OS string
	// Arch: arm64, amd64, arm, 386; wasm32 for wasm.
	Arch string
}

func (t Target) String() string { return t.OS + "-" + t.Arch }

// Host is the computer's own target.
func Host() Target {
	return Target{OS: runtime.GOOS, Arch: runtime.GOARCH}
}

// Minimum versions (DESIGN.md: iOS 16, Android API 33, macOS 11).
const (
	IOSMin     = "16.0"
	MacOSMin   = "11.0"
	AndroidAPI = "33"
)

// clangArch is an architecture's name in target triples.
func clangArch(arch string) string {
	switch arch {
	case "amd64":
		return "x86_64"
	case "arm64":
		return "aarch64"
	case "386":
		return "i686"
	case "arm":
		return "armv7a"
	}
	return arch
}

// Toolchain is a compiler and what it needs to build for a target: its
// flags, its archiver, how it links a shared library.
type Toolchain struct {
	Target Target
	CC     Compiler
	Flags  []string // before every compile and link
	// AR makes a static library: the command and its arguments before
	// the output and the objects.
	AR []string
	// Shared links a shared library (or, for wasm, the module).
	Shared []string
}

// ForTarget finds the toolchain for t: the platform's own compiler where
// there is one (Xcode's for Apple platforms, the NDK's for Android, the
// system's for this computer), else Zig's, which cross-compiles to
// anything. ASUKA_CC=zig makes it Zig's always.
func ForTarget(t Target) (*Toolchain, error) {
	forceZig := os.Getenv("ASUKA_CC") == "zig"
	switch t.OS {
	case "ios", "ios-simulator":
		if runtime.GOOS != "darwin" {
			return nil, fmt.Errorf("cbuild: building for iOS needs a Mac with Xcode")
		}
		sdk := "iphoneos"
		triple := "arm64-apple-ios" + IOSMin
		if t.OS == "ios-simulator" {
			sdk = "iphonesimulator"
			triple = clangArchApple(t.Arch) + "-apple-ios" + IOSMin + "-simulator"
		}
		root, err := output("xcrun", "--sdk", sdk, "--show-sdk-path")
		if err != nil {
			return nil, fmt.Errorf("cbuild: Xcode's %s SDK: %v (install Xcode)", sdk, err)
		}
		clang, err := output("xcrun", "--sdk", sdk, "--find", "clang")
		if err != nil {
			return nil, err
		}
		return &Toolchain{
			Target: t,
			CC:     Compiler{Path: clang},
			Flags:  []string{"-target", triple, "-isysroot", root},
			AR:     []string{"xcrun", "libtool", "-static", "-o"},
			Shared: []string{"-dynamiclib"},
		}, nil
	case "android":
		sys := androidsdk.Local()
		ndk, bin, err := sys.NDK(sys.SDK())
		if err != nil {
			return nil, err
		}
		_ = ndk
		clang := sys.Clang(bin)
		triple := clangArch(t.Arch) + "-linux-android"
		if t.Arch == "arm" {
			triple = "armv7a-linux-androideabi"
		}
		return &Toolchain{
			Target: t,
			CC:     Compiler{Path: clang},
			Flags:  []string{"--target=" + triple + AndroidAPI, "-fPIC"},
			AR:     []string{filepath.Join(bin, sys.Exe("llvm-ar")), "rcs"},
			Shared: []string{"-shared", "-Wl,-z,max-page-size=16384", "-llog"},
		}, nil
	case "wasm":
		z, err := Zig()
		if err != nil {
			return nil, fmt.Errorf("cbuild: building for the web needs Zig (ASUKA_CC=zig): %v", err)
		}
		return &Toolchain{
			Target: t,
			CC:     z,
			Flags:  []string{"-target", "wasm32-wasi", "-mbulk-memory"},
			AR:     []string{z.Path, "ar", "rcs"},
			Shared: []string{"-Wl,--no-entry", "-Wl,--export-dynamic", "-mexec-model=reactor"},
		}, nil
	}
	// Desktops: this computer's own compiler for its own system and
	// architectures it runs; Zig to cross-compile.
	if t.OS == runtime.GOOS && !forceZig {
		cc, err := Find()
		if err != nil {
			return nil, err
		}
		tc := &Toolchain{Target: t, CC: cc}
		switch t.OS {
		case "darwin":
			tc.Flags = []string{"-arch", clangArchApple(t.Arch), "-mmacosx-version-min=" + MacOSMin}
			tc.AR = []string{"xcrun", "libtool", "-static", "-o"}
			tc.Shared = []string{"-dynamiclib", "-install_name", "@rpath/libasuka.dylib"}
		case "windows":
			tc.AR = []string{"llvm-ar", "rcs"}
			tc.Shared = []string{"-shared"}
		default:
			tc.Flags = []string{"-fPIC"}
			tc.AR = []string{"ar", "rcs"}
			tc.Shared = []string{"-shared"}
		}
		if tc.CC.IsGCC() {
			tc.Flags = append(tc.Flags, GCCFlags...)
		}
		if t.Arch != runtime.GOARCH && t.OS != "darwin" {
			return nil, fmt.Errorf("cbuild: %s from %s/%s needs Zig (ASUKA_CC=zig)", t, runtime.GOOS, runtime.GOARCH)
		}
		return tc, nil
	}
	z, err := Zig()
	if err != nil {
		return nil, fmt.Errorf("cbuild: cross-compiling for %s needs Zig: %v", t, err)
	}
	triple := map[string]string{"linux": "linux-gnu", "windows": "windows-gnu", "darwin": "macos"}[t.OS]
	if triple == "" {
		return nil, fmt.Errorf("cbuild: no toolchain for %s", t)
	}
	tc := &Toolchain{Target: t, CC: z, Flags: []string{"-target", clangArch(t.Arch) + "-" + triple}, AR: []string{z.Path, "ar", "rcs"}, Shared: []string{"-shared"}}
	if t.OS == "linux" {
		tc.Flags = append(tc.Flags, "-fPIC")
	}
	return tc, nil
}

func clangArchApple(arch string) string {
	if arch == "amd64" {
		return "x86_64"
	}
	return "arm64"
}

func output(name string, args ...string) (string, error) {
	out, err := exec.Command(name, args...).Output()
	if err != nil {
		return "", fmt.Errorf("%s %s: %v", name, strings.Join(args, " "), err)
	}
	return strings.TrimSpace(string(out)), nil
}

// LibKind is what Lib makes.
type LibKind int

const (
	Static LibKind = iota // libasuka.a: linked into an app (iOS)
	Shared                // libasuka.so, .dylib, .dll: loaded by a host (Android, desktops)
	Module                // asuka.wasm: the web's
)

// Lib builds the core (and extra sources: an app compiled to C) for the
// toolchain's target into out, optimized for release unless debug. It
// caches objects under the user's cache: the core's by its sources and
// flags, the extra sources' by theirs, so a new build of an app compiles
// only the app.
func (tc *Toolchain) Lib(core string, kind LibKind, extra []string, debug bool, out string) error {
	return tc.LibWith(core, kind, LibOptions{Extra: extra, Debug: debug}, out)
}

// LibOptions say what Lib builds the core with.
type LibOptions struct {
	Extra   []string // more of the core's C: an app compiled to C
	Foreign *Foreign // the C an app's externs call
	Debug   bool     // a player's build: assertions on, optimized less
}

// LibWith is Lib as o says: with o.Foreign, its glue and units are
// compiled in, and a shared library or a module links what it links.
func (tc *Toolchain) LibWith(core string, kind LibKind, o LibOptions, out string) error {
	extra, debug, ffi := o.Extra, o.Debug, o.Foreign
	srcs, err := Sources(core, false)
	if err != nil {
		return err
	}
	flags := append(append(Includes(core), tc.Flags...), "-ffunction-sections", "-fdata-sections")
	// What the app's own C gets of the core's flags: the target, the
	// optimization, the visibility, the C core's include directory
	// (asuka_ffi.h).
	cflags := append(append([]string{"-I" + filepath.Join(core, "include")}, tc.Flags...), "-ffunction-sections", "-fdata-sections")
	if debug {
		flags = append(flags, "-g", "-O1", "-DAK_DEBUG=1")
		cflags = append(cflags, "-g", "-O1")
	} else {
		flags = append(flags, "-O2", "-DNDEBUG")
		cflags = append(cflags, "-O2", "-DNDEBUG")
	}
	if kind != Static {
		flags = append(flags, "-fvisibility=hidden", "-DAK_EXPORTS=1")
		cflags = append(cflags, "-fvisibility=hidden")
	}
	if ffi != nil {
		// The glue is compiled in: the core registers it (vm/init.c).
		flags = append(flags, "-DAK_FFI=1")
	}
	hdrs, _ := headers(core)
	if len(extra) > 0 {
		// The app is compiled in: the core names it (abi.c), so a static
		// library's linker keeps its object.
		flags = append(flags, "-DAK_APP_COMPILED=1")
	}
	objs, err := tc.compileCached(core, "core", srcs, hdrs, flags, kind)
	if err != nil {
		return err
	}
	if len(extra) > 0 {
		more, err := tc.compileCached(core, "app", extra, hdrs, flags, kind)
		if err != nil {
			return err
		}
		objs = append(objs, more...)
	}
	if ffi != nil {
		more, err := tc.compileCached(core, "ffi", []string{ffi.Glue}, hdrs, flags, kind)
		if err != nil {
			return err
		}
		objs = append(objs, more...)
		more, err = tc.compileForeign(ffi, cflags, kind)
		if err != nil {
			return err
		}
		objs = append(objs, more...)
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return err
	}
	switch kind {
	case Static:
		os.Remove(out)
		args := append(append([]string{}, tc.AR[1:]...), out)
		args = append(args, objs...)
		cmd := exec.Command(tc.AR[0], args...)
		if b, err := cmd.CombinedOutput(); err != nil {
			return fmt.Errorf("%s: %v\n%s", strings.Join(tc.AR, " "), err, b)
		}
		return nil
	default:
		link := append(append([]string{}, tc.Flags...), tc.Shared...)
		if kind == Module {
			link = append(link, exportFlags()...)
		}
		if ffi != nil {
			link = append(link, ffi.Link...)
		}
		link = append(link, "-lm")
		switch tc.Target.OS {
		case "linux":
			link = append(link, "-lpthread", "-ldl") // std/sqlite loads its plugin's library
		case "windows":
			link = append(link, "-lws2_32") // asuka dev's connection (abi/dev.c)
			if tc.CC.IsZig() {
				// lld writes an import library beside the DLL, named for an
				// object; the hosts load the DLL by its path and need none.
				implib := filepath.Join(os.TempDir(), fmt.Sprintf("asuka-%d.lib", os.Getpid()))
				link = append(link, "-Wl,--out-implib,"+implib)
				defer os.Remove(implib)
			}
		}
		if !debug {
			link = append(link, deadStrip(tc.Target)...)
		}
		return tc.CC.Link(objs, out, link)
	}
}

// Objects compiles srcs, C a host links beside the core (the C the
// desktop hosts share, in the Mac's host), with the toolchain's flags and
// flags, into objects cached by what, the target and the hash of the
// sources, hdrs (the headers they include) and the flags.
func (tc *Toolchain) Objects(what string, srcs, hdrs, flags []string) ([]string, error) {
	return tc.compileCached("", what, srcs, hdrs, append(append([]string{}, tc.Flags...), flags...), Static)
}

// compileCached compiles srcs with flags into a cache directory named by
// what (core, app), the target and the hash of the sources, the headers
// they include and the flags.
func (tc *Toolchain) compileCached(core, what string, srcs, hdrs, flags []string, kind LibKind) ([]string, error) {
	h := sha256.New()
	fmt.Fprintln(h, tc.CC.Path, tc.CC.Args, flags, kind)
	for _, f := range append(append([]string{}, srcs...), hdrs...) {
		b, err := os.ReadFile(f)
		if err != nil {
			return nil, err
		}
		fmt.Fprintln(h, f, len(b))
		h.Write(b)
	}
	dir := cacheDir(what + "-" + tc.Target.String() + "-" + hex.EncodeToString(h.Sum(nil))[:16])
	return tc.CC.Compile(srcs, filepath.Join(dir, "obj"), flags)
}

// compileForeign compiles f's units with flags, into a cache directory
// named by the target and the hash of the units' sources, headers and
// flags.
func (tc *Toolchain) compileForeign(f *Foreign, flags []string, kind LibKind) ([]string, error) {
	h := sha256.New()
	fmt.Fprintln(h, tc.CC.Path, tc.CC.Args, flags, kind)
	if err := f.hash(h); err != nil {
		return nil, err
	}
	return f.compileUnits(tc.CC, cacheDir("c-"+tc.Target.String()+"-"+hex.EncodeToString(h.Sum(nil))[:16]), flags)
}

// deadStrip drops what nothing calls, and symbols and debug information
// a release doesn't need.
func deadStrip(t Target) []string {
	switch t.OS {
	case "android":
		// Its symbols stay for Gradle, which strips what it packages and
		// keeps them for crash reports (native-debug-symbols.zip).
		return []string{"-Wl,--gc-sections"}
	case "darwin", "ios", "ios-simulator":
		return []string{"-Wl,-dead_strip", "-Wl,-x"}
	case "windows":
		return []string{"-s"}
	}
	return []string{"-Wl,--gc-sections", "-s"}
}

// exportFlags are the wasm module's exports: the C ABI (asuka.h), and
// allocation for the JavaScript host to pass bytes in.
func exportFlags() []string {
	var f []string
	for _, name := range []string{
		"asuka_start", "asuka_load", "asuka_event", "asuka_list_count", "asuka_list_bind", "asuka_list_unbind",
		"asuka_run_posted", "asuka_attach", "asuka_timer_fire", "asuka_native_done", "asuka_native_invoke", "asuka_alloc", "asuka_free",
		"asuka_run", "asuka_dev_reload", "asuka_free_string", "asuka_web_start", "asuka_io", "asuka_set_app_dir", "asuka_set_start",
		// spawn on the page's workers (core/src/abi/web.c)
		"asuka_web_workers", "asuka_spawn_done", "asuka_worker_start", "asuka_worker_call", "asuka_worker_answered",
		// std/sqlite's answers from the page's SQLite worker (core/src/sqlite/sqlite.c)
		"asuka_sqlite_done",
		// platform/crash's record (core/src/std/crash.c)
		"asuka_crash_start", "asuka_crash_notify", "asuka_crash_where", "asuka_crash_read",
	} {
		f = append(f, "-Wl,--export="+name)
	}
	return f
}

// Exe builds an executable for the toolchain's target into out: the core
// (core/src, without the fake host) and extra C sources (a C desktop
// host, the native registry, packages' C, an app compiled to C),
// optimized for release unless debug. extraFlags are the extra sources'
// own (their include directories, the app's define); link, what the
// executable links with besides the C library; deps, files the extra
// sources include that aren't headers beside them (a vendored library's
// sources, included by a package's own C), which count in the cache's
// key and are never compiled alone. Objects are cached as Lib's are.
func (tc *Toolchain) Exe(core string, extra, deps, extraFlags, link []string, debug bool, out string) error {
	srcs, err := Sources(core, false)
	if err != nil {
		return err
	}
	flags := append(append(Includes(core), tc.Flags...), "-ffunction-sections", "-fdata-sections")
	if debug {
		flags = append(flags, "-g", "-O1", "-DAK_DEBUG=1")
	} else {
		flags = append(flags, "-O2", "-DNDEBUG")
	}
	hdrs, _ := headers(core)
	objs, err := tc.compileCached(core, "core", srcs, hdrs, flags, Static)
	if err != nil {
		return err
	}
	if len(extra) > 0 {
		more := append([]string{}, deps...)
		for _, f := range extra {
			if filepath.Ext(f) == ".h" {
				more = append(more, f) // their headers count in the cache's key
			}
		}
		var cs []string
		for _, f := range extra {
			if filepath.Ext(f) == ".c" {
				cs = append(cs, f)
			}
		}
		eobjs, err := tc.compileCached(core, "exe", cs, append(hdrs, more...), append(append([]string{}, flags...), extraFlags...), Static)
		if err != nil {
			return err
		}
		objs = append(objs, eobjs...)
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return err
	}
	ld := append(append([]string{}, tc.Flags...), "-lm")
	switch tc.Target.OS {
	case "linux":
		ld = append(ld, "-lpthread", "-ldl")
	case "windows":
		ld = append(ld, "-lws2_32")
	}
	ld = append(ld, link...)
	if !debug {
		ld = append(ld, deadStrip(tc.Target)...)
	}
	return tc.CC.Link(objs, out, ld)
}
