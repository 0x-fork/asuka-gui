//go:build !(linux && !android) && !windows

package svg

// Elsewhere (macOS runs the package's Swift) there's no Go icon.
func newView(events ViewEvents) ViewNative { return nil }
