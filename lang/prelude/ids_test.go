package prelude

import (
	"fmt"
	"os"
	"strings"
	"testing"
)

// The IDs match the C header gen wrote.
func TestIDsMatchHeader(t *testing.T) {
	h, err := os.ReadFile("../../core/src/ui/elements_gen.h")
	if err != nil {
		t.Skip(err)
	}
	src := string(h)
	check := func(c string, id int) {
		t.Helper()
		if !strings.Contains(src, fmt.Sprintf("\t%s = %d,", c, id)) {
			t.Errorf("%s isn't %d in elements_gen.h", c, id)
		}
	}
	check("AK_EL_BUTTON", ElementID("Button"))
	check("AK_KEY_BUTTON_TITLE", KeyID("Button", "title"))
	check("AK_KEY_COLOR", KeyID("Button", "color"))
	check("AK_KEY_TOGGLE_ON", KeyID("Toggle", "on"))
	check("AK_KEY_TEXT_FIELD_TEXT", KeyID("TextField", "text"))
	check("AK_EV_BUTTON_PRESS", EventID("Button", "press"))
	check("AK_EV_TOGGLE_CHANGE", EventID("Toggle", "change"))
}
