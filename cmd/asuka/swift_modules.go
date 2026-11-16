package main

import (
	"errors"
	"fmt"
	"io/fs"
	"maps"
	"os"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"sync"

	"github.com/egoist/asuka/internal/ccache"
)

// Swift modules (DESIGN.md, "The build cache"): an app's Swift with its
// native packages' is compiled by swiftc as modules, each once per
// version of its sources, the compiler, the SDK, the architecture and the
// configuration, and kept in the build cache: the host's (the module
// Asuka, with its compilation conditions) and each native package's (its
// one target, against the host's module). A build of another app, or of
// this one after a change to its own package, compiles only what it
// hasn't, and links the modules' objects. Swift's modules are the
// compiler's own (a module from another swiftc can't be imported), so
// they're built on this computer, not shipped prebuilt. A package
// swiftc can't build alone (one with SwiftPM dependencies of its own,
// resources, settings: std/lottie) is built with the others by
// xcodebuild (swiftBuild.build), as before.

// modules builds b's Swift as modules when every native package is one
// (swiftInfo.Module); ok false when one isn't.
func (b *swiftBuild) modules() (out *swiftPackageOutput, ok bool, err error) {
	infos := make([]swiftInfo, len(b.natives))
	for i, p := range b.natives {
		info, err := swiftPackageInfo(p.dir)
		if err != nil {
			return nil, false, err
		}
		if info.Module == nil {
			return nil, false, nil
		}
		infos[i] = info
	}
	sdk, err := swiftSDK(b.sdkName)
	if err != nil {
		return nil, false, err
	}
	hostMin := iosMin
	if b.platform == "macos" {
		hostMin = macMin
	}
	host := swiftModule{name: "Asuka", sources: b.host, headers: b.cAsuka, defines: b.defines, min: hostMin}
	hostDir, err := b.module(sdk, host, "")
	if err != nil {
		return nil, true, err
	}
	// The packages' modules, each against the host's alone: at once.
	objs := make([]string, len(b.natives))
	errs := make([]error, len(b.natives))
	sem := make(chan struct{}, max(1, runtime.NumCPU()/2))
	var wg sync.WaitGroup
	for i, p := range b.natives {
		wg.Add(1)
		go func(i int, p nativePackage) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			mod := infos[i].Module
			srcs, err := swiftFiles(filepath.Join(p.dir, mod.Path))
			if err != nil {
				errs[i] = fmt.Errorf("%s: %v", p.path, err)
				return
			}
			min := mod.IOS
			if b.platform == "macos" {
				min = mod.MacOS
			}
			if min == "" {
				min = hostMin
			}
			dir, err := b.module(sdk, swiftModule{name: mod.Name, sources: srcs, min: min, swift6: mod.Swift6, pkg: true}, hostDir)
			if err != nil {
				errs[i] = fmt.Errorf("%s: %v", p.path, err)
				return
			}
			objs[i] = filepath.Join(dir, mod.Name+".o")
		}(i, p)
	}
	wg.Wait()
	if err := errors.Join(errs...); err != nil {
		return nil, true, err
	}
	return &swiftPackageOutput{objects: append([]string{filepath.Join(hostDir, "Asuka.o")}, objs...)}, true, nil
}

// swiftModule is a module's sources and how it's compiled.
type swiftModule struct {
	name    string
	sources map[string][]byte // its Swift, by path
	headers map[string][]byte // the host's C module (CAsuka: include/...)
	defines []string          // compilation conditions
	min     string            // its minimum OS version
	swift6  bool              // Swift 6's language mode
	pkg     bool              // a native package's (SwiftPM's conditions)
}

// module compiles m into the cache (once) and returns its directory:
// <name>.swiftmodule and <name>.o, and the host's C module's headers
// (CAsuka/include) for the host's. host is the host module's directory,
// for a package's.
func (b *swiftBuild) module(sdk string, m swiftModule, host string) (string, error) {
	target := b.arch + "-apple-macos" + m.min
	if b.platform == "ios" {
		target = b.arch + "-apple-ios" + m.min
		if b.sdkName == "iphonesimulator" {
			target += "-simulator"
		}
	}
	opt := []string{"-Onone", "-g", "-D", "DEBUG"}
	if b.release {
		opt = []string{"-O", "-g"}
	}
	if m.pkg {
		opt = append(opt, "-D", "SWIFT_PACKAGE")
	}
	lang := "5"
	if m.swift6 {
		lang = "6"
	}
	parts := []string{"swift-module-1", swiftcVersion(), sdk, swiftSDKVersion(b.sdkName), target, lang, m.name, strings.Join(opt, " "), strings.Join(m.defines, " "), host}
	for _, n := range slices.Sorted(maps.Keys(m.sources)) {
		parts = append(parts, n, string(m.sources[n]))
	}
	for _, n := range slices.Sorted(maps.Keys(m.headers)) {
		parts = append(parts, n, string(m.headers[n]))
	}
	dir := ccache.Sharded("swift", "m", ccache.Key(parts...), "")
	if _, err := os.Stat(filepath.Join(dir, m.name+".o")); err == nil {
		ccache.Touch(dir)
		ccache.Hit("swift")
		return dir, nil
	}
	ccache.Miss("swift")
	tmp := ccache.TmpName(dir)
	os.RemoveAll(tmp)
	defer os.RemoveAll(tmp)
	src := filepath.Join(tmp, "src")
	var files []string
	for _, n := range slices.Sorted(maps.Keys(m.sources)) {
		p := filepath.Join(src, filepath.FromSlash(n))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return "", err
		}
		if err := os.WriteFile(p, m.sources[n], 0o644); err != nil {
			return "", err
		}
		files = append(files, p)
	}
	inc := filepath.Join(tmp, "CAsuka")
	for n, data := range m.headers {
		if !strings.HasPrefix(n, "include/") {
			continue // the host's C module is its headers; its sources are the core's
		}
		p := filepath.Join(inc, filepath.FromSlash(n))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return "", err
		}
		if err := os.WriteFile(p, data, 0o644); err != nil {
			return "", err
		}
	}
	what := m.name
	if !m.pkg {
		what = "the host"
	}
	fmt.Printf("asuka: compiling %s's Swift for %s (once)\n", what, target)
	args := []string{"swiftc", "-target", target, "-sdk", sdk, "-swift-version", lang, "-module-name", m.name,
		"-parse-as-library", "-wmo", "-c", "-o", filepath.Join(tmp, m.name+".o"),
		"-emit-module", "-emit-module-path", filepath.Join(tmp, m.name+".swiftmodule")}
	args = append(args, opt...)
	for _, d := range m.defines {
		args = append(args, "-D", d)
	}
	if host != "" {
		args = append(args, "-I", host, "-I", filepath.Join(host, "CAsuka", "include"))
	} else {
		args = append(args, "-I", filepath.Join(inc, "include"))
	}
	if err := b.run(tmp, nil, "xcrun", append(args, files...)...); err != nil {
		return "", err
	}
	os.RemoveAll(src)
	if err := ccache.Commit(tmp, dir); err != nil {
		return "", err
	}
	return dir, nil
}

// swiftFiles are the .swift files under dir, by their paths relative to
// it.
func swiftFiles(dir string) (map[string][]byte, error) {
	out := map[string][]byte{}
	err := filepath.WalkDir(dir, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() || !strings.HasSuffix(p, ".swift") {
			return nil
		}
		b, err := os.ReadFile(p)
		if err != nil {
			return err
		}
		r, _ := filepath.Rel(dir, p)
		out[filepath.ToSlash(r)] = b
		return nil
	})
	if len(out) == 0 && err == nil {
		err = fmt.Errorf("no Swift in %s", dir)
	}
	return out, err
}

var (
	sdkMu       sync.Mutex
	sdkPaths    = map[string]string{}
	sdkVersions = map[string]string{}
)

// swiftSDK is an SDK's path (iphoneos, iphonesimulator, macosx).
func swiftSDK(name string) (string, error) {
	sdkMu.Lock()
	defer sdkMu.Unlock()
	if p, ok := sdkPaths[name]; ok {
		return p, nil
	}
	p, err := output("xcrun", "--sdk", name, "--show-sdk-path")
	if err != nil {
		return "", fmt.Errorf("Xcode's %s SDK: %v (install Xcode)", name, err)
	}
	sdkPaths[name] = p
	return p, nil
}

// swiftSDKVersion is an SDK's version and build.
func swiftSDKVersion(name string) string {
	sdkMu.Lock()
	defer sdkMu.Unlock()
	if v, ok := sdkVersions[name]; ok {
		return v
	}
	v, _ := output("xcrun", "--sdk", name, "--show-sdk-version")
	b, _ := output("xcrun", "--sdk", name, "--show-sdk-build-version")
	sdkVersions[name] = v + " " + b
	return sdkVersions[name]
}
