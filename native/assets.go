package native

import (
	"path/filepath"
	"strings"
	"sync"
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
