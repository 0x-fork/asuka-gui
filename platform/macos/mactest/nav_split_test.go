//go:build darwin

package mactest

import (
	"testing"
)

const splitApp = `
	app { Root() }
	component Root {
	  state dest = 0
	  state unread = "3"
	  Column {
	    grow: 1
	    Row {
	      gap: 8
	      Text("dest {dest}")
	      Button("Twelve") { on press { unread = "12" } }
	      Button("Clear") { on press { unread = "" } }
	    }
	    Split("Gallery") {
	      grow: 1
	      bind selected: dest
	      Destination("Text") { icon: "textformat"; section: "Views"; Text("text page") }
	      Destination("Charts") { icon: "chart.bar"; section: "Views"; badge: unread; Text("charts page") }
	    }
	  }
	}
`

func splitRows(tr *Tree) (*Node, []map[string]any) {
	sp := tr.Kind("Split")
	if len(sp) != 1 {
		return nil, nil
	}
	var rows []map[string]any
	for _, r := range sp[0].Get("rows").([]any) {
		rows = append(rows, r.(map[string]any))
	}
	return sp[0], rows
}

// A split view is a split view controller: its sidebar lists the
// destinations under their sections, the detail Stack fills its content,
// and choosing a row shows that destination (TestSplitView).
func TestNavSplitView(t *testing.T) {
	a := Run(t, splitApp)
	tr := a.Until(func(tr *Tree) bool {
		s, rows := splitRows(tr)
		return s != nil && len(rows) == 3 && s.Num("selectedRow") == 1
	})
	s, rows := splitRows(tr)
	if rows[0]["group"] != true || rows[0]["title"] != "Views" || rows[1]["title"] != "Text" || rows[2]["title"] != "Charts" {
		t.Fatalf("rows %v", rows)
	}
	if s.Bool("sidebarIsSidebar") != true || s.Str("title") != "Gallery" {
		t.Fatalf("not a sidebar titled Gallery: %v %q", s.Get("sidebarIsSidebar"), s.Str("title"))
	}
	if n := tr.FindText("text page"); n == nil || !n.Shown {
		t.Fatal("the first destination doesn't show")
	}
	// The user chooses Charts: the core hears it, and shows it.
	a.Do("tap Charts")
	a.UntilText("dest 1")
	a.Until(func(tr *Tree) bool {
		n := tr.FindText("charts page")
		s, _ := splitRows(tr)
		return n != nil && n.Shown && s.Num("selectedRow") == 2
	})
}

// A destination's badge is a count at the row's trailing end, VoiceOver
// hearing it as so many items; it changes in place, the row's view and the
// selection kept (TestSidebarBadge).
func TestNavSidebarBadge(t *testing.T) {
	a := Run(t, splitApp)
	tr := a.Until(func(tr *Tree) bool {
		_, rows := splitRows(tr)
		return len(rows) == 3 && rows[2]["shownBadge"] == "3"
	})
	_, rows := splitRows(tr)
	charts := rows[2]
	if charts["badgeLabel"] != "3 items" {
		t.Fatalf("VoiceOver hears %q", charts["badgeLabel"])
	}
	bx, _ := pair(charts["badgeFrame"])
	bw, _ := size(charts["badgeFrame"])
	tx, _ := pair(charts["titleFrame"])
	tw, _ := size(charts["titleFrame"])
	if cw := charts["cellWidth"].(float64); bx+bw > cw || bx < tx+tw {
		t.Fatalf("badge at %v+%v, title at %v+%v, in %v", bx, bw, tx, tw, cw)
	}
	if _, ok := rows[1]["shownBadge"]; ok {
		t.Fatal("Text has a badge")
	}
	cell := charts["cellID"]
	a.Do("tap Twelve")
	tr = a.Until(func(tr *Tree) bool {
		_, rows := splitRows(tr)
		return len(rows) == 3 && rows[2]["shownBadge"] == "12"
	})
	s, rows := splitRows(tr)
	if rows[2]["cellID"] != cell {
		t.Fatal("the row wasn't changed in place")
	}
	if s.Num("selectedRow") != 1 {
		t.Fatal("the selection moved")
	}
	a.Do("tap Clear")
	a.Until(func(tr *Tree) bool {
		_, rows := splitRows(tr)
		_, has := rows[2]["shownBadge"]
		return len(rows) == 3 && !has
	})
}
