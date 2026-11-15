//go:build darwin && !ios

package darwin

import (
	"testing"

	. "github.com/egoist/asuka/internal/darwin"
	"github.com/egoist/asuka/ops"
	"github.com/egoist/asuka/signal"
	"github.com/egoist/asuka/ui"
)

// Glass buttons are macOS 26's glass bezel: a capsule, a circle around
// an icon alone, the prominent one tinted as the primary action. A
// button that stops being glass loses its shape and tint.
func TestGlassButtonsDarwin(t *testing.T) {
	variant := signal.New(ui.ButtonGlass)
	h, _ := mount(t, func() ui.Node {
		return ui.Column(ui.Style{},
			ui.Button(ui.ButtonProps{Title: ui.Val("Add"), Variant: ui.ButtonGlassProminent}),
			ui.Button(ui.ButtonProps{Icon: ui.IconPlus, Label: ui.Val("Add"), Variant: ui.ButtonGlass}),
			ui.Dynamic(func() ui.Node { return ui.Button(ui.ButtonProps{Title: ui.Val("Cancel"), Variant: variant.Get()}) }),
		)
	})
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
		post(h, func() { variant.Set(ui.ButtonGray) })
		bs = find(h, ops.KindButton)
		check("no longer glass", bs[2], 1, shapeAutomatic, prominenceAuto)
	})
}
