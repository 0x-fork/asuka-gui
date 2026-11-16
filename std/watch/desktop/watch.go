// asuka/watch on the desktop (macOS, Windows, Linux): internal/fswatch,
// the watcher asuka dev follows its sources with. Changes go to the
// main thread, where changed is called.
package watch

import (
	"context"
	"errors"

	"github.com/egoist/asuka/internal/fswatch"
	"github.com/egoist/asuka/native"
)

type watching struct{}

func newWatching() (Watching, error) { return watching{}, nil }

func (watching) Open(ctx context.Context, path string, ignore []string, recursive, attributes bool) (Watcher, error) {
	opts := fswatch.Options{Recursive: recursive, Attributes: attributes}
	if len(ignore) > 0 {
		opts.Ignore = fswatch.Patterns(ignore...)
	}
	// The watch outlives this call: it stops as it's closed.
	wctx, cancel := context.WithCancel(context.Background())
	w, err := fswatch.New(wctx, path, opts)
	if err != nil {
		cancel()
		return nil, err
	}
	return &watcher{w: w, cancel: cancel}, nil
}

type watcher struct {
	w      *fswatch.Watcher
	cancel context.CancelFunc
}

func (w *watcher) Root() (string, error) { return w.w.Root(), nil }

func (w *watcher) Follow(ctx context.Context, changed func([]Event)) error {
	for {
		evs, err := w.w.Next(ctx)
		if err != nil {
			if ctx.Err() != nil {
				return ctx.Err()
			}
			// Closed, or its directory went: the stream ends.
			return nil
		}
		out := make([]Event, len(evs))
		for i, e := range evs {
			out[i] = Event{Op: op(e.Op), Path: e.Path, OldPath: e.OldPath, IsDir: e.IsDir}
		}
		native.Post(func() { changed(out) })
	}
}

func (w *watcher) Close() error {
	w.cancel()
	return w.w.Close()
}

func op(o fswatch.Op) Op {
	switch o {
	case fswatch.Create:
		return OpCreate
	case fswatch.Write:
		return OpWrite
	case fswatch.Remove:
		return OpRemove
	case fswatch.Rename:
		return OpRename
	case fswatch.Attrib:
		return OpAttrib
	}
	return OpOverflow
}

// A Watcher is made by Watching's open, not on its own.
func newWatcher() (Watcher, error) { return nil, errors.ErrUnsupported }
