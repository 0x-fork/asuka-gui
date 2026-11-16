//go:build darwin

package mactest

import (
	"image/png"
	"os"
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/conformance"
)

// TestConformance draws the conformance sheet in the player's window and
// measures it (package conformance): the window server's image of the
// window's content, as it shows.
func TestConformance(t *testing.T) {
	a := Run(t, conformance.Sheet()+"app { ConformanceSheet() }")
	a.Until(func(tr *Tree) bool {
		for _, n := range tr.All() {
			if n.Shown {
				return true
			}
		}
		return false
	})
	// Images and fonts load; the window settles.
	a.Do("wait 1s")
	shot := filepath.Join(t.TempDir(), "sheet.content.png")
	a.Snap(shot)
	f, err := os.Open(shot)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	conformance.Check(t, "macos", img, "macos")
}
