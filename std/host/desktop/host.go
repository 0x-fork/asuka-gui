// asuka/host on the desktop: the host's own modules (host/darwin,
// host/windows, host/linux's CallModule), on the main thread. The Mac's
// is this Go too: its host is Go.
package host

import (
	"context"
	"errors"

	"github.com/egoist/asuka/native"
)

type modules struct{}

func newModules() (Modules, error) {
	if callModule == nil {
		return nil, errors.ErrUnsupported
	}
	return modules{}, nil
}

func (modules) Call(ctx context.Context, module, method string, args []byte) ([]byte, error) {
	type result struct {
		b   []byte
		err error
	}
	ch := make(chan result, 1)
	native.Post(func() {
		callModule(module, method, args, func(b []byte, err error) { ch <- result{b, err} })
	})
	select {
	case r := <-ch:
		return r.b, r.err
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

func (modules) Send(module, method string, args []byte) error {
	native.OnMain(func() { callModule(module, method, args, func([]byte, error) {}) })
	return nil
}
