package syntax

import (
	"strings"
	"testing"
)

// A line that starts with a binary operator that can't start an
// expression, or with .name, continues the expression before it.
func TestLineContinuation(t *testing.T) {
	tests := []struct{ src, want string }{
		// Binary operators.
		{"a\n  ?? b", "(?? a b)"},
		{"a\n  && b\n  || c", "(|| (&& a b) c)"},
		{"a\n  * b\n  + c", "(+ (* a b) c)"},
		{"a\n  / b % c", "(% (/ a b) c)"},
		{"a\n  == b", "(== a b)"},
		{"a\n  != b", "(!= a b)"},
		{"a\n  < b", "(< a b)"},
		{"a\n  >= b", "(>= a b)"},
		{"a\n  >> b", "(>> a b)"},
		{"a\n  << b", "(<< a b)"},
		{"a\n  ** b", "(** a b)"},
		{"a\n  & b\n  | c\n  ^ d", "(^ (| (& a b) c) d)"},
		{"a\n  &* b\n  &+ c\n  &- d", "(&- (&+ (&* a b) c) d)"},
		{"0\n  ..< n", "(..< 0 n)"},
		{"0\n  .. n", "(.. 0 n)"},
		{"a\n  ?.b", "(?. a b)"},
		// Comments and blank lines between.
		{"a // why\n  // more\n\n  ?? b", "(?? a b)"},
		{"a /* x\n */ ?? b", "(?? a b)"},
		// Member chains.
		{"users\n  .filter(f)\n  .map(g)", "(call (. (call (. users filter) f) map) g)"},
		{"x\n  .y!\n  .z", "(. (! (. x y)) z)"},
		{"a.b(c)\n  .d[0]", "([] (. (call (. a b) c) d) 0)"},
		{"items\n  .type", "(. items type)"},
	}
	for _, tt := range tests {
		f := parseOK(t, "fn f() {\n  let v = "+tt.src+"\n}")
		body := f.Decls[0].(*FuncDecl).Body
		if len(body.Items) != 1 {
			t.Errorf("%q: %d items, want 1", tt.src, len(body.Items))
			continue
		}
		if got := sexpr(body.Items[0].(*LetDecl).Value); got != tt.want {
			t.Errorf("%q:\n got %s\nwant %s", tt.src, got, tt.want)
		}
	}
}

// Where a line that starts with an operator or .name starts something
// of its own, it still does: nothing that parsed before changes.
func TestLineContinuationKeepsMeaning(t *testing.T) {
	tests := []struct{ src, want string }{
		// - and ! can start an expression: a new line.
		{"fn f() -> int {\n  let a = x\n  -a\n}", "{(let x); (- a)}"},
		{"fn f() {\n  a\n  !b\n}", "{a; (! b)}"},
		// A match's arms start with .case.
		{"fn f() {\n  match x {\n    .a => g()\n    .b => h\n  }\n}", "{(match x [.a => (call g)] [.b => h])}"},
		{"fn f() {\n  let v = match x {\n    .a => y\n    .b => z.w\n  }\n}", "{(let (match x [.a => y] [.b => (. z w)]))}"},
		// A block's last line, alone, is its value where the block has
		// one: an if used as a value, a closure's block, an if that's
		// an item of one.
		{"fn f() {\n  let t: T = if c {\n    log()\n    .red\n  } else { .blue }\n}", "{(let (if c {(call log); .red} {.blue}))}"},
		{"fn f() {\n  let g = x => {\n    log()\n    .red\n  }\n}", "{(let (=> (x) {(call log); .red}))}"},
		{"fn f() {\n  let t: T = if c {\n    if d {\n      log()\n      .red\n    } else { .blue }\n  } else { .blue }\n}", "{(let (if c {(if d {(call log); .red} {.blue})} {.blue}))}"},
		// There, two lines are a chain: alone, .a() would be no value.
		{"fn f() {\n  let g = x => {\n    x\n      .a()\n      .b\n  }\n}", "{(let (=> (x) {(. (call (. x a)) b)}))}"},
		// Elsewhere the last line continues too.
		{"fn f() -> [string] {\n  return users\n    .map(u => u.name)\n}", "{(return (call (. users map) (=> (u) (. u name))))}"},
		{"fn f() {\n  if c {\n    total = items\n      .count()\n  }\n}", "{(if c {(= total (call (. items count)))})}"},
	}
	for _, tt := range tests {
		f := parseOK(t, tt.src)
		if got := blockExpr(f.Decls[0].(*FuncDecl).Body); got != tt.want {
			t.Errorf("%q:\n got %s\nwant %s", tt.src, got, tt.want)
		}
	}
	// A match's arms may be or-patterns broken across lines now.
	f := parseOK(t, "fn f() {\n  match x {\n    .a\n      | .b => g()\n  }\n}")
	arm := f.Decls[0].(*FuncDecl).Body.Items[0].(*MatchStmt).Arms[0]
	if _, ok := arm.Pattern.(*OrPat); !ok {
		t.Errorf("the arm's pattern is %T, want an OrPat", arm.Pattern)
	}
	// In a type's angle brackets.
	parseOK(t, "let x: Map<\n  string,\n  int\n> = [:]\n")
}

// blockExpr writes a block's items as sexpr does, and a let, a return,
// an assignment, an if and a match statement likewise.
func blockExpr(b *Block) string {
	var parts []string
	for _, it := range b.Items {
		switch x := it.(type) {
		case *LetDecl:
			parts = append(parts, "(let "+itemExpr(x.Value)+")")
		case *ReturnStmt:
			parts = append(parts, "(return "+itemExpr(x.Value)+")")
		case *AssignStmt:
			parts = append(parts, "(= "+itemExpr(x.Target)+" "+itemExpr(x.Value)+")")
		case *IfStmt:
			s := "(if " + itemExpr(x.Cond) + " " + blockExpr(x.Then)
			if b, ok := x.Else.(*Block); ok {
				s += " " + blockExpr(b)
			}
			parts = append(parts, s+")")
		case *MatchStmt:
			s := "(match " + itemExpr(x.Subject)
			for _, a := range x.Arms {
				s += " [" + printed(a.Pattern) + " => " + itemExpr(a.Body) + "]"
			}
			parts = append(parts, s+")")
		default:
			parts = append(parts, itemExpr(it))
		}
	}
	return "{" + strings.Join(parts, "; ") + "}"
}

func itemExpr(n Node) string {
	switch x := n.(type) {
	case *Block:
		return blockExpr(x)
	case *ExprStmt:
		return itemExpr(x.X)
	case *IfExpr:
		return "(if " + itemExpr(x.Cond) + " " + itemExpr(x.Then) + " " + itemExpr(x.Else) + ")"
	case *LambdaExpr:
		var ps []string
		for _, p := range x.Params {
			ps = append(ps, p.Name.Name)
		}
		return "(=> (" + strings.Join(ps, " ") + ") " + itemExpr(x.Body) + ")"
	case *MatchExpr:
		s := "(match " + itemExpr(x.Subject)
		for _, a := range x.Arms {
			s += " [" + printed(a.Pattern) + " => " + itemExpr(a.Body) + "]"
		}
		return s + ")"
	}
	return sexpr(n)
}
