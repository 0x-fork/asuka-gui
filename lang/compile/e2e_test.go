package compile

import (
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
	bc "github.com/egoist/asuka/lang/bytecode"
)

// The language's conformance suite: each testdata/run/*.asuka is a
// program whose test blocks run on the VM (asuka-run, built with
// sanitizers); what it prints must be what the comment block at its end,
// after a line "// Output:", says (the runner's "ok name" lines
// included). A file whose first line is "// skip: reason" is skipped.
// A program's C (its externs') is the .c file of its name beside it.

var dumpFlag = flag.Bool("dump", false, "print each module's disassembly")

var (
	runnerOnce sync.Once
	runnerExe  string
	runnerErr  error
)

// runBoth runs m's tests on the VM, then compiled to C (lang/cgen), and
// fails when the two backends don't print the same or don't agree on
// failing: the conformance suite holds for both. It returns the VM's.
func runBoth(t *testing.T, m *bc.Module) (string, error) {
	return runBothWith(t, m, nil)
}

// runBothWith is runBoth with the C m's externs call (f) built into
// both runners.
func runBothWith(t *testing.T, m *bc.Module, f *cbuild.Foreign) (string, error) {
	t.Helper()
	runner := testRunner(t)
	if f == nil && m.C != nil {
		// The C of the std packages it imports (std/sqlite's SQLite).
		var err error
		if f, err = Foreign(m, nil, nil); err != nil {
			t.Fatal(err)
		}
	}
	if f != nil {
		var err error
		if runner, err = RunnerFor(f, true, t.TempDir()); err != nil {
			t.Fatal(err)
		}
	}
	out, err := Run(runner, m, true, "")
	cout, cerr := RunCWith(m, f, true, true, "")
	if _, ok := cerr.(*exec.ExitError); cerr != nil && !ok {
		t.Fatalf("the C backend: %v\n%s", cerr, cout)
	}
	if cout != out || (cerr == nil) != (err == nil) {
		t.Fatalf("the VM and the compiled C disagree:\nVM (%v):\n%s\nC (%v):\n%s", err, out, cerr, cout)
	}
	return out, err
}

func testRunner(t *testing.T) string {
	t.Helper()
	if _, err := cbuild.Find(); err != nil {
		t.Skip(err)
	}
	runnerOnce.Do(func() { runnerExe, runnerErr = Runner(true) })
	if runnerErr != nil {
		t.Fatal(runnerErr)
	}
	return runnerExe
}

// wantOutput is the expected output written at the end of src.
func wantOutput(src string) (string, bool) {
	i := strings.Index(src, "\n// Output:\n")
	if i < 0 {
		return "", false
	}
	var lines []string
	for _, l := range strings.Split(strings.TrimSpace(src[i+len("\n// Output:\n"):]), "\n") {
		l = strings.TrimPrefix(l, "//")
		lines = append(lines, strings.TrimPrefix(l, " "))
	}
	return strings.Join(lines, "\n"), true
}

func TestRun(t *testing.T) {
	files, _ := filepath.Glob("testdata/run/*.asuka")
	if len(files) == 0 {
		t.Fatal("no programs")
	}
	for _, path := range files {
		path := path
		name := strings.TrimSuffix(filepath.Base(path), ".asuka")
		t.Run(name, func(t *testing.T) {
			t.Parallel() // each runs in processes of its own
			src, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			if strings.HasPrefix(string(src), "// skip:") {
				t.Skip(strings.SplitN(string(src), "\n", 2)[0])
			}
			want, ok := wantOutput(string(src))
			if !ok {
				t.Fatal("no // Output: block")
			}
			res := Files(map[string][]byte{path: src}, nil)
			if err := res.Err(); err != nil {
				t.Fatalf("compile:\n%v", err)
			}
			if err := res.IR.Validate(); err != nil {
				t.Fatalf("invalid IR: %v", err)
			}
			if *dumpFlag {
				for i := range res.Module.Funcs {
					t.Log(bc.Disasm(res.Module, &res.Module.Funcs[i]))
				}
			}
			var f *cbuild.Foreign
			if c, _ := filepath.Abs(strings.TrimSuffix(path, ".asuka") + ".c"); fileExists(c) {
				if f, err = Foreign(res.Module, []cbuild.CUnit{{Name: name, Sources: []string{c}}}, nil); err != nil {
					t.Fatal(err)
				}
			}
			out, err := runBothWith(t, res.Module, f)
			got := strings.TrimSpace(out)
			if got != want {
				t.Fatalf("got:\n%s\nwant:\n%s", got, want)
			}
			if strings.Contains(want, "FAIL ") {
				if err == nil {
					t.Fatal("the runner should fail")
				}
			} else if err != nil {
				if _, ok := err.(*exec.ExitError); !ok {
					t.Fatal(err)
				}
				t.Fatalf("runner: %v", err)
			}
		})
	}
}

func fileExists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}

// A literal of any length compiles, in as many registers as a short
// one: the rest of its elements are added one by one.
func TestBigLiteral(t *testing.T) {
	var src strings.Builder
	src.WriteString("let table = [")
	for i := range 70000 {
		if i > 0 {
			src.WriteString(", ")
		}
		src.WriteString(strconv.Itoa(i))
	}
	src.WriteString("]\nfn sum() -> int = table[0] + table[69999]\n")
	res := Files(map[string][]byte{"big.asuka": []byte(src.String())}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if err := res.IR.Validate(); err != nil {
		t.Fatal(err)
	}
	for _, f := range res.Module.Funcs {
		if len(f.Kinds) > 1000 {
			t.Errorf("%s has %d registers", f.Name, len(f.Kinds))
		}
	}
}

// A string of any number of parts and an enum of any number of cases
// (its names' table) compile in as many registers as small ones (a
// block of each part would take 2000 here), and run on the VM and as C.
func TestBigStringsAndEnums(t *testing.T) {
	var src strings.Builder
	src.WriteString("enum Big {\n")
	for i := range 1000 {
		fmt.Fprintf(&src, "  c%d\n", i)
	}
	src.WriteString("}\n\nfn parts(x: int) -> string = \"")
	for range 1000 {
		src.WriteString("{x}-")
	}
	src.WriteString("\"\n\ntest \"big\" {\n  print(\"{Big.c999} {Big.c300}\")\n  let s = parts(7)\n  print(\"{s.count()} {s.hasPrefix(\"7-7-\")} {s.hasSuffix(\"7-\")}\")\n}\n")
	res := Files(map[string][]byte{"big.asuka": []byte(src.String())}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if err := res.IR.Validate(); err != nil {
		t.Fatal(err)
	}
	for _, f := range res.Module.Funcs {
		if len(f.Kinds) > 1000 {
			t.Errorf("%s has %d registers", f.Name, len(f.Kinds))
		}
	}
	out, err := runBoth(t, res.Module)
	if want := "c999 c300\n2000 true true\nok big"; err != nil || !strings.HasPrefix(strings.TrimSpace(out), want) {
		t.Fatalf("got (%v):\n%s\nwant:\n%s", err, out, want)
	}
}

// What needs more than the bytecode holds (a register or an operand is
// 16 bits) is a compile error where it is, not a panic.
func TestBytecodeLimits(t *testing.T) {
	gen := func(f func(b *strings.Builder)) string {
		var b strings.Builder
		f(&b)
		return b.String()
	}
	for _, tc := range []struct {
		name, src, want string
	}{
		{"locals", gen(func(b *strings.Builder) {
			b.WriteString("fn f() -> int {\n")
			for i := range 70000 {
				fmt.Fprintf(b, "  let v%d = %d\n", i, i)
			}
			b.WriteString("  return v0 + v69999\n}\n\ntest \"t\" { print(f()) }\n")
		}), `big.asuka:65535:\d+: this needs more than 65534 registers \(values held at once\) in one function`},
		{"cases", gen(func(b *strings.Builder) {
			b.WriteString("enum E {\n")
			for i := range 70000 {
				fmt.Fprintf(b, "  c%d(int)\n", i)
			}
			b.WriteString("}\n\nfn f(n: int) -> E = E.c69999(n)\n\ntest \"t\" { let e = f(1) }\n")
		}), `big.asuka:70004:\d+: this needs more than 65535 enum cases in one instruction`},
	} {
		t.Run(tc.name, func(t *testing.T) {
			res := Files(map[string][]byte{"big.asuka": []byte(tc.src)}, nil)
			err := res.Err()
			if err == nil || !regexp.MustCompile(tc.want).MatchString(err.Error()) {
				t.Fatalf("got %v, want %s", err, tc.want)
			}
		})
	}
}

// A nested element is taken out of its place before it's changed (none
// put there), so that it's changed in place, not copied: IDX t, a, i
// then SETIDX a, i, none, before t changes.
func TestNestedChangeInPlace(t *testing.T) {
	src := "fn set(g: [[int]], i: int) -> [[int]] {\n  var x = g\n  x[i][0] = 1\n  return x\n}\ntest \"t\" { print(set([[0]], 0)) }\n"
	res := Files(map[string][]byte{"a.asuka": []byte(src)}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	m := res.Module
	for _, f := range m.Funcs {
		if f.Name != "set" {
			continue
		}
		type insn struct {
			name string
			args []int
		}
		var ins []insn
		for pc := 0; pc < len(f.Code); {
			op := bc.Op(f.Code[pc])
			in := insn{name: bc.Ops[op].Name}
			at := pc + 1
			for _, o := range bc.Ops[op].Operands {
				in.args = append(in.args, int(f.Code[at]))
				at += o.Words()
			}
			ins = append(ins, in)
			pc += op.Width()
		}
		// The optimizer may read the element from the register x was
		// copied from (MOVR x, g: the same array, the same references).
		copyOf := map[int]int{}
		for i := 0; i+1 < len(ins); i++ {
			a, b := ins[i], ins[i+1]
			if a.name == "MOVR" {
				copyOf[a.args[0]] = a.args[1]
			}
			same := b.args[0] == a.args[1] || copyOf[b.args[0]] == a.args[1]
			if a.name == "IDX" && b.name == "SETIDX" && same && b.args[1] == a.args[2] && b.args[2] != a.args[0] {
				return
			}
		}
		t.Fatalf("x[i] isn't taken out before it changes:\n%s", bc.Disasm(m, &f))
	}
	t.Fatal("no fn set")
}

// m[k] ?? d reads the map's value as it is (MGETJ), no optional made
// (MGET boxes a scalar).
func TestMapReadWithDefault(t *testing.T) {
	src := "fn count(m: [string: int], k: string) -> int = m[k] ?? 0\ntest \"t\" { print(count([\"a\": 1], \"a\")) }\n"
	res := Files(map[string][]byte{"a.asuka": []byte(src)}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	for _, f := range res.Module.Funcs {
		if f.Name != "count" {
			continue
		}
		d := bc.Disasm(res.Module, &f)
		if !strings.Contains(d, "MGETJ") || strings.Contains(d, "MGET ") {
			t.Fatalf("m[k] ?? 0:\n%s", d)
		}
		return
	}
	t.Fatal("no fn count")
}
