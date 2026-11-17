package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The forms of rfcs/0012 hover as they're declared: peek, a state that
// follows (what it follows), a state's own name in its initializer.
func TestFollowsHover(t *testing.T) {
	root := t.TempDir()
	src := `component Picker(items: [string]) {
  state draft = items.count()
  state top(items): int = 0
  state picked: string? = if let p = picked, items.contains(p) { p } else { items.first() }
  let first = peek(items.count())

  Text("{draft} {top} {picked ?? "-"} {first}")
}
`
	if err := os.WriteFile(filepath.Join(root, "asuka.toml"), []byte("name = \"follows\"\n"), 0o644); err != nil {
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
		{"pe|ek(items", "fn peek(value: T) -> T"},
		{"pe|ek(items", "read without following it"},
		{"state dr|aft", "state draft: int = items.count()"},
		{"state t|op", "state top(items): int"},
		{"let p = pic|ked", "the state's current value"},
	} {
		var h *Hover
		p := tdp(uri, posAt(t, text, tc.at))
		if err := c.call("textDocument/hover", &p, &h); err != nil {
			t.Fatal(err)
		}
		if h == nil || !strings.Contains(h.Contents.Value, tc.want) {
			t.Errorf("%s: hover %+v, want %q", tc.at, h, tc.want)
		}
	}
}
