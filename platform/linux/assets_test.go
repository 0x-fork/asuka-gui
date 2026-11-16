package linux_test

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
// last stays), in the assets folder beside the executable.
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
