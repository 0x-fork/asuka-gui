//go:build darwin

package mactest

import (
	"slices"
	"testing"
)

// A native component's view (the automation's MacTestSlider, an
// NSSlider, made as a package's typed view is): made by its type, its
// props set and followed, its events handled, measured by its
// sizeThatFits and filling its node's view; destroyed with the node.
func TestVisualNativeView(t *testing.T) {
	a := Run(t, `
		native component MacTestSlider(label: string) {
		  prop value: float = 0.5
		  event changed(value: float)
		}

		component Root {
		  state v = 0.25
		  state shown = true
		  state got = "none"
		  Column {
		    padding: 10; gap: 8; align: .start
		    if shown {
		      MacTestSlider("Volume") { value: v; on changed(n) { got = "{n}" } }
		    }
		    Text("got {got}")
		    Button("More") { on press { v = 0.75 } }
		    Button("Hide") { on press { shown = false } }
		  }
		}
		app { Root() }
	`)
	tr := a.UntilText("got none")
	ns := tr.Kind("Native")
	if len(ns) != 1 {
		t.Fatalf("%d native views:\n%s", len(ns), tr.Raw())
	}
	n := ns[0]
	nv := vobj(n, "native")
	if !vbool(nv, "made") || vstr(nv, "viewClass") != "NSSlider" {
		t.Fatalf("the factory wasn't called: %v", nv)
	}
	if n.Frame[2] != 160 || n.Frame[3] != 24 {
		t.Errorf("the native view's frame %v, want 160x24", n.Frame)
	}
	if f := vnums(nv, "viewFrame"); !vbool(nv, "inHost") || len(f) != 4 || f[2] != 160 || f[3] != 24 {
		t.Errorf("the app's view doesn't fill its node's: %v", nv)
	}
	if v := vnum(nv, "doubleValue"); v != 0.25 {
		t.Errorf("value %v", v)
	}
	if set := vstrs(nv, "set"); !slices.Contains(set, "Label") || !slices.Contains(set, "Value") {
		t.Errorf("props set %q", set)
	}
	a.Do("tap More")
	a.Until(func(tr *Tree) bool { return vnum(vobj(tr.Kind("Native")[0], "native"), "doubleValue") == 0.75 })

	a.Do("nativeemit " + vid(n) + " Changed 0.5")
	a.UntilText("got 0.5")

	a.Do("tap Hide")
	tr = a.Until(func(tr *Tree) bool { return len(tr.Kind("Native")) == 0 })
	if st := visualState(tr); vnum(st, "nativesDestroyed") != vnum(st, "nativesMade") || vnum(st, "nativesMade") != 1 {
		t.Errorf("not destroyed with its node: made %v, destroyed %v", st["nativesMade"], st["nativesDestroyed"])
	}
}
