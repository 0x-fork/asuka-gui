//go:build linux && !android

package scan

import (
	"errors"
	"fmt"
	"image"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Images decoded upright by GdkPixbuf (what its loaders read), as the
// host's Image does.

// ieLocal is a source's file, if it's one (a path, a file:// URL, an
// asset), else "".
func ieLocal(source, assets string) string {
	switch {
	case strings.HasPrefix(source, "http://"), strings.HasPrefix(source, "https://"):
		return ""
	case strings.HasPrefix(source, "file://"):
		if u, err := url.Parse(source); err == nil {
			return u.Path
		}
		return strings.TrimPrefix(source, "file://")
	case filepath.IsAbs(source):
		return source
	}
	return filepath.Join(assets, filepath.FromSlash(source))
}

// ieDecode decodes the image upright, as premultiplied RGBA.
func ieDecode(source, assets string) (*image.RGBA, error) {
	var px Pixels
	var err error
	if p := ieLocal(source, assets); p != "" {
		if st, serr := os.Stat(p); serr != nil || st.IsDir() {
			return nil, errNoImage
		}
		px, err = DecodePixbuf(p, nil)
	} else {
		data, rerr := readSource(source, assets)
		if rerr != nil {
			return nil, rerr
		}
		px, err = DecodePixbuf("", data)
	}
	if err != nil {
		return nil, errNoImage
	}
	return ieFromPixels(px), nil
}

// ieFromPixels premultiplies GdkPixbuf's RGB or RGBA.
func ieFromPixels(px Pixels) *image.RGBA {
	img := image.NewRGBA(image.Rect(0, 0, px.W, px.H))
	for y := range px.H {
		src, dst := px.Data[y*px.Stride:], img.Pix[y*img.Stride:]
		for x := range px.W {
			s, d := src[x*px.Channels:], dst[x*4:x*4+4]
			if px.Channels == 3 {
				d[0], d[1], d[2], d[3] = s[0], s[1], s[2], 255
				continue
			}
			a := uint32(s[3])
			d[0], d[1], d[2], d[3] = byte(uint32(s[0])*a/255), byte(uint32(s[1])*a/255), byte(uint32(s[2])*a/255), byte(a)
		}
	}
	return img
}

// readSource reads a source's bytes: an http(s) URL's, a file's, an
// asset's.
func readSource(src, assets string) ([]byte, error) {
	switch {
	case strings.HasPrefix(src, "http://") || strings.HasPrefix(src, "https://"):
		resp, err := http.Get(src)
		if err != nil {
			return nil, err
		}
		defer resp.Body.Close()
		if resp.StatusCode != 200 {
			return nil, fmt.Errorf("%s: %s", src, resp.Status)
		}
		return io.ReadAll(resp.Body)
	case strings.HasPrefix(src, "file://"):
		return os.ReadFile(strings.TrimPrefix(src, "file://"))
	}
	p := src
	if !filepath.IsAbs(p) {
		p = filepath.Join(assets, filepath.FromSlash(src))
	}
	return os.ReadFile(p)
}

var errNoImage = errors.New("no image")

// assets is the app's assets' directory.
func assets() string { return filepath.Dir(native.Asset("x")) }
