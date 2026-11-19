package appbuild_test

import (
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/compile"
)

// A function the optimizer inlined everywhere it's called is called
// nowhere, but stays: the inlined frames in traces are named by it, by
// its index in the stripped module.
func TestStripKeepsInlinedFunctions(t *testing.T) {
	src := `
fn twice(x: int) -> int = x * 2
fn helper() -> int = 7
fn main() { print(twice(21)) }
test "t" { expect(helper() == 7) }
`
	res := compile.Files(map[string][]byte{"main.asuka": []byte(src)}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	m, err := bytecode.Decode(appbuild.StripTests(res.Module).Encode())
	if err != nil {
		t.Fatal(err)
	}
	inlined := 0
	for _, f := range m.Funcs {
		if f.Name == "helper" {
			t.Errorf("helper, only a test's, is left")
		}
		for _, in := range f.Inlines {
			if int(in.Func) >= len(m.Funcs) {
				t.Fatalf("%s: inlined function %d of %d", f.Name, in.Func, len(m.Funcs))
			}
			if g := m.Funcs[in.Func].Name; f.Name == "main" && g == "twice" {
				inlined++
			}
		}
	}
	if inlined == 0 {
		t.Errorf("main has no inlined twice")
	}
}
