//go:build windows

package svg

import (
	win "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: a child window of the package's whose WM_PAINT fills and
// strokes the layers as GDI+ paths, antialiased, fitted to it (its
// pixels: points times the window's DPI); currentColor is the view's
// color, a semantic one the theme's text color. A Shape's layers are
// resolved in the window's size each time it paints.

type gdiIcon struct {
	hwnd    uintptr
	d       drawing
	color   int
	shape   []Layer
	isShape bool
}

func newGDIView(ctx native.Context) *gdiIcon {
	v := &gdiIcon{}
	v.hwnd = win.NewProcWindow(0, "", win.WS_CHILD|win.WS_VISIBLE, 0, 0, 0, 0, ctx.Parent(), func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool) {
		switch m {
		case win.WM_PAINT:
			var ps win.PaintStruct
			dc := win.BeginPaint(hwnd, &ps)
			v.paint(dc)
			win.EndPaint(hwnd, &ps)
			return 0, true
		case win.WM_ERASEBKGND:
			return 1, true
		case win.WM_SIZE:
			if v.isShape {
				win.Invalidate(hwnd)
			}
		}
		return 0, false
	})
	return v
}

func newView(events ViewEvents) ViewNative { return newGDIView(events.Context) }

func newShape(events ShapeEvents) ShapeNative {
	v := newGDIView(events.Context)
	v.isShape = true
	return v
}

func (v *gdiIcon) Handle() uintptr { return v.hwnd }

// Measure is the icon's size, in points, when its style gives none; a
// Shape has none.
func (v *gdiIcon) Measure(w, h float32) (float32, float32) {
	if v.isShape {
		return 0, 0
	}
	return v.d.width, v.d.height
}

func (v *gdiIcon) SetIcon(ic Icon) {
	v.d = decode(ic.Data)
	win.Invalidate(v.hwnd)
}

func (v *gdiIcon) SetColor(c int) {
	v.color = c
	win.Invalidate(v.hwnd)
}

func (v *gdiIcon) SetLayers(ls []Layer) {
	v.shape = ls
	win.Invalidate(v.hwnd)
}

func (v *gdiIcon) paint(dc uintptr) {
	r := win.ClientRect(v.hwnd)
	win.DrawParentBackground(v.hwnd, dc, &r)
	w, h := float32(r.W()), float32(r.H())
	if v.isShape {
		d := shape(v.shape, w, h, float32(win.DPI(v.hwnd))/96)
		paintDrawing(dc, &d, 1, 0, 0, [4]float32{})
		return
	}
	scale, dx, dy := v.d.fit(w, h)
	if scale <= 0 {
		return
	}
	label := [4]float32{0, 0, 0, 0.9} // Fluent's text, light and dark
	if !win.AppsUseLightTheme() {
		label = [4]float32{1, 1, 1, 1}
	}
	if c, ok := native.ResolveColor(v.color); ok {
		label = c
	}
	paintDrawing(dc, &v.d, scale, dx, dy, label)
}

// paintDrawing fills and strokes d's layers into dc, scaled by scale and
// moved by dx, dy; currentColor is cur.
func paintDrawing(dc uintptr, d *drawing, scale, dx, dy float32, cur [4]float32) {
	argb := func(p paint) (win.ARGB, bool) {
		switch p.kind {
		case 1:
			return win.MakeARGB(p.a, p.r, p.g, p.b), p.a > 0
		case 2:
			a := cur[3] * p.opacity
			return win.MakeARGB(uint8(a*255), uint8(cur[0]*255), uint8(cur[1]*255), uint8(cur[2]*255)), a > 0
		}
		return 0, false
	}
	g := win.NewGraphics(dc)
	defer g.Close()
	X := func(x float32) float32 { return x*scale + dx }
	Y := func(y float32) float32 { return y*scale + dy }
	for _, l := range d.layers {
		p := win.NewPath()
		var px, py, sx, sy float32 // the pen, and its figure's start
		for _, s := range l.path {
			a := s.args
			switch s.op {
			case 1:
				p.StartFigure()
				px, py, sx, sy = X(a[0]), Y(a[1]), X(a[0]), Y(a[1])
			case 2:
				p.Line(px, py, X(a[0]), Y(a[1]))
				px, py = X(a[0]), Y(a[1])
			case 3: // a quadratic curve as a cubic one
				cx, cy, x, y := X(a[0]), Y(a[1]), X(a[2]), Y(a[3])
				p.Bezier(px, py, px+(cx-px)*2/3, py+(cy-py)*2/3, x+(cx-x)*2/3, y+(cy-y)*2/3, x, y)
				px, py = x, y
			case 4:
				p.Bezier(px, py, X(a[0]), Y(a[1]), X(a[2]), Y(a[3]), X(a[4]), Y(a[5]))
				px, py = X(a[4]), Y(a[5])
			case 5:
				p.CloseFigure()
				px, py = sx, sy
			case 6:
				p.Rect(X(a[0]), Y(a[1]), a[2]*scale, a[3]*scale)
			case 7:
				p.RoundRect(X(a[0]), Y(a[1]), a[2]*scale, a[3]*scale, a[4]*scale)
			case 8:
				p.Ellipse(X(a[0]), Y(a[1]), a[2]*scale, a[3]*scale)
			}
		}
		if c, ok := argb(l.fill); ok {
			var b win.Brush
			if l.fade.kind != 0 {
				to, _ := argb(l.fade)
				b = win.LinearBrush(0, dy, 0, dy+l.fadeTo*scale, []win.ARGB{c, to}, []float32{0, 1})
			} else {
				b = win.SolidBrush(c)
			}
			g.FillPath(b, p)
			b.Close()
		}
		if c, ok := argb(l.stroke); ok && l.width > 0 {
			pen := win.NewPen(c, l.width*scale)
			pen.SetCaps(gdiCap[min(l.cap, 2)], gdiJoin[min(l.join, 2)])
			g.DrawPath(pen, p)
			pen.Close()
		}
		p.Close()
	}
}

// GDI+'s caps and joins, by ours (butt, round, square; miter, round,
// bevel).
var (
	gdiCap  = [3]int{0, 2, 1}
	gdiJoin = [3]int{0, 2, 1}
)
