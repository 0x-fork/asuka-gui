package compile

import (
	"github.com/egoist/asuka/internal/shadercode"
	"github.com/egoist/asuka/lang/ir"
	"github.com/egoist/asuka/sdk/svg"
)

// The standard library's structs the compiler makes from a constant
// (lang/ir's StructHooks): what an icon draws, from its markup, parsed
// once, here, rather than on the device each launch; a shader's code for
// every platform, from its Kage source.
func init() {
	ir.StructHooks["asuka/svg.Icon"] = func(src string) (map[string]any, error) {
		ic, err := svg.Parse([]byte(src))
		if err != nil {
			return nil, err
		}
		return map[string]any{"data": ic.Encode(), "width": float64(ic.Width), "height": float64(ic.Height)}, nil
	}
	ir.StructHooks["asuka/shader.Program"] = func(src string) (map[string]any, error) {
		c, err := shadercode.Compile(src)
		if err != nil {
			return nil, err
		}
		code := c.Encode()
		glsl, err := shadercode.GLSL(code)
		if err != nil {
			return nil, err
		}
		return map[string]any{"code": code, "glsl": glsl}, nil
	}
}
