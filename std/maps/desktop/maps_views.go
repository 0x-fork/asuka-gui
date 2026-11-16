//go:build linux && !android

package maps

// The package's views in Go on Windows and Linux (macOS runs its Swift).
func init() {
	implementView(newView)
}
