//go:build darwin

package mactest

import (
	"fmt"
	"image"
	"image/color"
	"image/gif"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// visualGIF writes an animated GIF of frames solid colors, 20×10, each
// shown for 2 hundredths of a second, looping forever.
func visualGIF(t *testing.T, frames int) string {
	t.Helper()
	g := &gif.GIF{LoopCount: 0}
	pal := color.Palette{color.RGBA{255, 0, 0, 255}, color.RGBA{0, 0, 255, 255}, color.RGBA{0, 255, 0, 255}}
	for i := range frames {
		img := image.NewPaletted(image.Rect(0, 0, 20, 10), pal)
		for p := range img.Pix {
			img.Pix[p] = uint8(i % len(pal))
		}
		g.Image = append(g.Image, img)
		g.Delay = append(g.Delay, 2)
	}
	p := filepath.Join(t.TempDir(), "a.gif")
	f, err := os.Create(p)
	if err != nil {
		t.Fatal(err)
	}
	if err := gif.EncodeAll(f, g); err != nil {
		t.Fatal(err)
	}
	f.Close()
	return p
}

// visualPNG writes a w×h PNG of one color.
func visualPNG(t *testing.T, name string, w, h int) string {
	t.Helper()
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for i := 0; i < len(img.Pix); i += 4 {
		img.Pix[i], img.Pix[i+3] = 200, 255
	}
	p := filepath.Join(t.TempDir(), name)
	f, err := os.Create(p)
	if err != nil {
		t.Fatal(err)
	}
	png.Encode(f, img)
	f.Close()
	return p
}

// imageOf is the tree's only Image's state (the host's "image" JSON).
func imageOf(t *testing.T, tr *Tree, i int) map[string]any {
	t.Helper()
	ims := tr.Kind("Image")
	if len(ims) <= i {
		t.Fatalf("no Image %d:\n%s", i, tr.Raw())
	}
	return vobj(ims[i], "image")
}

// An animated GIF plays through ImageIO (the frame it shows moves on),
// reports its size as it loads, and stops where it is when paused; a
// hash placeholder shows in a layer of its own until the image loads,
// then fades out.
func TestVisualAnimatedImage(t *testing.T) {
	path := visualGIF(t, 3)
	a := Run(t, fmt.Sprintf(`
		component Shown {
		  state src = ""
		  state playing = true
		  state loaded = "not loaded"
		  Column {
		    padding: 8
		    Image(src) {
		      animated: playing; blurHash: "LEHV6nWB2yk8pyo0adR*.7kCMdnj"; width: 80; height: 40
		      on loaded(w, h) { loaded = "loaded {w}x{h}" }
		    }
		    Text(loaded)
		    Button("Load") { on press { src = %q } }
		    Button("Pause") { on press { playing = false } }
		  }
		}
		app { Shown() }
	`, path))
	tr := a.Until(func(tr *Tree) bool {
		ims := tr.Kind("Image")
		return len(ims) == 1 && vbool(vobj(ims[0], "image"), "placeholderLayer")
	})
	im := imageOf(t, tr, 0)
	if !vbool(im, "hash") || !vbool(im, "placeholderShown") || vnum(im, "placeholderOpacity") != 1 {
		t.Fatalf("no hash placeholder before the image loads: %v", im)
	}
	a.Do("tap Load")
	tr = a.Until(func(tr *Tree) bool {
		for _, n := range tr.Kind("Text") {
			if strings.HasPrefix(n.Text, "loaded ") {
				return true
			}
		}
		return false
	})
	var got string
	for _, n := range tr.Kind("Text") {
		if strings.HasPrefix(n.Text, "loaded ") {
			got = n.Text
		}
	}
	if got != "loaded 20x10" && got != "loaded 20.0x10.0" {
		t.Errorf("loaded %q, want 20x10", got)
	}
	im = imageOf(t, tr, 0)
	if !vbool(im, "loaded") || !vbool(im, "animated") {
		t.Fatalf("loaded %v, animated %v", im["loaded"], im["animated"])
	}
	start := vnum(im, "frame")
	a.Until(func(tr *Tree) bool { return vnum(imageOf(t, tr, 0), "frame") != start })

	a.Do("tap Pause")
	tr = a.Until(func(tr *Tree) bool { return vbool(imageOf(t, tr, 0), "paused") })
	at := vnum(imageOf(t, tr, 0), "frame")
	time.Sleep(300 * time.Millisecond) // many frames' time
	if f := vnum(imageOf(t, a.Tree(), 0), "frame"); f != at {
		t.Errorf("paused at %v, now at %v", at, f)
	}
	// The placeholder faded out and went.
	a.Until(func(tr *Tree) bool { return !vbool(imageOf(t, tr, 0), "placeholderLayer") })
}

// A placeholder image (a file) shows in the placeholder's layer while the
// source loads, in its own mode; a source that fails reports its error
// and leaves the placeholder.
func TestVisualImagePlaceholderFile(t *testing.T) {
	ph := visualGIF(t, 1)
	a := Run(t, fmt.Sprintf(`
		component P {
		  state log = ""
		  Column {
		    padding: 8; gap: 8
		    Image("https://localhost:1/never.png") {
		      placeholder: %q; mode: .fill; placeholderMode: .fit; width: 80; height: 40
		      on error(m) { log = log + "error;" }
		    }
		    Image("/nonexistent/asuka-none.png") { placeholder: %q; color: #ff0000; width: 30; height: 30 }
		    Text("log:{log}")
		  }
		}
		app { P() }
	`, ph, ph))
	tr := a.Until(func(tr *Tree) bool {
		ims := tr.Kind("Image")
		return len(ims) == 2 && vbool(vobj(ims[0], "image"), "placeholderShown") && vbool(vobj(ims[1], "image"), "placeholderShown")
	})
	im := imageOf(t, tr, 0)
	if !vbool(im, "placeholderLayer") || vstr(im, "placeholderGravity") != "resizeAspect" {
		t.Errorf("the placeholder: %v", im)
	}
	// A tint is its color, masked by the placeholder.
	if im := imageOf(t, tr, 1); !vbool(im, "tinted") {
		t.Errorf("the tinted placeholder: %v", im)
	}
	l := &vlogger{a: a}
	l.until(t, func(s string) bool { return strings.Contains(s, "error;") })
	if im := imageOf(t, a.Tree(), 0); !vbool(im, "placeholderShown") || vbool(im, "loaded") {
		t.Errorf("after the error: %v", im)
	}
}

// An image's modes: fit and stretch and center are NSImageView's
// scalings, fill its layer's contents, cropped; a color makes it a
// template, tinted; it fades in as it arrives (Transition), or not
// (-1); the cache serves a source loaded before. Symbols are SF Symbols
// at their font's size, in the label color or their own.
func TestVisualImageModes(t *testing.T) {
	p := visualPNG(t, "red.png", 30, 20)
	a := Run(t, fmt.Sprintf(`
		component Modes {
		  state more = false
		  Column {
		    padding: 8; gap: 8; align: .start
		    Image(%[1]q) { width: 60; height: 40 }
		    Image(%[1]q) { mode: .fill; width: 61; height: 40 }
		    Image(%[1]q) { mode: .stretch; width: 62; height: 40 }
		    Image(%[1]q) { mode: .center; width: 63; height: 40; transition: 60 }
		    Image(%[1]q) { color: #00ff00; width: 64; height: 40; transition: -1 }
		    Image(%[1]q)
		    if more { Image(%[1]q) { width: 65; height: 40 } }
		    Symbol("star.fill") { font: Font(size: 30) }
		    Symbol("heart") { color: #ff0000 }
		    Symbol("no.such.symbol.asuka")
		    Button("More") { on press { more = true } }
		  }
		}
		app { Modes() }
	`, p))
	tr := a.Until(func(tr *Tree) bool {
		ims := tr.Kind("Image")
		if len(ims) != 6 {
			return false
		}
		for _, n := range ims {
			if !vbool(vobj(n, "image"), "loaded") {
				return false
			}
		}
		// A fill's image is its layer's contents once it's placed (its
		// load and its frame may come in either order).
		for _, n := range ims {
			if len(n.Frame) == 4 && n.Frame[2] == 61 && !vbool(vobj(n, "image"), "layerContents") {
				return false
			}
		}
		return true
	})
	fit, fill, stretch, center := kindSized(t, tr, "Image", 60, 40), kindSized(t, tr, "Image", 61, 40), kindSized(t, tr, "Image", 62, 40), kindSized(t, tr, "Image", 63, 40)
	tinted := kindSized(t, tr, "Image", 64, 40)
	if im := vobj(fit, "image"); vnum(im, "scaling") != 3 || !vbool(im, "viewImage") {
		t.Errorf("fit: %v", im)
	}
	if im := vobj(fill, "image"); vbool(im, "viewImage") || !vbool(im, "layerContents") || !fill.Bool("layerMasks") {
		t.Errorf("fill: %v (masks %v)", im, fill.Bool("layerMasks"))
	}
	if im := vobj(stretch, "image"); vnum(im, "scaling") != 1 {
		t.Errorf("stretch: %v", im)
	}
	if im := vobj(center, "image"); vnum(im, "scaling") != 2 || vnum(im, "arrival") != 60 || vbool(im, "template") {
		t.Errorf("center, fading in for 60s, not a template: %v", im)
	}
	if im := vobj(tinted, "image"); !vbool(im, "template") || vstr(im, "tint") == "" || im["arrival"] != nil {
		t.Errorf("tinted, no transition: %v", im)
	}
	// Without a size, its natural one: one point per pixel.
	for _, n := range tr.Kind("Image") {
		if im := vobj(n, "image"); len(vnums(im, "imageSize")) != 2 || vnums(im, "imageSize")[0] != 30 {
			t.Errorf("image size %v", im["imageSize"])
		}
	}
	natural := tr.Kind("Image")[5]
	if natural.Frame[2] != 30 || natural.Frame[3] != 20 {
		t.Errorf("the natural size %v", natural.Frame)
	}
	// A source loaded before comes from the cache, at once.
	a.Do("tap More")
	a.Until(func(tr *Tree) bool {
		for _, n := range tr.Kind("Image") {
			if len(n.Frame) == 4 && n.Frame[2] == 65 {
				return vbool(vobj(n, "image"), "loaded")
			}
		}
		return false
	})

	syms := tr.Kind("Symbol")
	if len(syms) != 3 {
		t.Fatalf("%d symbols", len(syms))
	}
	big, red, none := syms[0], syms[1], syms[2]
	if s := vnums(big.Props, "imageSize"); len(s) != 2 || s[1] < 24 || big.Str("symbol") != "star.fill" {
		t.Errorf("a 30-point symbol: %v %v", big.Props["imageSize"], big.Str("symbol"))
	}
	if big.Str("tint") == red.Str("tint") || red.Str("tint") == "" {
		t.Errorf("tints: the label's %q, red %q", big.Str("tint"), red.Str("tint"))
	}
	if none.Props["imageSize"] != nil || none.Frame[2] != 0 {
		t.Errorf("no such symbol: %v, frame %v", none.Props["imageSize"], none.Frame)
	}
}
