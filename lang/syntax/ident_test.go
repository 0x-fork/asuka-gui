package syntax

import (
	"strings"
	"testing"
	"unicode"

	"golang.org/x/text/unicode/norm"
)

// TestXID checks IsIdentStart and IsIdentContinue against UAX #31's
// definition of XID_Start and XID_Continue: ID_Start and ID_Continue
// less the code points whose NFKC form isn't an identifier.
func TestXID(t *testing.T) {
	if unicode.Version != norm.Version {
		t.Logf("Go's Unicode is %s, x/text's %s", unicode.Version, norm.Version)
	}
	valid := func(s string, start bool) bool {
		for i, r := range s {
			if (i == 0 && start) && !isIDStart(r) || !isIDContinue(r) {
				return false
			}
		}
		return s != ""
	}
	for r := rune(utf8RuneSelf); r <= unicode.MaxRune; r++ {
		if isIDStart(r) {
			if want := valid(norm.NFKC.String(string(r)), true); IsIdentStart(r) != want {
				t.Errorf("IsIdentStart(%U) = %v, want %v", r, !want, want)
			}
		} else if IsIdentStart(r) {
			t.Errorf("IsIdentStart(%U), not ID_Start", r)
		}
		if isIDContinue(r) {
			if want := valid(norm.NFKC.String(string(r)), false); IsIdentContinue(r) != want {
				t.Errorf("IsIdentContinue(%U) = %v, want %v", r, !want, want)
			}
		} else if IsIdentContinue(r) {
			t.Errorf("IsIdentContinue(%U), not ID_Continue", r)
		}
	}
}

const utf8RuneSelf = 0x80

func TestScanUnicodeNames(t *testing.T) {
	tests := []struct{ src, want string }{
		// Devanagari's vowel signs and virama are combining marks.
		{"नमस्ते = 1", "ident:नमस्ते = int:1"},
		// Thai's vowels and tone marks.
		{"ราคา น้ำ", "ident:ราคา ident:น้ำ"},
		{"名前 _ä x١٢", "ident:名前 ident:_ä ident:x١٢"},
		// A combining mark can't start a name; a decomposed é can follow.
		{"cafe\u0301", "ident:cafe\u0301"},
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
	for _, src := range []string{"\u0301x", "a ⸯ b", "x ͺ"} {
		if _, errs, _ := scanAll(src); len(errs) == 0 || !strings.Contains(errs[0], "unexpected character") {
			t.Errorf("%q: errors %v, want an unexpected character", src, errs)
		}
	}
}

// A name is NFC normalized: café typed with a combining accent is café;
// its span is its source's.
func TestParseNormalizesNames(t *testing.T) {
	src := "let cafe\u0301 = caf\u00e9\n"
	f, errs := ParseFile(NewFileSet(), "x.asuka", []byte(src))
	if len(errs) > 0 {
		t.Fatal(errs)
	}
	d := f.Decls[0].(*LetDecl)
	id := d.Pattern.(*BindPat).Name
	use := d.Value.(*Ident)
	if id.Name != "caf\u00e9" || use.Name != id.Name {
		t.Errorf("names %q and %q, want both %q", id.Name, use.Name, "caf\u00e9")
	}
	if n := int(id.End - id.Start); n != len("cafe\u0301") {
		t.Errorf("the name's span is %d bytes, its source %d", n, len("cafe\u0301"))
	}
	if !IsIdentifier("नमस्ते") || IsIdentifier("\u0301a") || IsIdentifier("let") {
		t.Error("IsIdentifier")
	}
}
