// asuka/screen on Windows and Linux (macOS runs the package's Swift): the
// displays, read on the main thread and followed there; capture kept out
// on Windows. A desktop's display doesn't turn with the window, and it
// has no status or navigation bar: lock and setBars do nothing.
package screen

import (
	"context"

	"github.com/egoist/asuka/native"
)

type screens struct{}

func newScreens() (Screens, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return screens{}, nil
}

func (screens) Displays() (ds []Display, err error) {
	native.OnMain(func() { ds = read() })
	return ds, nil
}

func (screens) WatchDisplays(ctx context.Context, changed func([]Display)) error {
	var stop func()
	native.OnMain(func() {
		var last []Display
		first := true
		report := func() {
			ds := read()
			if !first && same(ds, last) {
				return
			}
			first, last = false, ds
			changed(ds)
		}
		stop = follow(report)
		report()
	})
	<-ctx.Done()
	native.Post(stop)
	return ctx.Err()
}

func same(a, b []Display) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func (screens) Lock(Orientation) error { return nil }

func (screens) SetBars(BarStyle, bool, bool) error { return nil }

func (screens) PreventCapture(on bool) error {
	preventCapture(on)
	return nil
}

// WatchScreenshots: a desktop doesn't tell an app of its screenshots.
func (screens) WatchScreenshots(ctx context.Context, _ func(int)) error {
	<-ctx.Done()
	return ctx.Err()
}
