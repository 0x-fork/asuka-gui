//go:build !(linux && !android)

package markdown

// Elsewhere (macOS runs the package's Swift) there's no Go document.
func newDocument(events DocumentEvents) DocumentNative { return nil }
