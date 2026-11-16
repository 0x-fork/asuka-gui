//go:build !(linux && !android)

package bluetooth

// Elsewhere (macOS runs the package's Swift) there's no Go adapter.
func newAdapter() (Adapter, error) { return nil, ErrNotAvailable }
