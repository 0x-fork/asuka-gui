// Package svgparse turns an SVG icon's markup into what std/svg draws,
// at compile time: Parse makes it layers of paths (transforms applied,
// arcs made curves), and Encode writes them as std/svg's platforms read
// them (lang/compile's hook for std/svg.Icon). Nothing is rasterized.
//
// It reads what icon sets use (Lucide, Material, Heroicons, Feather):
// path, rect, circle, ellipse, line, polyline, polygon, g and use; fill,
// stroke and their widths, caps, joins and opacities, as attributes or
// in a style attribute; and transforms. It skips the rest: text,
// gradients (painted with the icon's color), clipping, masks, filters,
// and fill-rule evenodd (the platforms fill by the nonzero rule).
package svgparse

import "github.com/egoist/asuka/ops"

// Icon is parsed SVG: its viewBox and what it draws.
type Icon struct {
	// ViewBox is the drawing's area: min x, min y, width, height (the
	// svg's viewBox; without one, its width and height).
	ViewBox [4]float32
	// Width and Height are its size in points when its Style gives none:
	// the svg's width and height, else the viewBox's.
	Width, Height float32
	// Layers are drawn in order, the first at the back.
	Layers []Layer
}

// Layer is an outline and how it's painted: filled, stroked or both.
type Layer struct {
	Path         Path
	Fill, Stroke Paint
	// LineWidth is the stroke's, in viewBox units.
	LineWidth float32
	LineCap   ops.LineCap
	LineJoin  ops.LineJoin
}

// Paint is how a fill or stroke is painted: not at all (the zero Paint),
// with a color, or with the icon's color (currentColor, and a fill the
// SVG doesn't give).
type Paint struct {
	// Color is the color, opacity folded into its alpha.
	Color ops.Color
	// Current paints with the icon's color, at Opacity (0 to 1).
	Current bool
	Opacity float32
}

// None reports whether p paints nothing.
func (p Paint) None() bool { return !p.Current && p.Color == 0 }

// opaque reports whether p paints without seeing through.
func (p Paint) opaque() bool {
	if p.Current {
		return p.Opacity >= 1
	}
	_, _, _, a, ok := p.Color.RGBA()
	return !ok || a == 255
}

// Fit is where the viewBox goes in a box of width w and height h: scaled
// by scale (the same both ways) and moved by dx, dy, centered (SVG's
// preserveAspectRatio xMidYMid meet). A point x, y of the viewBox is at
// x*scale+dx, y*scale+dy.
func (ic *Icon) Fit(w, h float32) (scale, dx, dy float32) {
	vx, vy, vw, vh := ic.ViewBox[0], ic.ViewBox[1], ic.ViewBox[2], ic.ViewBox[3]
	if vw <= 0 || vh <= 0 {
		return 1, 0, 0
	}
	scale = min(w/vw, h/vh)
	return scale, (w-vw*scale)/2 - vx*scale, (h-vh*scale)/2 - vy*scale
}
