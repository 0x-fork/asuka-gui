package lsp

import (
	"path/filepath"
	"strings"
	"testing"
)

// defAt is the definition at the | of marked in text: its line (from
// 0), -1 when there's none.
func defAt(t *testing.T, c *client, uri, text, marked string) int {
	t.Helper()
	var locs []Location
	p := tdp(uri, posAt(t, text, marked))
	if err := c.call("textDocument/definition", &p, &locs); err != nil {
		t.Fatal(err)
	}
	if len(locs) != 1 {
		return -1
	}
	return locs[0].Range.Start.Line
}

// Type tests (c: Circle, Circle(radius), Unit.big(n)) and provided
// methods: hover, definition and completion.
func TestTypeTestsAndProvidedMethods(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `interface Shape {
  fn area() -> float
  /// What it is, in words.
  fn describe() -> string = "a shape of {area()}"
}

struct Circle {
  radius: float
  fn area() -> float = 3.0 * radius * radius
}

enum Unit: Shape {
  small
  big(float)
  fn area() -> float = 1.0
}

fn show(s: Shape) -> string = match s {
  c: Circle => "{c.radius}"
  Circle(radius) => "{radius}"
  Unit.big(n) => "{n}"
  _ => s.describe()
}

fn twice<T: Shape>(x: T) -> string = x.describe() + x.describe()
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"  |c: Circle", "c: Circle"},
		{"c: Cir|cle =>", "struct Circle"},
		{"{c.rad|ius}", "radius: float"},
		{"Circle(rad|ius) =>", "radius: float"},
		{"Unit.b|ig(n)", "big"},
		{"s.descr|ibe()", "fn Shape.describe() -> string"},
		{"s.descr|ibe()", "What it is, in words."},
		{"x.descr|ibe() +", "fn Shape.describe() -> string"},
		{"of {ar|ea()}", "fn Shape.area() -> float"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	for _, tc := range []struct {
		at   string
		line int
	}{
		{"c: Cir|cle =>", 6},
		{"{c.rad|ius}", 7},
		{"s.descr|ibe()", 3},
		{"x.descr|ibe() +", 3},
		{"of {ar|ea()}", 1},
		{"Unit.b|ig(n)", 13},
	} {
		if got := defAt(t, c, uri, text, tc.at); got != tc.line {
			t.Errorf("definition at %s: line %d, want %d", tc.at, got, tc.line)
		}
	}
	version := 1
	edited := strings.Replace(text, "_ => s.describe()", "_ => s.", 1)
	got := labels(complete(t, c, uri, &version, edited, edited[:strings.Index(edited, "_ => s.")+7]+"|"))
	for _, w := range []string{"area", "describe"} {
		if !contains(got, w) {
			t.Errorf("no %q in %v", w, got)
		}
	}
	version++
	edited = strings.Replace(text, `"a shape of {area()}"`, `"a shape of {ar}"`, 1)
	got = labels(complete(t, c, uri, &version, edited, edited[:strings.Index(edited, "{ar}")+3]+"|"))
	if !contains(got, "area") {
		t.Errorf("no area in %v", got)
	}
}
