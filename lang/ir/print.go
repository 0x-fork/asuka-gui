package ir

import (
	"slices"

	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/check"
)

// printCall lowers print and eprint (check.BPrint, BEprint): the values'
// texts, as interpolation shows them, joined by the separator (" "), and
// the terminator ("\n") after. print of one value and no labels is what
// it always was: CALLB print of its text, which writes it as a line (the
// log's, a test's capture). Anything else is one string written at once:
// CALLB print of the line for print with its default terminator, and
// CALLB print.text(fd, text) for eprint and a terminator of print's own.
func (b *fb) printCall(call *check.Call, stderr bool) {
	if len(call.Args) == 1 && call.Separator == nil && call.Terminator == nil && !stderr {
		v := b.value(call.Args[0], -1)
		b.callBTo("print", bc.None, b.describe(v, b.typeOf(call.Args[0])))
		return
	}
	var vals []int
	var ks []bc.Kind
	var ds [][]byte
	for _, a := range call.Args {
		v, k, d := b.stringPart(a, "")
		vals = append(vals, v)
		ks = append(ks, k)
		ds = append(ds, d)
	}
	sep := -1
	if call.Separator != nil {
		sep = b.value(call.Separator, -1)
	}
	term := -1
	if call.Terminator != nil {
		term = b.value(call.Terminator, -1)
	}
	str := []byte{byte(bc.KRef)}
	var pv []int
	var pk []bc.Kind
	var pd [][]byte
	for i, v := range vals {
		if i > 0 {
			if sep < 0 {
				sep = b.kstr(" ")
			}
			pv, pk, pd = append(pv, sep), append(pk, bc.KRef), append(pd, str)
		}
		pv, pk, pd = append(pv, v), append(pk, ks[i]), append(pd, ds[i])
	}
	if !stderr && term < 0 {
		b.callBTo("print", bc.None, b.joinParts(pv, pk, pd))
		return
	}
	if term < 0 {
		term = b.kstr("\n")
	}
	pv, pk, pd = append(pv, term), append(pk, bc.KRef), append(pd, str)
	fd := int64(1)
	if stderr {
		fd = 2
	}
	b.callBTo("print.text", bc.None, b.konst(fd), b.joinParts(pv, pk, pd))
}

// joinParts is the string of parts (registers of their kinds: strings,
// and the scalars INTERP prints), INTERP over literalChunk of them at a
// time.
func (b *fb) joinParts(vals []int, ks []bc.Kind, ds [][]byte) int {
	switch {
	case len(vals) == 0:
		return b.kstr("")
	case len(vals) == 1 && ks[0] == bc.KRef:
		return vals[0]
	}
	acc := -1
	for len(vals) > 0 {
		n := min(len(vals), literalChunk)
		blk := b.regs(ks[:n])
		for i, v := range vals[:n] {
			b.move(blk+i, v)
		}
		// INTERP's kinds: each part's descriptor (stringPart's), none when
		// every part is a string (joined at their size).
		var kinds []byte
		for _, d := range ds[:n] {
			kinds = append(kinds, d...)
		}
		if !slices.ContainsFunc(kinds, func(k byte) bool { return k != byte(bc.KRef) }) {
			kinds = nil
		}
		r := b.temp(bc.KRef)
		b.emit(op("INTERP"), r, blk, n, b.l.str(string(kinds)))
		b.releaseRegs(blk, ks[:n])
		vals, ks, ds = vals[n:], ks[n:], ds[n:]
		if acc >= 0 {
			t := b.temp(bc.KRef)
			b.emit(op("CONCAT"), t, acc, r)
			r = t
		}
		acc = r
	}
	return acc
}
