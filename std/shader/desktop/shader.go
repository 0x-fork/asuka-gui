// asuka/shader on Windows (Direct3D 11, shader_windows.go) and Linux
// (OpenGL, or the CPU, shader_linux.go); macOS runs the package's Swift.
// Both decode the program's code here, and the lanes its uniforms set.
package shader

import (
	"math"
	"strings"

	"github.com/egoist/asuka/internal/shadercode"
)

// words are the lanes the app's uniforms set, in c's order: a float's
// bits, an int's or a uint's, a bool's 0 or 1. The bridge's (rectSize,
// pixelScale, Time) are the view's to set.
func words(c *shadercode.Code, us []Uniform) []uint32 {
	w := make([]uint32, c.Lanes)
	for _, u := range us {
		if strings.HasPrefix(u.Name, "__") {
			continue
		}
		for _, v := range c.Uniforms {
			if v.Name != u.Name {
				continue
			}
			for i := 0; i < v.Lanes && i < len(u.Value); i++ {
				at := v.Lane + i
				if at >= len(w) {
					break
				}
				x := u.Value[i]
				switch v.Kind {
				case shadercode.Float:
					w[at] = math.Float32bits(float32(x))
				case shadercode.Int:
					w[at] = uint32(int32(math.Round(x)))
				case shadercode.Uint:
					w[at] = uint32(max(0, math.Round(x)))
				default:
					if x != 0 {
						w[at] = 1
					}
				}
			}
		}
	}
	return w
}
