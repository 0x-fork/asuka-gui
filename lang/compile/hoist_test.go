package compile

import (
	"os"
	"strings"
	"testing"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// TestConstantLiteralsHoisted: a function that gives a constant literal
// reads it from the global init.strings built it in (GGET), and makes
// nothing (no NEWA, NEWS, NEWM, NEWE, set.of) each time it's called;
// testdata/run/hoisted.asuka runs them on both backends.
func TestConstantLiteralsHoisted(t *testing.T) {
	path := "testdata/run/hoisted.asuka"
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	res := Files(map[string][]byte{path: src}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	m := res.Module
	makes := map[string]bool{"NEWA": true, "NEWS": true, "NEWM": true, "NEWE": true}
	for _, name := range []string{"names", "empty", "grid", "table", "noTable", "primes", "noSet", "plain", "shape", "pair"} {
		var f *bc.FuncDef
		for i := range m.Funcs {
			if m.Funcs[i].Name == name {
				f = &m.Funcs[i]
			}
		}
		if f == nil {
			t.Fatalf("no function %s", name)
		}
		gets := 0
		for pc := 0; pc < len(f.Code); {
			op := bc.Op(f.Code[pc])
			switch n := bc.Ops[op].Name; {
			case makes[n]:
				t.Errorf("%s makes its literal (%s):\n%s", name, n, bc.Disasm(m, f))
			case n == "CALLB" && strings.Contains(bc.Disasm(m, f), "set.of"):
				t.Errorf("%s makes its set:\n%s", name, bc.Disasm(m, f))
			case n == "GGET":
				gets++
			}
			pc += op.Width()
		}
		if gets != 1 {
			t.Errorf("%s reads %d globals, want its literal's:\n%s", name, gets, bc.Disasm(m, f))
		}
	}
}
