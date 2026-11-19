package compile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/ir"
)

// The optimizer (lang/ir's Optimize) changes how a program runs, never
// what it does: each testdata/opt/*.asuka program, compiled with and
// without it, writes the same output and the same errors with the same
// traces (their lines included), and exits the same way, on the VM and
// compiled to C. The programs are what the passes rewrite: constants
// folded or left to throw, fused comparisons, an array's struct
// elements changed in place, mut fns' receivers, calls whose argument
// copies are folded away.
func TestOptimizerKeepsBehavior(t *testing.T) {
	files, _ := filepath.Glob("testdata/opt/*.asuka")
	if len(files) == 0 {
		t.Fatal("no programs")
	}
	runner := testRunner(t)
	for _, path := range files {
		path := path
		t.Run(strings.TrimSuffix(filepath.Base(path), ".asuka"), func(t *testing.T) {
			t.Parallel()
			src, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			compile := func(noopt bool) *Result {
				res := Files(map[string][]byte{path: src}, &Options{NoOptimize: noopt})
				if err := res.Err(); err != nil {
					t.Fatalf("compile:\n%v", err)
				}
				if err := res.IR.Validate(); err != nil {
					t.Fatalf("invalid IR (optimized: %v): %v", !noopt, err)
				}
				return res
			}
			opt, plain := compile(false), compile(true)
			if same(opt.Module, plain.Module) {
				t.Fatal("the optimizer changed nothing")
			}
			for _, backend := range []string{"VM", "C"} {
				run := func(m *bc.Module) MainRun {
					var r MainRun
					var err error
					if backend == "VM" {
						r, err = RunMain(runner, m, MainOptions{})
					} else {
						r, err = RunMainC(m, true, MainOptions{})
					}
					if err != nil {
						t.Fatalf("%s: %v\n%s%s", backend, err, r.Stdout, r.Stderr)
					}
					return r
				}
				got, want := run(opt.Module), run(plain.Module)
				if got != want {
					t.Errorf("%s: optimized:\n%s\n%s(exit %d)\nnot optimized:\n%s\n%s(exit %d)", backend, got.Stdout, got.Stderr, got.Code, want.Stdout, want.Stderr, want.Code)
				}
				if testing.Verbose() {
					t.Logf("%s:\n%s%s", backend, got.Stdout, got.Stderr)
				}
			}
		})
	}
}

// same reports whether two modules' functions have the same code.
func same(a, b *bc.Module) bool {
	if len(a.Funcs) != len(b.Funcs) {
		return false
	}
	for i := range a.Funcs {
		if !slicesEqual(a.Funcs[i].Code, b.Funcs[i].Code) {
			return false
		}
	}
	return true
}

func slicesEqual(a, b []uint16) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// optimized compiles src and returns its function named name, optimized.
func optimized(t *testing.T, src, name string) *ir.Func {
	t.Helper()
	res := Files(map[string][]byte{"opt.asuka": []byte(src)}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if err := res.IR.Validate(); err != nil {
		t.Fatal(err)
	}
	for _, f := range res.IR.Funcs {
		if f.Name == name {
			return f
		}
	}
	t.Fatalf("no function %s", name)
	return nil
}

// ops counts f's instructions by opcode name.
func ops(f *ir.Func) map[string]int {
	n := map[string]int{}
	for _, in := range f.Code {
		n[bc.Ops[in.Op].Name]++
	}
	return n
}

// A counted loop: its test fused with its jump, its step an immediate,
// its jump back the test (one jump a turn), the copies of its counter
// gone.
func TestOptimizedLoop(t *testing.T) {
	f := optimized(t, `
fn loop(n: int) -> int {
  var s = 0
  var i = 0
  while i < n {
    s = (s + i * 3) % 1000003
    i += 1
  }
  return s
}
test "t" { expect(loop(10) == 135) }
`, "loop")
	n := ops(f)
	if n["LTI"] != 0 || n["JF"] != 0 || n["MOV"] != 0 || n["JMP"] != 0 {
		t.Errorf("comparisons, branches on them, copies or jumps left: %v", n)
	}
	if n["JLEI"] != 1 || n["JLTI"] != 1 || n["ADDIS"] != 1 || n["MULIS"] != 1 {
		t.Errorf("want the test fused (JLEI in, JLTI back), the step and the multiply immediates: %v", n)
	}
	if len(f.Code) > 11 {
		t.Errorf("%d instructions, want at most 11", len(f.Code))
	}
}

// Constants fold, but not into an overflow: that stays, to throw.
func TestOptimizedConstants(t *testing.T) {
	f := optimized(t, `
fn k() -> int {
  let a = 40
  let b = 2
  return a * b + a / b - a % 7
}
fn over() -> int {
  let big = 9223372036854775807
  return big + 1
}
test "t" { expect(k() == 95) }
`, "k")
	n := ops(f)
	if len(f.Code) != 2 || n["KSMALL"] != 1 || n["RET"] != 1 {
		t.Errorf("want k folded to KSMALL 95, RET: %v", n)
	}
	res := Files(map[string][]byte{"opt.asuka": []byte(`
fn over() -> int {
  let big = 9223372036854775807
  return big + 1
}
test "t" { expect(over() == 0) }
`)}, nil)
	for _, g := range res.IR.Funcs {
		if g.Name == "over" && ops(g)["ADDIS"]+ops(g)["ADDI"] != 1 {
			t.Errorf("an overflow folded away: %v", ops(g))
		}
	}
}

// xs[i].f and xs[i].f op= v read and change the element where it is:
// no IDX, SETIDX, GETF or SETF of a copy, no handler to put it back.
func TestOptimizedElements(t *testing.T) {
	f := optimized(t, `
struct B {
  x: float
  vx: float
}
fn step(bs: [B], dt: float) -> [B] {
  var bodies = bs
  for i in 0..<bodies.count() {
    bodies[i].x += dt * bodies[i].vx
    bodies[i].vx -= bodies[i].x
  }
  return bodies
}
test "t" { expect(step([B(x: 1, vx: 2)], 0.5)[0].x == 2) }
`, "step")
	n := ops(f)
	if n["IDX"] != 0 || n["SETIDX"] != 0 || n["GETF"] != 0 || n["SETF"] != 0 || n["CATCH"] != 0 || len(f.Handlers) != 0 {
		t.Errorf("an element taken out and put back: %v, %d handlers", n, len(f.Handlers))
	}
	if n["IDXF"] != 4 || n["SETIF"] != 2 {
		t.Errorf("want 4 IDXF and 2 SETIF: %v", n)
	}
}

// A mut fn's last write of its receiver is kept: it goes back to the
// caller.
func TestOptimizedMutReceiver(t *testing.T) {
	f := optimized(t, `
enum L {
  a
  b
  mut fn flip() { self = match self { .a => .b, .b => .a } }
}
test "t" { var l = L.a; l.flip(); expect(l == .b) }
`, "L.flip")
	wrote := false
	for _, in := range f.Code {
		if in.Args[0] == 0 && in.Op != bc.Byname["RET0"] && strings.HasPrefix(bc.Ops[in.Op].Name, "") {
			switch bc.Ops[in.Op].Name {
			case "MOV", "KSMALL":
				wrote = true
			}
		}
	}
	if !wrote {
		t.Errorf("the receiver's write is gone:\n%v", f.Code)
	}
}
