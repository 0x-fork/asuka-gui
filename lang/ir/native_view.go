package ir

import (
	"strings"
	"unicode"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// A package's native component (docs/native.md, "Declaring a view"):
// a Native view of the kind "<class>#<Name>", which each platform's
// generated code makes. Its parameters and props cross encoded as
// native calls' values (core/src/native), by their exported names
// (internal/nativegen's: value → Value), and its events come back the
// same way; the style keys are any element's.
const (
	uiNative     = "ui.native"      // (kind string) -> view
	uiNativeSet  = "ui.native_set"  // (view, name string, value, desc string) -> void; value borrowed
	uiNativeBind = "ui.native_bind" // (view, name string, thunk, desc string) -> void: a render effect
	uiNativeOn   = "ui.native_on"   // (view, name string, handler, desc string) -> void: the handler takes the event's values
)

// NativeViewKind is the kind a package's native component is shown as:
// its package's native class, # and its name.
func NativeViewKind(class, name string) string { return class + "#" + name }

// nativeName is a member's name on the wire, as internal/nativegen
// exports it: value → Value, on_tap → OnTap.
func nativeName(s string) string {
	var b strings.Builder
	up := true
	for _, c := range s {
		if !unicode.IsLetter(c) && !unicode.IsDigit(c) {
			up = true
			continue
		}
		if up {
			b.WriteRune(unicode.ToUpper(c))
			up = false
		} else {
			b.WriteRune(c)
		}
	}
	return b.String()
}

// nativeElement builds a package's native component's view.
func (b *fb) nativeElement(ec *check.ElementCall) int {
	tn := ec.Comp
	comp := tn.Comp
	v := b.local(bc.KRef)
	b.callBTo(uiNative, v, b.kstr(NativeViewKind(tn.Pkg.NativeClass, tn.Name())))
	// What each member is set to: its argument or prop, the last one
	// set winning, else its default; the style's keys apart.
	vals := map[*check.Member]syntax.Expr{}
	for i, m := range comp.Params {
		if a := ec.Args[i]; a != nil {
			vals[m] = a
		}
	}
	var styles []*check.PropArg
	for _, p := range ec.Props {
		if p.Member.Owner == tn {
			vals[p.Member] = p.Value
		} else {
			styles = append(styles, p)
		}
	}
	bound := map[*check.Member]bool{}
	for _, bd := range ec.Binds {
		bound[bd.Prop] = true
	}
	for _, m := range comp.Order {
		if m.Kind != check.ParamMember && m.Kind != check.PropMember || bound[m] {
			continue
		}
		x := vals[m]
		if x == nil {
			x = m.Default
		}
		if x == nil {
			continue // none: the platform's own
		}
		b.nativeProp(v, m, x)
	}
	last := map[*check.Member]int{}
	for i, p := range styles {
		last[p.Member] = i
	}
	for i, p := range styles {
		if last[p.Member] == i {
			b.setProp(v, "", p.Member, p.Value)
		}
	}
	for _, h := range ec.Handlers {
		clos := b.handler(h.Handler, h.Event)
		b.callBTo(uiNativeOn, bc.None, v, b.kstr(nativeName(h.Event.Name())), clos, b.kstr(b.nativeParamsDesc(h.Event)))
	}
	for _, bd := range ec.Binds {
		b.nativeBind(v, bd)
	}
	if len(ec.Slots) > 0 || len(ec.Children) > 0 {
		b.l.errorf(tn.Decl, "%s is a native component: it takes no children", tn.Name())
	}
	return v
}

// nativeProp sets a member of a native view to x: a binding when x reads
// what changes.
func (b *fb) nativeProp(v int, m *check.Member, x syntax.Expr) {
	mark := b.mark()
	defer b.release(mark)
	name := b.kstr(nativeName(m.Name()))
	desc := b.kstr(b.l.nativeDesc(m.Type))
	if b.l.prog.Reactive(x) {
		t := b.l.thunkType(x, m.Type)
		ctx := b.l.info.Ctxs[x]
		clos := b.thunk(ctx, x, t)
		b.callBTo(uiNativeBind, bc.None, v, name, clos, desc)
		return
	}
	val := b.value(x, -1)
	b.callBTo(uiNativeSet, bc.None, v, name, val, desc)
}

// nativeParamsDesc is an event's values' descriptors, in order.
func (b *fb) nativeParamsDesc(ev *check.Member) string {
	d := ""
	for _, p := range ev.Params {
		d += b.l.nativeDesc(p.Type)
	}
	return d
}

// nativeBind is bind name: target on a native view: the prop follows
// target, and the event <name>Change writes it.
func (b *fb) nativeBind(v int, bd *check.BindArg) {
	mark := b.mark()
	defer b.release(mark)
	readCtx := b.l.info.Ctxs[bd.Item]
	clos := b.thunk(readCtx, bd.Target, bd.Prop.Type)
	b.callBTo(uiNativeBind, bc.None, v, b.kstr(nativeName(bd.Prop.Name())), clos, b.kstr(b.l.nativeDesc(bd.Prop.Type)))
	wctx := b.l.info.Ctxs[bd.Target]
	params := make([]*check.Var, 1)
	pkinds := []bc.Kind{b.kind(bd.Prop.Type)}
	if len(wctx.Params) > 0 {
		params[0] = wctx.Params[0]
	}
	h := b.makeClosure(closure{name: "bind " + bd.Prop.Name(), ctxs: []*check.FuncCtx{wctx}, params: params, pkinds: pkinds, ret: bc.KVoid, throws: true, body: func(c *fb) {
		c.ctx = wctx
		if params[0] != nil {
			lv := c.prepare(bd.Target)
			c.set(lv, c.vars[params[0]])
		}
		c.emit(op("RET0"))
	}})
	b.callBTo(uiNativeOn, bc.None, v, b.kstr(nativeName(bd.Event.Name())), h, b.kstr(b.nativeParamsDesc(bd.Event)))
}
