// Package cachedir is where Asuka keeps what it builds and downloads:
// ASUKA_CACHE if set, else <the user's cache directory>/asuka.
package cachedir

import (
	"os"
	"path/filepath"
)

// Root is Asuka's cache directory: ASUKA_CACHE, else the user's cache
// directory's asuka (~/Library/Caches/asuka, ~/.cache/asuka,
// %LocalAppData%\asuka), else the temporary directory's.
func Root() string {
	if d := os.Getenv("ASUKA_CACHE"); d != "" {
		if abs, err := filepath.Abs(d); err == nil {
			return abs
		}
		return d
	}
	d, err := os.UserCacheDir()
	if err != nil {
		d = os.TempDir()
	}
	return filepath.Join(d, "asuka")
}

// Path is a path under Root.
func Path(elem ...string) string {
	return filepath.Join(append([]string{Root()}, elem...)...)
}
