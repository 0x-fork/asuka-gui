// asuka/js on Windows and Linux (macOS runs the package's Swift): the
// platform's web view, never shown, made for each script on the main
// thread, where its answer comes; the goroutine waits for it, the
// timeout or the call's cancellation.
package js

import (
	"context"
	"time"

	"github.com/egoist/asuka/internal/localfile"
	"github.com/egoist/asuka/native"
)

type engine struct{}

func newEngine() (Engine, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return engine{}, nil
}

type answer struct {
	json string
	err  error
}

func (engine) Run(ctx context.Context, script string, timeout time.Duration) (string, error) {
	ch := make(chan answer, 1)
	var stop func()
	native.OnMain(func() {
		stop = start(script, func(b []byte, err error) { ch <- answer{string(b), err} })
	})
	t := time.NewTimer(timeout)
	defer t.Stop()
	select {
	case a := <-ch:
		return a.json, a.err
	case <-t.C:
		native.Post(stop)
		return "", ErrTimeout
	case <-ctx.Done():
		native.Post(stop)
		return "", ctx.Err()
	}
}

// appName is the app's name (asuka.toml's), else its executable's.
func appName() string { return localfile.AppName() }
