// The shared desktop C's tests (test/desktop_test.c), built with this
// computer's compiler and run here: what platform/windows and
// platform/linux share, checked on any system.
package desktop_test

import (
	"bytes"
	"encoding/binary"
	"hash/crc32"
	"image"
	"image/color"
	"image/gif"
	"image/png"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

func TestC(t *testing.T) {
	cc, err := cbuild.Find()
	if err != nil {
		t.Skip(err)
	}
	// desktop.h's sources (the registry's and packages' shared C are
	// asuka_desktop.h's, tested by TestNative and TestStd).
	all, _ := filepath.Glob("src/*.c")
	var srcs []string
	for _, f := range all {
		if b, _ := os.ReadFile(f); bytes.Contains(b, []byte(`#include "desktop.h"`)) || bytes.Contains(b, []byte(`#include "words.h"`)) {
			srcs = append(srcs, f)
		}
	}
	srcs = append(srcs, "test/desktop_test.c")
	exe := filepath.Join(t.TempDir(), "desktop_test")
	args := append(append([]string{}, cc.Args...), cbuild.Flags...)
	args = append(args, "-Iinclude", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined")
	args = append(args, srcs...)
	args = append(args, "-o", exe)
	if out, err := exec.Command(cc.Path, args...).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	dir := t.TempDir()
	writeAnimations(t, dir)
	out, err := exec.Command(exe, dir).CombinedOutput()
	if err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
}

// writeAnimations writes the animated images test_anim decodes, as
// internal/animimage's tests make them: anim.gif (image/gif's), anim.png
// (an APNG of image/png's encodings) and anim.webp (frames of fake
// bitstreams, a VP8L chunk of one gray byte).
func writeAnimations(t *testing.T, dir string) {
	red, blue := color.NRGBA{255, 0, 0, 255}, color.NRGBA{0, 0, 255, 255}
	pal := color.Palette{color.NRGBA{}, red, blue}
	f0 := image.NewPaletted(image.Rect(0, 0, 4, 4), pal)
	for i := range f0.Pix {
		f0.Pix[i] = 1
	}
	f1 := image.NewPaletted(image.Rect(2, 2, 4, 4), pal)
	for i := range f1.Pix {
		f1.Pix[i] = 2
	}
	f1.Pix[0] = 0 // transparent: frame 0 shows through
	var g bytes.Buffer
	if err := gif.EncodeAll(&g, &gif.GIF{Image: []*image.Paletted{f0, f1}, Delay: []int{5, 0}, Disposal: []byte{0, gif.DisposalBackground},
		LoopCount: 2, Config: image.Config{Width: 4, Height: 4}}); err != nil {
		t.Fatal(err)
	}
	chunk := func(b []byte, typ string, data []byte) []byte {
		b = binary.BigEndian.AppendUint32(b, uint32(len(data)))
		start := len(b)
		b = append(append(b, typ...), data...)
		return binary.BigEndian.AppendUint32(b, crc32.ChecksumIEEE(b[start:]))
	}
	// An APNG: a 4×4 default image, then a 2×2 frame at 1, 1, blended and
	// disposed to the previous.
	a := []byte("\x89PNG\r\n\x1a\n")
	seq := uint32(0)
	for i, r := range []image.Rectangle{image.Rect(0, 0, 4, 4), image.Rect(1, 1, 3, 3)} {
		img := image.NewNRGBA(r)
		for j := range img.Pix {
			img.Pix[j] = 200
		}
		var b bytes.Buffer
		if err := png.Encode(&b, img); err != nil {
			t.Fatal(err)
		}
		fc := binary.BigEndian.AppendUint32(nil, seq)
		seq++
		for _, v := range []int{r.Dx(), r.Dy(), r.Min.X, r.Min.Y} {
			fc = binary.BigEndian.AppendUint32(fc, uint32(v))
		}
		fc = binary.BigEndian.AppendUint16(fc, 3)
		fc = binary.BigEndian.AppendUint16(fc, 100) // 30 ms
		fc = append(fc, byte(2*i), byte(i))         // dispose: none, previous; blend: source, over
		d := b.Bytes()
		for p := 8; p+12 <= len(d); {
			n := int(binary.BigEndian.Uint32(d[p:]))
			typ, data := string(d[p+4:p+8]), d[p+8:p+8+n]
			switch {
			case typ == "IHDR" && i == 0:
				a = chunk(a, "IHDR", data)
				actl := binary.BigEndian.AppendUint32(nil, 2)
				a = chunk(a, "acTL", binary.BigEndian.AppendUint32(actl, 0))
				a = chunk(a, "fcTL", fc)
			case typ == "IDAT" && i == 0:
				a = chunk(a, "IDAT", data)
			case typ == "IDAT":
				if fc != nil {
					a = chunk(a, "fcTL", fc)
					fc = nil
				}
				a = chunk(a, "fdAT", append(binary.BigEndian.AppendUint32(nil, seq), data...))
				seq++
			}
			p += 12 + n
		}
	}
	a = chunk(a, "IEND", nil)
	// A WebP: a 4×4 canvas, frames of gray 10 (all of it) and 20 (2×2 at 2,
	// 2, not blended, disposed to the background), looping twice.
	var w []byte
	w = append(w, "WEBPVP8X"...)
	w = binary.LittleEndian.AppendUint32(w, 10)
	w = append(w, 0x12, 0, 0, 0, 3, 0, 0, 3, 0, 0)
	w = append(w, "ANIM"...)
	w = binary.LittleEndian.AppendUint32(w, 6)
	w = append(w, 0, 0, 0, 0, 2, 0)
	for _, f := range []struct{ x, y, w, h, c, flags int }{{0, 0, 4, 4, 10, 0}, {2, 2, 2, 2, 20, 3}} {
		var fr []byte
		put24 := func(v int) { fr = append(fr, byte(v), byte(v>>8), byte(v>>16)) }
		put24(f.x / 2)
		put24(f.y / 2)
		put24(f.w - 1)
		put24(f.h - 1)
		put24(40)
		fr = append(fr, byte(f.flags))
		fr = append(fr, "VP8L"...)
		fr = binary.LittleEndian.AppendUint32(fr, 1)
		fr = append(fr, byte(f.c), 0)
		w = append(w, "ANMF"...)
		w = binary.LittleEndian.AppendUint32(w, uint32(len(fr)))
		w = append(w, fr...)
	}
	webp := append(append([]byte("RIFF"), binary.LittleEndian.AppendUint32(nil, uint32(len(w)))...), w...)
	for name, b := range map[string][]byte{"anim.gif": g.Bytes(), "anim.png": a, "anim.webp": webp} {
		if err := os.WriteFile(filepath.Join(dir, name), b, 0o644); err != nil {
			t.Fatal(err)
		}
	}
}
