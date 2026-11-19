package main

import (
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// asukaTest runs asuka test with args, what it prints to stdout and its
// error.
func asukaTest(t *testing.T, args ...string) (string, error) {
	t.Helper()
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	old := os.Stdout
	os.Stdout = w
	done := make(chan string)
	go func() {
		b, _ := io.ReadAll(r)
		done <- string(b)
	}()
	err = cmdTest(args)
	w.Close()
	os.Stdout = old
	return <-done, err
}

// jsonLines are the JSON objects asuka test --json printed.
func jsonLines(t *testing.T, out string) []map[string]any {
	t.Helper()
	var lines []map[string]any
	for _, l := range strings.Split(strings.TrimSpace(out), "\n") {
		var m map[string]any
		if err := json.Unmarshal([]byte(l), &m); err != nil {
			t.Fatalf("not JSON: %q", l)
		}
		lines = append(lines, m)
	}
	return lines
}

// testResult is the line of the test named name.
func testResult(t *testing.T, lines []map[string]any, name string) map[string]any {
	t.Helper()
	for _, l := range lines {
		if l["test"] == name && l["ok"] != nil {
			return l
		}
	}
	t.Fatalf("no result for %q in %v", name, lines)
	return nil
}

func writePkg(t *testing.T, files map[string]string) string {
	t.Helper()
	dir := t.TempDir()
	for name, src := range files {
		p := filepath.Join(dir, name)
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(src), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

const propsSrc = `struct Point {
  x: int
  y: int
}

fn parse(s: string) -> int {
  if s.hasPrefix("a") {
    if s.hasPrefix("ab") {
      if s.hasPrefix("abc") {
        if s.hasPrefix("abcd") {
          return 4
        }
        return 3
      }
      return 2
    }
    return 1
  }
  return 0
}

test "short lists" (xs: [int]) {
  expect(xs.count() < 3)
}

test "points" (p: Point, label: string?) {
  expect(p.x + p.y < 1000)
}

test "reverse twice" (xs: [int], s: string) {
  expect(xs.reversed().reversed() == xs)
  expect(s.reversed().reversed() == s)
}

test "parse never says 4" (s: string) {
  expect(parse(s) != 4)
}
`

// A property test that fails shrinks its input to the simplest that
// still fails, and says the seed to run it again; the same seed gives the
// same input. A corpus file's input runs first; fuzzing finds what the
// seeded cases don't, and saves it there.
func TestTestProperties(t *testing.T) {
	dir := writePkg(t, map[string]string{"props.asuka": propsSrc})
	out, err := asukaTest(t, dir, "--json")
	if err == nil {
		t.Fatalf("passed:\n%s", out)
	}
	lines := jsonLines(t, out)
	short := testResult(t, lines, "short lists")
	p := short["property"].(map[string]any)
	if short["ok"] != false || p["shown"] != "xs = [0, 0, 0]" || p["seed"] != 0.0 || p["input"] != "{\n  \"xs\": [0,0,0]\n}\n" {
		t.Errorf("short lists: %v", short)
	}
	if msg := short["message"].(string); !strings.Contains(msg, "rerun it: asuka test --run 'short lists' --seed 0") {
		t.Errorf("short lists' message: %s", msg)
	}
	points := testResult(t, lines, "points")
	if s := points["property"].(map[string]any)["shown"]; s != "p = Point(x: 1000, y: 0)\nlabel = none" {
		t.Errorf("points shrank to %q", s)
	}
	for _, name := range []string{"reverse twice", "parse never says 4"} {
		if r := testResult(t, lines, name); r["ok"] != true {
			t.Errorf("%s: %v", name, r)
		}
	}

	// Another seed, other cases: the same simplest input.
	out, _ = asukaTest(t, dir, "--run", "points", "--seed", "7", "--cases", "300")
	if !strings.Contains(out, "p = Point(x: 1000, y: 0)") || !strings.Contains(out, "of seed 7,") {
		t.Errorf("--seed 7:\n%s", out)
	}

	// Fuzzing, guided by what each input reaches, finds "abcd", shrinks
	// it, and keeps it in the corpus.
	out, err = asukaTest(t, dir, "--fuzz", "--fuzztime", "100000x", "--seed", "2", "--run", "parse")
	if err == nil || !strings.Contains(out, `s = "abcd"`) || !strings.Contains(out, "found by fuzzing") {
		t.Fatalf("--fuzz: %v\n%s", err, out)
	}
	saved, _ := filepath.Glob(filepath.Join(dir, "testdata", "fuzz", "parse-never-says-4", "*.json"))
	if len(saved) != 1 {
		t.Fatalf("saved: %v", saved)
	}
	if b, _ := os.ReadFile(saved[0]); string(b) != "{\n  \"s\": \"abcd\"\n}\n" {
		t.Fatalf("saved %q", b)
	}
	// Every run tries it now.
	out, _ = asukaTest(t, dir, "--run", "parse")
	if !strings.Contains(out, "FAIL parse never says 4") || !strings.Contains(out, "input (testdata/fuzz/parse-never-says-4/") {
		t.Fatalf("with the corpus:\n%s", out)
	}
	// A corpus file is JSON a person may write: a float JSON can't hold
	// too.
	if err := os.Remove(saved[0]); err != nil {
		t.Fatal(err)
	}
	floats := writePkg(t, map[string]string{
		"f.asuka":                       "test \"finite\" (x: float, n: int) {\n  if n > 0 { expect(x == x) }\n}\n",
		"testdata/fuzz/finite/nan.json": "{\"x\": NaN, \"n\": 1}",
	})
	out, _ = asukaTest(t, floats)
	if !strings.Contains(out, "x = nan\n       n = 1") {
		t.Fatalf("a corpus file's NaN:\n%s", out)
	}
}

const snapSrc = `struct Todo {
  title: string
  done: bool
}

fn todos(n: int) -> [Todo] {
  var out: [Todo] = []
  for i in 0..<n {
    out.push(Todo(title: "Task {i}", done: i % 2 == 0))
  }
  return out
}

test "todos" {
  expectSnapshot(todos(2))
  expectSnapshot(["b": 2, "a": 1])
  expectInlineSnapshot(todos(1))
  expectInlineSnapshot(42)
}

test "many" {
  expectInlineSnapshot(todos(4))
}
`

// Snapshots: written the first time (in testdata/snapshots, or in the
// call), compared after, a difference shown; --update takes what
// changed and removes what no test takes; in CI a missing one fails.
func TestTestSnapshots(t *testing.T) {
	dir := writePkg(t, map[string]string{"todos.asuka": snapSrc})
	src := filepath.Join(dir, "todos.asuka")
	snap := filepath.Join(dir, "testdata", "snapshots", "todos.snap")
	t.Setenv("CI", "")
	out, err := asukaTest(t, dir)
	if err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	if !strings.Contains(out, "snapshots: 2 written (") || !strings.Contains(out, "snapshots: 3 written in their calls") {
		t.Errorf("first run:\n%s", out)
	}
	b, _ := os.ReadFile(snap)
	want := `# The snapshots of todos.asuka's tests: asuka test writes them, and --update when they change.

snapshot "todos 1"
  [Todo(title: "Task 0", done: true), Todo(title: "Task 1", done: false)]

snapshot "todos 2"
  ["a": 1, "b": 2]
`
	if string(b) != want {
		t.Errorf("todos.snap:\n%s\nwant:\n%s", b, want)
	}
	b, _ = os.ReadFile(src)
	for _, w := range []string{
		`expectInlineSnapshot(todos(1), r#"[Todo(title: "Task 0", done: true)]"#)`,
		`expectInlineSnapshot(42, "42")`,
		"expectInlineSnapshot(todos(4), \"\"\"\n    [\n      Todo(title: \"Task 0\", done: true),\n      Todo(title: \"Task 1\", done: false),\n      Todo(title: \"Task 2\", done: true),\n      Todo(title: \"Task 3\", done: false),\n    ]\n    \"\"\")",
	} {
		if !strings.Contains(string(b), w) {
			t.Errorf("todos.asuka hasn't %s:\n%s", w, b)
		}
	}
	if out, err := asukaTest(t, dir); err != nil || strings.Contains(out, "snapshots:") {
		t.Fatalf("second run: %v\n%s", err, out)
	}

	// What changed fails the test, with the difference.
	edited := strings.Replace(string(b), `"Task {i}"`, `"Item {i}"`, 1)
	if err := os.WriteFile(src, []byte(edited), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err = asukaTest(t, dir)
	if err == nil {
		t.Fatalf("a changed snapshot passed:\n%s", out)
	}
	for _, w := range []string{
		"expectSnapshot (todos.asuka:15): what it shows isn't its snapshot",
		"- [Todo(title: \"Task 0\", done: true), Todo(title: \"Task 1\", done: false)]\n     + [Todo(title: \"Item 0\", done: true), Todo(title: \"Item 1\", done: false)]",
		"expectInlineSnapshot (todos.asuka:22): what it shows isn't its snapshot",
	} {
		if !strings.Contains(out, w) {
			t.Errorf("changed: no %q in\n%s", w, out)
		}
	}
	out, err = asukaTest(t, dir, "--update")
	if err != nil || !strings.Contains(out, "snapshots: 1 updated") {
		t.Fatalf("--update: %v\n%s", err, out)
	}
	if b, _ := os.ReadFile(snap); !strings.Contains(string(b), `Todo(title: "Item 0", done: true)`) {
		t.Errorf("--update didn't write it:\n%s", b)
	}
	if b, _ := os.ReadFile(src); !strings.Contains(string(b), `Todo(title: "Item 3", done: false)`) {
		t.Errorf("--update didn't rewrite the call:\n%s", b)
	}
	if out, err := asukaTest(t, dir); err != nil {
		t.Fatalf("after --update: %v\n%s", err, out)
	}

	// A snapshot no test takes any more: said, then removed by --update.
	b, _ = os.ReadFile(src)
	if err := os.WriteFile(src, []byte(strings.Replace(string(b), "  expectSnapshot([\"b\": 2, \"a\": 1])\n", "", 1)), 0o644); err != nil {
		t.Fatal(err)
	}
	if out, _ := asukaTest(t, dir); !strings.Contains(out, "snapshots: 1 no test takes (asuka test --update removes them)") {
		t.Errorf("obsolete:\n%s", out)
	}
	if out, _ := asukaTest(t, dir, "-u"); !strings.Contains(out, "1 removed") {
		t.Errorf("-u:\n%s", out)
	}
	if b, _ := os.ReadFile(snap); strings.Contains(string(b), "todos 2") {
		t.Errorf("still there:\n%s", b)
	}

	// In CI, a snapshot not stored fails rather than being written.
	t.Setenv("CI", "true")
	b, _ = os.ReadFile(src)
	if err := os.WriteFile(src, []byte(string(b)+"\ntest \"new\" {\n  expectSnapshot(1)\n}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err = asukaTest(t, dir)
	if err == nil || !strings.Contains(out, "no snapshot is stored for it, and in CI asuka test writes none") {
		t.Fatalf("in CI: %v\n%s", err, out)
	}
	if b, _ := os.ReadFile(snap); strings.Contains(string(b), `"new 1"`) {
		t.Errorf("written in CI:\n%s", b)
	}
}

const coverSrc = `fn sign(n: int) -> string {
  if n < 0 {
    return "negative"
  } else if n == 0 {
    return "zero"
  }
  return "positive"
}

fn kind(n: int) -> string = match n {
  0 => "none"
  1 => "one"
  _ => "many"
}

fn unused() -> int {
  return 1
}

test "signs" {
  expect(sign(-1) == "negative")
  expect(sign(5) == "positive")
  expect(kind(1) == "one")
}
`

// Coverage counts the package's statements, not its tests': a line each,
// an if's arms (the else it leaves out too) and a match's.
func TestTestCover(t *testing.T) {
	dir := writePkg(t, map[string]string{"cover.asuka": coverSrc})
	lcov := filepath.Join(t.TempDir(), "lcov.info")
	page := filepath.Join(t.TempDir(), "cover.html")
	out, err := asukaTest(t, dir, "--cover-lcov", lcov, "--cover-html", page)
	if err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	if !strings.Contains(out, "coverage: 55.6% of lines (5 of 9), 50.0% of branches (3 of 6)") || !strings.Contains(out, "not run: 5, 11, 13, 17") {
		t.Errorf("summary:\n%s", out)
	}
	b, err := os.ReadFile(lcov)
	if err != nil {
		t.Fatal(err)
	}
	got := strings.ReplaceAll(string(b), filepath.Join(dir, "cover.asuka"), "cover.asuka")
	want := `TN:
SF:cover.asuka
BRDA:2,0,0,1
BRDA:2,0,1,0
BRDA:2,0,2,1
BRDA:10,1,0,0
BRDA:10,1,1,1
BRDA:10,1,2,0
BRF:6
BRH:3
DA:2,2
DA:3,1
DA:5,0
DA:7,1
DA:10,1
DA:11,0
DA:12,1
DA:13,0
DA:17,0
LF:9
LH:5
end_of_record
`
	if got != want {
		t.Errorf("lcov:\n%s\nwant:\n%s", got, want)
	}
	if b, err := os.ReadFile(page); err != nil || !strings.Contains(string(b), `<tr class="miss"><td class="n">17</td><td class="c">0</td><td class="s">  return 1</td></tr>`) {
		t.Errorf("the page: %v\n%s", err, b)
	}
	out, _ = asukaTest(t, dir, "--cover", "--json")
	lines := jsonLines(t, out)
	last := lines[len(lines)-1]["coverTotal"].(map[string]any)
	if l := last["lines"].(map[string]any); l["hit"] != 5.0 || l["total"] != 9.0 {
		t.Errorf("--json: %v", last)
	}
}

const benchSrc = `fn fib(n: int) -> int {
  if n < 2 { return n }
  return fib(n - 1) + fib(n - 2)
}

bench "fib 10" {
  keep(fib(10))
}

bench "sorted" {
  let xs = [5, 3, 8, 1]
  measure {
    let ys = keep(xs.sorted())
    expect(ys.count() == 4)
  }
}

test "fib" {
  expect(fib(10) == 55)
}
`

// Benches: run once by asuka test, as tests; timed by --bench, compiled
// to C or on the VM; their results JSON, and compared with a run's.
func TestTestBench(t *testing.T) {
	dir := writePkg(t, map[string]string{"bench.asuka": benchSrc})
	out, err := asukaTest(t, dir)
	if err != nil || !strings.Contains(out, "ok   fib 10\nok   sorted\nok   fib\nok   3 tests passed") {
		t.Fatalf("as tests: %v\n%s", err, out)
	}
	for _, vm := range []bool{false, true} {
		args := []string{dir, "--bench", "--benchtime", "7x", "--json"}
		if vm {
			args = append(args, "--vm")
		}
		out, err := asukaTest(t, args...)
		if err != nil {
			t.Fatalf("--bench (vm %v): %v\n%s", vm, err, out)
		}
		lines := jsonLines(t, out)
		if len(lines) != 2 || lines[0]["bench"] != "fib 10" || lines[1]["bench"] != "sorted" {
			t.Fatalf("--bench: %v", lines)
		}
		for _, l := range lines {
			if l["n"] != 7.0 || l["samples"] != 1.0 || l["nsPerOp"].(float64) <= 0 || l["allocsPerOp"] == nil || l["bytesPerOp"] == nil || l["pos"] == nil {
				t.Errorf("a bench's result: %v", l)
			}
		}
		// fib allocates nothing; sorting a copy does, every iteration.
		if lines[0]["allocsPerOp"] != 0.0 || lines[1]["allocsPerOp"].(float64) < 1 {
			t.Errorf("allocations: %v", lines)
		}
	}
	base := filepath.Join(t.TempDir(), "base.json")
	out, _ = asukaTest(t, dir, "--bench", "--benchtime", "3x", "--json")
	if err := os.WriteFile(base, []byte(out), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err = asukaTest(t, dir, "--bench", "--benchtime", "3x", "--compare", base)
	if err != nil {
		t.Fatal(err)
	}
	line := regexp.MustCompile(`(?m)^bench fib 10 +\S+/op ±\d+% +0 allocs/op +0 B/op  \(1 × 3\)  was \S+/op: [+-]\d+\.\d%`)
	if !line.MatchString(out) {
		t.Errorf("--compare:\n%s", out)
	}
	for _, args := range [][]string{
		{dir, "--bench", "--fuzz"},
		{dir, "--bench", "--cover"},
		{dir, "--vm"},
		{dir, "--compare", base},
		{dir, "--bench", "--benchtime", "soon"},
	} {
		if _, err := asukaTest(t, args...); err == nil {
			t.Errorf("%v: no error", args)
		}
	}
}
