//go:build e2e

package e2e

import (
	"regexp"
	"strconv"
	"testing"
)

// A field being edited that goes away takes the keyboard with it, though a
// WebView on the screen could take the focus it leaves (as on iOS).
func TestRemovedFieldKeyboardAndroid(t *testing.T) {
	s := androidApp(t, "e2e/testdata/removed-field", "dev.asuka.removedfield")
	waitFor(t, s, []string{"Done"})
	l, top, r, bottom := s.fieldBounds()
	if _, err := s.adb("shell", "input", "tap", strconv.Itoa((l+r)/2), strconv.Itoa((top+bottom)/2)); err != nil {
		t.Fatal(err)
	}
	until(t, func() bool { return s.keyboardShown() }, func() string { return "no keyboard for the field" })
	s.tap("Done")
	waitFor(t, s, []string{"saved"})
	var served string
	until(t, func() bool {
		shown := s.keyboardShown()
		out, _ := s.adb("shell", "dumpsys", "input_method")
		served = regexp.MustCompile(`mServedView=\S+`).FindString(out)
		return !shown
	}, func() string { return "the keyboard is still up after the field went away (" + served + ")" })
}

// keyboardShown: the input method says its window shows.
func (s *androidScreen) keyboardShown() bool {
	out, _ := s.adb("shell", "dumpsys", "input_method")
	return regexp.MustCompile(`mInputShown=true`).MatchString(out)
}
