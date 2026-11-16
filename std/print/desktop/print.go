// asuka/print on Windows and Linux (macOS runs the package's Swift). A
// call's goroutine finds the file (an asset's, a download's) and waits;
// the platform's dialogs and pages are on the main thread (native.Post),
// where their answers come.
package print

import (
	"context"

	"github.com/egoist/asuka/internal/localfile"
	"github.com/egoist/asuka/native"
)

type printer struct{}

func newPrinter() (Printer, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return printer{}, nil
}

type answer[T any] struct {
	v   T
	err error
}

// wait runs start on the main thread and waits for its answer, or ctx.
func wait[T any](ctx context.Context, start func(done func(T, error))) (T, error) {
	ch := make(chan answer[T], 1)
	native.Post(func() { start(func(v T, err error) { ch <- answer[T]{v, err} }) })
	select {
	case a := <-ch:
		return a.v, a.err
	case <-ctx.Done():
		var zero T
		return zero, ctx.Err()
	}
}

// jobName is a job's name: the app's, else fallback (a file's name, a
// page's title), else the app's name.
func jobName(j Job, fallback string) string {
	switch {
	case j.Name != "":
		return j.Name
	case fallback != "":
		return fallback
	}
	return localfile.AppName()
}
