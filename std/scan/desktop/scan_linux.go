//go:build linux && !android

package scan

import (
	"context"
	"errors"
	"image"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: ZBar reads codes (QR, EAN, UPC, Code 128, 39 and 93, Codabar,
// interleaved 2 of 5, PDF417: it reads no Aztec or Data Matrix codes),
// where it's installed (libzbar0), in the image decoded upright.

type zbarReader struct{}

func newReader() (Reader, error) { return zbarReader{}, nil }

func (zbarReader) File(ctx context.Context, path string, kinds []string) ([]Code, error) {
	if err := LoadZBar(); err != nil {
		return nil, errors.New("scan: not available: " + err.Error())
	}
	img, err := ieDecode(path, assets())
	if err != nil {
		return nil, err
	}
	want := map[string]bool{}
	for _, k := range kinds {
		want[k] = true
	}
	wants := func(k string) bool { return len(want) == 0 || want[k] }
	codes := []Code{}
	seen := map[Code]bool{}
	for _, s := range ZBarScan(grayOf(img), img.Rect.Dx(), img.Rect.Dy()) {
		kind, value := zbarKind(s.Type), s.Data
		if kind == "" {
			continue
		}
		// An app that asked for EAN-13s but not UPC-As gets a UPC-A as an
		// EAN-13 with a leading 0, as Apple's readers give it.
		if kind == "upca" && !wants("upca") && wants("ean13") && len(value) == 12 {
			kind, value = "ean13", "0"+value
		}
		c := Code{Kind: kind, Value: value}
		if !wants(kind) || seen[c] {
			continue
		}
		seen[c] = true
		codes = append(codes, c)
	}
	return codes, nil
}

// grayOf is the image's luma, a byte a pixel (on white where it's
// transparent).
func grayOf(img *image.RGBA) []byte {
	w, h := img.Rect.Dx(), img.Rect.Dy()
	g := make([]byte, w*h)
	for y := range h {
		row := img.Pix[y*img.Stride:]
		for x := range w {
			p := row[x*4:]
			white := 255 - uint32(p[3])
			r, gg, b := uint32(p[0])+white, uint32(p[1])+white, uint32(p[2])+white
			g[y*w+x] = byte((299*r + 587*gg + 114*b) / 1000)
		}
	}
	return g
}

// zbarKind is asuka/sdk/scan's name of a ZBar symbology ("": none of its).
func zbarKind(t string) string {
	switch t {
	case "QR-Code":
		return "qr"
	case "EAN-13":
		return "ean13"
	case "EAN-8":
		return "ean8"
	case "UPC-A":
		return "upca"
	case "UPC-E":
		return "upce"
	case "CODE-128":
		return "code128"
	case "CODE-39":
		return "code39"
	case "CODE-93":
		return "code93"
	case "Codabar", "CODABAR":
		return "codabar"
	case "I2/5":
		return "itf"
	case "PDF417":
		return "pdf417"
	}
	return ""
}
