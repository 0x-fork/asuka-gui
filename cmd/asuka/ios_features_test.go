package main

import (
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"

	iosHost "github.com/egoist/asuka/platform/ios"
)

// Every flag of the iOS host gates code of the host (a flag nothing tests
// is a manifest's entry left behind), and none is a package's with Swift
// of its own: that package brings its code and frameworks to the apps
// that import it, so the host has none for it.
func TestIOSFeaturesGateTheHost(t *testing.T) {
	var src strings.Builder
	fs.WalkDir(iosHost.Files, ".", func(path string, d fs.DirEntry, err error) error {
		if err == nil && !d.IsDir() && strings.HasSuffix(path, ".swift") {
			b, _ := iosHost.Files.ReadFile(path)
			src.Write(b)
		}
		return nil
	})
	host := src.String()
	flags := allFeatures()
	for _, want := range []string{"ASUKA_VIDEO", "ASUKA_NOTIFY", "ASUKA_CONVERSATIONS", "ASUKA_ICLOUD", "ASUKA_CRASH"} {
		if !strings.Contains(strings.Join(flags, " "), want) {
			t.Errorf("no %s in %v", want, flags)
		}
	}
	for _, f := range flags {
		if !strings.Contains(host, f) {
			t.Errorf("%s gates nothing in platform/ios", f)
		}
	}
	ms, _ := filepath.Glob(filepath.Join("..", "..", "std", "*", configFile))
	for _, p := range ms {
		dir := filepath.Dir(p)
		m, err := readManifest(dir)
		if err != nil {
			t.Fatalf("%s: %v", p, err)
		}
		if _, err := os.Stat(filepath.Join(dir, "Package.swift")); err != nil || m == nil {
			continue
		}
		hostFlags := m.IOS.HostFlags
		for _, n := range m.Uses {
			hostFlags = append(hostFlags, n.IOS.HostFlags...)
		}
		if len(hostFlags) > 0 {
			t.Errorf("asuka/%s has Swift of its own, and asks for the host's %v", filepath.Base(dir), hostFlags)
		}
	}
}

// The elements' flags follow what the program refers to of the prelude:
// a Video, not a local name that's the same.
func TestElementFlags(t *testing.T) {
	app := &asukaApp{cfg: &appConfig{}, uses: testUses(map[string]bool{"Video": true, "asuka/location.current": true})}
	if got := strings.Join(usedFeatures(app, app.cfg), " "); got != "ASUKA_VIDEO" {
		t.Errorf("flags %s", got)
	}
	none := &asukaApp{cfg: &appConfig{}, uses: testUses(map[string]bool{"asuka/location.current": true})}
	if got := usedFeatures(none, none.cfg); len(got) > 0 {
		t.Errorf("flags %v", got)
	}
}
