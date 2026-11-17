package bytecode

import (
	"reflect"
	"strings"
	"testing"
)

func TestRoundTrip(t *testing.T) {
	m := &Module{
		Ints:    []int64{1, -2, 1 << 40},
		Floats:  []float64{1.5},
		Strings: []string{"hi", ""},
		Shapes: []ShapeDef{
			{Name: "P", Fields: []Field{{"x", KInt, -1}, {"s", KRef, -1}}, Methods: map[string]uint32{"m": 0}},
			{Name: "E", Cases: []Case{{Name: "a"}, {Name: "b", Fields: []Field{{"", KRef, 0}}}}, IsError: true},
		},
		Builtins: []string{"str.len"},
		Globals:  []Kind{KRef, KInt},
		App:      -1,
		Main:     -1,
		Tests:    []Test{{"t", 0}},
	}
	var a Asm
	a.Emit(Byname["KSMALL"], 0, 5)
	j := a.Emit(Byname["JT"], 0, 0)
	a.Emit(Byname["RET"], 0)
	a.PatchJump(j, len(a.Code))
	a.Emit(Byname["RET0"])
	m.Funcs = []FuncDef{{Name: "f", File: "a.asuka", NParams: 1, Kinds: []Kind{KInt}, Flags: FThrows, Code: a.Code, Handlers: []Handler{{0, 3, 5}}, Lines: []Line{{0, 1}}}}
	got, err := Decode(m.Encode())
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(got, m) {
		t.Fatalf("round trip:\n got %+v\nwant %+v", got, m)
	}
	d := Disasm(m, &m.Funcs[0])
	if !strings.Contains(d, "JT       r0, ->9") {
		t.Fatalf("disasm:\n%s", d)
	}
}

func TestTruncated(t *testing.T) {
	b := (&Module{App: -1, Main: -1}).Encode()
	if _, err := Decode(b[:len(b)-2]); err == nil {
		t.Fatal("want an error")
	}
}
