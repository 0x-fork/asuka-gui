package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"sort"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/toml"
)

// depManifest is what an app takes of a dependency's asuka.toml: what
// the platforms need of an app for the package's code to work (a package
// that reads the camera says why in its manifest), which the app's own
// asuka.toml may change. A package's native code says more in its own
// files, beside it: its Gradle module's AndroidManifest.xml (services,
// receivers, queries), its Swift package's Info.plist and
// iOS.entitlements (nativeNeeds). A package over a host's own module
// (asuka/host), which has neither, says it all here.
type depManifest struct {
	IOS struct {
		Usage           map[string]string `json:"usage"`
		BackgroundModes []string          `json:"backgroundModes"`
		// Entitlements, with a simulator build's values (a release's are
		// its profile's, which must have them).
		Entitlements map[string]any `json:"entitlements"`
		// Plist are Info.plist's keys: true (UIFileSharingEnabled), an
		// array the app's packages' join (LSApplicationQueriesSchemes),
		// or a string.
		Plist map[string]any `json:"plist"`
	} `json:"ios"`
	Android struct {
		// Permissions: "name", "name:28" (up to API level 28), or
		// "name|flags" (usesPermissionFlags).
		Permissions []string `json:"permissions"`
		// Application are elements of the manifest's application (a host
		// module's service or receiver); Manifest, of the manifest itself
		// (queries, uses-feature).
		Application []string `json:"application"`
		Manifest    []string `json:"manifest"`
		// HostModules are the host's optional Gradle modules
		// (platform/android/asuka-<name>) the package's host module needs:
		// asuka/push's Firebase, asuka/periodic's WorkManager.
		HostModules []string `json:"hostModules"`
	} `json:"android"`
}

// depManifests adds to c what the app's dependencies' manifests ask
// for (deps: package ID → directory).
func (c *appConfig) depManifests(deps map[string]string, u *appbuild.Uses) error {
	ids := make([]string, 0, len(deps))
	for id := range deps {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	var errs []error
	for _, id := range ids {
		b, err := os.ReadFile(filepath.Join(deps[id], configFile))
		if err != nil {
			continue
		}
		var m depManifest
		if err := toml.Unmarshal(b, &m); err != nil {
			errs = append(errs, fmt.Errorf("%s's %s: %v", id, configFile, err))
			continue
		}
		if err := c.addManifest(id, &m); err != nil {
			errs = append(errs, fmt.Errorf("%s's %s: %v", id, configFile, err))
		}
		if id == "asuka/maps" {
			c.mapsKey = true
		}
	}
	return errors.Join(errs...)
}

// addManifest adds what package id's manifest m asks for.
func (c *appConfig) addManifest(id string, m *depManifest) error {
	add := func(list *[]string, xs ...string) {
		for _, x := range xs {
			if !slices.Contains(*list, x) {
				*list = append(*list, x)
			}
		}
	}
	for k, v := range m.IOS.Usage {
		if c.IOS.Usage == nil {
			c.IOS.Usage = map[string]string{}
		}
		if c.IOS.Usage[k] == "" {
			c.IOS.Usage[k] = v
		}
	}
	add(&c.IOS.BackgroundModes, m.IOS.BackgroundModes...)
	// iOS's entitlements, by their iOS names: the Mac's signature takes
	// those it knows (macProfileFeatures: aps-environment is the Mac's
	// com.apple.developer.aps-environment), as it does the app's.
	for _, k := range manifestKeys(m.IOS.Entitlements) {
		if c.entitlements == nil {
			c.entitlements = map[string]any{}
		}
		if _, ok := c.entitlements[k]; !ok {
			c.entitlements[k] = manifestValue(m.IOS.Entitlements[k])
		}
	}
	for _, k := range manifestKeys(m.IOS.Plist) {
		switch v := manifestValue(m.IOS.Plist[k]).(type) {
		case bool:
			if !v {
				return fmt.Errorf("ios.plist.%s: false is every key's default", k)
			}
			add(&c.iosTrue, k)
		case []string:
			if c.plistArrays == nil {
				c.plistArrays = map[string][]string{}
			}
			have := c.plistArrays[k]
			add(&have, v...)
			c.plistArrays[k] = have
		case string:
			if c.nativePlist == nil {
				c.nativePlist = map[string]any{}
			}
			if _, ok := c.nativePlist[k]; !ok {
				c.nativePlist[k] = v
			}
		default:
			return fmt.Errorf("ios.plist.%s: true, a string or an array of strings", k)
		}
	}
	for _, p := range m.Android.Permissions {
		c.addPermission(p)
	}
	add(&c.androidApp, m.Android.Application...)
	add(&c.androidTop, m.Android.Manifest...)
	add(&c.androidModules, m.Android.HostModules...)
	return nil
}

// manifestValue is a manifest's value as a plist's: an array's strings
// as []string.
func manifestValue(v any) any {
	xs, ok := v.([]any)
	if !ok {
		return v
	}
	out := make([]string, 0, len(xs))
	for _, x := range xs {
		s, ok := x.(string)
		if !ok {
			return v
		}
		out = append(out, s)
	}
	return out
}

func manifestKeys[V any](m map[string]V) []string {
	ks := make([]string, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	return ks
}
