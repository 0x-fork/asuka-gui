// asuka/preview on Windows and Linux (macOS runs the package's Swift): the
// start file, found (an asset's, a download's) off the main thread, opens
// in the user's app for its type; the call returns once it's handed over.
package preview

import (
	"context"

	"github.com/egoist/asuka/internal/localfile"
	"github.com/egoist/asuka/native"
)

type viewer struct{}

func newViewer() (Viewer, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return viewer{}, nil
}

func (viewer) Show(ctx context.Context, files []string, start int, _ bool) error {
	path, err := localfile.App(files[min(max(start, 0), len(files)-1)], "")
	if err != nil {
		return err
	}
	ch := make(chan error, 1)
	native.Post(func() { open(path, func(err error) { ch <- err }) })
	select {
	case err := <-ch:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}
