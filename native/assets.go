package native

import (
	"os"
	"path/filepath"
	"strings"
	"sync"

	"github.com/egoist/asuka/internal/localfile"
)

var (
	assetsMu  sync.Mutex
	assetsDir = "assets"
)

// SetAssets is for hosts: the directory the app's assets are in.
func SetAssets(dir string) {
	assetsMu.Lock()
	assetsDir = dir
	assetsMu.Unlock()
}

// Asset is a source as the hosts take one (an http(s) or file URL, an
// absolute path, or a file in the app's assets) as a URL (http, https) or
// a path a desktop package's Go can open.
func Asset(source string) string {
	switch {
	case source == "", strings.HasPrefix(source, "http://"), strings.HasPrefix(source, "https://"), strings.HasPrefix(source, "data:"):
		return source
	case strings.HasPrefix(source, "file://"):
		return strings.TrimPrefix(source, "file://")
	case filepath.IsAbs(source):
		return source
	}
	assetsMu.Lock()
	d := assetsDir
	assetsMu.Unlock()
	return filepath.Join(d, filepath.FromSlash(strings.TrimPrefix(source, "./")))
}

// LocalFile is a source's file on this computer: an asset's, a path, or
// a download of an http(s) URL into the app's caches. Not on the main
// thread: a download waits.
func LocalFile(source string) (string, error) {
	assetsMu.Lock()
	d := assetsDir
	assetsMu.Unlock()
	if abs, err := filepath.Abs(d); err == nil {
		d = abs
	}
	return localfile.Resolve(strings.TrimPrefix(source, "file://"), "", d, CacheDir())
}

// CacheDir is the app's caches' directory (the user's cache directory's
// folder of the app's executable's name).
func CacheDir() string {
	return localfile.CacheDir(strings.TrimSuffix(filepath.Base(os.Args[0]), ".exe"))
}

var (
	raiseMu sync.Mutex
	raise   func()
)

// SetRaise is for hosts: how to bring the app's main window to the front
// (shown, if it's hidden).
func SetRaise(f func()) {
	raiseMu.Lock()
	raise = f
	raiseMu.Unlock()
}

// Raise brings the app's main window to the front, where the host can
// (a media player's Raise). Main thread.
func Raise() {
	raiseMu.Lock()
	f := raise
	raiseMu.Unlock()
	if f != nil {
		f()
	}
}
