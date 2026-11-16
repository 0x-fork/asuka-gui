package native

import "sync"

// A Color crosses a native declaration as an int, the core's encoding:
// its high 32 bits 2 for RGBA (the low 32, 0xRRGGBBAA), 1 for a semantic
// color (its index in the low 16, its transparency in bits 24-31), 0 for
// none (the platform's default).

var (
	colorMu  sync.Mutex
	resolver func(c uint64) ([4]float32, bool)
)

// SetColorResolver is for hosts: how a color resolves on them, a
// semantic one as their theme's (light or dark) and the app's accent.
func SetColorResolver(f func(c uint64) ([4]float32, bool)) {
	colorMu.Lock()
	resolver = f
	colorMu.Unlock()
}

// ResolveColor is a Color a native declaration took, as RGBA from 0 to
// 1: an RGBA one as it is, a semantic one as the host's theme has it now
// (on the main thread). ok is false for none, or a semantic color with no
// host to resolve it.
func ResolveColor(c int) (rgba [4]float32, ok bool) {
	u := uint64(c)
	if u>>32 == 2 {
		v := uint32(u)
		return [4]float32{float32(v>>24) / 255, float32(v>>16&255) / 255, float32(v>>8&255) / 255, float32(v&255) / 255}, true
	}
	if u == 0 {
		return rgba, false
	}
	colorMu.Lock()
	f := resolver
	colorMu.Unlock()
	if f == nil {
		return rgba, false
	}
	return f(u)
}
