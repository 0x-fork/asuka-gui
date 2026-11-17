package main

import (
	"context"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/fswatch"
)

// watcher notices changes to an app: its sources (.asuka), which asuka
// dev compiles and sends to the players, and the rest of it (asuka.toml,
// its assets, its native packages' code and projects, its catalogs),
// which a player is built and launched again for. The system tells it
// when something changed under its roots (internal/fswatch: FSEvents,
// inotify, ReadDirectoryChangesW), and wake receives; changed then
// compares the files' modification times with what it saw last, which
// decides.
type watcher struct {
	roots          []string
	lastSrc, lastP string
	// wake receives when something under the roots may have changed: a
	// value may stand for many changes.
	wake <-chan struct{}
	// embedded reports whether the program embeds a file (std/embed's,
	// by its absolute path): a change to it is a change to the sources,
	// whatever its name. embedKey says which set it is.
	embedded atomic.Pointer[func(string) bool]
	embedKey string
}

// watchEmbeds tells w what the program embeds, as u (its last compile's)
// says.
func watchEmbeds(w *watcher, u *appbuild.Uses) {
	if u == nil {
		return
	}
	var key strings.Builder
	for _, p := range u.Embeds {
		key.WriteString(p.Dir + "\x00" + p.Pattern + "\n")
	}
	w.setEmbedded(key.String(), u.Embedded)
}

// isEmbedded reports whether the program embeds the file at path.
func (w *watcher) isEmbedded(path string) bool {
	f := w.embedded.Load()
	return f != nil && (*f)(path)
}

// setEmbedded says what the program embeds, as its last compile found:
// key names the set (when it's the same, nothing changes), embedded
// matches a file's path. The files it names count as sources from now
// on: what they are now is the starting point.
func (w *watcher) setEmbedded(key string, embedded func(string) bool) {
	if key == w.embedKey {
		return
	}
	w.embedKey = key
	w.embedded.Store(&embedded)
	w.lastSrc, w.lastP = w.scan()
}

// newWatcher watches roots (and what's in them). Its loop waits on wake
// and asks changed.
func newWatcher(roots ...string) *watcher {
	roots = slices.DeleteFunc(slices.Clone(roots), func(r string) bool { return r == "" })
	w := &watcher{roots: roots}
	w.wake = newWakeup(roots, w.isEmbedded).c // before the first scan, so that no change falls between
	w.lastSrc, w.lastP = w.scan()
	return w
}

// skipDir reports whether a directory named name, under a root, is left
// out: hidden ones, testdata, build outputs and node_modules.
func skipDir(name string) bool {
	return strings.HasPrefix(name, ".") || name == "testdata" || name == "build" || name == "dist" || name == "node_modules"
}

// sourceFile reports whether the file named name is Asuka source.
func sourceFile(name string) bool { return strings.HasSuffix(name, ".asuka") }

// projectFile reports whether a change to the file at path named name
// needs the player built again: native packages' code and projects
// (Swift, Kotlin, their Gradle files, manifests, Info.plist and
// entitlements, JavaScript), C ([c]'s sources and headers), the app's
// assets, its asuka.toml and asuka.lock, and the language catalogs.
func projectFile(path, name string) bool {
	code := strings.HasSuffix(name, ".swift") || strings.HasSuffix(name, ".kt") || strings.HasSuffix(name, ".kts") ||
		strings.HasSuffix(name, ".c") || strings.HasSuffix(name, ".h") || strings.HasSuffix(name, ".inc") ||
		name == "AndroidManifest.xml" || name == "Info.plist" || strings.HasSuffix(name, ".entitlements") || strings.HasSuffix(name, ".js")
	asset := strings.Contains(filepath.ToSlash(path), "/assets/")
	return code || asset || name == configFile || name == "asuka.lock" || strings.HasSuffix(name, ".json")
}

// watchedFile reports, quickly, whether the system should say when the
// file at path (slashes) named name changes.
func watchedFile(path, name string) bool {
	return sourceFile(name) || projectFile(path, name)
}

// scan is the modification times of the sources, and of the rest.
func (w *watcher) scan() (src, proj string) {
	var bs, bp strings.Builder
	for _, root := range w.roots {
		filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return nil
			}
			name := d.Name()
			if d.IsDir() {
				if path != root && skipDir(name) {
					return filepath.SkipDir
				}
				return nil
			}
			// An embedded file is a source, whatever its name; one under
			// assets/ is the platform's too, which a player is given again.
			embedded := w.isEmbedded(path)
			asset := embedded && strings.Contains(filepath.ToSlash(path), "/assets/")
			var bufs []*strings.Builder
			switch {
			case sourceFile(name) || embedded:
				bufs = append(bufs, &bs)
				if asset {
					bufs = append(bufs, &bp)
				}
			case projectFile(path, name):
				bufs = append(bufs, &bp)
			default:
				return nil
			}
			if info, err := d.Info(); err == nil {
				for _, b := range bufs {
					b.WriteString(path)
					b.WriteString(info.ModTime().Format(time.RFC3339Nano))
				}
			}
			return nil
		})
	}
	return bs.String(), bp.String()
}

// changed reports what changed since it last looked: the sources, the
// rest.
func (w *watcher) changed() (src, proj bool) {
	s, p := w.scan()
	src, proj = s != w.lastSrc, p != w.lastP
	w.lastSrc, w.lastP = s, p
	return src, proj
}

// wakeup turns the system's reports of changes under its roots into
// wakes on c, at most one waiting. Where the system can't watch them (no
// way to on the platform, too many files, a root that went away), it
// polls instead.
type wakeup struct {
	c       chan struct{}
	polling sync.Once
}

const (
	// wakeDebounce is how long a burst of changes (a save of several
	// files, a formatter rewriting one after the editor) may take: they
	// wake once.
	wakeDebounce = 25 * time.Millisecond
	// pollEvery is how often a wakeup that can't watch polls.
	pollEvery = 300 * time.Millisecond
)

func newWakeup(roots []string, embedded func(string) bool) *wakeup {
	u := &wakeup{c: make(chan struct{}, 1)}
	var ws []*fswatch.Watcher
	for _, root := range outermost(roots) {
		if _, err := os.Stat(root); err != nil {
			continue // a native package's directory that isn't there
		}
		w, err := fswatch.New(context.Background(), root, fswatch.Options{
			Recursive: true,
			Ignore: func(path string, isDir bool) bool {
				name := path[strings.LastIndexByte(path, '/')+1:]
				if isDir {
					return skipDir(name)
				}
				return !watchedFile("/"+path, name) && !embedded(filepath.Join(root, filepath.FromSlash(path)))
			},
			Debounce:   wakeDebounce,
			MaxEntries: 1 << 18,
		})
		if err != nil {
			for _, w := range ws {
				w.Close()
			}
			u.poll(root, err)
			return u
		}
		ws = append(ws, w)
	}
	for _, w := range ws {
		go u.follow(w)
	}
	return u
}

func (u *wakeup) follow(w *fswatch.Watcher) {
	for {
		if _, err := w.Next(context.Background()); err != nil {
			u.poll(w.Root(), err)
			return
		}
		select {
		case u.c <- struct{}{}:
		default: // one is waiting already
		}
	}
}

func (u *wakeup) poll(root string, err error) {
	u.polling.Do(func() {
		fmt.Fprintf(os.Stderr, "asuka: looking for changes every %v: can't watch %s: %v\n", pollEvery, root, err)
		go func() {
			for range time.Tick(pollEvery) {
				select {
				case u.c <- struct{}{}:
				default:
				}
			}
		}()
	})
}

// outermost is roots without those inside another.
func outermost(roots []string) []string {
	var abs []string
	for _, r := range roots {
		if a, err := filepath.Abs(r); err == nil {
			abs = append(abs, filepath.Clean(a))
		}
	}
	slices.Sort(abs)
	abs = slices.Compact(abs)
	var out []string
	for _, r := range abs {
		if len(out) > 0 {
			last := out[len(out)-1]
			if r == last || strings.HasPrefix(r, last+string(filepath.Separator)) {
				continue
			}
		}
		out = append(out, r)
	}
	return out
}
