package svgparse

import (
	"encoding/binary"
	"math"
)

// Encode is the icon as std/svg's View draws it (the Asuka compiler
// makes it when an app compiles; std/svg's platforms read it), little
// endian:
//
//	f32 viewBox x, y, width, height; f32 width, height
//	u32 layer count; per layer: its fill and its stroke (a paint: u8
//	kind, 0 none, 1 a color (u8 r, g, b, a), 2 the icon's color (f32
//	opacity)), f32 line width, u8 cap (0 butt, 1 round, 2 square), u8 join
//	(0 miter, 1 round, 2 bevel), u32 path length, its path: per segment a
//	u8 op (1 move, 2 line, 3 quad, 4 cubic, 5 close, 6 rect, 7 rounded
//	rect, 8 ellipse: ops.PathOp's) and its f32 arguments, in viewBox
//	units.
func (ic *Icon) Encode() []byte {
	le := binary.LittleEndian
	var b []byte
	f32 := func(v float32) { b = le.AppendUint32(b, math.Float32bits(v)) }
	for _, v := range ic.ViewBox {
		f32(v)
	}
	f32(ic.Width)
	f32(ic.Height)
	b = le.AppendUint32(b, uint32(len(ic.Layers)))
	paint := func(p Paint) {
		switch {
		case p.Current:
			b = append(b, 2)
			f32(p.Opacity)
		case p.Color != 0:
			r, g, bl, a, _ := p.Color.RGBA()
			b = append(b, 1, r, g, bl, a)
		default:
			b = append(b, 0)
		}
	}
	for _, l := range ic.Layers {
		paint(l.Fill)
		paint(l.Stroke)
		f32(l.LineWidth)
		b = append(b, byte(l.LineCap), byte(l.LineJoin))
		var path []byte
		a := l.Path.args
		for _, op := range l.Path.cmds {
			path = append(path, byte(op))
			for _, v := range a[:nargs(op)] {
				path = le.AppendUint32(path, math.Float32bits(v))
			}
			a = a[nargs(op):]
		}
		b = le.AppendUint32(b, uint32(len(path)))
		b = append(b, path...)
	}
	return b
}
