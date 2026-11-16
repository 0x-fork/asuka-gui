//go:build darwin

package mactest

import "testing"

// The Counter on the C core: its views made, a real click through the
// button's target and action, the text set again, laid out in the window.
func TestCounter(t *testing.T) {
	a := Run(t, `
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
	tr := a.UntilText("Count: 0")
	l := tr.Find("Text", "Count: 0")
	if l.Frame[0] != 16 || l.Frame[1] != 16 || l.Frame[2] <= 0 || l.Frame[3] <= 0 {
		t.Fatalf("text frame %v", l.Frame)
	}
	if b := tr.Find("Button", "Add"); b == nil || b.Class != "NSButton" {
		t.Fatalf("no NSButton Add:\n%s", tr.Raw())
	}
	a.Do("tap Add")
	a.UntilText("Count: 1")
}
