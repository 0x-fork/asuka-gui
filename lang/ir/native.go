package ir

import (
	"strconv"
	"strings"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Native packages (LOWERING.md "Native packages"): a native interface's
// objects are the runtime's, made and called through the native.*
// builtins, with the package's errors and the values' descriptors.

const (
	nativeNew   = "native.new"   // (cls string, iface int, errors [Error]) -> object
	nativeCall  = "native.call"  // (obj, method int, args tuple, argsDesc, retDesc, errors) -> result
	nativeStart = "native.start" // (same) -> Task<result>
)

// nativeNewCall is X(): an object of the package's interface X.
func (b *fb) nativeNewCall(call *check.Call, dst int) int {
	tn := call.Type.Obj
	cls := b.kstr(tn.Pkg.NativeClass)
	idx := b.konst(int64(tn.NativeIndex))
	errs := b.temp(bc.KRef)
	b.emit(op("GGET"), errs, b.l.nativeErrors(tn.Pkg), int(bc.KRef))
	return b.copyTo(b.callB(nativeNew, bc.KRef, cls, idx, errs), dst)
}

// nativeMethodCall is x.m(…) on a native object: its arguments as a
// tuple, encoded by their descriptors; an async method's call is
// awaited.
func (b *fb) nativeMethodCall(e *syntax.CallExpr, call *check.Call, recv *int, dst int) int {
	x := b.recvValue(call, recv)
	vals := b.args(call, nil)
	types := make([]check.Type, len(call.Params))
	argsDesc := ""
	for i, p := range call.Params {
		types[i] = b.typ(p.Type)
		argsDesc += b.l.nativeDesc(types[i])
	}
	var args int
	if len(vals) == 0 {
		args = b.valueReg(-1, bc.KRef)
	} else {
		args = b.newStruct(b.l.shape(&check.Tuple{Elems: types}), types, vals, -1)
	}
	res := b.typ(call.Result)
	ad, rd := b.kstr(argsDesc), b.kstr(b.l.nativeDesc(res))
	errs := b.temp(bc.KRef)
	b.emit(op("GGET"), errs, b.l.nativeErrors(b.l.ifaceOf(call.RecvType).Pkg), int(bc.KRef))
	method := b.konst(int64(call.Index))
	k := b.l.kind(res)
	async := false
	if sig := b.l.nativeSig(call); sig != nil {
		async = sig.Async
	}
	if async {
		f := b.callB(nativeStart, bc.KRef, x, method, args, ad, rd, errs)
		if k == bc.KVoid {
			b.emit(op("AWAIT"), b.temp(bc.KRef), f)
			return -1
		}
		r := b.out(dst, k)
		b.emit(op("AWAIT"), r, f)
		return r
	}
	if k == bc.KVoid {
		b.callBTo(nativeCall, bc.None, x, method, args, ad, rd, errs)
		return -1
	}
	r := b.out(dst, k)
	b.callBTo(nativeCall, r, x, method, args, ad, rd, errs)
	return r
}

// ifaceOf is the native interface a receiver's type is.
func (l *Lowerer) ifaceOf(t check.Type) *check.TypeName {
	if n, ok := check.Unopt(t).(*check.Named); ok {
		return n.Obj
	}
	return nil
}

// nativeSig is the called method's signature.
func (l *Lowerer) nativeSig(call *check.Call) *check.Signature {
	tn := l.ifaceOf(call.RecvType)
	if tn == nil || call.Index < 1 || call.Index > len(tn.Iface.Methods) {
		return nil
	}
	return tn.Iface.Methods[call.Index-1].Sig
}

// nativeDesc is t's descriptor on the native wire (core/src/native/
// native.h): JSON's (desc), with native objects (o), callbacks
// (F<n><params><result>) and nothing (v).
func (l *Lowerer) nativeDesc(t check.Type) string {
	if t == nil || t == check.Void {
		return "v"
	}
	switch x := t.(type) {
	case *check.Named:
		if x.Obj.Kind == check.InterfaceType && x.Obj.Native {
			return "o"
		}
	case *check.Optional:
		if n, ok := x.Elem.(*check.Named); ok && n.Obj.Native {
			return "?o"
		}
	case *check.FuncType:
		d := "F" + strconv.Itoa(len(x.Params))
		for _, p := range x.Params {
			d += l.nativeDesc(p)
		}
		return d + l.nativeDesc(x.Result)
	}
	return l.desc(t)
}

// nativeErrors is the global holding a package's declared errors, each
// enum declared : Error's cases in order (what internal/nativegen
// numbers them by), made in init.
func (l *Lowerer) nativeErrors(p *check.Package) int {
	key := "native errors|" + p.Path + "|" + p.Dir
	if g, ok := l.enumNames[key]; ok {
		return g
	}
	g := len(l.mod.Globals)
	l.mod.Globals = append(l.mod.Globals, bc.KRef)
	l.enumNames[key] = g
	l.pendingNative = append(l.pendingNative, pendingNative{g, p})
	return g
}

type pendingNative struct {
	global int
	pkg    *check.Package
}

func (l *Lowerer) finishNativeErrors() {
	b := l.stringsFn
	for _, p := range l.pendingNative {
		mark := b.mark()
		var vals []int
		for _, tn := range p.pkg.Types {
			if tn.Kind != check.EnumType || !tn.IsError {
				continue
			}
			for _, k := range tn.Cases {
				if len(k.Params) > 0 {
					// A case with values can't come from the platform's
					// side: its message stands for it.
					vals = append(vals, b.callB("error", bc.KRef, b.kstr(k.Name)))
					continue
				}
				vals = append(vals, b.caseValue(tn.Type, k, -1))
			}
		}
		ks := repeatKind(bc.KRef, len(vals))
		blk := b.regs(ks)
		for i, v := range vals {
			b.move(blk+i, v)
		}
		arr := b.temp(bc.KRef)
		b.emit(op("NEWA"), arr, int(bc.KRef), bc.None, blk, len(vals))
		b.releaseRegs(blk, ks)
		arr = b.freeze(arr, bc.KRef)
		b.emit(op("GSET"), p.global, arr, int(bc.KRef))
		b.release(mark)
	}
	l.pendingNative = nil
}

// nativeBody is a native fn's body: a call of the runtime's builtin
// <package>.<name> (a method's: <package>.<Type>.<name>, its receiver
// first), awaited when it's async.
func (b *fb) nativeBody(f *check.Function, params []int) {
	name := f.Pkg.Name + "." + f.Name()
	args := params
	if f.Recv != nil {
		name = f.Pkg.Name + "." + f.Recv.Name() + "." + f.Name()
		if s := b.ctx.Self; s != nil {
			args = append([]int{b.vars[s]}, params...)
		}
	}
	k := b.f.Ret
	if f.Sig.Async {
		fut := b.callB(name, bc.KRef, args...)
		if k == bc.KVoid {
			b.emit(op("AWAIT"), b.temp(bc.KRef), fut)
			b.emit(op("RET0"))
			return
		}
		r := b.temp(k)
		b.emit(op("AWAIT"), r, fut)
		b.ret(r, k)
		return
	}
	if k == bc.KVoid {
		b.callBTo(name, bc.None, args...)
		b.emit(op("RET0"))
		return
	}
	b.ret(b.callB(name, k, args...), k)
}

// nativeView builds a native package's view (LOWERING.md "Native
// packages"): CALLB ui.native("<class>#<name>"), then each prop by its
// name as the platforms' generated code knows it (upper camel case) and
// its descriptor, once (ui.native_set) or as a binding (ui.native_bind),
// a prop left out at its default; the style's keys as any element's;
// each handler by its event's name and its parameters' descriptors
// (ui.native_on).
func (b *fb) nativeView(ec *check.ElementCall) int {
	tn := ec.Comp
	comp := tn.Comp
	v := b.local(bc.KRef)
	b.callBTo("ui.native", v, b.kstr(tn.Pkg.NativeClass+"#"+tn.Name()))
	set := func(m *check.Member, x syntax.Expr) {
		mark := b.mark()
		defer b.release(mark)
		t := b.typ(m.Type)
		name, desc := b.kstr(upperFirst(m.Name())), b.kstr(b.l.nativeDesc(t))
		if b.l.prog.Reactive(x) {
			clos := b.thunk(b.l.info.Ctxs[x], x, b.l.thunkType(x, m.Type))
			b.callBTo("ui.native_bind", bc.None, v, name, desc, clos)
			return
		}
		b.callBTo("ui.native_set", bc.None, v, name, desc, b.value(x, -1))
	}
	given := map[*check.Member]bool{}
	for i, m := range comp.Params {
		if a := ec.Args[i]; a != nil {
			set(m, a)
		} else if m.Default != nil {
			set(m, m.Default)
		}
		given[m] = true
	}
	// A prop set twice: the last one wins.
	last := map[*check.Member]int{}
	for i, p := range ec.Props {
		last[p.Member] = i
	}
	for i, p := range ec.Props {
		if last[p.Member] != i {
			continue
		}
		if comp.Members[p.Member.Name()] != p.Member {
			b.setProp(v, "", p.Member, p.Value) // a style key
			continue
		}
		set(p.Member, p.Value)
		given[p.Member] = true
	}
	for _, m := range comp.Props {
		if !given[m] && m.Default != nil {
			set(m, m.Default)
		}
	}
	for _, h := range ec.Handlers {
		desc := ""
		for _, p := range h.Event.Params {
			desc += b.l.nativeDesc(b.typ(p.Type))
		}
		clos := b.handler(h.Handler, h.Event)
		b.callBTo("ui.native_on", bc.None, v, b.kstr(upperFirst(h.Event.Name())), b.kstr(desc), clos)
	}
	return v
}

// upperFirst is a prop's or an event's name as internal/nativegen
// names it on the platforms' side.
func upperFirst(s string) string {
	if s == "" {
		return s
	}
	return strings.ToUpper(s[:1]) + s[1:]
}
