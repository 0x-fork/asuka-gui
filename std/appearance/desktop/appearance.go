// asuka/appearance on Windows and Linux (macOS runs the package's Swift).
// The platform is touched on the main thread (native.OnMain, native.Post),
// where changed is called too.
package appearance

import (
	"context"

	"github.com/egoist/asuka/native"
)

type system struct{}

func newSystem() (System, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return system{}, nil
}

func (system) Current() (s Settings, err error) {
	native.OnMain(func() { s = settings() })
	return s, nil
}

func (system) Watch(ctx context.Context, changed func(Settings)) error {
	var stop func()
	native.OnMain(func() {
		var last Settings
		first := true
		report := func() {
			s := settings()
			if !first && s == last {
				return
			}
			first, last = false, s
			changed(s)
		}
		stop = follow(report)
		report()
	})
	<-ctx.Done()
	native.Post(stop)
	return ctx.Err()
}
