//go:build e2e

package e2e

import "testing"

// Native packages (asuka native) are built into the app on each platform:
// the greeter's module answers the app (on iOS through its Swift
// package's own dependency, GreeterKit: a resource and C, "kit 42"), the
// kitchen's module answers with a struct, an enum and a list, and the
// greeter's typed view, set as it's made, sends the app an event (on
// Linux the packages' C, a GtkLabel its view).
func nativePackages(t *testing.T, s screen, platform, greeting string) {
	waitFor(t, s, []string{greeting, "kitchen home: salt 1.5g, 2 tags", "shown: " + platform + " badge for Asuka"})
}

func TestNativePackagesIOS(t *testing.T) {
	nativePackages(t, iosApp(t, "e2e/testdata/pluginapp", "dev.asuka.pluginapp"), "iOS", "hello Asuka from iOS, kit 42")
}

func TestNativePackagesAndroid(t *testing.T) {
	nativePackages(t, androidApp(t, "e2e/testdata/pluginapp", "dev.asuka.pluginapp"), "Android", "hello Asuka from Android")
}

func TestNativePackagesWeb(t *testing.T) {
	nativePackages(t, webApp(t, "e2e/testdata/pluginapp"), "web", "hello Asuka from the web")
}

func TestNativePackagesLinux(t *testing.T) {
	nativePackages(t, linuxApp(t, "e2e/testdata/pluginapp"), "Linux", "hello Asuka from Linux")
}
