package syntax

import (
	"unicode"
	"unicode/utf8"

	"golang.org/x/text/unicode/norm"
)

// Names are Unicode identifiers as UAX #31 defines them (its default
// identifier syntax, as Rust, Swift, Python and JavaScript take it): a
// letter of any script or _, then letters, combining marks (a Devanagari
// vowel sign, a Thai tone mark), digits and connector punctuation. A
// name is NFC normalized, so names that look the same and differ only
// in how their accents were typed (é as one code point, or e and a
// combining acute) are the same name.

// IsIdentStart reports whether r can start a name: _ or XID_Start.
func IsIdentStart(r rune) bool {
	if r < utf8.RuneSelf {
		return 'a' <= r && r <= 'z' || 'A' <= r && r <= 'Z' || r == '_'
	}
	return isIDStart(r) && !nfkcUnstable(r, true)
}

// IsIdentContinue reports whether r can follow in a name: XID_Continue.
func IsIdentContinue(r rune) bool {
	if r < utf8.RuneSelf {
		return 'a' <= r && r <= 'z' || 'A' <= r && r <= 'Z' || r == '_' || '0' <= r && r <= '9'
	}
	return isIDContinue(r) && !nfkcUnstable(r, false)
}

// NormalizeIdent is a name's canonical form, its NFC normalization.
func NormalizeIdent(s string) string {
	for i := 0; i < len(s); i++ {
		if s[i] >= utf8.RuneSelf {
			return norm.NFC.String(s)
		}
	}
	return s
}

// isIDStart is UAX #31's ID_Start: letters, letter numbers and
// Other_ID_Start, but not Pattern_Syntax or Pattern_White_Space.
func isIDStart(r rune) bool {
	return (unicode.IsLetter(r) || unicode.Is(unicode.Nl, r) || unicode.Is(unicode.Other_ID_Start, r)) &&
		!unicode.Is(unicode.Pattern_Syntax, r) && !unicode.Is(unicode.Pattern_White_Space, r)
}

// isIDContinue is UAX #31's ID_Continue: ID_Start, nonspacing and
// spacing marks, decimal digits, connector punctuation and
// Other_ID_Continue, but not Pattern_Syntax or Pattern_White_Space.
func isIDContinue(r rune) bool {
	return (isIDStart(r) || unicode.In(r, unicode.Mn, unicode.Mc, unicode.Nd, unicode.Pc, unicode.Other_ID_Continue)) &&
		!unicode.Is(unicode.Pattern_Syntax, r) && !unicode.Is(unicode.Pattern_White_Space, r)
}

// nfkcUnstable reports the code points ID_Start (start) or ID_Continue
// has and XID_Start or XID_Continue doesn't: those whose NFKC form isn't
// a name (DerivedCoreProperties.txt lists them; TestXID checks the list
// against NFKC).
func nfkcUnstable(r rune, start bool) bool {
	switch r {
	case 0x037A, 0x309B, 0x309C, 0xFDFA, 0xFDFB,
		0xFE70, 0xFE72, 0xFE74, 0xFE76, 0xFE78, 0xFE7A, 0xFE7C, 0xFE7E:
		return true
	case 0x0E33, 0x0EB3, 0xFF9E, 0xFF9F:
		return start
	}
	return 0xFC5E <= r && r <= 0xFC63
}
