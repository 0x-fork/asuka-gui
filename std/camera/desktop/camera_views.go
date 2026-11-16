//go:build linux && !android

package camera

// The package's views in Go on Windows and Linux (macOS runs its Swift).
func init() {
	implementView(newView)
}
