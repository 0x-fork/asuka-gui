// asuka/imageedit on Windows (WIC and Media Foundation decode, Go does
// the actions and writes, imageedit_windows.go) and Linux (GdkPixbuf and
// GStreamer decode, imageedit_linux.go); macOS runs the package's Swift.
// Each call's work is a goroutine's (the async method's own).
package imageedit

import (
	"context"
	"encoding/json"
	"errors"
	"time"
)

var errNoImage = errors.New("no image")

// The steps' op names, as the hosts' requests had them.
var opNames = map[Op]string{OpResize: "resize", OpCrop: "crop", OpRotate: "rotate", OpFlipHorizontal: "flipH", OpFlipVertical: "flipV", OpFill: "fill", OpPixelate: "pixelate"}

type ieAction struct {
	Op      string  `json:"op"`
	Width   int     `json:"width"`
	Height  int     `json:"height"`
	X       int     `json:"x"`
	Y       int     `json:"y"`
	Degrees float64 `json:"degrees"`
	Block   int     `json:"block"`
	RGB     int     `json:"rgb"`
}

func actionsOf(steps []Step) []ieAction {
	as := make([]ieAction, len(steps))
	for i, s := range steps {
		as[i] = ieAction{Op: opNames[s.Op], Width: s.Width, Height: s.Height, X: s.X, Y: s.Y, Degrees: s.Degrees, Block: s.Block, RGB: s.Rgb}
	}
	return as
}

// result is a call's answer: its JSON (path, width, height), or its
// error, no image the package's.
func result(b []byte, err error) (Result, error) {
	if errors.Is(err, errNoImage) {
		return Result{}, ErrNoImage
	}
	if err != nil {
		return Result{}, err
	}
	var r Result
	err = json.Unmarshal(b, &r)
	return r, err
}

type goEditor struct{}

func newEditor() (Editor, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return goEditor{}, nil
}

func (goEditor) Edit(ctx context.Context, source string, steps []Step, format Format, quality float64) (Result, error) {
	return result(run("edit", source, steps, format, quality, 0))
}

func (goEditor) Size(ctx context.Context, source string) (Result, error) {
	return result(run("size", source, nil, FormatJpeg, 0.9, 0))
}

func (goEditor) VideoFrame(ctx context.Context, source string, at time.Duration, steps []Step, format Format, quality float64) (Result, error) {
	return result(run("videoFrame", source, steps, format, quality, at))
}

func formatName(f Format) string {
	if f == FormatPng {
		return "png"
	}
	return "jpeg"
}
