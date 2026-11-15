// asuka/network on Windows and Linux (macOS runs the package's Swift).
// The platform's monitor is touched on the main thread (native.Post),
// where changed is called too.
package network

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
	native.OnMain(func() { s, err = state() })
	return s, err
}

func (monitor) Watch(ctx context.Context, changed func(Status)) error {
	var stop func()
	var err error
	native.OnMain(func() {
		var last Status
		first := true
		report := func() {
			s, e := state()
			if e != nil || (!first && s == last) {
				return
			}
			first, last = false, s
			changed(s)
		}
		stop, err = follow(report)
		if err == nil {
			report()
		}
	})
	if err != nil {
		return err
	}
	<-ctx.Done()
	native.Post(stop)
	return ctx.Err()
}
