//go:build darwin && !ios

package darwin

import (
	"testing"

	. "github.com/egoist/asuka/internal/darwin"
	"github.com/egoist/asuka/host/hosttest"
	"github.com/egoist/asuka/ops"
)

// mountApp runs an Asuka app (src) on a new host, on the C core as an
// app runs (host/hosttest), its window closed when the test ends.
func mountApp(t *testing.T, src string) *Host {
	t.Helper()
	var h *Host
	onMain(func() { h = newHost() })
	t.Cleanup(func() { onMain(func() { Send(h.window, "close") }) })
	hosttest.Mount(t, h, func(f func()) { onMain(func() { h.enter(f) }) }, src)
	return h
}

// The Counter on the C core: its views made, a real click through the
// button's target and action, the text set again, laid out in the
// window.
func TestCoreCounter(t *testing.T) {
	h := mountApp(t, `
		component Counter {
		  state count = 0
		  Column {
		    gap: 8; padding: 16
		    Text("Count: {count}")
		    Button("Add") { on press { count += 1 } }
		  }
		}
		app { Counter() }
	`)
	onMain(func() {
		texts, buttons := find(h, ops.KindText), find(h, ops.KindButton)
		if len(texts) != 1 || len(buttons) != 1 {
			fatalf(t, "%d texts, %d buttons", len(texts), len(buttons))
		}
		label, button := texts[0], buttons[0]
		if s := text(label); s != "Count: 0" {
			fatalf(t, "text %q", s)
		}
		h.enter(func() { Send(button.view, "performClick:", 0) })
		if s := text(label); s != "Count: 1" {
			fatalf(t, "after a click: %q", s)
		}
		f := MsgRect(label.view, Sel("frame"))
		if f.Origin.X != 16 || f.Origin.Y != 16 || f.Size.Width <= 0 || f.Size.Height <= 0 {
			fatalf(t, "text frame %+v", f)
		}
	})
}
