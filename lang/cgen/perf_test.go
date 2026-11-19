package cgen_test

import (
	"regexp"
	"strings"
	"testing"
)

// An error leaves a compiled function through ak_cg_unwind, a cold
// function, so the C compiler lays the error's paths out of the way
// (a call's test of its result falls through when it didn't throw).
func TestColdUnwind(t *testing.T) {
	c := cOf(t, `
fn down(n: int) -> int = if n < 2 { n } else { down(n - 1) + down(n - 2) }

fn main() { print("{down(20)}") }
`, "down")
	if !strings.Contains(c, `ak_cg_unwind("down", `) || strings.Contains(c, "ak_frame_hook") {
		t.Errorf("not unwound through ak_cg_unwind:\n%s", c)
	}
}

// A sum of two indexes (each less than 2^32, as an array's length is)
// can't overflow: it isn't checked (and is opaque to the C compiler, as
// the checked add was). One of ints that could be anything is.
func TestIndexSumsUnchecked(t *testing.T) {
	c := cOf(t, `
fn sieve(n: int) -> int {
  var composite = Seq(repeating: false, count: n + 1).toArray()
  var count = 0
  var i = 2
  while i <= n {
    if !composite[i] {
      count += 1
      var j = i * i
      while j <= n {
        composite[j] = true
        j += i
      }
    }
    i += 1
  }
  return count
}

fn main() { print("{sieve(100)}") }
`, "sieve")
	if !regexp.MustCompile(`r\d+\.i = r\d+\.i \+ r\d+\.i; AK_CG_OPAQUE\(r\d+\.i\);`).MatchString(c) {
		t.Errorf("j += i checked:\n%s", c)
	}
	if !strings.Contains(c, "ak_addi(&") {
		t.Errorf("count += 1 not checked (it can grow without bound):\n%s", c)
	}
	c = cOf(t, `
fn add(a: int, b: int) -> int = a + b

fn main() { print("{add(1, 2)}") }
`, "add")
	if !strings.Contains(c, "ak_addi(&") {
		t.Errorf("an add of any ints not checked:\n%s", c)
	}
}

// A file's let (a global only the module's init writes) is read borrowed,
// not retained and released; a var, which a function may change, is
// retained.
func TestStableGlobalBorrowed(t *testing.T) {
	src := `
let names = ["ann", "bob", "cy"]
var seen = ["x"]

fn pick(i: int) -> int = names[i % 3].byteCount() + seen[0].byteCount()

fn main() {
  seen = ["yy"]
  print("{pick(1)}")
}
`
	c := cOf(t, src, "pick")
	if !regexp.MustCompile(`v\d+ = M->globals\[\d+\]\.p;`).MatchString(c) {
		t.Errorf("the let isn't borrowed:\n%s", c)
	}
	if !strings.Contains(c, "ak_retain(M->globals[") {
		t.Errorf("the var isn't retained:\n%s", c)
	}
	// byteCount is the string's length, read in place.
	if !strings.Contains(c, "->len") || strings.Contains(c, "M->builtins[") {
		t.Errorf("byteCount not read in place:\n%s", c)
	}
}

// A parameter an inlined body only reads is the caller's register: the
// argument isn't copied in (retained) nor let go of after.
func TestInlinedParamsRead(t *testing.T) {
	c := cOf(t, `
fn first(xs: [int]) -> int = xs[0] + xs[xs.count() - 1]

fn twice(ys: [int]) -> int = first(ys) + first(ys)

fn main() { print("{twice([1, 2, 3])}") }
`, "twice")
	if strings.Contains(c, "ak_retain(") {
		t.Errorf("an inlined argument retained:\n%s", c)
	}
}

// An assignment's value is let go of once the place holds it, so the
// optimizer makes going down a tree (n = n.kids[i]) one read into n: the
// child retained once, not copied from a temp that kept it too.
func TestAssignedTempMoved(t *testing.T) {
	c := cOf(t, `
struct Node {
  keys: [int] = []
  kids: [Node] = []
}

fn depth(root: Node) -> int {
  var n = root
  var d = 0
  while !n.kids.isEmpty() {
    n = n.kids[0]
    d += 1
  }
  return d
}

fn main() { print("{depth(Node(kids: [Node()]))}") }
`, "depth")
	if regexp.MustCompile(`ak_retain\(r\d+\.p\)\); void \*o_ = r\d+\.p;`).MatchString(c[strings.Index(c, "\nL"):]) {
		t.Errorf("a register copied (retained) in the loop:\n%s", c)
	}
}
