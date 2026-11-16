//go:build windows

package vision

import (
	"errors"
	stdimage "image"
	"strings"

	"github.com/egoist/asuka/internal/barcode"
	. "github.com/egoist/asuka/internal/windows"
)

// Codes in images on Windows, which has no reader of its own for apps:
// package barcode's, in Go (QR codes, Data Matrix, EAN-13 and EAN-8,
// UPC-A and UPC-E, Code 128, Code 39), on the image WIC decodes upright.

var errNoImage = errors.New("no image")

// scanKinds is what asuka/sdk/scan asks for ("": every kind).
type scanKinds map[string]bool

func parseScanKinds(s string) scanKinds {
	k := scanKinds{}
	for _, x := range strings.Split(s, ",") {
		if x != "" {
			k[x] = true
		}
	}
	return k
}

func (k scanKinds) wants(kind string) bool { return len(k) == 0 || k[kind] }

// barcodeKinds is what to ask package barcode for; ok is false when
// none of the kinds asked for is one it reads (Aztec, PDF417...).
func (k scanKinds) barcodeKinds() (kinds []barcode.Kind, ok bool) {
	if len(k) == 0 {
		return nil, true
	}
	for _, b := range []barcode.Kind{barcode.QR, barcode.DataMatrix, barcode.EAN13, barcode.EAN8, barcode.UPCA, barcode.UPCE, barcode.Code128, barcode.Code39} {
		if k[string(b)] {
			kinds = append(kinds, b)
		}
	}
	return kinds, len(kinds) > 0
}

// scanImage reads the codes of the kinds asked for in img.
func scanImage(img stdimage.Image, k scanKinds) []barcode.Code {
	kinds, ok := k.barcodeKinds()
	if !ok {
		return nil
	}
	return barcode.Decode(img, kinds...)
}

// scanMaxSide: a larger photo is read scaled down to this first, then
// whole if nothing was found (a small code in a big photo).
const scanMaxSide = 2048

// scanFile reads a file's codes, the image upright. Off the main thread.
func scanFile(path string, k scanKinds) (codes []barcode.Code, w, h int, err error) {
	WithCOM(func() {
		var img *stdimage.NRGBA
		img, err = DecodeImageFile(path, 0)
		if err != nil {
			return
		}
		w, h = img.Rect.Dx(), img.Rect.Dy()
		if max(w, h) > scanMaxSide {
			if small, e := DecodeImageFile(path, scanMaxSide); e == nil {
				if codes = scanImage(small, k); len(codes) > 0 {
					sx, sy := float64(w)/float64(small.Rect.Dx()), float64(h)/float64(small.Rect.Dy())
					for i := range codes {
						b := codes[i].Bounds
						codes[i].Bounds = stdimage.Rect(int(float64(b.Min.X)*sx), int(float64(b.Min.Y)*sy), int(float64(b.Max.X)*sx+0.5), int(float64(b.Max.Y)*sy+0.5))
					}
					return
				}
			}
		}
		codes = scanImage(img, k)
	})
	return
}
