//go:build windows

package scan

import (
	"context"

	"github.com/egoist/asuka/native"
)

type barcodeReader struct{}

func newReader() (Reader, error) { return barcodeReader{}, nil }

func (barcodeReader) File(ctx context.Context, path string, kinds []string) ([]Code, error) {
	local, err := native.LocalFile(path)
	if err != nil {
		return nil, errNoImage
	}
	k := scanKinds{}
	for _, x := range kinds {
		k[x] = true
	}
	found, _, _, err := scanFile(local, k)
	if err != nil {
		return nil, errNoImage
	}
	codes := []Code{}
	seen := map[Code]bool{}
	for _, c := range found {
		code := Code{Kind: string(c.Kind), Value: c.Text}
		if k.wants(code.Kind) && !seen[code] {
			seen[code] = true
			codes = append(codes, code)
		}
	}
	return codes, nil
}
