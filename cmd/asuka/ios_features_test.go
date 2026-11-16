package main

import (
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	iosHost "github.com/egoist/asuka/platform/ios"
)

// Every feature flag gates code of the iOS host (a flag nothing tests is
// a table entry left behind), and none is for a package with Swift of its
// own: that package brings its code and frameworks to the apps that
// import it, so the host has none for it.
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
	flags := append(allFeatures(), "ASUKA_CONVERSATIONS", "ASUKA_LIVEACTIVITY")
	for k := range featureExtras {
		flags = append(flags, k)
	}
	for _, f := range flags {
		if !strings.Contains(host, f) {
			t.Errorf("%s gates nothing in platform/ios", f)
		}
	}
	for _, f := range iosFeatures {
		pkg, _ := splitSymbol(f.symbol)
		name, ok := strings.CutPrefix(pkg, sdkPrefix)
		if !ok {
			continue // the prelude's elements
		}
		if _, err := os.Stat(filepath.Join("..", "..", "std", name, "Package.swift")); err == nil {
			t.Errorf("%s (%s): asuka/%s has Swift of its own", f.flag, f.symbol, name)
		}
	}
}

// usedFeatures notes what an app's calls need of Info.plist without a
// flag: temporary full accuracy's purposes, for location.requestPrecise.
func TestUsedFeaturesLocationPrecise(t *testing.T) {
	app := &asukaApp{cfg: &appConfig{}, uses: &appbuild.Uses{
		Imports: map[string]bool{"asuka/location": true}, Names: map[string]bool{"requestPrecise": true}}}
	if fl := usedFeatures(app, app.cfg); !app.cfg.locationPrecise || slices.Contains(fl, "ASUKA_LOCATION_PRECISE") {
		t.Fatalf("precise %v, flags %v", app.cfg.locationPrecise, fl)
	}
	none := &asukaApp{cfg: &appConfig{}, uses: &appbuild.Uses{Imports: map[string]bool{"asuka/location": true}, Names: map[string]bool{"current": true}}}
	if usedFeatures(none, none.cfg); none.cfg.locationPrecise {
		t.Fatal("precise for an app that doesn't ask")
	}
}
