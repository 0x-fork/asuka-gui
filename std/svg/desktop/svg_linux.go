//go:build linux && !android

package svg

import (
	"sync"

	"github.com/ebitengine/purego"
	"github.com/egoist/asuka/internal/gtk"
)

// Linux: a widget of the package's (AsukaSvgView) whose snapshot fills
// and strokes the layers as GSK paths, fitted to it; currentColor is the
// view's color, a semantic one the widget's CSS color.

var (
	svgOnce  sync.Once
	svgType  gtk.GType
	svgViews = map[gtk.Ptr]*gtkIcon{}
)

type gtkIcon struct {
	widget  gtk.Ptr
	d       drawing
	color   int
	shape   []Layer // a Shape's: resolved in its size as it draws
	isShape bool
}

func svgClass() {
	svgOnce.Do(func() {
		snapshot := purego.NewCallback(func(widget, snap uintptr) uintptr {
			if v := svgViews[widget]; v != nil {
				v.snapshot(snap)
			}
			return 0
		})
		measure := purego.NewCallback(func(widget uintptr, orientation, forSize int32, mn, nat, minB, natB uintptr) uintptr {
			var s int32
			if v := svgViews[widget]; v != nil && !v.isShape {
				if orientation == gtk.OrientationHorizontal {
					s = int32(v.d.width + 0.5)
				} else {
					s = int32(v.d.height + 0.5)
				}
			}
			*gtk.At[int32](mn), *gtk.At[int32](nat) = 0, s
			if minB != 0 {
				*gtk.At[int32](minB) = -1
			}
			if natB != 0 {
				*gtk.At[int32](natB) = -1
			}
			return 0
		})
		svgType = gtk.Subclass(gtk.Call(gtk.Sym("gtk_widget_get_type")), "AsukaSvgView", func(class, parent gtk.Ptr) {
			gtk.SetVfunc(class, gtk.OffWidgetSnapshot, snapshot)
			gtk.SetVfunc(class, gtk.OffWidgetMeasure, measure)
		})
	})
}

func newView(events ViewEvents) ViewNative {
	svgClass()
	v := &gtkIcon{widget: gtk.NewObject(svgType)}
	svgViews[v.widget] = v
	return v
}

func newShape(events ShapeEvents) ShapeNative {
	svgClass()
	v := &gtkIcon{widget: gtk.NewObject(svgType), isShape: true}
	svgViews[v.widget] = v
	return v
}

func (v *gtkIcon) SetLayers(ls []Layer) {
	v.shape = ls
	gtk.Call(gtk.FnWidgetQueueDraw, v.widget)
}

func (v *gtkIcon) Handle() uintptr { return v.widget }

func (v *gtkIcon) SetIcon(ic Icon) {
	v.d = decode(ic.Data)
	gtk.WidgetQueueResize(v.widget)
	gtk.Call(gtk.FnWidgetQueueDraw, v.widget)
}

func (v *gtkIcon) SetColor(c int) {
	v.color = c
	gtk.Call(gtk.FnWidgetQueueDraw, v.widget)
}

func (v *gtkIcon) Destroy() { delete(svgViews, v.widget) }

func (v *gtkIcon) snapshot(snap gtk.Ptr) {
	w, h := float32(gtk.Call(gtk.FnWidgetGetWidth, v.widget)), float32(gtk.Call(gtk.FnWidgetGetHeight, v.widget))
	var fg gtk.RGBA
	gtk.WidgetGetColor(v.widget, &fg)
	if v.isShape {
		d := shape(v.shape, w, h, 1)
		paintDrawing(snap, &d, 1, 0, 0, [4]float32{fg.R, fg.G, fg.B, fg.A})
		return
	}
	scale, dx, dy := v.d.fit(w, h)
	if scale <= 0 {
		return
	}
	paintDrawing(snap, &v.d, scale, dx, dy, rgbaOf(v.color, [4]float32{fg.R, fg.G, fg.B, fg.A}))
}

// paintDrawing fills and strokes d's layers, scaled by scale and moved by
// dx, dy; currentColor is cur.
func paintDrawing(snap gtk.Ptr, d *drawing, scale, dx, dy float32, cur [4]float32) {
	color := func(p paint) (gtk.RGBA, bool) {
		switch p.kind {
		case 1:
			return gtk.RGBA{R: float32(p.r) / 255, G: float32(p.g) / 255, B: float32(p.b) / 255, A: float32(p.a) / 255}, p.a > 0
		case 2:
			return gtk.RGBA{R: cur[0], G: cur[1], B: cur[2], A: cur[3] * p.opacity}, cur[3]*p.opacity > 0
		}
		return gtk.RGBA{}, false
	}
	for _, l := range d.layers {
		b := gtk.PathBuilderNew()
		X := func(x float32) float32 { return x*scale + dx }
		Y := func(y float32) float32 { return y*scale + dy }
		for _, s := range l.path {
			a := s.args
			switch s.op {
			case 1:
				gtk.PathBuilderMoveTo(b, X(a[0]), Y(a[1]))
			case 2:
				gtk.PathBuilderLineTo(b, X(a[0]), Y(a[1]))
			case 3:
				gtk.PathBuilderQuadTo(b, X(a[0]), Y(a[1]), X(a[2]), Y(a[3]))
			case 4:
				gtk.PathBuilderCubicTo(b, X(a[0]), Y(a[1]), X(a[2]), Y(a[3]), X(a[4]), Y(a[5]))
			case 5:
				gtk.PathBuilderClose(b)
			case 6, 7:
				r := gtk.Rect{X: X(a[0]), Y: Y(a[1]), W: a[2] * scale, H: a[3] * scale}
				if s.op == 7 {
					rad := min(a[4]*scale, r.W/2, r.H/2)
					rr := gtk.RoundedRect{Bounds: r}
					for i := range rr.Corners {
						rr.Corners[i] = gtk.Size{W: rad, H: rad}
					}
					gtk.PathBuilderAddRoundedRect(b, &rr)
				} else {
					gtk.PathBuilderAddRect(b, &r)
				}
			case 8:
				// An ellipse: four cubic curves (a circle's kappa).
				const k = 0.5522848
				cx, cy, rx, ry := X(a[0])+a[2]*scale/2, Y(a[1])+a[3]*scale/2, a[2]*scale/2, a[3]*scale/2
				gtk.PathBuilderMoveTo(b, cx+rx, cy)
				gtk.PathBuilderCubicTo(b, cx+rx, cy+ry*k, cx+rx*k, cy+ry, cx, cy+ry)
				gtk.PathBuilderCubicTo(b, cx-rx*k, cy+ry, cx-rx, cy+ry*k, cx-rx, cy)
				gtk.PathBuilderCubicTo(b, cx-rx, cy-ry*k, cx-rx*k, cy-ry, cx, cy-ry)
				gtk.PathBuilderCubicTo(b, cx+rx*k, cy-ry, cx+rx, cy-ry*k, cx+rx, cy)
				gtk.PathBuilderClose(b)
			}
		}
		path := gtk.PathBuilderFreeToPath(b)
		if c, ok := color(l.fill); ok {
			if to, fades := color(l.fade); fades || l.fade.kind != 0 {
				// A fill that fades: the gradient, clipped to the path.
				bounds := gtk.Rect{X: dx, Y: dy, W: d.viewBox[2] * scale, H: d.viewBox[3] * scale}
				from, end := gtk.Point{X: 0, Y: dy}, gtk.Point{X: 0, Y: dy + l.fadeTo*scale}
				stops := [2]gtk.ColorStop{{Offset: 0, Color: c}, {Offset: 1, Color: to}}
				gtk.Call(gtk.FnSnapshotPushFill, snap, path, 0)
				gtk.Call(gtk.FnSnapshotAppendLinearGradient, snap, gtk.Addr(&bounds), gtk.Addr(&from), gtk.Addr(&end), gtk.Addr(&stops[0]), 2)
				gtk.Call(gtk.FnSnapshotPop, snap)
			} else {
				gtk.Call(gtk.FnSnapshotAppendFill, snap, path, 0, gtk.Addr(&c)) // GSK_FILL_RULE_WINDING
			}
		}
		if c, ok := color(l.stroke); ok && l.width > 0 {
			st := gtk.StrokeNew(l.width * scale)
			gtk.StrokeSetLineCap(st, int32(l.cap))
			gtk.StrokeSetLineJoin(st, int32(l.join))
			gtk.Call(gtk.FnSnapshotAppendStroke, snap, path, st, gtk.Addr(&c))
			gtk.StrokeFree(st)
		}
		gtk.PathUnref(path)
	}
}
