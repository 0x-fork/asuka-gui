// Package conformance checks that every host draws a Style the same
// way: a background's edges, rounded corners, borders, opacity, clips,
// shadows, gradients and transforms. Tests elsewhere check the ops the
// core sends and the views a host makes; these check the pixels.
//
// Sheet lays the cases out between two registration markers; a host's
// runner shows it, captures the screen as an image, and Check finds the
// sheet in the capture, measures each case and reports what's off. The
// cases assert measured quantities with tolerances (where an edge is to
// the device pixel, a shadow's Gaussian σ, a color's mix), not pixels
// against a reference, so one set of cases holds for every host at any
// scale; the platforms draw differently, and that's allowed.
//
// The runners:
//
//	go test ./platform/macos/mactest -run Conformance   # macOS, on the Mac player
//	go test -tags e2e ./e2e -run Conformance            # iOS, Android and the web (headless Chrome, at 1×, 1.5× and 2×)
//	ASUKA_CONFORMANCE_OUT=/tmp/c go test ...             # also write each capture, as <host>.png
//
// Its app (conformance/app, App's source, which go generate writes)
// shows it on any platform:
//
//	go run ./cmd/asuka dev --platform ios conformance/app
package conformance

import (
	"fmt"
	"image"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

//go:generate go run ./gen

// Feature is something a case relies on that a host may document it
// lacks; its cases are skipped there, with the reason, rather than fail.
type Feature string

const (
	// ShadowBlur: a shadow's blur is Style.Shadow's Radius (2σ, as CSS
	// and SwiftUI mean it) and its offset is Style.Shadow's X and Y.
	ShadowBlur Feature = "shadow blur and offset"
	// Transform: Style.Transform.
	Transform Feature = "transforms"
	// GroupOpacity: Style.Opacity fades a view and its children as one
	// picture, so overlapping children don't show through each other.
	GroupOpacity Feature = "group opacity"
)

// unsupported is what each host documents it lacks (DESIGN.md, Style's
// comments), and why.
var unsupported = map[string]map[Feature]string{
	"android": {
		ShadowBlur: "Android draws its elevation shadow: the radius is the elevation, and the light sets the offset",
	},
}

// skipped is why host skips c: a feature it lacks. Hosts are "ios",
// "android", "macos", "windows" and "web".
func skipped(host string, c Case) string {
	for _, f := range c.Needs {
		if why, ok := unsupported[host][f]; ok {
			return why
		}
	}
	return ""
}

// The sheet's geometry, in points.
const (
	Columns = 3
	Cell    = 100 // a case's square
	marker  = 8   // the registration squares' side
)

// Magenta is the registration markers' color, which no case uses.
const Magenta = "#ff00ff"

// swatches are the cases' colors, each a marker-sized square in the
// sheet's top margin, right of the top left marker, a marker apart: what
// a host's capture shows for each, solid. A host blends in its own color
// space (the Mac's window server in the display's, Display P3 on its own
// screen), and a mix is measured between the colors as captured, in that
// space: half-transparent red is half way from the white to the red the
// capture has, which in sRGB it needn't be.
var swatches = []struct {
	hex string
	rgb RGB
}{{cRed, red}, {cBlue, blue}, {cYellow, yellow}}

// swatchX is swatch i's left, in the sheet's points.
func swatchX(i int) float32 { return 2*marker + float32(i)*2*marker }

// SheetSize is the sheet's size in points.
func SheetSize() (w, h float32) {
	rows := (len(Cases) + Columns - 1) / Columns
	return 2*marker + Columns*Cell, 2*marker + float32(rows)*Cell
}

// cellOrigin is case i's top left in the sheet, in points.
func cellOrigin(i int) (x, y float32) {
	return marker + float32(i%Columns)*Cell, marker + float32(i/Columns)*Cell
}

// Sheet is the sheet as Asuka's ConformanceSheet component: every case
// in its cell, on white, between a magenta square at the top left and
// one at the bottom right. Show it anywhere on screen, at its own size:
// Check finds it in the capture.
func Sheet() string {
	w, h := SheetSize()
	var b strings.Builder
	b.WriteString("// The conformance sheet (package conformance's cases).\n")
	b.WriteString("component ConformanceSheet {\n")
	fmt.Fprintf(&b, "  View {\n    width: %g; height: %g; background: #ffffff; alignSelf: .center\n", w, h)
	fmt.Fprintf(&b, "    %s\n", box(0, 0, marker, marker, "background: "+Magenta))
	fmt.Fprintf(&b, "    %s\n", box(w-marker, h-marker, marker, marker, "background: "+Magenta))
	for i, s := range swatches {
		fmt.Fprintf(&b, "    %s\n", box(swatchX(i), 0, marker, marker, "background: "+s.hex))
	}
	for i, c := range Cases {
		x, y := cellOrigin(i)
		// A View with only layout props makes no native view: the case's
		// views go straight into the sheet, which its white shows under.
		fmt.Fprintf(&b, "    // %s\n    %s\n", c.Name, box(x, y, Cell, Cell, "", c.Scene))
	}
	b.WriteString("  }\n}\n")
	return b.String()
}

// App is an Asuka app that shows the sheet: conformance/app's app.asuka.
func App() string {
	return Sheet() + `
app {
  SafeArea {
    justify: .center; align: .center
    Text("Conformance")
    ConformanceSheet()
  }
}
`
}

// Result is one case's outcome on a capture.
type Result struct {
	Case     string
	Skipped  string   // why, when the host lacks a feature it needs
	Errors   []string // what's off; none: it passed
	Measured []string // what it measured, for the log
}

// Measure finds the sheet in img, a capture of a screen that shows it,
// and runs every case on it.
func Measure(img image.Image, host string) ([]Result, error) {
	s, err := locate(img)
	if err != nil {
		return nil, err
	}
	var out []Result
	for i, c := range Cases {
		r := Result{Case: c.Name}
		if why := skipped(host, c); why != "" {
			r.Skipped = why
			out = append(out, r)
			continue
		}
		x, y := cellOrigin(i)
		p := &Probe{s: s, x: float64(x), y: float64(y), lacks: unsupported[host]}
		c.Check(p)
		r.Errors, r.Measured = p.errors, p.measured
		out = append(out, r)
	}
	return out, nil
}

// Check runs Measure on img and reports each case as a subtest of t. It
// writes img to $ASUKA_CONFORMANCE_OUT/<name>.png when that's set, so
// the captures can be looked at side by side.
func Check(t *testing.T, name string, img image.Image, host string) {
	t.Helper()
	if dir := os.Getenv("ASUKA_CONFORMANCE_OUT"); dir != "" {
		if err := writePNG(filepath.Join(dir, name+".png"), img); err != nil {
			t.Log(err)
		}
	}
	results, err := Measure(img, host)
	if err != nil {
		t.Fatalf("%s: %v", name, err)
	}
	for _, r := range results {
		t.Run(r.Case, func(t *testing.T) {
			if len(r.Measured) > 0 {
				t.Log(strings.Join(r.Measured, ", "))
			}
			if r.Skipped != "" {
				t.Skip(r.Skipped)
			}
			for _, e := range r.Errors {
				t.Error(e)
			}
		})
	}
}

func writePNG(path string, img image.Image) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return png.Encode(f, img)
}

// sheet is where the sheet is in a capture: its top left, in pixels,
// and the pixels per point.
type sheet struct {
	img        image.Image
	ox, oy     float64
	sx, sy     float64
	maxX, maxY int
	// colors are how the capture shows the cases' colors (white, the
	// swatches'), by their sRGB values.
	colors map[RGB]RGB
}

// locate finds the sheet by its markers: the magenta square nearest the
// capture's top left and the one nearest its bottom right.
func locate(img image.Image) (*sheet, error) {
	b := img.Bounds()
	magenta := func(x, y int) bool {
		r, g, bl, _ := img.At(x, y).RGBA()
		return r>>8 > 200 && g>>8 < 70 && bl>>8 > 200
	}
	// Each marker's pixels: a square's bounding box. The top left one is
	// the magenta pixels nearest the top left, then its box grows.
	box := func(x, y int) image.Rectangle {
		x0, y0, x1, y1 := x, y, x, y
		for x0 > b.Min.X && magenta(x0-1, y) {
			x0--
		}
		for x1+1 < b.Max.X && magenta(x1+1, y) {
			x1++
		}
		for y0 > b.Min.Y && magenta(x, y0-1) {
			y0--
		}
		for y1+1 < b.Max.Y && magenta(x, y1+1) {
			y1++
		}
		return image.Rect(x0, y0, x1+1, y1+1)
	}
	var first, last image.Rectangle
	found := false
	for y := b.Min.Y; y < b.Max.Y && !found; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			if magenta(x, y) {
				// Its row's middle: the square's middle column, so the box
				// isn't cut by an antialiased corner.
				r := box(x, y)
				first, found = box((r.Min.X+r.Max.X)/2, y+(r.Dx())/2), true
				break
			}
		}
	}
	if !found {
		return nil, fmt.Errorf("no registration marker in the %dx%d capture: is the sheet on screen?", b.Dx(), b.Dy())
	}
	found = false
	for y := b.Max.Y - 1; y >= b.Min.Y && !found; y-- {
		for x := b.Max.X - 1; x >= b.Min.X; x-- {
			if magenta(x, y) {
				r := box(x, y)
				last, found = box((r.Min.X+r.Max.X)/2, y-r.Dx()/2), true
				break
			}
		}
	}
	if !found || last == first {
		return nil, fmt.Errorf("one registration marker in the capture: is all of the sheet on screen?")
	}
	w, h := SheetSize()
	s := &sheet{
		img: img, maxX: b.Max.X - 1, maxY: b.Max.Y - 1,
		ox: float64(first.Min.X), oy: float64(first.Min.Y),
		sx: float64(last.Max.X-first.Min.X) / float64(w),
		sy: float64(last.Max.Y-first.Min.Y) / float64(h),
	}
	// Each marker is marker points square: the scales agree, or what was
	// found isn't the sheet.
	for _, k := range []float64{float64(first.Dx()) / marker, float64(first.Dy()) / marker, float64(last.Dx()) / marker, float64(last.Dy()) / marker} {
		if k < s.sx-1.5/marker*2 || k > s.sx+1.5/marker*2 {
			return nil, fmt.Errorf("the markers (%v, %v) don't match the sheet's scale %.3f", first, last, s.sx)
		}
	}
	if d := s.sx - s.sy; d > 0.02 || d < -0.02 {
		return nil, fmt.Errorf("the sheet's scale differs across (%.3f) and down (%.3f)", s.sx, s.sy)
	}
	// The colors as the capture shows them: the white between the marker
	// and the first swatch, each swatch's middle.
	s.colors = map[RGB]RGB{white: s.at(1.5*marker, marker/2)}
	for i, sw := range swatches {
		got := s.at(float64(swatchX(i))+marker/2, marker/2)
		// A display's space moves a color, never this far (sRGB's red is
		// (234,51,35) in Display P3).
		if d := math.Abs(got.R-sw.rgb.R) + math.Abs(got.G-sw.rgb.G) + math.Abs(got.B-sw.rgb.B); d > 160 {
			return nil, fmt.Errorf("the swatch of %s shows %v: is the sheet the cases' own?", sw.hex, got)
		}
		s.colors[sw.rgb] = got
	}
	return s, nil
}

// pixel is the capture's color at pixel coordinates (clamped to it).
func (s *sheet) pixel(px, py int) RGB {
	px = min(max(px, s.img.Bounds().Min.X), s.maxX)
	py = min(max(py, s.img.Bounds().Min.Y), s.maxY)
	r, g, b, a := s.img.At(px, py).RGBA()
	if a == 0 {
		return RGB{}
	}
	// Straight, not premultiplied: captures are opaque, but be safe.
	k := 255 / float64(a)
	return RGB{float64(r) * k, float64(g) * k, float64(b) * k}
}

// at is the color at the point (x, y) of the sheet.
func (s *sheet) at(x, y float64) RGB {
	return s.pixel(int(math.Floor(s.ox+x*s.sx)), int(math.Floor(s.oy+y*s.sy)))
}
