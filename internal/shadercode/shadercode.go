// Package shadercode is a compiled ui.Shader as it crosses to the hosts
// (Shader's Code prop): its Kage source (for hosts that run it on the CPU
// with package interp), its Metal program (iOS, macOS), its AGSL program
// (Android), its GLSL ES 3.00 one (the web: no compiler runs in the
// page), the desktops' GLSL and HLSL (Linux's OpenGL, Windows's Direct3D
// 11, whose C runs no compiler but the platform's own), and where each
// uniform's lanes go in each.
//
// The uniforms' values cross as lanes (Shader's Uniforms prop): a u32 each,
// in package interp's order (each uniform's lanes in turn, a matrix's by
// columns, an array's elements in order), a float's bits, an int's or a
// uint's, a bool's 0 or 1. The bridge's own lanes come first: the hosts set
// the size, the scale and Time themselves.
package shadercode

import (
	"encoding/binary"
	"errors"
	"fmt"
	"strings"

	"github.com/egoist/asuka/internal/shader"
	"github.com/egoist/asuka/internal/shaderir"
	"github.com/egoist/asuka/internal/shaderir/glsl"
	"github.com/egoist/asuka/internal/shaderir/hlsl"
	"github.com/egoist/asuka/internal/shaderir/interp"
	"github.com/egoist/asuka/internal/shaderir/msl"
)

// Kind is a lane's type.
type Kind uint8

const (
	Float Kind = iota
	Int
	Uint
	Bool
)

// Uniform is one of a shader's uniform variables: its name in Go, where
// its lanes start, how many, and their kind.
type Uniform struct {
	Name        string
	Lane, Lanes int
	Kind        Kind
}

// Code is a compiled shader.
type Code struct {
	Source string // the Kage source
	Metal  string // a whole library: vertex asuka_vertex, fragment asuka_fragment (buffer 0: Uniforms; textures 0 to 3)
	AGSL   string // a whole RuntimeShader program
	GLSL   string // a whole GLSL ES 3.00 fragment shader (glslProgram)
	// MetalOffsets is where each lane goes in the Metal buffer, in bytes,
	// and MetalBool which lanes are a bool's (a byte); MetalSize its size.
	MetalOffsets []uint32
	MetalBool    []bool
	MetalSize    int
	// GL is the fragment shader Linux's OpenGL draws (GL 3.3 core or GL
	// ES 3.0), with no #version line (the host writes the context's): its
	// uniforms in the std140 block AsukaUniforms (lane i at GLOffsets[i]
	// bytes, GLSize bytes in all), its position GL's window position
	// mapped by the uniform vec4 asuka_frame (xy*frame.xy + frame.zw: a
	// framebuffer's rows count up, the view's down), its colors clamped,
	// premultiplied.
	GL        string
	GLOffsets []uint32
	GLSize    int
	// HLSL is the pixel shader asuka_ps Windows draws with Direct3D 11
	// (shader model 4 or 5): its uniforms in a cbuffer at b0 (lane i at
	// HLSLOffsets[i] bytes, HLSLSize bytes in all), asuka_step's at b1.
	HLSL        string
	HLSLOffsets []uint32
	HLSLSize    int
	// Uniforms are all the uniforms, the bridge's too (their names begin
	// with __), in order: AGSL's are U0, U1... in this order.
	Uniforms []Uniform
	// Lanes is how many lanes there are; Size, Scale and Time the lanes of
	// the box's size in points (two), the pixels a point and Time (-1: the
	// shader has no float Time).
	Lanes, Size, Scale, Time int
}

// Compile compiles a shader's Kage source into its code for every host.
func Compile(src string) (*Code, error) {
	p, err := shader.CompileFragment([]byte(src))
	if err != nil {
		return nil, err
	}
	c := &Code{Source: src, Time: -1}
	lane := 0
	for i, n := range interp.UniformWords(p) {
		u := Uniform{Name: p.UniformNames[i], Lane: lane, Lanes: n, Kind: kindOf(&p.Uniforms[i])}
		c.Uniforms = append(c.Uniforms, u)
		switch u.Name {
		case shader.RectSizeUniform:
			c.Size = lane
		case shader.ScaleUniform:
			c.Scale = lane
		case "Time":
			if p.Uniforms[i].Main == shaderir.Float {
				c.Time = lane
			}
		}
		lane += n
	}
	c.Lanes = lane
	scale := -1
	for i, n := range p.UniformNames {
		if n == shader.ScaleUniform {
			scale = i
		}
	}
	if scale < 0 {
		return nil, errors.New("shader: no scale uniform in the bridge")
	}
	if c.AGSL, err = glsl.AGSL(p, scale); err != nil {
		return nil, fmt.Errorf("shader: Android can't run it: %s", strings.TrimPrefix(err.Error(), "agsl: "))
	}
	c.Metal = msl.Fragment(p, "asuka_shade") + fmt.Sprintf(metalEntry, scale)
	if c.GLSL, err = glslProgram(p, c.Uniforms); err != nil {
		return nil, err
	}
	if err := c.desktop(p, scale); err != nil {
		return nil, err
	}
	offsets, size := msl.UniformLanes(p)
	c.MetalSize = size
	for _, u := range c.Uniforms {
		for j := range u.Lanes {
			c.MetalOffsets = append(c.MetalOffsets, uint32(offsets[u.Lane+j]))
			c.MetalBool = append(c.MetalBool, u.Kind == Bool)
		}
	}
	return c, nil
}

// metalEntry draws a triangle over the whole target and calls the
// shader's function with the pixel's position, and the position in points.
const metalEntry = `
struct AsukaVertex { float4 position [[position]]; };

vertex AsukaVertex asuka_vertex(uint id [[vertex_id]]) {
	float2 p = float2((id << 1) & 2, id & 2);
	AsukaVertex v;
	v.position = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return v;
}

fragment float4 asuka_fragment(AsukaVertex in [[stage_in]], constant Uniforms& uniforms [[buffer(0)]],
	texture2d<float> T0 [[texture(0)]], texture2d<float> T1 [[texture(1)]], texture2d<float> T2 [[texture(2)]], texture2d<float> T3 [[texture(3)]]) {
	return asuka_shade(uniforms, T0, T1, T2, T3, in.position, in.position.xy / uniforms.U%d);
}
`

func kindOf(t *shaderir.Type) Kind {
	for t.Main == shaderir.Array {
		t = &t.Sub[0]
	}
	switch t.Main {
	case shaderir.Bool:
		return Bool
	case shaderir.Int, shaderir.IVec2, shaderir.IVec3, shaderir.IVec4:
		return Int
	case shaderir.Uint, shaderir.UVec2, shaderir.UVec3, shaderir.UVec4:
		return Uint
	}
	return Float
}

var le = binary.LittleEndian

// desktop compiles the desktops' programs: GL's and HLSL's, and where
// their lanes go.
func (c *Code) desktop(p *shaderir.Program, scale int) error {
	lanes, size := glsl.UniformLanes(p)
	if len(lanes) != c.Lanes {
		return fmt.Errorf("shader: %d GL lanes, the code has %d", len(lanes), c.Lanes)
	}
	c.GL = glsl.Fragment(p, "asuka_shade", glsl.Options{Block: "AsukaUniforms"}) + fmt.Sprintf(glEntry, scale)
	c.GLOffsets, c.GLSize = offsets(lanes), size
	lanes, size = hlsl.UniformLanes(p)
	if len(lanes) != c.Lanes {
		return fmt.Errorf("shader: %d HLSL lanes, the code has %d", len(lanes), c.Lanes)
	}
	c.HLSL = hlsl.Fragment(p, "asuka_shade", 0, 0) + fmt.Sprintf(hlslEntry, scale)
	c.HLSLOffsets, c.HLSLSize = offsets(lanes), size
	return nil
}

func offsets(lanes []int) []uint32 {
	out := make([]uint32, len(lanes))
	for i, o := range lanes {
		out[i] = uint32(o)
	}
	return out
}

const glEntry = `
uniform vec4 asuka_frame;
out vec4 asuka_color;

void main() {
	vec4 p = vec4(gl_FragCoord.xy * asuka_frame.xy + asuka_frame.zw, gl_FragCoord.zw);
	vec4 c = clamp(asuka_shade(p, p.xy / U%d), 0.0, 1.0);
	asuka_color = vec4(min(c.rgb, vec3(c.a)), c.a);
}
`

const hlslEntry = `
cbuffer AsukaStep : register(b1) {
	float2 asuka_step;
}

float4 asuka_ps(float4 position : SV_Position) : SV_Target {
	float4 p = float4(position.xy * asuka_step, position.zw);
	float4 c = saturate(asuka_shade(p, p.xy / U%d));
	return float4(min(c.rgb, c.aaa), c.a);
}
`

// Encode is c on the wire: strings u32-length; then u32 lanes, size,
// scale, time+1; u32 Metal size, then per lane u32 offset (bit 31 a
// bool's); u32 uniform count, per uniform a string name, u32 lane, u32
// lanes, u8 Kind; then the GLSL program, a string; then the desktops':
// GL's program, a string, u32 its size, a u32 offset per lane, and
// HLSL's likewise.
func (c *Code) Encode() []byte {
	var b []byte
	str := func(s string) {
		b = le.AppendUint32(b, uint32(len(s)))
		b = append(b, s...)
	}
	str(c.Source)
	str(c.Metal)
	str(c.AGSL)
	b = le.AppendUint32(b, uint32(c.Lanes))
	b = le.AppendUint32(b, uint32(c.Size))
	b = le.AppendUint32(b, uint32(c.Scale))
	b = le.AppendUint32(b, uint32(c.Time+1))
	b = le.AppendUint32(b, uint32(c.MetalSize))
	for i, o := range c.MetalOffsets {
		if c.MetalBool[i] {
			o |= 1 << 31
		}
		b = le.AppendUint32(b, o)
	}
	b = le.AppendUint32(b, uint32(len(c.Uniforms)))
	for _, u := range c.Uniforms {
		str(u.Name)
		b = le.AppendUint32(b, uint32(u.Lane))
		b = le.AppendUint32(b, uint32(u.Lanes))
		b = append(b, byte(u.Kind))
	}
	str(c.GLSL)
	for _, d := range []struct {
		src  string
		size int
		offs []uint32
	}{{c.GL, c.GLSize, c.GLOffsets}, {c.HLSL, c.HLSLSize, c.HLSLOffsets}} {
		str(d.src)
		b = le.AppendUint32(b, uint32(d.size))
		for i := range c.Lanes {
			var o uint32
			if i < len(d.offs) {
				o = d.offs[i]
			}
			b = le.AppendUint32(b, o)
		}
	}
	return b
}

// Decode reads Encode's bytes.
func Decode(b []byte) (*Code, error) {
	i := 0
	bad := false
	u32 := func() int {
		if i+4 > len(b) {
			bad = true
			return 0
		}
		v := le.Uint32(b[i:])
		i += 4
		return int(v)
	}
	str := func() string {
		n := u32()
		if bad || i+n > len(b) {
			bad = true
			return ""
		}
		s := string(b[i : i+n])
		i += n
		return s
	}
	c := &Code{Source: str(), Metal: str(), AGSL: str()}
	c.Lanes, c.Size, c.Scale, c.Time = u32(), u32(), u32(), u32()-1
	c.MetalSize = u32()
	for range c.Lanes {
		o := uint32(u32())
		c.MetalOffsets = append(c.MetalOffsets, o&^(1<<31))
		c.MetalBool = append(c.MetalBool, o&(1<<31) != 0)
	}
	n := u32()
	for range n {
		u := Uniform{Name: str(), Lane: u32(), Lanes: u32()}
		if i < len(b) {
			u.Kind = Kind(b[i])
			i++
		}
		c.Uniforms = append(c.Uniforms, u)
	}
	if i < len(b) {
		c.GLSL = str()
	}
	if i < len(b) {
		c.GL = str()
		c.GLSize = u32()
		for range c.Lanes {
			c.GLOffsets = append(c.GLOffsets, uint32(u32()))
		}
		c.HLSL = str()
		c.HLSLSize = u32()
		for range c.Lanes {
			c.HLSLOffsets = append(c.HLSLOffsets, uint32(u32()))
		}
	}
	if bad || c.Lanes > 1<<16 {
		return nil, errors.New("shadercode: bad code")
	}
	return c, nil
}

// glslProgram is the shader as a whole GLSL ES 3.00 fragment shader,
// which the web host draws with WebGL 2: the uniforms U0, U1... in the
// order of uniforms, its entry point given the pixel's position from the
// top of the box (GL's counts from the bottom) and the position in
// points.
func glslProgram(p *shaderir.Program, uniforms []Uniform) (string, error) {
	size, scale := -1, -1
	for i, u := range uniforms {
		switch u.Name {
		case shader.RectSizeUniform:
			size = i
		case shader.ScaleUniform:
			scale = i
		}
	}
	if size < 0 || scale < 0 {
		return "", errors.New("shader: no size or scale uniform in the bridge")
	}
	lib := glsl.Fragment(p, "asuka_shade", glsl.Options{FlipY: "true"})
	return "#version 300 es\nprecision highp float;\nprecision highp int;\n\n" + lib + fmt.Sprintf(glslEntry, size, scale), nil
}

const glslEntry = `
out vec4 asuka_color;

void main() {
	vec4 p = vec4(gl_FragCoord.x, U%[1]d.y * U%[2]d - gl_FragCoord.y, gl_FragCoord.z, gl_FragCoord.w);
	asuka_color = asuka_shade(p, p.xy / U%[2]d);
}
`
