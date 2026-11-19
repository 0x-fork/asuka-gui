package cgen_test

import (
	"strings"
	"testing"

	"github.com/egoist/asuka/lang/cgen"
	"github.com/egoist/asuka/lang/compile"
)

// cOf compiles src and returns the C of its function name's direct body.
func cOf(t *testing.T, src, name string) string {
	t.Helper()
	res := compile.Files(map[string][]byte{"a.asuka": []byte(src)}, nil)
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

// An element's field read is through a pointer borrowed from the array:
// the element isn't retained, nor released.
func TestBorrowedRead(t *testing.T) {
	c := cOf(t, points+`
fn sumX(ps: [P]) -> float {
  var s = 0.0
  for i in 0..<ps.count() {
    s += ps[i].x
  }
  return s
}

fn main() { print("{sumX([P(x: 1.0, n: 1, tag: "a")])}") }
`, "sumX")
	if !strings.Contains(c, "= x_->data[i_].p; }") {
		t.Errorf("the element isn't borrowed:\n%s", c)
	}
	if strings.Contains(c, "ak_retain((x_->data[i_]).p)") {
		t.Errorf("the element is retained:\n%s", c)
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
	if strings.Contains(c, "ak_error_pending") {
		t.Errorf("a direct call tests the pending error:\n%s", c)
	}
}
