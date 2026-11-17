package syntax

import (
	"strings"
	"testing"
)

// r#"…"# holds a ", as Rust's does: it ends only at a quote followed by
// as many #s as it opened with.
func TestScanRawHashes(t *testing.T) {
	tests := []struct{ src, want string }{
		{`r#"say "hi""# x`, `string:r#"say "hi""# ident:x`},
		{`r##"a "# b"## x`, `string:r##"a "# b"## ident:x`},
		{`r#""#`, `string:r#""#`},
		{`r#"\n{x}"#`, `string:r#"\n{x}"#`},
		{"r#\"\"\"\na \"\"\" b\n\"\"\"#", "string:r#\"\"\"\na \"\"\" b\n\"\"\"#"},
		{`r"a" r #fff`, `string:r"a" ident:r color:#fff`},
	}
	for _, tt := range tests {
		toks, errs, _ := scanAll(tt.src)
		if len(errs) > 0 {
			t.Errorf("%q: errors %v", tt.src, errs)
		}
		if got := strings.Join(toks, " "); got != tt.want {
			t.Errorf("%q:\n got %s\nwant %s", tt.src, got, tt.want)
		}
	}
	for _, src := range []string{`r#"open" x`, "r#\"a\nb\"#"} {
		if _, errs, _ := scanAll(src); len(errs) == 0 || !strings.Contains(errs[0], "string not terminated") {
			t.Errorf("%q: errors %v, want string not terminated", src, errs)
		}
	}
}

func TestParseRawHashes(t *testing.T) {
	tests := []struct {
		src    string
		hashes int
		multi  bool
		text   string
	}{
		{`r#"say "hi""#`, 1, false, `say "hi"`},
		{`r##"a "# b"##`, 2, false, `a "# b`},
		{`r"C:\x"`, 0, false, `C:\x`},
		{"r#\"\"\"\na \"\"\" b\n\"\"\"#", 1, true, "\na \"\"\" b\n"},
	}
	for _, tt := range tests {
		x, errs := ParseExpr(tt.src)
		if len(errs) > 0 {
			t.Fatalf("%q: %v", tt.src, errs)
		}
		s, ok := x.(*StringLit)
		if !ok || !s.Raw || s.Hashes != tt.hashes || s.Multiline != tt.multi || len(s.Segments) != 1 || s.Segments[0] != tt.text {
			t.Errorf("%q: %#v", tt.src, x)
			continue
		}
		src := "let s = " + tt.src + "\n"
		out, err := Format("x.asuka", []byte(src))
		if err != nil || string(out) != src {
			t.Errorf("%q formats as %q, %v", src, out, err)
		}
	}
}
