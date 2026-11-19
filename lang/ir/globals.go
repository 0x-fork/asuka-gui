package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
)

// updateGlobal runs fn on a register holding a file's var's value (lv,
// lvGlobal), which fn may change in place, and stores it back. A
// reference is taken out of the global (GTAKE, none left in it), so that
// it's unique and changed in place, not copied, as a boxed var's VTAKE;
// a handler puts it back if the change throws. A scalar, or a change
// whose code may read the var (reads: a closure a builtin calls), is
// read (GGET) and written back (GSET).
func (b *fb) updateGlobal(lv *lvalue, reads bool, fn func(cur int)) {
	k := b.kind(lv.g.Type)
	cur := b.temp(k)
	if reads || k != bc.KRef {
		b.emit(op("GGET"), cur, lv.cell, int(k))
		fn(cur)
		b.emit(op("GSET"), lv.cell, cur, int(k))
		return
	}
	b.emit(op("GTAKE"), cur, lv.cell)
	b.protect(func() { fn(cur) }, func(int) { b.emit(op("GSET"), lv.cell, cur, int(k)) })
	b.emit(op("GSET"), lv.cell, cur, int(k))
}
