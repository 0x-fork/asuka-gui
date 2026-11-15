// Package watch follows changes to files and directories: a folder the
// app shows, a project open in an editor, a settings file another
// program writes, a document saved by another app.
//
// In a component, Path watches a path while the component lives and
// calls a function on the main thread with each batch of changes:
//
//	func Folder(dir signal.Accessor[string]) ui.Node {
//		names := signal.NewFunc[[]string](nil, slices.Equal)
//		reload := func() {
//			d := signal.Untrack(dir.Get)
//			go func() {
//				list := readNames(d) // os.ReadDir, off the main thread
//				ui.Post(func() { names.Set(list) })
//			}()
//		}
//		signal.Effect(func() { dir.Get(); reload() }) // and as dir changes
//		w := watch.Path(dir, watch.Options{Ignore: watch.Patterns(".*")}, func([]watch.Event) {
//			reload() // read the folder again
//		})
//		return ui.Column(nil,
//			ui.Show(signal.Func(func() bool { return w.Err() != nil }), func() ui.Node {
//				return ui.Text(signal.Func(func() string { return w.Err().Error() }))
//			}),
//			ui.For(names, func(n string) string { return n }, func(n signal.Accessor[string], _ signal.Accessor[int]) ui.Node {
//				return ui.Text(n)
//			}),
//		)
//	}
//
// Elsewhere, New watches a path from any goroutine and Next returns its
// changes, until Close:
//
//	w, err := watch.New(ctx, dir, watch.Options{Recursive: true})
//	if err != nil {
//		return err
//	}
//	defer w.Close()
//	for {
//		events, err := w.Next(ctx)
//		if err != nil {
//			return err // io.EOF once closed, or once dir went away
//		}
//		for _, e := range events {
//			log.Println(e.Op, e.Path)
//		}
//	}
//
// A watch keeps what it saw of the files (their identity, size,
// modification time and permissions) and, each time the system says
// something changed, looks there again and compares: the events are the
// differences, not every step that made them. A file written many times
// in a row is one Write; one created and removed in between is nothing; a
// file replaced at its path by another, as editors and atomic saves do (a
// temporary file renamed over it), is a Write, and the watch follows the
// new file; a file or directory that moved within the watched directory
// is a Rename with its old and new path, paired by its identity (device
// and inode; volume and file ID on Windows), on every platform. Treat
// events as hints to read the files again.
//
// Each platform's own way, all without cgo:
//
//   - macOS: FSEvents (CoreServices, through purego), with file-level
//     events: one stream covers a whole tree, so a watch costs no file
//     descriptor per file and a large folder is fine.
//   - iOS: kqueue (EVFILT_VNODE), a descriptor per directory and file
//     watched, within the app's sandbox.
//   - Linux and Android: inotify, a watch descriptor per directory
//     (fs.inotify.max_user_watches), through one inotify instance for
//     the app; new directories are watched as they appear.
//   - Windows: ReadDirectoryChangesW with overlapped I/O on a completion
//     port: one handle covers a recursive watch's whole tree, so its
//     directories can still be renamed, and the root's parent is watched
//     too, to know when the root is renamed.
//   - The web: the app's files are the web host's file system in the
//     page's memory (platform/web/src/fs.js), which says what the app's
//     Go code changes in it. Under Node (go test), Node's fs.watch.
//
// When the system drops changes (inotify's queue overflowed, FSEvents
// says to rescan, ReadDirectoryChangesW's buffer was too small), the
// watch looks at everything again and the batch starts with an Overflow
// event: the other events of the batch are what it found.
//
// Symlinks are reported as themselves (created, removed, renamed),
// never followed: a watch doesn't leave its directory. The path given is
// resolved first (macOS's /tmp is /private/tmp), and Root says what it
// resolved to.
package watch

import (
	"context"

	"github.com/egoist/asuka/internal/fswatch"
)

// The watcher is internal/fswatch's, which asuka dev watches sources with.
type (
	Op      = fswatch.Op
	Event   = fswatch.Event
	Options = fswatch.Options
	Watcher = fswatch.Watcher
)

const (
	Create   = fswatch.Create
	Write    = fswatch.Write
	Remove   = fswatch.Remove
	Rename   = fswatch.Rename
	Attrib   = fswatch.Attrib
	Overflow = fswatch.Overflow
)

var (
	ErrLimit       = fswatch.ErrLimit
	ErrUnsupported = fswatch.ErrUnsupported
)

// New watches the file or directory at path (fswatch.New).
func New(ctx context.Context, path string, opts Options) (*Watcher, error) {
	return fswatch.New(ctx, path, opts)
}

// Patterns is an Ignore of gitignore-style patterns (fswatch.Patterns).
func Patterns(patterns ...string) func(path string, isDir bool) bool {
	return fswatch.Patterns(patterns...)
}
