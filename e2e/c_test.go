//go:build e2e

package e2e

import (
	"strings"
	"testing"
)

// C from Asuka (RFC 0008, docs/c.md): cmd/asuka/testdata/capp calls its
// own C and the system's zlib (on the web, its own CRC in the module),
// and its C calls back: during the call, and later from another thread,
// posted to the main thread.
func cFromAsuka(t *testing.T, s screen, zlib string) {
	waitFor(t, s, []string{"CRC 2877110229", "Lines", "Later", "Later: 0"})
	found := false
	for _, x := range s.texts() {
		found = found || strings.HasPrefix(x, "zlib ") && (zlib == "" || x == "zlib "+zlib)
	}
	if !found {
		t.Errorf("no zlib %q in %q", zlib, s.texts())
	}
	s.tap("Lines")
	waitFor(t, s, []string{"Line 0: 907060870", "Line 1: 980881731"})
	s.tap("Later")
	waitFor(t, s, []string{"Later: 2877110229"})
}

const capp = "cmd/asuka/testdata/capp"

func TestCFromAsukaIOS(t *testing.T) {
	cFromAsuka(t, iosApp(t, capp, "dev.asuka.capp"), "")
}

func TestCFromAsukaAndroid(t *testing.T) {
	cFromAsuka(t, androidApp(t, capp, "dev.asuka.capp"), "")
}

func TestCFromAsukaWeb(t *testing.T) {
	cFromAsuka(t, webApp(t, capp), "built in")
}
