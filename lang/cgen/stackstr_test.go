package cgen_test

import (
	"regexp"
	"strings"
	"testing"
)

// stackSites counts the strings a function's C makes in its frame.
func stackSites(c string) int {
	return len(regexp.MustCompile(`(?m)^\s*ak_strbuf S\d+_;`).FindAllString(c, -1))
}

// A string a function keeps to itself (only read in place: its length,
// a comparison, a search, a map's lookup, another string's part, a
// print) is made in a buffer in its frame: nothing allocated, nothing
// retained.
func TestStackStrings(t *testing.T) {
	src := `
let parts = ["ka", "to", "ra", "chi", "suyuq"]

fn concat(n: int) -> int {
  var total = 0
  for i in 0..<n {
    let k = parts[i % 5] + "-" + parts[(i / 5) % 5] + "/" + parts[(i / 25) % 5]
    total += k.byteCount()
    if k == "ka-ka/ka" || k.hasPrefix("to") || k < "b" || k.contains("x") { total += 1 }
    let two = parts[i % 5] + parts[(i + 1) % 5]
    total += two.count() + (two.find("a") ?? 0)
    print("{k} {i}")
  }
  return total
}

fn lookup(m: [string: int], i: int) -> int {
  let k = parts[i % 5] + "-" + parts[(i + 1) % 5]
  return m[k] ?? -1
}

fn main() { print("{concat(30)} {lookup(["ka-to": 1], 0)}") }
`
	c := cOf(t, src, "concat")
	if n := stackSites(c); n != 3 {
		t.Errorf("%d strings in the frame, want 3 (k, two, the print's):\n%s", n, c)
	}
	if !strings.Contains(c, "ak_strs_join_in(&S") || !strings.Contains(c, "ak_str_concat_in(&S") || !strings.Contains(c, "ak_op_interp_in(&S") {
		t.Errorf("not joined into the frame's buffers:\n%s", c)
	}
	// A copy of one (let k) is ak_retain_local's: ak_retain panics on
	// one in a build with assertions.
	if !strings.Contains(c, "ak_retain_local(") {
		t.Errorf("k's copy (let) not ak_retain_local's:\n%s", c)
	}
	if c := cOf(t, src, "lookup"); stackSites(c) != 1 {
		t.Errorf("a map's key looked up not in the frame:\n%s", c)
	}
}

// A string that goes anywhere is made on the heap: returned, stored in
// an array, a struct or a map, captured, carried by a register into the
// next turn of the loop that makes it, passed to a function (one not
// inlined: an inlined body's reads are the caller's).
func TestStackStringsEscape(t *testing.T) {
	src := `
struct Box {
  label: string
}

let parts = ["ka", "to", "ra", "chi", "suyuq"]

fn made(i: int) -> string {
  if i < 0 { return made(i + 1) }
  let k = parts[i % 5] + "/" + parts[(i + 2) % 5] + "/"
  return k
}

fn kept(n: int) -> int {
  var xs: [string] = []
  var boxes: [Box] = []
  var m: [string: int] = [:]
  var fs: [fn() -> int] = []
  var last = ""
  var total = 0
  for i in 0..<n {
    let a = parts[i % 5] + "+" + parts[(i + 1) % 5]
    xs.push(a)
    let b = parts[i % 5] + "*" + parts[(i + 3) % 5] + "*"
    boxes.push(Box(label: b))
    let c = parts[i % 5] + "=" + parts[i % 3]
    m[c] = i
    let d = parts[i % 5] + "~" + parts[(i + 4) % 5] + "~"
    fs.push(() => d.byteCount())
    if last.byteCount() > 3 { total += 1 }
    let e = parts[i % 5] + "^" + parts[(i + 2) % 5] + "^"
    last = e
    total += made(i).byteCount() + size(parts[i % 5] + "!" + parts[i % 2] + "!", 2)
  }
  return total + xs.count() + boxes.count() + m.count() + fs.count()
}

// Not inlined (it calls itself): its argument is a call's.
fn size(s: string, n: int) -> int = if n == 0 { s.byteCount() } else { size(s, n - 1) + 1 }

extend string {
  mut fn shout() { self = self + "!" + "!" }
}

fn loud(s: string) -> string {
  var t = s
  t.shout()
  return t
}

fn main() { print("{kept(10)} {loud("hi")}") }
`
	for _, fn := range []string{"made", "kept", "string.shout"} {
		if c := cOf(t, src, fn); stackSites(c) != 0 {
			t.Errorf("%s: a string that escapes made in the frame:\n%s", fn, c)
		}
	}
}
