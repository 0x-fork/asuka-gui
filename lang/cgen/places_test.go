package cgen_test

import (
	"strings"
	"testing"

	"github.com/egoist/asuka/lang/cgen"
	"github.com/egoist/asuka/lang/compile"
)

// cOf compiles src and returns the C of its function name's direct body,
// from the code as lang/ir's optimizer leaves it.
func cOf(t *testing.T, src, name string) string {
	t.Helper()
	return cOfOpt(t, src, name, true)
}

// cOfOpt is cOf from the optimized code or the code as lowered: the
// place analysis plans both.
func cOfOpt(t *testing.T, src, name string, opt bool) string {
	t.Helper()
	res := compile.Files(map[string][]byte{"a.asuka": []byte(src)}, &compile.Options{NoOptimize: !opt})
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	c, err := cgen.Generate(res.Module)
	if err != nil {
		t.Fatal(err)
	}
	s := string(c)
	at := strings.Index(s, "\n// "+name+" (")
	if at < 0 {
		t.Fatalf("no function %s in:\n%s", name, s)
	}
	s = s[at:]
	return s[:strings.Index(s, "\n}\n")]
}

const points = `
struct P {
  x: float
  n: int
  tag: string
}
`

// An element's field read is through a pointer borrowed from the array
// (the optimizer's IDXF reads it there too): the element isn't retained,
// nor released.
func TestBorrowedRead(t *testing.T) {
	for _, opt := range []bool{false, true} {
		c := cOfOpt(t, points+`
fn sumX(ps: [P]) -> float {
  var s = 0.0
  for i in 0..<ps.count() {
    s += ps[i].x
  }
  return s
}

fn main() { print("{sumX([P(x: 1.0, n: 1, tag: "a")])}") }
`, "sumX", opt)
		if !strings.Contains(c, "= *(void *const *)&x_->data[i_]; }") && !strings.Contains(c, "((ak_struct *)*(void *const *)&x_->data[i_])->f[0]") {
			t.Errorf("the element isn't borrowed (optimized: %v):\n%s", opt, c)
		}
		if strings.Contains(c, "ak_retain((x_->data[i_]).p)") {
			t.Errorf("the element is retained (optimized: %v):\n%s", opt, c)
		}
	}
}

// The optimizer's changes of an element's fields (SETIF) make the array
// and the element unique once, and check the index once, on a straight
// path: what's known after the first is known at the others.
func TestChangeFieldsInPlace(t *testing.T) {
	c := cOf(t, `
struct V {
  x: float
  y: float
  z: float
}

fn step(vs: [V]) -> [V] {
  var q = vs
  for i in 0..<q.count() {
    q[i].x += 1.0
    q[i].y += 2.0
    q[i].z += 3.0
  }
  return q
}

fn main() { print("{step([V(x: 1.0, y: 2.0, z: 3.0)]).count()}") }
`, "step")
	// The index isn't checked at all: the loop's test says it's in range.
	for what, n := range map[string]int{"ak_op_range": 0, "ak_struct_mut": 1, "ak_arr_mut": 1} {
		if got := strings.Count(c, what); got != n {
			t.Errorf("%s %d times, not %d:\n%s", what, got, n, c)
		}
	}
	if strings.Contains(c, "x_->data[i_].p = NULL") {
		t.Errorf("an element is taken out:\n%s", c)
	}
}

// A float field of an element changed (nothing on the way can throw) is
// changed where it is: no element taken out and put back.
func TestChangeInPlace(t *testing.T) {
	c := cOf(t, points+`
fn bump(ps: [P]) -> [P] {
  var q = ps
  for i in 0..<q.count() {
    q[i].x += 1.0
  }
  return q
}

fn main() { print("{bump([P(x: 1.0, n: 1, tag: "a")]).count()}") }
`, "bump")
	if !strings.Contains(c, "= s_; }") || !strings.Contains(c, "ak_struct_mut") {
		t.Errorf("not changed in place:\n%s", c)
	}
	if strings.Contains(c, "x_->data[i_].p = NULL") {
		t.Errorf("the element is taken out:\n%s", c)
	}
}

// An int field's change can throw (overflow), whose handler puts the
// element back: it's taken out and put back, as moves (no retain).
func TestTakeOutMoves(t *testing.T) {
	c := cOf(t, points+`
fn count(ps: [P]) -> [P] {
  var q = ps
  for i in 0..<q.count() {
    q[i].n += 1
  }
  return q
}

fn main() { print("{count([P(x: 1.0, n: 1, tag: "a")]).count()}") }
`, "count")
	if !strings.Contains(c, "x_->data[i_].p = NULL") {
		t.Errorf("the element isn't moved out:\n%s", c)
	}
	if strings.Contains(c, "ak_retain((x_->data[i_]).p)") {
		t.Errorf("the element is retained:\n%s", c)
	}
}

// A parameter only read is borrowed; a call of a compiled function is
// its direct body's, its error tested in the result.
func TestDirectCalls(t *testing.T) {
	c := cOf(t, points+`
fn tagLen(p: P) -> int = p.tag.byteCount()

fn total(ps: [P]) -> int {
  var t = 0
  for p in ps {
    t += tagLen(p)
  }
  return t
}

fn main() { print("{total([P(x: 1.0, n: 1, tag: "a")])}") }
`, "tagLen")
	if strings.Contains(c, "ak_retain(p0)") {
		t.Errorf("the parameter is retained:\n%s", c)
	}
	c = cOf(t, points+`
fn fib(n: int) -> int = if n < 2 { n } else { fib(n - 1) + fib(n - 2) }

fn main() { print("{fib(10)}") }
`, "fib")
	if !strings.Contains(c, "ak_ret v_ = f") || !strings.Contains(c, "if (AK_UNLIKELY(v_.e))") {
		t.Errorf("not a direct call:\n%s", c)
	}
	if strings.Contains(c, "ak_error_pending") || strings.Contains(c, "ak_cg_pending") {
		t.Errorf("a direct call tests the pending error:\n%s", c)
	}
}

// A counted loop over an array's indexes (0..<xs.count(), (i + 1)..<n
// with n its length) checks no index: its test says each is in range
// (flow.go). The array it changes is made unique as each loop is
// entered, not a turn, and the element at the outer loop's index once
// as the inner loop is entered.
func TestCountedLoops(t *testing.T) {
	c := cOf(t, `
struct V {
  x: float
  y: float
}

fn step(vs: [V], steps: int) -> [V] {
  var q = vs
  let n = q.count()
  for _ in 0..<steps {
    for i in 0..<n {
      for j in (i + 1)..<n {
        q[i].x += q[j].y
        q[j].y += 1.0
      }
    }
  }
  return q
}

fn main() { print("{step([V(x: 1.0, y: 2.0), V(x: 3.0, y: 4.0)], 3).count()}") }
`, "step")
	if strings.Contains(c, "ak_op_range") {
		t.Errorf("an index is checked:\n%s", c)
	}
	if got := strings.Count(c, "ak_arr_mut"); got != 1 {
		t.Errorf("the array made unique %d times, not once (before the inner loop):\n%s", got, c)
	}
	// The inner loop's body: from its first instruction's label to its
	// jump back.
	inner := c[strings.LastIndex(c, "ak_arr_mut"):]
	inner = inner[strings.Index(inner, "\nL"):]
	if got := strings.Count(inner[:strings.Index(inner, "goto L")], "ak_struct_mut"); got != 1 {
		t.Errorf("elements made unique %d times a turn, not once (q[j]'s):\n%s", got, c)
	}
}

// A closure call whose arguments' kinds are its function's parameters'
// calls the function's direct body with them in registers, when it's a
// compiled one (its sig).
func TestTypedClosureCalls(t *testing.T) {
	c := cOf(t, `
fn apply(f: fn(int, float) -> bool, a: int, b: float) -> bool = f(a, b)

fn main() { print("{apply((x, y) => x.float() < y, 1, 2.0)}") }
`, "apply")
	if !strings.Contains(c, "g_->sig == 0x80020004u") || !strings.Contains(c, "(ak_closure *, uintptr_t, int64_t, double))g_->body)(c_, lim_, ") {
		t.Errorf("not a typed call:\n%s", c)
	}
}
