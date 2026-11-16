//go:build linux && !android

package linux

import (
	"testing"

	"github.com/egoist/asuka/host/hosttest"
	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/ops"
)

// mountApp runs an Asuka app (src) on a new host, on the C core as an
// app runs (host/hosttest), its window shown; its windows go when the
// test ends.
func mountApp(t *testing.T, src string) *Host {
	t.Helper()
	var h *Host
	onMain(func() {
		h = newHost()
		h.app = testApp
		h.setup()
	})
	t.Cleanup(func() {
		onMain(func() {
			for _, w := range h.wins {
				WindowDestroy(w.window)
			}
		})
	})
	hosttest.Mount(t, h, func(f func()) { onMain(func() { h.enter(f) }) }, src)
	onMain(h.showMain)
	settle(t, h, func() bool { return h.root != nil && (h.main.envW > 0 || h.main.hidden) })
	return h
}

func labelText(n *node) string { return GoString(Call(Sym("gtk_label_get_text"), n.inner)) }

// The Counter on the C core: its widgets made, real clicks through the
// button's own activation, the label set again, laid out in the window.
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
	var label, button *node
	onMain(func() {
		texts, buttons := find(h, ops.KindText), find(h, ops.KindButton)
		if len(texts) != 1 || len(buttons) != 1 {
			fatalf(t, "%d texts, %d buttons", len(texts), len(buttons))
		}
		label, button = texts[0], buttons[0]
		if s := labelText(label); s != "Count: 0" {
			fatalf(t, "text %q", s)
		}
		click(button.inner)
		click(button.inner)
	})
	settle(t, h, func() bool { return label.text == "Count: 2" })
	onMain(func() {
		if s := labelText(label); s != "Count: 2" {
			fatalf(t, "after two clicks: %q", s)
		}
	})
	settle(t, h, func() bool {
		x, y, w, _ := allocation(label.widget)
		return x == 16 && y == 16 && w > 0
	})
}

// What the user types reaches the app's state, Enter submits, and the
// app's write reaches the field.
func TestCoreTextField(t *testing.T) {
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
	var f, label *node
	onMain(func() {
		f, label = fieldNode(t, h), find(h, ops.KindText)[0]
		EditableSetText(f.field.editable, "hello")
	})
	settle(t, h, func() bool { return label.text == "hello 0" })
	onMain(func() {
		s := CString("activate")
		Call(Sym("g_signal_emit_by_name"), EditableGetDelegate(f.field.editable), Addr(s))
	})
	settle(t, h, func() bool { return label.text == "hello 1" })
	onMain(func() { click(find(h, ops.KindButton)[0].inner) })
	settle(t, h, func() bool { return EditableGetText(f.field.editable) == "set" })
}
