package compile

import (
	"strings"
	"testing"
)

// An effect that writes what it reads where the compiler can't see it (a
// closure it calls) isn't a compile error (rfcs/0012's check follows
// what it sees): the run-time guard stops it, and names the effect, its
// line and the state it wrote last, on the VM and compiled to C.
func TestEffectLoopGuard(t *testing.T) {
	const src = `store Looper {
  state runs = 0
  state step: fn() = () => {}

  effect {
    let _ = runs
    step()
  }
}

test "an effect's loop the compiler can't see" {
  let l = Looper()
  l.step = () => { l.runs += 1 }
  print("unreached")
}
`
	res := Files(map[string][]byte{"loop.asuka": []byte(src)}, nil)
	if err := res.Err(); err != nil {
		t.Fatalf("compile:\n%v", err)
	}
	out, err := runBoth(t, res.Module)
	if err == nil {
		t.Fatalf("the runner should fail:\n%s", out)
	}
	const want = "signal: Looper's effect at loop.asuka:5 is still writing runs after 100 passes"
	if !strings.Contains(out, want) || strings.Contains(out, "unreached") {
		t.Fatalf("got:\n%s\nwant %q", out, want)
	}
}
