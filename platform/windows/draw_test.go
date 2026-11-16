package windows_test

import (
	"bytes"
	"image"
	"image/color"
	"image/gif"
	"image/png"
	"os"
	"path/filepath"
	"testing"
)

// writeAssets writes the drawing test's assets: pic.png (64×48, a
// gradient) and anim.gif (two frames, red then blue, played once: the
// last stays).
func writeAssets(t *testing.T, dir string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Join(dir, "assets"), 0o755); err != nil {
		t.Fatal(err)
	}
	img := image.NewNRGBA(image.Rect(0, 0, 64, 48))
	for y := range 48 {
		for x := range 64 {
			img.SetNRGBA(x, y, color.NRGBA{uint8(x * 4), uint8(y * 5), 128, 255})
		}
	}
	var b bytes.Buffer
	if err := png.Encode(&b, img); err != nil {
		t.Fatal(err)
	}
	pal := color.Palette{color.NRGBA{255, 0, 0, 255}, color.NRGBA{0, 0, 255, 255}}
	var frames []*image.Paletted
	for i := range 2 {
		f := image.NewPaletted(image.Rect(0, 0, 16, 16), pal)
		for j := range f.Pix {
			f.Pix[j] = uint8(i)
		}
		frames = append(frames, f)
	}
	var g bytes.Buffer
	if err := gif.EncodeAll(&g, &gif.GIF{Image: frames, Delay: []int{20, 20}, LoopCount: -1}); err != nil {
		t.Fatal(err)
	}
	for name, data := range map[string][]byte{"pic.png": b.Bytes(), "anim.gif": g.Bytes()} {
		if err := os.WriteFile(filepath.Join(dir, "assets", name), data, 0o644); err != nil {
			t.Fatal(err)
		}
	}
}

// What the host draws itself (testdata/draw.asuka): images from the
// app's assets (their natural size reported, an animated GIF, a missing
// one failing, a tinted one), a Shape, a Canvas, a Shader rendered by
// Direct3D 11, a rounded clip, a turned view, a faded one and a
// material, each looked at in the window's picture.
func TestDrawing(t *testing.T) {
	w := onWindows(t)
	local, guest := w.job(t)
	src, err := os.ReadFile(filepath.Join("testdata", "draw.asuka"))
	if err != nil {
		t.Fatal(err)
	}
	buildApp(t, w, string(src), local)
	writeAssets(t, local)
	d := script(t, w, local, guest, "app.exe", "wait:1500ms", "snap:drawn", "tap:Faded", "wait:500ms", "snap:after")
	shows(t, d["drawn"], "drawn", []string{"Loaded 64x48, failed", "Turned", "Glass"})
	drawn, after := picture(t, local, "drawn"), picture(t, local, "after")
	// Points are pixels at 96 DPI; each check is a pixel of the window's
	// client area.
	type check struct {
		what string
		x, y int
		ok   func(r, g, b int) bool
	}
	red := func(r, g, b int) bool { return r > 180 && g < 90 && b < 90 }
	green := func(r, g, b int) bool { return g > 100 && r < 80 && b < 80 }
	behind := func(r, g, b int) bool { return r > 225 && g > 225 && b > 225 && max(r, g, b)-min(r, g, b) < 12 }
	for _, c := range []check{
		{"the image", 48, 48, func(r, g, b int) bool { return !behind(r, g, b) }},
		{"the image, tinted", 184, 32, red},
		{"the animation, played to its last frame", 112, 40, func(r, g, b int) bool { return b > 180 && r < 80 }},
		{"the shape's fill", 36, 135, red},
		{"the shape's stroke", 17, 135, func(r, g, b int) bool { return b > 150 && r < 100 }},
		{"the trimmed shape's base", 84, 153, func(r, g, b int) bool { return !green(r, g, b) }},
		{"the canvas's radial gradient", 132, 130, func(r, g, b int) bool { return r > 150 && g > 150 && b > 150 }},
		{"the shader, its right", 700, 183, green},
		{"the shader, its left", 24, 183, func(r, g, b int) bool { return g < 40 }},
		{"the shader's rounded corner", 16, 163, behind},
		{"the clip's inside", 46, 241, red},
		{"the clip's corner", 17, 212, behind},
		{"the turned view, under its text", 127, 238, green},
		{"the turned view's old corner", 93, 212, func(r, g, b int) bool { return !green(r, g, b) }},
		{"the faded view", 198, 262, func(r, g, b int) bool { return r > 230 && g > 230 && b > 90 && b < 160 }},
		{"the material", 275, 290, func(r, g, b int) bool { return r > 235 && g > 235 && b > 235 }},
	} {
		if r, g, b := drawn.at(c.x, c.y); !c.ok(r, g, b) {
			t.Errorf("%s at %d, %d: %d %d %d", c.what, c.x, c.y, r, g, b)
		}
	}
	if r, g, b := after.at(84, 153); !green(r, g, b) {
		t.Errorf("trimmed to its end, the shape's base: %d %d %d", r, g, b)
	}
	if t.Failed() {
		t.Logf("%s", d["drawn"])
	}
}

type snapshot struct {
	img    image.Image
	dx, dy int // where the client area is in the window's picture
}

// picture reads a snapshot of the window (its frame and title bar too),
// its client area 800×600.
func picture(t *testing.T, dir, name string) snapshot {
	t.Helper()
	f, err := os.Open(filepath.Join(dir, name+".png"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	b := img.Bounds()
	dx := (b.Dx() - 800) / 2
	return snapshot{img, dx, b.Dy() - 600 - dx}
}

func (s snapshot) at(x, y int) (r, g, b int) {
	c := color.NRGBAModel.Convert(s.img.At(s.dx+x, s.dy+y)).(color.NRGBA)
	return int(c.R), int(c.G), int(c.B)
}
