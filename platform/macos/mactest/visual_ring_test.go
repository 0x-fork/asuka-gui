//go:build darwin

package mactest

import (
	"fmt"
	"math"
	"strings"
	"testing"
)

// ringCase is a ProgressRing of a size, in the Style's Color or AppKit's,
// with a value or without.
type ringCase struct {
	side          float64
	color         bool
	indeterminate bool
}

func (c ringCase) String() string {
	return fmt.Sprintf("%v color=%v indeterminate=%v", c.side, c.color, c.indeterminate)
}

// native is whether AppKit's own indicator draws it: one of its sizes, in
// its colors.
func (c ringCase) native() bool { return !c.color && (c.side == 16 || c.side == 32) }

// ringSource is a ProgressRing's Asuka: its value done (or none), its
// width side, its color pink or AppKit's.
func (c ringCase) source(side, color string) string {
	var b strings.Builder
	b.WriteString("ProgressRing { ")
	if !c.indeterminate {
		b.WriteString("value: done; ")
	}
	fmt.Fprintf(&b, "width: %s", side)
	if color != "" {
		fmt.Fprintf(&b, "; color: %s", color)
	}
	b.WriteString(" }")
	return b.String()
}

// ringPink is the rings' Color.
const ringPink = "#ff2d55"

// ringStrokes are the circle's stroke widths AppKit's proportions give.
var ringStrokes = map[float64]float64{16: 5, 20: 5, 24: 5, 32: 5, 48: 7.5, 64: 10}

// checkRing checks what n shows: AppKit's indicator or the drawn ring, its
// size, its value and what VoiceOver reads.
func checkRing(t *testing.T, n *Node, c ringCase, value float64) {
	t.Helper()
	r := vobj(n, "ring")
	if n.Class != "AsukaRingView" || r == nil {
		t.Fatalf("%v: a %s", c, n.Class)
	}
	if n.Frame[2] != c.side || n.Frame[3] != c.side {
		t.Fatalf("%v: frame %v", c, n.Frame)
	}
	if c.native() {
		if !vbool(r, "native") || vbool(r, "track") || vbool(r, "spinner") {
			t.Fatalf("%v: not AppKit's indicator alone: %v", c, r)
		}
		cs := 0.0 // regular
		if c.side == 16 {
			cs = 1 // small
		}
		if vnum(r, "nativeStyle") != 1 || vnum(r, "nativeControlSize") != cs {
			t.Fatalf("%v: style %v, control size %v", c, r["nativeStyle"], r["nativeControlSize"])
		}
		if f := vnums(r, "nativeFrame"); len(f) != 4 || f[0] != 0 || f[1] != 0 || f[2] != c.side || f[3] != c.side {
			t.Fatalf("%v: AppKit's indicator at %v", c, f)
		}
		if vbool(r, "nativeIndeterminate") != c.indeterminate {
			t.Fatalf("%v: indeterminate %v", c, r["nativeIndeterminate"])
		}
		if !c.indeterminate && vnum(r, "nativeValue") != value {
			t.Fatalf("%v: value %v", c, r["nativeValue"])
		}
		if vbool(r, "nativeA11yElement") {
			t.Fatalf("%v: AppKit's indicator is read besides the ring", c)
		}
	} else {
		if vbool(r, "native") {
			t.Fatalf("%v: AppKit's indicator", c)
		}
		side := c.side
		if c.indeterminate {
			if !vbool(r, "spinner") || vbool(r, "track") {
				t.Fatalf("%v: no spinner: %v", c, r)
			}
			if vnum(r, "spokes") != 8 {
				t.Fatalf("%v: %v spokes", c, r["spokes"])
			}
			if f := vnums(r, "spinnerFrame"); f[2] != side || f[3] != side {
				t.Fatalf("%v: spinner %v", c, f)
			}
			if w := vnum(r, "spokeWidth"); w != side/8 {
				t.Fatalf("%v: spoke %v wide", c, w)
			}
			// The spoke reaches the edge, round end included.
			if b := vnums(r, "spokeBox"); math.Abs(b[1]-side/8/2) > 1e-6 || b[0] != side/2 {
				t.Fatalf("%v: spoke %v", c, b)
			}
			if !vbool(r, "spinning") {
				t.Fatalf("%v: the spinner doesn't turn", c)
			}
		} else {
			if !vbool(r, "track") || !vbool(r, "arc") || vbool(r, "spinner") {
				t.Fatalf("%v: no circle: %v", c, r)
			}
			w := ringStrokes[side]
			if vnum(r, "strokeWidth") != w || vnum(r, "trackWidth") != w || vstr(r, "lineCap") != "round" || vstr(r, "trackCap") != "round" {
				t.Fatalf("%v: stroke %v (want %v), cap %v", c, r["strokeWidth"], w, r["lineCap"])
			}
			// The stroke's outer edge is the frame's.
			for _, k := range []string{"arcBox", "trackBox"} {
				b := vnums(r, k)
				if math.Abs(b[2]+w-side) > 1e-6 || math.Abs(b[0]-w/2) > 1e-6 {
					t.Fatalf("%v: circle %v", c, b)
				}
			}
			if e := vnum(r, "strokeEnd"); e != value {
				t.Fatalf("%v: arc ends at %v", c, e)
			}
		}
		// Its colors: the Style's, or AppKit's as read from its indicator
		// (gray while its window isn't active).
		key := "arcColor"
		if c.indeterminate {
			key = "spokeColor"
		}
		got := vnums(r, key)
		if len(got) != 4 {
			t.Fatalf("%v: no %s", c, key)
		}
		if c.color {
			want := vnums(r, "colorExpected")
			for i := range 4 {
				if len(want) != 4 || math.Abs(got[i]-want[i]) > 1e-3 {
					t.Fatalf("%v: %s %v, want the Color's %v", c, key, got, want)
				}
			}
		} else {
			if !vbool(r, "lookRead") || got[3] == 0 {
				t.Fatalf("%v: %s %v, AppKit's not read", c, key, got)
			}
			if !vbool(r, "windowActive") && (math.Abs(got[0]-got[1]) > 0.02 || math.Abs(got[1]-got[2]) > 0.02) {
				t.Fatalf("%v: %s %v, not AppKit's gray in an inactive window", c, key, got)
			}
		}
		if !c.indeterminate {
			if tc := vnums(r, "trackColor"); len(tc) != 4 || tc[3] <= 0 || tc[3] > 0.2 {
				t.Fatalf("%v: track %v", c, tc)
			}
		}
	}
	// It reads as AppKit's own indicator does: the same role, description,
	// value, minimum and maximum, and nothing else read in it.
	got, _ := r["a11y"].(map[string]any)
	want, _ := r["appKitA11y"].(map[string]any)
	for _, attr := range []string{"AXRole", "AXRoleDescription", "AXValue", "AXMinValue", "AXMaxValue"} {
		if got[attr] != want[attr] {
			t.Fatalf("%v: %s %q, AppKit's %q", c, attr, got[attr], want[attr])
		}
	}
	if !n.Bool("a11yElement") || vnum(r, "a11yChildren") != 0 {
		t.Fatalf("%v: read %v, %v elements in it", c, n.Bool("a11yElement"), r["a11yChildren"])
	}
}

// A ProgressRing is AppKit's spinning NSProgressIndicator at its sizes (16
// and 32 points) in its colors, and drawn as AppKit draws it at any other
// size or in the Style's Color: the circle's track and arc as wide as
// AppKit's stroke at that size, as large as the frame; the spinner's
// spokes. Either way the AsukaRingView is what VoiceOver reads, as AppKit's
// indicator.
func TestVisualProgressRing(t *testing.T) {
	var cases []ringCase
	var rings []string
	for _, side := range []float64{16, 24, 32, 64} {
		for _, color := range []bool{false, true} {
			for _, ind := range []bool{false, true} {
				c := ringCase{side, color, ind}
				cases = append(cases, c)
				col := ""
				if color {
					col = ringPink
				}
				rings = append(rings, c.source(fmt.Sprint(side), col))
			}
		}
	}
	a := Run(t, `
		component Rings {
		  state done = 0.25
		  Column {
		    gap: 4; padding: 12; align: .start
		    `+strings.Join(rings, "\n")+`
		    Button("Done") { on press { done = 2 } }
		  }
		}
		app { Rings() }
	`)
	tr := a.Until(func(tr *Tree) bool {
		return len(tr.Kind("ProgressRing")) == len(cases) && tr.Find("Button", "Done") != nil
	})
	for i, c := range cases {
		checkRing(t, tr.Kind("ProgressRing")[i], c, 0.25)
	}
	// A new value (past the end: full): AppKit's indicator moves to it, the
	// drawn arc ends there.
	a.Do("tap Done")
	tr = a.Until(func(tr *Tree) bool { return vnum(vobj(tr.Kind("ProgressRing")[0], "ring"), "nativeValue") == 1 })
	for i, c := range cases {
		checkRing(t, tr.Kind("ProgressRing")[i], c, 1)
	}
}

// The ring switches between AppKit's indicator and the drawn one as its
// size and its Color change, keeping its view, its value and what
// VoiceOver reads.
func TestVisualProgressRingSwitches(t *testing.T) {
	a := Run(t, `
		component R {
		  state side = 32.0
		  state pink = false
		  state done = 0.5
		  Column {
		    padding: 12; gap: 4; align: .start
		    ProgressRing { value: done; width: side; color: if pink { `+ringPink+` } else { .default } }
		    Button("48") { on press { side = 48 } }
		    Button("16") { on press { side = 16 } }
		    Button("Pink") { on press { pink = true } }
		    Button("Three quarters") { on press { done = 0.75 } }
		    Button("Plain") { on press { pink = false } }
		    Button("Back") { on press { side = 32; done = 0.25 } }
		    Button("20") { on press { side = 20 } }
		    Button("32") { on press { side = 32 } }
		  }
		}
		app { R() }
	`)
	tr := a.UntilText("32")
	id := tr.Kind("ProgressRing")[0].ID
	step := func(tap string, side float64, pink bool, v float64) {
		t.Helper()
		if tap != "" {
			a.Do("tap " + tap)
		}
		c := ringCase{side: side, color: pink}
		tr := a.Until(func(tr *Tree) bool {
			n := tr.Kind("ProgressRing")[0]
			r := vobj(n, "ring")
			if n.Frame[2] != side || vbool(r, "native") != c.native() {
				return false
			}
			if c.native() {
				return vnum(r, "nativeValue") == v
			}
			return vnum(r, "strokeEnd") == v
		})
		n := tr.Kind("ProgressRing")[0]
		if n.ID != id {
			t.Fatalf("the ring's view changed")
		}
		checkRing(t, n, c, v)
	}
	step("", 32, false, 0.5)
	step("48", 48, false, 0.5)
	step("16", 16, false, 0.5)
	step("Pink", 16, true, 0.5)
	step("Three quarters", 16, true, 0.75)
	step("Plain", 16, false, 0.75)
	step("Back", 32, false, 0.25)
	step("20", 20, false, 0.25)
	step("32", 32, false, 0.25)
	if vbool(vobj(a.Tree().Kind("ProgressRing")[0], "ring"), "ref") {
		t.Fatal("AppKit's unseen indicator outlived the drawn ring")
	}
}

// A value change animates as AppKit's animator does, a quarter second, on
// the drawn arc as on AppKit's indicator; not with Reduce Motion.
func TestVisualProgressRingAnimates(t *testing.T) {
	a := Run(t, `
		component R {
		  state done = 0.25
		  Row {
		    padding: 12; gap: 8; align: .start
		    ProgressRing { value: done }
		    ProgressRing { value: done; width: 48 }
		    Button("Set") { on press { done = 0.75 } }
		  }
		}
		app { R() }
	`)
	tr := a.UntilText("Set")
	rs := tr.Kind("ProgressRing")
	native, drawn := vobj(rs[0], "ring"), vobj(rs[1], "ring")
	if !vbool(native, "native") || vbool(drawn, "native") {
		t.Fatalf("not one of each: %v %v", native["native"], drawn["native"])
	}
	if vbool(native, "reduceMotion") {
		t.Skip("Reduce Motion is on")
	}
	a.Do("tap Set")
	tr = a.Until(func(tr *Tree) bool { return vnum(vobj(tr.Kind("ProgressRing")[1], "ring"), "strokeEnd") == 0.75 })
	rs = tr.Kind("ProgressRing")
	if d := vnum(vobj(rs[1], "ring"), "lastAnimation"); d != 0.25 {
		t.Errorf("the arc animates for %vs, not as AppKit's", d)
	}
	if d := vnum(vobj(rs[0], "ring"), "lastAnimation"); d != 0.25 {
		t.Errorf("AppKit's indicator animates for %vs", d)
	}
	a.Until(func(tr *Tree) bool { return vnum(vobj(tr.Kind("ProgressRing")[0], "ring"), "nativeValue") == 0.75 })
}

// Disposing a ring lets go of what it drew and of AppKit's indicators.
func TestVisualProgressRingDisposes(t *testing.T) {
	a := Run(t, `
		component R {
		  state shown = true
		  Column {
		    padding: 12; gap: 8
		    if shown {
		      Row {
		        gap: 8
		        ProgressRing { value: 0.5 }
		        ProgressRing { value: 0.5; width: 48 }
		        ProgressRing { width: 48 }
		      }
		    }
		    Button("Hide") { on press { shown = false } }
		  }
		}
		app { R() }
	`)
	tr := a.Until(func(tr *Tree) bool { return len(tr.Kind("ProgressRing")) == 3 })
	if h := vnum(visualState(tr), "ringsHolding"); h != 3 {
		t.Fatalf("%v rings hold their views", h)
	}
	a.Do("tap Hide")
	tr = a.Until(func(tr *Tree) bool { return len(tr.Kind("ProgressRing")) == 0 })
	if h := vnum(visualState(tr), "ringsHolding"); h != 0 {
		t.Fatalf("%v rings hold on", h)
	}
}
