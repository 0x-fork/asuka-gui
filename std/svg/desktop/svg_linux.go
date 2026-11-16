//go:build linux && !android

package svg

import (
	"sync"

	"github.com/ebitengine/purego"
	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: a widget of the package's (AsukaSvgView) whose snapshot fills
// and strokes the layers as GSK paths, fitted to it; currentColor is the
// view's color, a semantic one the widget's CSS color.

var (
	svgOnce  sync.Once
	svgType  GType
	svgViews = map[Ptr]*gtkIcon{}
)

type gtkIcon struct {
	widget Ptr
	d      drawing
	color  int
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
			if v := svgViews[widget]; v != nil {
				if orientation == OrientationHorizontal {
					s = int32(v.d.width + 0.5)
				} else {
					s = int32(v.d.height + 0.5)
				}
			}
			*At[int32](mn), *At[int32](nat) = 0, s
			if minB != 0 {
				*At[int32](minB) = -1
			}
			if natB != 0 {
				*At[int32](natB) = -1
			}
			return 0
		})
		svgType = Subclass(Call(Sym("gtk_widget_get_type")), "AsukaSvgView", func(class, parent Ptr) {
			SetVfunc(class, OffWidgetSnapshot, snapshot)
			SetVfunc(class, OffWidgetMeasure, measure)
		})
	})
}

func newView(events ViewEvents) ViewNative {
	svgClass()
	v := &gtkIcon{widget: NewObject(svgType)}
	svgViews[v.widget] = v
	return v
}

func (v *gtkIcon) Handle() uintptr { return v.widget }

func (v *gtkIcon) SetIcon(ic Icon) {
	v.d = decode(ic.Data)
	WidgetQueueResize(v.widget)
	Call(FnWidgetQueueDraw, v.widget)
}

func (v *gtkIcon) SetColor(c int) {
	v.color = c
	Call(FnWidgetQueueDraw, v.widget)
}

func (v *gtkIcon) Destroy() { delete(svgViews, v.widget) }

func (v *gtkIcon) snapshot(snap Ptr) {
	w, h := float32(Call(FnWidgetGetWidth, v.widget)), float32(Call(FnWidgetGetHeight, v.widget))
	scale, dx, dy := v.d.fit(w, h)
	if scale <= 0 {
		return
	}
	var fg RGBA
	WidgetGetColor(v.widget, &fg)
	cur := rgbaOf(v.color, [4]float32{fg.R, fg.G, fg.B, fg.A})
	color := func(p paint) (RGBA, bool) {
		switch p.kind {
		case 1:
			return RGBA{R: float32(p.r) / 255, G: float32(p.g) / 255, B: float32(p.b) / 255, A: float32(p.a) / 255}, p.a > 0
		case 2:
			return RGBA{R: cur[0], G: cur[1], B: cur[2], A: cur[3] * p.opacity}, cur[3]*p.opacity > 0
		}
		return RGBA{}, false
	}
	for _, l := range v.d.layers {
		b := PathBuilderNew()
		X := func(x float32) float32 { return x*scale + dx }
		Y := func(y float32) float32 { return y*scale + dy }
		for _, s := range l.path {
			a := s.args
			switch s.op {
			case 1:
				PathBuilderMoveTo(b, X(a[0]), Y(a[1]))
			case 2:
				PathBuilderLineTo(b, X(a[0]), Y(a[1]))
			case 3:
				PathBuilderQuadTo(b, X(a[0]), Y(a[1]), X(a[2]), Y(a[3]))
			case 4:
				PathBuilderCubicTo(b, X(a[0]), Y(a[1]), X(a[2]), Y(a[3]), X(a[4]), Y(a[5]))
			case 5:
				PathBuilderClose(b)
			case 6, 7:
				r := Rect{X: X(a[0]), Y: Y(a[1]), W: a[2] * scale, H: a[3] * scale}
				if s.op == 7 {
					rad := min(a[4]*scale, r.W/2, r.H/2)
					rr := RoundedRect{Bounds: r}
					for i := range rr.Corners {
						rr.Corners[i] = Size{W: rad, H: rad}
					}
					PathBuilderAddRoundedRect(b, &rr)
				} else {
					PathBuilderAddRect(b, &r)
				}
			case 8:
				// An ellipse: four cubic curves (a circle's kappa).
				const k = 0.5522848
				cx, cy, rx, ry := X(a[0])+a[2]*scale/2, Y(a[1])+a[3]*scale/2, a[2]*scale/2, a[3]*scale/2
				PathBuilderMoveTo(b, cx+rx, cy)
				PathBuilderCubicTo(b, cx+rx, cy+ry*k, cx+rx*k, cy+ry, cx, cy+ry)
				PathBuilderCubicTo(b, cx-rx*k, cy+ry, cx-rx, cy+ry*k, cx-rx, cy)
				PathBuilderCubicTo(b, cx-rx, cy-ry*k, cx-rx*k, cy-ry, cx, cy-ry)
				PathBuilderCubicTo(b, cx+rx*k, cy-ry, cx+rx, cy-ry*k, cx+rx, cy)
				PathBuilderClose(b)
			}
		}
		path := PathBuilderFreeToPath(b)
		if c, ok := color(l.fill); ok {
			Call(FnSnapshotAppendFill, snap, path, 0, Addr(&c)) // GSK_FILL_RULE_WINDING
		}
		if c, ok := color(l.stroke); ok && l.width > 0 {
			st := StrokeNew(l.width * scale)
			StrokeSetLineCap(st, int32(l.cap))
			StrokeSetLineJoin(st, int32(l.join))
			Call(FnSnapshotAppendStroke, snap, path, st, Addr(&c))
			StrokeFree(st)
		}
		PathUnref(path)
	}
}
