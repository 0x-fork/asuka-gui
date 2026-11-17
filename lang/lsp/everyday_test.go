package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// RFC 0011's constructs in an editor: a mut fn's hover, an extension's
// members (hover, definition, completion after . and where their type
// is expected, rename), and a loop's name (hover, definition, rename).
func TestEverydayLanguage(t *testing.T) {
	root := t.TempDir()
	src := `struct Cart {
  n: int = 0

  mut fn add(k: int) { n += k }
}

extend string {
  /// The text as a URL's path segment.
  fn slug() -> string = lower().fields().joined(separator: "-")
}

extend Color {
  let brand = #7c3aed
}

fn paint(c: Color) -> Color = c

fn run(grid: [[int]]) -> string {
  var cart = Cart()
  cart.add(1)
  for row in grid as rows {
    for c in row {
      if c == 0 { break rows }
    }
  }
  print(paint(.brand))
  return "A B".slug()
}

test "run" {
  expect(run([[1]]) == "a-b")
}
`
	if err := os.WriteFile(filepath.Join(root, "asuka.toml"), []byte("name = \"everyday\"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	main := filepath.Join(root, "main.asuka")
	if err := os.WriteFile(main, []byte(src), 0o644); err != nil {
		t.Fatal(err)
	}
	c := newClient(t, root)
	uri, text := c.open(main)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"cart.a|dd(1)", "mut fn Cart.add(k: int)"},
		{`"A B".sl|ug()`, "fn string.slug() -> string"},
		{`"A B".sl|ug()`, "a URL's path segment"},
		{"paint(.bra|nd)", "brand"},
		{"break ro|ws", "for … as rows"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	for _, tc := range []struct{ at, to string }{
		{`"A B".sl|ug()`, "fn |slug()"},
		{"paint(.bra|nd)", "let |brand"},
		{"break ro|ws", "as |rows"},
	} {
		var locs []Location
		p := tdp(uri, posAt(t, text, tc.at))
		if err := c.call("textDocument/definition", &p, &locs); err != nil {
			t.Fatal(err)
		}
		if want := posAt(t, text, tc.to); len(locs) != 1 || locs[0].Range.Start != want {
			t.Errorf("definition at %s: %+v, want %+v", tc.at, locs, want)
		}
	}
	version := 1
	at := strings.Index(text, `"A B".slug()`) + len(`"A B".`)
	if got := labels(complete(t, c, uri, &version, text, text[:at]+"|")); !contains(got, "slug") || !contains(got, "trim") {
		t.Errorf("after a string's dot: %v", got)
	}
	at = strings.Index(text, "paint(.brand)") + len("paint(.")
	if got := labels(complete(t, c, uri, &version, text, text[:at]+"|")); !contains(got, "brand") || !contains(got, "accent") {
		t.Errorf("where a Color is expected: %v", got)
	}
	for _, tc := range []struct{ at, name, want string }{
		{"break ro|ws", "outer", "for row in grid as outer {"},
		{`"A B".sl|ug()`, "kebab", "fn kebab() -> string"},
	} {
		var edit WorkspaceEdit
		rp := RenameParams{TextDocumentPositionParams: tdp(uri, posAt(t, text, tc.at)), NewName: tc.name}
		if err := c.call("textDocument/rename", &rp, &edit); err != nil {
			t.Fatal(err)
		}
		got := applyEdits(text, edit.Changes[uri])
		if !strings.Contains(got, tc.want) || strings.Count(got, tc.name) != 2 {
			t.Errorf("renamed at %s:\n%s", tc.at, got)
		}
	}
}
