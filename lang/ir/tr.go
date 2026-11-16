package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// The app's catalogs (LOWERING.md's "Languages"): the compiled catalog is
// a module global the init makes first, i18n.catalog of the blob (a
// string constant, as bytes), which registers it; tr.key(args) is
// i18n.say(catalog, key, args, none), args a tuple of the arguments in
// the key's order (none without any).

// initCatalog makes the catalog's global, when the app has catalogs.
func (l *Lowerer) initCatalog(b *fb) {
	cat := l.prog.Catalog
	if cat == nil {
		return
	}
	l.catalog = len(l.mod.Globals)
	l.mod.Globals = append(l.mod.Globals, bc.KRef)
	mark := b.mark()
	blob := b.kstr(string(cat.Blob))
	bytes := b.callB("str.to_bytes", bc.KRef, blob)
	c := b.callB("i18n.catalog", bc.KRef, bytes)
	b.emit(op("GSET"), l.catalog, c, int(bc.KRef))
	b.release(mark)
}

// trCall says a message: a string in the language shown, tracked.
func (b *fb) trCall(e *syntax.CallExpr, call *check.Call, dst int) int {
	if b.l.catalog < 0 {
		b.l.errorf(e, "internal: tr without a catalog")
		return -1
	}
	cat := b.temp(bc.KRef)
	b.emit(op("GGET"), cat, b.l.catalog, int(bc.KRef))
	key := b.konst(int64(call.TrKey.Index))
	var args int
	if len(call.Args) == 0 {
		args = b.valueReg(-1, bc.KRef)
	} else {
		types := make([]check.Type, len(call.Args))
		vals := make([]int, len(call.Args))
		for i, x := range call.Args {
			t := b.rawType(x)
			v := b.value(x, -1)
			if fi := check.TimeInstant(t); fi >= 0 && t != check.Int {
				// A Time: its instant, in nanoseconds.
				r := b.temp(bc.KInt)
				b.emit(op("GETF"), r, v, fi)
				v, t = r, check.Int
			}
			types[i], vals[i] = t, v
		}
		args = b.newStruct(b.l.shape(&check.Tuple{Elems: types}), types, vals, -1)
	}
	lang := b.valueReg(-1, bc.KRef)
	return b.copyTo(b.callB("i18n.say", bc.KRef, cat, key, args, lang), dst)
}
