//go:build windows

package vision

import (
	"context"
	"errors"
	stdimage "image"
	"strings"
	"unicode/utf8"

	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the image decoded upright (WIC), its text read by Windows's
// OCR (Windows.Media.Ocr: line by line, word by word, in the first
// language asked for whose OCR is installed, else the user's), its faces
// found by Windows.Media.FaceAnalysis, its codes read by package barcode;
// boxes in fractions of the image from its top left.

type winAnalyzer struct{}

func newAnalyzer() (Analyzer, error) { return winAnalyzer{}, nil }

func union(b, o Box) Box {
	if b.W <= 0 || b.H <= 0 {
		return o
	}
	x0, y0 := min(b.X, o.X), min(b.Y, o.Y)
	x1, y1 := max(b.X+b.W, o.X+o.W), max(b.Y+b.H, o.Y+o.H)
	return Box{X: x0, Y: y0, W: x1 - x0, H: y1 - y0}
}

func (winAnalyzer) Analyze(ctx context.Context, path string, text, faces, codes bool, languages []string) (out Result, err error) {
	local, lerr := native.LocalFile(path)
	if lerr != nil {
		return Result{}, errNoImage
	}
	WithCOM(func() {
		img, e := DecodeImageFile(local, 0)
		if e != nil {
			err = errNoImage
			return
		}
		w, ht := img.Rect.Dx(), img.Rect.Dy()
		out = Result{Width: w, Height: ht, Lines: []Line{}, Faces: []Box{}, Codes: []Code{}}
		if text {
			if out.Lines, err = visReadText(local, img, languages); err != nil {
				return
			}
		}
		if faces {
			rs, e := DetectFaces(img)
			if e != nil {
				err = e
				return
			}
			for _, r := range rs {
				out.Faces = append(out.Faces, Box{X: float64(r.Min.X) / float64(w), Y: float64(r.Min.Y) / float64(ht), W: float64(r.Dx()) / float64(w), H: float64(r.Dy()) / float64(ht)})
			}
		}
		if codes {
			for _, c := range scanImage(img, nil) {
				r := c.Bounds
				out.Codes = append(out.Codes, Code{Kind: string(c.Kind), Value: c.Text,
					Box: Box{X: float64(r.Min.X) / float64(w), Y: float64(r.Min.Y) / float64(ht), W: float64(r.Dx()) / float64(w), H: float64(r.Dy()) / float64(ht)}})
			}
		}
	})
	return out, err
}

// visReadText reads the lines: the image scaled down to what the OCR
// takes, if it's larger (the boxes are fractions: the same).
func visReadText(path string, img *stdimage.NRGBA, languages []string) ([]Line, error) {
	w, ht := img.Rect.Dx(), img.Rect.Dy()
	src := img
	if m := OCRMaxSide(); m > 0 && max(w, ht) > m {
		small, err := DecodeImageFile(path, m)
		if err != nil {
			return nil, err
		}
		src = small
	}
	sw, sh := float64(src.Rect.Dx()), float64(src.Rect.Dy())
	lines, err := OCR(src, languages)
	if errors.Is(err, ErrNoOCR) {
		return nil, errors.New("not available: " + err.Error())
	}
	if err != nil {
		return nil, err
	}
	out := []Line{}
	for _, l := range lines {
		line := Line{Text: l.Text, Words: []Word{}}
		at := 0 // bytes into the line's text
		for _, wd := range l.Words {
			box := Box{X: wd.X / sw, Y: wd.Y / sh, W: wd.W / sw, H: wd.H / sh}
			line.Box = union(line.Box, box)
			start, end := 0, 0
			if i := strings.Index(l.Text[at:], wd.Text); i >= 0 {
				start = utf8.RuneCountInString(l.Text[:at+i])
				at += i + len(wd.Text)
				end = start + utf8.RuneCountInString(wd.Text)
			}
			line.Words = append(line.Words, Word{Text: wd.Text, Box: box, Start: start, End: end})
		}
		out = append(out, line)
	}
	return out, nil
}
