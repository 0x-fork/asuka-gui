//go:build darwin

package mactest

import (
	"testing"
)

// tabScreens are a Tabs' screens, in the order of its children.
func tabScreens(tr *Tree) []*Node {
	tabs := tr.Kind("Tabs")
	if len(tabs) != 1 {
		return nil
	}
	var out []*Node
	for _, c := range tabs[0].Children {
		if c.Kind == "Screen" {
			out = append(out, c)
		}
	}
	return out
}

// A tab's badge is in its label, in parentheses, and VoiceOver hears it
// after the title, as so many items; it changes the label alone, the tab
// kept (TestTabBadge).
func TestNavTabBadge(t *testing.T) {
	a := Run(t, `
		app { Root() }
		component Root {
		  state unread = "3"
		  Column {
		    grow: 1
		    Button("Word") { on press { unread = "New" } }
		    Button("Clear") { on press { unread = "" } }
		    Tabs {
		      grow: 1
		      Tab("Inbox") { badge: unread; Text("inbox page") }
		      Tab("Archive") { Text("archive page") }
		    }
		  }
		}
	`)
	check := func(label, said string) *Tree {
		t.Helper()
		return a.Until(func(tr *Tree) bool {
			s := tabScreens(tr)
			return len(s) == 2 && s[0].Str("tabLabel") == label && s[0].Str("tabSpoken") == said &&
				s[1].Str("tabLabel") == "Archive" && s[1].Str("tabSpoken") == "Archive"
		})
	}
	tr := check("Inbox (3)", "Inbox, 3 items")
	tabs := tr.Kind("Tabs")[0]
	if tabs.Class != "NSTabView" {
		t.Fatalf("Tabs is a %s", tabs.Class)
	}
	item := tabScreens(tr)[0].Num("tabItemID")
	a.Do("tap Word")
	tr = check("Inbox (New)", "Inbox, New")
	if tabScreens(tr)[0].Num("tabItemID") != item {
		t.Fatal("the tab was made again")
	}
	a.Do("tap Clear")
	check("Inbox", "Inbox")
}

// Choosing a tab selects its page, reports it (the core's selected), and
// titles the window after it.
func TestNavTabsSelect(t *testing.T) {
	a := Run(t, `
		app { Root() }
		component Root {
		  state sel = 0
		  Column {
		    grow: 1
		    Text("selected {sel}")
		    Button("Second") { on press { sel = 1 } }
		    Tabs {
		      grow: 1
		      bind selected: sel
		      Tab("Inbox") { Text("inbox page") }
		      Tab("Archive") { Text("archive page") }
		    }
		  }
		}
	`)
	a.UntilText("selected 0")
	tr := a.Until(func(tr *Tree) bool { return tr.Main().Title == "Inbox" })
	if got := tr.Kind("Tabs")[0].Get("tabs"); len(got.([]any)) != 2 {
		t.Fatalf("tabs %v", got)
	}
	// The user's choice.
	a.Do("tab Archive")
	a.UntilText("selected 1")
	tr = a.Until(func(tr *Tree) bool { return tr.Main().Title == "Archive" && tr.Kind("Tabs")[0].Str("selectedTab") == "Archive" })
	if n := tr.FindText("archive page"); n == nil || !n.Shown {
		t.Fatalf("the archive page doesn't show:\n%s", tr.Raw())
	}
	// The app's.
	a.Do("tab Inbox")
	a.UntilText("selected 0")
	a.Do("tap Second")
	a.Until(func(tr *Tree) bool { return tr.Kind("Tabs")[0].Str("selectedTab") == "Archive" && tr.Main().Title == "Archive" })
}

// Tabs reading right to left where AppKit reads left to right: NSTabView
// follows the app's direction, not its own, so the host puts its items in
// the reverse order (locale.go's orderTabs).
func TestNavTabsRightToLeft(t *testing.T) {
	a := Run(t, `
		app { Root() }
		component Root {
		  Tabs {
		    grow: 1
		    layoutDirection: .rightToLeft
		    Tab("One") { Text("one") }
		    Tab("Two") { Text("two") }
		    Tab("Three") { Text("three") }
		  }
		}
	`)
	a.Until(func(tr *Tree) bool {
		ts := tr.Kind("Tabs")
		if len(ts) != 1 {
			return false
		}
		got, _ := ts[0].Get("tabs").([]any)
		return len(got) == 3 && got[0] == "Three" && got[2] == "One" && ts[0].Str("selectedTab") == "One"
	})
}
