package syntax

import "testing"

// load followed by ( calls a function named load, unless the parentheses
// hold one call: then they're the load operator's operand, as before.
func TestParseLoadParen(t *testing.T) {
	tests := []struct{ src, want string }{
		{"load()", "(call load)"},
		{"load(id)", "(call load id)"},
		{"load(id: 1, 2)", "(call load id:1 2)"},
		{"load().value", "(. (call load) value)"},
		{"load(f())", "(load (paren (call f)))"},
		{"load (api.user(id))", "(load (paren (call (. api user) id)))"},
		{"load ((f()))", "(load (paren (paren (call f))))"},
		{"load (f())(x)", "(load (call (paren (call f)) x))"},
		{"load f(x)", "(load (call f x))"},
	}
	for _, tt := range tests {
		x, errs := ParseExpr(tt.src)
		if len(errs) > 0 {
			t.Errorf("%q: %v", tt.src, errs)
			continue
		}
		if got := sexpr(x); got != tt.want {
			t.Errorf("%q:\n got %s\nwant %s", tt.src, got, tt.want)
		}
	}
	src := "component C {\n  task(id) { await load() }\n  let u = load (api.user(id))\n}\n"
	if out, err := Format("x.asuka", []byte(src)); err != nil || string(out) != src {
		t.Errorf("formats as %q, %v", out, err)
	}
}
