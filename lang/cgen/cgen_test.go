package cgen

import "testing"

func TestCBytes(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{"plain name", `"plain name"`},
		// An octal escape is three digits: the digits after it stay.
		{"é1/日本a.asuka", `"\303\2511/\346\227\245\346\234\254a.asuka"`},
		{`"q" \ ??= %s`, `"\042q\042 \134 \077\077= %s"`},
		{"a\nb\x00", `"a\012b\000"`},
	} {
		if got := cbytes(c.in); got != c.want {
			t.Errorf("cbytes(%q) = %s, want %s", c.in, got, c.want)
		}
	}
}

// pathArgs counts what a state path reads from its argument registers
// (core/src/vm/path.h): an index's, a key's, a member's, a write's old
// value, and after a key whether it was there.
func TestPathArgs(t *testing.T) {
	for _, c := range []struct {
		path string
		want int
	}{
		{"", 0},
		{"f\x01\x00", 0},
		{"f\x01\x00i", 1},
		{"F\x02\x00gn", 1},
		{"k\x04f\x00\x00", 1},
		{"Ia", 1},
		{"f\x00\x00ir", 1},
		{"io", 2},
		{"k\x04o", 3},
		{"h\x01", 1},
	} {
		if got := pathArgs(c.path); got != c.want {
			t.Errorf("pathArgs(%q) = %d, want %d", c.path, got, c.want)
		}
	}
}
