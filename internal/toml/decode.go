package toml

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"reflect"
	"sort"
	"strconv"
	"strings"
	"unicode/utf8"
)

// decode stores the tree in v, through its JSON.
func decode(root *table, v any) error {
	w := &jsonWriter{}
	if err := w.table(root, ""); err != nil {
		return err
	}
	if err := json.Unmarshal(w.buf, v); err != nil {
		return w.explain(err)
	}
	return nil
}

// spot is where a value's JSON starts, and the value.
type spot struct {
	off  int
	v    *value
	path string
}

// jsonWriter writes a tree as JSON, noting where each value starts, so
// that a decoding error can say where in the TOML it is.
type jsonWriter struct {
	buf   []byte
	spots []spot
}

func (w *jsonWriter) value(v *value, path string) error {
	w.spots = append(w.spots, spot{len(w.buf), v, path})
	switch v.kind {
	case kString, kDatetime, kDatetimeLocal, kDateLocal, kTimeLocal:
		w.buf = appendJSONString(w.buf, v.str)
	case kInteger:
		w.buf = strconv.AppendInt(w.buf, v.i, 10)
	case kFloat:
		if math.IsInf(v.f, 0) || math.IsNaN(v.f) {
			return &Error{v.line, v.col, fmt.Sprintf("%s: %v has no JSON form, so no Go value", pathName(path), v.f)}
		}
		n := len(w.buf)
		w.buf = strconv.AppendFloat(w.buf, v.f, 'g', -1, 64)
		if !strings.ContainsAny(string(w.buf[n:]), ".e") {
			w.buf = append(w.buf, ".0"...) // a float, which an int field refuses
		}
	case kBool:
		w.buf = strconv.AppendBool(w.buf, v.b)
	case kArray:
		w.buf = append(w.buf, '[')
		for i, e := range v.arr {
			if i > 0 {
				w.buf = append(w.buf, ',')
			}
			if err := w.value(e, fmt.Sprintf("%s[%d]", path, i)); err != nil {
				return err
			}
		}
		w.buf = append(w.buf, ']')
	case kTable:
		return w.table(v.tbl, path)
	}
	return nil
}

func (w *jsonWriter) table(t *table, path string) error {
	w.buf = append(w.buf, '{')
	for i, k := range t.keys {
		if i > 0 {
			w.buf = append(w.buf, ',')
		}
		w.buf = appendJSONString(w.buf, k)
		w.buf = append(w.buf, ':')
		p := quoteKey(k)
		if path != "" {
			p = path + "." + p
		}
		if err := w.value(t.m[k], p); err != nil {
			return err
		}
	}
	w.buf = append(w.buf, '}')
	return nil
}

// explain is a decoding error as where in the document it is.
func (w *jsonWriter) explain(err error) error {
	var te *json.UnmarshalTypeError
	if !errors.As(err, &te) {
		return err
	}
	// The value the decoder was at: the last to start before the offset
	// it reports, which is past the value's start.
	i := sort.Search(len(w.spots), func(i int) bool { return int64(w.spots[i].off) >= te.Offset }) - 1
	if i < 0 {
		return err
	}
	s := w.spots[i]
	want := describeType(te.Type)
	if s.v.kind == kInteger && isNumber(te.Type) {
		return &Error{s.v.line, s.v.col, fmt.Sprintf("%s: %s is out of range for %s", pathName(s.path), strings.TrimPrefix(te.Value, "number "), te.Type)}
	}
	return &Error{s.v.line, s.v.col, fmt.Sprintf("%s: want %s, not %s", pathName(s.path), want, s.v.kind)}
}

func pathName(path string) string {
	if path == "" {
		return "the document"
	}
	return path
}

func isNumber(t reflect.Type) bool {
	switch t.Kind() {
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64,
		reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr,
		reflect.Float32, reflect.Float64:
		return true
	}
	return false
}

// describeType is what a Go type takes, as a message says it.
func describeType(t reflect.Type) string {
	switch t.Kind() {
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64,
		reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		return "an integer"
	case reflect.Float32, reflect.Float64:
		return "a number"
	case reflect.String:
		return "a string"
	case reflect.Bool:
		return "a boolean"
	case reflect.Slice, reflect.Array:
		return "an array"
	case reflect.Struct, reflect.Map:
		return "a table"
	}
	return t.String()
}

// appendJSONString appends s as a JSON string.
func appendJSONString(b []byte, s string) []byte {
	b = append(b, '"')
	for _, r := range s {
		switch {
		case r == '"' || r == '\\':
			b = append(b, '\\', byte(r))
		case r < 0x20:
			b = fmt.Appendf(b, `\u%04x`, r)
		default:
			b = utf8.AppendRune(b, r)
		}
	}
	return append(b, '"')
}
