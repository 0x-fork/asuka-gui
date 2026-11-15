package core

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
	bc "github.com/egoist/asuka/lang/bytecode"
)

// These tests run hand-assembled modules on the VM (asuka-run, built
// with sanitizers) and compare what they print. The compiler's own tests
// (lang/...) cover the language; these cover the VM's instructions.

var runnerPath string

func runner(t *testing.T) string {
	t.Helper()
	if runnerPath != "" {
		return runnerPath
	}
	if _, err := cbuild.Find(); err != nil {
		t.Skip(err)
	}
	root, _ := filepath.Abs(".")
	exe, err := cbuild.Runner(root, true)
	if err != nil {
		t.Fatal(err)
	}
	runnerPath = exe
	return exe
}

func run(t *testing.T, m *bc.Module, args ...string) (string, error) {
	t.Helper()
	p := filepath.Join(t.TempDir(), "m.akb")
	if err := os.WriteFile(p, m.Encode(), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := exec.Command(runner(t), append(args, p)...).CombinedOutput()
	return string(out), err
}

// fn helps assemble a function: registers by kind, ops by name.
type fn struct {
	m   *bc.Module
	def bc.FuncDef
	a   bc.Asm
}

func newFn(m *bc.Module, name string, params ...bc.Kind) *fn {
	f := &fn{m: m, def: bc.FuncDef{Name: name, File: "test.asuka", NParams: uint16(len(params))}}
	f.def.Kinds = append(f.def.Kinds, params...)
	return f
}

func (f *fn) reg(k bc.Kind) int {
	f.def.Kinds = append(f.def.Kinds, k)
	return len(f.def.Kinds) - 1
}

func (f *fn) op(name string, args ...int) int {
	op, ok := bc.Byname[name]
	if !ok {
		panic("no op " + name)
	}
	return f.a.Emit(op, args...)
}

func (f *fn) str(s string) int  { return int(f.m.String(s)) }
func (f *fn) b(name string) int { return int(f.m.Builtin(name)) }
func (f *fn) pc() int           { return len(f.a.Code) }

// print emits a print of register r of kind k.
func (f *fn) print(r int, k bc.Kind) {
	s := r
	if k != bc.KRef {
		s = f.reg(bc.KRef)
		f.op("TOSTR", s, r, int(k))
	}
	f.op("CALLB", bc.None, f.b("print"), s, 1)
}

func (f *fn) done(ret bc.Kind, flags uint8) int {
	f.def.Code = f.a.Code
	f.def.Ret = ret
	f.def.Flags = flags
	f.m.Funcs = append(f.m.Funcs, f.def)
	return len(f.m.Funcs) - 1
}

func module() *bc.Module { return &bc.Module{App: -1} }

func addInit(m *bc.Module) {
	f := newFn(m, "init")
	f.op("RET0")
	m.Init = uint32(f.done(bc.KVoid, 0))
}

func test(m *bc.Module, name string, i int) {
	m.Tests = append(m.Tests, bc.Test{Name: name, Func: uint32(i)})
}

func expect(t *testing.T, got string, want ...string) {
	t.Helper()
	g := strings.Split(strings.TrimSpace(got), "\n")
	if strings.Join(g, "\n") != strings.Join(want, "\n") {
		t.Fatalf("got:\n%s\nwant:\n%s", strings.TrimSpace(got), strings.Join(want, "\n"))
	}
}

func TestVMArith(t *testing.T) {
	m := module()
	addInit(m)
	f := newFn(m, "t")
	a, b, c := f.reg(bc.KInt), f.reg(bc.KInt), f.reg(bc.KInt)
	f.op("KSMALL", a, 40)
	f.op("KSMALL", b, 2)
	f.op("ADDI", c, a, b)
	f.print(c, bc.KInt)
	x, y := f.reg(bc.KFloat), f.reg(bc.KFloat)
	f.op("I2F", x, c)
	m.Floats = append(m.Floats, 0.5)
	f.op("KFLOAT", y, 0)
	f.op("MULF", x, x, y)
	f.print(x, bc.KFloat)
	// Interpolation: "n={c} half={x} ok={true}"
	s1, n1, s2, n2, s3, n3 := f.reg(bc.KRef), f.reg(bc.KInt), f.reg(bc.KRef), f.reg(bc.KFloat), f.reg(bc.KRef), f.reg(bc.KBool)
	f.op("KSTR", s1, f.str("n="))
	f.op("MOV", n1, c)
	f.op("KSTR", s2, f.str(" half="))
	f.op("MOV", n2, x)
	f.op("KSTR", s3, f.str(" ok="))
	f.op("KSMALL", n3, 1)
	out := f.reg(bc.KRef)
	f.op("INTERP", out, s1, 6, f.str(string([]byte{4, 1, 4, 2, 4, 3})))
	f.print(out, bc.KRef)
	// Division by zero throws.
	z := f.reg(bc.KInt)
	f.op("KSMALL", z, 0)
	f.op("DIVI", c, a, z)
	f.op("RET0")
	test(m, "arith", f.done(bc.KVoid, bc.FThrows))
	got, err := run(t, m, "-test")
	if err == nil {
		t.Fatal("want a failure")
	}
	expect(t, got, "42", "21", "n=42 half=21 ok=true", "FAIL arith: division by zero")
}

func TestVMCallsAndLoops(t *testing.T) {
	m := module()
	addInit(m)
	// fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
	fib := newFn(m, "fib", bc.KInt)
	two, c, r1, r2, n1 := fib.reg(bc.KInt), fib.reg(bc.KBool), fib.reg(bc.KInt), fib.reg(bc.KInt), fib.reg(bc.KInt)
	fib.op("KSMALL", two, 2)
	fib.op("LTI", c, 0, two)
	j := fib.op("JF", c, 0)
	fib.op("RET", 0)
	fib.a.PatchJump(j, fib.pc())
	fib.op("ADDIK", n1, 0, -1)
	fibIdx := len(m.Funcs) // this function's index once done
	fib.op("CALL", r1, fibIdx, n1, 1)
	fib.op("ADDIK", n1, 0, -2)
	fib.op("CALL", r2, fibIdx, n1, 1)
	fib.op("ADDI", r1, r1, r2)
	fib.op("RET", r1)
	fib.done(bc.KInt, 0)

	f := newFn(m, "t")
	n, res := f.reg(bc.KInt), f.reg(bc.KInt)
	f.op("KSMALL", n, 20)
	f.op("CALL", res, fibIdx, n, 1)
	f.print(res, bc.KInt)
	// A loop summing 1..100.
	i, sum, lim, cond := f.reg(bc.KInt), f.reg(bc.KInt), f.reg(bc.KInt), f.reg(bc.KBool)
	f.op("KSMALL", i, 1)
	f.op("KSMALL", sum, 0)
	f.op("KSMALL", lim, 100)
	top := f.pc()
	f.op("LEI", cond, i, lim)
	exit := f.op("JF", cond, 0)
	f.op("ADDI", sum, sum, i)
	f.op("ADDIK", i, i, 1)
	back := f.op("JMP", 0)
	f.a.PatchJump(back, top)
	f.a.PatchJump(exit, f.pc())
	f.print(sum, bc.KInt)
	f.op("RET0")
	test(m, "calls", f.done(bc.KVoid, 0))
	got, err := run(t, m, "-test")
	if err != nil {
		t.Fatalf("%v\n%s", err, got)
	}
	expect(t, got, "6765", "5050", "ok calls")
}

func TestVMClosuresStructsArrays(t *testing.T) {
	m := module()
	addInit(m)
	m.Shapes = []bc.ShapeDef{{Name: "Todo", Fields: []bc.Field{{Name: "title", Kind: bc.KRef, Shape: -1}, {Name: "done", Kind: bc.KBool, Shape: -1}}}}
	// isDone(todo) = todo.done
	isDone := newFn(m, "isDone", bc.KRef)
	d := isDone.reg(bc.KBool)
	isDone.op("GETF", d, 0, 1)
	isDone.op("RET", d)
	isDoneIdx := isDone.done(bc.KBool, 0)
	// title(todo) = todo.title + suffix (a capture)
	title := newFn(m, "title", bc.KRef)
	title.def.CapKinds = []bc.Kind{bc.KRef}
	tr, suf := title.reg(bc.KRef), title.reg(bc.KRef)
	title.op("GETF", tr, 0, 0)
	title.op("CAP", suf, 0)
	title.op("CONCAT", tr, tr, suf)
	title.op("RET", tr)
	titleIdx := title.done(bc.KRef, 0)

	f := newFn(m, "t")
	// Three todos.
	fields := []int{f.reg(bc.KRef), f.reg(bc.KBool)}
	todos := []int{f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KRef)}
	for i, name := range []string{"milk", "eggs", "bread"} {
		f.op("KSTR", fields[0], f.str(name))
		f.op("KSMALL", fields[1], map[bool]int{true: 1, false: 0}[i == 1])
		f.op("NEWS", todos[i], 0, fields[0])
	}
	arr := f.reg(bc.KRef)
	f.op("NEWA", arr, int(bc.KRef), 0, todos[0], 3)
	// A copy, then a change to the copy: the original keeps its value.
	cp := f.reg(bc.KRef)
	f.op("MOVR", cp, arr)
	one, flag, el := f.reg(bc.KInt), f.reg(bc.KBool), f.reg(bc.KRef)
	f.op("KSMALL", one, 0)
	f.op("IDX", el, cp, one)
	f.op("KSMALL", flag, 1)
	f.op("SETF", el, 1, flag)
	f.op("SETIDX", cp, one, el)
	// filter(isDone) on both.
	pred, done1, done2, n := f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KInt)
	f.op("CLOS", pred, isDoneIdx, 0, 0)
	args := []int{f.reg(bc.KRef), f.reg(bc.KRef)}
	f.op("MOVR", args[0], arr)
	f.op("MOVR", args[1], pred)
	f.op("CALLB", done1, f.b("arr.filter"), args[0], 2)
	f.op("LEN", n, done1)
	f.print(n, bc.KInt)
	f.op("MOVR", args[0], cp)
	f.op("CALLB", done2, f.b("arr.filter"), args[0], 2)
	f.op("LEN", n, done2)
	f.print(n, bc.KInt)
	// map(title + "!") with a captured suffix.
	suf2, mapper, kind, mapped := f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KInt), f.reg(bc.KRef)
	f.op("KSTR", suf2, f.str("!"))
	f.op("CLOS", mapper, titleIdx, suf2, 1)
	margs := []int{f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KInt)}
	f.op("MOVR", margs[0], arr)
	f.op("MOVR", margs[1], mapper)
	f.op("KSMALL", margs[2], int(bc.KRef))
	_ = kind
	f.op("CALLB", mapped, f.b("arr.map"), margs[0], 3)
	sep, joined := f.reg(bc.KRef), f.reg(bc.KRef)
	f.op("KSTR", sep, f.str(", "))
	jargs := []int{f.reg(bc.KRef), f.reg(bc.KRef)}
	f.op("MOVR", jargs[0], mapped)
	f.op("MOVR", jargs[1], sep)
	f.op("CALLB", joined, f.b("str.join"), jargs[0], 2)
	f.print(joined, bc.KRef)
	// push in place.
	f.op("CALLB", bc.None, f.b("arr.push"), arr, 2) // arr.push(arr, pred?) no: push todos[0]
	f.op("RET0")
	// Fix the last push: arguments must be consecutive (arr, value).
	f.a.Code = f.a.Code[:len(f.a.Code)-(1+4)-1]
	pa := []int{f.reg(bc.KRef), f.reg(bc.KRef)}
	f.op("MOVR", pa[0], arr)
	f.op("MOVR", pa[1], todos[2])
	f.op("CALLB", bc.None, f.b("arr.push"), pa[0], 2)
	f.op("LEN", n, pa[0])
	f.print(n, bc.KInt)
	f.op("LEN", n, arr)
	f.print(n, bc.KInt)
	f.op("RET0")
	test(m, "values", f.done(bc.KVoid, 0))
	got, err := run(t, m, "-test")
	if err != nil {
		t.Fatalf("%v\n%s", err, got)
	}
	expect(t, got, "1", "2", "milk!, eggs!, bread!", "4", "3", "ok values")
}

func TestVMErrorsAndHandlers(t *testing.T) {
	m := module()
	addInit(m)
	// boom() throws error("boom")
	boom := newFn(m, "boom")
	msg, e := boom.reg(bc.KRef), boom.reg(bc.KRef)
	boom.op("KSTR", msg, boom.str("boom"))
	boom.op("CALLB", e, boom.b("error"), msg, 1)
	boom.op("THROW", e)
	boomIdx := boom.done(bc.KVoid, bc.FThrows)

	f := newFn(m, "t")
	start := f.pc()
	f.op("CALL", bc.None, boomIdx, 0, 0)
	end := f.pc()
	ok := f.reg(bc.KRef)
	f.op("KSTR", ok, f.str("not reached"))
	f.print(ok, bc.KRef)
	f.op("RET0")
	handler := f.pc()
	err := f.reg(bc.KRef)
	f.op("CATCH", err)
	m2, s := f.reg(bc.KRef), f.reg(bc.KRef)
	f.op("CALLB", m2, f.b("error.message"), err, 1)
	f.op("KSTR", s, f.str("caught: "))
	f.op("CONCAT", s, s, m2)
	f.print(s, bc.KRef)
	f.op("RET0")
	f.def.Handlers = []bc.Handler{{Start: uint32(start), End: uint32(end), Target: uint32(handler)}}
	test(m, "catch", f.done(bc.KVoid, 0))

	// Out-of-range index throws past a function with no handler.
	g := newFn(m, "u")
	arr, i, el := g.reg(bc.KRef), g.reg(bc.KInt), g.reg(bc.KInt)
	g.op("NEWA", arr, int(bc.KInt), bc.None, 0, 0)
	g.op("KSMALL", i, 3)
	g.op("IDX", el, arr, i)
	g.op("RET0")
	test(m, "range", g.done(bc.KVoid, bc.FThrows))
	got, runErr := run(t, m, "-test")
	if runErr == nil {
		t.Fatal("want a failure")
	}
	expect(t, got, "caught: boom", "ok catch", "FAIL range: index 3 out of range (length 0)")
}

func TestVMStateAndEffects(t *testing.T) {
	m := module()
	addInit(m)
	// effect body: print("count=" + cell)
	body := newFn(m, "body")
	body.def.CapKinds = []bc.Kind{bc.KRef}
	c, v, s, out := body.reg(bc.KRef), body.reg(bc.KInt), body.reg(bc.KRef), body.reg(bc.KRef)
	body.op("CAP", c, 0)
	body.op("CGET", v, c)
	body.op("KSTR", s, body.str("count="))
	body.op("MOV", out, s) // placeholder, replaced below
	body.a.Code = body.a.Code[:len(body.a.Code)-3]
	parts := []int{body.reg(bc.KRef), body.reg(bc.KInt)}
	body.op("MOVR", parts[0], s)
	body.op("MOV", parts[1], v)
	body.op("INTERP", out, parts[0], 2, body.str(string([]byte{4, 1})))
	body.print(out, bc.KRef)
	body.op("RET0")
	bodyIdx := body.done(bc.KVoid, 0)
	// memo: cell * 2
	dbl := newFn(m, "double")
	dbl.def.CapKinds = []bc.Kind{bc.KRef}
	dc, dv := dbl.reg(bc.KRef), dbl.reg(bc.KInt)
	dbl.op("CAP", dc, 0)
	dbl.op("CGET", dv, dc)
	dbl.op("ADDI", dv, dv, dv)
	dbl.op("RET", dv)
	dblIdx := dbl.done(bc.KInt, 0)

	f := newFn(m, "t")
	zero, cell, eff, k := f.reg(bc.KInt), f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KInt)
	f.op("KSMALL", zero, 0)
	f.op("CELL", cell, zero, int(bc.KInt))
	f.op("CLOS", eff, bodyIdx, cell, 1)
	f.op("EFFECT", eff)
	f.op("KSMALL", k, 1)
	f.op("CSET", cell, k)
	f.op("CSET", cell, k) // equal: no effect
	// A batch: two writes, one run.
	f.op("BATCH", 1)
	f.op("KSMALL", k, 2)
	f.op("CSET", cell, k)
	f.op("KSMALL", k, 3)
	f.op("CSET", cell, k)
	f.op("BATCH", 0)
	memoClos, memo, mv := f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KInt)
	f.op("CLOS", memoClos, dblIdx, cell, 1)
	f.op("MEMO", memo, memoClos, int(bc.KInt))
	f.op("MREAD", mv, memo)
	f.print(mv, bc.KInt)
	f.op("RET0")
	test(m, "state", f.done(bc.KVoid, 0))
	got, err := run(t, m, "-test")
	if err != nil {
		t.Fatalf("%v\n%s", err, got)
	}
	expect(t, got, "count=0", "count=1", "count=3", "6", "ok state")
}

func TestVMTasks(t *testing.T) {
	m := module()
	addInit(m)
	// async worker(): sleep 1s; print "slept"; return 7
	w := newFn(m, "worker")
	ns, fut, seven := w.reg(bc.KInt), w.reg(bc.KRef), w.reg(bc.KInt)
	m.Ints = append(m.Ints, 1_000_000_000)
	w.op("KINT", ns, 0)
	w.op("CALLB", fut, w.b("time.sleep"), ns, 1)
	w.op("AWAIT", bc.None, fut)
	msg := w.reg(bc.KRef)
	w.op("KSTR", msg, w.str("slept"))
	w.print(msg, bc.KRef)
	w.op("KSMALL", seven, 7)
	w.op("RET", seven)
	wIdx := w.done(bc.KInt, bc.FAsync)

	f := newFn(m, "t")
	clos, task, res := f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KInt)
	f.op("CLOS", clos, wIdx, 0, 0)
	f.op("TASK", task, clos)
	before := f.reg(bc.KRef)
	f.op("KSTR", before, f.str("started"))
	f.print(before, bc.KRef)
	f.op("AWAIT", res, task)
	f.print(res, bc.KInt)
	// A direct call of an async function runs on this task.
	f.op("CALL", res, wIdx, 0, 0)
	f.print(res, bc.KInt)
	f.op("RET0")
	test(m, "tasks", f.done(bc.KVoid, bc.FAsync))
	got, err := run(t, m, "-test")
	if err != nil {
		t.Fatalf("%v\n%s", err, got)
	}
	expect(t, got, "started", "slept", "7", "slept", "7", "ok tasks")
}
