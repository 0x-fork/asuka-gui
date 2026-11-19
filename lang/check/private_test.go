package check

import (
	"sort"
	"strconv"
	"strings"
	"testing"
)

// private: a declaration, or a type's member, is its file's (and its
// package's tests'); other files can't name it.
func TestPrivate(t *testing.T) {
	a := `pub struct Heap {
  by: int = 0
  private items: [int] = []
  private secret: int

  private fn up() -> int = 1
  fn count() -> int = items.count() + up()
  private static fn make() -> Heap = Heap(secret: 1)
  static fn of() -> Heap = Heap.make()
  private let limit = 3
  private fn add(o: Heap) -> Heap = o
}

pub struct Open {
  private items: [int] = []
  fn count() -> int = items.count()
}

private fn helper() -> int = 2
private struct Node {
  x: int
}

fn sameFile() -> int {
  let h = Heap(secret: 2)
  match Open() { Open(items) => print(items) }
  return h.items.count() + h.secret + helper() + Node(x: 1).x + Heap.limit
}
`
	b := `interface Upper {
  fn up() -> int
}

fn other() {
  let h = Heap.of()
  print(h.count())
  print(h.items)
  print(h.up())
  let k = Heap(secret: 1)
  let o = Open()
  let o2 = Open(items: [1])
  print(helper())
  let n: Node? = none
  print(Heap.limit)
  let m = Heap.make()
  match o { Open(items) => print(items) }
  let u: Upper = h
  let sum = h + h
}

extend Open {
  fn twice() -> int = items.count() * 2
}
`
	test := `test "the package's tests see its insides" {
  let h = Heap(secret: 3)
  expect(h.items.isEmpty() && h.secret == 3 && helper() == 2 && Heap.limit == 3)
}
`
	_, diags := checkSrc(t, map[string]string{"pkg/a.asuka": a, "pkg/b.asuka": b, "pkg/a_test.asuka": test})
	var got []string
	for _, d := range diags {
		got = append(got, d.Pos.Filename+":"+strconv.Itoa(d.Pos.Line)+": "+d.Code+": "+d.Msg)
	}
	sort.Strings(got)
	want := []string{
		"pkg/b.asuka:10: not-pub: Heap is made in a.asuka only",
		"pkg/b.asuka:12: not-pub: Open.items is private to a.asuka",
		"pkg/b.asuka:13: not-pub: helper is private to a.asuka",
		"pkg/b.asuka:14: not-pub: Node is private to a.asuka",
		"pkg/b.asuka:15: not-pub: Heap.limit is private to a.asuka",
		"pkg/b.asuka:16: not-pub: Heap.make is private to a.asuka",
		"pkg/b.asuka:17: not-pub: Open.items is private to a.asuka",
		"pkg/b.asuka:18: type-mismatch: Heap isn't a Upper: its up is private",
		"pkg/b.asuka:19: bad-operand: + doesn't take a Heap",
		"pkg/b.asuka:23: not-pub: Open.items is private to a.asuka",
		"pkg/b.asuka:8: not-pub: Heap.items is private to a.asuka",
		"pkg/b.asuka:9: not-pub: Heap.up is private to a.asuka",
	}
	if len(got) != len(want) {
		t.Fatalf("got %d diagnostics:\n%s", len(got), strings.Join(got, "\n"))
	}
	for i := range want {
		if !strings.HasPrefix(got[i], want[i]) {
			t.Errorf("got %q, want %q", got[i], want[i])
		}
	}
}
