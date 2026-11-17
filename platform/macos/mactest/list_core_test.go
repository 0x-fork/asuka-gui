//go:build darwin

package mactest

import (
	"fmt"
	"strings"
	"testing"
)

// listOf is the tree's only List's state (the host's "list" JSON).
func listOf(t *testing.T, tr *Tree) (*Node, map[string]any) {
	t.Helper()
	ls := tr.Kind("List")
	if len(ls) == 0 {
		t.Fatalf("no List:\n%s", tr.Raw())
	}
	m, _ := ls[0].Get("list").(map[string]any)
	if m == nil {
		t.Fatalf("the List has no list state:\n%s", tr.Raw())
	}
	return ls[0], m
}

func num(m map[string]any, k string) float64 {
	f, _ := m[k].(float64)
	return f
}

func nums(m map[string]any, k string) []float64 {
	a, _ := m[k].([]any)
	out := make([]float64, len(a))
	for i, x := range a {
		out[i], _ = x.(float64)
	}
	return out
}

// cellsOf are a list's bound cells shown, by row.
func cellsOf(l *Node) map[int]*Node {
	out := map[int]*Node{}
	for _, c := range l.Children {
		if c.Kind == "Cell" {
			out[c.Row] = c
		}
	}
	return out
}

// cellState is a cell's "cell" JSON.
func cellState(c *Node) map[string]any {
	m, _ := c.Get("cell").(map[string]any)
	return m
}

// firstTextIn is the text of the first Text or RichText under n.
func firstTextIn(n *Node) string {
	if n.Kind == "Text" {
		return n.Text
	}
	for _, c := range n.Children {
		if s := firstTextIn(c); s != "" {
			return s
		}
	}
	return ""
}

// A list of 10,000 rows binds only what shows (and a screen's worth
// around it), its content is as tall as all its rows, and scrolled to the
// middle, the rows there are bound.
func TestListBindsOnlyWhatShows(t *testing.T) {
	a := Run(t, `
		app {
		  List {
		    for i in 0..<10000 key i {
		      View { height: 40; Text("Row {i}") }
		    }
		  }
		}
	`)
	tr := a.UntilText("Row 0")
	l, m := listOf(t, tr)
	if n := num(m, "shown"); n == 0 || n > 60 {
		t.Fatalf("%v rows shown of 10,000", n)
	}
	if total := num(m, "total"); total < 10000*40-1 {
		t.Fatalf("the content is %.0f tall", total)
	}
	if n := len(cellsOf(l)); n == 0 || n > 60 {
		t.Fatalf("%d cells in the tree", n)
	}
	a.Do("scroll 200000")
	tr = a.Until(func(tr *Tree) bool {
		l, _ := listOf(t, tr)
		c := cellsOf(l)[5000]
		return c != nil && firstTextIn(c) == "Row 5000"
	})
	l, m = listOf(t, tr)
	if n := num(m, "shown"); n == 0 || n > 60 {
		t.Fatalf("%v rows shown at 200,000pt", n)
	}
	if c := cellsOf(l)[0]; c != nil {
		t.Fatal("row 0 is still shown at 200,000pt")
	}
}

// An inset grouped list's rows are inset by the core's inset (macOS's
// 20pt), on a rounded box behind them; the core lays them out at the cells'
// width.
func TestListInsetGrouped(t *testing.T) {
	a := Run(t, `
		app {
		  List {
		    appearance: .insetGrouped
		    for i in [1, 2, 3] key i { Text("Row {i}") { padding: 10 } }
		  }
		}
	`)
	tr := a.UntilText("Row 3")
	l, m := listOf(t, tr)
	width := num(m, "width")
	if m["grouped"] != true || num(m, "inset") != 20 || num(m, "shown") != 3 {
		t.Fatalf("grouped %v inset %v, %v rows", m["grouped"], m["inset"], m["shown"])
	}
	c := cellsOf(l)[0]
	f := nums(cellState(c), "rowFrame")
	if f[0] != 20 || f[1] != 20 || f[2] != width-40 {
		t.Fatalf("the first row is at %v, the list %v wide", f, width)
	}
	if txt := tr.Find("Text", "Row 1"); txt == nil || txt.Frame[2] != width-40 {
		t.Fatalf("the row's text is %v, its cell %v wide", txt.Frame, width-40)
	}
	g := nums(m, "group")
	tops := m["tops"].(map[string]any)
	top0, _ := tops["0"].(float64)
	top2, _ := tops["2"].(float64)
	h2 := m["rowHeights"].(map[string]any)["2"].(float64)
	if len(g) != 4 || g[0] != 20 || g[1] != top0 || g[3] != top2+h2-top0 {
		t.Fatalf("the group's box is %v (rows at %v, %v, the last %v tall)", g, top0, top2, h2)
	}
	// Separators between the rows, not below the last.
	if s := cellState(cellsOf(l)[0])["separator"]; s != true {
		t.Fatalf("the first row's separator: %v", s)
	}
	if s := cellState(cellsOf(l)[2])["separator"]; s != false {
		t.Fatalf("the last row's separator: %v", s)
	}
}

// A bare list draws no separators.
func TestListBare(t *testing.T) {
	a := Run(t, `
		app {
		  List {
		    appearance: .bare
		    for i in [1, 2, 3] key i { Text("Row {i}") { padding: 10 } }
		  }
		}
	`)
	tr := a.UntilText("Row 3")
	l, _ := listOf(t, tr)
	for r, c := range cellsOf(l) {
		if s, ok := cellState(c)["separator"]; ok && s == true {
			t.Fatalf("row %d has a separator", r)
		}
	}
}

// A grid: each row holds Columns cells, each (width - gap × (columns - 1))
// / columns wide, the gap below each row but the last.
func TestListGrid(t *testing.T) {
	a := Run(t, `
		app {
		  List {
		    columns: 3
		    gap: 10
		    for i in 0..<9 key i { View { height: 50; Text("Cell {i}") } }
		  }
		}
	`)
	tr := a.UntilText("Cell 8")
	l, m := listOf(t, tr)
	if num(m, "rows") != 3 || num(m, "columns") != 3 {
		t.Fatalf("%v rows of %v columns", m["rows"], m["columns"])
	}
	width := num(m, "width")
	cw := (width - 20) / 3
	var xs []float64
	for _, c := range l.Children {
		if c.Kind == "Cell" && c.Row == 0 {
			xs = append(xs, c.Frame[0])
			if c.Frame[2] != cw {
				t.Fatalf("a cell is %v wide, want %v", c.Frame[2], cw)
			}
		}
	}
	if len(xs) != 3 || xs[0] != 0 || xs[1] != cw+10 || xs[2] != 2*(cw+10) {
		t.Fatalf("the first row's cells are at %v (%v wide each)", xs, cw)
	}
	tops := m["tops"].(map[string]any)
	if t0, t1 := tops["0"].(float64), tops["1"].(float64); t1-t0 != 60 {
		t.Fatalf("rows at %v and %v: want 50 and the gap", t0, t1)
	}
}

// A row's swipe actions are its context menu, leading then trailing, a
// destructive one red; choosing one runs it and reports it.
func TestListSwipeActionsMenu(t *testing.T) {
	a := Run(t, `
		component Rows {
		  state did = "none"
		  Column {
		    grow: 1
		    Text("did {did}")
		    List {
		      grow: 1
		      leadingSwipeActions: i => [.button(title: "Pin", run: () => { did = "pin {i}" })]
		      swipeActions: i => [
		        .button(title: "Archive", run: () => { did = "archive {i}" }),
		        .button(title: "Delete", role: .destructive, run: () => { did = "delete {i}" }),
		      ]
		      on action(i, act) { did = did + " ({act})" }
		      for i in [1, 2] key i { Text("Row {i}") { padding: 10 } }
		    }
		  }
		}
		app { Rows() }
	`)
	tr := a.UntilText("Row 2")
	l, _ := listOf(t, tr)
	menu, _ := cellState(cellsOf(l)[0])["contextMenu"].([]any)
	var titles []string
	for _, it := range menu {
		titles = append(titles, it.(map[string]any)["title"].(string))
	}
	if strings.Join(titles, ",") != "Pin,Archive,Delete" {
		t.Fatalf("the row's menu is %q:\n%s", titles, tr.Raw())
	}
	if c, _ := menu[2].(map[string]any)["color"].(string); c == "" {
		t.Fatal("the destructive action isn't red")
	}
	a.Do("rowaction Row 2|Delete")
	a.UntilText("did delete 1 (1)")
	a.Do("rowaction Row 1|Pin")
	a.UntilText("did pin 0 (-1)")
}

// A selecting list: a click on a row selects or deselects it (RowSelected),
// highlighted while it's selected; ⌘-click on a row of a list that isn't
// selecting begins selecting there (RowSelectBegin). No context menu while
// selecting.
func TestListSelecting(t *testing.T) {
	a := Run(t, `
		component Rows {
		  state selecting = false
		  state chosen: [int] = []
		  Column {
		    grow: 1
		    Text("chosen {chosen.count()} selecting {selecting}")
		    List {
		      grow: 1
		      selecting: selecting
		      selected: i => chosen.contains(i)
		      swipeActions: i => [.button(title: "Archive", run: () => {})]
		      on select(i, on) {
		        if on { chosen.push(i) } else { chosen.removeWhere(c => c == i) }
		      }
		      on selectBegin(i) {
		        selecting = true
		        chosen.push(i)
		      }
		      for i in [1, 2, 3] key i { Text("Row {i}") { padding: 10 } }
		    }
		  }
		}
		app { Rows() }
	`)
	a.UntilText("Row 3")
	// Not selecting: a click doesn't select.
	a.Do("click Row 1")
	a.UntilText("chosen 0 selecting false")
	a.Do("cmdclick Row 2")
	tr := a.UntilText("chosen 1 selecting true")
	l, m := listOf(t, tr)
	if m["selecting"] != true {
		t.Fatal("the list isn't selecting")
	}
	a.Until(func(tr *Tree) bool {
		l, _ := listOf(t, tr)
		return cellState(cellsOf(l)[1])["highlighted"] == true
	})
	if _, ok := cellState(cellsOf(l)[0])["contextMenu"].([]any); ok {
		if menu := cellState(cellsOf(l)[0])["contextMenu"].([]any); len(menu) != 0 {
			t.Fatalf("a selecting list's row has a context menu: %v", menu)
		}
	}
	a.Do("click Row 3")
	a.UntilText("chosen 2 selecting true")
	a.Do("click Row 2")
	tr = a.UntilText("chosen 1 selecting true")
	tr = a.Until(func(tr *Tree) bool {
		l, _ := listOf(t, tr)
		c := cellsOf(l)
		return cellState(c[1])["highlighted"] == false && cellState(c[2])["highlighted"] == true
	})
	_ = tr
}

// Pulled past its top beyond 60 points and let go, a List refreshes: a
// spinner above the content (its top content inset) until refreshing goes
// false. Less doesn't.
func TestListPullToRefresh(t *testing.T) {
	a := Run(t, `
		component Rows {
		  state refreshing = false
		  state count = 0
		  Column {
		    grow: 1
		    Text("refreshed {count}")
		    Button("Done") { on press { refreshing = false } }
		    List {
		      grow: 1
		      refreshing: refreshing
		      on refresh {
		        refreshing = true
		        count += 1
		      }
		      for i in [1, 2, 3] key i { Text("Row {i}") { padding: 10 } }
		    }
		  }
		}
		app { Rows() }
	`)
	a.UntilText("Row 3")
	a.Do("pull 30")
	tr := a.UntilText("refreshed 0")
	l, _ := listOf(t, tr)
	if r, _ := l.Get("refresh").(map[string]any); r == nil || r["refreshing"] != false {
		t.Fatalf("after a short pull: %v", l.Get("refresh"))
	}
	a.Do("pull 80")
	tr = a.UntilText("refreshed 1")
	l, _ = listOf(t, tr)
	r, _ := l.Get("refresh").(map[string]any)
	if r["refreshing"] != true || r["insetTop"] != 40.0 || r["spinner"] != true {
		t.Fatalf("refreshing: %v", r)
	}
	a.Do("tap Done")
	a.Until(func(tr *Tree) bool {
		l, _ := listOf(t, tr)
		r, _ := l.Get("refresh").(map[string]any)
		return r["refreshing"] == false && r["insetTop"] == 0.0 && r["spinner"] == false
	})
}

// A section's header is a row of its own; with its rows it sits in a
// section's box of an inset grouped list.
// (A header closure that reads its section's key crashes the core:
// bind_header hands it an accessor where the compiled closure takes a
// string. The headers here say "Section" alone.)
func TestListSectionsGrouped(t *testing.T) {
	a := Run(t, `
		let names = ["A 1", "A 2", "B 1", "B 2"]
		app {
		  List {
		    appearance: .insetGrouped
		    section: i => names[i].slice(0, 1)
		    header: s => Text("Section") { padding: 6 }
		    for n in names key n { Text(n) { padding: 10 } }
		  }
		}
	`)
	tr := a.UntilText("B 2")
	l, m := listOf(t, tr)
	if num(m, "sections") != 2 || num(m, "rows") != 6 || num(m, "groups") != 2 {
		t.Fatalf("%v sections, %v rows, %v boxes", m["sections"], m["rows"], m["groups"])
	}
	cells := cellsOf(l)
	if cellState(cells[0])["header"] != true || firstTextIn(cells[0]) != "Section" {
		t.Fatalf("row 0: %v %q", cellState(cells[0]), firstTextIn(cells[0]))
	}
	if cellState(cells[3])["header"] != true || firstTextIn(cells[3]) != "Section" {
		t.Fatalf("row 3: %v %q", cellState(cells[3]), firstTextIn(cells[3]))
	}
	if s := fmt.Sprint(cellState(cells[1])["separator"], cellState(cells[2])["separator"]); s != "true false" {
		t.Fatalf("separators in section A: %s", s)
	}
}
