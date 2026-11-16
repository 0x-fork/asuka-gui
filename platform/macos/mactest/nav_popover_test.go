//go:build darwin

package mactest

import (
	"testing"
)

// A popover is an NSPopover as big as its content, shown from its anchor;
// closing it from the core closes the NSPopover, and closing the NSPopover
// reports Dismissed (TestPopover).
func TestNavPopover(t *testing.T) {
	a := Run(t, `
		app { Root() }
		component Root {
		  state open = true
		  Column {
		    padding: 20
		    gap: 8
		    Text("open {open}")
		    Button("Show") { on press { open = true } }
		    Button("Hide") { on press { open = false } }
		    Popover {
		      bind open: open
		      content { Column { padding: 12; Text("Hello") } }
		      Button("Info") { on press { open = true } }
		    }
		  }
		}
	`)
	shown := func(tr *Tree) *Node {
		for _, n := range tr.Kind("Popover") {
			if n.Bool("popoverShown") {
				return n
			}
		}
		return nil
	}
	tr := a.Until(func(tr *Tree) bool { return shown(tr) != nil })
	p := shown(tr)
	if w, h := pair(p.Get("popoverContentSize")); w <= 24 || h <= 24 {
		t.Fatalf("content size %vx%v", w, h)
	}
	if n := tr.FindText("Hello"); n == nil || n.Window == tr.Main().Title {
		t.Fatalf("its content isn't in the popover's window:\n%s", tr.Raw())
	}
	// A click outside closes the NSPopover: the core hears Dismissed.
	a.Do("dismiss")
	a.UntilText("open false")
	a.Until(func(tr *Tree) bool { return shown(tr) == nil })
	// Shown again.
	a.Do("tap Show")
	a.Until(func(tr *Tree) bool { return shown(tr) != nil })
	// Closed by the core.
	a.Do("tap Hide")
	a.UntilText("open false")
	a.Until(func(tr *Tree) bool { return shown(tr) == nil })
}
