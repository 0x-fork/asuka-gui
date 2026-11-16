//go:build windows || (linux && !android)

package shader

// The package's views in Go on Windows and Linux (macOS runs its Swift).
func init() {
	implementShader(newShader)
}
