// asuka/secure on Windows and Linux (macOS runs the package's Swift): the
// platform's own store of secrets, by the app's name.
package secure

import "github.com/egoist/asuka/internal/localfile"

type keychain struct{}

func newKeychain() (Keychain, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return keychain{}, nil
}

// appName is the app's name (asuka.toml's), else its executable's.
func appName() string { return localfile.AppName() }
