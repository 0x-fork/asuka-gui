//go:build darwin && !ios

package darwin

import (
	"fmt"
	"image"
	"image/color"
	"image/gif"
	"os"
	"path/filepath"
	"testing"

	. "github.com/egoist/asuka/internal/darwin"
	"github.com/egoist/asuka/ops"
)

// writeGIF writes an animated GIF of frames solid colors, 20×10, each
// shown for 2 hundredths of a second, looping forever.
func writeGIF(t *testing.T, frames int) string {
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

// An animated GIF plays through ImageIO (the frame it shows moves on),
// reports its size as it loads, and stops where it is when paused; a
// hash placeholder shows in a layer of its own until the image loads,
// then fades out.
func TestAnimatedImage(t *testing.T) {
	path := writeGIF(t, 3)
	h := mountApp(t, fmt.Sprintf(`
		component Shown {
		  state src = ""
		  state playing = true
		  state loaded = "not loaded"
		  Column {
		    padding: 8
		    Image(src) {
		      animated: playing; blurHash: "LEHV6nWB2yk8pyo0adR*.7kCMdnj"; width: 80; height: 40
		      on loaded(w, h) { loaded = "{w}x{h}" }
		    }
		    Text(loaded)
		    Button("Load") { on press { src = %q } }
		    Button("Pause") { on press { playing = false } }
		  }
		}
		app { Shown() }
	`, path))
	var n *node
	onMain(func() {
		n = find(h, ops.KindImage)[0]
		if n.imgx == nil || n.imgx.hash == 0 || n.imgx.layer == 0 {
			fatal(t, "no placeholder layer before the image loads")
		}
		h.enter(func() { Send(button(h, "Load").view, "performClick:", 0) })
	})
	onMain(func() {
		if !pumpUntil(func() bool { return n.loaded }) {
			fatal(t, "the GIF didn't load")
		}
		if !pumpUntil(func() bool { return text(find(h, ops.KindText)[0]) == "20x10" }) || n.imgx.anim == nil {
			fatalf(t, "loaded %q, animated %v", text(find(h, ops.KindText)[0]), n.imgx.anim != nil)
		}
		start := n.imgx.frame
		if !pumpUntil(func() bool { return n.imgx.frame != start }) {
			fatal(t, "the GIF's frames don't move on")
		}
		h.enter(func() { Send(button(h, "Pause").view, "performClick:", 0) })
		if !n.imgx.paused {
			fatal(t, "not paused")
		}
		at := n.imgx.frame
		k := 0
		pumpUntil(func() bool { k++; return k > 30 }) // several frames' time passes
		if n.imgx.frame != at {
			fatalf(t, "paused at %d, now at %d", at, n.imgx.frame)
		}
		if !pumpUntil(func() bool { return n.imgx.layer == 0 }) {
			fatal(t, "the placeholder stays")
		}
	})
}

// A placeholder image (a file) shows in the placeholder's layer while
// the source loads, in its own mode.
func TestImagePlaceholderFile(t *testing.T) {
	ph := writeGIF(t, 1)
	h := mountApp(t, fmt.Sprintf(`
		app {
		  Column {
		    padding: 8
		    Image("https://localhost:1/never.png") { placeholder: %q; mode: .fill; placeholderMode: .fit; width: 80; height: 40 }
		  }
		}
	`, ph))
	onMain(func() {
		n := find(h, ops.KindImage)[0]
		if !pumpUntil(func() bool { return n.imgx != nil && n.imgx.layer != 0 && n.imgx.shown != 0 }) {
			fatal(t, "the placeholder doesn't show")
		}
	})
}
