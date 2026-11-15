package toml

import (
	"fmt"
	"math"
	"strconv"
	"strings"
	"unicode/utf8"
)

// kind is a TOML value's type.
type kind uint8

const (
	kString kind = iota
	kInteger
	kFloat
	kBool
	kDatetime      // an offset date-time: 1979-05-27T07:32:00Z
	kDatetimeLocal // a local date-time: 1979-05-27T07:32:00
	kDateLocal     // a local date: 1979-05-27
	kTimeLocal     // a local time: 07:32:00
	kArray
	kTable
)

// String is the kind as a message says it, with its article.
func (k kind) String() string {
	switch k {
	case kString:
		return "a string"
	case kInteger:
		return "an integer"
	case kFloat:
		return "a float"
	case kBool:
		return "a boolean"
	case kDatetime:
		return "an offset date-time"
	case kDatetimeLocal:
		return "a local date-time"
	case kDateLocal:
		return "a local date"
	case kTimeLocal:
		return "a local time"
	case kArray:
		return "an array"
	}
	return "a table"
}

// value is a parsed value, where it starts in the text.
type value struct {
	kind      kind
	line, col int
	str       string // a string; a date or time, as RFC 3339 writes it
	i         int64
	f         float64
	b         bool
	arr       []*value
	aot       bool // an array of tables, from [[headers]]: one may be appended
	tbl       *table
}

// table is a table's keys, in the order they came, and how it was made,
// which says what may add to it.
type table struct {
	keys    []string
	m       map[string]*value
	header  bool // made by a [header], naming it or a table under it
	defined bool // named by a [header]: no other may
	dotted  bool // made by a dotted key: no [header] may name it
	inline  bool // an inline table, or in one: closed
}

func newTable() *table { return &table{m: map[string]*value{}} }

func (t *table) add(k string, v *value) {
	t.keys = append(t.keys, k)
	t.m[k] = v
}

// seal closes t and every table in it, as an inline table's are.
func (t *table) seal() {
	t.inline = true
	for _, v := range t.m {
		if v.kind == kTable {
			v.tbl.seal()
		}
	}
}

// key is a part of a key, where it is.
type key struct {
	name      string
	line, col int
}

// parser reads a document, byte by byte (the text is valid UTF-8, so a
// byte below 0x80 is a character).
type parser struct {
	src       []byte
	pos       int
	line      int
	lineStart int
}

// fail stops the parse with an error at the current position.
func (p *parser) fail(format string, args ...any) {
	p.failAt(p.line, p.col(), format, args...)
}

func (p *parser) failAt(line, col int, format string, args ...any) {
	panic(&Error{Line: line, Col: col, Msg: fmt.Sprintf(format, args...)})
}

// col is the current column, in characters, from 1.
func (p *parser) col() int { return utf8.RuneCount(p.src[p.lineStart:p.pos]) + 1 }

func (p *parser) eof() bool { return p.pos >= len(p.src) }

// peek is the current byte, 0 at the end.
func (p *parser) peek() byte {
	if p.pos < len(p.src) {
		return p.src[p.pos]
	}
	return 0
}

func (p *parser) peekAt(n int) byte {
	if p.pos+n < len(p.src) {
		return p.src[p.pos+n]
	}
	return 0
}

func (p *parser) has(s string) bool { return strings.HasPrefix(string(p.src[p.pos:]), s) }

// next consumes a byte.
func (p *parser) next() byte {
	c := p.src[p.pos]
	p.pos++
	if c == '\n' {
		p.line++
		p.lineStart = p.pos
	}
	return c
}

func (p *parser) skip(n int) {
	for range n {
		p.next()
	}
}

// found is the current character, for a message.
func (p *parser) found() string {
	if p.eof() {
		return "the end of the document"
	}
	r, _ := utf8.DecodeRune(p.src[p.pos:])
	switch r {
	case '\n', '\r':
		return "the end of the line"
	}
	return strconv.QuoteRune(r)
}

func (p *parser) expect(c byte, what string) {
	if p.peek() != c || p.eof() {
		p.fail("expected %s, found %s", what, p.found())
	}
	p.next()
}

func (p *parser) skipSpace() {
	for p.peek() == ' ' || p.peek() == '\t' {
		p.next()
	}
}

// comment consumes a comment, from its #, to the end of its line.
func (p *parser) comment() {
	p.next()
	for !p.eof() {
		c := p.peek()
		if c == '\n' || c == '\r' && p.peekAt(1) == '\n' {
			return
		}
		if isControl(c) {
			p.fail("a control character (%s) in a comment", p.found())
		}
		p.next()
	}
}

// newline consumes a line's end: LF or CRLF.
func (p *parser) newline() {
	if p.peek() == '\r' {
		if p.peekAt(1) != '\n' {
			p.fail("a carriage return not followed by a newline")
		}
		p.next()
	}
	p.next()
}

func (p *parser) atNewline() bool {
	return p.peek() == '\n' || p.peek() == '\r'
}

// endLine consumes what may end a line after a key/value or a header:
// spaces, a comment, the newline.
func (p *parser) endLine(after string) {
	p.skipSpace()
	if p.peek() == '#' {
		p.comment()
	}
	if p.eof() {
		return
	}
	if !p.atNewline() {
		p.fail("expected the end of the line after %s, found %s", after, p.found())
	}
	p.newline()
}

// skipBlank consumes spaces, comments and newlines: what may come
// between an array's values.
func (p *parser) skipBlank() {
	for {
		switch p.peek() {
		case ' ', '\t':
			p.next()
		case '#':
			p.comment()
		case '\n', '\r':
			p.newline()
		default:
			return
		}
	}
}

// isControl reports whether c is a control character TOML allows in no
// string or comment: those but the tab, newline included.
func isControl(c byte) bool { return c < 0x20 && c != '\t' || c == 0x7f }

func isBare(c byte) bool {
	return c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z' || c >= '0' && c <= '9' || c == '_' || c == '-'
}

// parse reads a document into its root table.
func parse(src []byte) (root *table, err error) {
	p := &parser{src: src, line: 1}
	defer func() {
		if r := recover(); r != nil {
			e, ok := r.(*Error)
			if !ok {
				panic(r)
			}
			root, err = nil, e
		}
	}()
	if !utf8.Valid(src) {
		for i := 0; i < len(src); {
			r, n := utf8.DecodeRune(src[i:])
			if r == utf8.RuneError && n <= 1 {
				for p.pos < i {
					p.next()
				}
				p.fail("not UTF-8")
			}
			i += n
		}
	}
	if len(src) >= 3 && string(src[:3]) == "\ufeff" {
		p.pos, p.lineStart = 3, 3 // a byte order mark
	}
	root = newTable()
	cur := root
	for {
		p.skipSpace()
		if p.eof() {
			return root, nil
		}
		switch c := p.peek(); {
		case c == '#':
			p.comment()
			if !p.eof() {
				p.newline()
			}
		case c == '\n' || c == '\r':
			p.newline()
		case c == '[':
			cur = p.header(root)
			p.endLine("a table's header")
		default:
			p.keyValue(cur)
			p.endLine("a value")
		}
	}
}

// key reads a key: its parts, dotted.
func (p *parser) key() []key {
	var ks []key
	for {
		line, col := p.line, p.col()
		var name string
		switch c := p.peek(); {
		case c == '"':
			if p.has(`"""`) {
				p.fail("a multi-line string can't be a key")
			}
			name = p.basicString()
		case c == '\'':
			if p.has("'''") {
				p.fail("a multi-line string can't be a key")
			}
			name = p.literalString()
		case isBare(c) && !p.eof():
			start := p.pos
			for !p.eof() && isBare(p.peek()) {
				p.next()
			}
			name = string(p.src[start:p.pos])
		default:
			p.fail("expected a key, found %s", p.found())
		}
		ks = append(ks, key{name, line, col})
		p.skipSpace()
		if p.peek() != '.' {
			return ks
		}
		p.next()
		p.skipSpace()
	}
}

// keyPath is ks as a message names it.
func keyPath(ks []key) string {
	var b strings.Builder
	for i, k := range ks {
		if i > 0 {
			b.WriteByte('.')
		}
		b.WriteString(quoteKey(k.name))
	}
	return b.String()
}

// keyValue reads a key/value into t.
func (p *parser) keyValue(t *table) {
	ks := p.key()
	p.expect('=', "= after a key")
	p.skipSpace()
	v := p.value()
	assign(p, t, ks, v)
}

// assign sets ks to v in t, making the tables its dots go through.
func assign(p *parser, t *table, ks []key, v *value) {
	for i, k := range ks[:len(ks)-1] {
		e, ok := t.m[k.name]
		if !ok {
			nt := newTable()
			nt.dotted = true
			t.add(k.name, &value{kind: kTable, line: k.line, col: k.col, tbl: nt})
			t = nt
			continue
		}
		switch {
		case e.kind != kTable:
			p.failAt(k.line, k.col, "%s is %s, not a table", keyPath(ks[:i+1]), e.kind)
		case e.tbl.inline:
			p.failAt(k.line, k.col, "%s is an inline table, which nothing may add to", keyPath(ks[:i+1]))
		case e.tbl.header:
			p.failAt(k.line, k.col, "%s has a [header]: a dotted key may not add to it", keyPath(ks[:i+1]))
		}
		t = e.tbl
	}
	k := ks[len(ks)-1]
	if _, ok := t.m[k.name]; ok {
		p.failAt(k.line, k.col, "%s is defined twice", keyPath(ks))
	}
	t.add(k.name, v)
}

// header reads a [table] or [[array of tables]] header, and is the table
// its key/values go to.
func (p *parser) header(root *table) *table {
	line, col := p.line, p.col()
	p.next()
	aot := p.peek() == '['
	if aot {
		p.next()
	}
	p.skipSpace()
	ks := p.key()
	if aot {
		p.expect(']', "]] after an array of tables' name")
		if p.peek() != ']' {
			p.fail("expected ]] after an array of tables' name, found %s", p.found())
		}
		p.next()
	} else {
		p.expect(']', "] after a table's name")
	}
	t := root
	for i, k := range ks[:len(ks)-1] {
		e, ok := t.m[k.name]
		if !ok {
			nt := newTable()
			nt.header = true
			t.add(k.name, &value{kind: kTable, line: k.line, col: k.col, tbl: nt})
			t = nt
			continue
		}
		switch {
		case e.kind == kTable && e.tbl.inline:
			p.failAt(k.line, k.col, "%s is an inline table, which nothing may add to", keyPath(ks[:i+1]))
		case e.kind == kTable:
			t = e.tbl
		case e.kind == kArray && e.aot:
			t = e.arr[len(e.arr)-1].tbl
		default:
			p.failAt(k.line, k.col, "%s is %s, not a table", keyPath(ks[:i+1]), e.kind)
		}
	}
	k := ks[len(ks)-1]
	e, ok := t.m[k.name]
	nt := newTable()
	nt.header, nt.defined = true, true
	if aot {
		el := &value{kind: kTable, line: line, col: col, tbl: nt}
		switch {
		case !ok:
			t.add(k.name, &value{kind: kArray, line: line, col: col, aot: true, arr: []*value{el}})
		case e.kind == kArray && e.aot:
			e.arr = append(e.arr, el)
		case e.kind == kArray:
			p.failAt(k.line, k.col, "%s is a static array: [[%s]] may not add to it", keyPath(ks), keyPath(ks))
		default:
			p.failAt(k.line, k.col, "%s is %s, not an array of tables", keyPath(ks), e.kind)
		}
		return nt
	}
	if !ok {
		t.add(k.name, &value{kind: kTable, line: line, col: col, tbl: nt})
		return nt
	}
	switch {
	case e.kind != kTable:
		p.failAt(k.line, k.col, "%s is %s, not a table", keyPath(ks), e.kind)
	case e.tbl.inline:
		p.failAt(k.line, k.col, "%s is an inline table, which nothing may add to", keyPath(ks))
	case e.tbl.dotted:
		p.failAt(k.line, k.col, "%s was made by dotted keys: a [header] may not name it", keyPath(ks))
	case e.tbl.defined:
		p.failAt(k.line, k.col, "table %s is defined twice", keyPath(ks))
	}
	e.tbl.defined = true
	return e.tbl
}

// value reads a value.
func (p *parser) value() *value {
	v := &value{line: p.line, col: p.col()}
	switch c := p.peek(); {
	case p.eof() || p.atNewline():
		p.fail("expected a value, found %s", p.found())
	case c == '"':
		v.kind = kString
		if p.has(`"""`) {
			v.str = p.multiBasicString()
		} else {
			v.str = p.basicString()
		}
	case c == '\'':
		v.kind = kString
		if p.has("'''") {
			v.str = p.multiLiteralString()
		} else {
			v.str = p.literalString()
		}
	case c == '[':
		p.array(v)
	case c == '{':
		p.inlineTable(v)
	case c == 't' || c == 'f':
		w := "false"
		if c == 't' {
			w = "true"
		}
		if !p.has(w) || isBare(p.peekAt(len(w))) {
			p.fail("expected a value, found %s (a string needs quotes)", p.word())
		}
		p.skip(len(w))
		v.kind, v.b = kBool, c == 't'
	case c == '+' || c == '-' || c == 'i' || c == 'n' || c >= '0' && c <= '9':
		p.scalar(v)
	default:
		p.fail("expected a value, found %s", p.found())
	}
	return v
}

// word is the bare word at the current position, quoted, for a message.
func (p *parser) word() string {
	end := p.pos
	for end < len(p.src) && isBare(p.src[end]) {
		end++
	}
	if end == p.pos {
		return p.found()
	}
	return strconv.Quote(string(p.src[p.pos:end]))
}

func isScalarChar(c byte) bool {
	return isBare(c) || c == '+' || c == '.' || c == ':'
}

// scalar reads a number, a date or a time.
func (p *parser) scalar(v *value) {
	line, col := p.line, p.col()
	start := p.pos
	for !p.eof() && isScalarChar(p.peek()) {
		p.next()
	}
	tok := string(p.src[start:p.pos])
	// A date-time may have a space for its T: 1979-05-27 07:32:00.
	if isDate(tok) && p.peek() == ' ' && isDigit(p.peekAt(1)) && isDigit(p.peekAt(2)) && p.peekAt(3) == ':' {
		p.next()
		for !p.eof() && isScalarChar(p.peek()) {
			p.next()
		}
		tok = string(p.src[start:p.pos])
	}
	fail := func(format string, args ...any) { p.failAt(line, col, format, args...) }
	if len(tok) >= 10 && tok[4] == '-' && isDigit(tok[0]) || len(tok) >= 3 && tok[2] == ':' && isDigit(tok[0]) {
		k, s, msg := datetime(tok)
		if msg != "" {
			fail("%s: %s", strconv.Quote(tok), msg)
		}
		v.kind, v.str = k, s
		return
	}
	switch ok, msg := number(v, tok); {
	case !ok:
		fail("%s isn't a value (a string needs quotes)", strconv.Quote(tok))
	case msg != "":
		fail("%s: %s", strconv.Quote(tok), msg)
	}
}

func isDigit(c byte) bool { return c >= '0' && c <= '9' }
func isHex(c byte) bool   { return isDigit(c) || c >= 'a' && c <= 'f' || c >= 'A' && c <= 'F' }
func isOct(c byte) bool   { return c >= '0' && c <= '7' }
func isBin(c byte) bool   { return c == '0' || c == '1' }

func isDate(s string) bool {
	return len(s) == 10 && s[4] == '-' && s[7] == '-' && allDigits(s[:4]) && allDigits(s[5:7]) && allDigits(s[8:])
}

func allDigits(s string) bool {
	for i := range len(s) {
		if !isDigit(s[i]) {
			return false
		}
	}
	return s != ""
}

// digits is s without its underscores, if s is digits with each
// underscore between two of them.
func digits(s string, ok func(byte) bool) (string, bool) {
	if s == "" || s[0] == '_' || s[len(s)-1] == '_' || strings.Contains(s, "__") {
		return "", false
	}
	for i := range len(s) {
		if s[i] != '_' && !ok(s[i]) {
			return "", false
		}
	}
	return strings.ReplaceAll(s, "_", ""), true
}

// number reads tok as an integer or a float into v. It's false if tok
// isn't a number at all; of a number TOML forbids (a leading zero, out
// of range) msg says what's wrong.
func number(v *value, tok string) (ok bool, msg string) {
	bad := func(msg string) (bool, string) { return true, msg }
	s, sign := tok, ""
	if s != "" && (s[0] == '+' || s[0] == '-') {
		sign, s = s[:1], s[1:]
	}
	switch s {
	case "inf":
		v.kind, v.f = kFloat, math.Inf(1)
		if sign == "-" {
			v.f = math.Inf(-1)
		}
		return true, ""
	case "nan":
		v.kind, v.f = kFloat, math.NaN()
		return true, ""
	}
	if len(s) > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'o' || s[1] == 'b') {
		base, isDigitOf := 16, isHex
		switch s[1] {
		case 'o':
			base, isDigitOf = 8, isOct
		case 'b':
			base, isDigitOf = 2, isBin
		}
		d, valid := digits(s[2:], isDigitOf)
		if !valid {
			return false, ""
		}
		if sign != "" {
			return bad("a hexadecimal, octal or binary integer has no sign")
		}
		n, err := strconv.ParseInt(d, base, 64)
		if err != nil {
			return bad("out of a 64-bit integer's range")
		}
		v.kind, v.i = kInteger, n
		return true, ""
	}
	// A decimal integer or a float: an integer part, then a fraction, an
	// exponent or both.
	intPart, rest := s, ""
	if i := strings.IndexAny(s, ".eE"); i >= 0 {
		intPart, rest = s[:i], s[i:]
	}
	ip, valid := digits(intPart, isDigit)
	if !valid {
		return false, ""
	}
	if len(ip) > 1 && ip[0] == '0' {
		return bad("a leading zero")
	}
	if rest == "" {
		n, err := strconv.ParseInt(sign+ip, 10, 64)
		if err != nil {
			return bad("out of a 64-bit integer's range")
		}
		v.kind, v.i = kInteger, n
		return true, ""
	}
	clean := sign + ip
	if rest[0] == '.' {
		frac := rest[1:]
		rest = ""
		if i := strings.IndexAny(frac, "eE"); i >= 0 {
			frac, rest = frac[:i], frac[i:]
		}
		f, valid := digits(frac, isDigit)
		if !valid {
			return false, ""
		}
		clean += "." + f
	}
	if rest != "" {
		exp := rest[1:]
		esign := ""
		if exp != "" && (exp[0] == '+' || exp[0] == '-') {
			esign, exp = exp[:1], exp[1:]
		}
		e, valid := digits(exp, isDigit)
		if !valid {
			return false, ""
		}
		clean += "e" + esign + e
	}
	f, err := strconv.ParseFloat(clean, 64)
	if err != nil && math.IsInf(f, 0) {
		return bad("out of a 64-bit float's range")
	}
	v.kind, v.f = kFloat, f
	return true, ""
}

// datetime reads tok as a date, a time or both: its kind, its RFC 3339
// form (a T between date and time, Z upper case), or what's wrong.
func datetime(tok string) (kind, string, string) {
	if len(tok) >= 10 && tok[4] == '-' {
		date := tok[:10]
		if !isDate(date) {
			return 0, "", "not a date (YYYY-MM-DD)"
		}
		y, _ := strconv.Atoi(date[:4])
		m, _ := strconv.Atoi(date[5:7])
		d, _ := strconv.Atoi(date[8:])
		if m < 1 || m > 12 {
			return 0, "", "no such month"
		}
		if d < 1 || d > daysIn(y, m) {
			return 0, "", "no such day"
		}
		rest := tok[10:]
		if rest == "" {
			return kDateLocal, date, ""
		}
		if rest[0] != 'T' && rest[0] != 't' && rest[0] != ' ' {
			return 0, "", "expected T between the date and the time"
		}
		t, offset, msg := timeOfDay(rest[1:])
		if msg != "" {
			return 0, "", msg
		}
		if offset == "" {
			return kDatetimeLocal, date + "T" + t, ""
		}
		return kDatetime, date + "T" + t + offset, ""
	}
	t, offset, msg := timeOfDay(tok)
	if msg != "" {
		return 0, "", msg
	}
	if offset != "" {
		return 0, "", "a time without a date has no offset"
	}
	return kTimeLocal, t, ""
}

// timeOfDay reads HH:MM:SS, its fraction and its offset (Z, +HH:MM).
func timeOfDay(s string) (t, offset, msg string) {
	if len(s) < 8 || s[2] != ':' || s[5] != ':' || !allDigits(s[:2]) || !allDigits(s[3:5]) || !allDigits(s[6:8]) {
		return "", "", "not a time (HH:MM:SS, with seconds)"
	}
	h, _ := strconv.Atoi(s[:2])
	m, _ := strconv.Atoi(s[3:5])
	sec, _ := strconv.Atoi(s[6:8])
	if h > 23 || m > 59 || sec > 60 {
		return "", "", "no such time"
	}
	n := 8
	if n < len(s) && s[n] == '.' {
		n++
		start := n
		for n < len(s) && isDigit(s[n]) {
			n++
		}
		if n == start {
			return "", "", "a fraction of a second needs digits"
		}
	}
	t, rest := s[:n], s[n:]
	switch {
	case rest == "":
		return t, "", ""
	case rest == "Z" || rest == "z":
		return t, "Z", ""
	case len(rest) == 6 && (rest[0] == '+' || rest[0] == '-') && rest[3] == ':' && allDigits(rest[1:3]) && allDigits(rest[4:]):
		oh, _ := strconv.Atoi(rest[1:3])
		om, _ := strconv.Atoi(rest[4:])
		if oh > 23 || om > 59 {
			return "", "", "no such offset"
		}
		return t, rest, ""
	}
	return "", "", "expected an offset (Z, +HH:MM) or nothing after the time"
}

func daysIn(y, m int) int {
	switch m {
	case 2:
		if y%4 == 0 && (y%100 != 0 || y%400 == 0) {
			return 29
		}
		return 28
	case 4, 6, 9, 11:
		return 30
	}
	return 31
}

// array reads an array.
func (p *parser) array(v *value) {
	v.kind = kArray
	p.next()
	for {
		p.skipBlank()
		if p.peek() == ']' {
			p.next()
			return
		}
		if p.eof() {
			p.fail("an array not closed with ]")
		}
		v.arr = append(v.arr, p.value())
		p.skipBlank()
		switch {
		case p.peek() == ',':
			p.next()
		case p.peek() == ']':
			p.next()
			return
		case p.eof():
			p.fail("an array not closed with ]")
		default:
			p.fail("expected , or ] after an array's value, found %s", p.found())
		}
	}
}

// inlineTable reads an inline table: on one line, no comma after its
// last key/value.
func (p *parser) inlineTable(v *value) {
	v.kind, v.tbl = kTable, newTable()
	p.next()
	p.skipSpace()
	if p.peek() == '}' {
		p.next()
		v.tbl.seal()
		return
	}
	for {
		p.skipSpace()
		if p.atNewline() {
			p.fail("a newline in an inline table")
		}
		ks := p.key()
		p.expect('=', "= after a key")
		p.skipSpace()
		assign(p, v.tbl, ks, p.value())
		p.skipSpace()
		switch {
		case p.peek() == ',':
			p.next()
			p.skipSpace()
			if p.peek() == '}' {
				p.fail("a comma after an inline table's last value")
			}
		case p.peek() == '}':
			p.next()
			v.tbl.seal()
			return
		case p.atNewline():
			p.fail("a newline in an inline table")
		default:
			p.fail("expected , or } after an inline table's value, found %s", p.found())
		}
	}
}

// basicString reads a "string", its escapes replaced.
func (p *parser) basicString() string {
	p.next()
	var b []byte
	for {
		switch c := p.peek(); {
		case p.eof():
			p.fail(`a string not closed with "`)
		case c == '"':
			p.next()
			return string(b)
		case c == '\\':
			b = p.escape(b)
		case c == '\n' || c == '\r':
			p.fail(`a newline in a string (a multi-line one is in """)`)
		case isControl(c):
			p.fail("a control character (%s) in a string: escape it", p.found())
		default:
			b = append(b, p.next())
		}
	}
}

// multiBasicString reads a """multi-line string""".
func (p *parser) multiBasicString() string {
	p.skip(3)
	if p.atNewline() {
		p.newline()
	}
	var b []byte
	for {
		switch c := p.peek(); {
		case p.eof():
			p.fail(`a multi-line string not closed with """`)
		case c == '"' && p.has(`"""`):
			n := 0
			for p.peekAt(n) == '"' {
				n++
			}
			if n > 5 {
				p.fail(`too many quotes closing a multi-line string`)
			}
			b = append(b, strings.Repeat(`"`, n-3)...)
			p.skip(n)
			return string(b)
		case c == '\\':
			// A backslash ending a line: the newline and the whitespace
			// after it are left out.
			i := 1
			for p.peekAt(i) == ' ' || p.peekAt(i) == '\t' {
				i++
			}
			if p.peekAt(i) == '\n' || p.peekAt(i) == '\r' && p.peekAt(i+1) == '\n' {
				p.skip(i)
				for p.peek() == ' ' || p.peek() == '\t' || p.atNewline() {
					if p.atNewline() {
						p.newline()
					} else {
						p.next()
					}
				}
				continue
			}
			b = p.escape(b)
		case c == '\n' || c == '\r':
			p.newline()
			b = append(b, '\n')
		case isControl(c):
			p.fail("a control character (%s) in a string: escape it", p.found())
		default:
			b = append(b, p.next())
		}
	}
}

// escape reads an escape, from its backslash, onto b.
func (p *parser) escape(b []byte) []byte {
	line, col := p.line, p.col()
	p.next()
	if p.eof() {
		p.fail(`a string not closed with "`)
	}
	c := p.peek()
	n := 0
	switch c {
	case 'b', 't', 'n', 'f', 'r', '"', '\\':
		p.next()
		return append(b, map[byte]byte{'b': '\b', 't': '\t', 'n': '\n', 'f': '\f', 'r': '\r', '"': '"', '\\': '\\'}[c])
	case 'u':
		n = 4
	case 'U':
		n = 8
	default:
		p.failAt(line, col, "no escape \\%s (\\b \\t \\n \\f \\r \\\" \\\\ \\uXXXX \\UXXXXXXXX)", strings.Trim(p.found(), "'"))
	}
	p.next()
	var r uint32
	for range n {
		d := p.peek()
		if !isHex(d) || p.eof() {
			p.failAt(line, col, "\\%c needs %d hexadecimal digits", c, n)
		}
		p.next()
		v, _ := strconv.ParseUint(string(d), 16, 8)
		r = r<<4 | uint32(v)
	}
	if r > utf8.MaxRune || r >= 0xd800 && r <= 0xdfff {
		p.failAt(line, col, "\\%c%0*X isn't a Unicode scalar value", c, n, r)
	}
	return utf8.AppendRune(b, rune(r))
}

// literalString reads a 'literal string': no escapes.
func (p *parser) literalString() string {
	p.next()
	start := p.pos
	for {
		switch c := p.peek(); {
		case p.eof():
			p.fail("a string not closed with '")
		case c == '\'':
			s := string(p.src[start:p.pos])
			p.next()
			return s
		case c == '\n' || c == '\r':
			p.fail("a newline in a string (a multi-line one is in ''')")
		case isControl(c):
			p.fail("a control character (%s) in a string", p.found())
		default:
			p.next()
		}
	}
}

// multiLiteralString reads a ”'multi-line literal string”'.
func (p *parser) multiLiteralString() string {
	p.skip(3)
	if p.atNewline() {
		p.newline()
	}
	var b []byte
	for {
		switch c := p.peek(); {
		case p.eof():
			p.fail("a multi-line string not closed with '''")
		case c == '\'' && p.has("'''"):
			n := 0
			for p.peekAt(n) == '\'' {
				n++
			}
			if n > 5 {
				p.fail("too many quotes closing a multi-line string")
			}
			b = append(b, strings.Repeat("'", n-3)...)
			p.skip(n)
			return string(b)
		case c == '\n' || c == '\r':
			p.newline()
			b = append(b, '\n')
		case isControl(c):
			p.fail("a control character (%s) in a string", p.found())
		default:
			b = append(b, p.next())
		}
	}
}
