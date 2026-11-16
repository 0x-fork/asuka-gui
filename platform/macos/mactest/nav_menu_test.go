//go:build darwin

package mactest

import (
	"fmt"
	"testing"
)

// menuTitles lists a menu's items: a separator as "-", a disabled item in
// brackets, a checked one with a ✓, a red one with a !, a submenu with its
// items after a ">".
func menuTitles(items []any) []string {
	var out []string
	for _, x := range items {
		it := x.(map[string]any)
		if it["separator"] == true {
			out = append(out, "-")
			continue
		}
		s, _ := it["title"].(string)
		if it["disabled"] == true {
			s = "[" + s + "]"
		}
		if it["checked"] == true {
			s += "✓"
		}
		if c, _ := it["color"].(string); c != "" && c != "#000000ff" && c != "#ffffffff" {
			s += "!"
		}
		if sub, ok := it["items"].([]any); ok {
			s += fmt.Sprint(">", menuTitles(sub))
		}
		out = append(out, s)
	}
	return out
}

// A button's menu shows its items' sections, submenus, checkmarks, disabled
// and destructive items; a nested item's click reports its number; a
// checked item follows its state (TestRichMenu).
func TestNavRichMenu(t *testing.T) {
	a := Run(t, `
		app { Root() }
		component Root {
		  state sort = "name"
		  state picked = ""
		  Column {
		    padding: 20
		    Text("sort {sort} picked {picked}")
		    Button("More") {
		      menu: [
		        .button(title: "Copy", run: () => { picked = "copy" }),
		        .section(title: "Sort by", items: [
		          .button(title: "Name", checked: sort == "name", run: () => { sort = "name" }),
		          .button(title: "Date", checked: sort == "date", run: () => { sort = "date" }),
		        ]),
		        .submenu(title: "Move to", items: [.button(title: "Archive", run: () => { picked = "archive" })]),
		        .button(title: "Delete", role: .destructive, disabled: true, run: () => { picked = "delete" }),
		      ]
		    }
		  }
		}
	`)
	menu := func(tr *Tree) string {
		b := tr.Find("Button", "More")
		if b == nil {
			return ""
		}
		m, _ := b.Get("menu").([]any)
		return fmt.Sprint(menuTitles(m))
	}
	// macOS 14's section header is an item of its own, its title the
	// section's; a submenu's items print in brackets (fmt's []string), a
	// disabled item's title too.
	want := func(name, date string) string {
		return fmt.Sprint([]string{"Copy", "-", "Sort by", "Name" + name, "Date" + date, "-", "Move to>[Archive]", "[Delete]!"})
	}
	tr := a.UntilText("sort name picked ")
	if got := menu(tr); got != want("✓", "") {
		t.Fatalf("menu %s", got)
	}
	// The submenu's item, chosen: its number reaches the core.
	a.Do("pick Archive")
	a.UntilText("sort name picked archive")
	// A checked item follows its state.
	a.Do("pick Date")
	a.UntilText("sort date picked archive")
	a.Until(func(tr *Tree) bool { return menu(tr) == want("", "✓") })
	// A disabled item can't be chosen.
	if err := a.Try("pick Delete"); err == "" {
		t.Fatal("a disabled item was chosen")
	}
}

// A view's context menu (Menu's longPress) is its view's menu for a
// secondary click; a tap menu pops up from its button.
func TestNavContextMenu(t *testing.T) {
	a := Run(t, `
		app { Root() }
		component Root {
		  state picked = ""
		  Column {
		    padding: 20
		    Text("picked {picked}")
		    Menu {
		      longPress: true
		      actions: [
		        .button(title: "Reply", run: () => { picked = "reply" }),
		        .button(title: "Delete…", role: .destructive, run: () => { picked = "delete" }),
		      ]
		      View {
		        padding: 16
		        background: .secondaryBackground
		        Text("Long-press here")
		      }
		    }
		  }
		}
	`)
	tr := a.UntilText("picked ")
	var found []string
	for _, n := range tr.All() {
		if m, ok := n.Get("menu").([]any); ok {
			found = menuTitles(m)
		}
	}
	if fmt.Sprint(found) != fmt.Sprint([]string{"Reply", "Delete…!"}) {
		t.Fatalf("context menu %v", found)
	}
	a.Do("pick Delete…")
	a.UntilText("picked delete")
}
