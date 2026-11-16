// asuka/location on Windows and Linux (macOS runs the package's Swift).
// Windows: WinRT's Geolocator and GeofenceMonitor; Linux: the location
// portal, else GeoClue2 (no geofences: the package computes the regions
// from the positions). A desktop app runs out of sight, so its
// background updates are positions it watches while it runs, each a
// batch. Everything the platform touches runs on the main thread
// (native.Post, native.OnMain), where the callbacks are called.
package location

import (
	"context"
	"errors"
	"time"

	"github.com/egoist/asuka/native"
)

type desktop struct{}

func newLocator() (Locator, error) { return desktop{}, nil }

// The state, on the main thread.
var st struct {
	authReaders  map[int]func(Authorization)
	eventReaders map[int]func(Event)
	next         int
	updates      *Updates
	stopUpdates  func()
	nextEvent    int
}

func add(m *map[int]func(Authorization), f func(Authorization)) int {
	if *m == nil {
		*m = map[int]func(Authorization){}
	}
	st.next++
	(*m)[st.next] = f
	return st.next
}

// authorizationChanged tells the followers; main thread.
func authorizationChanged() {
	a := access()
	for _, f := range st.authReaders {
		f(a)
	}
}

func (desktop) Authorization() (a Authorization, err error) {
	native.OnMain(func() { a = access() })
	return a, nil
}

func (desktop) WatchAuthorization(ctx context.Context, changed func(Authorization)) error {
	var id int
	native.OnMain(func() {
		id = add(&st.authReaders, changed)
		changed(access())
	})
	<-ctx.Done()
	native.Post(func() { delete(st.authReaders, id) })
	return ctx.Err()
}

func (d desktop) Request(ctx context.Context, always bool) (Authorization, error) {
	done := make(chan struct{})
	native.Post(func() { requestAccess(func() { close(done) }) })
	select {
	case <-done:
	case <-ctx.Done():
		return Authorization{}, ctx.Err()
	}
	return d.Authorization()
}

func (d desktop) RequestPrecise(ctx context.Context, _ string) (Authorization, error) {
	return d.Authorization()
}

func (desktop) Current(ctx context.Context) (Position, error) {
	type answer struct {
		p   Position
		err error
	}
	c := make(chan answer, 1)
	native.Post(func() { fix(func(p Position, err error) { c <- answer{p, err} }) })
	select {
	case a := <-c:
		return a.p, a.err
	case <-ctx.Done():
		return Position{}, ctx.Err()
	}
}

func (desktop) Watch(ctx context.Context, moved func(Position)) error {
	failed := make(chan error, 1)
	var stop func()
	native.OnMain(func() {
		stop = watchFixes(moved, func(err error) {
			select {
			case failed <- err:
			default:
			}
		})
	})
	defer native.Post(stop)
	select {
	case err := <-failed:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (desktop) PlaceName(ctx context.Context, lat, lon float64) (string, error) {
	type answer struct {
		s   string
		err error
	}
	c := make(chan answer, 1)
	native.Post(func() { geocode(lat, lon, func(s string, err error) { c <- answer{s, err} }) })
	select {
	case a := <-c:
		return a.s, a.err
	case <-ctx.Done():
		return "", ctx.Err()
	}
}

func (desktop) OpenSettings() error { return openSettings() }

func (desktop) Support() (s Support, err error) {
	native.OnMain(func() { s = support() })
	return s, nil
}

// event gives the readers an event, made on the main thread.
func event(e Event) {
	st.nextEvent++
	e.Id = st.nextEvent
	for _, f := range st.eventReaders {
		f(e)
	}
}

func (d desktop) StartUpdates(ctx context.Context, o Updates) error {
	a, err := d.Request(ctx, false)
	if err != nil {
		return err
	}
	if a.Level != LevelWhenInUse && a.Level != LevelAlways {
		return ErrDenied
	}
	native.OnMain(func() {
		if st.stopUpdates != nil {
			st.stopUpdates()
		}
		st.updates = &o
		st.stopUpdates = watchFixes(func(p Position) { event(Event{Positions: []Position{p}}) }, func(error) {})
	})
	return nil
}

func (desktop) StopUpdates() error {
	native.Post(func() {
		if st.stopUpdates != nil {
			st.stopUpdates()
		}
		st.stopUpdates, st.updates = nil, nil
	})
	return nil
}

func (desktop) Updating(context.Context) (on bool, err error) {
	native.OnMain(func() { on = st.updates != nil })
	return on, nil
}

func (desktop) Monitor(ctx context.Context, r Region) (err error) {
	native.OnMain(func() { err = addFence(r) })
	return err
}

func (desktop) StopMonitoring(ctx context.Context, id string) error {
	if id == "" {
		return nil
	}
	native.OnMain(func() { removeFence(id) })
	return nil
}

func (desktop) StopMonitoringAll(context.Context) error {
	native.OnMain(func() { removeFence("") })
	return nil
}

func (desktop) Monitored(context.Context) (rs []Region, err error) {
	native.OnMain(func() { rs = fences() })
	return rs, nil
}

func (desktop) Events(ctx context.Context, f func(Event)) error {
	var id int
	native.OnMain(func() {
		if st.eventReaders == nil {
			st.eventReaders = map[int]func(Event){}
		}
		st.next++
		id = st.next
		st.eventReaders[id] = f
		watchFences(func(region string, kind Transition, at *Position) {
			e := Event{Crossing: true, Region: region, Kind: kind, Time: int(time.Now().UnixNano())}
			if at != nil {
				e.At = *at
			}
			event(e)
		})
	})
	<-ctx.Done()
	native.Post(func() { delete(st.eventReaders, id) })
	return ctx.Err()
}

// A desktop app isn't put to sleep for its events.
func (desktop) Hold(int) error { return nil }
func (desktop) Done(int) error { return nil }

var errUnsupported = errors.New("unsupported: not on this platform")
