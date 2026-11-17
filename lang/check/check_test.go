package check

import (
	"strings"
	"testing"

	"github.com/egoist/asuka/lang/syntax"
)

// checkSrc checks one package of files (name: source) and returns its
// diagnostics, the prelude's included.
func checkSrc(t *testing.T, files map[string]string) (*Program, []Diagnostic) {
	t.Helper()
	fset := syntax.NewFileSet()
	c := NewChecker(fset)
	var parsed []*syntax.SourceFile
	var diags []Diagnostic
	for name, src := range files {
		f, errs := syntax.ParseFile(fset, name, []byte(src))
		for _, e := range errs {
			diags = append(diags, Diagnostic{Pos: e.Pos, Msg: e.Msg})
		}
		parsed = append(parsed, f)
	}
	p := c.CheckPackage("", "main", parsed, nil)
	prog := c.Program(p)
	return prog, append(diags, prog.Diags...)
}

func TestPreludeChecks(t *testing.T) {
	_, diags := checkSrc(t, map[string]string{"a.asuka": ""})
	for _, d := range diags {
		t.Error(d)
	}
}

func TestSmoke(t *testing.T) {
	_, diags := checkSrc(t, map[string]string{"a.asuka": `
struct Todo {
  id: int
  title: string
  done: bool = false

  fn label() -> string = "{id}: {title}"
}

enum Filter {
  all
  done
  from(string)

  fn matches(t: Todo) -> bool = match self {
    .all => true
    .done => t.done
    .from(p) => t.title.hasPrefix(p)
  }
}

fn total<T>(items: [T], size: fn(T) -> float) -> float {
  var sum = 0.0
  for i in items { sum += size(i) }
  return sum
}

state count = 0
let doubled = count * 2

store Mailbox {
  state todos: [Todo] = []
  let left = todos.count(t => !t.done)
  fn add(title: string) {
    todos.push(Todo(id: todos.count() + 1, title: title))
  }
}

component Counter {
  state n = 0
  prop step = 1
  event changed(value: int)

  Column {
    gap: 12; padding: 16
    Text("Count: {n}") { font: .largeTitle.bold() }
    Button("Add") { on press { n += step; emit changed(n) } }
    if n > 3 { Text("many") }
    for t in [1, 2, 3] key t { Text("{t}") }
  }
}

async fn later() -> int {
  await sleep(10ms)
  return 3
}

test "things" {
  let ts = [Todo(id: 1, title: "a"), Todo(id: 2, title: "b", done: true)]
  let f: Filter = .done
  expect(ts.filter(t => f.matches(t)).count() == 1)
  expect(total(ts, t => float(t.id)) == 3.0)
  let x = await later()
  let (a, b) = await all(later(), later())
  expect(x + a + b == 9)
  let m = Mailbox()
  m.add("hi")
  expect(m.left == 1)
  let names = ts.map(t => t.title).joined(separator: ",")
  expect(names == "a,b")
}
`})
	for _, d := range diags {
		t.Error(d)
	}
}

func TestErrorsReported(t *testing.T) {
	_, diags := checkSrc(t, map[string]string{"a.asuka": `
fn f() -> int { return "x" }
fn g() { let x = y }
component C {
  state n = 0
  n = 3
}
`})
	var msgs []string
	for _, d := range diags {
		msgs = append(msgs, d.String())
	}
	got := strings.Join(msgs, "\n")
	for _, want := range []string{"the result is a string, not an int", "unknown name y", "an assignment in a view"} {
		if !strings.Contains(got, want) {
			t.Errorf("missing %q in:\n%s", want, got)
		}
	}
}
