// asuka/sensors on Windows and Linux (macOS runs the package's Swift,
// which has no sensors to read there). Linux reads iio-sensor-proxy
// (sensors_linux.go); Windows apps that aren't packaged have no sensor
// API to read without WinRT's activation: not available there.
package sensors

import (
	"context"
	"errors"
	"time"

	"github.com/egoist/asuka/native"
)

type desktop struct{}

func newSensors() (Sensors, error) {
	native.Post(prime)
	return desktop{}, nil
}

func (desktop) Available(s Sensor) (ok bool, err error) {
	native.OnMain(func() { ok = has(s) })
	return ok, nil
}

func (desktop) Watch(ctx context.Context, s Sensor, every time.Duration, reading func(Sample)) error {
	props := make(chan map[string]any, 1)
	native.Post(func() { readProps(func(p map[string]any) { props <- p }) })
	var p map[string]any
	select {
	case p = <-props:
	case <-ctx.Done():
		return ctx.Err()
	}
	var stop func()
	var err error
	native.OnMain(func() {
		var last time.Duration = -1
		stop, err = follow(s, p, func(v []float64, at time.Duration) {
			if last >= 0 && at-last < every*9/10 {
				return
			}
			last = at
			reading(Sample{Values: v, Time: at})
		})
	})
	if err != nil {
		return err
	}
	<-ctx.Done()
	native.Post(stop)
	return ctx.Err()
}

func (desktop) Steps(context.Context, int, int) (Steps, error) { return Steps{}, ErrNotAvailable }

func (desktop) Simulate(context.Context, string) error {
	return errors.New("unsupported: only on the iOS simulator")
}
