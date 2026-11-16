//go:build windows

package svg

import (
	. "github.com/egoist/asuka/internal/windows"
)

// Windows: a child window of the package's whose WM_PAINT fills and
// strokes the layers as GDI+ paths, antialiased, fitted to it (its
// pixels: points times the window's DPI); currentColor is the view's
// color, a semantic one the theme's text color.

type gdiIcon struct {
	hwnd  uintptr
	d     drawing
	color int
}

func newView(events ViewEvents) ViewNative {
	v := &gdiIcon{}
	v.hwnd = NewProcWindow(0, "", WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, events.Context.Parent(), func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool) {
		switch m {
		case WM_PAINT:
			var ps PaintStruct
			dc := BeginPaint(hwnd, &ps)
			v.paint(dc)
			EndPaint(hwnd, &ps)
			return 0, true
		case WM_ERASEBKGND:
			return 1, true
		}
		return 0, false
	})
	return v
}

func (v *gdiIcon) Handle() uintptr { return v.hwnd }

// Measure is the icon's size, in points, when its style gives none.
func (v *gdiIcon) Measure(w, h float32) (float32, float32) { return v.d.width, v.d.height }

func (v *gdiIcon) SetIcon(ic Icon) {
	v.d = decode(ic.Data)
	Invalidate(v.hwnd)
}

func (v *gdiIcon) SetColor(c int) {
	v.color = c
	Invalidate(v.hwnd)
}

func (v *gdiIcon) paint(dc uintptr) {
	r := ClientRect(v.hwnd)
	DrawParentBackground(v.hwnd, dc, &r)
	scale, dx, dy := v.d.fit(float32(r.W()), float32(r.H()))
	if scale <= 0 {
		return
	}
	label := [4]float32{0, 0, 0, 0.9} // Fluent's text, light and dark
	if !AppsUseLightTheme() {
		label = [4]float32{1, 1, 1, 1}
	}
	cur := rgbaOf(v.color, label)
	argb := func(p paint) (ARGB, bool) {
		switch p.kind {
		case 1:
			return MakeARGB(p.a, p.r, p.g, p.b), p.a > 0
		case 2:
			a := cur[3] * p.opacity
			return MakeARGB(uint8(a*255), uint8(cur[0]*255), uint8(cur[1]*255), uint8(cur[2]*255)), a > 0
		}
		return 0, false
	}
	g := NewGraphics(dc)
	defer g.Close()
	X := func(x float32) float32 { return x*scale + dx }
	Y := func(y float32) float32 { return y*scale + dy }
	for _, l := range v.d.layers {
		p := NewPath()
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
			b := SolidBrush(c)
			g.FillPath(b, p)
			b.Close()
		}
		if c, ok := argb(l.stroke); ok && l.width > 0 {
			pen := NewPen(c, l.width*scale)
			pen.SetCaps(int(l.cap), int(l.join))
			g.DrawPath(pen, p)
			pen.Close()
		}
		p.Close()
	}
}
