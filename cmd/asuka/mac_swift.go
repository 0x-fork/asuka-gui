package main

import (
	"bytes"
	"crypto/sha256"
	"debug/macho"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"maps"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/platform/macos"
)

// A Mac app's executable: the Swift host (platform/macos, AppKit), with the
// app's native packages' Swift (their Package.swift, built with the host's
// as Swift packages: native_swift.go), linked with the C core (libasuka.a:
// the app compiled into it for a release, none in asuka dev's player), for
// each architecture, then one universal binary (lipo). Without native
// packages the host's object is the same for every app, compiled once per
// architecture and kept in the user's cache.

// macHostDefines are the host's compilation conditions on the Mac: what
// the iOS host's files it shares build in (StoreKit's, iCloud's). Every
// app's host has them all.
var macHostDefines = []string{"ASUKA_PURCHASE", "ASUKA_ICLOUD"}

// macSwiftArch is swiftc's name for a Go architecture.
func macSwiftArch(goarch string) string {
	if goarch == "amd64" {
		return "x86_64"
	}
	return goarch
}

// macBuild builds a Mac app's executable.
type macBuild struct {
	app     *asukaApp
	work    string // the build's work directory
	release bool   // optimized, dead code stripped
	run     func(dir string, env []string, cmd string, args ...string) error
	// packages is what the app's Swift packages bring besides objects, as
	// the first architecture's build made it (nil without any): their
	// frameworks and resource bundles, which go in the bundle.
	packages *swiftPackageOutput
}

// executable builds the app's executable at exe for archs (Go's names):
// the host and the core (with m compiled in; nil, the player's) for each,
// one universal binary of them.
func (b *macBuild) executable(exe string, archs []string, m *bytecode.Module) error {
	natives, err := nativeFor(b.app, "macos")
	if err != nil {
		return err
	}
	var parts []string
	for _, arch := range archs {
		dir := filepath.Join(b.work, "mac-"+arch)
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
		// The core: a release's with the app compiled in, the player's
		// without, both with the app's C (asuka.toml's [c], its externs').
		lib := filepath.Join(dir, "libasuka.a")
		target := cbuild.Target{OS: "darwin", Arch: arch}
		if m != nil {
			err = appbuild.Lib(target, cbuild.Static, m, lib)
		} else {
			err = appbuild.PlayerLib(target, cbuild.Static, b.app.c, lib)
		}
		if err != nil {
			return err
		}
		objs, pkgs, err := b.host(arch, natives)
		if err != nil {
			return err
		}
		if b.packages == nil {
			b.packages = pkgs // a Mac framework is universal
		}
		out := filepath.Join(dir, "app")
		args := []string{"swiftc", "-target", macSwiftArch(arch) + "-apple-macos" + macMin, "-sdk", macSDK()}
		args = append(args, objs...)
		args = append(args, lib, "-o", out, "-lresolv",
			// The Swift runtime: the system's, else (macOS 11, for Swift
			// concurrency) the copy in Frameworks; packages' frameworks there.
			"-Xlinker", "-rpath", "-Xlinker", "/usr/lib/swift",
			"-Xlinker", "-rpath", "-Xlinker", "@executable_path/../Frameworks")
		if b.release {
			args = append(args, "-Xlinker", "-dead_strip")
		}
		args = append(args, pkgs.linkFlags()...)
		if err := b.run(b.work, nil, "xcrun", args...); err != nil {
			return err
		}
		parts = append(parts, out)
	}
	if err := os.MkdirAll(filepath.Dir(exe), 0o755); err != nil {
		return err
	}
	if len(parts) == 1 {
		return copyFile(parts[0], exe)
	}
	os.Remove(exe)
	return b.run(b.work, nil, "lipo", append([]string{"-create", "-output", exe}, parts...)...)
}

// host compiles the host's Swift for arch: one object, cached by its
// sources, or with native packages the objects of their packages' build,
// and what else that build made.
func (b *macBuild) host(arch string, natives []nativePackage) ([]string, *swiftPackageOutput, error) {
	sources, err := swiftSources(macos.Files, "Sources/AsukaHost", "Sources/AsukaOps", "App")
	if err != nil {
		return nil, nil, err
	}
	if len(natives) > 0 {
		cAsuka, err := filesUnder(macos.Files, "Sources/CAsuka")
		if err != nil {
			return nil, nil, err
		}
		// The core's stand-ins are the host's package's tests', never an
		// app's: weak, they'd keep the core's archive from being linked.
		delete(cAsuka, "stubs.c")
		// SwiftPM makes a target with a main.swift an executable, which a
		// library product can't have: the entry point (@main) goes in
		// the host's library under another name.
		if m, ok := sources["main.swift"]; ok {
			delete(sources, "main.swift")
			sources["AsukaMain.swift"] = m
		}
		sb := &swiftBuild{dir: filepath.Join(b.work, "swift-"+arch), platform: "macos", sdkName: "macosx",
			arch: macSwiftArch(arch), release: b.release, defines: macHostDefines, host: sources, cAsuka: cAsuka, natives: natives, run: b.run}
		out, err := sb.build()
		if err != nil {
			return nil, nil, err
		}
		return out.objects, out, nil
	}
	return b.hostObject(arch, sources)
}

// hostObject compiles the host alone (no native packages) into one object
// shared by every app, in the user's cache.
func (b *macBuild) hostObject(arch string, sources map[string][]byte) ([]string, *swiftPackageOutput, error) {
	opt := []string{"-Onone", "-g"}
	if b.release {
		opt = []string{"-O"}
	}
	target := macSwiftArch(arch) + "-apple-macos" + macMin
	sdk := macSDK()
	h := sha256.New()
	fmt.Fprintln(h, sdk, target, opt, macHostDefines, swiftcVersion())
	names := slices.Sorted(maps.Keys(sources))
	for _, n := range names {
		fmt.Fprintln(h, n, len(sources[n]))
		h.Write(sources[n])
	}
	header, err := macos.Files.ReadFile("Sources/CAsuka/include/asuka.h")
	if err != nil {
		return nil, nil, err
	}
	h.Write(header)
	base, err := os.UserCacheDir()
	if err != nil {
		return nil, nil, err
	}
	shared := filepath.Join(base, "asuka", "mac-host") // shared by all apps
	unlock, err := lockShared(shared)
	if err != nil {
		return nil, nil, err
	}
	defer unlock()
	kind := "debug"
	if b.release {
		kind = "release"
	}
	obj := filepath.Join(shared, hex.EncodeToString(h.Sum(nil))[:16]+"-"+arch+"."+kind+".o")
	if _, err := os.Stat(obj); err == nil {
		return []string{obj}, nil, nil
	}
	// The objects of hosts before this one go: one per architecture and
	// kind of build is kept.
	old, _ := filepath.Glob(filepath.Join(shared, "*-"+arch+"."+kind+".o"))
	for _, o := range old {
		os.Remove(o)
	}
	src := filepath.Join(shared, "src")
	os.RemoveAll(src)
	inc := filepath.Join(src, "CAsuka")
	if err := os.MkdirAll(inc, 0o755); err != nil {
		return nil, nil, err
	}
	for _, f := range []string{"asuka.h", "module.modulemap"} {
		data, err := macos.Files.ReadFile("Sources/CAsuka/include/" + f)
		if err != nil {
			return nil, nil, err
		}
		if err := os.WriteFile(filepath.Join(inc, f), data, 0o644); err != nil {
			return nil, nil, err
		}
	}
	var files []string
	for _, n := range names {
		p := filepath.Join(src, n)
		if err := os.WriteFile(p, sources[n], 0o644); err != nil {
			return nil, nil, err
		}
		files = append(files, p)
	}
	fmt.Printf("asuka: compiling the Mac host for %s (once)\n", arch)
	args := append([]string{"swiftc", "-target", target, "-sdk", sdk, "-swift-version", "5",
		"-module-name", "AsukaApp", "-parse-as-library", "-wmo"}, opt...)
	for _, d := range macHostDefines {
		args = append(args, "-D", d)
	}
	args = append(args, "-I", inc, "-c", "-o", obj)
	if err := b.run(shared, nil, "xcrun", append(args, files...)...); err != nil {
		return nil, nil, err
	}
	os.RemoveAll(src)
	return []string{obj}, nil, nil
}

// embed puts what the app's Swift packages bring in the bundle (their
// frameworks in Frameworks, their resource bundles in Resources), and
// Swift's concurrency runtime for macOS 11 when the executable links it.
func (b *macBuild) embed(bundle, exe string) error {
	fw := filepath.Join(bundle, "Contents", "Frameworks")
	if b.packages != nil {
		if err := os.MkdirAll(fw, 0o755); err != nil {
			return err
		}
		if err := b.packages.embed(fw, filepath.Join(bundle, "Contents", "Resources")); err != nil {
			return err
		}
	}
	return macSwiftBackDeploy(exe, fw)
}

var (
	macSDKPath   string
	swiftcVer    string
	macSDKLoaded bool
)

// macSDK is the macOS SDK's path.
func macSDK() string {
	if !macSDKLoaded {
		macSDKLoaded = true
		macSDKPath, _ = output("xcrun", "--sdk", "macosx", "--show-sdk-path")
		swiftcVer, _ = output("xcrun", "swiftc", "--version")
	}
	return macSDKPath
}

func swiftcVersion() string {
	macSDK()
	return swiftcVer
}

// macSwiftBackDeploy copies Swift's concurrency runtime into Frameworks
// when the executable links it: macOS 12 and later have it, macOS 11 (the
// apps' minimum) doesn't, so the executable finds this copy there (its
// rpath), as Xcode's builds do.
func macSwiftBackDeploy(exe, fw string) error {
	const name = "libswift_Concurrency.dylib"
	dst := filepath.Join(fw, name)
	if !macImports(exe, "@rpath/"+name) {
		os.Remove(dst)
		return nil
	}
	out, err := exec.Command("xcrun", "--find", "swiftc").Output()
	if err != nil {
		return fmt.Errorf("asuka: finding swiftc: %v", err)
	}
	src := filepath.Join(filepath.Dir(strings.TrimSpace(string(out))), "..", "lib", "swift-5.5", "macosx", name)
	if _, err := os.Stat(src); err != nil {
		return fmt.Errorf("asuka: the app's Swift uses Swift concurrency, which macOS 11 doesn't have, and this toolchain has no copy to ship (%s)", src)
	}
	if err := os.MkdirAll(fw, 0o755); err != nil {
		return err
	}
	return copyFile(src, dst)
}

// macImports reports whether a Mach-O file (thin or universal) links a
// library.
func macImports(path, lib string) bool {
	var files []*macho.File
	if fat, err := macho.OpenFat(path); err == nil {
		defer fat.Close()
		for _, a := range fat.Arches {
			files = append(files, a.File)
		}
	} else if f, err := macho.Open(path); err == nil {
		defer f.Close()
		files = append(files, f)
	}
	for _, f := range files {
		for _, l := range f.Loads {
			if machoDylib(l.Raw(), f.ByteOrder) == lib {
				return true
			}
		}
	}
	return false
}

// machoDylib is the library a load command links (LC_LOAD_DYLIB,
// LC_LOAD_WEAK_DYLIB: debug/macho's ImportedLibraries leaves the weak
// ones out, and Swift links its concurrency runtime weakly), or "".
func machoDylib(raw []byte, bo binary.ByteOrder) string {
	const loadDylib, loadWeakDylib = 0xc, 0x80000018
	if len(raw) < 12 {
		return ""
	}
	if cmd := bo.Uint32(raw); cmd != loadDylib && cmd != loadWeakDylib {
		return ""
	}
	off := int(bo.Uint32(raw[8:]))
	if off >= len(raw) {
		return ""
	}
	name := raw[off:]
	if i := bytes.IndexByte(name, 0); i >= 0 {
		name = name[:i]
	}
	return string(name)
}

// copyFile copies an executable or a library (executable, as the linker
// makes them).
func copyFile(src, dst string) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	os.Remove(dst) // a signed file is replaced, not written over
	return os.WriteFile(dst, data, 0o755)
}
