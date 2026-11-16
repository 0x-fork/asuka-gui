//go:build darwin

package mactest

import (
	"fmt"
	"testing"
)

// fieldOf is the TextField whose placeholder is p, and its "field" JSON.
func fieldOf(t *testing.T, tr *Tree, p string) (*Node, map[string]any) {
	t.Helper()
	n, f := findField(tr, p)
	if n == nil {
		t.Fatalf("no field %q:\n%s", p, tr.Raw())
	}
	return n, f
}

// findField is the TextField whose placeholder is p (nil if none shows),
// and its "field" JSON.
func findField(tr *Tree, p string) (*Node, map[string]any) {
	for _, n := range tr.All() {
		if n.Kind == "TextField" && n.Str("placeholder") == p {
			f, _ := n.Get("field").(map[string]any)
			return n, f
		}
	}
	return nil, nil
}

// Up and Down in a field go to the view around it that declared them, not
// to the field's caret; a character it didn't declare goes into the field;
// held, a key's repeats are taken too. A one-line field (its field editor
// has the focus) and a multiline one (its text view) (TestKeysAroundFieldDarwin).
func TestKeysAroundField(t *testing.T) {
	for _, multiline := range []string{"false", "true"} {
		t.Run("multiline="+multiline, func(t *testing.T) {
			a := Run(t, `
				component K {
				  state sel = 0
				  state text = "hello"
				  View {
				    keys: [
				      Shortcut(key: "down", run: () => { sel += 1 }),
				      Shortcut(key: "up", run: () => { sel -= 1 }),
				    ]
				    Text("sel {sel} text {text}")
				    TextField { bind text: text; placeholder: "f"; multiline: `+multiline+` }
				  }
				}
				app { K() }
			`)
			a.UntilText("sel 0 text hello")
			a.Do("tap f", "select 2,2")
			a.Until(func(tr *Tree) bool {
				_, f := fieldOf(t, tr, "f")
				return f["editing"] == true && sel(f) == "2,2"
			})
			a.Do("keyin down", "keyrepeat down", "keyin up")
			tr := a.UntilText("sel 1 text hello")
			if _, f := fieldOf(t, tr, "f"); sel(f) != "2,2" {
				t.Fatalf("the caret moved: %v", f["selection"])
			}
			a.Do("keyin a")
			a.UntilText("sel 1 text heallo")
		})
	}
}

// sel is a field's selection in its JSON, "start,end".
func sel(f map[string]any) string {
	s, _ := f["selection"].([]any)
	if len(s) != 2 {
		return ""
	}
	return fmt.Sprintf("%v,%v", s[0], s[1])
}

// The nearest first: a field's own Keys, then the view around it's; what
// neither has goes on (Return: Submit) (TestKeysNearestFirstDarwin).
func TestKeysNearestFirst(t *testing.T) {
	a := Run(t, `
		component K {
		  state got = ""
		  View {
		    keys: [
		      Shortcut(key: "escape", run: () => { got = got + "outer escape;" }),
		      Shortcut(key: "up", run: () => { got = got + "outer up;" }),
		    ]
		    Text("got {got}")
		    TextField {
		      placeholder: "f"
		      keys: [
		        Shortcut(key: "up", run: () => { got = got + "field up;" }),
		        Shortcut(key: "a", shift: true, run: () => { got = got + "field shift a;" }),
		      ]
		      on submit { got = got + "submit;" }
		    }
		  }
		}
		app { K() }
	`)
	a.UntilText("got ")
	a.Do("tap f", "keyin up", "keyin escape", "keyin shift+a", "keyin enter")
	a.UntilText("got field up;outer escape;field shift a;submit;")
}

// A view's ⌘S comes before the menu bar's: in a field inside it, the
// view's runs; in a field elsewhere, the menu's Save (TestKeysBeforeMenuBarDarwin).
func TestKeysBeforeMenuBar(t *testing.T) {
	a := Run(t, `
		app { K() }
		component K {
		  state saved = 0
		  state viewSaved = 0
		  Column {
		    MenuBar {
		      Menu("File") {
		        MenuItem("Save") {
		          shortcut: Shortcut(key: "s", primary: true)
		          on press { saved += 1 }
		        }
		      }
		    }
		    Text("menu {saved} view {viewSaved}")
		    View {
		      keys: [Shortcut(key: "s", primary: true, run: () => { viewSaved += 1 })]
		      TextField { placeholder: "in" }
		    }
		    TextField { placeholder: "out" }
		  }
		}
	`)
	a.UntilText("menu 0 view 0")
	a.Do("tap out", "keyin cmd+s")
	a.UntilText("menu 1 view 0")
	a.Do("tap in", "keyin cmd+s")
	a.UntilText("menu 1 view 1")
	// As the window server sends it, through the application's queue.
	a.Do("key cmd+s")
	a.UntilText("menu 1 view 2")
}

// useKeys on the main window takes keys when nothing has the focus, and
// last when a field has it (TestUseKeysDarwin).
func TestUseKeys(t *testing.T) {
	a := Run(t, `
		component K {
		  state got = ""
		  useKeys([
		    Shortcut(key: "space", run: () => { got = got + "space;" }),
		    Shortcut(key: "down", run: () => { got = got + "down;" }),
		  ])
		  Column {
		    padding: 10
		    Text("got {got}")
		    TextField { placeholder: "f" }
		  }
		}
		app { K() }
	`)
	a.UntilText("got ")
	a.Do("tap f", "unfocus", "keyin space", "tap f", "keyin down")
	a.UntilText("got space;down;")
}

// A Window's keys are its own: its content's, from a field in it; the main
// window's don't reach it, and it takes nothing with nothing focused in it;
// the main window's are the main window's (TestKeysInWindowDarwin). (The
// core registers useKeys in a Window's content on the app's root, not on the
// Window, so a Window's own window keys are a view's here.)
func TestKeysInWindow(t *testing.T) {
	a := Run(t, `
		store Log { state got = "" }
		let log = Log()
		component Content {
		  state text = ""
		  View {
		    padding: 10
		    keys: [Shortcut(key: "escape", run: () => { log.got = log.got + "window escape;" })]
		    Text("window field {text}")
		    TextField { bind text: text; placeholder: "w" }
		  }
		}
		component Main {
		  state open = false
		  useKeys([
		    Shortcut(key: "escape", run: () => { log.got = log.got + "main escape;" }),
		    Shortcut(key: "space", run: () => { log.got = log.got + "main space;" }),
		  ])
		  Column {
		    Text("got {log.got}")
		    Button("Open") { on press { open = true } }
		    Window {
		      bind open: open
		      title: "Other"
		      width: 300
		      height: 200
		      Content()
		    }
		  }
		}
		app { Main() }
	`)
	a.UntilText("got ")
	a.Do("tap Open")
	a.UntilText("window field ")
	a.Do("tap w", "keyin escape", "keyin space")
	a.UntilText("window field  ")
	a.UntilText("got window escape;")
	a.Do("unfocus", "keyin escape")
	// The main window in use, nothing focused in it.
	a.Do("unfocus "+mainTitle(a), "keyin escape")
	a.UntilText("got window escape;main escape;")
}

// mainTitle is the main window's title.
func mainTitle(a *App) string {
	if w := a.Tree().Main(); w != nil {
		return w.Title
	}
	return ""
}

// In a sheet (a window of its own), its content's keys come first, then the
// window it's a sheet of: the main window's useKeys, from a field in it or
// with nothing focused (TestKeysInSheetDarwin).
func TestKeysInSheet(t *testing.T) {
	a := Run(t, `
		store Log { state got = "" }
		let log = Log()
		component SheetContent {
		  useKeys([Shortcut(key: "d", primary: true, run: () => { log.got = log.got + "sheet usekeys;" })])
		  View {
		    padding: 20
		    keys: [Shortcut(key: "down", run: () => { log.got = log.got + "sheet down;" })]
		    TextField { placeholder: "s" }
		  }
		}
		component Main {
		  state open = false
		  useKeys([
		    Shortcut(key: "up", run: () => { log.got = log.got + "main up;" }),
		    Shortcut(key: "down", run: () => { log.got = log.got + "main down;" }),
		  ])
		  Column {
		    Text("got {log.got}")
		    Button("Show") { on press { open = true } }
		    Sheet {
		      bind open: open
		      SheetContent()
		    }
		  }
		}
		app { Main() }
	`)
	a.UntilText("got ")
	a.Do("tap Show")
	a.Until(func(tr *Tree) bool {
		n, _ := findField(tr, "s")
		return n != nil && n.Shown
	})
	a.Do("tap s", "keyrepeat down", "keyin up", "keyin cmd+d", "unfocus", "keyin up")
	a.UntilText("got sheet down;main up;sheet usekeys;main up;")
}

// A popover's content goes on to its anchor's window: the main window's
// useKeys, after what the content declared (TestKeysInPopoverDarwin).
func TestKeysInPopover(t *testing.T) {
	a := Run(t, `
		store Log { state got = "" }
		let log = Log()
		component Main {
		  state open = false
		  useKeys([
		    Shortcut(key: "up", run: () => { log.got = log.got + "main up;" }),
		    Shortcut(key: "down", run: () => { log.got = log.got + "main down;" }),
		  ])
		  Column {
		    padding: 20
		    Text("got {log.got}")
		    Popover {
		      bind open: open
		      content {
		        View {
		          padding: 12
		          keys: [Shortcut(key: "down", run: () => { log.got = log.got + "popover down;" })]
		          TextField { placeholder: "p"; width: 120 }
		        }
		      }
		      Button("Info") { on press { open = true } }
		    }
		  }
		}
		app { Main() }
	`)
	a.UntilText("got ")
	a.Do("tap Info")
	a.Until(func(tr *Tree) bool {
		n, _ := findField(tr, "p")
		return n != nil && n.Shown && n.Window != tr.Main().Title
	})
	a.Do("tap p", "keyin down", "keyin up", "unfocus", "keyin up")
	a.UntilText("got popover down;main up;main up;")
}

// A list's row is a cell of the host's, with no parent in the core's tree:
// a key in it goes on to the list, the views around it, the window's
// (TestKeysInListRowDarwin).
func TestKeysInListRow(t *testing.T) {
	a := Run(t, `
		component K {
		  state got = ""
		  useKeys([Shortcut(key: "up", run: () => { got = got + "main up;" })])
		  Column {
		    grow: 1
		    Text("got {got}")
		    View {
		      grow: 1
		      keys: [Shortcut(key: "down", run: () => { got = got + "around down;" })]
		      List {
		        grow: 1
		        for i in [1, 2, 3] key i {
		          Row { height: 44; TextField { placeholder: "row {i}"; grow: 1 } }
		        }
		      }
		    }
		  }
		}
		app { K() }
	`)
	a.UntilText("got ")
	a.Until(func(tr *Tree) bool { n, _ := findField(tr, "row 1"); return n != nil && n.Shown })
	a.Do("tap row 1", "keyin down", "keyin up")
	a.UntilText("got around down;main up;")
}

// While an input method composes text, its keys are its own
// (TestKeysMarkedTextDarwin).
func TestKeysMarkedText(t *testing.T) {
	a := Run(t, `
		component K {
		  state downs = 0
		  View {
		    keys: [Shortcut(key: "down", run: () => { downs += 1 })]
		    Text("downs {downs}")
		    TextField { placeholder: "f" }
		  }
		}
		app { K() }
	`)
	a.UntilText("downs 0")
	a.Do("tap f", "mark にほ")
	a.Until(func(tr *Tree) bool { _, f := fieldOf(t, tr, "f"); return f["markedText"] == true })
	a.Do("keyin down")
	a.UntilText("downs 0")
	a.Do("unmark", "keyin down")
	a.UntilText("downs 1")
}

// In a panel, a declared Escape is the content's; an Escape no one
// declared puts the panel away, as before (TestKeysEscapeInPanelDarwin).
func TestKeysEscapeInPanel(t *testing.T) {
	// It keeps running: a panel that loses the keyboard (to another test's
	// app) goes away, which would quit an app whose last window it is.
	a := RunWith(t, `
		app {
		  Window {
		    panel: true
		    minWidth: 400
		    K()
		  }
		}
		component K {
		  state escapes = 0
		  Column {
		    padding: 10
		    Text("escapes {escapes}")
		    View {
		      keys: [Shortcut(key: "escape", run: () => { escapes += 1 })]
		      TextField { placeholder: "in" }
		    }
		    TextField { placeholder: "out" }
		  }
		}
	`, Options{Env: []string{shellConfig(t, `{"keepRunning": true}`)}})
	a.UntilText("escapes 0")
	// Shown, as the Dock's icon shows it (it opens hidden while another
	// app is in front, as it would close then).
	a.Do("reopen")
	a.Until(func(tr *Tree) bool { return tr.Main() != nil && tr.Main().Visible })
	a.Do("tap in", "keyin escape")
	tr := a.UntilText("escapes 1")
	if !tr.Main().Visible {
		t.Fatal("a declared Escape put the panel away")
	}
	a.Do("tap out", "keyin escape")
	a.Until(func(tr *Tree) bool { return !tr.Main().Visible })
	if a.Tree().FindText("escapes 1") == nil {
		t.Fatal("the undeclared Escape was taken")
	}
}
