// Package toml reads and writes TOML 1.0 (https://toml.io/en/v1.0.0),
// the format of Asuka's manifests (asuka.toml, asuka-plugin.toml),
// through encoding/json: a value's json struct tags name its keys, and its
// UnmarshalJSON and MarshalJSON methods are used, so a type reads the
// same from JSON and from TOML.
//
// Unmarshal parses the document, rejecting what TOML forbids (a key or
// a table defined twice, an invalid escape, a leading zero...), then
// decodes it as encoding/json decodes the JSON of the same tree: a table
// is an object, an array an array, a date or a time its RFC 3339 string.
// An error says where, as line:col: what. JSON has no infinity and no
// NaN: a float that is one has no Go value here.
//
// Marshal writes the JSON encoding/json makes of a value as TOML, keeping
// its keys' order: scalars and arrays of them as key = value, objects as
// [tables], arrays of objects as [[arrays of tables]], objects within
// other arrays as inline tables. A JSON null is left out.
package toml

import (
	"fmt"
)

// An Error is what's wrong with a document, and where.
type Error struct {
	// Line and Col are where, from 1; Col counts characters.
	Line, Col int
	Msg       string
}

func (e *Error) Error() string { return fmt.Sprintf("%d:%d: %s", e.Line, e.Col, e.Msg) }

// Unmarshal parses the TOML document data and stores what it says in the
// value v points to, as encoding/json's Unmarshal would the same tree in
// JSON.
func Unmarshal(data []byte, v any) error {
	root, err := parse(data)
	if err != nil {
		return err
	}
	return decode(root, v)
}

// Valid reports whether data is a TOML document, and if not, why.
func Valid(data []byte) error {
	_, err := parse(data)
	return err
}
