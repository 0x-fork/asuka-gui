package compile

import (
	"strings"
	"testing"
)

// The standard library's runtime builtins: asuka/appearance's rgb, an
// opaque color of its channels (clamped), on both backends.
func TestAppearanceRGB(t *testing.T) {
	res := Files(map[string][]byte{"a.asuka": []byte(`import asuka/appearance

test "rgb" {
  let c: Color = #0a7cff
  print(appearance.rgb(10, 124, 255) == c)
  print(appearance.rgb(300, -1, 0) == #ff0000)
}
`)}, &Options{Packages: map[string]map[string][]byte{
		"asuka/appearance": {"a.asuka": []byte("pub native fn rgb(r: int, g: int, b: int) -> Color\n")},
	}})
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	out, err := runBoth(t, res.Module)
	if want := "true\ntrue\nok rgb"; strings.TrimSpace(out) != want || err != nil {
		t.Fatalf("got (%v):\n%s\nwant:\n%s", err, out, want)
	}
}
