//go:build windows || (linux && !android)

package markdown

// The package's views in Go on Windows and Linux (macOS runs its Swift).
func init() {
	implementDocument(newDocument)
}
