package main

import (
	"slices"
	"strings"
	"testing"
)

// asuka/location says what it needs itself: its permissions, purpose
// strings and background mode in its asuka.toml, its receiver and
// foreground service in its Gradle module's manifest (the host has none
// of them); temporary full accuracy's purposes are the app's, for an app
// that calls location.requestPrecise.
func TestLocationExtras(t *testing.T) {
	var c appConfig
	c.IOS.LocationPurposes = map[string]string{"route": "To draw your route."}
	importStd(t, &c, "ios", "location")
	c.locationPrecise = true // usedFeatures: the app calls location.requestPrecise
	man := c.androidManifest(false)
	for _, want := range []string{"android.permission.ACCESS_FINE_LOCATION", "android.permission.ACCESS_BACKGROUND_LOCATION", "android.permission.FOREGROUND_SERVICE_LOCATION"} {
		if !strings.Contains(man, want) {
			t.Errorf("no %s in the manifest:\n%s", want, man)
		}
	}
	if strings.Contains(man, "dev.asuka.AsukaLocation") || len(c.androidModules) > 0 {
		t.Errorf("the host's old location service, receiver or module: %v\n%s", c.androidModules, man)
	}
	mod := stdAndroidManifest(t, "location")
	for _, want := range []string{`<service android:name="asuka.location.LocationService" android:exported="false" android:foregroundServiceType="location" />`,
		`<receiver android:name="asuka.location.LocationReceiver"`, "android.intent.action.BOOT_COMPLETED", "android.permission.RECEIVE_BOOT_COMPLETED"} {
		if !strings.Contains(mod, want) {
			t.Errorf("no %s in asuka/location's module's manifest:\n%s", want, mod)
		}
	}
	if c.IOS.Usage["NSLocationWhenInUseUsageDescription"] == "" || c.IOS.Usage["NSLocationAlwaysAndWhenInUseUsageDescription"] == "" {
		t.Errorf("usage %v", c.IOS.Usage)
	}
	x := c.plistExtras()
	if m, _ := x["UIBackgroundModes"].([]string); !slices.Equal(m, []string{"location"}) {
		t.Errorf("background modes %v", m)
	}
	ps, _ := x["NSLocationTemporaryUsageDescriptionDictionary"].(map[string]any)
	if ps["default"] == nil || ps["route"] != "To draw your route." {
		t.Errorf("purposes %v", ps)
	}
	var none appConfig
	importStd(t, &none, "ios")
	if _, ok := none.plistExtras()["NSLocationTemporaryUsageDescriptionDictionary"]; ok || strings.Contains(none.androidManifest(false), "LOCATION") {
		t.Error("an app without asuka/location has its needs")
	}
}
