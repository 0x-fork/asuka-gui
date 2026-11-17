//go:build e2e

package e2e

import (
	"strings"
	"testing"
)

// A Scroll made horizontal can be read by an accessibility service: the
// view that replaced Android's vertical one doesn't take on that view's
// own accessibility delegate (which casts its host to NestedScrollView).
func TestHorizontalScrollAccessibilityAndroid(t *testing.T) {
	s := androidApp(t, "e2e/testdata/hscroll", "dev.asuka.hscroll")
	waitFor(t, s, []string{"Horizontal scroll", "three"})
	out, err := s.adb("shell", "uiautomator", "dump", "/sdcard/asuka-ui.xml")
	if err != nil {
		t.Fatalf("uiautomator dump: %v: %s", err, out)
	}
	if pid, _ := s.adb("shell", "pidof", s.pkg); strings.TrimSpace(pid) == "" {
		log, _ := s.adb("logcat", "-d", "-b", "crash")
		t.Fatalf("the app quit when its views were read:\n%s", log)
	}
	ui, err := s.adb("exec-out", "cat", "/sdcard/asuka-ui.xml")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(ui, `text="three"`) {
		t.Fatalf(`the dump has no text="three": %s`, ui)
	}
}
