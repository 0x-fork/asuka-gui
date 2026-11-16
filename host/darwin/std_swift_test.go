//go:build darwin && !ios

package darwin

import (
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/nativegen"
)

// iosHost is what a package's Swift sees of the iOS host (platform/ios's
// AsukaHost, Media.swift) besides NativeModules.swift: enough to
// typecheck it for the iOS simulator without building the host.
const iosHost = `import UIKit

public enum AsukaNativeValue {
    case bool(Bool), int(Int64), float(Double), string(String), bytes(Data)
    public var bool: Bool { if case .bool(let v) = self { return v }; return false }
    public var int: Int64 { if case .int(let v) = self { return v }; return 0 }
    public var float: Double { if case .float(let v) = self { return v }; return 0 }
    public var string: String { if case .string(let v) = self { return v }; return "" }
    public var bytes: Data { if case .bytes(let v) = self { return v }; return Data() }
}

@MainActor public protocol AsukaNativeView: AnyObject {
    var view: UIView { get }
    func set(_ name: String, _ value: AsukaNativeValue)
}

@MainActor public final class AsukaNativeContext {
    public let id: UInt32
    init(id: UInt32) { self.id = id }
    public func emit(_ name: String, _ values: [String: AsukaNativeValue] = [:]) {}
}
`

// Every standard library package's Swift (std/<name>/swift) builds for
// macOS, as an app's library has it (with module Asuka: platform/macos's
// runtime), and typechecks for the iOS simulator. A package whose
// Package.swift depends on more than Asuka needs SwiftPM to resolve it,
// and is skipped.
func TestStdSwift(t *testing.T) {
	if _, err := exec.LookPath("xcrun"); err != nil {
		t.Skip("no Xcode command line tools")
	}
	dir := t.TempDir()
	arch := map[string]string{"arm64": "arm64", "amd64": "x86_64"}[runtime.GOARCH]
	swiftc := func(t *testing.T, args ...string) {
		t.Helper()
		out, err := exec.Command("xcrun", append([]string{"swiftc", "-swift-version", "5", "-parse-as-library", "-Onone"}, args...)...).CombinedOutput()
		if err != nil {
			t.Fatalf("swiftc: %v\n%s", err, out)
		}
	}
	src := "../../platform/macos/Sources/AsukaMac/"
	mac := filepath.Join(dir, "mac")
	os.MkdirAll(mac, 0o755)
	swiftc(t, "-target", arch+"-apple-macos11", "-module-name", "Asuka", "-emit-module", "-emit-module-path", filepath.Join(mac, "Asuka.swiftmodule"), "-wmo", "-c",
		src+"AsukaMacRuntime.swift", src+"NativeModules.swift", "-o", filepath.Join(mac, "Asuka.o"))
	sim, _ := exec.Command("xcrun", "--sdk", "iphonesimulator", "--show-sdk-path").Output()
	simSDK := strings.TrimSpace(string(sim))
	ios := filepath.Join(dir, "ios")
	if simSDK != "" {
		os.MkdirAll(ios, 0o755)
		shim := filepath.Join(ios, "Host.swift")
		os.WriteFile(shim, []byte(iosHost), 0o644)
		swiftc(t, "-sdk", simSDK, "-target", arch+"-apple-ios16.0-simulator", "-module-name", "Asuka", "-emit-module", "-emit-module-path", filepath.Join(ios, "Asuka.swiftmodule"),
			"-wmo", shim, "../../platform/ios/Sources/AsukaHost/NativeModules.swift")
	}
	pkgs, _ := filepath.Glob("../../std/*/Package.swift")
	for _, ps := range pkgs {
		pkg := filepath.Dir(ps)
		name := filepath.Base(pkg)
		t.Run(name, func(t *testing.T) {
			manifest, _ := os.ReadFile(ps)
			if strings.Count(string(manifest), ".package(") > 1 {
				t.Skip("depends on more than Asuka: SwiftPM resolves it")
			}
			p, err := nativegen.LoadAsuka(pkg)
			if err != nil {
				t.Fatal(err)
			}
			if stale, err := nativegen.Stale(p); err != nil || len(stale) > 0 {
				t.Fatalf("stale generated files (asuka native generate %s): %v %v", pkg, stale, err)
			}
			srcs, _ := filepath.Glob(filepath.Join(pkg, nativegen.SwiftDir, "*.swift"))
			mod := strings.ToUpper(name[:1]) + name[1:] + "Native"
			// The macOS the package says it needs.
			macos := "11"
			if m := regexp.MustCompile(`\.macOS\("([0-9.]+)"\)`).FindStringSubmatch(string(manifest)); m != nil {
				macos = m[1]
			}
			swiftc(t, append([]string{"-target", arch + "-apple-macos" + macos, "-module-name", mod, "-I", mac, "-wmo", "-c", "-o", filepath.Join(dir, name+".o")}, srcs...)...)
			if simSDK != "" {
				swiftc(t, append([]string{"-sdk", simSDK, "-target", arch + "-apple-ios16.0-simulator", "-module-name", mod, "-I", ios, "-typecheck"}, srcs...)...)
			}
		})
	}
}
