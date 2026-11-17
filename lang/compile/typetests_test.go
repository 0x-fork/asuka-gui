package compile

import (
	"strings"
	"testing"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/target"
)

// A type test of a type parameter's value is decided as each
// instantiation is compiled: the arms and the if lets that can't match
// aren't in the module (their strings neither), and nothing tests the
// type when it runs; an interface's value is tested with ISSHAPE.
func TestTypeTestsDecidedPerInstantiation(t *testing.T) {
	res := compileFor(t, map[string]string{"main.asuka": `
interface Shape { fn area() -> float }
struct Circle { radius: float; fn area() -> float = radius }
struct Square { side: float; fn area() -> float = side }

fn kind<T: Shape>(x: T) -> string = match x {
  c: Circle => "circle arm"
  Square(side) => "square arm"
  _ => "other arm"
}

fn radius<T>(x: T) -> float {
  if let c: Circle = x {
    print("radius branch")
    return c.radius
  } else {
    print("not a circle")
  }
  return 0
}

fn dynamic(s: Shape) -> string = match s {
  c: Circle => "dynamic circle"
  _ => "dynamic other"
}

test "t" {
  print(kind(Square(side: 1)))
  print(radius(3))
  let s: Shape = Circle(radius: 1)
  print(dynamic(s))
}
`}, target.Linux)
	m := res.Module
	for _, s := range []string{"circle arm", "other arm", "radius branch"} {
		if hasString(m, s) {
			t.Errorf("%q is in the module: its branch can't run", s)
		}
	}
	for _, s := range []string{"square arm", "not a circle", "dynamic circle", "dynamic other"} {
		if !hasString(m, s) {
			t.Errorf("%q isn't in the module", s)
		}
	}
	isShape := 0
	for i := range m.Funcs {
		f := &m.Funcs[i]
		d := bc.Disasm(m, f)
		n := strings.Count(d, "ISSHAPE")
		switch {
		case strings.HasPrefix(f.Name, "dynamic"):
			isShape += n
		case n > 0:
			t.Errorf("%s tests a type when it runs:\n%s", f.Name, d)
		}
	}
	if isShape != 1 {
		t.Errorf("dynamic has %d ISSHAPEs, want 1", isShape)
	}
}
