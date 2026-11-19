package compile

import (
	"testing"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// A mut fn changes its struct's array field in place: when it takes the
// field out of self to change it (SETF of none), no other register still
// holds the field, read by the change's right side, its arguments, a
// condition or a let before it. One that did would make the change copy
// the whole array: a heap's swap O(n), not O(1).
func TestFieldChangedInPlace(t *testing.T) {
	src := `
struct Heap {
  items: [int] = []
  less: fn(int, int) -> bool

  mut fn swap(i: int, j: int) {
    let t = items[i]
    items[i] = items[j]
    items[j] = t
  }
  mut fn siftUp(i: int) {
    if less(items[i], items[0]) && items.count() > 1 { swap(i, 0) }
    while i > 0 && less(items[i], items[i - 1]) {
      items[i - 1] = items[i]
      items.push(items[0])
      items[0] += items[i]
    }
  }
}

test "heap" {
  var h = Heap(less: (a, b) => a < b)
  h.items.push(2)
  h.siftUp(0)
}
`
	res := Files(map[string][]byte{"heap.asuka": []byte(src)}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	getf, setf, clr := bc.Byname["GETF"], bc.Byname["SETF"], bc.Byname["SETIDX"]
	clear := bc.Byname["CLR"]
	checked := 0
	for _, f := range res.IR.Funcs {
		if f.Name != "Heap.swap" && f.Name != "Heap.siftUp" {
			continue
		}
		// The registers holding self's items (field 0), by a GETF not yet
		// cleared or written over.
		held := map[int32]bool{}
		for i, in := range f.Code {
			a := in.Args
			switch in.Op {
			case getf:
				delete(held, a[0])
				if a[1] == 0 && a[2] == 0 {
					held[a[0]] = true
				}
			case setf:
				if a[0] == 0 && a[2] != 0 && i > 0 && f.Code[i-1].Op == getf {
					// Taken out: only the register it's taken into holds it.
					taken := f.Code[i-1].Args[0]
					for r := range held {
						if r != taken {
							t.Errorf("%s: at %d, r%d still holds items as it's taken out into r%d", f.Name, i, r, taken)
						}
					}
					checked++
				}
			case clear:
				delete(held, a[0])
			case clr:
				// SETIDX changes its first register's array: not a write of it.
			default:
				delete(held, a[0])
			}
		}
	}
	if checked < 5 {
		t.Fatalf("checked %d takes, want the swap's and siftUp's", checked)
	}
}
