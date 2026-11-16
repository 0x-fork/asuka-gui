// The shared desktop C's tests (test/desktop_test.c), built with this
// computer's compiler and run here: what platform/windows and
// platform/linux share, checked on any system.
package desktop_test

import (
	"bytes"
	"fmt"
	"image"
	"image/color"
	"image/gif"
	"os"
	"os/exec"
	"strings"
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/internal/animimage"
	"github.com/egoist/asuka/internal/cbuild"
)

func TestC(t *testing.T) {
	cc, err := cbuild.Find()
	if err != nil {
		t.Skip(err)
	}
	srcs, _ := filepath.Glob("src/*.c")
	srcs = append(srcs, "test/desktop_test.c")
	exe := filepath.Join(t.TempDir(), "desktop_test")
	args := append(append([]string{}, cc.Args...), cbuild.Flags...)
	args = append(args, "-Iinclude", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined")
	args = append(args, srcs...)
	args = append(args, "-o", exe)
	if out, err := exec.Command(cc.Path, args...).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	out, err := exec.Command(exe, t.TempDir()).CombinedOutput()
	if err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
}

// TestAnim decodes GIFs with the C (src/anim.c) and plays them, frame by
// frame, as internal/animimage does in Go: their canvases the same.
func TestAnim(t *testing.T) {
	cc, err := cbuild.Find()
	if err != nil {
		t.Skip(err)
	}
	dir := t.TempDir()
	exe := filepath.Join(dir, "anim_test")
	args := append(append([]string{}, cc.Args...), cbuild.Flags...)
	args = append(args, "-Iinclude", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "src/anim.c", "src/files.c", "src/json.c", "test/anim_test.c", "-o", exe)
	if out, err := exec.Command(cc.Path, args...).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	red, blue := color.NRGBA{255, 0, 0, 255}, color.NRGBA{0, 0, 255, 255}
	pal := color.Palette{color.NRGBA{}, red, blue, color.NRGBA{0, 255, 0, 255}}
	paletted := func(r image.Rectangle, c uint8) *image.Paletted {
		p := image.NewPaletted(r, pal)
		for i := range p.Pix {
			p.Pix[i] = c
		}
		return p
	}
	gifs := map[string]*gif.GIF{
		"disposal": {Image: []*image.Paletted{paletted(image.Rect(0, 0, 4, 4), 1), paletted(image.Rect(2, 2, 4, 4), 2)},
			Delay: []int{5, 0}, Disposal: []byte{0, gif.DisposalBackground}, LoopCount: 2, Config: image.Config{Width: 4, Height: 4}},
		"previous": {Image: []*image.Paletted{paletted(image.Rect(0, 0, 6, 5), 1), paletted(image.Rect(1, 1, 4, 3), 3), paletted(image.Rect(0, 0, 2, 2), 2)},
			Delay: []int{20, 30, 40}, Disposal: []byte{0, gif.DisposalPrevious, 0}, Config: image.Config{Width: 6, Height: 5}},
		"transparent": {Image: []*image.Paletted{paletted(image.Rect(0, 0, 3, 3), 2), paletted(image.Rect(0, 0, 3, 3), 0)},
			Delay: []int{10, 10}, LoopCount: -1, Config: image.Config{Width: 3, Height: 3}},
	}
	// A bigger one: LZW codes past 9 bits.
	big := image.NewPaletted(image.Rect(0, 0, 61, 47), pal)
	for i := range big.Pix {
		big.Pix[i] = uint8((i*7 + i/13) % 4)
	}
	gifs["big"] = &gif.GIF{Image: []*image.Paletted{big, paletted(image.Rect(5, 5, 20, 30), 3)}, Delay: []int{3, 7}, Config: image.Config{Width: 61, Height: 47}}
	for name, g := range gifs {
		var b bytes.Buffer
		if err := gif.EncodeAll(&b, g); err != nil {
			t.Fatal(err)
		}
		path := filepath.Join(dir, name+".gif")
		if err := os.WriteFile(path, b.Bytes(), 0o644); err != nil {
			t.Fatal(err)
		}
		out, err := exec.Command(exe, path).CombinedOutput()
		if err != nil {
			t.Fatalf("%s: %v\n%s", name, err, out)
		}
		a, err := animimage.Decode(b.Bytes(), nil)
		if err != nil {
			t.Fatal(err)
		}
		var want strings.Builder
		fmt.Fprintf(&want, "animated %d\n%dx%d loops %d frames %d\n", map[bool]int{false: 0, true: 1}[animimage.IsAnimated(b.Bytes())], a.Width, a.Height, a.Loops, len(a.Frames))
		p := animimage.NewPlayer(a)
		for range 8 {
			d, done := p.Next()
			if done {
				want.WriteString("-1 ")
				break
			}
			fmt.Fprintf(&want, "%d %x\n", d.Milliseconds(), p.Canvas().Pix)
		}
		want.WriteString("\n")
		if string(out) != want.String() {
			t.Errorf("%s:\n got %s\nwant %s", name, out, want.String())
		}
	}
}
