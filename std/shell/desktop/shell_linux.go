//go:build linux && !android

package shell

import (
	"context"
	"path/filepath"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: the file manager's org.freedesktop.FileManager1 (Files, Dolphin,
// Nemo, Thunar...) and GtkFileLauncher (the OpenURI portal in a
// sandbox), GIO's trash, the display's beep. Each starts on the main
// thread, where GLib answers, and the goroutine waits for it.

func supported() error { return nil }

// wait runs start on the main thread and waits for the error it gives
// done, or ctx.
func wait(ctx context.Context, start func(done func(error))) error {
	ch := make(chan error, 1)
	native.Post(func() { start(func(err error) { ch <- err }) })
	select {
	case err := <-ch:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (desktop) Reveal(ctx context.Context, path string) error {
	if err := there(path, true); err != nil {
		return err
	}
	abs, _ := filepath.Abs(path)
	return wait(ctx, func(done func(error)) {
		f := Sys.FileNewForPath(abs)
		uri := GoStringFree(Sys.FileGetURI(f))
		ObjectUnref(f)
		inFolder := func() { launch(filepath.Dir(abs), done) }
		conn, err := Bus(BusSession)
		if err != nil {
			inFolder()
			return
		}
		DBusCall(conn, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", "ShowItems",
			VTuple(VStrings([]string{uri}), VStr("")), 5000, func(_ Ptr, err error) {
				if err != nil {
					inFolder()
					return
				}
				done(nil)
			})
	})
}

func (desktop) Open(ctx context.Context, path string) error {
	if err := there(path, false); err != nil {
		return err
	}
	return wait(ctx, func(done func(error)) { launch(path, done) })
}

func (desktop) Trash(ctx context.Context, path string) error {
	if err := there(path, true); err != nil {
		return err
	}
	return wait(ctx, func(done func(error)) {
		f := Sys.FileNewForPath(path)
		Async(func(a AsyncCall) { Dialogs.FileTrashAsync(f, 0, 0, a.Callback, a.Data) }, func(_, res Ptr) {
			var gerr Ptr
			ok := Dialogs.FileTrashFinish(f, res, &gerr)
			ObjectUnref(f)
			if ok {
				done(nil)
				return
			}
			done(GError(gerr))
		})
	})
}

func (desktop) Beep() error {
	Dialogs.DisplayBeep(DisplayGetDefault())
	return nil
}

// launch opens path in its app (a folder in the file manager), owned by
// the window the user works in: ErrNoApp where none opens its type.
func launch(path string, done func(error)) {
	file := Sys.FileNewForPath(path)
	l := Sys.FileLauncherNew(file)
	ObjectUnref(file)
	Async(func(a AsyncCall) { Sys.FileLauncherLaunch(l, native.Window(), 0, a.Callback, a.Data) }, func(_, res Ptr) {
		var gerr Ptr
		ok := Sys.FileLauncherLaunchFinish(l, res, &gerr)
		ObjectUnref(l)
		switch {
		case ok:
			done(nil)
		case Cancelled(gerr): // the portal's app chooser closed
			ErrorFree(gerr)
			done(nil)
		case notSupported(gerr):
			ErrorFree(gerr)
			done(ErrNoApp)
		default:
			done(GError(gerr))
		}
	})
}

// notSupported reports whether a GError is G_IO_ERROR_NOT_SUPPORTED:
// GIO's "no application is registered" for a type.
func notSupported(gerr Ptr) bool {
	d, c := GErrorCode(gerr)
	return gerr != 0 && d == uint32(Call(Sym("g_io_error_quark"))) && c == 15
}
