package compile

import (
	"slices"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/shadercode"
)

// A shader declaration compiles to a program for every GPU, its uniforms
// its parameters (and the host's time), in the order declared.
func TestShaderProgram(t *testing.T) {
	res := Files(map[string][]byte{"app.asuka": []byte(`
shader Glow(tint: Color, level: float = 1, rings: int = 3, on: bool = true) {
  fn fragment(pos: vec2) -> vec4 {
    let d = distance(pos / size, vec2(0.5))
    var g = smoothstep(0.5, 0, d) * level
    if !on {
      g = 0
    }
    return vec4(tint.rgb * g * float(rings), 1) * tint.a * (0.8 + 0.2 * sin(time))
  }
}

app { Glow(tint: .accent) { height: 100 } }
`)}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if len(res.Program.Info.Shaders) != 1 {
		t.Fatalf("%d programs", len(res.Program.Info.Shaders))
	}
	for _, b := range res.Program.Info.Shaders {
		code, err := shadercode.Decode(b)
		if err != nil {
			t.Fatal(err)
		}
		var names []string
		for _, u := range code.Uniforms {
			if !strings.HasPrefix(u.Name, "__") {
				names = append(names, u.Name)
			}
		}
		for _, want := range []string{"Time", "U_tint", "U_level", "U_rings", "U_on"} {
			if !slices.Contains(names, want) {
				t.Errorf("no uniform %s in %v", want, names)
			}
		}
		for what, src := range map[string]string{"Metal": code.Metal, "AGSL": code.AGSL, "GLSL": code.GLSL, "HLSL": code.HLSL, "GL": code.GL} {
			if src == "" {
				t.Errorf("no %s program", what)
			}
		}
	}
}

// What a shader can't do is said at its place, in its own names.
func TestShaderErrors(t *testing.T) {
	for _, c := range []struct{ src, want string }{
		{"shader S { fn other() -> float = 1 }", "a shader needs its fragment function"},
		{"shader S { fn fragment(p: vec2, q: vec2) -> vec4 = vec4(1) }", "fragment takes the pixel's position and gives its color"},
		{"shader S { fn fragment(p: vec2) -> vec4 { return vec4(nope) } }", "unknown name nope in a shader"},
		{"shader S { fn fragment(p: vec2) -> vec4 { return vec4(noise(p)) } }", "unknown function noise in a shader"},
		{"shader S { fn fragment(p: vec2) -> vec4 { let f = x => x\n return vec4(1) } }", "a shader's expressions are numbers, vectors and matrices"},
		{"shader S { fn fragment(p: vec2) -> vec4 { let a = if p.x > 0 { 1 } else { 0 }\n return vec4(a) } }", "a shader's if is a statement, not a value"},
		{"shader S(name: string) { fn fragment(p: vec2) -> vec4 = vec4(1) }", "a shader's parameter is a uniform"},
		{"shader S { fn fragment(p: vec2) -> vec4 {\n  let uv = p / size\n  return uv\n} }", "the shader: cannot use type vec2 as type vec4 in return argument"},
		{"shader S { fn fragment(p: vec2) -> vec4 {\n  for i in [1, 2] { }\n  return vec4(1)\n} }", "a shader's for counts over a range"},
		{"let x = shader(\"package main\")", "is a declaration now"},
	} {
		res := Files(map[string][]byte{"app.asuka": []byte(c.src + "\n")}, nil)
		err := res.Err()
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s\nwant %q, got %v", c.src, c.want, err)
		}
	}
	// A mistake Kage finds is at the statement it's in.
	res := Files(map[string][]byte{"app.asuka": []byte("shader S {\n  fn fragment(p: vec2) -> vec4 {\n    let uv = p / size\n    return uv\n  }\n}\n")}, nil)
	if len(res.Diags) != 1 || res.Diags[0].Pos.Line != 4 {
		t.Errorf("the mistake's place: %+v", res.Diags)
	}
}
