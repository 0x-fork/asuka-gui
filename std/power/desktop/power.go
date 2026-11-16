// asuka/power on Windows and Linux (macOS runs the package's Swift). The
// platform is touched on the main thread (native.OnMain, native.Post),
// where changed and happened are called too.
package power

import (
	"context"

	"github.com/egoist/asuka/native"
)

type monitor struct{}

func newMonitor() (Monitor, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return monitor{}, nil
}

func (monitor) Current() (s Status, err error) {
	native.OnMain(func() { s = state() })
	return s, nil
}

func (monitor) Watch(ctx context.Context, changed func(Status)) error {
	var stop func()
	native.OnMain(func() {
		var last Status
		first := true
		report := func() {
			s := state()
			if !first && s == last {
				return
			}
			first, last = false, s
			changed(s)
		}
		stop = follow(report, nil)
		report()
	})
	<-ctx.Done()
	native.Post(stop)
	return ctx.Err()
}

func (monitor) Events(ctx context.Context, happened func(Event)) error {
	var stop func()
	native.OnMain(func() { stop = follow(nil, happened) })
	<-ctx.Done()
	native.Post(stop)
	return ctx.Err()
}

// unknown is a status without a battery.
var unknown = Status{Level: -1}
