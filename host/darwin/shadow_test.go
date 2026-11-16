//go:build darwin && !ios

package darwin

import (
	"testing"

	"github.com/egoist/asuka/colors"
	. "github.com/egoist/asuka/internal/darwin"
	"github.com/egoist/asuka/ops"
	"github.com/egoist/asuka/signal"
	"github.com/egoist/asuka/ui"
)

// A view that clips and casts a shadow clips its children in a view of
// their own, so its layer keeps its shadow; without the shadow they go
// back, and the view clips itself again.
func TestClipShadow(t *testing.T) {
	h := mountApp(t, `
		component Card {
		  state shadow = true
		  state more = false
		  Column {
		    View {
		      width: 60; height: 60; clip: true; radius: 12
		      shadow: if shadow { Shadow(color: #000000, radius: 8) } else { none }
		      View { width: 100; height: 100; background: #ff0000 }
		      if more { View { width: 10; height: 10; background: #0000ff } }
		    }
		    Button("More") { on press { more = true } }
		    Button("Flat") { on press { shadow = false } }
		  }
		}
		app { Card() }
	`)
	onMain(func() {
		var p *node
		for _, n := range find(h, ops.KindView) {
			if n.clip {
				p = n
			}
		}
		if p == nil {
			fatal(t, "no clipping view")
		}
		inClip := func() bool {
			for _, c := range p.children {
				if Send(c.view, "superview") != p.clipView {
					return false
				}
			}
			return true
		}
		l := Send(p.view, "layer")
		if p.clipView == 0 || !inClip() {
			fatal(t, "the children aren't in a clip view")
		}
		cl := Send(p.clipView, "layer")
		if SendBool(l, "masksToBounds") || !SendBool(cl, "masksToBounds") || MsgFloat(cl, Sel("cornerRadius")) != 12 {
			fatalf(t, "masks: the view's %v, the clip view's %v (radius %v)", SendBool(l, "masksToBounds"), SendBool(cl, "masksToBounds"), MsgFloat(cl, Sel("cornerRadius")))
		}
		if f := MsgRect(p.clipView, Sel("frame")); f.Size.Width != 60 || f.Size.Height != 60 {
			fatalf(t, "the clip view is %+v", f)
		}
		if Send(l, "shadowPath") == 0 {
			fatal(t, "no shadow path")
		}
		// A child added later goes in the clip view too.
		h.enter(func() { Send(button(h, "More").view, "performClick:", 0) })
		if len(p.children) != 2 || !inClip() {
			fatalf(t, "%d children, in the clip view: %v", len(p.children), inClip())
		}
		h.enter(func() { Send(button(h, "Flat").view, "performClick:", 0) })
		if p.clipView != 0 {
			fatal(t, "the clip view stayed without a shadow")
		}
		for _, c := range p.children {
			if Send(c.view, "superview") != p.view {
				fatal(t, "a child didn't come back")
			}
		}
		if !SendBool(l, "masksToBounds") {
			fatal(t, "the view doesn't clip itself again")
		}
	})
}

// AppKit sets a layer-backed view's layer's shadow from the view's own
// (none) as it moves into a window: the style's shadow holds.
func TestShadowInWindow(t *testing.T) {
	h := mountApp(t, `app { View { width: 60; height: 30; background: #ff0000; shadow: Shadow(color: #000000, radius: 8, y: 4) } }`)
	onMain(func() {
		var n *node
		for _, x := range find(h, ops.KindView) {
			if x.hasShadow() {
				n = x
			}
		}
		l := Send(n.view, "layer")
		opacity := func() float64 {
			return MsgFloat(Send(l, "valueForKey:", uintptr(String("shadowOpacity"))), Sel("doubleValue"))
		}
		if opacity() != 1 {
			fatalf(t, "shadow opacity %v once in the window", opacity())
		}
		sup := Send(n.view, "superview")
		Send(n.view, "removeFromSuperview")
		Send(sup, "addSubview:", uintptr(n.view))
		if o := opacity(); o != 1 || MsgSize(l, Sel("shadowOffset")).Height != 4 {
			fatalf(t, "shadow opacity %v, offset %+v after moving in again", o, MsgSize(l, Sel("shadowOffset")))
		}
	})
}

// Style.Transform holds on the layer (AppKit sets the layer's own
// transform), and ui.Animate animates it.
func TestTransformHolds(t *testing.T) {
	scale := signal.New(float32(0.5))
	h, _ := mount(t, func() ui.Node {
		return ui.View(ui.ViewProps{Style: ui.StyleFunc(func() ui.Style {
			return ui.Style{Width: ui.Pt(60), Height: ui.Pt(60), Background: colors.RGB(255, 0, 0), Transform: ui.Transform{Scale: scale.Get()}}
		})})
	})
	onMain(func() {
		var n *node
		for _, x := range find(h, ops.KindView) {
			if x.transform != nil {
				n = x
			}
		}
		l := Send(n.view, "layer")
		Send(h.window, "display") // AppKit sets the layer's transform here
		a := Send(l, "animationForKey:", uintptr(transformKey()))
		if a == 0 || n.held.M11 != 0.5 || n.held.M41 != 15 {
			fatalf(t, "held %+v by %v", n.held, a)
		}
		h.enter(func() { ui.Post(func() { ui.Animate(func() { scale.Set(1) }) }) })
		h.runPosted()
		a = Send(l, "animationForKey:", uintptr(transformKey()))
		if d := MsgFloat(a, Sel("duration")); n.held.M11 != 1 || d < 0.2 {
			fatalf(t, "animated to %+v in %vs", n.held, d)
		}
	})
}
