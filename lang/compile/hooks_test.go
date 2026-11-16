package compile

import (
	"strings"
	"testing"
)

// A struct a hook makes is made from a literal: anything else, or markup
// that doesn't parse, is the compiler's error at it.
func TestStructHookErrors(t *testing.T) {
	for _, c := range []struct{ src, want string }{
		{"import asuka/svg\nfn icon(s: string) -> svg.Icon { return svg.Icon(s) }\nlet i = icon(\"<svg/>\")\n", "give it a string literal"},
		{"import asuka/svg\nlet n = 2\nlet i = svg.Icon(\"<svg>{n}</svg>\")\n", "give it a string literal"},
		{"import asuka/svg\nlet i = svg.Icon(\"<svg\")\n", "app.asuka:2"},
		{"import asuka/shader\nlet p = shader.Program(r\"package main; func Fragment(dst vec4, pos vec2) vec4 { return nope }\")\n", "nope"},
	} {
		err := Files(map[string][]byte{"app.asuka": []byte(c.src)}, nil).Err()
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%q: got %v, want an error with %q", c.src, err, c.want)
		}
	}
}
