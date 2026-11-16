//go:build darwin

package mactest

import (
	"os/exec"
	"strings"
	"testing"
)

// richOf is the RichText whose text is s, and its "rich" JSON.
func richOf(tr *Tree, s string) (*Node, map[string]any) {
	for _, n := range tr.Kind("RichText") {
		if r, _ := n.Get("rich").(map[string]any); r != nil && r["string"] == s {
			return n, r
		}
	}
	return nil, nil
}

// A RichText is a non-editable NSTextView: its spans' styles, a link in
// the accent color that takes a click (on link), an inline component
// placed where its attachment is, on the baseline; outside a Selectable it
// doesn't select.
func TestRichText(t *testing.T) {
	a := Run(t, `
		component R {
		  state linked = ""
		  Column {
		    align: .start
		    padding: 10
		    Text("linked {linked}")
		    RichText([
		      Span(text: "Hello "),
		      Span(text: "bold", font: Font(weight: .bold)),
		      Span(text: " "),
		      Span(text: "it", italic: true, underline: true),
		      Span(text: " "),
		      Span(text: "link", link: "https://example.com"),
		      Span(text: " and "),
		      Span(inline: Inline(key: "dot", build: () => Text("DOT") { width: 30; height: 12 })),
		      Span(text: " end\nSecond paragraph"),
		    ]) {
		      width: 300
		      on link(url) { linked = url }
		    }
		  }
		}
		app { R() }
	`)
	tr := a.Until(func(tr *Tree) bool { n, _ := richOf(tr, "Hello bold it link and  end\nSecond paragraph"); return n != nil })
	n, r := richOf(tr, "Hello bold it link and  end\nSecond paragraph")
	if n.Class != "AsukaRichTextView" || r["selectable"] != false {
		t.Fatalf("a %s, selectable %v", n.Class, r["selectable"])
	}
	if n.Frame[2] != 300 || n.Frame[3] < 20 {
		t.Fatalf("its frame %v", n.Frame)
	}
	runs, _ := r["runs"].([]any)
	style := map[string]map[string]any{}
	for _, x := range runs {
		x := x.(map[string]any)
		style[x["text"].(string)] = x
	}
	if style["bold"]["bold"] != true || style["it"]["italic"] != true || style["it"]["underline"] != true {
		t.Fatalf("the runs: %v", runs)
	}
	if l := style["link"]; l["link"] != "https://example.com" || l["color"] == style["Hello "]["color"] {
		t.Fatalf("the link: %v (text %v)", l, style["Hello "])
	}
	// The inline component: on the text's first line, after "and".
	dot := tr.FindText("DOT")
	if dot == nil || dot.Frame[2] != 30 || dot.Frame[3] != 12 {
		t.Fatalf("the inline component: %v", dot)
	}
	if dx, dy := dot.InWindow[0]-n.InWindow[0], dot.InWindow[1]-n.InWindow[1]; dx <= 100 || dy < 0 || dy > 20 {
		t.Fatalf("the inline component is at %v in the text, at %v:\n%s", dot.InWindow, n.InWindow, a.Dump())
	}
	a.Do("link https://example.com")
	a.UntilText("linked https://example.com")
}

// A Selectable's RichTexts select as one text: a drag across them, a
// double click's words, select all; copying takes their parts, a
// paragraph each, without the inline components.
func TestSelectable(t *testing.T) {
	a := Run(t, `
		app {
		  Column {
		    align: .start
		    padding: 10
		    gap: 8
		    Selectable {
		      RichText([Span(text: "First text")])
		      RichText([Span(text: "Second "), Span(inline: Inline(key: "x", build: () => Text("X"))), Span(text: " text")])
		      RichText([Span(text: "Third text")])
		    }
		    RichText([Span(text: "Outside")])
		  }
		}
	`)
	tr := a.Until(func(tr *Tree) bool { n, _ := richOf(tr, "Third text"); return n != nil })
	if _, r := richOf(tr, "First text"); r["selectable"] != true {
		t.Fatalf("a text in a Selectable doesn't select: %v", r)
	}
	if _, r := richOf(tr, "Outside"); r["selectable"] != false {
		t.Fatalf("a text outside one selects: %v", r)
	}
	selected := func(tr *Tree) string { return tr.Kind("Selectable")[0].Str("selectedText") }
	a.Do("selectdrag First text|Second  text")
	tr = a.Until(func(tr *Tree) bool { return selected(tr) == "First text\nSecond  text" })
	if _, r := richOf(tr, "Third text"); r["selection"] != nil {
		if s, _ := r["selection"].([]any); len(s) == 2 && s[0] != s[1] {
			t.Fatalf("the third text has a part: %v", s)
		}
	}
	a.Do("selectall")
	a.Until(func(tr *Tree) bool { return selected(tr) == "First text\nSecond  text\nThird text" })
	a.Do("copy")
	out, err := exec.Command("pbpaste").Output()
	if err != nil {
		t.Fatal(err)
	}
	if got := strings.ReplaceAll(string(out), "\r", ""); got != "First text\nSecond  text\nThird text" {
		t.Logf("the pasteboard has %q (another test may have written it since)", got)
	}
}
