package syntax

import (
	"strconv"
	"unicode"
	"unicode/utf8"
)

// An ErrorHandler is told about each error the scanner finds. The
// scanner keeps going after one.
type ErrorHandler func(pos Pos, msg string)

// A Comment is a // line comment (/// for documentation) or a /* block
// */ comment, with its delimiters.
type Comment struct {
	Pos  Pos
	Text string
}

// End is the position just after the comment.
func (c *Comment) End() Pos { return c.Pos + Pos(len(c.Text)) }

// IsDoc reports whether the comment is a /// documentation comment.
func (c *Comment) IsDoc() bool {
	return len(c.Text) >= 3 && c.Text[:3] == "///" && (len(c.Text) == 3 || c.Text[3] != '/')
}

// A Scanner turns a file's source into tokens. Comments don't come out
// of Scan; they're kept, in order, for Comments.
//
// Newlines are significant: Scan returns a NEWLINE where a line ends an
// item, a member or a statement. It doesn't when the line can't end
// there, as Go and Swift do:
//
//   - inside ( ), [ ] and an import's .{ }; inside { } newlines are
//     significant again;
//   - inside a """ string's {interpolation}; a one-line string's
//     interpolation ends with its line (the string isn't terminated);
//   - after an operator that needs an operand on its right (binary
//     operators, =, the assignment operators, =>, ->, ., ?., .., ..<, |),
//     after a , or a :, and after an opening bracket;
//   - after another newline, a ; or the start of the file: runs of
//     newlines are one.
//
// Not after ? (a line may end with an optional type, String?), ! (one
// may end with a force unwrap, x!) or > (one may end with a generic
// type, Loading<User>): a comparison broken across lines puts its > at
// the start of the next line instead, inside parentheses.
type Scanner struct {
	file *File
	src  string
	err  ErrorHandler

	ch       rune // the current character, -1 at the end
	offset   int  // ch's offset
	rdOffset int  // the offset after ch

	last     Token // the last token returned, for newlines
	frames   []frame
	comments []*Comment

	ErrorCount int
}

type frameKind uint8

const (
	paren    frameKind = iota // ( or [: newlines don't count
	brace                     // {: newlines count
	dotbrace                  // an import's .{: newlines don't count
	interp                    // a string's {…}: newlines don't count
)

type frame struct {
	kind      frameKind
	multiline bool // interp: in a """ string
	start     int  // interp: the string's first byte
}

const eof = -1

// Init prepares s to scan src, the contents of file. err, if not nil, is
// told about each error.
func (s *Scanner) Init(file *File, src []byte, err ErrorHandler) {
	if file.Size() != len(src) {
		panic("syntax: file size doesn't match the source's length")
	}
	s.file = file
	s.src = string(src)
	s.err = err
	s.ch = ' '
	s.offset, s.rdOffset = 0, 0
	s.last = NEWLINE // so a file's leading newlines don't count
	s.frames = s.frames[:0]
	s.comments = nil
	s.ErrorCount = 0
	s.next()
	if s.ch == '\uFEFF' {
		s.next() // a byte order mark
	}
}

// Comments are the comments scanned so far, in source order.
func (s *Scanner) Comments() []*Comment { return s.comments }

func (s *Scanner) error(offset int, msg string) {
	if s.err != nil {
		s.err(s.file.Pos(offset), msg)
	}
	s.ErrorCount++
}

// next reads the next character into s.ch.
func (s *Scanner) next() {
	if s.rdOffset >= len(s.src) {
		s.offset = len(s.src)
		if s.ch == '\n' {
			s.file.AddLine(s.offset)
		}
		s.ch = eof
		return
	}
	s.offset = s.rdOffset
	if s.ch == '\n' {
		s.file.AddLine(s.offset)
	}
	r, w := rune(s.src[s.rdOffset]), 1
	if r >= utf8.RuneSelf {
		r, w = utf8.DecodeRuneInString(s.src[s.rdOffset:])
		if r == utf8.RuneError && w == 1 {
			s.error(s.offset, "invalid UTF-8")
		} else if r == '\uFEFF' && s.offset > 0 {
			s.error(s.offset, "a byte order mark in the middle of the file")
		}
	}
	s.rdOffset += w
	s.ch = r
}

func (s *Scanner) peek() byte {
	if s.rdOffset < len(s.src) {
		return s.src[s.rdOffset]
	}
	return 0
}

func (s *Scanner) peek2() byte {
	if s.rdOffset+1 < len(s.src) {
		return s.src[s.rdOffset+1]
	}
	return 0
}

// newlineCounts reports whether a newline here ends something.
func (s *Scanner) newlineCounts() bool {
	if n := len(s.frames); n > 0 && s.frames[n-1].kind != brace {
		return false
	}
	switch s.last {
	case NEWLINE, SEMICOLON, LBRACE, LPAREN, LBRACK, DOTBRACE, COMMA, COLON,
		ADD, SUB, MUL, QUO, REM,
		ASSIGN, ADD_ASSIGN, SUB_ASSIGN, MUL_ASSIGN, QUO_ASSIGN, REM_ASSIGN, COALESCE_ASSIGN,
		EQL, NEQ, LSS, LEQ, GEQ, LAND, LOR, COALESCE,
		OPTDOT, FATARROW, ARROW, RANGE, RANGELT, PERIOD, PIPE, ELSE:
		return false
	}
	return true
}

func (s *Scanner) push(k frameKind) { s.frames = append(s.frames, frame{kind: k}) }

// pop closes the innermost bracket of kind k, if that's what's open.
func (s *Scanner) pop(k frameKind) {
	if n := len(s.frames); n > 0 && s.frames[n-1].kind == k {
		s.frames = s.frames[:n-1]
	}
}

// Scan returns the next token: its position, its kind and its source
// text (for every token, so its end is pos+len(lit); "" at the end).
func (s *Scanner) Scan() (pos Pos, tok Token, lit string) {
	pos, tok, lit = s.scan()
	s.last = tok
	return
}

func (s *Scanner) scan() (Pos, Token, string) {
redo:
	// Whitespace, newlines and comments.
	for {
		switch s.ch {
		case ' ', '\t', '\r':
			s.next()
			continue
		case '\n':
			if n := len(s.frames); n > 0 && s.frames[n-1].kind == interp && !s.frames[n-1].multiline {
				// An interpolation of a one-line string ends with its
				// line: the string wasn't terminated.
				s.error(s.frames[n-1].start, "string not terminated")
				s.frames = s.frames[:n-1]
			}
			if s.newlineCounts() {
				off := s.offset
				s.next()
				return s.file.Pos(off), NEWLINE, "\n"
			}
			s.next()
			continue
		case '/':
			if c := s.peek(); c == '/' || c == '*' {
				if nl := s.comment(); nl >= 0 && s.newlineCounts() {
					// A block comment spanning lines ends a line.
					return s.file.Pos(nl), NEWLINE, "\n"
				}
				continue
			}
		}
		break
	}

	start := s.offset
	pos := s.file.Pos(start)
	ch := s.ch
	switch {
	case ch == eof:
		if n := len(s.frames); n > 0 && s.frames[n-1].kind == interp {
			s.error(s.frames[n-1].start, "string not terminated")
			s.frames = s.frames[:n-1]
		}
		return pos, EOF, ""
	case isLetter(ch):
		if ch == 'r' && s.peek() == '"' {
			s.next()
			return s.string(start, true)
		}
		s.ident()
		lit := s.src[start:s.offset]
		// After . or ?., a keyword is a member's name: x.type, .default.
		if s.last != PERIOD && s.last != OPTDOT {
			if t, ok := keywords[lit]; ok {
				return pos, t, lit
			}
		}
		return pos, IDENT, lit
	case isDigit(ch):
		tok := s.number()
		return pos, tok, s.src[start:s.offset]
	case ch == '"':
		return s.string(start, false)
	}

	s.next() // past ch
	tok := ILLEGAL
	switch ch {
	case '#':
		tok = s.color(start)
	case '(':
		s.push(paren)
		tok = LPAREN
	case '[':
		s.push(paren)
		tok = LBRACK
	case '{':
		s.push(brace)
		tok = LBRACE
	case ')':
		s.pop(paren)
		tok = RPAREN
	case ']':
		s.pop(paren)
		tok = RBRACK
	case '}':
		if n := len(s.frames); n > 0 && s.frames[n-1].kind == interp {
			// The end of an interpolation: the string goes on.
			multiline, open := s.frames[n-1].multiline, s.frames[n-1].start
			s.frames = s.frames[:n-1]
			return s.stringRest(start, open, false, multiline, true)
		}
		// A } closes its { and any ( or [ left open inside it, so an
		// unclosed bracket doesn't swallow the newlines of the rest of
		// the file; the parser reports it.
		for n := len(s.frames); n > 0; n-- {
			k := s.frames[n-1].kind
			if k == interp {
				break
			}
			s.frames = s.frames[:n-1]
			if k == brace || k == dotbrace {
				break
			}
		}
		tok = RBRACE
	case ',':
		tok = COMMA
	case ':':
		tok = COLON
	case ';':
		tok = SEMICOLON
	case '+':
		tok = s.alt('=', ADD_ASSIGN, ADD)
	case '-':
		switch s.ch {
		case '>':
			s.next()
			tok = ARROW
		case '=':
			s.next()
			tok = SUB_ASSIGN
		default:
			tok = SUB
		}
	case '*':
		tok = s.alt('=', MUL_ASSIGN, MUL)
	case '/':
		tok = s.alt('=', QUO_ASSIGN, QUO)
	case '%':
		tok = s.alt('=', REM_ASSIGN, REM)
	case '=':
		switch s.ch {
		case '=':
			s.next()
			tok = EQL
		case '>':
			s.next()
			tok = FATARROW
		default:
			tok = ASSIGN
		}
	case '!':
		tok = s.alt('=', NEQ, NOT)
	case '<':
		tok = s.alt('=', LEQ, LSS)
	case '>':
		tok = s.alt('=', GEQ, GTR)
	case '&':
		if s.ch == '&' {
			s.next()
			tok = LAND
		} else {
			s.error(start, "unexpected '&' (did you mean '&&'?)")
		}
	case '|':
		tok = s.alt('|', LOR, PIPE)
	case '@':
		tok = AT
	case '?':
		switch s.ch {
		case '?':
			s.next()
			tok = s.alt('=', COALESCE_ASSIGN, COALESCE)
		case '.':
			s.next()
			tok = OPTDOT
		default:
			tok = QUESTION
		}
	case '.':
		switch s.ch {
		case '.':
			s.next()
			tok = s.alt('<', RANGELT, RANGE)
			if tok == RANGE && s.ch == '.' {
				s.error(start, "unexpected '...' (a range is a..b or a..<b)")
				s.next()
			}
		case '{':
			s.next()
			s.push(dotbrace)
			tok = DOTBRACE
		default:
			tok = PERIOD
		}
	default:
		if ch == '\uFEFF' {
			goto redo
		}
		s.error(start, "unexpected character "+strconv.QuoteRune(ch))
	}
	return pos, tok, s.src[start:s.offset]
}

func (s *Scanner) alt(c rune, yes, no Token) Token {
	if s.ch == c {
		s.next()
		return yes
	}
	return no
}

// comment scans a comment at s.ch == '/' and keeps it. For a block
// comment that spans lines it returns the offset of its first newline,
// otherwise -1.
func (s *Scanner) comment() int {
	start := s.offset
	nl := -1
	s.next()
	if s.ch == '/' {
		for s.ch != '\n' && s.ch != eof {
			s.next()
		}
	} else {
		s.next() // past *
		for {
			if s.ch == eof {
				s.error(start, "comment not terminated")
				break
			}
			if s.ch == '\n' && nl < 0 {
				nl = s.offset
			}
			if s.ch == '*' && s.peek() == '/' {
				s.next()
				s.next()
				break
			}
			s.next()
		}
	}
	text := s.src[start:s.offset]
	if n := len(text); n > 0 && text[n-1] == '\r' && text[1] == '/' {
		text = text[:n-1]
	}
	s.comments = append(s.comments, &Comment{Pos: s.file.Pos(start), Text: text})
	return nl
}

func isLetter(ch rune) bool {
	return 'a' <= ch && ch <= 'z' || 'A' <= ch && ch <= 'Z' || ch == '_' ||
		ch >= utf8.RuneSelf && unicode.IsLetter(ch)
}

func isDigit(ch rune) bool { return '0' <= ch && ch <= '9' }

func isHex(ch rune) bool {
	return '0' <= ch && ch <= '9' || 'a' <= ch && ch <= 'f' || 'A' <= ch && ch <= 'F'
}

func (s *Scanner) ident() {
	for isLetter(s.ch) || isDigit(s.ch) || s.ch >= utf8.RuneSelf && unicode.IsDigit(s.ch) {
		s.next()
	}
}

// digits scans digits and underscores (an underscore only between
// digits), as isDigit or isHex says.
func (s *Scanner) digits(ok func(rune) bool) {
	for ok(s.ch) || s.ch == '_' {
		if s.ch == '_' {
			if r := rune(s.peek()); !ok(r) {
				s.error(s.offset, "'_' must separate digits")
			}
		}
		s.next()
	}
}

// number scans an integer or a float and its unit: 300ms, 5s, 2min, 1h
// (durations), 50% and 1fr (lengths).
//
// A % straight after a number is a percentage unless it's followed by
// what starts an operand (a letter, a digit, an opening bracket, a
// string or a .), which makes it the remainder: 50% is a length, 10%3
// and 10 % 3 are remainders.
func (s *Scanner) number() Token {
	start := s.offset
	tok := INT
	if s.ch == '0' && (s.peek() == 'x' || s.peek() == 'X') {
		s.next()
		s.next()
		if !isHex(s.ch) {
			s.error(start, "a hexadecimal number needs digits after 0x")
		}
		s.digits(isHex)
		if isLetter(s.ch) {
			s.error(s.offset, "unexpected "+strconv.QuoteRune(s.ch)+" in a hexadecimal number")
			s.ident()
		}
		return INT
	}
	s.digits(isDigit)
	if s.ch == '.' && isDigit(rune(s.peek())) {
		tok = FLOAT
		s.next()
		s.digits(isDigit)
	}
	if s.ch == 'e' || s.ch == 'E' {
		p, p2 := rune(s.peek()), rune(s.peek2())
		if isDigit(p) || (p == '+' || p == '-') && isDigit(p2) {
			tok = FLOAT
			s.next()
			if s.ch == '+' || s.ch == '-' {
				s.next()
			}
			s.digits(isDigit)
		}
	}
	switch {
	case s.ch == '%':
		switch c := rune(s.peek()); {
		case isLetter(c), isDigit(c), c == '(', c == '[', c == '"', c == '.', c == '#':
			// a remainder: 10%3
		default:
			s.next()
			return LENGTH
		}
	case isLetter(s.ch):
		u := s.offset
		s.ident()
		switch unit := s.src[u:s.offset]; unit {
		case "ms", "s", "min", "h":
			return DURATION
		case "fr":
			return LENGTH
		default:
			s.error(u, "unknown unit "+strconv.Quote(unit)+" after a number (a duration takes ms, s, min or h; a length % or fr)")
		}
	}
	return tok
}

// color scans a color's hex digits after its #.
func (s *Scanner) color(start int) Token {
	d := s.offset
	for isHex(s.ch) {
		s.next()
	}
	n := s.offset - d
	if isLetter(s.ch) || isDigit(s.ch) {
		s.ident()
		s.error(start, "a color is #rgb, #rgba, #rrggbb or #rrggbbaa, in hexadecimal digits")
		return COLOR
	}
	switch n {
	case 3, 4, 6, 8:
	case 0:
		s.error(start, "unexpected '#' (a color is #rgb, #rrggbb or #rrggbbaa)")
	default:
		s.error(start, "a color has 3, 4, 6 or 8 hexadecimal digits, not "+strconv.Itoa(n))
	}
	return COLOR
}

// string scans a string starting at its opening quote (s.ch == '"';
// start is the r of a raw string). A string with interpolations comes
// out as STRING_HEAD, the interpolation's tokens, then STRING_MID or
// STRING_TAIL from the } that ends it.
func (s *Scanner) string(start int, raw bool) (Pos, Token, string) {
	multiline := false
	if s.peek() == '"' && s.peek2() == '"' {
		multiline = true
		s.next()
		s.next()
	}
	s.next() // past the (last) opening quote
	return s.stringRest(start, start, raw, multiline, false)
}

// stringRest scans a string's text from s.ch to its closing quote or
// its next interpolation; resumed says the text follows an
// interpolation's }. start is the token's first byte, open the string's.
func (s *Scanner) stringRest(start, open int, raw, multiline, resumed bool) (Pos, Token, string) {
	pos := s.file.Pos(start)
	done := func(t Token) (Pos, Token, string) { return pos, t, s.src[start:s.offset] }
	for {
		switch s.ch {
		case eof:
			s.error(open, "string not terminated")
			return done(tailOr(resumed))
		case '\n':
			if !multiline {
				s.error(open, "string not terminated (a string that spans lines is \"\"\"…\"\"\")")
				return done(tailOr(resumed))
			}
		case '"':
			if !multiline {
				s.next()
				return done(tailOr(resumed))
			}
			if s.peek() == '"' && s.peek2() == '"' {
				s.next()
				s.next()
				s.next()
				return done(tailOr(resumed))
			}
		case '\\':
			if !raw {
				s.escape()
				continue
			}
		case '{':
			if !raw {
				s.next()
				s.frames = append(s.frames, frame{kind: interp, multiline: multiline, start: open})
				if resumed {
					return done(STRING_MID)
				}
				return done(STRING_HEAD)
			}
		}
		s.next()
	}
}

func tailOr(resumed bool) Token {
	if resumed {
		return STRING_TAIL
	}
	return STRING
}

// escape scans an escape at s.ch == '\\': \n \t \r \0 \" \' \\ \{ \}
// \u{1F600}.
func (s *Scanner) escape() {
	start := s.offset
	s.next()
	switch s.ch {
	case 'n', 't', 'r', '0', '"', '\'', '\\', '{', '}':
		s.next()
	case 'u':
		s.next()
		if s.ch != '{' {
			s.error(start, "a \\u escape is \\u{hex digits}")
			return
		}
		s.next()
		d := s.offset
		for isHex(s.ch) {
			s.next()
		}
		n, err := strconv.ParseUint(s.src[d:s.offset], 16, 32)
		if s.ch != '}' || s.offset == d || s.offset-d > 6 {
			s.error(start, "a \\u escape is \\u{1 to 6 hex digits}")
			return
		}
		s.next()
		if err != nil || n > unicode.MaxRune || 0xD800 <= n && n < 0xE000 {
			s.error(start, "\\u{"+s.src[d:s.offset-1]+"} isn't a Unicode code point")
		}
	case eof, '\n':
		s.error(start, "escape not terminated")
	default:
		s.error(start, "unknown escape \\"+string(s.ch))
		s.next()
	}
}
