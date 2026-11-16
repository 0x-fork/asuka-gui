package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

// needMacBuild skips a test that builds a Mac app where it can't: not on
// a Mac, without Xcode's tools, or with -short.
func needMacBuild(t *testing.T) {
	t.Helper()
	if runtime.GOOS != "darwin" {
		t.Skip("a Mac app builds on a Mac")
	}
	if testing.Short() {
		t.Skip("builds an app (not -short)")
	}
	if _, err := exec.LookPath("xcrun"); err != nil {
		t.Skip("no Xcode command line tools")
	}
	if _, err := cbuild.ForTarget(cbuild.Target{OS: "darwin", Arch: runtime.GOARCH}); err != nil {
		t.Skip(err)
	}
}

// An app's native packages' Swift (testdata/nativeapp's: the pantry,
// internal/nativegen's testdata) is built with the host's Swift into the
// app's executable, linked with the core, with Swift's concurrency runtime
// in its Frameworks for macOS 11 when it links it. An app without native
// packages links the host's object every app shares, compiled once.
func TestMacSwift(t *testing.T) {
	needMacBuild(t)
	if _, err := exec.LookPath("xcodebuild"); err != nil {
		t.Skip("no Xcode")
	}
	app := testNativeApp(t)
	tmp := t.TempDir()
	bundle := filepath.Join(tmp, "App.app")
	exe := filepath.Join(bundle, "Contents", "MacOS", "App")
	b := &macBuild{app: app, work: filepath.Join(tmp, "work"), run: macRunner(false)}
	if err := b.executable(exe, []string{runtime.GOARCH}, nil); err != nil {
		t.Fatal(err)
	}
	if err := b.embed(bundle, exe); err != nil {
		t.Fatal(err)
	}
	out, err := exec.Command("nm", exe).Output()
	if err != nil {
		t.Fatal(err)
	}
	s := string(out)
	for _, sym := range []string{"_asuka_start", "_asuka_dev_connect"} {
		if !containsField(s, sym) {
			t.Errorf("the executable hasn't the core's %s", sym)
		}
	}
	if !strings.Contains(s, "AsukaModule__asuka_pantry") {
		t.Error("the executable hasn't the pantry's Swift")
	}
	fw := filepath.Join(bundle, "Contents", "Frameworks")
	if macImports(exe, "@rpath/libswift_Concurrency.dylib") {
		if _, err := os.Stat(filepath.Join(fw, "libswift_Concurrency.dylib")); err != nil {
			t.Error("the executable links Swift concurrency, and its runtime isn't in Frameworks")
		}
	}

	// An app without native packages: the shared host object, no
	// packages' build.
	plainDir := t.TempDir()
	os.WriteFile(filepath.Join(plainDir, "app.asuka"), []byte("app { Text(\"plain\") }\n"), 0o644)
	plain := projectApp(t, plainDir, nil)
	pb := &macBuild{app: plain, work: filepath.Join(tmp, "plain"), run: macRunner(false)}
	objs, pkgs, err := pb.host(runtime.GOARCH, nil)
	if err != nil {
		t.Fatal(err)
	}
	if pkgs != nil || len(objs) != 1 || !strings.Contains(objs[0], filepath.Join("asuka", "mac-host")) {
		t.Errorf("an app without native packages: %v, %v", objs, pkgs)
	}
	again, _, err := pb.host(runtime.GOARCH, nil)
	if err != nil || again[0] != objs[0] {
		t.Errorf("compiled again: %v %v", again, err)
	}
}

func containsField(s, f string) bool {
	for _, line := range strings.Split(s, "\n") {
		if fs := strings.Fields(line); len(fs) > 0 && fs[len(fs)-1] == f {
			return true
		}
	}
	return false
}
