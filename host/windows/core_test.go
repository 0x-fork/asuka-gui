//go:build windows

package windows

import (
	"testing"

	"github.com/egoist/asuka/host/hosttest"
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/ops"
)

// mountApp runs an Asuka app (src) on a new host, on the C core as an
// app runs (host/hosttest); its windows go when the test ends.
func mountApp(t *testing.T, src string) *Host {
	t.Helper()
	var h *Host
	onMain(t, func() { h = newHost() })
	t.Cleanup(func() {
		onMain(t, func() {
			// What the host deferred (WebView2 controllers it closes on a
			// later turn) runs before its windows go.
			h.runPosted()
			DestroyWindow(h.window)
			DestroyWindow(h.park)
			DestroyWindow(h.msgWin)
		})
	})
	hosttest.Mount(t, h, func(f func()) { onMain(t, func() { h.enter(f) }) }, src)
	return h
}

// The Counter on the C core: its windows made, a real click (BM_CLICK,
// through BN_CLICKED to its parent), the text set again, laid out in the
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
	onMain(t, func() {
		texts, buttons := find(h, ops.KindText), find(h, ops.KindButton)
		if len(texts) != 1 || len(buttons) != 1 {
			fatalf("%d texts, %d buttons", len(texts), len(buttons))
		}
		label, button := texts[0], buttons[0]
		if label.text != "Count: 0" {
			fatalf("text %q", label.text)
		}
		h.enter(func() { SendMessage(button.ctl, BM_CLICK, 0, 0) })
		if label.text != "Count: 1" {
			fatalf("after a click: %q", label.text)
		}
		f := frame(h, label)
		if f[0] != 16 || f[1] != 16 || f[2] <= 0 || f[3] <= 0 {
			fatalf("text frame %v", f)
		}
		if WindowText(button.ctl) != "Add" {
			fatalf("the button's title is %q", WindowText(button.ctl))
		}
	})
}
