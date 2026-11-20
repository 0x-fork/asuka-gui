package cgen_test

import (
	"regexp"
	"strings"
	"testing"
)

// A temp's value is let go of after its last read, before what's made
// next (lang/ir's opt_release.go): of three splits counted in one
// statement, each array is freed after its count, so one is held at a
// time, not three.
func TestTempsReleasedAfterLastRead(t *testing.T) {
	c := cOf(t, `
fn counts(text: string) -> int = text.split(" ").count() + text.lines().count() + text.fields().count()

fn main() { print(counts("a b\nc d")) }
`, "counts")
	lens := regexp.MustCompile(`ak_op_len\(r(\d+)\.p\)`)
	lines := strings.Split(c, "\n")
	n := 0
	for i, l := range lines {
		m := lens.FindStringSubmatch(l)
		if m == nil || i+1 >= len(lines) {
			continue
		}
		n++
		if !strings.Contains(lines[i+1], "void *n_ = (NULL); void *o_ = r"+m[1]+".p;") {
			t.Errorf("r%s isn't let go of after its count:\n%s", m[1], c)
		}
	}
	if n != 3 {
		t.Errorf("%d counts, not 3:\n%s", n, c)
	}
}

// A local of data alone a loop reads is let go of as the loop ends, not
// held while what comes after is made; a closure local is held to its
// block's end (what letting go of it does is the program's to see).
func TestDataLocalsReleasedAfterLoop(t *testing.T) {
	src := `
fn twice(n: int) -> int {
  let words = "a b c d e".split(" ")
  var total = 0
  for w in words { total += w.byteCount() + n }
  let more = "{total} {n}".split(" ")
  return more.count()
}

fn main() { print(twice(3)) }
`
	c := cOf(t, src, "twice")
	// The array the loop reads (words, borrowed by the loop: no copy),
	// cleared as the way out of it starts, before the second split.
	arr := regexp.MustCompile(`const ak_arr \*x_ = r(\d+)\.p;`).FindStringSubmatch(c)
	// The loop's end: its jump back, then the label of the way out.
	ends := regexp.MustCompile(`goto (L\d+);\n(L\d+):;\n(.*)\n`).FindAllStringSubmatch(c, -1)
	var back []string
	if len(ends) > 0 {
		back = ends[len(ends)-1]
	}
	if arr == nil || back == nil || !strings.Contains(back[3], "void *n_ = (NULL); void *o_ = r"+arr[1]+".p;") {
		t.Errorf("the loop's array isn't let go of as the loop ends:\n%s", c)
	}
}
