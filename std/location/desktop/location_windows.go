//go:build windows

package location

import (
	"errors"
	"slices"

	"github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: WinRT's Geolocator (the user's permission through
// RequestAccessAsync; a desktop app that's allowed has it always, as it
// runs out of sight) and the app's GeofenceMonitor, while the app runs.
// WinRT answers on its own threads: everything is posted back to the
// main thread. Windows has no approximate choice for an app, and no
// geocoder without a map service key: placeName is unsupported.

var win struct {
	geo      *windows.Geolocator
	asked    bool
	allowed  bool
	movers   map[int]func(Position)
	failers  map[int]func(error)
	next     int
	watching bool
	fences   *windows.Geofences
	followed bool
	regions  []Region
	crossed  func(string, Transition, *Position)
}

func locator() *windows.Geolocator {
	if win.geo == nil && windows.WinRTAvailable() {
		if g, err := windows.NewGeolocator(); err == nil {
			win.geo = g
			g.WatchStatus(func(int) { native.Post(authorizationChanged) })
		}
	}
	return win.geo
}

func access() Authorization {
	g := locator()
	if g == nil {
		return Authorization{Level: LevelDenied, Off: true}
	}
	switch g.Status() {
	case windows.PositionDisabled:
		return Authorization{Level: LevelDenied}
	case windows.PositionNotAvailable:
		return Authorization{Level: LevelDenied, Off: true}
	case windows.PositionNotInitialized:
		switch {
		case !win.asked:
			return Authorization{Level: LevelNotAsked}
		case !win.allowed:
			return Authorization{Level: LevelDenied}
		}
	}
	return Authorization{Level: LevelAlways, Precise: true}
}

func requestAccess(done func()) {
	windows.RequestLocation(func(allowed bool, err error) {
		native.Post(func() {
			if err == nil {
				win.asked, win.allowed = true, allowed
			}
			authorizationChanged()
			done()
		})
	})
}

func toPosition(f windows.Fix) Position {
	return Position{Latitude: f.Lat, Longitude: f.Lon, Accuracy: f.Acc, Altitude: f.Alt, Speed: f.Spd, Course: f.Crs, Time: int(f.T * 1e6)}
}

func fix(done func(Position, error)) {
	requestAccess(func() {
		g := locator()
		if !win.allowed || g == nil {
			done(Position{}, ErrDenied)
			return
		}
		g.Current(func(f windows.Fix, err error) {
			native.Post(func() {
				if errors.Is(err, windows.ErrLocationDenied) {
					err = ErrDenied
				}
				done(toPosition(f), err)
			})
		})
	})
}

func watchFixes(moved func(Position), failed func(error)) func() {
	if win.movers == nil {
		win.movers, win.failers = map[int]func(Position){}, map[int]func(error){}
	}
	win.next++
	id := win.next
	win.movers[id], win.failers[id] = moved, failed
	requestAccess(func() {
		g := locator()
		if !win.allowed || g == nil {
			if f, ok := win.failers[id]; ok {
				f(ErrDenied)
			}
			return
		}
		if !win.watching {
			win.watching = true
			g.Watch(func(f windows.Fix) {
				native.Post(func() {
					p := toPosition(f)
					for _, m := range win.movers {
						m(p)
					}
				})
			})
		}
	})
	return func() {
		delete(win.movers, id)
		delete(win.failers, id)
		if len(win.movers) == 0 && win.watching && win.geo != nil {
			win.geo.Stop()
			win.watching = false
		}
	}
}

func geocode(_, _ float64, done func(string, error)) {
	done("", errors.New("unsupported: Windows has no geocoder an app reaches without a map service key"))
}

func openSettings() error { return windows.ShellExecute(0, "", "ms-settings:privacy-location") }

func monitor() *windows.Geofences {
	if win.fences == nil && windows.WinRTAvailable() {
		if g, err := windows.OpenGeofences(); err == nil {
			win.fences = g
		}
	}
	return win.fences
}

func support() Support { return Support{Regions: monitor() != nil} }

func addFence(r Region) error {
	g := monitor()
	if g == nil {
		return ErrUnavailable
	}
	states := 0
	if slices.Contains(r.Events, TransitionEnter) {
		states |= windows.GeofenceEntered
	}
	if slices.Contains(r.Events, TransitionExit) {
		states |= windows.GeofenceExited
	}
	if err := g.Add(r.Id, r.Latitude, r.Longitude, r.Radius, states, 0); err != nil {
		return err
	}
	win.regions = append(slices.DeleteFunc(win.regions, func(x Region) bool { return x.Id == r.Id }), r)
	return nil
}

func removeFence(id string) {
	if g := monitor(); g != nil {
		g.Remove(id)
	}
	if id == "" {
		win.regions = nil
	} else {
		win.regions = slices.DeleteFunc(win.regions, func(x Region) bool { return x.Id == id })
	}
}

func fences() []Region {
	g := monitor()
	if g == nil {
		return nil
	}
	ids := g.IDs()
	var out []Region
	for _, r := range win.regions {
		if slices.Contains(ids, r.Id) {
			out = append(out, r)
		}
	}
	return out
}

func watchFences(crossed func(string, Transition, *Position)) {
	win.crossed = crossed
	g := monitor()
	if g == nil || win.followed {
		return
	}
	win.followed = true
	g.Watch(func(rs []windows.GeofenceReport) {
		native.Post(func() {
			for _, rep := range rs {
				var at *Position
				if rep.At != nil {
					p := toPosition(*rep.At)
					at = &p
				}
				switch rep.State {
				case windows.GeofenceEntered:
					win.crossed(rep.ID, TransitionEnter, at)
				case windows.GeofenceExited:
					win.crossed(rep.ID, TransitionExit, at)
				case windows.GeofenceRemoved:
					win.regions = slices.DeleteFunc(win.regions, func(x Region) bool { return x.Id == rep.ID })
				}
			}
		})
	})
}
