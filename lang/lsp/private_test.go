package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// A type's private members complete in its file only; another file of
// the package gets the rest. A private declaration hovers as one.
func TestPrivateMembers(t *testing.T) {
	root := t.TempDir()
	files := map[string]string{
		"asuka.toml": "name = \"priv\"\n",
		"heap.asuka": `pub struct Heap {
  private items: [int] = []
  private fn up() -> int = 1
  fn count() -> int = items.count() + up()
}

fn inside(h: Heap) -> int = h.count()
`,
		"use.asuka": `fn outside(h: Heap) -> int = h.count()
`,
	}
	for n, s := range files {
		if err := os.WriteFile(filepath.Join(root, n), []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	c := newClient(t, root)
	for _, tc := range []struct {
		file    string
		private bool
	}{{"heap.asuka", true}, {"use.asuka", false}} {
		uri, text := c.open(filepath.Join(root, tc.file))
		c.waitDiags(uri, noDiags)
		version := 1
		marked := strings.Replace(text, "h.count()\n", "h.|\n", 1)
		i := strings.IndexByte(marked, '|')
		edited := marked[:i] + marked[i+1:]
		got := labels(complete(t, c, uri, &version, edited, marked[:i+1]))
		if !contains(got, "count") || contains(got, "items") != tc.private || contains(got, "up") != tc.private {
			t.Errorf("%s: %v", tc.file, got)
		}
	}
	uri, text := c.open(filepath.Join(root, "heap.asuka"))
	var h *Hover
	p := tdp(uri, posAt(t, text, "+ u|p()"))
	if err := c.call("textDocument/hover", &p, &h); err != nil {
		t.Fatal(err)
	}
	if h == nil || !strings.Contains(h.Contents.Value, "private fn Heap.up") {
		t.Errorf("hover: %+v", h)
	}
}
