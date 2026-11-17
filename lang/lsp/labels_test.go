package lsp

import (
	"path/filepath"
	"strings"
	"testing"
)

// An argument's label names a parameter of what it's passed to: an
// emit's event, a style, a function, whichever element is around it.
func TestLabels(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `style chip(tint: Color = .blue) { color: tint }

component Row(title: string) {
  event archive(id: string)

  View {
    on press { emit archive(id: title) }
    Text(title) { style: chip(tint: .red) }
  }
}

app { Row("a") { on archive(id) { print(id) } } }
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"emit archive(i|d: title)", "(parameter) id: string"},
		{"emit archive(i|d: title)", "Parameter of event `archive` of `Row`."},
		{"event archive(i|d: string)", "(parameter) id: string"},
		{"chip(ti|nt: .red)", "(parameter) tint: Color"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	for _, tc := range []struct{ at, decl string }{
		{"emit archive(i|d: title)", "event archive(|id: string)"},
		{"chip(ti|nt: .red)", "style chip(|tint"},
	} {
		var locs []Location
		p := tdp(uri, posAt(t, text, tc.at))
		if err := c.call("textDocument/definition", &p, &locs); err != nil {
			t.Fatal(err)
		}
		if want := posAt(t, text, tc.decl); len(locs) != 1 || locs[0].Range.Start != want {
			t.Errorf("definition at %s: %+v, want %+v", tc.at, locs, want)
		}
	}

	// Renaming the event's parameter renames the emit's label; the
	// handler's own name for it stays.
	var edit WorkspaceEdit
	rp := RenameParams{TextDocumentPositionParams: tdp(uri, posAt(t, text, "event archive(i|d")), NewName: "key"}
	if err := c.call("textDocument/rename", &rp, &edit); err != nil {
		t.Fatal(err)
	}
	got := applyEdits(text, edit.Changes[uri])
	if !strings.Contains(got, "event archive(key: string)") || !strings.Contains(got, "emit archive(key: title)") || !strings.Contains(got, "on archive(id) { print(id) }") {
		t.Errorf("renamed:\n%s", got)
	}

	// A label that names nothing has no hover: not the element's around
	// it.
	version := 1
	broken := strings.Replace(text, "emit archive(id: title)", "emit archive(nope: title)", 1)
	version++
	c.change(uri, version, broken)
	if got := hoverAt(t, c, uri, broken, "emit archive(no|pe: title)"); got != "" {
		t.Errorf("hover on a label that names nothing: %q", got)
	}
}
