//go:build darwin

package mactest

import (
	"fmt"
	"testing"
)

// toolbarLabels are the labels of a window's toolbar items that have one
// (the flexible space has none), in the toolbar's order.
func toolbarLabels(w *Window) []string {
	var out []string
	items, _ := w.Props["toolbarItems"].([]any)
	for _, x := range items {
		it := x.(map[string]any)
		if l, _ := it["label"].(string); l != "" {
			out = append(out, l)
		}
	}
	return out
}

// toolbarItem is the item of w's toolbar labeled label, nil if none.
func toolbarItem(w *Window, label string) map[string]any {
	items, _ := w.Props["toolbarItems"].([]any)
	for _, x := range items {
		if it := x.(map[string]any); it["label"] == label {
			return it
		}
	}
	return nil
}

// In this language a Stack's own child is built before the stack has it
// (pushing from it finds no stack, and the core wraps a Screen in a screen
// of its own): a Split's destination is its stack's first screen, built
// by the stack.
const stackApp = `
	app { Split { Destination("Home") { Home() } } }
	component Home {
	  Screen {
	    title: "Home"
	    Column {
	      padding: 20
	      Text("home page")
	      Button("Push") { on press { push(Detail()) } }
	    }
	  }
	}
	component Detail {
	  Screen {
	    title: "Detail"
	    Column {
	      padding: 20
	      Text("detail page")
	    }
	  }
	}
`

// A push slides the screen in and titles the window after it, the toolbar
// getting a back button; back (the button, ⌘[) pops it: the core hears
// Popped and destroys it, and the title and toolbar follow
// (TestStackPushAndBack).
func TestNavStackPushAndBack(t *testing.T) {
	a := Run(t, stackApp)
	tr := a.Until(func(tr *Tree) bool { return tr.FindText("home page") != nil && tr.Main().Title == "Home" })
	home := tr.Main().Title
	if toolbarItem(tr.Main(), "Back") != nil {
		t.Fatal("a back button with nothing to go back to")
	}
	push := func() {
		t.Helper()
		a.Do("tap Push")
		a.Until(func(tr *Tree) bool {
			n := tr.FindText("detail page")
			s := tr.Kind("Stack")
			return tr.Main().Title == "Detail" && n != nil && n.Shown && len(s) == 1 && s[0].Num("liveScreens") == 2 &&
				toolbarItem(tr.Main(), "Back") != nil
		})
	}
	popped := func(how string) {
		t.Helper()
		tr := a.Until(func(tr *Tree) bool {
			s := tr.Kind("Stack")
			return tr.Main().Title == home && tr.FindText("detail page") == nil && len(s) == 1 && s[0].Num("liveScreens") == 1
		})
		if toolbarItem(tr.Main(), "Back") != nil {
			t.Fatalf("%s: the back button stayed", how)
		}
		if n := tr.FindText("home page"); n == nil || !n.Shown {
			t.Fatalf("%s: the home screen doesn't show", how)
		}
	}
	push()
	back := toolbarItem(a.Tree().Main(), "Back")
	if back["navigational"] != true || back["hasImage"] != true {
		t.Fatalf("the back button: %v", back)
	}
	// The toolbar's back button.
	a.Do("tap Back")
	popped("the back button")
	// The View menu's Back, ⌘[.
	push()
	a.Do("key cmd+[")
	popped("⌘[")
	// The automation's back.
	push()
	a.Do("back")
	popped("back")
}

// A screen's bar buttons, then its Toolbar's items, then its Primary last
// and prominent are the window's toolbar items; each reports its own
// Action; a disabled one is dimmed; one with a menu opens it, its items
// reporting theirs (TestScreenToolbarItems, TestBarButtonMenu).
func TestNavScreenToolbar(t *testing.T) {
	a := Run(t, `
		state picked: [string] = []
		app { Split { Destination("Inbox") { Home() } } }
		component Home {
		  Screen {
		    title: "Inbox"
		    actions {
		      Button("Edit") { on press { picked.push("edit") } }
		      Button("Add") {
		        icon: "plus"
		        menu: [
		          .button(title: "Note", run: () => { picked.push("note") }),
		          .button(title: "Folder", run: () => { picked.push("folder") }),
		        ]
		      }
		    }
		    toolbar {
		      Button("Filter") { icon: "line.3.horizontal.decrease"; on press { picked.push("filter") } }
		      Button("Sort") {
		        icon: "arrow.up.arrow.down"
		        menu: [
		          .button(title: "Date", run: () => { picked.push("date") }),
		          .button(title: "Sender", run: () => { picked.push("sender") }),
		        ]
		      }
		      Button("Delete all") { disabled: true; on press { picked.push("delete") } }
		    }
		    primary {
		      Button("Compose") { icon: "square.and.pencil"; on press { picked.push("compose") } }
		    }
		    Text("picked {picked.joined(",")}")
		  }
		}
	`)
	want := fmt.Sprint([]string{"Edit", "Add", "Filter", "Sort", "Delete all", "Compose"})
	tr := a.Until(func(tr *Tree) bool { return fmt.Sprint(toolbarLabels(tr.Main())) == want })
	w := tr.Main()
	if w.Title != "Inbox" {
		t.Fatalf("title %q", w.Title)
	}
	if it := toolbarItem(w, "Delete all"); it["enabled"] != false {
		t.Fatal("Delete all is enabled")
	}
	if it := toolbarItem(w, "Compose"); it["hasImage"] != true {
		t.Fatal("Compose has no symbol")
	} else if p, ok := it["prominent"]; ok && p != true {
		t.Fatal("Compose isn't prominent")
	}
	if it := toolbarItem(w, "Filter"); it["prominent"] == true {
		t.Fatal("Filter is prominent")
	}
	if m, _ := toolbarItem(w, "Add")["menu"].([]any); fmt.Sprint(menuTitles(m)) != "[Note Folder]" {
		t.Fatalf("Add's menu %v", menuTitles(m))
	}
	if err := a.Try("tap Delete all"); err == "" {
		t.Fatal("a disabled bar button was pressed")
	}
	a.Do("tap Edit", "tap Filter", "tap Compose", "pick Sender", "pick Folder")
	a.UntilText("picked edit,filter,compose,sender,folder")
}

// A screen's search field is the toolbar's: typing writes its text,
// writing the text from code changes the field, and return submits.
func TestNavScreenSearch(t *testing.T) {
	a := Run(t, `
		state query = ""
		state submitted = 0
		app { Split { Destination("Inbox") { Home() } } }
		component Home {
		  Screen {
		    title: "Notes"
		    bind search: query
		    on searchSubmit { submitted += 1 }
		    Column {
		      padding: 20
		      Text("query {query} submitted {submitted}")
		      Button("Clear") { on press { query = "" } }
		      Button("Set") { on press { query = "milk" } }
		    }
		  }
		}
	`)
	a.Until(func(tr *Tree) bool { return searchItem(tr.Main()) != nil })
	a.Do("search eggs")
	a.UntilText("query eggs submitted 0")
	a.Do("tap Set")
	a.Until(func(tr *Tree) bool { s := searchItem(tr.Main()); return s != nil && s["search"] == "milk" })
	a.Do("searchsubmit")
	a.UntilText("query milk submitted 1")
}

// searchItem is w's toolbar's search item, nil if none.
func searchItem(w *Window) map[string]any {
	items, _ := w.Props["toolbarItems"].([]any)
	for _, x := range items {
		if it := x.(map[string]any); it["id"] == "asuka.search" {
			return it
		}
	}
	return nil
}
