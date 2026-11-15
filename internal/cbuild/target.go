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
// caches objects by their sources and flags under the user's cache.
func (tc *Toolchain) Lib(core string, kind LibKind, extra []string, debug bool, out string) error {
	srcs, err := Sources(core, false)
	if err != nil {
		return err
	}
	srcs = append(srcs, extra...)
	flags := append(append(Includes(core), tc.Flags...), "-ffunction-sections", "-fdata-sections")
	if debug {
		flags = append(flags, "-g", "-O1", "-DAK_DEBUG=1")
	} else {
		flags = append(flags, "-O2", "-DNDEBUG")
	}
	if kind != Static {
		flags = append(flags, "-fvisibility=hidden", "-DAK_EXPORTS=1")
	}
	h := sha256.New()
	fmt.Fprintln(h, tc.CC.Path, tc.CC.Args, flags, kind)
	hdrs, _ := headers(core)
	for _, f := range append(srcs, hdrs...) {
		b, err := os.ReadFile(f)
		if err != nil {
			return err
		}
		fmt.Fprintln(h, f, len(b))
		h.Write(b)
	}
	cache, err := os.UserCacheDir()
	if err != nil {
		cache = os.TempDir()
	}
	dir := filepath.Join(cache, "asuka", "core-"+tc.Target.String()+"-"+hex.EncodeToString(h.Sum(nil))[:16])
	objs, err := tc.CC.Compile(srcs, filepath.Join(dir, "obj"), flags)
	if err != nil {
		return err
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
		link = append(link, "-lm")
		if tc.Target.OS == "linux" {
			link = append(link, "-lpthread")
		}
		if !debug {
			link = append(link, deadStrip(tc.Target)...)
		}
		return tc.CC.Link(objs, out, link)
	}
}

func deadStrip(t Target) []string {
	switch t.OS {
	case "darwin", "ios", "ios-simulator":
		return []string{"-Wl,-dead_strip"}
	case "windows":
		return nil
	}
	return []string{"-Wl,--gc-sections"}
}

// exportFlags are the wasm module's exports: the C ABI (asuka.h), and
// allocation for the JavaScript host to pass bytes in.
func exportFlags() []string {
	var f []string
	for _, name := range []string{
		"asuka_start", "asuka_load", "asuka_event", "asuka_list_count", "asuka_list_bind", "asuka_list_unbind",
		"asuka_run_posted", "asuka_attach", "asuka_timer_fire", "asuka_native_done", "asuka_native_invoke", "asuka_alloc", "asuka_free",
		"asuka_run", "asuka_free_string", "asuka_web_start", "asuka_io",
	} {
		f = append(f, "-Wl,--export="+name)
	}
	return f
}
