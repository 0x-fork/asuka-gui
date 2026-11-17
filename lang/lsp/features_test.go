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

// A tuple's elements (t.0) hover with their types and complete after
// its dot.
func TestTupleElements(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `fn f(p: (int, (string, bool))) -> bool {
  let n = p.0
  return p.1.1
}
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"p.|0", "(int, (string, bool)).0: int"},
		{"p.1.|1", "(string, bool).1: bool"},
		{"p.1.|1", "The tuple's element 1"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	version := 1
	edited := strings.Replace(text, "p.1.1\n", "p.1.\n", 1)
	got := labels(complete(t, c, uri, &version, edited, edited[:strings.Index(edited, "p.1.\n")+4]+"|"))
	for _, w := range []string{"0", "1"} {
		if !contains(got, w) {
			t.Errorf("no %q in %v", w, got)
		}
	}
}

// A Set's methods complete and hover, and Set itself has its doc.
func TestSets(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `fn f() -> bool {
  var tags: Set<string> = ["a", "b"]
  tags.insert("c")
  return tags.contains("a")
}

fn g(xs: [int]) -> int = Set(xs).count()
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"tags.ins|ert", "fn Set<string>.insert(value: string) -> bool"},
		{"tags.con|tains", "Whether value is one of the elements."},
		{"var ta|gs", "var tags: Set<string>"},
		{"Se|t(xs)", "fn Set<T: Hashable>"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	version := 1
	edited := strings.Replace(text, "tags.insert(\"c\")", "tags.", 1)
	got := labels(complete(t, c, uri, &version, edited, edited[:strings.Index(edited, "tags.\n")+5]+"|"))
	for _, w := range []string{"insert", "remove", "union", "isSubset", "contains", "count"} {
		if !contains(got, w) {
			t.Errorf("no %q in %v", w, got)
		}
	}
	for _, w := range []string{"push", "keys"} {
		if contains(got, w) {
			t.Errorf("%q in %v", w, got)
		}
	}
}

// An operator's hover: the method of a type's it calls, what a
// wrapping operator does, and that an int's + throws past its range.
func TestOperatorHover(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `struct Vec {
  x: int

  /// The dot product.
  fn mul(o: Vec) -> int = x * o.x
  fn neg() -> Vec = Vec(x: -x)
}

fn f(a: Vec, n: int) -> int {
  var h = n &* 31
  h &+= 1
  let b = -a
  return a * b + h + n
}
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"n &|* 31", "An int's product, wrapping"},
		{"h &|+= 1", "An int's sum, wrapping"},
		{"a |* b", "fn Vec.mul(o: Vec) -> int"},
		{"a |* b", "The dot product."},
		{"let b = |-a", "fn Vec.neg() -> Vec"},
		{"h |+ n", "integer overflow"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
}

// An int's checked arithmetic completes on it.
func TestCheckedCompletion(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := "fn f(n: int) -> int? = n.addingChecked(1)\n"
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	if got := hoverAt(t, c, uri, text, "n.adding|Checked"); !strings.Contains(got, "none when it overflows") {
		t.Errorf("hover: %q", got)
	}
	version := 1
	edited := "fn f(n: int) -> int? = n.\n"
	got := labels(complete(t, c, uri, &version, edited, "fn f(n: int) -> int? = n.|"))
	for _, w := range []string{"addingChecked", "subtractingChecked", "multipliedChecked"} {
		if !contains(got, w) {
			t.Errorf("no %q in %v", w, got)
		}
	}
}
