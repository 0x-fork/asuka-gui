// Package apptest hand-assembles small apps as the compiler lowers them
// (lang/LOWERING.md), for the tests of what builds and runs a module
// (internal/appbuild, the CLI) while the compiler is written: a Counter
// with its test block.
package apptest

import (
	"fmt"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/prelude"
)

// File is the source file the functions say they're from.
const File = "app.asuka"

// fn helps assemble a function: registers by kind, ops by name.
type fn struct {
	m   *bc.Module
	def bc.FuncDef
	a   bc.Asm
}

func newFn(m *bc.Module, name string, line int) *fn {
	f := &fn{m: m, def: bc.FuncDef{Name: name, File: File}}
	if line > 0 {
		f.def.Lines = []bc.Line{{PC: 0, Line: uint32(line)}}
	}
	return f
}

func (f *fn) reg(k bc.Kind) int {
	f.def.Kinds = append(f.def.Kinds, k)
	return len(f.def.Kinds) - 1
}

func (f *fn) op(name string, args ...int) {
	op, ok := bc.Byname[name]
	if !ok {
		panic("apptest: no op " + name)
	}
	f.a.Emit(op, args...)
}

func (f *fn) str(s string) int { return int(f.m.String(s)) }

// callb emits a builtin's call with args copied into consecutive
// registers of their kinds (CALLB takes a run of registers).
func (f *fn) callb(dst int, name string, args ...[2]int) {
	first := -1
	for i, a := range args {
		r := f.reg(bc.Kind(a[1]))
		if i == 0 {
			first = r
		}
		if bc.Kind(a[1]) == bc.KRef {
			f.op("MOVR", r, a[0])
		} else {
			f.op("MOV", r, a[0])
		}
	}
	if first < 0 {
		first = 0
	}
	f.op("CALLB", dst, int(f.m.Builtin(name)), first, len(args))
}

func (f *fn) done(ret bc.Kind, flags uint8) int {
	f.def.Code = f.a.Code
	f.def.Ret = ret
	f.def.Flags = flags
	f.m.Funcs = append(f.m.Funcs, f.def)
	return len(f.m.Funcs) - 1
}

func ref(r int) [2]int { return [2]int{r, int(bc.KRef)} }
func num(r int) [2]int { return [2]int{r, int(bc.KInt)} }

// Counter is
//
//	component Counter {
//	  state count = 0
//	  Column {
//	    gap: 8
//	    Text("<label>{count}")
//	    Button("Add") { on press { count += 1 } }
//	  }
//	}
//	app { Counter() }
//	test "Add counts" { let app = mount(Counter()); app.tap("Add"); app.expect("<label>1") }
//
// as a module: its app, its test. Two Counters with different labels
// keep each other's count across a reload (the state cell is
// Counter.count in both).
func Counter(label string) *bc.Module {
	m := &bc.Module{App: -1}
	in := newFn(m, "init", 0)
	in.op("RET0")
	m.Init = uint32(in.done(bc.KVoid, 0))

	// The Text's binding: "<label>{count}".
	lf := newFn(m, "Counter.label", 6)
	lf.def.CapKinds = []bc.Kind{bc.KRef}
	c, v, s, out := lf.reg(bc.KRef), lf.reg(bc.KInt), lf.reg(bc.KRef), lf.reg(bc.KRef)
	lf.op("CAP", c, 0)
	lf.op("CGET", v, c)
	lf.op("KSTR", s, lf.str(label))
	parts := []int{lf.reg(bc.KRef), lf.reg(bc.KInt)}
	lf.op("MOVR", parts[0], s)
	lf.op("MOV", parts[1], v)
	lf.op("INTERP", out, parts[0], 2, lf.str(string([]byte{byte(bc.KRef), byte(bc.KInt)})))
	lf.op("RET", out)
	labelIdx := lf.done(bc.KRef, 0)

	// on press { count += 1 }
	press := newFn(m, "Counter.press", 7)
	press.def.CapKinds = []bc.Kind{bc.KRef}
	pc, pv := press.reg(bc.KRef), press.reg(bc.KInt)
	press.op("CAP", pc, 0)
	press.op("CGET", pv, pc)
	press.op("ADDIK", pv, pv, 1)
	press.op("CSET", pc, pv)
	press.op("RET0")
	pressIdx := press.done(bc.KVoid, 0)

	body := newFn(m, "Counter", 1)
	name := body.reg(bc.KRef)
	body.op("KSTR", name, body.str("Counter"))
	body.callb(bc.None, "ui.enter", ref(name))
	zero, cell := body.reg(bc.KInt), body.reg(bc.KRef)
	body.op("KSMALL", zero, 0)
	body.op("CELLK", cell, zero, int(bc.KInt), body.str("Counter.count"))
	k := body.reg(bc.KInt)
	col := body.reg(bc.KRef)
	body.op("KSMALL", k, elID("Column"))
	body.callb(col, "ui.new", num(k))
	gap := body.reg(bc.KFloat)
	m.Floats = append(m.Floats, 8)
	body.op("KFLOAT", gap, len(m.Floats)-1)
	body.op("KSMALL", k, keyID("", "gap"))
	body.callb(bc.None, "ui.set", ref(col), num(k), [2]int{gap, int(bc.KFloat)})
	txt, thunk := body.reg(bc.KRef), body.reg(bc.KRef)
	body.op("KSMALL", k, elID("Text"))
	body.callb(txt, "ui.new", num(k))
	body.op("CLOS", thunk, labelIdx, cell, 1)
	body.op("KSMALL", k, keyID("Text", "text"))
	body.callb(bc.None, "ui.bind", ref(txt), num(k), ref(thunk))
	body.callb(bc.None, "ui.add", ref(col), ref(txt))
	btn, title, handler := body.reg(bc.KRef), body.reg(bc.KRef), body.reg(bc.KRef)
	body.op("KSMALL", k, elID("Button"))
	body.callb(btn, "ui.new", num(k))
	body.op("KSTR", title, body.str("Add"))
	body.op("KSMALL", k, keyID("Button", "title"))
	body.callb(bc.None, "ui.set", ref(btn), num(k), ref(title))
	body.op("CLOS", handler, pressIdx, cell, 1)
	body.op("KSMALL", k, evID("Button", "press"))
	body.callb(bc.None, "ui.on", ref(btn), num(k), ref(handler))
	body.callb(bc.None, "ui.add", ref(col), ref(btn))
	body.callb(bc.None, "ui.leave")
	body.op("RET", col)
	bodyIdx := body.done(bc.KRef, 0)
	m.App = int32(bodyIdx)

	m.Tests = append(m.Tests, bc.Test{Name: "Add counts", Func: uint32(expectTest(m, bodyIdx, label+"1", 12))})
	return m
}

// Failing adds to m a test that expects what its Counter never shows:
// "<label>7" before anything is tapped.
func Failing(m *bc.Module, label string) {
	m.Tests = append(m.Tests, bc.Test{Name: "shows seven", Func: uint32(expectTest(m, int(m.App), label+"7", 18))})
}

// expectTest is a test that mounts the component body, taps Add when
// want ends in 1, and expects want.
func expectTest(m *bc.Module, body int, want string, line int) int {
	f := newFn(m, fmt.Sprintf("test%d", len(m.Tests)), line)
	build, app, s := f.reg(bc.KRef), f.reg(bc.KRef), f.reg(bc.KRef)
	f.op("CLOS", build, body, 0, 0)
	f.callb(app, "test.mount", ref(build))
	if want[len(want)-1] == '1' {
		f.op("KSTR", s, f.str("Add"))
		f.callb(bc.None, "test.tap", ref(app), ref(s))
	}
	f.op("KSTR", s, f.str(want))
	f.callb(bc.None, "test.expect", ref(app), ref(s))
	f.op("RET0")
	return f.done(bc.KVoid, bc.FThrows)
}

// The UI runtime's numbers (core/src/ui/elements_gen.h), from the
// prelude's tables they're generated from.

func elID(name string) int {
	for i, e := range prelude.Elements {
		if e.Name == name {
			return i + 1
		}
	}
	panic("apptest: no element " + name)
}

func keyID(el, name string) int {
	n := 0
	if el == "" {
		for _, k := range prelude.Style {
			n++
			if k.Name == name {
				return n
			}
		}
		panic("apptest: no style key " + name)
	}
	n = len(prelude.Style)
	for _, e := range prelude.Elements {
		for _, k := range e.Keys {
			n++
			if e.Name == el && k.Name == name {
				return n
			}
		}
		n += len(e.Slots)
	}
	panic("apptest: no key " + el + "." + name)
}

func evID(el, name string) int {
	n := 0
	for _, e := range prelude.Elements {
		for _, ev := range e.Events {
			n++
			if e.Name == el && ev.Name == name {
				return n
			}
		}
	}
	panic("apptest: no event " + el + "." + name)
}
