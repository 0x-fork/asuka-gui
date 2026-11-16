//go:build darwin && !ios

package darwin

import (
	"testing"

	"github.com/egoist/asuka/host/hosttest"
	. "github.com/egoist/asuka/internal/darwin"
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
func TestCounter(t *testing.T) {
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

func TestTextWraps(t *testing.T) {
	h := mountApp(t, `
		app {
		  Column {
		    width: 120
		    Text("The quick brown fox jumps over the lazy dog, again and again, until the line has to wrap.")
		  }
		}
	`)
	onMain(func() {
		f := MsgRect(find(h, ops.KindText)[0].view, Sel("frame"))
		if f.Size.Width > 120 || f.Size.Height < 40 {
			fatalf(t, "a long text in 120pt: %+v", f.Size)
		}
	})
}

// What the user types reaches the app's state, return submits, and the
// app's write reaches the field.
func TestTextField(t *testing.T) {
	h := mountApp(t, `
		component Form {
		  state value = ""
		  state submitted = 0
		  Column {
		    TextField { bind text: value; on submit { submitted += 1 } }
		    Text("{value} {submitted}")
		    Button("Set") { on press { value = "set" } }
		  }
		}
		app { Form() }
	`)
	onMain(func() {
		f := find(h, ops.KindTextField)[0]
		label := find(h, ops.KindText)[0]
		h.enter(func() {
			Send(f.view, "setStringValue:", uintptr(String("hello")))
			h.textChanged(f.view)
		})
		if s := text(label); s != "hello 0" {
			fatalf(t, "after typing, the app has %q", s)
		}
		h.enter(func() { h.textCommand(f.view, 0, Sel("insertNewline:")) })
		if s := text(label); s != "hello 1" {
			fatalf(t, "after return, the app has %q", s)
		}
		h.enter(func() { Send(find(h, ops.KindButton)[0].view, "performClick:", 0) })
		if s := text(f); s != "set" {
			fatalf(t, "the field shows %q", s)
		}
	})
}

func TestListBindsOnlyWhatShows(t *testing.T) {
	h := mountApp(t, `
		app {
		  List {
		    for i in 0..<10000 key i {
		      View { height: 40; Text("Row {i}") }
		    }
		  }
		}
	`)
	onMain(func() {
		l := find(h, ops.KindList)[0].list
		if len(l.shown) == 0 || len(l.shown) > 60 {
			fatalf(t, "%d rows shown of 10,000", len(l.shown))
		}
		if total := l.tops[len(l.tops)-1]; total < 10000*40-1 {
			fatalf(t, "the content is %.0f tall", total)
		}
		// Scrolled to the middle, the rows there are bound.
		h.enter(func() {
			h.scrollToOffset(l.n, 0, 200000, false)
			h.scrolledList(Send(l.scroll, "contentView"))
		})
		found := false
		for r, rv := range l.shown {
			if r == 5000 {
				found = true
				if s := text(firstText(rv.cells[0].n)); s != "Row 5000" {
					fatalf(t, "row 5000 shows %q", s)
				}
			}
		}
		if !found {
			fatal(t, "row 5000 isn't shown at 200,000pt")
		}
	})
}

// An inset grouped list's rows are inset by the core's inset (macOS's
// 20pt), on a rounded box behind them; the core lays them out at the
// cells' width.
func TestListInsetGrouped(t *testing.T) {
	h := mountApp(t, `
		app {
		  List {
		    appearance: .insetGrouped
		    for i in [1, 2, 3] key i { Text("Row {i}") { padding: 10 } }
		  }
		}
	`)
	onMain(func() {
		l := find(h, ops.KindList)[0].list
		if !l.grouped() || l.inset != 20 || len(l.shown) != 3 {
			fatalf(t, "grouped %v inset %v, %d rows", l.grouped(), l.inset, len(l.shown))
		}
		rv := l.shown[0]
		f := MsgRect(rv.view, Sel("frame"))
		if f.Origin.X != 20 || f.Origin.Y != 20 || f.Size.Width != l.width-40 {
			fatalf(t, "the first row is at %v, the list %v wide", f, l.width)
		}
		if w := find(h, ops.KindText)[0].frame.Size.Width; w != l.width-40 {
			fatalf(t, "the row's text is %v wide, its cell %v", w, l.width-40)
		}
		g := MsgRect(l.group, Sel("frame"))
		if l.group == 0 || SendBool(l.group, "isHidden") || g.Origin.X != 20 || g.Size.Height != l.tops[3]-l.inset-l.tops[0] {
			fatalf(t, "the group's box is %v", g)
		}
	})
}

// The app's going destroys every view the host made for it.
func TestUnloadDestroysViews(t *testing.T) {
	h := mountApp(t, `app { Column { Text("a"); Button("b") {} } }`)
	onMain(func() {
		if len(h.nodes) == 0 {
			fatal(t, "no views")
		}
		h.enter(hosttest.Core(t).Unload)
		if len(h.nodes) != 0 || len(h.byView) != 0 {
			fatalf(t, "%d nodes, %d views left", len(h.nodes), len(h.byView))
		}
	})
}

// button is the host's button titled title.
func button(h *Host, title string) *node {
	for _, n := range find(h, ops.KindButton) {
		if GoString(Send(n.view, "title")) == title {
			return n
		}
	}
	return nil
}
