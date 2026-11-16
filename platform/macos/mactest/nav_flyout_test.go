//go:build darwin

package mactest

import (
	"strconv"
	"testing"
)

// AppKit's collection behavior bits and levels.
const (
	joinAllSpaces    = 1 << 0
	ignoresCycle     = 1 << 6
	fullScreenAux    = 1 << 8
	floatingLevel    = 3
	popUpMenuLevel   = 101
	placedTolerance  = 1.0
	flyoutRowsHeight = 30
)

// A flyout hanging from a rectangle of its parent: the parent's child
// window, below the rectangle and lined up with its start, gap away
// (TestFlyoutAnchoredDarwin).
func TestNavFlyoutAnchored(t *testing.T) {
	a := Run(t, `
		app { Root() }
		component Root {
		  state open = false
		  Column {
		    padding: 20
		    Button("Open") { on press { open = true } }
		    Window {
		      bind open: open
		      title: "Fly"
		      flyout: Flyout(anchor: Anchor(x: 10, y: 20, width: 100, height: 30), placement: .bottomStart, gap: 4)
		      width: 180
		      height: 90
		      Text("Hello")
		    }
		  }
		}
	`)
	a.Do("movewindow 200,200", "tap Open")
	tr := a.Until(func(tr *Tree) bool { w := tr.Window("Fly"); return w != nil && w.Visible })
	w, m := tr.Window("Fly"), tr.Main()
	c := winRect(m, "contentTopLeft")
	f := winRect(w, "frameYDown")
	if !near(f[0], c[0]+10) || !near(f[1], c[1]+54) || f[2] != 180 || f[3] != 90 {
		t.Fatalf("flyout at %v; the parent's content at %v", f, c)
	}
	if w.Props["parent"] != m.Title {
		t.Fatalf("not the parent's child window: %v", w.Props["parent"])
	}
	if w.Props["class"] != "AsukaPanel" || int(winNum(w, "styleMask"))&styleNonactivating == 0 || w.Props["canBecomeMain"] != false {
		t.Fatalf("the flyout is a %v, style %b", w.Props["class"], int(winNum(w, "styleMask")))
	}
	// Rounded over the menu's material, shadowed, clear.
	if w.Props["opaque"] != false || w.Props["hasShadow"] != true || w.Props["hasMaterial"] != true || winNum(w, "cornerRadius") != 12 {
		t.Fatalf("opaque %v shadow %v material %v radius %v", w.Props["opaque"], w.Props["hasShadow"], w.Props["hasMaterial"], w.Props["cornerRadius"])
	}
	// Escape its content leaves closes it, as the user closing it does.
	a.Do("cancel Fly")
	a.Until(func(tr *Tree) bool { return tr.Window("Fly") == nil })
	// Open again: a click in another app closes it, and so does its losing
	// the keyboard.
	a.Do("tap Open")
	a.Until(func(tr *Tree) bool { w := tr.Window("Fly"); return w != nil && w.Visible })
	a.Do("outsideclick")
	a.Until(func(tr *Tree) bool { return tr.Window("Fly") == nil })
	a.Do("tap Open")
	a.Until(func(tr *Tree) bool { w := tr.Window("Fly"); return w != nil && w.Visible })
	a.Do("resignkey Fly")
	a.Until(func(tr *Tree) bool { return tr.Window("Fly") == nil })
}

// A tray icon's flyout: an AsukaPanel, nonactivating, at the level of
// menus, under the status item's button and centered on it, sized to its
// content and placed again as that grows; a modal Window over it is its
// child window at its level and keeps it open; it closes as it loses the
// keyboard and on Escape (TestFlyoutDarwin).
func TestNavTrayFlyout(t *testing.T) {
	a := Run(t, `
		import asuka/tray
		app { Root() }
		component Root {
		  state open = false
		  state rows = 2
		  state confirm = false
		  Column {
		    padding: 20
		    Text("open {open}")
		    tray.Tray("power") {
		      symbol: "bolt.fill"
		      on click { open = !open }
		    }
		    Window {
		      bind open: open
		      title: "Power"
		      flyout: Flyout(anchor: tray.anchor("power"), placement: .bottom)
		      fitContent: true
		      minWidth: 200
		      maxHeight: 500
		      Column {
		        padding: 10
		        View { width: 240; height: 30 * rows }
		        Button("More") { on press { rows = 5 } }
		        Button("Quit…") { on press { confirm = true } }
		        Window {
		          bind open: confirm
		          modal: true
		          title: "Quit?"
		          width: 300
		          height: 120
		          Button("Cancel") { on press { confirm = false } }
		        }
		      }
		    }
		  }
		}
	`)
	a.UntilText("open false")
	a.Do("trayclick power")
	tr := a.Until(func(tr *Tree) bool { w := tr.Window("Power"); return w != nil && w.Visible })
	w := tr.Window("Power")
	anchor := winRect(w, "flyoutAnchor")
	if len(anchor) != 4 || anchor[2] <= 0 || anchor[1] > 100 {
		t.Skipf("the status item isn't in the menu bar (no menu bar in this session, or no room in it): anchor %v", anchor)
	}
	if w.Props["class"] != "AsukaPanel" || int(winNum(w, "styleMask"))&styleNonactivating == 0 {
		t.Fatal("the flyout isn't a nonactivating panel")
	}
	if winNum(w, "level") != popUpMenuLevel || w.Props["opaque"] != false || w.Props["hasShadow"] != true {
		t.Fatalf("level %v opaque %v shadow %v", w.Props["level"], w.Props["opaque"], w.Props["hasShadow"])
	}
	// Its content's size: 10 + 60 + its buttons + 10, 260 wide.
	content := func(tr *Tree) (float64, float64) { w := tr.Window("Power"); return w.Content[2], w.Content[3] }
	cw, ch := content(tr)
	if cw != 260 || ch < 80 {
		t.Fatalf("content %vx%v", cw, ch)
	}
	// Below the icon, centered on it, within the display's work area. (The
	// anchor is the one it was placed against: the automation's click on
	// the icon is no real one, which the copy of the button it hit comes
	// from.)
	under := func(w *Window, height float64) {
		t.Helper()
		f, anchor, work := winRect(w, "frameYDown"), winRect(w, "flyoutAnchor"), winRect(w, "anchorWorkArea")
		wantX := max(min(anchor[0]+(anchor[2]-f[2])/2, work[0]+work[2]-f[2]), work[0])
		if !near(f[1], max(anchor[1]+anchor[3], work[1])) || !near(f[0], wantX) || f[3] != height {
			t.Fatalf("flyout at %v; anchor %v, work %v", f, anchor, work)
		}
	}
	under(w, ch)
	// Its content grows: resized, placed again under the icon.
	a.Do("tap More")
	tr = a.Until(func(tr *Tree) bool { _, h := content(tr); return h == ch+90 })
	under(tr.Window("Power"), ch+90)
	// A dialog over it: its child window, not a sheet, at its level; the
	// flyout stays as it loses the keyboard to it.
	a.Do("tap Quit…")
	tr = a.Until(func(tr *Tree) bool { d := tr.Window("Quit?"); return d != nil && d.Visible })
	d := tr.Window("Quit?")
	if d.Props["parent"] != "Power" || d.Props["sheetParent"] != nil || winNum(d, "level") != popUpMenuLevel {
		t.Fatalf("the dialog: parent %v, sheet of %v, level %v", d.Props["parent"], d.Props["sheetParent"], d.Props["level"])
	}
	a.Do("resignkey Power", "wait 300ms")
	a.UntilText("open true")
	a.Do("tap Cancel")
	a.Until(func(tr *Tree) bool { return tr.Window("Quit?") == nil })
	// It loses the keyboard: closed, as the user closes it.
	a.Do("resignkey Power")
	a.UntilText("open false")
	a.Until(func(tr *Tree) bool { return tr.Window("Power") == nil })
	// Open again; Escape closes it.
	a.Do("trayclick power")
	a.Until(func(tr *Tree) bool { w := tr.Window("Power"); return w != nil && w.Visible })
	a.Do("cancel Power")
	a.UntilText("open false")
}

// A panel as the main window: an AsukaPanel made from the main window,
// nonactivating, on every Space, floating; sized to its content, centered
// on the pointer's display a third of the way down, growing down from its
// top; Escape closes it as the user closing it does (TestPanelDarwin).
func TestNavPanel(t *testing.T) {
	// A launcher's: an app in the background (AppKit counts no panel as
	// a window an app quits after), whose panel shows once it's asked to,
	// as a shortcut would.
	a := RunWith(t, `
		state shown = false
		state asked = 0
		state results = 0
		app {
		  Window {
		    bind open: shown
		    title: "Launcher"
		    panel: true
		    fitContent: true
		    minWidth: 600
		    maxHeight: 400
		    // Kept open (the test's app may lose the keyboard to another
		    // app's window, which closes a panel), its asking counted.
		    on closeRequest { asked += 1 }
		    Launcher()
		  }
		}
		component Launcher {
		  task {
		    await sleep(500ms)
		    shown = true
		  }
		  Column {
		    Text("asked {asked}")
		    Row {
		      height: 36
		      Button("Three") { on press { results = 3 } }
		      Button("Thirty") { on press { results = 30 } }
		    }
		    View { height: 40 * results }
		  }
		}
	`, Options{Env: []string{shellConfig(t, `{"background": true}`)}})
	tr := a.Until(func(tr *Tree) bool {
		m := tr.Main()
		return m.Visible && m.Content[2] == 600 && m.Content[3] > 36 && m.Content[3] < 100
	})
	m := tr.Main()
	if m.Props["class"] != "AsukaPanel" || int(winNum(m, "styleMask"))&styleNonactivating == 0 {
		t.Fatalf("the main window is a %v, style %b", m.Props["class"], int(winNum(m, "styleMask")))
	}
	b := int(winNum(m, "collectionBehavior"))
	if b&joinAllSpaces == 0 || b&fullScreenAux == 0 || b&ignoresCycle == 0 || winNum(m, "level") != floatingLevel {
		t.Fatalf("collection behavior %b level %v", b, m.Props["level"])
	}
	if m.Content[2] != 600 {
		t.Fatalf("content %v", m.Content)
	}
	f := winRect(m, "frameYDown")
	work := winRect(m, "pointerWorkArea")
	if !near(f[0], work[0]+(work[2]-f[2])/2) || !near(f[1], work[1]+(work[3]-f[3])/3) {
		t.Fatalf("panel at %v; work area %v", f, work)
	}
	// Results come: it grows down from its top.
	a.Do("tap Three")
	h0 := m.Content[3]
	tr = a.Until(func(tr *Tree) bool { return tr.Main().Content[3] == h0+120 })
	if f2 := winRect(tr.Main(), "frameYDown"); !near(f2[1], f[1]) {
		t.Fatalf("after results: %v (was %v)", f2, f)
	}
	a.Do("tap Thirty")
	a.Until(func(tr *Tree) bool { return tr.Main().Content[3] == 400 })
	// Escape: the user's closing, which the app is asked about.
	n := 0.0
	for _, x := range a.Tree().All() {
		if len(x.Text) > 6 && x.Text[:6] == "asked " {
			n, _ = strconv.ParseFloat(x.Text[6:], 64)
		}
	}
	a.Do("cancel Launcher")
	a.UntilText("asked " + ftoa(n+1))
}
