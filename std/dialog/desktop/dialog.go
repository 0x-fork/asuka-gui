// asuka/dialog on Windows and Linux (macOS runs the package's Swift): the
// system's file dialogs, modal to the window the user works in, shown on
// the main thread (native.Post); the call's goroutine waits for the
// user's answer.
package dialog

import (
	"context"
	"os"
	"path/filepath"

	"github.com/egoist/asuka/native"
)

type dialogs struct{}

func newChooser() (Chooser, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return dialogs{}, nil
}

type chosen struct {
	paths []string
	err   error
}

// show runs start on the main thread and waits for what the user chose,
// or ctx.
func show(ctx context.Context, start func(done func(chosen))) ([]string, error) {
	ch := make(chan chosen, 1)
	native.Post(func() { start(func(c chosen) { ch <- c }) })
	select {
	case c := <-ch:
		if c.paths == nil {
			c.paths = []string{}
		}
		return c.paths, c.err
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

func (dialogs) Open(ctx context.Context, r Request) ([]string, error) {
	return show(ctx, func(done func(chosen)) { fileDialog("open", r, done) })
}

func (dialogs) Save(ctx context.Context, r Request) (*string, error) {
	paths, err := show(ctx, func(done func(chosen)) { fileDialog("save", r, done) })
	if err != nil || len(paths) == 0 {
		return nil, err
	}
	return &paths[0], nil
}

func (dialogs) Folder(ctx context.Context, r Request) ([]string, error) {
	return show(ctx, func(done func(chosen)) { fileDialog("folder", r, done) })
}

// startIn is the folder a dialog starts in, and the name it suggests,
// from a path: a folder, or a file (in a folder that's there).
func startIn(start string, folder bool) (dir, name string) {
	if start == "" {
		return "", ""
	}
	if st, err := os.Stat(start); err == nil && st.IsDir() {
		return start, ""
	}
	if folder {
		return filepath.Dir(start), ""
	}
	return filepath.Dir(start), filepath.Base(start)
}
