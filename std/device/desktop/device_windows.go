//go:build windows

package device

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	"image/png"
	"mime"
	"os"
	"strings"
	"time"

	"github.com/egoist/asuka/internal/boot"
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the open dialog (modal to the window the user works in),
// ShellExecute for the browser, the OLE clipboard (its sequence number
// read twice a second while watched), SetThreadExecutionState.

func supported() error { return nil }

func (platform) Info(context.Context) (Info, error) {
	return Info{Model: "PC", System: "Windows", SystemVersion: fmt.Sprintf("10.0.%d", Build()), AppVersion: boot.Version, Build: boot.Build}, nil
}

func (platform) Browse(_ context.Context, u string) error {
	if err := webURL(u); err != nil {
		return err
	}
	native.Post(func() { OpenURL(u) })
	return nil
}

// pick shows the open dialog on the main thread and copies what's chosen.
func pick(ctx context.Context, filters []FileFilter, multiple bool, limit int) ([]File, error) {
	type picked struct {
		paths []string
		err   error
	}
	p, err := wait(ctx, func(done func(picked)) {
		paths, err := OpenFiles(native.Window(), "", filters, multiple)
		done(picked{paths, err})
	})
	if err != nil {
		return nil, err
	}
	if p.err != nil || len(p.paths) == 0 {
		return []File{}, p.err
	}
	if limit > 0 && len(p.paths) > limit {
		p.paths = p.paths[:limit]
	}
	return copyPicked(cacheDir("asuka-files"), p.paths)
}

func (platform) PickPhotos(ctx context.Context, max int) ([]string, error) {
	files, err := pick(ctx, fileFilters([]string{"image/*"}), max != 1, max)
	return photos(files), err
}

func (platform) PickFiles(ctx context.Context, types []string, multiple bool) ([]File, error) {
	return pick(ctx, fileFilters(types), multiple, 0)
}

// fileFilters are the open dialog's types for MIME types: the extensions
// Windows knows for each ("image/*" its family's); none: any file.
func fileFilters(mimes []string) []FileFilter {
	families := map[string]string{
		"image/*": "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.heic;*.heif;*.tif;*.tiff;*.ico;*.svg",
		"video/*": "*.mp4;*.m4v;*.mov;*.webm;*.avi;*.wmv;*.mkv",
		"audio/*": "*.mp3;*.m4a;*.aac;*.wav;*.flac;*.ogg;*.opus;*.wma",
		"text/*":  "*.txt;*.csv;*.md;*.html;*.htm;*.xml;*.json;*.log",
	}
	var pats []string
	for _, m := range mimes {
		m = strings.TrimSpace(m)
		switch {
		case m == "" || m == "*/*" || m == "application/*":
			return nil
		case families[m] != "":
			pats = append(pats, families[m])
		default:
			exts, _ := mime.ExtensionsByType(m)
			for _, e := range exts {
				pats = append(pats, "*"+e)
			}
		}
	}
	if len(pats) == 0 {
		return nil
	}
	return []FileFilter{{Name: strings.Join(mimes, ", "), Patterns: strings.Join(pats, ";")}, {Name: "*.*", Patterns: "*.*"}}
}

func (platform) ClipboardText(context.Context) (*string, error) {
	var s string
	var ok bool
	native.OnMain(func() { s, ok = ClipboardText() })
	if !ok {
		return nil, nil
	}
	return &s, nil
}

func kinds() ClipboardKinds {
	text, img, url := ClipboardHas()
	return ClipboardKinds{Text: text, Image: img, Url: url}
}

func (platform) ClipboardHas(context.Context) (k ClipboardKinds, err error) {
	native.OnMain(func() { k = kinds() })
	return k, nil
}

func (platform) ClipboardImage(context.Context) (*string, error) {
	var data []byte
	native.OnMain(func() { data = ClipboardImage(native.Window()) })
	if data == nil {
		return nil, nil
	}
	path, err := writeCache("asuka-clipboard", "png", data)
	if err != nil {
		return nil, err
	}
	return &path, nil
}

func (platform) CopyImage(_ context.Context, path string) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return errors.New("device: no file at " + path)
	}
	img, format, err := image.Decode(bytes.NewReader(b))
	if err != nil {
		// Windows's decoders (WIC: HEIC, WebP, TIFF...), upright.
		wic, werr := DecodeImageFile(path, 0)
		if werr != nil {
			return errors.New("device: not an image: " + path)
		}
		img, format = wic, ""
	}
	pngData := b
	if format != "png" {
		var buf bytes.Buffer
		if png.Encode(&buf, img) == nil {
			pngData = buf.Bytes()
		}
	}
	native.OnMain(func() { err = SetClipboardImage(native.Window(), img, pngData) })
	return err
}

func (platform) WatchClipboard(ctx context.Context, changed func(ClipboardKinds)) error {
	var seen uint32
	native.OnMain(func() { seen = ClipboardSequence() })
	t := time.NewTicker(500 * time.Millisecond)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-t.C:
			native.Post(func() {
				if n := ClipboardSequence(); n != seen {
					seen = n
					changed(kinds())
				}
			})
		}
	}
}

func (platform) KeepAwake(on bool) error {
	KeepAwake(on)
	return nil
}
