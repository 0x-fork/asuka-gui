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
