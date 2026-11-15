package nativegen

import (
	"fmt"
	"go/token"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/toml"
	"github.com/egoist/asuka/lang/syntax"
)

// LoadAsuka reads the native declarations of the Asuka package in dir
// (its .asuka files): native interfaces (a module's objects), native
// components (views), the structs and enums they use, and the errors
// they throw (an enum declared `: Error` whose cases' docs are their
// messages). Nil when it declares nothing native.
//
// On the wire an Asuka int is 8 bytes, a float 8, an enum its case's
// index in 8 bytes, a Duration its nanoseconds: what core/src/native
// encodes.
//
// A doc line @mainThread on an interface or a method makes the platform
// run it on the main thread; a method close() releases the object after.
func LoadAsuka(dir string) (*Package, error) {
	abs, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}
	paths, err := filepath.Glob(filepath.Join(abs, "*.asuka"))
	if err != nil {
		return nil, err
	}
	sort.Strings(paths)
	fset := syntax.NewFileSet()
	var files []*syntax.SourceFile
	for _, p := range paths {
		src, err := os.ReadFile(p)
		if err != nil {
			return nil, err
		}
		f, errs := syntax.ParseFile(fset, p, src)
		if len(errs) > 0 {
			return nil, errs[0]
		}
		files = append(files, f)
	}
	name := filepath.Base(abs)
	path := name
	if b, err := os.ReadFile(filepath.Join(abs, "asuka.toml")); err == nil {
		var m struct {
			Name string `json:"name"`
		}
		if err := toml.Unmarshal(b, &m); err == nil && m.Name != "" {
			path = m.Name
		}
	}
	l := &aloader{fset: fset, out: &Package{Path: path, Name: sanitizeName(name), Dir: abs, Asuka: true},
		structs: map[string]*syntax.StructDecl{}, enums: map[string]*syntax.EnumDecl{}, ifaces: map[string]bool{},
		doneS: map[string]*StructDecl{}, doneE: map[string]*EnumDecl{}}
	if err := l.load(files); err != nil {
		return nil, err
	}
	if l.out.Empty() {
		return nil, nil
	}
	l.out.Hash = l.out.hash()
	return l.out, nil
}

// sanitizeName makes a directory's name a package name: what the
// generated files and classes are named after.
func sanitizeName(s string) string {
	var b strings.Builder
	for _, r := range strings.ToLower(s) {
		if r >= 'a' && r <= 'z' || r >= '0' && r <= '9' {
			b.WriteRune(r)
		}
	}
	if b.Len() == 0 {
		return "native"
	}
	return b.String()
}

type aloader struct {
	fset    *syntax.FileSet
	out     *Package
	structs map[string]*syntax.StructDecl
	enums   map[string]*syntax.EnumDecl
	ifaces  map[string]bool
	doneS   map[string]*StructDecl
	doneE   map[string]*EnumDecl
}

func (l *aloader) errorf(n syntax.Node, format string, args ...any) error {
	pos := l.fset.Position(n.Range().Start)
	return &Error{Pos: token.Position{Filename: pos.Filename, Line: pos.Line, Column: pos.Column}, Msg: fmt.Sprintf(format, args...)}
}

func docOf(n syntax.Node) string {
	t := syntax.TriviaOf(n)
	if t == nil {
		return ""
	}
	var lines []string
	for _, l := range strings.Split(t.Doc(), "\n") {
		if !strings.HasPrefix(strings.TrimSpace(l), "@") {
			lines = append(lines, l)
		}
	}
	return strings.TrimSpace(strings.Join(lines, "\n"))
}

func tagged(n syntax.Node, tag string) bool {
	t := syntax.TriviaOf(n)
	if t == nil {
		return false
	}
	for _, l := range strings.Split(t.Doc(), "\n") {
		if strings.TrimSpace(l) == "@"+tag {
			return true
		}
	}
	return false
}

func exported(n string) string { return upperCamel(n) }

func (l *aloader) load(files []*syntax.SourceFile) error {
	var ifaces []*syntax.InterfaceDecl
	var views []*syntax.ComponentDecl
	var errEnums []*syntax.EnumDecl
	for _, f := range files {
		for _, d := range f.Decls {
			switch d := d.(type) {
			case *syntax.StructDecl:
				l.structs[d.Name.Name] = d
			case *syntax.EnumDecl:
				l.enums[d.Name.Name] = d
				for _, c := range d.Conforms {
					if nt, ok := c.(*syntax.NamedType); ok && len(nt.Path) == 1 && nt.Path[0].Name == "Error" {
						errEnums = append(errEnums, d)
					}
				}
			case *syntax.InterfaceDecl:
				if d.Native.IsValid() {
					ifaces = append(ifaces, d)
					l.ifaces[d.Name.Name] = true
				}
			case *syntax.ComponentDecl:
				if d.Native.IsValid() {
					views = append(views, d)
				}
			}
		}
	}
	for _, e := range errEnums {
		for _, c := range e.Cases {
			msg := docOf(c)
			if msg == "" {
				msg = c.Name.Name
			}
			l.out.Errors = append(l.out.Errors, &ErrorDecl{Name: "Err" + exported(c.Name.Name), Message: msg, Index: uint32(len(l.out.Errors))})
		}
	}
	for i, d := range ifaces {
		it, err := l.iface(d, uint32(i))
		if err != nil {
			return err
		}
		l.out.Interfaces = append(l.out.Interfaces, it)
	}
	for _, d := range views {
		v, err := l.view(d)
		if err != nil {
			return err
		}
		l.out.Views = append(l.out.Views, v)
	}
	return nil
}

func (l *aloader) iface(d *syntax.InterfaceDecl, index uint32) (*Interface, error) {
	if len(d.TypeParams) > 0 {
		return nil, l.errorf(d, "a native interface isn't generic")
	}
	it := &Interface{Name: d.Name.Name, Index: index, MainThread: tagged(d, "mainThread"), Doc: docOf(d)}
	n := uint32(0)
	for _, m := range d.Members {
		fd, ok := m.(*syntax.FuncDecl)
		if !ok {
			return nil, l.errorf(m, "a native interface has methods only")
		}
		n++
		meth := &Method{Name: exported(fd.Name.Name), Index: n, Context: fd.Async.IsValid(), Error: true,
			MainThread: it.MainThread || tagged(fd, "mainThread"), Doc: docOf(fd)}
		if fd.Name.Name == "close" && (fd.Params == nil || len(fd.Params.List) == 0) && fd.Result == nil {
			meth.Close = true
		}
		if fd.Params != nil {
			for _, p := range fd.Params.List {
				t, err := l.typ(p.Type, true)
				if err != nil {
					return nil, err
				}
				meth.Params = append(meth.Params, &Param{Name: p.Name.Name, Type: t})
			}
		}
		if fd.Result != nil {
			t, err := l.typ(fd.Result, false)
			if err != nil {
				return nil, err
			}
			meth.Result = t
		}
		it.Methods = append(it.Methods, meth)
	}
	return it, nil
}

func (l *aloader) view(d *syntax.ComponentDecl) (*View, error) {
	v := &View{Name: d.Name.Name, Style: true, Doc: docOf(d)}
	if d.Params != nil {
		for _, p := range d.Params.List {
			t, err := l.typ(p.Type, false)
			if err != nil {
				return nil, err
			}
			v.Props = append(v.Props, &Field{Name: exported(p.Name.Name), Type: t, Accessor: true})
		}
	}
	if d.Body != nil {
		for _, it := range d.Body.Items {
			switch it := it.(type) {
			case *syntax.PropDecl:
				if it.Type == nil {
					return nil, l.errorf(it, "a native component's prop says its type")
				}
				t, err := l.typ(it.Type, false)
				if err != nil {
					return nil, err
				}
				v.Props = append(v.Props, &Field{Name: exported(it.Name.Name), Type: t, Accessor: true, Doc: docOf(it)})
			case *syntax.EventDecl:
				ft := &FuncType{}
				if it.Params != nil {
					for _, p := range it.Params.List {
						t, err := l.typ(p.Type, false)
						if err != nil {
							return nil, err
						}
						ft.Params = append(ft.Params, &Param{Name: p.Name.Name, Type: t})
					}
				}
				v.Events = append(v.Events, &Field{Name: exported(it.Name.Name), Type: &Type{Kind: Func, Func: ft}, Doc: docOf(it)})
			default:
				return nil, l.errorf(it, "a native component declares props and events only")
			}
		}
	}
	return v, nil
}

// typ is an Asuka type's on the wire; param says it's a parameter (a
// callback may be one).
func (l *aloader) typ(t syntax.Type, param bool) (*Type, error) {
	switch t := t.(type) {
	case *syntax.OptionalType:
		e, err := l.typ(t.Elem, false)
		if err != nil {
			return nil, err
		}
		return &Type{Kind: Optional, Elem: e}, nil
	case *syntax.ArrayType:
		e, err := l.typ(t.Elem, false)
		if err != nil {
			return nil, err
		}
		if e.Kind == Object || e.Kind == Func {
			return nil, l.errorf(t, "an object or a function isn't in an array")
		}
		return &Type{Kind: Slice, Elem: e}, nil
	case *syntax.MapType:
		k, ok := t.Key.(*syntax.NamedType)
		if !ok || len(k.Path) != 1 || k.Path[0].Name != "string" {
			return nil, l.errorf(t, "a map's keys are strings")
		}
		e, err := l.typ(t.Value, false)
		if err != nil {
			return nil, err
		}
		return &Type{Kind: Map, Elem: e}, nil
	case *syntax.FuncType:
		if !param {
			return nil, l.errorf(t, "a function crosses as a parameter only")
		}
		ft := &FuncType{}
		for i, p := range t.Params {
			pt, err := l.typ(p, false)
			if err != nil {
				return nil, err
			}
			ft.Params = append(ft.Params, &Param{Name: fmt.Sprintf("p%d", i), Type: pt})
		}
		if t.Result != nil {
			r, err := l.typ(t.Result, false)
			if err != nil {
				return nil, err
			}
			ft.Result = r
		}
		return &Type{Kind: Func, Func: ft}, nil
	case *syntax.NamedType:
		if len(t.Path) != 1 || len(t.Args) > 0 {
			return nil, l.errorf(t, "a native declaration uses the package's own types and the built-in ones")
		}
		n := t.Path[0].Name
		switch n {
		case "int":
			return &Type{Kind: Int, Go: "int"}, nil
		case "float":
			return &Type{Kind: Float64, Go: "float64"}, nil
		case "bool":
			return &Type{Kind: Bool, Go: "bool"}, nil
		case "string":
			return &Type{Kind: String, Go: "string"}, nil
		case "bytes":
			return &Type{Kind: Bytes, Go: "[]byte"}, nil
		case "Duration":
			return &Type{Kind: Duration, Go: "time.Duration"}, nil
		}
		if l.ifaces[n] {
			return &Type{Kind: Object, Name: n, Go: n}, nil
		}
		if sd, ok := l.structs[n]; ok {
			if _, err := l.structDecl(sd); err != nil {
				return nil, err
			}
			return &Type{Kind: Struct, Name: n, Go: n}, nil
		}
		if ed, ok := l.enums[n]; ok {
			if _, err := l.enumDecl(ed); err != nil {
				return nil, err
			}
			return &Type{Kind: Enum, Name: n, Go: n}, nil
		}
		return nil, l.errorf(t, "%s can't cross to the platform's side", n)
	}
	return nil, l.errorf(t, "this type can't cross to the platform's side")
}

func (l *aloader) structDecl(d *syntax.StructDecl) (*StructDecl, error) {
	if s, ok := l.doneS[d.Name.Name]; ok {
		return s, nil
	}
	s := &StructDecl{Name: d.Name.Name, Doc: docOf(d)}
	l.doneS[d.Name.Name] = s
	for _, m := range d.Members {
		f, ok := m.(*syntax.Field)
		if !ok {
			continue // constants and methods stay on Asuka's side
		}
		t, err := l.typ(f.Type, false)
		if err != nil {
			return nil, err
		}
		s.Fields = append(s.Fields, &Field{Name: exported(f.Name.Name), Type: t, Doc: docOf(f)})
	}
	l.out.Structs = append(l.out.Structs, s)
	return s, nil
}

func (l *aloader) enumDecl(d *syntax.EnumDecl) (*EnumDecl, error) {
	if e, ok := l.doneE[d.Name.Name]; ok {
		return e, nil
	}
	e := &EnumDecl{Name: d.Name.Name, Basic: Int, Doc: docOf(d)}
	for i, c := range d.Cases {
		if c.Params != nil {
			return nil, l.errorf(c, "an enum that crosses has cases without payloads")
		}
		e.Values = append(e.Values, EnumValue{Name: exported(d.Name.Name) + exported(c.Name.Name), Value: fmt.Sprint(i)})
	}
	l.doneE[d.Name.Name] = e
	l.out.Enums = append(l.out.Enums, e)
	return e, nil
}


// LoadAny reads the native declarations of the package in dir, in Asuka
// when it has .asuka files, else in Go.
func LoadAny(dir string) (*Package, error) {
	if m, _ := filepath.Glob(filepath.Join(dir, "*.asuka")); len(m) > 0 {
		return LoadAsuka(dir)
	}
	return Load(dir)
}
