package lsp

import (
	"path/filepath"
	"strings"
	"testing"
)

// hoverAt is the hover at the | of marked in text, "" when there's none.
func hoverAt(t *testing.T, c *client, uri, text, marked string) string {
	t.Helper()
	var h *Hover
	p := tdp(uri, posAt(t, text, marked))
	if err := c.call("textDocument/hover", &p, &h); err != nil {
		t.Fatal(err)
	}
	if h == nil {
		return ""
	}
	return h.Contents.Value
}

// A type parameter constrained to interfaces: hover names them, its
// values' members complete and resolve to the interface's.
func TestConstrainedTypeParams(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `interface Shape {
  /// The area, in points.
  fn area() -> float
  name: string
}

fn total<T: Shape & Hashable>(xs: [T]) -> float {
  var sum = 0.0
  for x in xs { sum += x.area() }
  return sum
}

fn label<T: Shape>(x: T) -> string = x.name
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"fn total<|T", "T: Shape & Hashable"},
		{"x.ar|ea()", "fn Shape.area() -> float"},
		{"x.ar|ea()", "The area, in points."},
		{"x.na|me", "name: string"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	version := 1
	edited := strings.Replace(text, "x.name\n", "x.\n", 1)
	got := labels(complete(t, c, uri, &version, edited, edited[:strings.Index(edited, "x.\n")+2]+"|"))
	for _, w := range []string{"area", "name"} {
		if !contains(got, w) {
			t.Errorf("no %q in %v", w, got)
		}
	}
}
