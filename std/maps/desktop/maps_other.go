//go:build !(linux && !android)

package maps

// Elsewhere (macOS runs the package's Swift) there's no Go map.
func newView(events ViewEvents) ViewNative { return nil }
