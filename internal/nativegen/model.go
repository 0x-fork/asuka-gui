// Package nativegen reads a package's native declarations (its Asuka's
// native interfaces and native components: LoadAsuka) and writes the code
// that connects them to each platform: the Swift, Kotlin, JavaScript and
// C its implementations are compiled with. The Asuka compiler makes the
// calling side. One model makes them all, so the sides always agree on
// the wire (core/src/native's format).
package nativegen

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"go/token"
	"os"
	"strings"
)

// Kind is a type's kind on the wire.
type Kind int

const (
	Bool Kind = iota
	Int8
	Int16
	Int32
	Int64
	Int // Go's int: 8 bytes
	Uint8
	Uint16
	Uint32
	Uint64
	Uint
	Float32
	Float64
	String
	Bytes
	Time
	Duration
	Slice
	Map // map[string]T
	Optional
	Struct
	Enum
	Object // a native interface of the package
	Func
	// ViewRef is one of the app's views (an Asuka Ref), as a parameter:
	// the u64 id its host knows it by (0: none), which each platform's
	// generated code turns into its view.
	ViewRef
)

// Type is a parameter's, a result's or a field's type.
type Type struct {
	Kind Kind
	Elem *Type     // Slice, Map, Optional
	Name string    // Struct, Enum, Object: the Go type's name
	Func *FuncType // Func
	// Go is the type as the package's Go writes it.
	Go string
}

// FuncType is a callback: its parameters, and its result (a question)
// or none (a notification).
type FuncType struct {
	Params []*Param
	Result *Type
}

// Param is a parameter.
type Param struct {
	Name string
	Type *Type
}

// Method is a native interface's method.
type Method struct {
	Name       string
	Index      uint32 // from 1, in declaration order
	Context    bool   // takes a context.Context first: asynchronous
	Params     []*Param
	Result     *Type // nil: none
	Error      bool  // returns an error
	MainThread bool
	Close      bool // Close() error: releases the object after
	Doc        string
}

// Interface is a //asuka:native interface.
type Interface struct {
	Name       string
	Index      uint32 // from 0, in declaration order
	Methods    []*Method
	MainThread bool
	Doc        string
}

// Field is a struct's field, or a view's prop or event.
type Field struct {
	Name string // Go's
	Type *Type
	// Accessor: a view's prop given as a signal.Accessor of Type.
	Accessor bool
	Doc      string
}

// StructDecl is a struct of the package that crosses.
type StructDecl struct {
	Name   string
	Fields []*Field
	Doc    string
}

// EnumValue is one of an enum's constants.
type EnumValue struct {
	Name  string // Go's constant
	Value string // its value, as Go writes it (a quoted string, or a number)
}

// EnumDecl is a named type of the package over a string or an integer, with
// its constants.
type EnumDecl struct {
	Name   string
	Basic  Kind // String or an integer kind
	Values []EnumValue
	Doc    string
}

// ErrorDecl is a declared error: a case of the package's enum ...: Error.
type ErrorDecl struct {
	Name    string // ErrLowPower
	Message string
	Index   uint32
}

// View is a //asuka:view props struct.
type View struct {
	Name   string // the view's: the struct's name without Props
	Props  []*Field
	Events []*Field // func fields: Type.Func
	Style  bool     // has a Style ui.Style (or ui.Styler: a StyleFunc too) field
	Doc    string
}

// Package is a package's typed native code.
type Package struct {
	Path, Name, Dir string
	Interfaces      []*Interface
	Views           []*View
	Structs         []*StructDecl
	Enums           []*EnumDecl
	Errors          []*ErrorDecl
	// Hash is the declarations', which every generated file records.
	Hash string
	// ErrorName is the name of the package's one enum of declared errors
	// (an Asuka package's enum ... : Error), which the platforms' error
	// types are named after; "": the package's name and Error.
	ErrorName string
}

// declares reports whether the package declares a type of the name.
func (p *Package) declares(name string) bool {
	for _, s := range p.Structs {
		if s.Name == name {
			return true
		}
	}
	for _, e := range p.Enums {
		if e.Name == name {
			return true
		}
	}
	for _, it := range p.Interfaces {
		if it.Name == name {
			return true
		}
	}
	for _, v := range p.Views {
		if v.Name == name {
			return true
		}
	}
	return false
}

// Empty reports whether the package declares nothing native.
func (p *Package) Empty() bool { return len(p.Interfaces) == 0 && len(p.Views) == 0 }

// Error is a declaration Asuka can't connect, at its position.
type Error struct {
	Pos token.Position
	Msg string
}

func (e *Error) Error() string { return fmt.Sprintf("%s: %s", e.Pos, e.Msg) }

// hash is what the declarations say, for generated files to record.
func (p *Package) hash() string {
	var b strings.Builder
	var typ func(t *Type) string
	typ = func(t *Type) string {
		if t == nil {
			return "-"
		}
		s := fmt.Sprintf("%d:%s", t.Kind, t.Name)
		if t.Elem != nil {
			s += "[" + typ(t.Elem) + "]"
		}
		if t.Func != nil {
			s += "("
			for _, p := range t.Func.Params {
				s += p.Name + " " + typ(p.Type) + ","
			}
			s += ")" + typ(t.Func.Result)
		}
		return s
	}
	fmt.Fprintln(&b, "v1", p.Path, p.Name)
	for _, it := range p.Interfaces {
		fmt.Fprintln(&b, "iface", it.Name, it.Index, it.MainThread)
		for _, m := range it.Methods {
			fmt.Fprint(&b, " method ", m.Name, m.Index, m.Context, m.Error, m.MainThread, m.Close, typ(m.Result))
			for _, p := range m.Params {
				fmt.Fprint(&b, " ", p.Name, " ", typ(p.Type))
			}
			fmt.Fprintln(&b, " //", m.Doc)
		}
		fmt.Fprintln(&b, "//", it.Doc)
	}
	for _, s := range p.Structs {
		fmt.Fprintln(&b, "struct", s.Name, s.Doc)
		for _, f := range s.Fields {
			fmt.Fprintln(&b, " ", f.Name, typ(f.Type), f.Doc)
		}
	}
	for _, e := range p.Enums {
		fmt.Fprintln(&b, "enum", e.Name, e.Basic, e.Values, e.Doc)
	}
	for _, e := range p.Errors {
		fmt.Fprintln(&b, "error", e.Name, e.Message)
	}
	for _, v := range p.Views {
		fmt.Fprintln(&b, "view", v.Name, v.Style, v.Doc)
		for _, f := range v.Props {
			fmt.Fprintln(&b, " prop", f.Name, typ(f.Type), f.Accessor, f.Doc)
		}
		for _, f := range v.Events {
			fmt.Fprintln(&b, " event", f.Name, typ(f.Type), f.Doc)
		}
	}
	sum := sha256.Sum256([]byte(b.String()))
	return hex.EncodeToString(sum[:])[:16]
}

// exists reports whether path exists.
func exists(path string) bool { _, err := os.Stat(path); return err == nil }
