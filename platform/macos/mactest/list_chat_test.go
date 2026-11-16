//go:build darwin

package mactest

import (
	"strconv"
	"testing"
)

// listTop is the top of shown row r ("tops" in the list's JSON), and
// whether it's shown.
func listTop(m map[string]any, r int) (float64, bool) {
	tops, _ := m["tops"].(map[string]any)
	v, ok := tops[strconv.Itoa(r)].(float64)
	return v, ok
}

// listRowHeight is shown row r's height.
func listRowHeight(m map[string]any, r int) float64 {
	hs, _ := m["rowHeights"].(map[string]any)
	v, _ := hs[strconv.Itoa(r)].(float64)
	return v
}

// A plain list's section header stays at its top while its section's rows
// scroll under it, and the next header pushes it up (list_chat_test.go's
// TestListStickyHeaders).
// (A header closure that reads its section's key crashes the core:
// bind_header hands it an accessor where the compiled closure takes a
// string. The headers here say "Section" alone.)
func TestListStickyHeaders(t *testing.T) {
	a := Run(t, `
		app {
		  List {
		    section: i => "{i / 20}"
		    header: s => Text("Section") { height: 30 }
		    for i in 0..<200 key i { Text("Row {i}") { height: 40 } }
		  }
		}
	`)
	tr := a.UntilText("Row 0")
	_, m := listOf(t, tr)
	if num(m, "sections") != 10 || num(m, "rows") != 210 {
		t.Fatalf("%v sections, %v rows", m["sections"], m["rows"])
	}
	// Into section 2: its header (row 42) is pinned at the top. A section
	// is 30 + 20 × 40 points.
	a.Do("scroll " + strconv.Itoa(2*830+100))
	tr = a.Until(func(tr *Tree) bool {
		l, m := listOf(t, tr)
		c := cellsOf(l)[42]
		if c == nil {
			return false
		}
		top, ok := listTop(m, 42)
		vis := nums(m, "visible")
		f := nums(cellState(c), "rowFrame")
		return ok && top < vis[1] && f[1] == vis[1] && cellState(c)["header"] == true && firstTextIn(c) == "Section"
	})
	// Just above section 3: its header pushes section 2's up.
	_, m = listOf(t, tr)
	top63, ok := listTop(m, 63)
	if !ok {
		a.Do("scroll " + strconv.Itoa(3*830-10))
		_, m = listOf(t, a.Tree())
		if top63, ok = listTop(m, 63); !ok {
			t.Fatalf("section 3's header isn't shown:\n%s", tr.Raw())
		}
	}
	a.Do("scroll " + strconv.FormatFloat(top63-10, 'f', -1, 64))
	a.Until(func(tr *Tree) bool {
		l, m := listOf(t, tr)
		c := cellsOf(l)[42]
		top63, ok := listTop(m, 63)
		if c == nil || !ok {
			return false
		}
		f := nums(cellState(c), "rowFrame")
		return f[1] == top63-listRowHeight(m, 42) && nums(m, "visible")[1] == top63-10
	})
}

// A list anchored at its bottom: few rows sit at its bottom, it opens at
// its end and stays there as rows come, and rows added above what it shows
// don't move that (TestListAnchorBottom).
func TestListAnchorBottom(t *testing.T) {
	a := Run(t, `
		component Chat {
		  state items = [1, 2, 3]
		  Column {
		    grow: 1
		    Row {
		      Button("Many") {
		        on press {
		          var v: [int] = []
		          for i in 1..300 { v.push(i) }
		          items = v
		        }
		      }
		      Button("Older") {
		        on press {
		          var v: [int] = []
		          for i in -49..0 { v.push(i) }
		          items = v + items
		        }
		      }
		    }
		    List {
		      grow: 1
		      anchor: .bottom
		      for i in items key i { Text("Row {i}") { height: 40 } }
		    }
		  }
		}
		app { Chat() }
	`)
	tr := a.UntilText("Row 3")
	tr = a.Until(func(tr *Tree) bool {
		_, m := listOf(t, tr)
		top, ok := listTop(m, 0)
		vis := nums(m, "visible")
		return ok && m["bottom"] == true && num(m, "lead") == vis[3]-120 && top == num(m, "lead")
	})
	a.Do("tap Many")
	tr = a.Until(func(tr *Tree) bool {
		_, m := listOf(t, tr)
		vis := nums(m, "visible")
		return num(m, "items") == 300 && vis[1] == num(m, "total")-vis[3] && m["stuck"] == true
	})
	// The user scrolls up: off the end.
	a.Do("scroll 4000")
	tr = a.Until(func(tr *Tree) bool {
		_, m := listOf(t, tr)
		return nums(m, "visible")[1] == 4000 && m["stuck"] == false
	})
	_, m := listOf(t, tr)
	anchor, _ := m["anchor"].(map[string]any)
	if anchor == nil {
		t.Fatalf("no anchor row:\n%s", tr.Raw())
	}
	// Older rows come first: what shows stays.
	a.Do("tap Older")
	tr = a.Until(func(tr *Tree) bool {
		_, m := listOf(t, tr)
		return num(m, "items") == 350
	})
	_, m = listOf(t, tr)
	got, _ := m["anchor"].(map[string]any)
	if got == nil || got["key"] != anchor["key"] || got["dy"] != anchor["dy"] {
		t.Fatalf("row %v at %v shows, was %v at %v", got["key"], got["dy"], anchor["key"], anchor["dy"])
	}
}

// A List's Ref scrolls it to an offset (ScrollToOffset), bound as it gets
// there, and it reports where it is as it scrolls (on scroll).
func TestListScrollTo(t *testing.T) {
	a := Run(t, `
		component L {
		  state y = 0.0
		  let list = Ref()
		  Column {
		    grow: 1
		    Text("y {y.rounded()}")
		    Button("Jump") { on press { list.scrollTo(0, 4000, animated: false) } }
		    List {
		      ref: list
		      grow: 1
		      on scroll(sx, sy) { y = sy }
		      for i in 0..<500 key i { Text("Row {i}") { height: 40 } }
		    }
		  }
		}
		app { L() }
	`)
	a.UntilText("Row 0")
	a.Do("tap Jump")
	a.Until(func(tr *Tree) bool {
		l, m := listOf(t, tr)
		c := cellsOf(l)[100]
		return nums(m, "visible")[1] == 4000 && c != nil && firstTextIn(c) == "Row 100" && findPrefix(tr, "y 4000") != ""
	})
}
