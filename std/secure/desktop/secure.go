// asuka/secure on Windows and Linux (macOS runs the package's Swift): the
// platform's own store of secrets, by the app's name.
package secure

import (
	"os"
	"path/filepath"
	"strings"

	"github.com/egoist/asuka/internal/boot"
)

type keychain struct{}

func newKeychain() (Keychain, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return keychain{}, nil
}

// appName is the app's name (asuka.toml's), else its executable's.
func appName() string {
	if boot.Name != "" {
		return boot.Name
	}
	exe, err := os.Executable()
	if err != nil {
		return "asuka"
	}
	return strings.TrimSuffix(strings.TrimSuffix(filepath.Base(exe), ".exe"), ".EXE")
}
