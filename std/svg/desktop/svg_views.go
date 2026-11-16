//go:build windows || (linux && !android)

package svg

// The package's views in Go on Windows and Linux (macOS runs its Swift).
func init() {
	implementView(newView)
	implementShape(newShape)
}
