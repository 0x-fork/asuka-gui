package compile

import (
	"fmt"
	"strings"
	"testing"
)

// bigProgram is a program of about n lines: structs, enums, functions
// with loops, closures and matches, a store and tests.
func bigProgram(n int) string {
	var b strings.Builder
	for i := 0; b.Len() < n*40; i++ {
		fmt.Fprintf(&b, `
struct Item%d {
  id: int
  name: string
  tags: [string] = []
  done: bool = false

  fn label() -> string = "{id}: {name}"
}

enum Kind%d {
  small
  large(int)
  named(string)

  fn size() -> int = match self {
    .small => 1
    .large(n) => n
    .named(s) => s.count()
  }
}

fn work%d(items: [Item%d], k: Kind%d) -> int {
  var total = k.size()
  for (i, it) in items.enumerated() {
    if it.done && it.tags.contains("x") { total += i }
    total += it.label().count()
  }
  let names = items.filter(x => !x.done).map(x => x.name.upper())
  return total + names.count()
}

test "work %d" {
  let xs = [Item%d(id: 1, name: "a"), Item%d(id: 2, name: "b", done: true)]
  expect(work%d(xs, .large(3)) > 0)
}
`, i, i, i, i, i, i, i, i, i)
	}
	return b.String()
}

func BenchmarkCompile1000Lines(b *testing.B) {
	src := []byte(bigProgram(1000))
	lines := strings.Count(string(src), "\n")
	b.ReportMetric(float64(lines), "lines")
	for i := 0; i < b.N; i++ {
		res := Files(map[string][]byte{"big.asuka": src}, nil)
		if err := res.Err(); err != nil {
			b.Fatal(err)
		}
	}
}

func TestBigProgramRuns(t *testing.T) {
	src := []byte(bigProgram(1000))
	res := Files(map[string][]byte{"big.asuka": src}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	out, err := Run(testRunner(t), res.Module, true, "")
	if err != nil || strings.Contains(out, "FAIL") {
		t.Fatalf("%v\n%s", err, out)
	}
}
