//go:build linux && !android

package preview

import (
	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: the user's app for the file's type (GtkFileLauncher: the OpenURI
// portal in a sandbox), owned by the window the user works in.

func supported() error { return nil }

func open(path string, done func(error)) {
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
		default:
			ErrorFree(gerr)
			done(ErrNoViewer)
		}
	})
}
