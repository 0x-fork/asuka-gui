//go:build darwin

package mactest

import (
	"testing"
)

// exiting are the views going out in a window (a transition's, a match's,
// a popped screen's).
func exiting(w *Window) []map[string]any {
	var out []map[string]any
	l, _ := w.Props["exiting"].([]any)
	for _, x := range l {
		out = append(out, x.(map[string]any))
	}
	return out
}

// A view with a transition comes in during animate { } at its first frame,
// its text laid out at its size at once; going out, its node goes from the
// tree while its view stays on screen until it has gone out
// (TestTransitions).
func TestNavTransitions(t *testing.T) {
	a := Run(t, `
		state shown = false
		app {
		  Column {
		    Button("In") { on press { animate(() => { shown = true }) } }
		    Button("Out") { on press { animate(Animation(duration: 3s), () => { shown = false }) } }
		    if shown {
		      View {
		        height: 40
		        background: #ff0000
		        transition: .slide
		        Text("toast")
		        View { height: 20; material: .thin }
		      }
		    }
		  }
		}
	`)
	a.UntilText("In")
	a.Do("tap In")
	tr := a.Until(func(tr *Tree) bool { return tr.FindText("toast") != nil })
	// Its text is at its size at once: the batch's animation doesn't grow
	// it out of the zero frame a new view has, which a label would
	// ellipsize its text in.
	if f := tr.FindText("toast").Frame; f[2] <= 0 || f[3] <= 0 {
		t.Fatalf("new text at %v", f)
	}
	a.Do("tap Out")
	// It's gone from the core's tree, and stays on screen while it goes.
	tr = a.Until(func(tr *Tree) bool { return tr.FindText("toast") == nil && len(exiting(tr.Main())) > 0 })
	a.Until(func(tr *Tree) bool { return len(exiting(tr.Main())) == 0 })
}

// A view with a key destroyed during an animation waits in the window's
// content view, where it was, for the view inserted with its key, which
// starts at its place and grows to its own; the old one then goes
// (TestMatch).
func TestNavMatch(t *testing.T) {
	a := Run(t, `
		state big = false
		app {
		  Column {
		    padding: 10
		    align: .start
		    Button("Grow") { on press { animate(Animation(duration: 2s), () => { big = true }) } }
		    if big {
		      View { width: 200; height: 150; background: .blue; shared: "card" }
		    } else {
		      View { width: 40; height: 30; background: .blue; shared: "card" }
		    }
		  }
		}
	`)
	a.UntilText("Grow")
	a.Do("tap Grow")
	tr := a.Until(func(tr *Tree) bool { return len(exiting(tr.Main())) == 1 })
	ex := exiting(tr.Main())[0]
	if ex["inContent"] != true {
		t.Fatalf("the old view didn't wait in the content view: %v", ex)
	}
	if tr.Main().Props["matchWaiting"] != 0.0 {
		t.Fatalf("not matched: %v waiting", tr.Main().Props["matchWaiting"])
	}
	// The old view goes; the new one ends at its own place and size.
	a.Until(func(tr *Tree) bool {
		if len(exiting(tr.Main())) != 0 {
			return false
		}
		for _, n := range tr.Kind("View") {
			if f := n.Frame; len(f) == 4 && f[2] == 200 && f[3] == 150 && f[0] == 10 {
				return true
			}
		}
		return false
	})
}
