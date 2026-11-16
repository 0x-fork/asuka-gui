//go:build darwin && !ios

package darwin

import (
	"testing"

	. "github.com/egoist/asuka/internal/darwin"
	"github.com/egoist/asuka/ops"
)

// Glass buttons are macOS 26's glass bezel: a capsule, a circle around
// an icon alone, the prominent one tinted as the primary action. A
// button that stops being glass loses its shape and tint.
func TestGlassButtonsDarwin(t *testing.T) {
	h := mountApp(t, `
		component Buttons {
		  state glass = true
		  Column {
		    Button("Add") { variant: .glassProminent }
		    Button("") { icon: "plus"; label: "Add"; variant: .glass }
		    Button("Cancel") { variant: if glass { .glass } else { .gray } }
		    Button("Plain") { on press { glass = false } }
		  }
		}
		app { Buttons() }
	`)
	onMain(func() {
		bs := find(h, ops.KindButton)
		if !hasGlass(bs[0].view) {
			t.Skip("before macOS 26: glass is the push and default buttons")
		}
		check := func(what string, b *node, bezel, shape, prominence int) {
			t.Helper()
			v := b.view
			if got := []int{SendInt(v, "bezelStyle"), SendInt(v, "borderShape"), SendInt(v, "tintProminence")}; got[0] != bezel || got[1] != shape || got[2] != prominence {
				fatalf(t, "%s: bezel, shape, prominence %v, want %v", what, got, []int{bezel, shape, prominence})
			}
		}
		check("prominent", bs[0], bezelGlass, shapeCapsule, prominencePrimary)
		check("an icon alone", bs[1], bezelGlass, shapeCircle, prominenceAuto)
		check("glass", bs[2], bezelGlass, shapeCapsule, prominenceAuto)
		h.enter(func() { Send(button(h, "Plain").view, "performClick:", 0) })
		bs = find(h, ops.KindButton)
		check("no longer glass", bs[2], 1, shapeAutomatic, prominenceAuto)
	})
}
