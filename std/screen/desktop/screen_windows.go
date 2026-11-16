//go:build windows

package screen

import (
	"time"

	win "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the monitors attached to the desktop (EnumDisplayMonitors,
// GetMonitorInfo), each at its effective DPI, named by its EDID and at
// its current mode's refresh rate. A change is WM_DISPLAYCHANGE,
// WM_SETTINGCHANGE (the work area) or WM_DPICHANGED, which the system
// window hears; the displays are read again then, and again a moment
// later, as some changes settle after the message. Capture is kept out
// with WDA_EXCLUDEFROMCAPTURE on the app's windows, those that show
// later too.

func supported() error { return nil }

// read places the monitors in points: each one's origin is its top left
// corner in pixels over the smallest scale of the monitors, and it goes
// on in its own scale (its size, its work area's place in it), so
// monitors never overlap.
func read() []Display {
	ms := win.Monitors()
	out := make([]Display, 0, len(ms))
	least := 0.0
	for _, m := range ms {
		if s := float64(m.DPI) / 96; least == 0 || s < least {
			least = s
		}
	}
	for _, m := range ms {
		k := float64(m.DPI) / 96
		x, y := float64(m.Bounds.Left)/least, float64(m.Bounds.Top)/least
		out = append(out, Display{
			Id: m.Device, Name: m.Name,
			Bounds: Rect{X: x, Y: y, Width: float64(m.Bounds.W()) / k, Height: float64(m.Bounds.H()) / k},
			WorkArea: Rect{
				X: x + float64(m.Work.Left-m.Bounds.Left)/k, Y: y + float64(m.Work.Top-m.Bounds.Top)/k,
				Width: float64(m.Work.W()) / k, Height: float64(m.Work.H()) / k,
			},
			Scale: k, RefreshRate: float64(m.Hz), Primary: m.Primary,
		})
	}
	return out
}

func follow(changed func()) (stop func()) {
	return win.OnSystemMessage(func(m uint32, _, _ uintptr) {
		switch m {
		case win.WM_DISPLAYCHANGE, win.WM_SETTINGCHANGE, win.WM_DPICHANGED:
			changed()
			time.AfterFunc(250*time.Millisecond, func() { native.Post(changed) })
		}
	})
}

var capture struct {
	on        bool
	stopShown func()
}

func preventCapture(on bool) {
	capture.on = on
	for _, w := range win.ThreadWindows() {
		win.ExcludeFromCapture(w, on)
	}
	if on && capture.stopShown == nil {
		// The windows that show later too (a sheet, a Window).
		capture.stopShown = win.WatchWindowsShown(func(hwnd uintptr) {
			if capture.on && win.TopLevel(hwnd) == hwnd {
				win.ExcludeFromCapture(hwnd, true)
			}
		})
	} else if !on && capture.stopShown != nil {
		capture.stopShown()
		capture.stopShown = nil
	}
}
