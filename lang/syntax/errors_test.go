package syntax

import (
	"os"
	"strings"
	"testing"
)

// TestParseErrors checks the diagnostics of broken files: each wanted
// error, by position and message, and that what follows the mistake
// still parses.
func TestParseErrors(t *testing.T) {
	tests := []struct {
		name  string
		src   string
		errs  []string // "line:col: message" prefixes, in order
		decls int      // the declarations the tree still has
	}{
		{
			name: "unclosed block before a declaration",
			src: `component A {
  Column {
    Text("a")
}

component B {
  Text("b")
}
`,
			errs:  []string{"6:1: expected '}' to close the block opened at 1:13, found keyword 'component'"},
			decls: 2,
		},
		{
			name: "a keyword newer than the code",
			src: `fn f() -> int {
  let worker = 2
  return worker
}

worker W {}
`,
			errs: []string{
				"2:7: expected a pattern, found keyword 'worker' (rename it: worker is a keyword since 0.2.7)",
				"3:10: expected an expression, found keyword 'worker' (rename it: worker is a keyword since 0.2.7)",
			},
			decls: 2,
		},
		{
			name: "extern without fn, type or struct",
			src: `extern component A {}

extern type T with free
extern fn f() -> int
`,
			errs:  []string{"1:8: expected 'fn', 'type' or 'struct' after 'extern'", "3:20: expected 'release' and the function that frees it"},
			decls: 3,
		},
		{
			name: "unclosed block at the end",
			src: `component A {
  if ready {
    Text("a")
  }
`,
			errs:  []string{"4:5: expected '}' to close the block opened at 1:13, found the end of the file"},
			decls: 1,
		},
		{
			name: "unclosed argument list",
			src: `component A {
  Text("a"
  state x = 1
}
`,
			errs:  []string{"3:3: expected ')' to close the argument list opened at 2:7, found keyword 'state'"},
			decls: 1,
		},
		{
			name: "missing values",
			src: `component A {
  state x =
  let y = 1 +
}
state z: int
`,
			errs: []string{
				"3:3: expected an expression, found keyword 'let'",
				"4:1: expected an expression, found '}'",
				"5:13: expected '=' and the state's first value, found a newline",
			},
			decls: 2,
		},
		{
			name: "several errors, one per line",
			src: `fn f() {
  let a = )
  let b = 1 2
  c(
}
fn g() {}
`,
			errs: []string{
				"2:11: expected an expression, found ')'",
				"3:13: expected a newline or ';' after the let, found '2'",
				"5:1: expected ')' to close the argument list opened at 4:4, found '}'",
			},
			decls: 2,
		},
		{
			name: "not a declaration",
			src: `x = 1
component A {}
pub 3
`,
			errs: []string{
				"1:1: expected a declaration (component, screen, store, struct, enum, fn, …), found 'x'",
				"3:5: expected a declaration after 'pub', found '3'",
			},
			decls: 3,
		},
		{
			name: "declarations in a block",
			src: `component A {
  struct S { x: int }
  Text("a")
}
`,
			errs:  []string{"2:3: a struct is declared at the top level of a file, not in a block"},
			decls: 1,
		},
		{
			name: "chained comparison",
			src:  "let x = a < b < c\nlet y = 0..1..2\n",
			errs: []string{
				"1:15: comparisons don't chain: write a < b && b < c",
				"2:13: ranges don't chain",
			},
			decls: 2,
		},
		{
			name: "C's operators",
			src: `fn f() {
  x++
  y--
  ++z
  w = a--b
  v = a ? b : c
}
`,
			errs: []string{
				"2:4: there's no ++ operator: write x += 1",
				"3:4: there's no -- operator: write x -= 1",
				"4:3: there's no ++ operator: write x += 1",
				"6:9: there's no ?: operator: write if a { b } else { c }",
			},
			decls: 1,
		},
		{
			name:  "a number without its 0",
			src:   "let a = .5\nlet b = x * .25\nlet c = .5s\n",
			errs:  []string{"1:9: a number starts with a digit: 0.5", "2:13: a number starts with a digit: 0.25", "3:9: a number starts with a digit: 0.5s"},
			decls: 3,
		},
		{
			name:  "if value without else",
			src:   "let x = if a { 1 }\n",
			errs:  []string{"1:19: an if used as a value needs an else"},
			decls: 1,
		},
		{
			name:  "do without catch",
			src:   "fn f() {\n  do { a() }\n  b()\n}\n",
			errs:  []string{"2:13: expected 'catch' after do { … }, found a newline"},
			decls: 1,
		},
		{
			name: "bad tokens and literals",
			src: `let a = #12
let b = 5kg
let c = "open
let d = 1
`,
			errs: []string{
				"1:9: a color has 3, 4, 6 or 8 hexadecimal digits, not 2",
				`2:10: unknown unit "kg" after a number`,
				"3:9: string not terminated",
			},
			decls: 4,
		},
		{
			name:  "unclosed interpolation",
			src:   "let s = \"a {b c\"\nlet t = 1\n",
			errs:  []string{"1:9: string not terminated", "1:16: string not terminated"},
			decls: 2,
		},
		{
			// A block comment's newline ends the line, as a newline does.
			name:  "interpolation spanning lines in a comment",
			src:   "let s = \"a {b /* x\n*/ } c\"\nlet t = 1\n",
			errs:  []string{"1:9: string not terminated", "2:7: string not terminated"},
			decls: 3, // the rest of line 2 is a bad one
		},
		{
			name: "a brace on a line of its own",
			src: `fn f()
{
  if a
  {
    b()
  }
  Row
  { }
}
struct S
{
  x: int
}
`,
			errs: []string{
				"2:1: a '{' goes at the end of the line before, not on a line of its own",
				"4:3: a '{' goes at the end of the line before, not on a line of its own",
				"8:3: a block on a line of its own isn't one: its '{' goes at the end of the line before (Row {)",
				"11:1: a '{' goes at the end of the line before, not on a line of its own",
			},
			decls: 2,
		},
		{
			name:  "var at the top level",
			src:   "var n = 0\n",
			errs:  []string{"1:1: a file has no variables: its mutable values are state (state x = …)"},
			decls: 1,
		},
		{
			name:  "import after a declaration",
			src:   "let a = 1\nimport std/http\n",
			errs:  []string{"2:1: an import goes before the file's declarations"},
			decls: 1,
		},
		{
			name:  "unexpected brace",
			src:   "let a = 1\n}\nlet b = 2\n",
			errs:  []string{"2:1: unexpected '}'"},
			decls: 2,
		},
		{
			name:  "unclosed match",
			src:   "fn f() {\n  match x {\n    .a => 1\n\nstruct S {}\n",
			errs:  []string{"5:1: expected '}' to close the match opened at 2:11, found keyword 'struct'"},
			decls: 2,
		},
	}
	for _, tt := range tests {
		f, errs := ParseFile(NewFileSet(), "e.asuka", []byte(tt.src))
		if len(errs) != len(tt.errs) {
			t.Errorf("%s: %d errors, want %d:\n%v", tt.name, len(errs), len(tt.errs), errsText(errs))
		}
		for i, want := range tt.errs {
			if i < len(errs) && !strings.HasPrefix(errs[i].Error(), "e.asuka:"+want) {
				t.Errorf("%s: error %d\n got %s\nwant %s", tt.name, i, errs[i], want)
			}
		}
		if got := len(f.Imports) + len(f.Decls); got != tt.decls+len(f.Imports) && len(f.Decls) != tt.decls {
			t.Errorf("%s: %d declarations, want %d", tt.name, len(f.Decls), tt.decls)
		}
	}
}

func errsText(errs ErrorList) string {
	var b strings.Builder
	for _, e := range errs {
		b.WriteString(e.Error() + "\n")
	}
	return b.String()
}

// TestParseNeverPanics parses every prefix of the corpus and the corpus
// with each line dropped: whatever comes in, the parser ends, with a
// tree whose spans are in order, and Format refuses what has errors.
func TestParseNeverPanics(t *testing.T) {
	for _, name := range corpus(t) {
		src, err := os.ReadFile(name)
		if err != nil {
			t.Fatal(err)
		}
		step := 1
		if testing.Short() {
			step = 13
		}
		for n := 0; n <= len(src); n += step {
			check(t, name, src[:n])
		}
		lines := strings.SplitAfter(string(src), "\n")
		for i := range lines {
			check(t, name, []byte(strings.Join(lines[:i], "")+strings.Join(lines[i+1:], "")))
		}
	}
}

func check(t *testing.T, name string, src []byte) {
	t.Helper()
	defer func() {
		if r := recover(); r != nil {
			t.Fatalf("%s: panic on %q: %v", name, src, r)
		}
	}()
	f, errs := ParseFile(NewFileSet(), name, src)
	var stack []Node
	Inspect(f, func(n Node) bool {
		if n == nil {
			stack = stack[:len(stack)-1]
			return false
		}
		r := n.Range()
		if r.End < r.Start {
			t.Fatalf("%s: %T's span ends before it starts in %q", name, n, src)
		}
		// Without errors, a node is inside its parent.
		if k := len(stack); k > 0 && len(errs) == 0 {
			if pr := stack[k-1].Range(); r.Start < pr.Start || r.End > pr.End {
				t.Fatalf("%s: %T's span is outside its parent %T's in %q", name, n, stack[k-1], src)
			}
		}
		stack = append(stack, n)
		return true
	})
	out, err := Format(name, src)
	if (err != nil) != (len(errs) > 0) {
		t.Fatalf("%s: Format's error %v with %d parse errors", name, err, len(errs))
	}
	if err == nil {
		if msg := roundTrip(name, src, out); msg != "" {
			t.Fatal(msg)
		}
	}
}

// FuzzParse checks what check does on any input; its seeds are every
// .asuka file in the repository.
func FuzzParse(f *testing.F) {
	for _, name := range repoFiles(f) {
		src, _ := os.ReadFile(name)
		f.Add(src)
	}
	for _, s := range []string{"", "{", "}", "(", "\"{", "component", "if a {", "match x { .a =>", "x => (", "[a: ", "#", "1_", "///"} {
		f.Add([]byte(s))
	}
	f.Fuzz(func(t *testing.T, src []byte) {
		check(t, "fuzz.asuka", src)
	})
}
