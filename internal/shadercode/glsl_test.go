package shadercode

import (
	"strings"
	"testing"
)

func TestGLSL(t *testing.T) {
	c, err := Compile(`package main

var Time float
var Tint vec4

func Fragment(dst vec4, pos vec2) vec4 {
	d := distance(pos/rectSize(), vec2(0.5))
	return vec4(Tint.rgb*smoothstep(0.5, 0, d)*(0.8+0.2*sin(Time*2)), 1) * Tint.a
}
`)
	if err != nil {
		t.Fatal(err)
	}
	d, err := Decode(c.Encode())
	if err != nil {
		t.Fatal(err)
	}
	src := d.GLSL
	if src != c.GLSL {
		t.Fatal("the GLSL didn't cross")
	}
	for _, want := range []string{"#version 300 es", "vec4 asuka_shade(", "out vec4 asuka_color;", "void main()"} {
		if !strings.Contains(src, want) {
			t.Errorf("GLSL lacks %q:\n%s", want, src)
		}
	}
	if d.GL != c.GL || d.HLSL != c.HLSL || d.GLSize != c.GLSize || d.HLSLSize != c.HLSLSize ||
		len(d.GLOffsets) != c.Lanes || len(d.HLSLOffsets) != c.Lanes {
		t.Fatal("the desktops' programs didn't cross")
	}
	for i := range c.Lanes {
		if d.GLOffsets[i] != c.GLOffsets[i] || d.HLSLOffsets[i] != c.HLSLOffsets[i] {
			t.Fatalf("lane %d's offsets didn't cross", i)
		}
	}
	for _, want := range []string{"uniform AsukaUniforms", "vec4 asuka_shade(", "uniform vec4 asuka_frame;", "void main()"} {
		if !strings.Contains(d.GL, want) {
			t.Errorf("GL lacks %q:\n%s", want, d.GL)
		}
	}
	if strings.Contains(d.GL, "#version") {
		t.Errorf("GL has a version line: the host writes its context's")
	}
	for _, want := range []string{"cbuffer", "float4 asuka_shade(", "float4 asuka_ps(float4 position : SV_Position) : SV_Target"} {
		if !strings.Contains(d.HLSL, want) {
			t.Errorf("HLSL lacks %q:\n%s", want, d.HLSL)
		}
	}
	// Tint (a vec4) takes four lanes, each 4 bytes after the one before.
	for _, u := range c.Uniforms {
		if u.Name == "Tint" {
			for k := 1; k < 4; k++ {
				if c.GLOffsets[u.Lane+k] != c.GLOffsets[u.Lane]+uint32(4*k) || c.HLSLOffsets[u.Lane+k] != c.HLSLOffsets[u.Lane]+uint32(4*k) {
					t.Errorf("Tint's lanes: GL %v, HLSL %v", c.GLOffsets[u.Lane:u.Lane+4], c.HLSLOffsets[u.Lane:u.Lane+4])
				}
			}
		}
	}
}
