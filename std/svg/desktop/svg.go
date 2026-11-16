// asuka/svg on Windows (GDI+, svg_windows.go) and Linux (GSK's paths,
// svg_linux.go); macOS runs the package's Swift. Both read the icon's
// layers (the compiler's encoding: sdk/svg's Encode) here.
package svg

import (
	"encoding/binary"
	"math"

	"github.com/egoist/asuka/native"
)

// paint is a layer's fill or stroke: kind 0 none, 1 rgba, 2 the icon's
// color at opacity.
type paint struct {
	kind       byte
	r, g, b, a uint8
	opacity    float32
}

// seg is a path's segment: its op (sdk/svg's Encode's, ops.PathOp's) and
// arguments.
type seg struct {
	op   byte
	args []float32
}

type layer struct {
	fill, stroke paint
	width        float32
	cap, join    byte
	path         []seg
	// A Shape's fill that fades: from fill at y 0 to fade at fadeTo.
	fade   paint
	fadeTo float32
}

type drawing struct {
	viewBox       [4]float32
	width, height float32
	layers        []layer
}

func decode(d []byte) drawing {
	var out drawing
	at := 0
	u8 := func() byte {
		if at >= len(d) {
			return 0
		}
		at++
		return d[at-1]
	}
	u32 := func() uint32 {
		if at+4 > len(d) {
			at = len(d)
			return 0
		}
		at += 4
		return binary.LittleEndian.Uint32(d[at-4:])
	}
	f32 := func() float32 { return math.Float32frombits(u32()) }
	if len(d) < 24 {
		return out
	}
	for i := range out.viewBox {
		out.viewBox[i] = f32()
	}
	out.width, out.height = f32(), f32()
	readPaint := func() paint {
		switch k := u8(); k {
		case 1:
			return paint{kind: 1, r: u8(), g: u8(), b: u8(), a: u8()}
		case 2:
			return paint{kind: 2, opacity: f32()}
		}
		return paint{}
	}
	nargs := map[byte]int{1: 2, 2: 2, 3: 4, 4: 6, 5: 0, 6: 4, 7: 5, 8: 4}
	n := int(u32())
	for i := 0; i < n && at < len(d); i++ {
		var l layer
		l.fill, l.stroke = readPaint(), readPaint()
		l.width = f32()
		l.cap, l.join = u8(), u8()
		end := at + int(u32())
		for at < min(end, len(d)) {
			op := u8()
			k, ok := nargs[op]
			if !ok {
				break
			}
			s := seg{op: op, args: make([]float32, k)}
			for j := range s.args {
				s.args[j] = f32()
			}
			l.path = append(l.path, s)
		}
		at = end
		out.layers = append(out.layers, l)
	}
	return out
}

// fit is where the viewBox goes in a box of w×h: scaled by scale and
// moved by dx, dy (xMidYMid meet).
func (d *drawing) fit(w, h float32) (scale, dx, dy float32) {
	vx, vy, vw, vh := d.viewBox[0], d.viewBox[1], d.viewBox[2], d.viewBox[3]
	if vw <= 0 || vh <= 0 {
		return 0, 0, 0
	}
	scale = min(w/vw, h/vh)
	return scale, (w-vw*scale)/2 - vx*scale, (h-vh*scale)/2 - vy*scale
}

// rgbaOf is a Color (the core's encoding) as RGBA, 0 to 1; a semantic one
// is def.
func rgbaOf(c int, def [4]float32) [4]float32 {
	u := uint64(c)
	if u>>32 != 2 {
		return def
	}
	v := uint32(u)
	return [4]float32{float32(v>>24) / 255, float32(v>>16&255) / 255, float32(v>>8&255) / 255, float32(v&255) / 255}
}

// paintOf is a Shape's color as a paint: none for none, or one the host
// can't resolve.
func paintOf(c *int) paint {
	if c == nil {
		return paint{}
	}
	v, ok := native.ResolveColor(*c)
	if !ok {
		return paint{}
	}
	b := func(f float32) uint8 { return uint8(min(max(f, 0), 1)*255 + 0.5) }
	return paint{kind: 1, r: b(v[0]), g: b(v[1]), b: b(v[2]), a: b(v[3])}
}

// shape is a Shape's layers as a drawing in a box of w×h pixels, s
// pixels a point (a point is a fraction of the box plus points), drawn
// at scale 1: its viewBox is the box.
func shape(ls []Layer, w, h, s float32) drawing {
	d := drawing{viewBox: [4]float32{0, 0, w, h}, width: w, height: h}
	for _, l := range ls {
		out := layer{fill: paintOf(l.Fill), stroke: paintOf(l.Stroke), width: float32(l.Width) * s,
			cap: byte(min(l.Cap, 2)), join: byte(min(l.Join, 2))}
		if out.fill.kind != 0 {
			if f := paintOf(l.Fade); f.kind != 0 {
				out.fade, out.fadeTo = f, h
			}
		}
		data := l.Path.Data
		i := 0
		pt := func() (float32, float32) {
			if i+4 > len(data) {
				i = len(data)
				return 0, 0
			}
			x, y := float32(data[i])*w+float32(data[i+1])*s, float32(data[i+2])*h+float32(data[i+3])*s
			i += 4
			return x, y
		}
	path:
		for i < len(data) {
			op := int(data[i])
			i++
			switch op {
			case 1, 2:
				x, y := pt()
				out.path = append(out.path, seg{op: byte(op), args: []float32{x, y}})
			case 3:
				cx, cy := pt()
				x, y := pt()
				out.path = append(out.path, seg{op: 3, args: []float32{cx, cy, x, y}})
			case 4:
				ax, ay := pt()
				bx, by := pt()
				x, y := pt()
				out.path = append(out.path, seg{op: 4, args: []float32{ax, ay, bx, by, x, y}})
			case 5:
				out.path = append(out.path, seg{op: 5})
			case 6, 7:
				ax, ay := pt()
				bx, by := pt()
				x, y, rw, rh := min(ax, bx), min(ay, by), abs(bx-ax), abs(by-ay)
				if op == 7 {
					out.path = append(out.path, seg{op: 8, args: []float32{x, y, rw, rh}})
					break
				}
				var r float32
				if i < len(data) {
					r = float32(data[i]) * s
				}
				i++
				r = max(0, min(r, rw/2, rh/2))
				out.path = append(out.path, seg{op: 7, args: []float32{x, y, rw, rh, r}})
			default:
				break path
			}
		}
		d.layers = append(d.layers, out)
	}
	return d
}

func abs(v float32) float32 {
	if v < 0 {
		return -v
	}
	return v
}
