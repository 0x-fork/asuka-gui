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
