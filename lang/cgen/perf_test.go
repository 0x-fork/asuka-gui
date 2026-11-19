package cgen_test

import (
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
