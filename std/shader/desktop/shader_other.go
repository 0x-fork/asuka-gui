//go:build !(linux && !android)

package shader

// Elsewhere (macOS runs the package's Swift) there's no Go shader.
func newShader(events ShaderEvents) ShaderNative { return nil }
