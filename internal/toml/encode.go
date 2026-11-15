package toml

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"strconv"
	"strings"
	"unicode/utf8"
)

// Marshal is v as a TOML document: what encoding/json's Marshal makes of
// it, which must be an object (a struct, a map), in its keys' order.
func Marshal(v any) ([]byte, error) {
	data, err := json.Marshal(v)
	if err != nil {
		return nil, err
	}
	d := json.NewDecoder(bytes.NewReader(data))
	d.UseNumber()
	root, err := readJSON(d)
	if err != nil {
		return nil, err
	}
	if root.kind != 'o' {
		return nil, fmt.Errorf("toml: a document is a table, not %s", root.describe())
	}
	e := &encoder{}
	if err := e.body(nil, root); err != nil {
		return nil, err
	}
	return e.buf, nil
}

// node is a JSON value, its object's keys in order.
type node struct {
	kind byte // 'o' object, 'a' array, 's' string, 'n' number, 'b' boolean, 'z' null
	keys []string
	vals []*node
	arr  []*node
	s    string // a string, a number's text
	b    bool
}

func (n *node) describe() string {
	return map[byte]string{'o': "an object", 'a': "an array", 's': "a string", 'n': "a number", 'b': "a boolean", 'z': "null"}[n.kind]
}

func readJSON(d *json.Decoder) (*node, error) {
	t, err := d.Token()
	if err != nil {
		return nil, err
	}
	switch t := t.(type) {
	case json.Delim:
		if t == '[' {
			n := &node{kind: 'a'}
			for d.More() {
				e, err := readJSON(d)
				if err != nil {
					return nil, err
				}
				n.arr = append(n.arr, e)
			}
			_, err := d.Token()
			return n, err
		}
		n := &node{kind: 'o'}
		for d.More() {
			k, err := d.Token()
			if err != nil {
				return nil, err
			}
			v, err := readJSON(d)
			if err != nil {
				return nil, err
			}
			// A key given twice: the last value, where the first was,
			// as encoding/json reads it.
			if i := indexOf(n.keys, k.(string)); i >= 0 {
				n.vals[i] = v
				continue
			}
			n.keys = append(n.keys, k.(string))
			n.vals = append(n.vals, v)
		}
		_, err := d.Token()
		return n, err
	case string:
		return &node{kind: 's', s: t}, nil
	case json.Number:
		return &node{kind: 'n', s: t.String()}, nil
	case bool:
		return &node{kind: 'b', b: t}, nil
	case nil:
		return &node{kind: 'z'}, nil
	}
	return nil, io.ErrUnexpectedEOF
}

func indexOf(s []string, k string) int {
	for i, x := range s {
		if x == k {
			return i
		}
	}
	return -1
}

// encoder writes a document.
type encoder struct {
	buf []byte
}

// isTable reports whether n is written as a [table].
func isTable(n *node) bool { return n.kind == 'o' }

// isTables reports whether n is written as an [[array of tables]].
func isTables(n *node) bool {
	if n.kind != 'a' || len(n.arr) == 0 {
		return false
	}
	for _, e := range n.arr {
		if e.kind != 'o' {
			return false
		}
	}
	return true
}

// body writes t's key/values, then its tables, path being t's.
func (e *encoder) body(path []string, t *node) error {
	for i, k := range t.keys {
		v := t.vals[i]
		if v.kind == 'z' || isTable(v) || isTables(v) {
			continue
		}
		e.buf = append(e.buf, quoteKey(k)...)
		e.buf = append(e.buf, " = "...)
		s, err := inline(v, true, len(k)+3)
		if err != nil {
			return fmt.Errorf("toml: %s: %v", keyPathOf(append(path, k)), err)
		}
		e.buf = append(e.buf, s...)
		e.buf = append(e.buf, '\n')
	}
	for i, k := range t.keys {
		v := t.vals[i]
		p := append(path[:len(path):len(path)], k)
		switch {
		case isTable(v):
			// A table with only tables in it needs no header of its own.
			if hasKeyValues(v) || !hasTables(v) {
				e.header("[" + keyPathOf(p) + "]")
			}
			if err := e.body(p, v); err != nil {
				return err
			}
		case isTables(v):
			for _, el := range v.arr {
				e.header("[[" + keyPathOf(p) + "]]")
				if err := e.body(p, el); err != nil {
					return err
				}
			}
		}
	}
	return nil
}

func (e *encoder) header(h string) {
	if len(e.buf) > 0 {
		e.buf = append(e.buf, '\n')
	}
	e.buf = append(e.buf, h...)
	e.buf = append(e.buf, '\n')
}

func hasKeyValues(t *node) bool {
	for _, v := range t.vals {
		if v.kind != 'z' && !isTable(v) && !isTables(v) {
			return true
		}
	}
	return false
}

func hasTables(t *node) bool {
	for _, v := range t.vals {
		if isTable(v) || isTables(v) {
			return true
		}
	}
	return false
}

// keyPathOf is a table's name in its header.
func keyPathOf(path []string) string {
	q := make([]string, len(path))
	for i, k := range path {
		q[i] = quoteKey(k)
	}
	return strings.Join(q, ".")
}

// lineWidth is how long an array's line may be before its values go one
// to a line.
const lineWidth = 80

// inline is v as a value on a line: top (a key/value's own, not one in
// an array or an inline table) may spread an array over lines, indent
// being where its line's value starts.
func inline(v *node, top bool, indent int) (string, error) {
	switch v.kind {
	case 's':
		return quote(v.s), nil
	case 'b':
		return strconv.FormatBool(v.b), nil
	case 'n':
		if !strings.ContainsAny(v.s, ".eE") {
			if _, err := strconv.ParseInt(v.s, 10, 64); err != nil {
				return "", fmt.Errorf("%s is out of a TOML integer's range", v.s)
			}
		}
		return v.s, nil
	case 'z':
		return "", errors.New("TOML has no null")
	case 'o':
		var b strings.Builder
		b.WriteByte('{')
		n := 0
		for i, k := range v.keys {
			if v.vals[i].kind == 'z' {
				continue
			}
			s, err := inline(v.vals[i], false, 0)
			if err != nil {
				return "", fmt.Errorf("%s: %v", quoteKey(k), err)
			}
			if n > 0 {
				b.WriteString(", ")
			}
			n++
			b.WriteString(quoteKey(k) + " = " + s)
		}
		b.WriteByte('}')
		return b.String(), nil
	}
	// An array.
	els := make([]string, len(v.arr))
	width, tables := indent+2, false
	for i, el := range v.arr {
		s, err := inline(el, false, 0)
		if err != nil {
			return "", fmt.Errorf("[%d]: %v", i, err)
		}
		els[i] = s
		width += len(s) + 2
		tables = tables || el.kind == 'o'
	}
	if !top || len(els) < 2 || width <= lineWidth && !tables {
		return "[" + strings.Join(els, ", ") + "]", nil
	}
	return "[\n  " + strings.Join(els, ",\n  ") + ",\n]", nil
}

// quoteKey is k as a key: bare if it can be.
func quoteKey(k string) string {
	if k == "" {
		return `""`
	}
	for i := range len(k) {
		if !isBare(k[i]) {
			return quote(k)
		}
	}
	return k
}

// Quote is s as a TOML basic string, quoted, escaped.
func Quote(s string) string { return quote(s) }

// quote is s as a basic string.
func quote(s string) string {
	b := make([]byte, 0, len(s)+2)
	b = append(b, '"')
	for _, r := range s {
		switch r {
		case '"':
			b = append(b, `\"`...)
		case '\\':
			b = append(b, `\\`...)
		case '\b':
			b = append(b, `\b`...)
		case '\t':
			b = append(b, `\t`...)
		case '\n':
			b = append(b, `\n`...)
		case '\f':
			b = append(b, `\f`...)
		case '\r':
			b = append(b, `\r`...)
		default:
			if r < 0x20 || r == 0x7f {
				b = fmt.Appendf(b, `\u%04X`, r)
			} else {
				b = utf8.AppendRune(b, r)
			}
		}
	}
	return string(append(b, '"'))
}
