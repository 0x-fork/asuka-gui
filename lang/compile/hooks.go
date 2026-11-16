package compile

import (
	"github.com/egoist/asuka/internal/shadercode"
	"github.com/egoist/asuka/internal/svgparse"
	"github.com/egoist/asuka/lang/ir"
)

// The standard library's structs the compiler makes from a constant
// (lang/ir's StructHooks): what an icon draws, from its markup, parsed
// once, here, rather than on the device each launch; a shader's code for
// every platform, from its Kage source.
func init() {
	ir.StructHooks["std/svg.Icon"] = func(src string) (map[string]any, error) {
		ic, err := svgparse.Parse([]byte(src))
		if err != nil {
			return nil, err
		}
		return map[string]any{"data": ic.Encode(), "width": float64(ic.Width), "height": float64(ic.Height)}, nil
	}
	ir.StructHooks["std/shader.Program"] = func(src string) (map[string]any, error) {
		c, err := shadercode.Compile(src)
		if err != nil {
			return nil, err
		}
		return map[string]any{"code": c.Encode(), "glsl": c.GLSL}, nil
	}
}
