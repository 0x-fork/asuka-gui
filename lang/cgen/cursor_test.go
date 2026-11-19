package cgen_test

import (
	"regexp"
	"strings"
	"testing"
)

// A fused loop's appends into its own array (map's, filter's) store
// through a cursor the loop keeps in locals, the array's count written
// back on each way out: falling out of its end, and an error's (the
// overflow check in map's closure), through a stub.
func TestWriteCursors(t *testing.T) {
	c := cOf(t, `
fn seqs(n: int) -> int = (0..<n).lazy().toArray().map(x => x * 3).filter(x => x % 2 == 0).map(x => x + 1).reduce(0, (s, x) => s + x)

fn main() { print("{seqs(100)}") }
`, "seqs")
	if strings.Contains(c, "ak_op_append_room_") || strings.Contains(c, "ak_op_append_own_") {
		t.Errorf("an append through the array:\n%s", c)
	}
	opens := regexp.MustCompile(`(?m)^\s*AK_CUR_OPEN\(`).FindAllString(c, -1)
	if len(opens) != 3 {
		t.Errorf("%d cursors opened as loops start, want 3:\n%s", len(opens), c)
	}
	// Each loop's end, and the two maps' overflows (a stub each).
	if n := strings.Count(c, "AK_CUR_CLOSE("); n < 3+2 {
		t.Errorf("%d closes, want at least 5:\n%s", n, c)
	}
	if !regexp.MustCompile(`X\d+_:;\n\s*AK_CUR_CLOSE\(r\d+\.p, W\d+_\); goto unwind_;`).MatchString(c) {
		t.Errorf("an error's way out doesn't close the cursor:\n%s", c)
	}
	// The appends are stores through the cursor.
	if !regexp.MustCompile(`\*W\d+_\+\+ = r\d+\.i;`).MatchString(c) {
		t.Errorf("no store through a cursor:\n%s", c)
	}
}

// An array the loop reads, or passes on, as well as appends to isn't
// written through a cursor: its count must be right everywhere.
func TestWriteCursorsNotWhenRead(t *testing.T) {
	c := cOf(t, `
fn grow(n: int) -> [int] {
  var ys: [int] = []
  for i in 0..<n {
    ys.push(i)
    if ys.count() > 5 { ys.push(ys[0]) }
  }
  return ys
}

fn main() { print("{grow(10)}") }
`, "grow")
	if strings.Contains(c, "AK_CUR_OPEN(") {
		t.Errorf("a cursor on an array the loop reads:\n%s", c)
	}
}
