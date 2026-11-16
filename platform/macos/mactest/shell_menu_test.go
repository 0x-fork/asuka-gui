//go:build darwin

package mactest

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// appConfig writes an asuka-app.json for a test's app and returns the
// environment that points the player at it.
func shellConfig(t *testing.T, json string) string {
	p := filepath.Join(t.TempDir(), "asuka-app.json")
	if err := os.WriteFile(p, []byte(json), 0o644); err != nil {
		t.Fatal(err)
	}
	return "ASUKA_APP_CONFIG=" + p
}

// menuBarItem is the menu bar's item path ("Tools", "Export", "PDF"), nil
// if it isn't there.
func shellMenuItem(tr *Tree, path ...string) *MenuItem {
	m := tr.Menu(path[0])
	for _, p := range path[1:] {
		if m == nil {
			return nil
		}
		var next *MenuItem
		for _, it := range m.Items {
			if it.Title == p {
				next = it
				break
			}
		}
		m = next
	}
	return m
}

// treeMap is the tree's JSON as a map (the fields the Tree type has no
// field for: the tray's, the Dock's, the panels').
func shellJSON(t *testing.T, tr *Tree) map[string]any {
	var m map[string]any
	if err := json.Unmarshal([]byte(tr.Raw()), &m); err != nil {
		t.Fatal(err)
	}
	return m
}

// The standard menus and the app's: a menu titled as the app adds its
// items to the app's menu, after About ("Check for Updates…"); one titled
// as a standard one joins it; the others go between View and Window. A
// submenu is an NSMenu of its own, its items' shortcuts key equivalents;
// a disabled item is off, a destructive one red, Quit AppKit's terminate:
// with ⌘Q; a chosen item is its press.
func TestShellMenuBar(t *testing.T) {
	a := RunWith(t, `
		state checked = 0
		state chose = ""
		app {
		  MenuBar {
		    Menu("Notes") {
		      MenuItem("Check for Updates…") { on press { checked += 1 } }
		    }
		    Menu("File") {
		      MenuItem("Export…") {
		        shortcut: Shortcut(key: "e", primary: true, shift: true)
		        on press { chose = "export" }
		      }
		    }
		    Menu("Tools") {
		      MenuItem("Export") {
		        MenuItem("PDF") {
		          shortcut: Shortcut(key: "e", primary: true)
		          on press { chose = "pdf" }
		        }
		        MenuItem("Off") { disabled: true }
		      }
		      MenuItem("Reset") { role: .destructive }
		      MenuItem("Fast") { checked: true }
		      MenuSeparator()
		      MenuItem("") { role: .quit }
		    }
		  }
		  Text("checked {checked} chose {chose}")
		}
	`, Options{Env: []string{shellConfig(t, `{"name": "Notes"}`)}})
	tr := a.Until(func(tr *Tree) bool { return tr.Menu("Tools") != nil })
	if len(tr.MenuBar) == 0 || tr.MenuBar[0].Title != "Notes" {
		t.Fatalf("the app menu: %s", tr.Raw())
	}
	app := tr.MenuBar[0]
	if len(app.Items) < 2 || app.Items[0].Title != "About Notes" || app.Items[1].Title != "Check for Updates…" {
		t.Fatalf("the app menu's items: %+v", app.Items)
	}
	for i, m := range tr.MenuBar {
		if m.Title == "Notes" && i != 0 {
			t.Fatal("the app's menu shows as a menu of its own too")
		}
	}
	var titles []string
	for _, m := range tr.MenuBar {
		titles = append(titles, m.Title)
	}
	if got := strings.Join(titles, ","); got != "Notes,File,Edit,View,Tools,Window" {
		t.Fatalf("the menus: %s", got)
	}
	// The File menu has the app's items first, then Close Window.
	file := tr.Menu("File")
	if file.Items[0].Title != "Export…" || file.Items[0].Key != "e" || file.Items[0].Mods != 1<<17|1<<20 {
		t.Fatalf("File's Export…: %+v", file.Items[0])
	}
	// (AppKit adds Close All, its alternate with ⌥, after it.)
	last := file.Items[len(file.Items)-1]
	if last.Title == "Close All" {
		last = file.Items[len(file.Items)-2]
	}
	if last.Title != "Close Window" || last.Key != "w" {
		t.Fatalf("File's last item: %+v", last)
	}
	pdf := shellMenuItem(tr, "Tools", "Export", "PDF")
	if pdf == nil || pdf.Key != "e" || pdf.Mods != 1<<20 {
		t.Fatalf("PDF: %+v", pdf)
	}
	if off := shellMenuItem(tr, "Tools", "Export", "Off"); off == nil || !off.Disabled {
		t.Fatalf("Off isn't off: %+v", off)
	}
	if r := shellMenuItem(tr, "Tools", "Reset"); r == nil || r.Color == "" {
		t.Fatalf("Reset isn't red: %+v", r)
	}
	if f := shellMenuItem(tr, "Tools", "Fast"); f == nil || !f.Checked {
		t.Fatalf("Fast isn't checked: %+v", f)
	}
	tools := shellMenu(t, tr, "Tools")
	quit := tools[len(tools)-1]
	if quit["title"] != "Quit Notes" || quit["action"] != "terminate:" || quit["key"] != "q" {
		t.Fatalf("Quit: %v", quit)
	}
	// The View menu's Back goes to the window, off with nothing to go back
	// from.
	if b := shellMenuItem(tr, "View", "Back"); b == nil || b.Key != "[" || !b.Disabled {
		t.Fatalf("Back: %+v", b)
	}

	a.Do("menu Check for Updates…")
	a.UntilText("checked 1 chose ")
	a.Do("menu PDF")
	a.UntilText("checked 1 chose pdf")
	a.Do("key cmd+shift+e")
	a.UntilText("checked 1 chose export")
}

// treeMenu is the menu bar's menu titled title as JSON.
func shellMenu(t *testing.T, tr *Tree, title string) []map[string]any {
	for _, m := range shellJSON(t, tr)["menuBar"].([]any) {
		mm := m.(map[string]any)
		if mm["title"] == title {
			var out []map[string]any
			for _, it := range mm["items"].([]any) {
				out = append(out, it.(map[string]any))
			}
			return out
		}
	}
	t.Fatalf("no menu %s", title)
	return nil
}

// The menu bar follows the app's menus: one added makes the bar again.
func TestShellMenuBarChanges(t *testing.T) {
	a := Run(t, `
		state more = false
		app {
		  MenuBar {
		    Menu("Tools") {
		      MenuItem("Show More") { on press { more = true } }
		    }
		    if more {
		      Menu("More") { MenuItem("Again") {} }
		    }
		  }
		  Text("more {more}")
		}
	`)
	a.Until(func(tr *Tree) bool { return tr.Menu("Tools") != nil })
	a.Do("menu Show More")
	tr := a.Until(func(tr *Tree) bool { return tr.Menu("More") != nil })
	if shellMenuItem(tr, "More", "Again") == nil {
		t.Fatalf("More: %+v", tr.Menu("More"))
	}
}

// The standard menus' words are in the language the app shows (AppKit
// doesn't translate a menu made at run time): the pseudo locale's here
// (ASUKA_LOCALE, as asuka dev --lang sets it, which nothing saves), with
// the app's own menus as they are.
func TestShellMenuBarLanguage(t *testing.T) {
	a := RunWith(t, `
		app {
		  MenuBar { Menu("Tools") { MenuItem("Run") {} } }
		  Text("x")
		}
	`, Options{Env: []string{"ASUKA_LOCALE=en-XA"}})
	tr := a.Until(func(tr *Tree) bool { return tr.Menu("[Éðîţ one]") != nil })
	if shellMenuItem(tr, "[Éðîţ one]", "[Çöþý one]") == nil {
		t.Errorf("Edit in en-XA: %+v", tr.Menu("[Éðîţ one]"))
	}
	if tr.Menu("[Ŵîñðöŵ one]") == nil || shellMenuItem(tr, "Tools", "Run") == nil {
		t.Errorf("the menus in en-XA: %s", tr.Raw())
	}
}
